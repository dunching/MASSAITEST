#include "MassCrowdBoundaryOrchestrator.h"

#include "HAL/PlatformTime.h"
#include "Tasks/Task.h"

#define FnvOffset64 BoundaryOrchestrator_FnvOffset64
#define FnvPrime64 BoundaryOrchestrator_FnvPrime64
#define FoldBytes BoundaryOrchestrator_FoldBytes
#define FoldUint32 BoundaryOrchestrator_FoldUint32
#define FoldUint64 BoundaryOrchestrator_FoldUint64

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
  struct FTelemetry
  {
    double EnqueueSeconds = 0.0;
    double StartSeconds = 0.0;
    double FinishSeconds = 0.0;
  };

  FCrowdBoundaryTaskDescriptor Descriptor;
  FCrowdBoundaryTaskKey Key;
  TArray<FCrowdBoundaryTaskKey> Prerequisites;
  FCrowdBoundaryTaskBody Body;
  TSharedRef<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe> Result =
    MakeShared<FCrowdBoundaryTaskResult, ESPMode::ThreadSafe>();
  TSharedRef<FTelemetry, ESPMode::ThreadSafe> Telemetry =
    MakeShared<FTelemetry, ESPMode::ThreadSafe>();
  UE::Tasks::FTask Task;
  bool bRequireOffGameThread = true;
};

bool FCrowdBoundaryTaskDescriptor::IsValid() const
{
  if (!Key.IsValid() || !Output.IsValid() || TelemetryId == 0)
    return false;
  for (int32 Index = 0; Index < Prerequisites.Num(); ++Index)
  {
    if (!Prerequisites[Index].IsValid()
      || Prerequisites[Index] == Key
      || (Index > 0
        && !(Prerequisites[Index - 1] < Prerequisites[Index])))
      return false;
  }
  FCrowdBoundaryResourceId Previous;
  for (const FCrowdBoundaryInputResource& Input : Inputs)
  {
    if (!Input.IsValid()
      || (Previous.IsValid() && !(Previous < Input.ResourceId)))
      return false;
    Previous = Input.ResourceId;
  }
  return true;
}

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

bool FCrowdBoundaryPatchTransaction::AddAdapter(
  const ICrowdBoundaryCommitAdapter& Adapter)
{
  if (bPrepared || bValidated || bApplied)
    return false;
  FEntry& Entry = Entries.AddDefaulted_GetRef();
  Entry.Adapter = &Adapter;
  return true;
}

bool FCrowdBoundaryPatchTransaction::PrepareAll(
  const FCrowdMassBoundarySnapshot& Snapshot)
{
  if (bPrepared || bValidated || bApplied
    || !Snapshot.bValid || Entries.IsEmpty())
    return false;
  for (FEntry& Entry : Entries)
  {
    Entry.Patch = {};
    if (!Entry.Adapter
      || !Entry.Adapter->Prepare(Snapshot, Entry.Patch)
      || !Entry.Patch.bValid
      || !Entry.Patch.ApplyPhase.IsValid()
      || !Entry.Patch.AdapterId.IsValid()
      || !Entry.Patch.PatchKey.IsValid()
      || Entry.Patch.FixedStepIndex != Snapshot.FixedStepIndex
      || Entry.Patch.PlanRevision != Snapshot.PlanRevision
      || Entry.Patch.StableHash == 0)
    {
      Entries.Reset();
      PreparedPatches.Reset();
      return false;
    }
  }
  Entries.Sort([](const FEntry& A, const FEntry& B)
  {
    if (A.Patch.ApplyPhase != B.Patch.ApplyPhase)
      return A.Patch.ApplyPhase < B.Patch.ApplyPhase;
    if (A.Patch.AdapterId != B.Patch.AdapterId)
      return A.Patch.AdapterId < B.Patch.AdapterId;
    return A.Patch.PatchKey < B.Patch.PatchKey;
  });
  PreparedPatches.Reserve(Entries.Num());
  for (int32 Index = 0; Index < Entries.Num(); ++Index)
  {
    if (Index > 0)
    {
      const FCrowdBoundaryPreparedPatch& Previous =
        Entries[Index - 1].Patch;
      const FCrowdBoundaryPreparedPatch& Current =
        Entries[Index].Patch;
      if (Previous.ApplyPhase == Current.ApplyPhase
        && Previous.AdapterId == Current.AdapterId
        && Previous.PatchKey == Current.PatchKey)
      {
        Entries.Reset();
        PreparedPatches.Reset();
        return false;
      }
    }
    PreparedPatches.Add(Entries[Index].Patch);
  }
  bPrepared = true;
  return true;
}

bool FCrowdBoundaryPatchTransaction::ValidateAll(
  const TConstArrayView<FCrowdMassCommitTarget> Targets)
{
  if (!bPrepared || bValidated || bApplied)
    return false;
  for (const FEntry& Entry : Entries)
  {
    if (!Entry.Adapter
      || !Entry.Adapter->ValidatePrepared(Entry.Patch, Targets))
      return false;
  }
  bValidated = true;
  return true;
}

void FCrowdBoundaryPatchTransaction::ApplyAll(
  FCrowdBoundaryApplyContext& Context)
{
  check(IsInGameThread());
  check(bPrepared && bValidated && !bApplied);
  for (const FEntry& Entry : Entries)
  {
    check(Entry.Adapter);
    Entry.Adapter->ApplyPrepared(Entry.Patch, Context);
  }
  bApplied = true;
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
  FCrowdBoundaryTaskDescriptor Descriptor;
  Descriptor.Key = Key;
  Descriptor.Prerequisites =
    TArray<FCrowdBoundaryTaskKey>(Prerequisites);
  Descriptor.Prerequisites.Sort();
  Descriptor.Output.ResourceId = {
    Key.TaskTypeId.Value != 0 ? Key.TaskTypeId.Value : 1};
  Descriptor.Output.Revision = 1;
  Descriptor.Output.SchemaId = 1;
  Descriptor.Output.Capacity = 1;
  Descriptor.Output.StableHash = 1;
  Descriptor.TelemetryId =
    Key.TaskTypeId.Value != 0 ? Key.TaskTypeId.Value : 1;
  Descriptor.bRequireOffGameThread = bRequireOffGameThread;
  return AddTask(MoveTemp(Descriptor), MoveTemp(Body));
}

bool FCrowdMassBoundaryOrchestrator::AddTask(
  FCrowdBoundaryTaskDescriptor Descriptor,
  FCrowdBoundaryTaskBody&& Body)
{
  Descriptor.Prerequisites.Sort();
  Descriptor.Inputs.Sort([](
    const FCrowdBoundaryInputResource& A,
    const FCrowdBoundaryInputResource& B)
  {
    return A.ResourceId < B.ResourceId;
  });
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Gathering
    || !Body
    || !Descriptor.IsValid()
    || FindNode(Descriptor.Key))
    return false;
  TUniquePtr<FTaskNode> Node = MakeUnique<FTaskNode>();
  Node->Descriptor = MoveTemp(Descriptor);
  Node->Key = Node->Descriptor.Key;
  Node->Prerequisites = Node->Descriptor.Prerequisites;
  Node->Body = MoveTemp(Body);
  Node->bRequireOffGameThread =
    Node->Descriptor.bRequireOffGameThread;
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
    for (const FCrowdBoundaryInputResource& Input
      : Node->Descriptor.Inputs)
    {
      bool bMatched = false;
      for (const FCrowdBoundaryTaskKey& Prerequisite
        : Node->Prerequisites)
      {
        const FTaskNode* Producer = FindNode(Prerequisite);
        if (!Producer
          || Producer->Descriptor.Output.ResourceId != Input.ResourceId)
          continue;
        bMatched = Producer->Descriptor.Output.Revision == Input.Revision
          && Producer->Descriptor.Output.SchemaId == Input.SchemaId
          && Producer->Descriptor.Output.StableHash == Input.StableHash;
        break;
      }
      if (!bMatched) return false;
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
      const TSharedRef<FTaskNode::FTelemetry, ESPMode::ThreadSafe>
        Telemetry = Node->Telemetry;
      Telemetry->EnqueueSeconds = FPlatformTime::Seconds();
      Node->Task = UE::Tasks::Launch(
        TEXT("MassCrowdBoundaryWork"),
        [Body = MoveTemp(Body), Result, Telemetry,
          PrerequisiteResults = MoveTemp(PrerequisiteResults)]() mutable
        {
          Telemetry->StartSeconds = FPlatformTime::Seconds();
          for (const auto& PrerequisiteResult : PrerequisiteResults)
          {
            if (!PrerequisiteResult->bSucceeded)
            {
              *Result = FCrowdBoundaryTaskResult::Failure();
              Result->bRanOffGameThread = !IsInGameThread();
              Telemetry->FinishSeconds = FPlatformTime::Seconds();
              return;
            }
          }
          *Result = Body();
          Result->bRanOffGameThread = !IsInGameThread();
          Telemetry->FinishSeconds = FPlatformTime::Seconds();
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
  CompletionTask = UE::Tasks::Launch(
    TEXT("MassCrowdBoundaryCompletion"),
    [] {},
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

ECrowdBoundaryPollResult
FCrowdMassBoundaryOrchestrator::PollAndDrain()
{
  if (!IsInGameThread()
    || State != ECrowdBoundaryTransactionState::Working)
  {
    Fail();
    return ECrowdBoundaryPollResult::Failed;
  }
  if (!CompletionTask.IsValid() || !CompletionTask.IsCompleted())
    return ECrowdBoundaryPollResult::Pending;

  const double WorkEnd = FPlatformTime::Seconds();
  Timings.WaitMilliseconds = 0.0;
  Timings.WorkMilliseconds = (WorkEnd - WorkStartSeconds) * 1000.0;
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
  {
    if (!Node->Result->bSucceeded
      || (Node->bRequireOffGameThread
        && !Node->Result->bRanOffGameThread))
    {
      Fail();
      return ECrowdBoundaryPollResult::Failed;
    }
  }
  State = ECrowdBoundaryTransactionState::Merging;
  return ECrowdBoundaryPollResult::Ready;
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
      || !Patch.ApplyPhase.IsValid()
      || !Patch.AdapterId.IsValid()
      || !Patch.PatchKey.IsValid()
      || Patch.FixedStepIndex != Snapshot.FixedStepIndex
      || Patch.PlanRevision != Snapshot.PlanRevision
      || Patch.StableHash == 0
      || !Patch.Payload.IsValid()
      || !IsStrictlySortedUnique(Patch.EntityRefs)
      || Patch.EntityRefs != MovementRefs)
      return false;
    OutEnvelope.Patches.Add({
      Patch.ApplyPhase, Patch.AdapterId, Patch.PatchKey,
      Patch.StableHash});
  }
  OutEnvelope.Patches.Sort([](
    const FCrowdBoundaryPatchDescriptor& A,
    const FCrowdBoundaryPatchDescriptor& B)
  {
    if (A.ApplyPhase != B.ApplyPhase)
      return A.ApplyPhase < B.ApplyPhase;
    if (A.AdapterId != B.AdapterId)
      return A.AdapterId < B.AdapterId;
    return A.PatchKey < B.PatchKey;
  });
  for (int32 Index = 1; Index < OutEnvelope.Patches.Num(); ++Index)
  {
    if (OutEnvelope.Patches[Index - 1].ApplyPhase
        == OutEnvelope.Patches[Index].ApplyPhase
      && OutEnvelope.Patches[Index - 1].AdapterId
        == OutEnvelope.Patches[Index].AdapterId
      && OutEnvelope.Patches[Index - 1].PatchKey
        == OutEnvelope.Patches[Index].PatchKey)
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
    Hash = FoldUint32(Hash, Patch.ApplyPhase.Value);
    Hash = FoldUint32(Hash, Patch.AdapterId.Value);
    Hash = FoldUint64(Hash, Patch.PatchKey.Value);
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
  {
    FCrowdBoundaryTaskTimings TaskTimings;
    if (Node->Telemetry->StartSeconds > 0.0)
    {
      TaskTimings.QueueMilliseconds =
        (Node->Telemetry->StartSeconds
          - Node->Telemetry->EnqueueSeconds) * 1000.0;
    }
    if (Node->Telemetry->FinishSeconds > 0.0)
    {
      TaskTimings.ExecutionMilliseconds =
        (Node->Telemetry->FinishSeconds
          - Node->Telemetry->StartSeconds) * 1000.0;
      TaskTimings.EndToEndMilliseconds =
        (Node->Telemetry->FinishSeconds
          - Node->Telemetry->EnqueueSeconds) * 1000.0;
    }
    Result.Tasks.Add({
      Node->Key, Node->Descriptor.TelemetryId, *Node->Result,
      TaskTimings});
  }
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

#undef FoldUint64
#undef FoldUint32
#undef FoldBytes
#undef FnvPrime64
#undef FnvOffset64

const FCrowdMassBoundaryOrchestrator::FTaskNode*
FCrowdMassBoundaryOrchestrator::FindNode(
  const FCrowdBoundaryTaskKey& Key) const
{
  for (const TUniquePtr<FTaskNode>& Node : Nodes)
    if (Node->Key == Key) return Node.Get();
  return nullptr;
}
