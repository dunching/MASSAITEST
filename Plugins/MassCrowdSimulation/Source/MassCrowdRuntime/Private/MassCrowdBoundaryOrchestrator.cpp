#include "MassCrowdBoundaryOrchestrator.h"

#include "HAL/PlatformTime.h"
#include "Tasks/Task.h"

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  uint64 FoldBytes(
    uint64 Hash,
    const uint8* Bytes,
    const int32 NumBytes)
  {
    for (int32 Index = 0; Index < NumBytes; ++Index)
    {
      Hash ^= Bytes[Index];
      Hash *= FnvPrime64;
    }
    return Hash;
  }

  uint64 FoldUint32(uint64 Hash, const uint32 Value)
  {
    const uint8 Bytes[] = {
      static_cast<uint8>(Value),
      static_cast<uint8>(Value >> 8),
      static_cast<uint8>(Value >> 16),
      static_cast<uint8>(Value >> 24)};
    return FoldBytes(Hash, Bytes, UE_ARRAY_COUNT(Bytes));
  }

  uint64 FoldUint64(uint64 Hash, const uint64 Value)
  {
    const uint8 Bytes[] = {
      static_cast<uint8>(Value),
      static_cast<uint8>(Value >> 8),
      static_cast<uint8>(Value >> 16),
      static_cast<uint8>(Value >> 24),
      static_cast<uint8>(Value >> 32),
      static_cast<uint8>(Value >> 40),
      static_cast<uint8>(Value >> 48),
      static_cast<uint8>(Value >> 56)};
    return FoldBytes(Hash, Bytes, UE_ARRAY_COUNT(Bytes));
  }

  uint64 FoldName(uint64 Hash, const FName Name)
  {
    const FTCHARToUTF8 Utf8(*Name.ToString());
    Hash = FoldUint32(Hash, static_cast<uint32>(Utf8.Length()));
    return FoldBytes(
      Hash,
      reinterpret_cast<const uint8*>(Utf8.Get()),
      Utf8.Length());
  }

  bool IsStrictlySortedUnique(
    const TConstArrayView<FCrowdStableEntityRef> Refs)
  {
    if (Refs.IsEmpty()) return false;
    for (int32 Index = 0; Index < Refs.Num(); ++Index)
    {
      if (!Refs[Index].IsValid()) return false;
      if (Index > 0 && !(Refs[Index - 1] < Refs[Index])) return false;
    }
    return true;
  }
}

struct FCrowdMassBoundaryOrchestrator::FTaskNode
{
  FCrowdBoundaryTaskKey Key;
  TArray<FCrowdBoundaryTaskKey> Prerequisites;
  FCrowdBoundaryTaskBody Body;
  TSharedRef<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe> Result =
    MakeShared<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe>();
  UE::Tasks::FTask Task;
  bool bRequireOffGameThread = true;
};

FCrowdBoundaryTaskResult FCrowdBoundaryTaskResult::Success(
  const uint64 StableHash)
{
  FCrowdBoundaryTaskResult Result;
  Result.StableHash = StableHash;
  Result.bSucceeded = StableHash != 0;
  return Result;
}

FCrowdBoundaryTaskResult FCrowdBoundaryTaskResult::Failure()
{
  return {};
}

FCrowdMassBoundaryOrchestrator::FCrowdMassBoundaryOrchestrator() = default;
FCrowdMassBoundaryOrchestrator::~FCrowdMassBoundaryOrchestrator() = default;

bool FCrowdMassBoundaryOrchestrator::Begin(
  const FCrowdMassBoundarySnapshot& InSnapshot,
  const double GatherMilliseconds)
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Idle
    || !InSnapshot.bValid
    || InSnapshot.FixedStepIndex < 0
    || InSnapshot.PlanRevision < 0
    || InSnapshot.Agents.IsEmpty())
    return false;
  Snapshot = InSnapshot;
  Timings = {};
  Timings.GatherMilliseconds = FMath::Max(0.0, GatherMilliseconds);
  CommitPlanHash = 0;
  Nodes.Reset();
  CompletionEvent->Reset();
  CompletionTask = {};
  State = ECrowdBoundaryTransactionState::Gathering;
  return true;
}

bool FCrowdMassBoundaryOrchestrator::AddTask(
  const FCrowdBoundaryTaskKey Key,
  const TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites,
  FCrowdBoundaryTaskBody&& Body,
  const bool bRequireOffGameThread)
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Gathering
    || !Body
    || FindNode(Key))
    return false;
  TUniquePtr<FTaskNode> Node = MakeUnique<FTaskNode>();
  Node->Key = Key;
  Node->Prerequisites = TArray<FCrowdBoundaryTaskKey>(Prerequisites);
  Node->Prerequisites.Sort();
  for (int32 Index = 0; Index < Node->Prerequisites.Num(); ++Index)
  {
    if (Node->Prerequisites[Index] == Key
      || (Index > 0
        && Node->Prerequisites[Index] == Node->Prerequisites[Index - 1]))
      return false;
  }
  Node->Body = MoveTemp(Body);
  Node->bRequireOffGameThread = bRequireOffGameThread;
  Nodes.Add(MoveTemp(Node));
  return true;
}

bool FCrowdMassBoundaryOrchestrator::ValidateTaskGraph() const
{
  if (Nodes.IsEmpty()) return false;
  TMap<FCrowdBoundaryTaskKey, int32> InDegree;
  TMap<FCrowdBoundaryTaskKey, TArray<FCrowdBoundaryTaskKey>> Dependents;
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
  {
    InDegree.Add(Node->Key, Node->Prerequisites.Num());
    for (const FCrowdBoundaryTaskKey& Prerequisite : Node->Prerequisites)
    {
      if (!FindNode(Prerequisite)) return false;
      Dependents.FindOrAdd(Prerequisite).Add(Node->Key);
    }
  }
  TArray<FCrowdBoundaryTaskKey> Ready;
  for (const TPair<FCrowdBoundaryTaskKey, int32>& Pair : InDegree)
    if (Pair.Value == 0) Ready.Add(Pair.Key);
  int32 Visited = 0;
  while (!Ready.IsEmpty())
  {
    Ready.Sort();
    const FCrowdBoundaryTaskKey Key = Ready[0];
    Ready.RemoveAt(0, EAllowShrinking::No);
    ++Visited;
    for (const FCrowdBoundaryTaskKey& Dependent
      : Dependents.FindRef(Key))
    {
      int32* Degree = InDegree.Find(Dependent);
      if (!Degree) return false;
      if (--(*Degree) == 0) Ready.Add(Dependent);
    }
  }
  return Visited == Nodes.Num();
}

bool FCrowdMassBoundaryOrchestrator::Dispatch()
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Gathering
    || !ValidateTaskGraph())
  {
    Fail();
    return false;
  }
  const double QueueStart = FPlatformTime::Seconds();
  State = ECrowdBoundaryTransactionState::Queued;
  Nodes.Sort([](const TUniquePtr<FTaskNode>& A,
    const TUniquePtr<FTaskNode>& B)
  {
    return A->Key < B->Key;
  });

  TSet<FCrowdBoundaryTaskKey> Launched;
  while (Launched.Num() < Nodes.Num())
  {
    bool bMadeProgress = false;
    for (const TUniquePtr<FTaskNode>& Node : Nodes)
    {
      if (Launched.Contains(Node->Key)) continue;
      bool bPrerequisitesLaunched = true;
      for (const FCrowdBoundaryTaskKey& Key : Node->Prerequisites)
        bPrerequisitesLaunched &= Launched.Contains(Key);
      if (!bPrerequisitesLaunched) continue;

      TArray<UE::Tasks::FTask> PrerequisiteTasks;
      TArray<TSharedRef<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe>>
        PrerequisiteResults;
      for (const FCrowdBoundaryTaskKey& Key : Node->Prerequisites)
      {
        const FTaskNode* Prerequisite = FindNode(Key);
        PrerequisiteTasks.Add(Prerequisite->Task);
        PrerequisiteResults.Add(Prerequisite->Result);
      }
      FCrowdBoundaryTaskBody Body = MoveTemp(Node->Body);
      const TSharedRef<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe>
        Result = Node->Result;
      Node->Task = UE::Tasks::Launch(
        TEXT("MassCrowdBoundaryWork"),
        [Body = MoveTemp(Body), Result,
          PrerequisiteResults = MoveTemp(PrerequisiteResults)]() mutable
        {
          for (const auto& PrerequisiteResult : PrerequisiteResults)
          {
            if (!PrerequisiteResult->bSucceeded)
            {
              *Result = FCrowdBoundaryTaskResult::Failure();
              Result->bRanOffGameThread = !IsInGameThread();
              return;
            }
          }
          *Result = Body();
          Result->bRanOffGameThread = !IsInGameThread();
        },
        UE::Tasks::Prerequisites(PrerequisiteTasks),
        UE::Tasks::ETaskPriority::Normal,
        UE::Tasks::EExtendedTaskPriority::None,
        UE::Tasks::ETaskFlags::DoNotRunInsideBusyWait);
      Launched.Add(Node->Key);
      bMadeProgress = true;
    }
    if (!bMadeProgress)
    {
      Fail();
      return false;
    }
  }
  TArray<UE::Tasks::FTask> AllTasks;
  AllTasks.Reserve(Nodes.Num());
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
    AllTasks.Add(Node->Task);
  FEvent* const Completion = CompletionEvent.Get();
  CompletionTask = UE::Tasks::Launch(
    TEXT("MassCrowdBoundaryCompletion"),
    [Completion] { Completion->Trigger(); },
    UE::Tasks::Prerequisites(AllTasks),
    UE::Tasks::ETaskPriority::Normal,
    UE::Tasks::EExtendedTaskPriority::None,
    UE::Tasks::ETaskFlags::DoNotRunInsideBusyWait);
  Timings.QueueMilliseconds =
    (FPlatformTime::Seconds() - QueueStart) * 1000.0;
  WorkStartSeconds = FPlatformTime::Seconds();
  State = ECrowdBoundaryTransactionState::Working;
  return true;
}

bool FCrowdMassBoundaryOrchestrator::WaitAndDrain()
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Working)
  {
    Fail();
    return false;
  }
  const double WaitStart = FPlatformTime::Seconds();
  CompletionEvent->Wait();
  const double WorkEnd = FPlatformTime::Seconds();
  Timings.WaitMilliseconds = (WorkEnd - WaitStart) * 1000.0;
  Timings.WorkMilliseconds = (WorkEnd - WorkStartSeconds) * 1000.0;
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
  {
    if (!Node->Result->bSucceeded
      || (Node->bRequireOffGameThread
        && !Node->Result->bRanOffGameThread))
    {
      Fail();
      return false;
    }
  }
  State = ECrowdBoundaryTransactionState::Merging;
  return true;
}

bool FCrowdMassBoundaryOrchestrator::SealMergedPlan(
  const FCrowdMassCommitPlan& CommitPlan,
  const double MergeMilliseconds)
{
  FCrowdBoundaryCommitEnvelope Envelope;
  if (!BuildCommitEnvelope(Snapshot, CommitPlan, {}, Envelope))
  {
    Fail();
    return false;
  }
  return SealMergedEnvelope(Envelope, MergeMilliseconds);
}

bool FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
  const FCrowdMassBoundarySnapshot& Snapshot,
  const FCrowdMassCommitPlan& MovementPlan,
  const TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
  FCrowdBoundaryCommitEnvelope& OutEnvelope,
  const FCrowdBehaviorBoundaryMetadata* BehaviorMetadata)
{
  OutEnvelope = {};
  if (!Snapshot.bValid
    || !MovementPlan.bValid
    || MovementPlan.FixedStepIndex != Snapshot.FixedStepIndex
    || MovementPlan.PlanRevision != Snapshot.PlanRevision
    || MovementPlan.Records.Num() != Snapshot.Agents.Num()
    || MovementPlan.StableHash == 0)
    return false;

  TArray<FCrowdStableEntityRef> MovementRefs;
  MovementRefs.Reserve(MovementPlan.Records.Num());
  for (const FCrowdMassCommitRecord& Record : MovementPlan.Records)
    MovementRefs.Add(Record.EntityRef);
  if (!IsStrictlySortedUnique(MovementRefs))
    return false;

  OutEnvelope.MovementPlan = MovementPlan;
  if (BehaviorMetadata)
  {
    if (!BehaviorMetadata->IsValid()) return false;
    OutEnvelope.Behavior = *BehaviorMetadata;
  }
  OutEnvelope.Patches.Reserve(PreparedPatches.Num());
  for (const FCrowdBoundaryPreparedPatch& Patch : PreparedPatches)
  {
    if (!Patch.bValid
      || Patch.AdapterId.IsNone()
      || Patch.FixedStepIndex != Snapshot.FixedStepIndex
      || Patch.PlanRevision != Snapshot.PlanRevision
      || Patch.StableHash == 0
      || !Patch.Payload.IsValid()
      || !IsStrictlySortedUnique(Patch.EntityRefs)
      || Patch.EntityRefs != MovementRefs)
      return false;
    OutEnvelope.Patches.Add({Patch.AdapterId, Patch.StableHash});
  }
  OutEnvelope.Patches.Sort([](
    const FCrowdBoundaryPatchDescriptor& A,
    const FCrowdBoundaryPatchDescriptor& B)
  {
    return A.AdapterId.LexicalLess(B.AdapterId);
  });
  for (int32 Index = 1; Index < OutEnvelope.Patches.Num(); ++Index)
  {
    if (OutEnvelope.Patches[Index - 1].AdapterId
      == OutEnvelope.Patches[Index].AdapterId)
      return false;
  }

  uint64 Hash = FoldUint32(FnvOffset64, OutEnvelope.Version);
  Hash = FoldUint64(Hash, Snapshot.StableHash);
  Hash = FoldUint64(Hash, MovementPlan.StableHash);
  Hash = FoldUint32(Hash, OutEnvelope.Behavior.SourceSetRevision);
  Hash = FoldUint64(Hash, OutEnvelope.Behavior.SourceSetHash);
  Hash = FoldUint64(Hash, OutEnvelope.Behavior.CommandBatchHash);
  Hash = FoldUint64(Hash, OutEnvelope.Behavior.ResolvedChannelHash);
  Hash = FoldUint32(Hash, static_cast<uint32>(OutEnvelope.Patches.Num()));
  for (const FCrowdBoundaryPatchDescriptor& Patch : OutEnvelope.Patches)
  {
    Hash = FoldName(Hash, Patch.AdapterId);
    Hash = FoldUint64(Hash, Patch.StableHash);
  }
  OutEnvelope.StableHash = Hash;
  OutEnvelope.bValid = Hash != 0;
  return OutEnvelope.bValid;
}

bool FCrowdMassBoundaryOrchestrator::SealMergedEnvelope(
  const FCrowdBoundaryCommitEnvelope& Envelope,
  const double MergeMilliseconds)
{
  const double ValidateStartSeconds = FPlatformTime::Seconds();
  const FCrowdMassCommitPlan& CommitPlan = Envelope.MovementPlan;
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Merging
    || !Envelope.bValid
    || Envelope.Version != FCrowdBoundaryCommitEnvelope::CurrentVersion
    || Envelope.StableHash == 0
    || !CommitPlan.bValid
    || CommitPlan.FixedStepIndex != Snapshot.FixedStepIndex
    || CommitPlan.PlanRevision != Snapshot.PlanRevision
    || CommitPlan.Records.Num() != Snapshot.Agents.Num()
    || CommitPlan.StableHash == 0)
  {
    Fail();
    return false;
  }
  TArray<FCrowdStableEntityRef> Refs;
  Refs.Reserve(CommitPlan.Records.Num());
  for (const FCrowdMassCommitRecord& Record : CommitPlan.Records)
    Refs.Add(Record.EntityRef);
  if (!IsStrictlySortedUnique(Refs))
  {
    Fail();
    return false;
  }
  Timings.MergeMilliseconds = FMath::Max(0.0, MergeMilliseconds);
  Timings.ValidateMilliseconds =
    (FPlatformTime::Seconds() - ValidateStartSeconds) * 1000.0;
  CommitPlanHash = Envelope.StableHash;
  State = ECrowdBoundaryTransactionState::Validating;
  return true;
}

bool FCrowdMassBoundaryOrchestrator::MarkValidated(
  const double ValidateMilliseconds)
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Validating)
  {
    Fail();
    return false;
  }
  Timings.ValidateMilliseconds += FMath::Max(0.0, ValidateMilliseconds);
  State = ECrowdBoundaryTransactionState::ReadyToCommit;
  return true;
}

bool FCrowdMassBoundaryOrchestrator::MarkCommitted(
  const double CommitMilliseconds)
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::ReadyToCommit)
  {
    Fail();
    return false;
  }
  Timings.CommitMilliseconds = FMath::Max(0.0, CommitMilliseconds);
  State = ECrowdBoundaryTransactionState::Committed;
  return true;
}

void FCrowdMassBoundaryOrchestrator::Fail()
{
  if (State != ECrowdBoundaryTransactionState::Committed)
    State = ECrowdBoundaryTransactionState::Failed;
}

FCrowdBoundaryOrchestratorResult
FCrowdMassBoundaryOrchestrator::BuildResult() const
{
  FCrowdBoundaryOrchestratorResult Result;
  Result.State = State;
  Result.Timings = Timings;
  Result.SnapshotHash = Snapshot.StableHash;
  Result.CommitPlanHash = CommitPlanHash;
  Result.bSucceeded =
    State == ECrowdBoundaryTransactionState::Committed;
  Result.Tasks.Reserve(Nodes.Num());
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
    Result.Tasks.Add({Node->Key, *Node->Result});
  Result.Tasks.Sort([](const auto& A, const auto& B)
  {
    return A.Key < B.Key;
  });
  return Result;
}

FCrowdMassBoundaryOrchestrator::FTaskNode*
FCrowdMassBoundaryOrchestrator::FindNode(
  const FCrowdBoundaryTaskKey& Key)
{
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
    if (Node->Key == Key) return Node.Get();
  return nullptr;
}

const FCrowdMassBoundaryOrchestrator::FTaskNode*
FCrowdMassBoundaryOrchestrator::FindNode(
  const FCrowdBoundaryTaskKey& Key) const
{
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
    if (Node->Key == Key) return Node.Get();
  return nullptr;
}
