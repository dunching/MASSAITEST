#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"
#include "Mass/CrowdDemoMassSubsystem.h"

#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoMassCrowdRuntimeAdapter.h"
#include "Mass/CrowdDemoWorkerInputSync.h"
#include "CrowdDemoVatShowcasePlanner.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerConsistencyDomains.h"
#include "MassCrowdWorkerCombatState.h"
#include "MassCrowdWorkerInteractionDomain.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerProjectileDomain.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tasks/Task.h"

// Demo-local scheduler for one immutable Round input batch. It intentionally
// owns no commit envelope and exposes no Runtime boundary transaction API.
class FCrowdDemoRoundWorkBatch
{
public:
  bool Begin(
    const FCrowdMassBoundarySnapshot& Snapshot,
    const double GatherMilliseconds,
    const uint64 Generation)
  {
    if (!IsInGameThread()
      || State != ECrowdBoundaryTransactionState::Idle
      || !Snapshot.bValid || Snapshot.FixedStepIndex < 0
      || Snapshot.PlanRevision < 0 || Snapshot.Agents.IsEmpty()
      || Generation == 0)
      return false;
    FixedStepIndex = Snapshot.FixedStepIndex;
    PlanRevision = Snapshot.PlanRevision;
    SnapshotHash = Snapshot.StableHash;
    BatchGeneration = Generation;
    Timings = {};
    Timings.GatherMilliseconds = FMath::Max(0.0, GatherMilliseconds);
    State = ECrowdBoundaryTransactionState::Gathering;
    return SnapshotHash != 0;
  }

  bool Matches(
    const FCrowdMassBoundarySnapshot& Snapshot,
    const uint64 Generation) const
  {
    return Generation != 0 && Generation == BatchGeneration
      && Snapshot.bValid && Snapshot.FixedStepIndex == FixedStepIndex
      && Snapshot.PlanRevision == PlanRevision
      && Snapshot.StableHash == SnapshotHash;
  }

  bool AddTask(
    const FCrowdBoundaryTaskKey Key,
    const TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites,
    FCrowdBoundaryTaskBody&& Body,
    const bool bRequireOffGameThread = true)
  {
    if (!IsInGameThread()
      || State != ECrowdBoundaryTransactionState::Gathering
      || !Key.IsValid() || !Body || FindNode(Key))
      return false;
    TUniquePtr<FTaskNode> Node = MakeUnique<FTaskNode>();
    Node->Key = Key;
    Node->Prerequisites = TArray<FCrowdBoundaryTaskKey>(Prerequisites);
    Node->Prerequisites.Sort();
    for (int32 Index = 0; Index < Node->Prerequisites.Num(); ++Index)
    {
      if (!Node->Prerequisites[Index].IsValid()
        || Node->Prerequisites[Index] == Key
        || (Index > 0
          && !(Node->Prerequisites[Index - 1]
            < Node->Prerequisites[Index])))
        return false;
    }
    Node->Body = MoveTemp(Body);
    Node->bRequireOffGameThread = bRequireOffGameThread;
    Nodes.Add(MoveTemp(Node));
    return true;
  }

  bool Dispatch()
  {
    if (!IsInGameThread()
      || State != ECrowdBoundaryTransactionState::Gathering
      || !ValidateGraph())
    {
      Fail();
      return false;
    }
    const double QueueStartSeconds = FPlatformTime::Seconds();
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
        for (const FCrowdBoundaryTaskKey& Prerequisite
          : Node->Prerequisites)
          bPrerequisitesLaunched &= Launched.Contains(Prerequisite);
        if (!bPrerequisitesLaunched) continue;

        TArray<UE::Tasks::FTask> PrerequisiteTasks;
        TArray<TSharedRef<FCrowdBoundaryTaskResult,
          ESPMode::ThreadSafe>> PrerequisiteResults;
        for (const FCrowdBoundaryTaskKey& Prerequisite
          : Node->Prerequisites)
        {
          const FTaskNode* PrerequisiteNode = FindNode(Prerequisite);
          if (!PrerequisiteNode)
          {
            Fail();
            return false;
          }
          PrerequisiteTasks.Add(PrerequisiteNode->Task);
          PrerequisiteResults.Add(PrerequisiteNode->Result);
        }
        FCrowdBoundaryTaskBody Body = MoveTemp(Node->Body);
        const auto Result = Node->Result;
        const auto Telemetry = Node->Telemetry;
        Telemetry->EnqueueSeconds = FPlatformTime::Seconds();
        Node->Task = UE::Tasks::Launch(
          TEXT("CrowdDemoRoundWork"),
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
      TEXT("CrowdDemoRoundWorkCompletion"), [] {},
      UE::Tasks::Prerequisites(AllTasks),
      UE::Tasks::ETaskPriority::Normal,
      UE::Tasks::EExtendedTaskPriority::None,
      UE::Tasks::ETaskFlags::DoNotRunInsideBusyWait);
    Timings.QueueMilliseconds =
      (FPlatformTime::Seconds() - QueueStartSeconds) * 1000.0;
    WorkStartSeconds = FPlatformTime::Seconds();
    State = ECrowdBoundaryTransactionState::Working;
    return true;
  }

  ECrowdBoundaryPollResult Poll()
  {
    if (!IsInGameThread()
      || State != ECrowdBoundaryTransactionState::Working)
    {
      Fail();
      return ECrowdBoundaryPollResult::Failed;
    }
    if (!CompletionTask.IsValid() || !CompletionTask.IsCompleted())
      return ECrowdBoundaryPollResult::Pending;
    Timings.WorkMilliseconds =
      (FPlatformTime::Seconds() - WorkStartSeconds) * 1000.0;
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

  bool MarkApplyPlanValidated(
    const uint64 ApplyPlanHash,
    const double MergeMilliseconds,
    const double ValidateMilliseconds)
  {
    if (!IsInGameThread()
      || State != ECrowdBoundaryTransactionState::Merging
      || ApplyPlanHash == 0)
    {
      Fail();
      return false;
    }
    CommitPlanHash = ApplyPlanHash;
    Timings.MergeMilliseconds = FMath::Max(0.0, MergeMilliseconds);
    Timings.ValidateMilliseconds =
      FMath::Max(0.0, ValidateMilliseconds);
    State = ECrowdBoundaryTransactionState::ReadyToCommit;
    return true;
  }

  bool MarkCommitted(const double CommitMilliseconds)
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

  void Fail()
  {
    if (State != ECrowdBoundaryTransactionState::Committed)
      State = ECrowdBoundaryTransactionState::Failed;
  }

  ECrowdBoundaryTransactionState GetState() const { return State; }

  FCrowdBoundaryOrchestratorResult BuildResult() const
  {
    FCrowdBoundaryOrchestratorResult Result;
    Result.State = State;
    Result.Timings = Timings;
    Result.SnapshotHash = SnapshotHash;
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
      Result.Tasks.Add({Node->Key, Node->Key.TaskTypeId.Value,
        *Node->Result, TaskTimings});
    }
    Result.Tasks.Sort([](const auto& A, const auto& B)
    {
      return A.Key < B.Key;
    });
    return Result;
  }

private:
  struct FTaskNode
  {
    struct FTelemetry
    {
      double EnqueueSeconds = 0.0;
      double StartSeconds = 0.0;
      double FinishSeconds = 0.0;
    };
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

  FTaskNode* FindNode(const FCrowdBoundaryTaskKey& Key)
  {
    for (const TUniquePtr<FTaskNode>& Node : Nodes)
      if (Node->Key == Key) return Node.Get();
    return nullptr;
  }

  const FTaskNode* FindNode(const FCrowdBoundaryTaskKey& Key) const
  {
    for (const TUniquePtr<FTaskNode>& Node : Nodes)
      if (Node->Key == Key) return Node.Get();
    return nullptr;
  }

  bool ValidateGraph() const
  {
    if (Nodes.IsEmpty()) return false;
    TMap<FCrowdBoundaryTaskKey, int32> InDegree;
    TMap<FCrowdBoundaryTaskKey, TArray<FCrowdBoundaryTaskKey>> Dependents;
    for (const TUniquePtr<FTaskNode>& Node : Nodes)
    {
      InDegree.Add(Node->Key, Node->Prerequisites.Num());
      for (const FCrowdBoundaryTaskKey& Prerequisite
        : Node->Prerequisites)
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

  TArray<TUniquePtr<FTaskNode>> Nodes;
  UE::Tasks::FTask CompletionTask;
  FCrowdBoundaryPhaseTimings Timings;
  ECrowdBoundaryTransactionState State =
    ECrowdBoundaryTransactionState::Idle;
  uint64 BatchGeneration = 0;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  uint64 SnapshotHash = 0;
  uint64 CommitPlanHash = 0;
  double WorkStartSeconds = 0.0;
};

bool FCrowdDemoRoundWorkGraph::BuildMovementInput(
  const FCrowdDemoRoundWorkGraphInput& Input,
  const FCrowdMassSharedFlowSampleOutput& SharedFlow,
  FCrowdMassMovementPipelineWorkInput& OutMovement)
{
  OutMovement = {};
  if (!SharedFlow.bValid
    || SharedFlow.FixedStepIndex != Input.Movement.Guidance.FixedStepIndex
    || SharedFlow.PlanRevision != Input.Movement.Guidance.PlanRevision
    || SharedFlow.Agents.Num() != Input.Movement.Guidance.Records.Num())
    return false;
  TMap<int32, const FCrowdMassSharedFlowAgentOutput*> FlowById;
  for (const FCrowdMassSharedFlowAgentOutput& Agent : SharedFlow.Agents)
  {
    if (Agent.AgentId == INDEX_NONE || FlowById.Contains(Agent.AgentId))
      return false;
    FlowById.Add(Agent.AgentId, &Agent);
  }
  OutMovement = Input.Movement;
  for (FCrowdMassGatherRecord& Record : OutMovement.Guidance.Records)
  {
    const FCrowdMassSharedFlowAgentOutput* const* Flow =
      FlowById.Find(Record.Identity.AgentId);
    if (!Flow) return false;
    Record.Guidance.SharedFlow = (*Flow)->Candidate;
  }
  return FlowById.Num() == OutMovement.Guidance.Records.Num();
}

bool FCrowdDemoRoundWorkGraph::BuildParticleInput(
  const FCrowdDemoRoundWorkGraphInput& Input,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  FCrowdMassParticlePipelineWorkInput& OutParticle)
{
  OutParticle = {};
  if (!Movement.bCompleted
    || !Input.ParticleTemplate.Snapshot.bValid
    || Movement.MovementPredict.Results.Num()
      != Input.ParticleTemplate.Snapshot.Agents.Num())
    return false;
  OutParticle = Input.ParticleTemplate;
  OutParticle.PredictedMovements = Movement.MovementPredict.Results;
  TMap<int32, const FCrowdMassPredictedMovement*> PredictedById;
  int32 ActivePredictedCount = 0;
  for (const FCrowdMassPredictedMovement& Predicted
    : OutParticle.PredictedMovements)
  {
    if (!Predicted.bValid || PredictedById.Contains(Predicted.AgentId))
      return false;
    PredictedById.Add(Predicted.AgentId, &Predicted);
    if (Predicted.bParticleActive) ++ActivePredictedCount;
  }
  int32 Joined = 0;
  for (FCrowdParticleConstraintAgent& Agent : OutParticle.Particle.Agents)
  {
    const FCrowdMassPredictedMovement* const* Predicted =
      PredictedById.Find(Agent.AgentId);
    if (!Predicted) continue;
    Agent.StartPosition = (*Predicted)->StartPosition;
    Agent.PredictedPosition = (*Predicted)->PredictedPosition;
    ++Joined;
  }
  OutParticle.Particle.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  return Joined == ActivePredictedCount
    && OutParticle.Particle.Agents.Num() - Joined
      == OutParticle.ExpectedExternalAgentCount;
}

bool FCrowdDemoRoundWorkGraph::BuildFacingInput(
  const FCrowdDemoRoundWorkGraphInput& Input,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  const FCrowdMassParticlePipelineWorkOutput& Particle,
  FCrowdMassFacingFinalizeWorkInput& OutFacing)
{
  if (!Movement.bCompleted || !Particle.bCompleted
    || !Particle.PublishPlan.bValid
    || Input.FacingTemplates.Num()
      != Input.ParticleTemplate.Snapshot.Agents.Num())
    return false;
  return BuildFacingInputFromKinematics(
    Input, Input.ParticleTemplate.Snapshot, Movement,
    Particle.PublishPlan.FinalKinematics, OutFacing);
}

bool FCrowdDemoRoundWorkGraph::BuildFacingInputFromKinematics(
  const FCrowdDemoRoundWorkGraphInput& Input,
  const FCrowdMassBoundarySnapshot& Snapshot,
  const FCrowdMassMovementPipelineWorkOutput& Movement,
  const TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
  FCrowdMassFacingFinalizeWorkInput& OutFacing)
{
  OutFacing = {};
  if (!Movement.bCompleted || !Snapshot.bValid
    || Input.FacingTemplates.Num() != Snapshot.Agents.Num()
    || Kinematics.Num() != Snapshot.Agents.Num())
    return false;
  TMap<int32, const FCrowdComposedGuidance*> GuidanceById;
  for (const FCrowdComposedGuidance& Guidance
    : Movement.Guidance.ComposedGuidance)
  {
    if (GuidanceById.Contains(Guidance.AgentId)) return false;
    GuidanceById.Add(Guidance.AgentId, &Guidance);
  }
  TMap<int32, const FCrowdMassFinalKinematicState*> KinematicById;
  for (const FCrowdMassFinalKinematicState& Kinematic : Kinematics)
  {
    if (!Kinematic.bValid || KinematicById.Contains(Kinematic.AgentId))
      return false;
    KinematicById.Add(Kinematic.AgentId, &Kinematic);
  }
  OutFacing.Facing.FixedStepIndex = Snapshot.FixedStepIndex;
  OutFacing.Facing.PlanRevision = Snapshot.PlanRevision;
  OutFacing.Facing.Settings = Input.FacingSettings;
  for (const FCrowdDemoRoundFacingTemplate& Template
    : Input.FacingTemplates)
  {
    const FCrowdComposedGuidance* const* Guidance =
      GuidanceById.Find(Template.Input.AgentId);
    const FCrowdMassFinalKinematicState* const* Kinematic =
      KinematicById.Find(Template.Input.AgentId);
    if (!Guidance || !Kinematic) return false;
    FCrowdFacingInput& Facing = OutFacing.Facing.Agents.AddDefaulted_GetRef();
    Facing = Template.Input;
    Facing.AutonomousPreferredVelocity = FVector2f(
      (*Guidance)->AutonomousPreferredVelocity.X,
      (*Guidance)->AutonomousPreferredVelocity.Y);
    Facing.Location = FVector2f(
      (*Kinematic)->Position.X, (*Kinematic)->Position.Y);
  }
  OutFacing.Facing.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutFacing.Snapshot = Snapshot;
  OutFacing.Kinematics =
    TArray<FCrowdMassFinalKinematicState>(Kinematics);
  return GuidanceById.Num() == OutFacing.Facing.Agents.Num()
    && KinematicById.Num() == OutFacing.Facing.Agents.Num();
}

namespace
{
  constexpr int32 BoundarySnapshotCheckpointCadenceTicks = 300;

  bool IsWorkerDomainProductionMode(const TCHAR* Key)
  {
    FString Value;
    return FParse::Value(FCommandLine::Get(), Key, Value)
      && Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase);
  }

  bool IsFullWorkerProductionMode()
  {
    const TCHAR* Keys[] = {
      TEXT("CrowdWorkerMovementMode="),
      TEXT("CrowdWorkerBehaviorMode="),
      TEXT("CrowdWorkerTargetMode="),
      TEXT("CrowdWorkerParticleMode="),
      TEXT("CrowdWorkerProjectileMode="),
      TEXT("CrowdWorkerCombatMode=")};
    for (const TCHAR* Key : Keys)
      if (!IsWorkerDomainProductionMode(Key))
        return false;
    return true;
  }

  bool BuildTargetObjectiveRevisionDelta(
    const FCrowdDemoTargetFact& Fact,
    const int32 FixedStepIndex,
    const uint64 ResourceRevision,
    FCrowdWorkerObjectiveRevisionDelta& OutDelta)
  {
    OutDelta = {};
    if (FixedStepIndex < 0 || ResourceRevision == 0)
      return false;
    FCrowdWorkerTargetObjectiveRevision Revision;
    Revision.TargetRevision = Fact.TargetRevision;
    Revision.EffectiveFixedStepIndex = FixedStepIndex;
    Revision.TargetLocation = FVector2f(
      Fact.Location.X, Fact.Location.Y);
    Revision.TargetVelocity = FVector2f(
      Fact.Velocity.X, Fact.Velocity.Y);
    OutDelta.ObjectiveId =
      CrowdWorkerTargetObjectiveIds::PrimaryTarget;
    OutDelta.Revision = ResourceRevision;
    return FCrowdWorkerTargetObjectiveRevisionCodec::Encode(
      Revision, OutDelta.Payload);
  }

  uint64 CalculateTargetObjectiveSemanticHash(
    const FCrowdDemoTargetFact& Fact)
  {
    uint32 Hash = 2166136261u;
    const auto Fold = [&Hash](const uint32 Value)
    {
      Hash ^= Value;
      Hash *= 16777619u;
    };
    Fold(static_cast<uint32>(Fact.TargetRevision));
    Fold(GetTypeHash(Fact.Location));
    Fold(GetTypeHash(Fact.Velocity));
    return static_cast<uint64>(Hash) + 1;
  }

  float CalculateCanonicalSimulationTimeSeconds(
    const double TargetSimulationTimeSeconds,
    const double FixedSimulationQuantumSeconds)
  {
    if (!FMath::IsFinite(TargetSimulationTimeSeconds)
      || !FMath::IsFinite(FixedSimulationQuantumSeconds)
      || TargetSimulationTimeSeconds < 0.0
      || FixedSimulationQuantumSeconds <= 0.0)
      return 0.0f;
    const uint64 SimulationTick = FMath::Max<uint64>(
      1,
      static_cast<uint64>(FMath::RoundToInt64(
        TargetSimulationTimeSeconds
          / FixedSimulationQuantumSeconds)));
    return static_cast<float>(
      static_cast<double>(SimulationTick)
        * FixedSimulationQuantumSeconds);
  }

  struct FCrowdDemoOwnedSharedFlowShadowInput
  {
    int32 FixedStepIndex = INDEX_NONE;
    int32 PlanRevision = INDEX_NONE;
    float FixedStepSeconds = 0.0f;
    TArray<FCrowdSharedFlowField> Fields;
    TArray<FCrowdMassSharedFlowAgentInput> Agents;

    bool Capture(const FCrowdMassSharedFlowSampleInput& Input)
    {
      FixedStepIndex = Input.FixedStepIndex;
      PlanRevision = Input.PlanRevision;
      FixedStepSeconds = Input.FixedStepSeconds;
      Agents = Input.Agents;
      Fields.Reset(Input.Fields.Num());
      for (const FCrowdSharedFlowField* Field : Input.Fields)
      {
        if (!Field) return false;
        Fields.Add(*Field);
      }
      return FixedStepIndex >= 0
        && PlanRevision >= 0
        && FixedStepSeconds > 0.0f
        && !Fields.IsEmpty()
        && !Agents.IsEmpty();
    }

    uint64 Execute(
      const int32 ShardSize,
      const bool bReverseDispatchOrder) const
    {
      FCrowdMassSharedFlowSampleInput Input;
      Input.FixedStepIndex = FixedStepIndex;
      Input.PlanRevision = PlanRevision;
      Input.FixedStepSeconds = FixedStepSeconds;
      Input.Agents = Agents;
      Input.Fields.Reserve(Fields.Num());
      for (const FCrowdSharedFlowField& Field : Fields)
        Input.Fields.Add(&Field);
      const FCrowdMassSharedFlowSampleOutput Output =
        FCrowdMassSharedFlowWork::BuildPreferredSharded(
          Input, ShardSize, bReverseDispatchOrder);
      return Output.bValid ? Output.StableHash : 0;
    }
  };

  uint64 FoldBoundaryHash(uint64 Hash, const uint64 Value)
  {
    for (int32 Shift = 0; Shift < 64; Shift += 8)
    {
      Hash ^= static_cast<uint8>(Value >> Shift);
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  uint64 CalculateCombatCommitStableHash(
    const uint64 SnapshotHash,
    const int32 FixedStepIndex,
    TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
    const FCrowdDemoProjectileStepSummary& ProjectileSummary,
    const FCrowdDemoHitResponseSummary& HitSummary)
  {
    if (SnapshotHash == 0 || FixedStepIndex < 0
      || !ProjectileSummary.bValid || !HitSummary.bValid)
      return 0;
    TArray<FCrowdDemoRangedCombatAgent> Sorted(Agents);
    Sorted.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    uint64 StableHash = FoldBoundaryHash(
      SnapshotHash, static_cast<uint32>(FixedStepIndex));
    StableHash = FoldBoundaryHash(StableHash, 1);
    for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    {
      const FCrowdDemoRangedCombatAgent& Agent = Sorted[Index];
      if (Agent.AgentId == INDEX_NONE
        || Agent.LifecycleSerial <= 0
        || (Index > 0
          && Sorted[Index - 1].AgentId >= Agent.AgentId))
        return 0;
      StableHash = FoldBoundaryHash(
        StableHash, static_cast<uint32>(Agent.AgentId));
      StableHash = FoldBoundaryHash(
        StableHash, static_cast<uint32>(Agent.LifecycleSerial));
      StableHash = FoldBoundaryHash(
        StableHash,
        static_cast<uint32>(Agent.Combat.BusinessStateRevision));
      StableHash = FoldBoundaryHash(
        StableHash,
        static_cast<uint32>(Agent.Combat.ReactiveRevision));
      StableHash = FoldBoundaryHash(
        StableHash, Agent.Combat.LastConsumedHitEventId);
    }
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.AttackStateHash);
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.ProjectileStateHash);
    StableHash = FoldBoundaryHash(
      StableHash, ProjectileSummary.EventHash);
    StableHash = FoldBoundaryHash(
      StableHash, static_cast<uint32>(HitSummary.AppliedHitCount));
    StableHash = FoldBoundaryHash(
      StableHash, static_cast<uint32>(HitSummary.DuplicateHitCount));
    return StableHash;
  }

  uint32 FoldTargetDiagnosticHash(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
  }

  uint64 CalculateTargetResourceCohortStateHash(
    const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime)
  {
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldBoundaryHash(
      Hash, Runtime.Cohort.CapabilityProfileKey);
    Hash = FoldBoundaryHash(Hash, Runtime.TopologyRoundHash);
    Hash = FoldBoundaryHash(Hash, Runtime.DemandRoundHash);
    Hash = FoldBoundaryHash(Hash, Runtime.TransportRoundHash);
    Hash = FoldBoundaryHash(Hash, Runtime.GuidanceRoundHash);
    Hash = FoldBoundaryHash(Hash, Runtime.ValidationRoundHash);
    Hash = FoldBoundaryHash(Hash, Runtime.Plan.TransportHash);
    Hash = FoldBoundaryHash(Hash, Runtime.QuotaExecution.ExecutionHash);
    Hash = FoldBoundaryHash(Hash, Runtime.GuidanceSummary.GuidanceHash);
    Hash = FoldBoundaryHash(Hash, static_cast<uint32>(Runtime.PlanRebuildCount));
    Hash = FoldBoundaryHash(Hash, static_cast<uint32>(Runtime.InvalidStepCount));
    Hash = FoldBoundaryHash(
      Hash, static_cast<uint32>(Runtime.ValidationFailureCount));
    Hash = FoldBoundaryHash(
      Hash, static_cast<uint32>(Runtime.GuidanceUnroutedStepCount));
    return Hash;
  }

  uint64 CalculatePreparedTargetResourceHash(
    TConstArrayView<FCrowdDemoBoundaryFacingWorkState::FTargetTopologySlot>
      Slots)
  {
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldBoundaryHash(Hash, static_cast<uint64>(Slots.Num()) + 1);
    for (const auto& Slot : Slots)
    {
      Hash = FoldBoundaryHash(Hash, Slot.CohortKey);
      Hash = FoldBoundaryHash(Hash, Slot.Output.Topology.TopologyHash);
      Hash = FoldBoundaryHash(Hash, Slot.DemandOutput.Demand.DemandHash);
      Hash = FoldBoundaryHash(Hash, Slot.PlanOutput.Plan.TransportHash);
      Hash = FoldBoundaryHash(Hash, Slot.PlanOutput.Execution.ExecutionHash);
      Hash = FoldBoundaryHash(
        Hash, Slot.PlanOutput.Validation.ValidationHash);
      Hash = FoldBoundaryHash(
        Hash, Slot.GuidanceOutput.Summary.GuidanceHash);
    }
    return Hash;
  }

  uint64 CalculateHomogeneousTargetResourceStateHash(
    const FCrowdDemoTargetPolarTopology& Topology,
    const FCrowdDemoTargetRegionDemandResult& Demand,
    const FCrowdDemoTargetRegionFlowPlan& Plan,
    const FCrowdDemoTargetRegionQuotaExecutionState& Execution,
    const FCrowdDemoTargetRegionPlanValidationResult& Validation,
    const FCrowdDemoTargetRegionGuidanceSummary& Guidance,
    const uint32 TopologyRoundHash,
    const uint32 DemandRoundHash,
    const uint32 TransportRoundHash,
    const uint32 GuidanceRoundHash,
    const uint32 ValidationRoundHash,
    const int32 PlanRebuildCount,
    const int32 InvalidStepCount)
  {
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldBoundaryHash(Hash, Topology.TopologyHash);
    Hash = FoldBoundaryHash(Hash, Demand.DemandHash);
    Hash = FoldBoundaryHash(Hash, Plan.TransportHash);
    Hash = FoldBoundaryHash(Hash, Execution.ExecutionHash);
    Hash = FoldBoundaryHash(Hash, Validation.ValidationHash);
    Hash = FoldBoundaryHash(Hash, Guidance.GuidanceHash);
    Hash = FoldBoundaryHash(Hash, TopologyRoundHash);
    Hash = FoldBoundaryHash(Hash, DemandRoundHash);
    Hash = FoldBoundaryHash(Hash, TransportRoundHash);
    Hash = FoldBoundaryHash(Hash, GuidanceRoundHash);
    Hash = FoldBoundaryHash(Hash, ValidationRoundHash);
    Hash = FoldBoundaryHash(Hash, static_cast<uint32>(PlanRebuildCount));
    Hash = FoldBoundaryHash(Hash, static_cast<uint32>(InvalidStepCount));
    return Hash;
  }

  FCrowdDemoCombatAgentState BuildBoundaryCombatState(
    const FCrowdDemoRoundBoundaryBusinessFact& Fact)
  {
    FCrowdDemoCombatAgentState Result;
    Result.AgentId = Fact.AgentId;
    Result.LifecycleSerial =
      static_cast<int32>(Fact.EntityRef.LifecycleSerial);
    Result.Health = Fact.Stats.Health;
    Result.MaxHealth = Fact.Stats.MaxHealth;
    Result.LifecycleState = Fact.Stats.LifecycleState;
    Result.bAlive = Fact.Stats.bAlive;
    Result.BusinessState = Fact.Business.State;
    Result.BusinessStateRevision = Fact.Business.StateRevision;
    Result.BusinessStateEnterFixedStep =
      Fact.Business.StateEnterFixedStep;
    Result.TargetAgentId = Fact.Business.TargetAgentId;
    Result.TargetLifecycleSerial = Fact.Business.TargetLifecycleSerial;
    Result.LastConsumedHitEventId =
      Fact.Business.LastConsumedHitEventId;
    Result.AttackPhase = Fact.Attack.Phase;
    Result.AttackPhaseEnterFixedStep =
      Fact.Attack.PhaseEnterFixedStep;
    Result.CooldownEndFixedStep = Fact.Attack.CooldownEndFixedStep;
    Result.LockedTargetAgentId = Fact.Attack.LockedTargetAgentId;
    Result.LockedTargetLifecycleSerial =
      Fact.Attack.LockedTargetLifecycleSerial;
    Result.LockedTargetLocation = Fact.Attack.LockedTargetLocation;
    Result.FireSequence = Fact.Attack.FireSequence;
    Result.bFireRequestIssued = Fact.Attack.bFireRequestIssued;
    Result.ReactiveMode = Fact.ReactiveMotion.Mode;
    Result.HorizontalReactiveVelocity =
      Fact.ReactiveMotion.HorizontalVelocity;
    Result.VerticalReactiveVelocityCmps =
      Fact.ReactiveMotion.VerticalVelocityCmps;
    Result.ReactiveStartFixedStep =
      Fact.ReactiveMotion.StartFixedStep;
    Result.ReactiveEndFixedStep = Fact.ReactiveMotion.EndFixedStep;
    Result.ReactiveRevision = Fact.ReactiveMotion.ReactiveRevision;
    Result.RestoreBusinessState =
      Fact.ReactiveMotion.RestoreBusinessState;
    Result.ApexCount = Fact.ReactiveMotion.ApexCount;
    Result.LandingCount = Fact.ReactiveMotion.LandingCount;
    Result.HitFlashRevision = Fact.HitFlash.FlashRevision;
    Result.HitFlashStartServerTimeSeconds =
      Fact.HitFlash.StartServerTimeSeconds;
    Result.HitFlashDurationSeconds = Fact.HitFlash.DurationSeconds;
    Result.HitFlashProfileKey = Fact.HitFlash.ProfileKey;
    Result.HitFlashPeakIntensity = Fact.HitFlash.PeakIntensity;
    Result.VisualState = Fact.Visual.VisualState;
    Result.VisualRevision = Fact.Visual.VisualRevision;
    Result.VisualStateStartServerTimeSeconds =
      Fact.Visual.StateStartServerTimeSeconds;
    Result.VisualPhaseSeed = Fact.Visual.PhaseSeed;
    return Result;
  }

  bool ApplyWorkerCombatState(
    const FCrowdDemoCombatAgentState& State,
    FCrowdDemoRoundBoundaryBusinessFact& Fact)
  {
    if (State.AgentId != Fact.AgentId
      || State.LifecycleSerial
        != static_cast<int32>(Fact.EntityRef.LifecycleSerial))
      return false;
    Fact.Stats.Health = State.Health;
    Fact.Stats.MaxHealth = State.MaxHealth;
    Fact.Stats.LifecycleState = State.LifecycleState;
    Fact.Stats.bAlive = State.bAlive;
    Fact.Business.State = State.BusinessState;
    Fact.Business.StateRevision = State.BusinessStateRevision;
    Fact.Business.StateEnterFixedStep =
      State.BusinessStateEnterFixedStep;
    Fact.Business.TargetAgentId = State.TargetAgentId;
    Fact.Business.TargetLifecycleSerial =
      State.TargetLifecycleSerial;
    Fact.Business.LastConsumedHitEventId =
      State.LastConsumedHitEventId;
    Fact.Attack.Phase = State.AttackPhase;
    Fact.Attack.PhaseEnterFixedStep =
      State.AttackPhaseEnterFixedStep;
    Fact.Attack.CooldownEndFixedStep = State.CooldownEndFixedStep;
    Fact.Attack.LockedTargetAgentId = State.LockedTargetAgentId;
    Fact.Attack.LockedTargetLifecycleSerial =
      State.LockedTargetLifecycleSerial;
    Fact.Attack.LockedTargetLocation = State.LockedTargetLocation;
    Fact.Attack.FireSequence = State.FireSequence;
    Fact.Attack.bFireRequestIssued = State.bFireRequestIssued;
    Fact.ReactiveMotion.Mode = State.ReactiveMode;
    Fact.ReactiveMotion.HorizontalVelocity =
      State.HorizontalReactiveVelocity;
    Fact.ReactiveMotion.VerticalVelocityCmps =
      State.VerticalReactiveVelocityCmps;
    Fact.ReactiveMotion.StartFixedStep = State.ReactiveStartFixedStep;
    Fact.ReactiveMotion.EndFixedStep = State.ReactiveEndFixedStep;
    Fact.ReactiveMotion.ReactiveRevision = State.ReactiveRevision;
    Fact.ReactiveMotion.RestoreBusinessState =
      State.RestoreBusinessState;
    Fact.ReactiveMotion.ApexCount = State.ApexCount;
    Fact.ReactiveMotion.LandingCount = State.LandingCount;
    Fact.HitFlash.FlashRevision = State.HitFlashRevision;
    Fact.HitFlash.StartServerTimeSeconds =
      State.HitFlashStartServerTimeSeconds;
    Fact.HitFlash.DurationSeconds = State.HitFlashDurationSeconds;
    Fact.HitFlash.ProfileKey = State.HitFlashProfileKey;
    Fact.HitFlash.PeakIntensity = State.HitFlashPeakIntensity;
    Fact.Visual.VisualState = State.VisualState;
    Fact.Visual.VisualRevision = State.VisualRevision;
    Fact.Visual.StateStartServerTimeSeconds =
      State.VisualStateStartServerTimeSeconds;
    Fact.Visual.PhaseSeed = State.VisualPhaseSeed;
    return true;
  }

  FCrowdDemoBoundaryBusinessWorkOutput RunBoundaryBusinessWork(
    const FCrowdDemoBoundaryBusinessWorkInput& Input)
  {
    FCrowdDemoBoundaryBusinessWorkOutput Output;
    const bool bProjectileCombat =
      Input.Rules.RangedCombatSettings.bEnabled != 0;
    const bool bShowcase =
      Input.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && Input.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
    Output.bRequiresCommit = bProjectileCombat || bShowcase;
    if (!Input.Snapshot.bValid
      || Input.FixedStepIndex != Input.Snapshot.FixedStepIndex
      || Input.PlanRevision != Input.Snapshot.PlanRevision
      || Input.Facts.Num() != Input.Snapshot.Agents.Num()
      || Input.FixedStepSeconds <= 0.0f)
      return Output;

    uint64 StableHash = FoldBoundaryHash(
      Input.Snapshot.StableHash,
      static_cast<uint32>(Input.FixedStepIndex));
    if (!Output.bRequiresCommit)
    {
      Output.StableHash = FoldBoundaryHash(StableHash, 0);
      Output.bCompleted = Output.StableHash != 0;
      return Output;
    }

    TArray<FCrowdDemoRangedCombatAgent> Agents;
    TMap<int32, FVector> LocalOffsetByAgentId;
    TMap<int32, float> YawByAgentId;
    Agents.Reserve(Input.Facts.Num());
    for (int32 Index = 0; Index < Input.Facts.Num(); ++Index)
    {
      const FCrowdDemoRoundBoundaryBusinessFact& Fact =
        Input.Facts[Index];
      const FCrowdMassBoundaryAgentRecord& Base =
        Input.Snapshot.Agents[Index];
      if (Fact.EntityRef != Base.AgentFacts.StableEntityRef
        || Fact.AgentId != Base.Identity.AgentId
        || !Fact.bHasCombatCapability)
        return Output;
      FCrowdDemoRangedCombatAgent& Agent =
        Agents.AddDefaulted_GetRef();
      Agent.EntityRef = Fact.EntityRef;
      Agent.AgentId = Fact.AgentId;
      Agent.LifecycleSerial =
        static_cast<int32>(Fact.EntityRef.LifecycleSerial);
      Agent.FormationIndex = Fact.FormationIndex;
      Agent.FactionId = Base.AgentFacts.FactionKey;
      Agent.Position = Base.State.Position;
      Agent.Velocity = Base.State.Velocity;
      Agent.RadiusCm = Fact.RadiusCm;
      Agent.bAlive = Fact.Stats.bAlive;
      Agent.Combat = BuildBoundaryCombatState(Fact);
      LocalOffsetByAgentId.Add(Agent.AgentId, Fact.LocalOffset);
      YawByAgentId.Add(Agent.AgentId, Fact.YawDegrees);
    }
    Agents.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    if (Agents.Num() != Input.Snapshot.Agents.Num())
      return Output;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      if (Agents[Index].AgentId
          != Input.Snapshot.Agents[Index].Identity.AgentId
        || (Index > 0
          && Agents[Index - 1].AgentId >= Agents[Index].AgentId))
        return Output;
    }

    if (bShowcase && Input.FixedStepIndex == 0)
    {
      for (FCrowdDemoRangedCombatAgent& Agent : Agents)
      {
        if (Agent.Combat.BusinessStateRevision != 0) continue;
        Agent.Combat.BusinessState =
          static_cast<ECrowdDemoBusinessState>(
            FCrowdDemoVatShowcasePlanner::ResolveInitialState(
              Agent.FormationIndex));
        Agent.Combat.AttackPhase =
          Agent.Combat.BusinessState
            == ECrowdDemoBusinessState::Attacking
          ? ECrowdDemoAttackPhase::Windup
          : ECrowdDemoAttackPhase::None;
        Agent.Combat.BusinessStateRevision = 1;
        Agent.Combat.BusinessStateEnterFixedStep = 0;
      }
    }

    TArray<FCrowdDemoHitFact> HitFacts;
    FCrowdDemoProjectileStepSummary ProjectileSummary;
    TArray<FCrowdDemoProjectileVisualEvent> ProjectileEvents;
    TArray<FCrowdProjectileState> NextProjectiles =
      Input.Projectiles;
    if (bProjectileCombat)
    {
      FCrowdPreparedProjectileBoundary ProjectileBoundary;
      if (!FCrowdDemoProjectileAdapters::PrepareProjectileBoundary(
          Input.RoundId, Input.FixedStepIndex,
          Input.SimulationTimeSeconds, Input.FixedStepSeconds,
          Input.Rules.RangedCombatSettings,
          Input.Rules.FlowFieldConfig, Agents, Input.Projectiles,
          ProjectileBoundary, HitFacts, ProjectileEvents,
          ProjectileSummary))
        return Output;
      NextProjectiles = MoveTemp(ProjectileBoundary.States);
    }
    if (bShowcase)
    {
      for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
      {
        const ECrowdDemoVatInjectedHit InjectedHit =
          FCrowdDemoVatShowcasePlanner::SelectInjectedHit(
            Agent.FormationIndex, Input.FixedStepIndex);
        if (InjectedHit == ECrowdDemoVatInjectedHit::None)
          continue;
        const bool bKnockback =
          InjectedHit == ECrowdDemoVatInjectedHit::Knockback;
        const bool bKnockUp =
          InjectedHit == ECrowdDemoVatInjectedHit::KnockUp;
        const bool bDeath =
          InjectedHit == ECrowdDemoVatInjectedHit::Death;
        FCrowdDemoHitFact& Fact = HitFacts.AddDefaulted_GetRef();
        Fact.HitEventId =
          (static_cast<uint64>(Input.RoundId) << 32)
          | (static_cast<uint64>(Input.FixedStepIndex) << 16)
          | static_cast<uint32>(Agent.AgentId);
        Fact.ApplyFixedStep = Input.FixedStepIndex;
        Fact.TargetAgentId = Agent.AgentId;
        Fact.TargetLifecycleSerial = Agent.Combat.LifecycleSerial;
        Fact.HitPosition = Agent.Position;
        Fact.HitDirection = FVector::ForwardVector;
        Fact.Damage = bDeath ? 1000.0f : 10.0f;
        Fact.HorizontalImpulseCmps = bKnockback ? 500.0f : 0.0f;
        Fact.VerticalImpulseCmps = bKnockUp ? 650.0f : 0.0f;
        Fact.HitFlashProfileKey = 1;
      }
    }

    TArray<FCrowdDemoCombatAgentState> CombatStates;
    CombatStates.Reserve(Agents.Num());
    for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
      CombatStates.Add(Agent.Combat);
    FCrowdDemoHitResponseSettings HitSettings;
    HitSettings.FixedStepSeconds = Input.FixedStepSeconds;
    FCrowdDemoHitResponseSummary HitSummary;
    FCrowdDemoCombatStateKernel::ResolveHitFacts(
      Input.FixedStepIndex, Input.SimulationTimeSeconds, HitFacts,
      HitSettings, CombatStates, HitSummary);
    TMap<int32, const FCrowdDemoCombatAgentState*> CombatByAgentId;
    for (const FCrowdDemoCombatAgentState& Combat : CombatStates)
      CombatByAgentId.Add(Combat.AgentId, &Combat);
    for (FCrowdDemoRangedCombatAgent& Agent : Agents)
    {
      const FCrowdDemoCombatAgentState* const* Combat =
        CombatByAgentId.Find(Agent.AgentId);
      if (!Combat) return Output;
      Agent.Combat = **Combat;

      const float YawDegrees =
        YawByAgentId.FindRef(Agent.AgentId);
      FCrowdDemoGuidanceCandidate BusinessCandidate;
      if (bProjectileCombat)
      {
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, FVector::ZeroVector,
            Agent.Position, YawDegrees, true);
      }
      else
      {
        const FVector Anchor = FVector(Input.Rules.SpawnOrigin)
          + LocalOffsetByAgentId.FindRef(Agent.AgentId);
        const FCrowdDemoVatShowcaseMotionResult Showcase =
          FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
            Agent.FormationIndex, Input.FixedStepIndex,
            Agent.Position, Anchor);
        if (Showcase.bValid)
        {
          BusinessCandidate =
            FCrowdDemoGuidanceComposeKernel::BuildCandidate(
              Agent.AgentId,
              ECrowdDemoGuidanceProvider::BusinessOverride,
              Input.PlanRevision, Showcase.DesiredVelocity,
              Showcase.DesiredLocation,
              Showcase.DesiredVelocity.IsNearlyZero()
                ? YawDegrees
                : Showcase.DesiredVelocity.Rotation().Yaw,
              true);
        }
      }
      const FCrowdDemoReactiveMotionStepResult StepResult =
        FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(
          Input.FixedStepIndex, Agent.Position.Z, HitSettings,
          Agent.Combat);
      FCrowdDemoPreparedReactiveMotionStep& ReactiveStep =
        Output.ReactiveSteps.AddDefaulted_GetRef();
      ReactiveStep.AgentId = Agent.AgentId;
      ReactiveStep.LifecycleSerial = Agent.LifecycleSerial;
      if (!Agent.Combat.bAlive)
      {
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, FVector::ZeroVector,
            Agent.Position, YawDegrees, true);
      }
      else if (StepResult.bValid
        && Agent.Combat.ReactiveMode
          != ECrowdDemoReactiveMotionMode::None)
      {
        const FVector ReactiveVelocity(
          StepResult.HorizontalVelocity.X,
          StepResult.HorizontalVelocity.Y, 0.0f);
        const FVector DesiredLocation = BusinessCandidate.bValid
          ? BusinessCandidate.DesiredLocation : Agent.Position;
        BusinessCandidate =
          FCrowdDemoGuidanceComposeKernel::BuildCandidate(
            Agent.AgentId,
            ECrowdDemoGuidanceProvider::BusinessOverride,
            Input.PlanRevision, ReactiveVelocity, DesiredLocation,
            ReactiveVelocity.IsNearlyZero()
              ? YawDegrees : ReactiveVelocity.Rotation().Yaw,
            true);
        ReactiveStep.bActive = true;
        ReactiveStep.ProposedZ = StepResult.NewZ;
        ReactiveStep.VerticalVelocityCmps =
          StepResult.NewVerticalVelocityCmps;
      }
      if (BusinessCandidate.bValid)
      {
        Output.GuidanceCandidates.Add(
          FCrowdDemoMassCrowdRuntimeAdapter::
            BuildCoreGuidanceCandidate(BusinessCandidate));
      }
    }
    Output.GuidanceCandidates.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    Output.ReactiveSteps.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });

    StableHash = CalculateCombatCommitStableHash(
      Input.Snapshot.StableHash, Input.FixedStepIndex,
      Agents, ProjectileSummary, HitSummary);

    Output.Commit.FixedStepIndex = Input.FixedStepIndex;
    Output.Commit.PlanRevision = Input.PlanRevision;
    Output.Commit.Agents = MoveTemp(Agents);
    Output.Commit.Projectiles = MoveTemp(NextProjectiles);
    Output.Commit.ProjectileEvents = MoveTemp(ProjectileEvents);
    Output.Commit.ProjectileSummary = ProjectileSummary;
    Output.Commit.HitSummary = HitSummary;
    Output.Commit.StableHash = StableHash;
    Output.Commit.bProjectileCombat = bProjectileCombat;
    Output.Commit.bValid = StableHash != 0;
    Output.StableHash = StableHash;
    Output.bCompleted = Output.Commit.bValid
      && Output.ReactiveSteps.Num() == Input.Snapshot.Agents.Num();
    return Output;
  }

  constexpr float CorrectionMaxAgeMs = 1000.0f;
  constexpr float OverlapRadiusCm = 78.0f;
  constexpr float SevereOverlapRadiusCm = 42.0f;

  bool IsFlowScenario(const ECrowdDemoScenario Scenario)
  {
    return Scenario == ECrowdDemoScenario::SimRoundObstacle
      || Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
  }

  uint32 FoldHash(uint32 Hash, const uint32 Value)
  {
    for (int32 Shift = 0; Shift < 32; Shift += 8)
    {
      Hash ^= (Value >> Shift) & 0xffu;
      Hash *= 16777619u;
    }
    return Hash;
  }

  uint64 CalculateMovementTailHash(
    const FCrowdDemoRoundWorkGraphOutput& GraphOutput,
    const FCrowdMassFacingFinalizeWorkOutput& Output,
    const TConstArrayView<FCrowdMassFinalKinematicState>
      ObstacleKinematics = {})
  {
    if (!GraphOutput.Movement.bCompleted
      || !Output.bCompleted)
      return 0;
    uint64 Hash = 14695981039346656037ull;
    Hash = FoldBoundaryHash(
      Hash, GraphOutput.Movement.StableHash);
    if (GraphOutput.Particle.bCompleted)
    {
      Hash = FoldBoundaryHash(
        Hash, GraphOutput.Particle.StableHash);
    }
    else
    {
      if (ObstacleKinematics.IsEmpty()) return 0;
      for (const FCrowdMassFinalKinematicState& Kinematic
        : ObstacleKinematics)
      {
        Hash = FoldBoundaryHash(
          Hash, static_cast<uint32>(Kinematic.AgentId));
        Hash = FoldBoundaryHash(
          Hash, GetTypeHash(Kinematic.Position));
        Hash = FoldBoundaryHash(
          Hash, GetTypeHash(Kinematic.Velocity));
      }
    }
    Hash = FoldBoundaryHash(Hash, Output.StableHash);
    return Hash;
  }

  uint32 CalculatePreparedMovementPredictHash(
    const FCrowdMassMovementPredictWorkOutput& Output,
    const float FixedStepSeconds)
  {
    if (!Output.bCompleted || Output.FixedStepIndex < 0
      || Output.PlanRevision < 0
      || !FMath::IsFinite(FixedStepSeconds)
      || FixedStepSeconds <= 0.0f
      || Output.Results.IsEmpty())
      return 0;
    TArray<FCrowdMassPredictedMovement> Results = Output.Results;
    Results.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    uint32 Hash = FoldHash(2166136261u, 1u);
    Hash = FoldHash(Hash,
      static_cast<uint32>(Output.FixedStepIndex));
    Hash = FoldHash(Hash,
      static_cast<uint32>(Output.PlanRevision));
    Hash = FoldHash(Hash, static_cast<uint32>(
      FMath::RoundToInt(FixedStepSeconds * 100.0f)));
    int32 PreviousAgentId = INDEX_NONE;
    const auto FoldVector = [](uint32 InHash, const FVector& Value)
    {
      InHash = FoldHash(InHash, static_cast<uint32>(
        FMath::RoundToInt(Value.X * 100.0)));
      InHash = FoldHash(InHash, static_cast<uint32>(
        FMath::RoundToInt(Value.Y * 100.0)));
      return FoldHash(InHash, static_cast<uint32>(
        FMath::RoundToInt(Value.Z * 100.0)));
    };
    for (const FCrowdMassPredictedMovement& Result : Results)
    {
      if (!Result.bValid || Result.AgentId == INDEX_NONE
        || Result.AgentId <= PreviousAgentId
        || Result.StartPosition.ContainsNaN()
        || Result.PredictedPosition.ContainsNaN()
        || Result.Velocity.ContainsNaN())
        return 0;
      PreviousAgentId = Result.AgentId;
      Hash = FoldHash(Hash, static_cast<uint32>(Result.AgentId));
      Hash = FoldVector(Hash, Result.StartPosition);
      Hash = FoldVector(Hash, Result.PredictedPosition);
      Hash = FoldVector(Hash, Result.Velocity);
      Hash = FoldHash(Hash, Result.bParticleActive ? 1u : 0u);
    }
    return Hash;
  }

  enum class ECrowdDemoWorkerParticleAuthorityMode : uint8
  {
    Shadow = 0,
    Canary,
    Production
  };

  enum class ECrowdDemoWorkerTargetAuthorityMode : uint8
  {
    Shadow = 0,
    Canary,
    Production
  };

  enum class ECrowdDemoWorkerProjectileAuthorityMode : uint8
  {
    Shadow = 0,
    Canary,
    Production
  };

  enum class ECrowdDemoWorkerCombatAuthorityMode : uint8
  {
    Shadow = 0,
    Canary,
    Production
  };

  bool ResolveWorkerProjectileAuthority(
    ECrowdDemoWorkerProjectileAuthorityMode& OutMode)
  {
    OutMode = ECrowdDemoWorkerProjectileAuthorityMode::Shadow;
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerProjectileMode="), Value)
      || Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerProjectileAuthorityMode::Canary;
      return true;
    }
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerProjectileAuthorityMode::Production;
      return true;
    }
    return false;
  }

  bool ResolveWorkerCombatAuthority(
    ECrowdDemoWorkerCombatAuthorityMode& OutMode)
  {
    OutMode = ECrowdDemoWorkerCombatAuthorityMode::Shadow;
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerCombatMode="), Value)
      || Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerCombatAuthorityMode::Canary;
      return true;
    }
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerCombatAuthorityMode::Production;
      return true;
    }
    return false;
  }

  bool ResolveWorkerTargetAuthority(
    const FCrowdMassBoundarySnapshot& Snapshot,
    ECrowdDemoWorkerTargetAuthorityMode& OutMode,
    TSet<FCrowdStableEntityRef>& OutCanaries)
  {
    OutMode = ECrowdDemoWorkerTargetAuthorityMode::Shadow;
    OutCanaries.Reset();
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerTargetMode="), Value)
      || Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerTargetAuthorityMode::Production;
      return true;
    }
    if (!Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
      return false;
    int32 CanaryCount = 0;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerTargetCanaryCount="), CanaryCount)
      || CanaryCount <= 0
      || CanaryCount >= Snapshot.Agents.Num())
      return false;
    TArray<FCrowdStableEntityRef> Sorted;
    Sorted.Reserve(Snapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
      Sorted.Add(Agent.AgentFacts.StableEntityRef);
    Sorted.Sort();
    for (int32 Index = 0; Index < CanaryCount; ++Index)
      OutCanaries.Add(Sorted[Index]);
    OutMode = ECrowdDemoWorkerTargetAuthorityMode::Canary;
    return true;
  }

  bool ResolveWorkerParticleAuthority(
    const FCrowdMassBoundarySnapshot& Snapshot,
    ECrowdDemoWorkerParticleAuthorityMode& OutMode,
    TSet<FCrowdStableEntityRef>& OutCanaries)
  {
    OutMode = ECrowdDemoWorkerParticleAuthorityMode::Shadow;
    OutCanaries.Reset();
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerParticleMode="), Value)
      || Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdDemoWorkerParticleAuthorityMode::Production;
      return true;
    }
    if (!Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
      return false;
    int32 CanaryCount = 0;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerParticleCanaryCount="), CanaryCount)
      || CanaryCount <= 0
      || CanaryCount >= Snapshot.Agents.Num())
      return false;
    TArray<FCrowdStableEntityRef> Sorted;
    Sorted.Reserve(Snapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
      Sorted.Add(Agent.AgentFacts.StableEntityRef);
    Sorted.Sort();
    for (int32 Index = 0; Index < CanaryCount; ++Index)
      OutCanaries.Add(Sorted[Index]);
    OutMode = ECrowdDemoWorkerParticleAuthorityMode::Canary;
    return true;
  }

  bool BuildWorkerV2PreparedMovement(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TFunctionRef<const FCrowdWorkerDomainProxyState*(
      const FCrowdStableEntityRef&, ECrowdWorkerField)>
      FindResultDomain,
    const FCrowdWorkerMovementAuthority& Authority,
    const uint64 ExpectedInputSequence,
    const ECrowdWorkerMovementAuthorityMode Mode,
    const float FixedStepSeconds,
    const FCrowdMassMovementPipelineWorkOutput& LegacyShape,
    const TConstArrayView<FCrowdMassFinalKinematicState>
      LegacyObstacleKinematics,
    FCrowdMassMovementPipelineWorkOutput& OutMovement)
  {
    OutMovement = LegacyShape;
    if (ExpectedInputSequence == 0
      || !LegacyShape.bCompleted
      || LegacyShape.MovementPredict.Results.Num()
        != Snapshot.Agents.Num())
      return false;
    TMap<int32, FCrowdMassPredictedMovement*> ResultByAgentId;
    for (FCrowdMassPredictedMovement& Result :
      OutMovement.MovementPredict.Results)
    {
      if (!Result.bValid || ResultByAgentId.Contains(Result.AgentId))
        return false;
      ResultByAgentId.Add(Result.AgentId, &Result);
    }
    TMap<int32, const FCrowdMassFinalKinematicState*>
      ObstacleByAgentId;
    for (const FCrowdMassFinalKinematicState& Kinematic :
      LegacyObstacleKinematics)
    {
      if (!Kinematic.bValid
        || ObstacleByAgentId.Contains(Kinematic.AgentId))
        return false;
      ObstacleByAgentId.Add(Kinematic.AgentId, &Kinematic);
    }
    int32 WorkerOwnedCount = 0;
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      Snapshot.Agents)
    {
      const FCrowdStableEntityRef EntityRef =
        Agent.AgentFacts.StableEntityRef;
      const bool bWorkerOwner =
        Mode == ECrowdWorkerMovementAuthorityMode::Production
        || Authority.IsWorkerOwner(EntityRef);
      if (!bWorkerOwner) continue;
      FCrowdMassPredictedMovement* const* Result =
        ResultByAgentId.Find(Agent.Identity.AgentId);
      const FCrowdWorkerDomainProxyState* Worker =
        FindResultDomain(EntityRef, ECrowdWorkerField::Movement);
      FCrowdWorkerMovementState WorkerState;
      if (!Result || !Worker
        || Worker->SourceInputSequence != ExpectedInputSequence
        || !FCrowdWorkerMovementStateCodec::Decode(
          Worker->State.Payload, WorkerState))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerV2PreparedMovementMissing agent=%d expected_input=%llu result=%d proxy=%d proxy_input=%llu"),
          Agent.Identity.AgentId,
          ExpectedInputSequence,
          Result ? 1 : 0,
          Worker ? 1 : 0,
          Worker ? Worker->SourceInputSequence : 0);
        return false;
      }
      const FCrowdMassFinalKinematicState* const*
        ObstacleKinematic =
          ObstacleByAgentId.Find(Agent.Identity.AgentId);
      const FVector& ExpectedPosition = ObstacleKinematic
        ? (*ObstacleKinematic)->Position
        : (*Result)->PredictedPosition;
      const FVector& ExpectedVelocity = ObstacleKinematic
        ? (*ObstacleKinematic)->Velocity
        : (*Result)->Velocity;
      const double PositionError = FVector::Distance(
        WorkerState.Position, ExpectedPosition);
      const double VelocityError = FVector::Distance(
        WorkerState.Velocity, ExpectedVelocity);
      if (Mode == ECrowdWorkerMovementAuthorityMode::Canary
        && (PositionError > 0.001 || VelocityError > 0.001))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerV2CanaryMovementMismatch agent=%d expected_input=%llu position_error_cm=%.6f velocity_error_cmps=%.6f worker_position=%s legacy_position=%s worker_velocity=%s legacy_velocity=%s"),
          Agent.Identity.AgentId,
          ExpectedInputSequence,
          PositionError,
          VelocityError,
          *WorkerState.Position.ToString(),
          *ExpectedPosition.ToString(),
          *WorkerState.Velocity.ToString(),
          *ExpectedVelocity.ToString());
        return false;
      }
      (*Result)->PredictedPosition = WorkerState.Position;
      (*Result)->Velocity = WorkerState.Velocity;
      ++WorkerOwnedCount;
    }
    if (WorkerOwnedCount == 0
      || (Mode == ECrowdWorkerMovementAuthorityMode::Production
        && WorkerOwnedCount != Snapshot.Agents.Num()))
      return false;
    OutMovement.MovementPredict.StableHash =
      CalculatePreparedMovementPredictHash(
        OutMovement.MovementPredict, FixedStepSeconds);
    if (OutMovement.MovementPredict.StableHash == 0)
      return false;
    uint32 Hash = FoldHash(2166136261u, 1u);
    Hash = FoldHash(Hash, OutMovement.Guidance.StableHash);
    Hash = FoldHash(Hash,
      OutMovement.LocalPredictive.bCompleted
        ? OutMovement.LocalPredictive.StableHash : 0u);
    Hash = FoldHash(
      Hash, OutMovement.MovementPredict.StableHash);
    OutMovement.StableHash = Hash;
    OutMovement.bCompleted = Hash != 0;
    return OutMovement.bCompleted;
  }

  bool BuildWorkerV2ParticleKinematics(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TFunctionRef<const FCrowdWorkerDomainProxyState*(
      const FCrowdStableEntityRef&, ECrowdWorkerField)>
      FindResultDomain,
    const uint64 ExpectedInputSequence,
    const uint64 AvailableInputSequence,
    const ECrowdDemoWorkerParticleAuthorityMode Mode,
    const TSet<FCrowdStableEntityRef>& Canaries,
    const FCrowdMassMovementPipelineWorkOutput& PreparedMovement,
    const TConstArrayView<FCrowdMassFinalKinematicState>
      LegacyKinematics,
    TArray<FCrowdMassFinalKinematicState>& OutKinematics)
  {
    OutKinematics.Reset();
    if (ExpectedInputSequence == 0
      || Mode == ECrowdDemoWorkerParticleAuthorityMode::Shadow
      || !PreparedMovement.bCompleted
      || PreparedMovement.MovementPredict.Results.Num()
        != Snapshot.Agents.Num()
      || LegacyKinematics.Num() != Snapshot.Agents.Num()
      || AvailableInputSequence < ExpectedInputSequence)
      return false;
    TMap<int32, const FCrowdMassPredictedMovement*> MovementById;
    for (const FCrowdMassPredictedMovement& Movement :
      PreparedMovement.MovementPredict.Results)
    {
      if (!Movement.bValid
        || MovementById.Contains(Movement.AgentId))
        return false;
      MovementById.Add(Movement.AgentId, &Movement);
    }
    TMap<int32, const FCrowdMassFinalKinematicState*> LegacyById;
    for (const FCrowdMassFinalKinematicState& Kinematic :
      LegacyKinematics)
    {
      if (!Kinematic.bValid
        || LegacyById.Contains(Kinematic.AgentId))
        return false;
      LegacyById.Add(Kinematic.AgentId, &Kinematic);
    }
    int32 WorkerOwnedCount = 0;
    for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    {
      const FCrowdStableEntityRef EntityRef =
        Agent.AgentFacts.StableEntityRef;
      const FCrowdMassPredictedMovement* const* Movement =
        MovementById.Find(Agent.Identity.AgentId);
      const FCrowdMassFinalKinematicState* const* Legacy =
        LegacyById.Find(Agent.Identity.AgentId);
      if (!Movement || !Legacy) return false;
      FCrowdMassFinalKinematicState& Final =
        OutKinematics.AddDefaulted_GetRef();
      Final = **Legacy;
      const bool bWorkerOwner =
        Mode == ECrowdDemoWorkerParticleAuthorityMode::Production
        || Canaries.Contains(EntityRef);
      if (!bWorkerOwner) continue;
      const FCrowdWorkerDomainProxyState* Worker =
        FindResultDomain(EntityRef, ECrowdWorkerField::Particle);
      FCrowdWorkerParticleState WorkerState;
      if (!Worker
        || Worker->SourceInputSequence > ExpectedInputSequence
        || !FCrowdWorkerParticleStateCodec::Decode(
          Worker->State.Payload, WorkerState))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerV2PreparedParticleMissing agent=%d expected_input=%llu proxy=%d proxy_input=%llu"),
          Agent.Identity.AgentId,
          ExpectedInputSequence,
          Worker ? 1 : 0,
          Worker ? Worker->SourceInputSequence : 0);
        return false;
      }
      const FVector WorkerPosition =
        (*Movement)->PredictedPosition + WorkerState.PositionOffset;
      const FVector WorkerVelocity =
        (*Movement)->Velocity + WorkerState.VelocityDelta;
      const double PositionError = FVector::Distance(
        WorkerPosition, (*Legacy)->Position);
      const double VelocityError = FVector::Distance(
        WorkerVelocity, (*Legacy)->Velocity);
      if (Mode == ECrowdDemoWorkerParticleAuthorityMode::Canary
        && (PositionError > 0.001 || VelocityError > 0.001))
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerV2CanaryParticleMismatch agent=%d expected_input=%llu proxy_input=%llu proxy_epoch=%llu position_error_cm=%.6f velocity_error_cmps=%.6f worker_position=%s legacy_position=%s worker_velocity=%s legacy_velocity=%s movement_position=%s movement_velocity=%s particle_offset=%s particle_velocity_delta=%s"),
          Agent.Identity.AgentId,
          ExpectedInputSequence,
          Worker->SourceInputSequence,
          Worker->WorkerEpoch,
          PositionError,
          VelocityError,
          *WorkerPosition.ToString(),
          *(*Legacy)->Position.ToString(),
          *WorkerVelocity.ToString(),
          *(*Legacy)->Velocity.ToString(),
          *(*Movement)->PredictedPosition.ToString(),
          *(*Movement)->Velocity.ToString(),
          *WorkerState.PositionOffset.ToString(),
          *WorkerState.VelocityDelta.ToString());
        return false;
      }
      Final.Position = WorkerPosition;
      Final.Velocity = WorkerVelocity;
      Final.bValid = true;
      ++WorkerOwnedCount;
    }
    OutKinematics.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    return WorkerOwnedCount > 0
      && (Mode != ECrowdDemoWorkerParticleAuthorityMode::Production
        || WorkerOwnedCount == Snapshot.Agents.Num());
  }

  bool RunWorkerFacingTailFromKinematics(
    FCrowdDemoRoundWorkGraphInput GraphInput,
    FCrowdMassMovementPipelineWorkOutput PreparedMovement,
    TArray<FCrowdMassFinalKinematicState> Kinematics,
    TMap<int32, int32> PreviousSettleStepsByAgentId,
    TMap<int32, bool> TerminalOwnerByAgentId,
    FCrowdMassBoundarySnapshot Snapshot,
    FCrowdDemoWorkerMovementTailExecution& OutExecution)
  {
    OutExecution.GraphOutput.Movement =
      MoveTemp(PreparedMovement);
    OutExecution.ObstacleKinematics = MoveTemp(Kinematics);
    FCrowdMassFacingFinalizeWorkInput FacingInput;
    if (!FCrowdDemoRoundWorkGraph::BuildFacingInputFromKinematics(
        GraphInput, Snapshot, OutExecution.GraphOutput.Movement,
        OutExecution.ObstacleKinematics, FacingInput))
      return false;
    TMap<int32, const FCrowdMassPredictedMovement*> MovementById;
    for (const FCrowdMassPredictedMovement& Movement :
      OutExecution.GraphOutput.Movement.MovementPredict.Results)
      MovementById.Add(Movement.AgentId, &Movement);
    TMap<int32, const FCrowdMassFinalKinematicState*> KinematicById;
    for (const FCrowdMassFinalKinematicState& Kinematic :
      OutExecution.ObstacleKinematics)
      KinematicById.Add(Kinematic.AgentId, &Kinematic);
    for (FCrowdFacingInput& Agent : FacingInput.Facing.Agents)
    {
      const FCrowdMassPredictedMovement* const* Movement =
        MovementById.Find(Agent.AgentId);
      const FCrowdMassFinalKinematicState* const* Kinematic =
        KinematicById.Find(Agent.AgentId);
      const int32* Previous =
        PreviousSettleStepsByAgentId.Find(Agent.AgentId);
      const bool* bTerminal =
        TerminalOwnerByAgentId.Find(Agent.AgentId);
      if (!Movement || !Kinematic || !Previous || !bTerminal)
        return false;
      const bool bSettledThisStep = *bTerminal
        && FVector2f(
          (*Kinematic)->Velocity.X,
          (*Kinematic)->Velocity.Y).Size() <= 20.0f
        && ((*Kinematic)->Position
          - (*Movement)->PredictedPosition).Size2D() <= 1.0f;
      const int32 Consecutive =
        bSettledThisStep ? *Previous + 1 : 0;
      Agent.bFinalPositionSettled = Consecutive >= 15;
      OutExecution.ConsecutiveSettleStepsByAgentId.Add(
        Agent.AgentId, Consecutive);
      OutExecution.FinalSettledByAgentId.Add(
        Agent.AgentId, Agent.bFinalPositionSettled);
    }
    OutExecution.Output =
      FCrowdMassFacingFinalizeWork::Run(FacingInput);
    OutExecution.StableHash = CalculateMovementTailHash(
      OutExecution.GraphOutput, OutExecution.Output,
      OutExecution.ObstacleKinematics);
    return OutExecution.Output.bCompleted
      && OutExecution.StableHash != 0;
  }

  bool RunWorkerDownstreamTail(
    FCrowdDemoRoundWorkGraphInput GraphInput,
    FCrowdMassMovementPipelineWorkOutput PreparedMovement,
    TMap<int32, int32> PreviousSettleStepsByAgentId,
    TMap<int32, bool> TerminalOwnerByAgentId,
    FCrowdMassBoundarySnapshot Snapshot,
    const bool bUseObstacleConstraint,
    const bool bMovementAlreadyEnvironmentConstrained,
    const FCrowdDemoSharedFlowFieldConfig ObstacleConfig,
    const float ObstacleFixedStepSeconds,
    FCrowdDemoWorkerMovementTailExecution& OutExecution)
  {
    OutExecution.GraphOutput.Movement =
      MoveTemp(PreparedMovement);
    if (bUseObstacleConstraint)
    {
      if (!OutExecution.GraphOutput.Movement.bCompleted)
        return false;
      const auto& Predicted =
        OutExecution.GraphOutput.Movement.MovementPredict.Results;
      TMap<int32, const FCrowdMassPredictedMovement*> ById;
      for (const FCrowdMassPredictedMovement& Value : Predicted)
      {
        if (!Value.bValid || ById.Contains(Value.AgentId))
          return false;
        ById.Add(Value.AgentId, &Value);
      }
      for (const FCrowdMassBoundaryAgentRecord& Agent
        : Snapshot.Agents)
      {
        const FCrowdMassPredictedMovement* const* Value =
          ById.Find(Agent.Identity.AgentId);
        if (!Value) return false;
        FCrowdMassFinalKinematicState& Kinematic =
          OutExecution.ObstacleKinematics.AddDefaulted_GetRef();
        Kinematic.AgentId = Agent.Identity.AgentId;
        if (bMovementAlreadyEnvironmentConstrained)
        {
          Kinematic.Position = (*Value)->PredictedPosition;
          Kinematic.Velocity = (*Value)->Velocity;
        }
        else
        {
          const FCrowdDemoSharedFlowConstraintResult Result =
            FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
              ObstacleConfig, (*Value)->StartPosition,
              (*Value)->PredictedPosition,
              ObstacleFixedStepSeconds, false);
          Kinematic.Position = Result.Location;
          Kinematic.Velocity = Result.Velocity;
        }
        Kinematic.bValid = true;
      }
      OutExecution.ObstacleKinematics.Sort([](
        const auto& A, const auto& B)
      {
        return A.AgentId < B.AgentId;
      });
      FCrowdMassFacingFinalizeWorkInput FacingInput;
      if (!FCrowdDemoRoundWorkGraph::
        BuildFacingInputFromKinematics(
          GraphInput, Snapshot,
          OutExecution.GraphOutput.Movement,
          OutExecution.ObstacleKinematics, FacingInput))
        return false;
      OutExecution.Output =
        FCrowdMassFacingFinalizeWork::Run(FacingInput);
      OutExecution.StableHash = CalculateMovementTailHash(
        OutExecution.GraphOutput, OutExecution.Output,
        OutExecution.ObstacleKinematics);
      return OutExecution.Output.bCompleted
        && OutExecution.StableHash != 0;
    }
    FCrowdMassParticlePipelineWorkInput ParticleInput;
    if (!OutExecution.GraphOutput.Movement.bCompleted
      || !FCrowdDemoRoundWorkGraph::BuildParticleInput(
        GraphInput, OutExecution.GraphOutput.Movement,
        ParticleInput))
      return false;
    OutExecution.GraphOutput.Particle =
      FCrowdMassParticlePipelineWork::Run(ParticleInput);
    FCrowdMassFacingFinalizeWorkInput FacingInput;
    if (!OutExecution.GraphOutput.Particle.bCompleted
      || !FCrowdDemoRoundWorkGraph::BuildFacingInput(
        GraphInput, OutExecution.GraphOutput.Movement,
        OutExecution.GraphOutput.Particle, FacingInput))
      return false;
    TMap<int32, const FCrowdParticleConstraintResult*> ParticleById;
    for (const FCrowdParticleConstraintResult& Result
      : OutExecution.GraphOutput.Particle.PublishPlan.PreparedResults)
      ParticleById.Add(Result.AgentId, &Result);
    for (FCrowdFacingInput& Agent : FacingInput.Facing.Agents)
    {
      const FCrowdParticleConstraintResult* const* ParticleResult =
        ParticleById.Find(Agent.AgentId);
      const int32* Previous =
        PreviousSettleStepsByAgentId.Find(Agent.AgentId);
      const bool* bTerminal =
        TerminalOwnerByAgentId.Find(Agent.AgentId);
      if (!ParticleResult || !Previous || !bTerminal)
        return false;
      const bool bSettledThisStep = *bTerminal
        && FVector2f((*ParticleResult)->CorrectedVelocity.X,
          (*ParticleResult)->CorrectedVelocity.Y).Size() <= 20.0f
        && (*ParticleResult)->RealizedCorrection.Size2D() <= 1.0f;
      const int32 Consecutive =
        bSettledThisStep ? *Previous + 1 : 0;
      Agent.bFinalPositionSettled = Consecutive >= 15;
      OutExecution.ConsecutiveSettleStepsByAgentId.Add(
        Agent.AgentId, Consecutive);
      OutExecution.FinalSettledByAgentId.Add(
        Agent.AgentId, Agent.bFinalPositionSettled);
    }
    OutExecution.Output =
      FCrowdMassFacingFinalizeWork::Run(FacingInput);
    OutExecution.StableHash = CalculateMovementTailHash(
      OutExecution.GraphOutput, OutExecution.Output);
    return OutExecution.Output.bCompleted
      && OutExecution.StableHash != 0;
  }

  bool RunWorkerMovementTail(
    FCrowdDemoRoundWorkGraphInput GraphInput,
    FCrowdMassMovementPipelineWorkInput MovementInput,
    TMap<int32, int32> PreviousSettleStepsByAgentId,
    TMap<int32, bool> TerminalOwnerByAgentId,
    FCrowdMassBoundarySnapshot Snapshot,
    const bool bUseObstacleConstraint,
    const FCrowdDemoSharedFlowFieldConfig ObstacleConfig,
    const float ObstacleFixedStepSeconds,
    FCrowdDemoWorkerMovementTailExecution& OutExecution)
  {
    return RunWorkerDownstreamTail(
      MoveTemp(GraphInput),
      FCrowdMassMovementPipelineWork::Run(MovementInput),
      MoveTemp(PreviousSettleStepsByAgentId),
      MoveTemp(TerminalOwnerByAgentId),
      MoveTemp(Snapshot), bUseObstacleConstraint, false,
      ObstacleConfig, ObstacleFixedStepSeconds,
      OutExecution);
  }

  uint64 MakeTargetPlanResourceKey(
    const uint32 CapabilityProfileKey,
    const FCrowdDemoTargetRegionFlowPlan& Plan)
  {
    uint32 Low = 2166136261u;
    Low = FoldHash(Low, CapabilityProfileKey);
    Low = FoldHash(Low, Plan.TransportHash);
    Low = FoldHash(Low, static_cast<uint32>(Plan.PlanEpoch));
    uint32 High = 2166136261u;
    High = FoldHash(High, static_cast<uint32>(Plan.BuildFixedStepIndex));
    High = FoldHash(High, static_cast<uint32>(Plan.TargetRevision));
    High = FoldHash(High, Plan.FeasibleGraphHash);
    return (static_cast<uint64>(High) << 32) | Low;
  }

  uint32 BuildAppliedStateDifferenceMask(
    const FCrowdDemoRoundAgentState& Local,
    const FCrowdDemoRoundAgentState& Server)
  {
    uint32 Mask = 0;
    const auto QuantizedEqual = [](const float A, const float B, const float Scale)
    {
      return FMath::RoundToInt(A * Scale) == FMath::RoundToInt(B * Scale);
    };
    if (Local.LifecycleSerial != Server.LifecycleSerial
      || !QuantizedEqual(Local.Location.X, Server.Location.X, 1000.0f)
      || !QuantizedEqual(Local.Location.Y, Server.Location.Y, 1000.0f)
      || !QuantizedEqual(Local.Location.Z, Server.Location.Z, 1000.0f)
      || !QuantizedEqual(Local.Velocity.X, Server.Velocity.X, 1000.0f)
      || !QuantizedEqual(Local.Velocity.Y, Server.Velocity.Y, 1000.0f)
      || !QuantizedEqual(Local.Velocity.Z, Server.Velocity.Z, 1000.0f)
      || !QuantizedEqual(Local.YawDegrees, Server.YawDegrees, 1000.0f)
      || !QuantizedEqual(Local.RadiusCm, Server.RadiusCm, 1000.0f))
      Mask |= 1u << 0;
    if (!QuantizedEqual(Local.Combat.Health, Server.Combat.Health, 100.0f)
      || !QuantizedEqual(Local.Combat.MaxHealth, Server.Combat.MaxHealth, 100.0f)
      || Local.Combat.LifecycleState != Server.Combat.LifecycleState
      || Local.Combat.bAlive != Server.Combat.bAlive)
      Mask |= 1u << 1;
    if (Local.Combat.BusinessState != Server.Combat.BusinessState
      || Local.Combat.BusinessStateRevision != Server.Combat.BusinessStateRevision
      || Local.Combat.BusinessStateEnterFixedStep != Server.Combat.BusinessStateEnterFixedStep
      || Local.Combat.TargetAgentId != Server.Combat.TargetAgentId
      || Local.Combat.TargetLifecycleSerial != Server.Combat.TargetLifecycleSerial)
      Mask |= 1u << 2;
    if (Local.Combat.AttackPhase != Server.Combat.AttackPhase
      || Local.Combat.AttackPhaseEnterFixedStep != Server.Combat.AttackPhaseEnterFixedStep
      || Local.Combat.CooldownEndFixedStep != Server.Combat.CooldownEndFixedStep
      || Local.Combat.LockedTargetAgentId != Server.Combat.LockedTargetAgentId
      || Local.Combat.LockedTargetLifecycleSerial != Server.Combat.LockedTargetLifecycleSerial
      || Local.Combat.FireSequence != Server.Combat.FireSequence
      || Local.Combat.bFireRequestIssued != Server.Combat.bFireRequestIssued)
      Mask |= 1u << 3;
    if (Local.Combat.ReactiveMode != Server.Combat.ReactiveMode
      || !QuantizedEqual(Local.Combat.HorizontalReactiveVelocity.X,
        Server.Combat.HorizontalReactiveVelocity.X, 10.0f)
      || !QuantizedEqual(Local.Combat.HorizontalReactiveVelocity.Y,
        Server.Combat.HorizontalReactiveVelocity.Y, 10.0f)
      || !QuantizedEqual(Local.Combat.VerticalReactiveVelocityCmps,
        Server.Combat.VerticalReactiveVelocityCmps, 10.0f)
      || Local.Combat.ReactiveStartFixedStep != Server.Combat.ReactiveStartFixedStep
      || Local.Combat.ReactiveEndFixedStep != Server.Combat.ReactiveEndFixedStep
      || Local.Combat.ReactiveRevision != Server.Combat.ReactiveRevision
      || Local.Combat.ApexCount != Server.Combat.ApexCount
      || Local.Combat.LandingCount != Server.Combat.LandingCount)
      Mask |= 1u << 4;
    if (Local.Combat.HitFlashRevision != Server.Combat.HitFlashRevision
      || Local.Combat.HitFlashProfileKey != Server.Combat.HitFlashProfileKey
      || Local.Combat.LastConsumedHitEventId != Server.Combat.LastConsumedHitEventId)
      Mask |= 1u << 5;
    if (Local.Combat.VisualState != Server.Combat.VisualState
      || Local.Combat.VisualRevision != Server.Combat.VisualRevision
      || Local.Combat.VisualPhaseSeed != Server.Combat.VisualPhaseSeed)
      Mask |= 1u << 6;
    return Mask;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueBootstrap(const FCrowdDemoRoundBootstrapPacket& Packet)
{
  if (Packet.bValid != 0 && (!bBootstrapApplied || Packet.Revision >= PendingBootstrap.Revision))
  {
    PendingBootstrap = Packet;
    if (!bPlanActive)
    {
      LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundPlan(const FCrowdDemoRoundPlanPacket& Packet)
{
  if (Packet.bValid != 0 && (!bPlanActive || Packet.Revision > ActivePlan.Revision))
  {
    PendingPlans.Add(Packet.Revision, Packet);
    LastCompareMetrics.RoundPlanRevisionSeen = FMath::Max(LastCompareMetrics.RoundPlanRevisionSeen, Packet.Revision);
    // The old round-end boundary may already have been inspected before the
    // next plan arrives. Re-open it just as late RoundResult arrival does.
    LastClaimedPlanApplyBoundarySequence = MAX_uint64;
  }
}

float FCrowdDemoRoundErrorSeries::GetMax() const
{
  float Maximum = -1.0f;
  for (const float Sample : CheckpointP95Samples)
  {
    Maximum = FMath::Max(Maximum, Sample);
  }
  return Maximum;
}

float FCrowdDemoRoundErrorSeries::GetExpansionFromFirst() const
{
  if (CheckpointP95Samples.Num() < 2)
  {
    return 0.0f;
  }
  float LaterMax = CheckpointP95Samples[1];
  for (int32 Index = 2; Index < CheckpointP95Samples.Num(); ++Index)
  {
    LaterMax = FMath::Max(LaterMax, CheckpointP95Samples[Index]);
  }
  return FMath::Max(0.0f, LaterMax - CheckpointP95Samples[0]);
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryClaimPlanApplyBoundary()
{
  if (LastClaimedPlanApplyBoundarySequence == PlanApplyBoundarySequence)
  {
    return false;
  }
  LastClaimedPlanApplyBoundarySequence = PlanApplyBoundarySequence;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::EnsureFormationIndexCache(
  const TConstArrayView<int32> AgentIds)
{
  uint64 MembershipHash = 1469598103934665603ull ^ static_cast<uint64>(AgentIds.Num());
  for (const int32 AgentId : AgentIds)
  {
    uint64 Mixed = static_cast<uint64>(static_cast<uint32>(AgentId)) + 0x9e3779b97f4a7c15ull;
    Mixed = (Mixed ^ (Mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
    Mixed = (Mixed ^ (Mixed >> 27)) * 0x94d049bb133111ebull;
    Mixed ^= Mixed >> 31;
    MembershipHash ^= Mixed;
  }
  if (FormationMembershipCount == AgentIds.Num()
    && FormationMembershipHash == MembershipHash
    && FormationIndexByAgentId.Num() == AgentIds.Num())
  {
    return;
  }

  TArray<int32> SortedAgentIds;
  SortedAgentIds.Append(AgentIds.GetData(), AgentIds.Num());
  SortedAgentIds.Sort();
  FormationIndexByAgentId.Empty(SortedAgentIds.Num());
  for (int32 Index = 0; Index < SortedAgentIds.Num(); ++Index)
  {
    FormationIndexByAgentId.Add(SortedAgentIds[Index], Index);
  }
  FormationMembershipCount = AgentIds.Num();
  FormationMembershipHash = MembershipHash;
  ++FormationCacheRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::QueueRoundResult(const FCrowdDemoRoundResultPacket& Packet)
{
  if (Packet.bValid != 0)
  {
    const bool bNewResult = !PendingResults.Contains(Packet.CheckpointRevision);
    PendingResults.Add(Packet.CheckpointRevision, Packet);
    if (!bNewResult)
    {
      return;
    }
    ++RoundResultPipelineQueuedCount;
    // Network work may arrive after the stationary round-end boundary was
    // already inspected. Re-open that boundary without advancing simulation.
    LastClaimedPlanApplyBoundarySequence = MAX_uint64;
    UE_LOG(
      LogTemp,
      Display,
      TEXT("CrowdDemoRoundResultTransport role=client stage=pipeline_queued round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d simulated_server_time=%.6f local_plan_terminal=%.6f terminal_delta=%.6f plan_apply_boundary=%llu last_claimed_boundary=%llu source=MassPipeline"),
      Packet.RoundId,
      Packet.CheckpointRevision,
      Packet.Agents.Num(),
      RoundResultPipelineQueuedCount,
      SimulatedServerTimeSeconds,
      ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds,
      ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds
        - SimulatedServerTimeSeconds,
      PlanApplyBoundarySequence,
      LastClaimedPlanApplyBoundarySequence);
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::PeekBootstrap(FCrowdDemoRoundBootstrapPacket& OutPacket) const
{
  if (bBootstrapApplied || PendingBootstrap.bValid == 0)
  {
    return false;
  }
  OutPacket = PendingBootstrap;
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkBootstrapApplied(const int32 AgentCount)
{
  bBootstrapApplied = true;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
  RoundInputHash = 0;
  RoundInitialStateHash = 0;
  RoundResetCount = 0;
  RoundTransitionOrderViolationCount = 0;
  DynamicFlowAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.DynamicAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.IntegrationRebuildCount = 0;
  DynamicFlowIntegrationRebuildCount = 0;
  DynamicFlowRoundHash = 2166136261u;
  DynamicFlowRoundHashFixedStepIndex = INDEX_NONE;
  bDynamicFlowIntegrationCacheInvalidated = false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopDueRoundPlan(
  const float BoundaryServerTimeSeconds,
  FCrowdDemoRoundPlanPacket& OutPacket)
{
  if (bPlanActive && GetWorld()
    && SimulatedServerTimeSeconds + KINDA_SMALL_NUMBER
      >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds)
  {
    const bool bOldRoundFrozen = GetWorld()->GetNetMode() == NM_Client
      ? LastCompareMetrics.CompletedRoundCount >= ActivePlan.RoundId
      : LastBuiltResultRoundId >= ActivePlan.RoundId;
    if (!bOldRoundFrozen)
    {
      return false;
    }
  }
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER
      && (SelectedRevision == INDEX_NONE || Pair.Key < SelectedRevision))
    {
      SelectedRevision = Pair.Key;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingPlans.FindAndRemoveChecked(SelectedRevision);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::PopRoundResultForBoundary(FCrowdDemoRoundResultPacket& OutPacket)
{
  int32 SelectedRevision = INDEX_NONE;
  for (const TPair<int32, FCrowdDemoRoundResultPacket>& Pair : PendingResults)
  {
    if (Pair.Value.RoundId == GetCurrentRoundId()
      && Pair.Value.Revision == GetCurrentPlanRevision())
    {
      SelectedRevision = Pair.Key;
      break;
    }
  }
  if (SelectedRevision == INDEX_NONE)
  {
    return false;
  }
  OutPacket = PendingResults.FindAndRemoveChecked(SelectedRevision);
  ++RoundResultBoundaryAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundResultTransport role=client stage=boundary_applied round_id=%d checkpoint_revision=%d agents=%d pipeline_queued_count=%d boundary_applied_count=%d source=MassPipeline"),
    OutPacket.RoundId,
    OutPacket.CheckpointRevision,
    OutPacket.Agents.Num(),
    RoundResultPipelineQueuedCount,
    RoundResultBoundaryAppliedCount);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasDueRoundPlan(const float BoundaryServerTimeSeconds) const
{
  for (const TPair<int32, FCrowdDemoRoundPlanPacket>& Pair : PendingPlans)
  {
    if (Pair.Value.StartServerTimeSeconds <= BoundaryServerTimeSeconds + KINDA_SMALL_NUMBER)
    {
      return true;
    }
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::HasDueAuthorityInput(
  const float BoundaryServerTimeSeconds) const
{
  if ((!bBootstrapApplied && PendingBootstrap.bValid != 0)
    || HasDueRoundPlan(BoundaryServerTimeSeconds))
  {
    return true;
  }
  for (const TPair<int32, FCrowdDemoRoundResultPacket>& Pair
    : PendingResults)
  {
    if (Pair.Value.RoundId == GetCurrentRoundId()
      && Pair.Value.Revision == GetCurrentPlanRevision())
    {
      return true;
    }
  }
  return false;
}

void UCrowdDemoRoundSimPipelineSubsystem::ActivatePlan(
  const FCrowdDemoRoundPlanPacket& Packet,
  const int32 AgentCount,
  const bool bLate)
{
  ++BoundaryGeneration;
  if (BoundaryGeneration == 0)
    BoundaryGeneration = 1;
  if (bPlanActive && Packet.Revision > ActivePlan.Revision + 1)
  {
    LastCompareMetrics.RoundPlanGapCount += Packet.Revision - ActivePlan.Revision - 1;
  }
  ActivePlan = Packet;
  bPlanActive = true;
  bStepInProgress = false;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
  PreparedTargetResourceSlots.Reset();
  PreparedMovementBoundaryCommit = {};
  PreparedRuntimeSharedFlowOutputs.Reset();
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedPlannerDecisionHash = 0;
  PreparedReactiveMotionSteps.Reset();
  PreparedCombatBoundaryCommit = {};
  PreparedRuntimeComposedGuidance.Reset();
  PreparedRuntimePredictedMovements.Reset();
  PreparedRuntimeParticleResults.Reset();
  PreparedRuntimeFinalKinematics.Reset();
  bPreparedRuntimeFinalKinematicsWorkerOwned = false;
  PreparedRuntimeFacingResults.Reset();
  PreparedFacingRollbackFacts.Reset();
  PreparedParticleDiagnosticCommit = {};
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  for (TArray<float>& Samples : RoundPerformanceStageMsSamples)
  {
    Samples.Reset();
  }
  FixedStepPipelineMsSamples.Reset();
  FixedStepsPerGameFrameSamples.Reset();
  FixedStepBacklogMsSamples.Reset();
  BoundaryWorkerQueueMsSamples.Reset();
  BoundaryWorkerRunMsSamples.Reset();
  BoundaryWorkerCriticalPathMsSamples.Reset();
  RollbackReplayMsSamples.Reset();
  PerformanceCatchupFrameCount = 0;
  PerformanceCatchupCpuBudgetHitCount = 0;
  PerformanceCatchupCpuBudgetConsecutiveCount = 0;
  PerformanceCatchupCpuBudgetConsecutiveMax = 0;
  PerformanceMaxFixedStepsPerFrameHitCount = 0;
  PerformanceFixedStepBacklogMsMax = 0.0f;
  PerformanceRoundWallStartSeconds = FPlatformTime::Seconds();
  PerformanceRoundSimStartSeconds = Packet.StartServerTimeSeconds;
  PendingRollbackReplaySteps = 0;
  PendingRollbackReplayMilliseconds = 0.0f;
  PerformanceZeroErrorRollbackReplayCount = 0;
  PerformanceTargetTopologyBuildCount = 0;
  PerformanceTargetTopologyCacheHitCount = 0;
  PerformanceTargetDemandFullBuildCount = 0;
  PerformanceTargetDemandPopulationUpdateCount = 0;
  BoundaryPendingFrameCount = 0;
  WorkerV2MovementShadowCompareCount = 0;
  WorkerV2MovementShadowMismatchCount = 0;
  WorkerV2MovementPositionErrorMaxCm = 0.0;
  WorkerV2MovementVelocityErrorMaxCmps = 0.0;
  WorkerV2MovementYawErrorMaxDegrees = 0.0;
  WorkerV2MovementStageCompareCount = 0;
  WorkerV2MovementStageMismatchCount = 0;
  WorkerV2MovementStageStaleSkipCount = 0;
  WorkerV2MovementStageLastExpectedInputSequence = 0;
  WorkerV2MovementStagePositionErrorMaxCm = 0.0;
  WorkerV2MovementStageVelocityErrorMaxCmps = 0.0;
  WorkerV2MovementStageTimeErrorMaxSeconds = 0.0;
  WorkerV2MovementStageLastPositionErrorMaxCm = 0.0;
  WorkerV2MovementStageLastVelocityErrorMaxCmps = 0.0;
  WorkerV2MovementStageLastMismatchCount = 0;
  PendingWorkerV2MovementExpectations.Reset();
  BoundaryStaleResultCount = 0;
  BoundaryOrdinaryBlockWaitCount = 0;
  RoundInputHash = 0;
  RoundInitialStateHash = 0;
  RoundResetCount = 0;
  RoundTransitionOrderViolationCount = 0;
  DynamicFlowAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.DynamicAnchorCellKey = INDEX_NONE;
  RuntimeSharedFlowResource.IntegrationRebuildCount = 0;
  DynamicFlowIntegrationRebuildCount = 0;
  DynamicFlowRoundHash = 2166136261u;
  DynamicFlowRoundHashFixedStepIndex = INDEX_NONE;
  bDynamicFlowIntegrationCacheInvalidated = false;
  bTargetStabilityDiagnosticPlanEnabled = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTargetStabilityDiagnostic"))
    && Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Packet.Rules.TargetRegionTransportSettings.bEnabled != 0;
  bTargetRegionPlanLifecycleDiagnosticPlanEnabled = FParse::Param(
      FCommandLine::Get(), TEXT("CrowdDemoTargetRegionPlanLifecycleDiagnostic"))
    && Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && Packet.Rules.TargetRegionTransportSettings.bEnabled != 0;
  CurrentFixedStepSeconds = FMath::Max(1.0f / 120.0f, Packet.Rules.FixedStepSeconds);
  if (LastCompareMetrics.RoundPlanAppliedCount == 0 || SimulatedServerTimeSeconds < Packet.StartServerTimeSeconds)
  {
    SimulatedServerTimeSeconds = Packet.StartServerTimeSeconds;
  }
  ++LastCompareMetrics.RoundPlanAppliedCount;
  LastCompareMetrics.CurrentRoundId = Packet.RoundId;
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.RoundBootstrapAgentCount = AgentCount;
  if (bLate)
  {
    ++LastCompareMetrics.RoundPlanLateCount;
  }
  LastBuiltResultRoundId = FMath::Min(LastBuiltResultRoundId, Packet.RoundId - 1);
  CorrectionIntervalPositionP95Samples.Reset();
  CorrectionIntervalPositionMaxSamples.Reset();
  LastCompareMetrics.CorrectionIntervalPositionErrorCmP95 = -1.0f;
  LastCompareMetrics.CorrectionIntervalPositionErrorCmMax = -1.0f;
  if (IsFlowScenario(Packet.Rules.Scenario))
  {
    SoftPressureRollbackHistory.Reset();
    SoftPressureRollbackSnapshotHitCount = 0;
    SoftPressureRollbackSnapshotMissCount = 0;
    SoftPressureRollbackAgentMismatchCount = 0;
    SoftPressureRollbackReplayedStepCount = 0;
    FlowGoalReachedAgentIds.Reset();
    FlowWallPassAgentIds.Reset();
    FlowCorridorExitAgentIds.Reset();
    FlowTurnExitAgentIds.Reset();
    FlowLowSpeedSecondsByAgentId.Reset();
    FlowCorridorDeadlockAgentIds.Reset();
    LastCompareMetrics.FlowUnreachableAgentCount = 0;
    LastCompareMetrics.FlowGoalReachedCount = 0;
    LastCompareMetrics.FlowWallPassCount = 0;
    LastCompareMetrics.FlowCorridorExitCount = 0;
    LastCompareMetrics.FlowTurnExitCount = 0;
    LastCompareMetrics.ServerObstaclePenetrationCount = 0;
    LastCompareMetrics.ClientSimObstaclePenetrationCount = 0;
    LastCompareMetrics.CorridorDeadlockAgentCount = 0;
  }
  if (Packet.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure)
  {
    TargetRegionPlanResources.Reset();
    ParticleSolverMillisecondsSamples.Reset();
    LastParticleCandidateSummary = FCrowdDemoParticleConstraintSummary();
    LastParticleAppliedSummary = FCrowdDemoParticleConstraintSummary();
    PreparedLocalPredictiveResults.Reset();
    LocalPredictiveGrantStates.Reset();
    LastLocalPredictiveSummary = FCrowdDemoLocalPredictiveSummary();
    LocalPredictiveDiagnosticFrame = FCrowdDemoLocalPredictiveDiagnosticFrame();
    LocalPredictiveComponentFixture = FCrowdDemoLocalPredictiveComponentFixture();
    LocalPredictiveRoundHash = 2166136261u;
    LocalPredictiveSampleCount = 0;
    LocalPredictiveInvalidStepCount = 0;
    GuidanceCandidateRoundHash = 2166136261u;
    GuidanceComposeRoundHash = 2166136261u;
    GuidanceComposeSampleCount = 0;
    ParticleCandidateStateHash = 2166136261u;
    ParticleAppliedStateHash = 2166136261u;
    ParticleInvalidStepCount = 0;
    ParticleGlobalFallbackStepCount = 0;
    ParticleStepCount = 0;
    CrossProfileHardViolationCount = 0;
    CrossProfileSweptViolationCount = 0;
    ParticleSettlingWindowCount = 0;
    ParticleSettlingSteps = INDEX_NONE;
    ParticlePreviousSoftErrorP95 = -1.0f;
    bParticleConstraintRunFailure = false;
    ParticleFailureFixture = FCrowdDemoParticleFailureFixture();
    if (UWorld* World = GetWorld())
      if (UCrowdDemoMassSubsystem* MassSubsystem =
        World->GetSubsystem<UCrowdDemoMassSubsystem>())
        MassSubsystem->ResetProjectileStates();
    OutgoingProjectileVisualEvents.Reset();
    ProjectileMetrics = FCrowdDemoProjectileMetrics();
    ProjectileMetrics.bValid = 1;
    OpenSpawnRelaxationLayout = {};
    OpenSpawnRelaxationRuntime = {};
    OpenCohortMovementLayout = {};
    OpenCohortMovementProgress = {};
    BidirectionalSwapLayout = {};
    BidirectionalSwapProgress = {};
    ValidCorridorTransitLayout = {};
    ValidCorridorTransitProgress = {};
    SoftPressureRouteDiagnosticRuntime = {};
    SoftPressureRouteDiagnosticSummary = {};
    TargetFact = FCrowdDemoTargetFact();
    TargetStabilityRuntime = {};
    TargetStabilityRuntime.Settings.ExpectedAgentCount = AgentCount;
    TargetStabilityRuntime.Settings.PositionQuantumCm =
      Packet.Rules.ParticlePositionQuantumCm;
    TargetStabilitySummary = {};
    PreparedTargetRegionTopology = {};
    TargetRegionTopologySummary = {};
    PreparedTargetRegionAgents.Reset();
    PreparedTargetRegionDemand = {};
    PreparedTargetRegionPlan = {};
    TargetRegionQuotaExecution = {};
    TargetRegionPlanValidation = {};
    PreparedTargetRegionGuidance.Reset();
    TargetRegionGuidanceSummary = {};
    TargetRegionTopologyRoundHash = 2166136261u;
    TargetRegionDemandRoundHash = 2166136261u;
    TargetRegionTransportRoundHash = 2166136261u;
    TargetRegionGuidanceRoundHash = 2166136261u;
    TargetRegionPlanRebuildCount = 0;
    TargetRegionLifetimeRebuildCount = 0;
    TargetRegionTargetRebuildCount = 0;
    TargetRegionEnvironmentRebuildCount = 0;
    TargetRegionMembershipRebuildCount = 0;
    TargetRegionDemandSatisfiedRebuildCount = 0;
    TargetRegionPathInvalidRebuildCount = 0;
    TargetRegionSolverMillisecondsSamples.Reset();
    bTargetRegionRoundValid = true;
    TargetRegionInvalidStepCount = 0;
    TargetRegionLastInvalidStep = INDEX_NONE;
    TargetRegionValidationFailureCount = 0;
    TargetRegionValidationRoundHash = 2166136261u;
    TargetRegionGuidanceUnroutedStepCount = 0;
    TargetRegionGuidanceUnroutedAgentSampleCount = 0;
    TargetRegionGuidanceUnroutedAgentMax = 0;
    TargetRegionGuidanceFirstFailureStep = INDEX_NONE;
    TargetRegionGuidanceFirstFailureAgentId = INDEX_NONE;
    bTargetRegionFailureFixtureValid = false;
    TargetRegionFailureFixtureStep = INDEX_NONE;
    TargetRegionFailureFixtureKind = 0;
    TargetRegionFailureFixtureAgentId = INDEX_NONE;
    TargetRegionFailureFixtureCellKey = INDEX_NONE;
    TargetRegionFailureFixtureHash = 0;
    TargetRegionCapabilityCohorts.Reset();
    CapabilityProfileSummary = {};
    CapabilityCohortRebuildCount = 0;
    TargetRegionPlanLifecycleSummary = {};
    TargetRegionPlanLifecycleFixture = {};
    LastCompareMetrics.InitialOverlapPairCount = 0;
  }
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundInit role=%s round_id=%d revision=%d previous_checkpoint_revision=%d agents=%d start_server_time=%.3f duration=%.3f nominal_duration=%.3f completion_grace=%.3f fixed_step=%.4f scenario=%d plan_late=%d source=MassPipeline"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    Packet.RoundId,
    Packet.Revision,
    Packet.PreviousCheckpointRevision,
    AgentCount,
    Packet.StartServerTimeSeconds,
    Packet.DurationSeconds,
    Packet.NominalDurationSeconds,
    Packet.CompletionGraceSeconds,
    CurrentFixedStepSeconds,
    static_cast<int32>(Packet.Rules.Scenario),
    bLate ? 1 : 0);
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureSharedFlowField(
  const FCrowdDemoSharedFlowFieldConfig& Config)
{
  FCrowdMassSharedFlowBuildInput Input;
  Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
  const FCrowdMassSharedFlowBuildOutput Output =
    FCrowdMassSharedFlowWork::EnsureResource(
      Input, RuntimeSharedFlowResource);
  if (!Output.bValid) return false;
  if (Output.bFieldRebuilt)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
    SharedFlowFieldRebuildCount = RuntimeSharedFlowResource.FieldRebuildCount;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoSharedFlowField: role=%s revision=%d hash=%u rebuild_count=%d cells=%d blocked=%d goal_cell=%d"),
      GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
      Config.Revision,
      SharedFlowField.BuildHash,
      SharedFlowFieldRebuildCount,
      SharedFlowField.Width * SharedFlowField.Height,
      SharedFlowField.BlockedCellCount,
      SharedFlowField.GoalCellIndex);
  }
  else if (!SharedFlowField.IsValid()
    || SharedFlowField.BuildHash != RuntimeSharedFlowResource.Field.BuildHash)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
  }
  LastCompareMetrics.FlowFieldRevision = SharedFlowField.Config.Revision;
  LastCompareMetrics.FlowFieldBuildHash = SharedFlowField.BuildHash;
  LastCompareMetrics.FlowFieldRebuildCount = SharedFlowFieldRebuildCount;
  LastCompareMetrics.FlowBlockedCellCount = SharedFlowField.BlockedCellCount;
  return SharedFlowField.IsValid();
}

bool UCrowdDemoRoundSimPipelineSubsystem::PublishBoundarySnapshot(
  FCrowdMassBoundarySnapshot&& Snapshot,
  TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
  TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts)
{
  TArray<FCrowdDemoRoundBoundaryBusinessFact> BusinessFacts;
  BusinessFacts.Reserve(Snapshot.Agents.Num());
  for (FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
  {
    if (!Agent.AgentFacts.StableEntityRef.IsValid())
    {
      const FCrowdStableEntityRef FixtureRef = {
        1u,
        static_cast<uint64>(FMath::Max(0, Agent.Identity.AgentId)) + 1u,
        FMath::Max<uint32>(1u, Agent.Identity.LifecycleSerial)};
      Agent.Identity.SetStableEntityRef(FixtureRef);
      Agent.AgentFacts.StableEntityRef = FixtureRef;
    }
    FCrowdDemoRoundBoundaryBusinessFact& Fact =
      BusinessFacts.AddDefaulted_GetRef();
    Fact.EntityRef = Agent.AgentFacts.StableEntityRef;
    Fact.AgentId = Agent.Identity.AgentId;
  }
  return PublishBoundarySnapshot(
    MoveTemp(Snapshot), MoveTemp(FormationFacts), MoveTemp(FacingFacts),
    MoveTemp(BusinessFacts));
}

bool UCrowdDemoRoundSimPipelineSubsystem::PublishBoundarySnapshot(
  FCrowdMassBoundarySnapshot&& Snapshot,
  TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
  TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts,
  TArray<FCrowdDemoRoundBoundaryBusinessFact>&& BusinessFacts)
{
  if (!Snapshot.bValid
    || Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Snapshot.PlanRevision != GetCurrentPlanRevision()
    || Snapshot.Agents.Num() != FormationFacts.Num()
    || Snapshot.Agents.Num() != FacingFacts.Num()
    || Snapshot.Agents.Num() != BusinessFacts.Num())
    return false;
  FormationFacts.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  FacingFacts.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  BusinessFacts.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
    if (FormationFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || FacingFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || BusinessFacts[Index].EntityRef
        != Snapshot.Agents[Index].AgentFacts.StableEntityRef
      || BusinessFacts[Index].AgentId
        != Snapshot.Agents[Index].Identity.AgentId
      || FacingFacts[Index].ConsecutiveFinalSettleSteps < 0)
      return false;
  BoundarySnapshot = MoveTemp(Snapshot);
  BoundaryFormationFacts = MoveTemp(FormationFacts);
  BoundaryFacingFacts = MoveTemp(FacingFacts);
  BoundaryBusinessFacts = MoveTemp(BusinessFacts);
  CurrentStepMassDirtyEntityRefs.Reset(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
    CurrentStepMassDirtyEntityRefs.Add(Agent.AgentFacts.StableEntityRef);
  bBootstrapBoundarySnapshotPending = true;
  ++FullBoundarySnapshotPublishCount;
  ++FullBoundarySnapshotHashCount;
  WorkerProxySnapshotBaselineHash = BoundarySnapshot.StableHash;
  ResetBoundaryDerivedStateAfterPublish();
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::
ResetBoundaryDerivedStateAfterPublish()
{
  MovementFinalizeAppliedFixedStepIndex = INDEX_NONE;
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  PreparedTargetResourceSlots.Reset();
  PreparedMovementBoundaryCommit = {};
  PreparedRuntimeSharedFlowOutputs.Reset();
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
  PreparedRuntimeComposedGuidance.Reset();
  PreparedRuntimePredictedMovements.Reset();
  PreparedRuntimeParticleResults.Reset();
  PreparedRuntimeFinalKinematics.Reset();
  bPreparedRuntimeFinalKinematicsWorkerOwned = false;
  PreparedRuntimeFacingResults.Reset();
  PreparedFacingRollbackFacts.Reset();
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
TryPublishWorkerProxyBoundarySnapshot()
{
  const auto Reject = [this](
    const TCHAR* Reason,
    const FCrowdStableEntityRef EntityRef = {})
  {
    ++WorkerProxyGatherFallbackCount;
    if (WorkerProxyGatherFallbackCount <= 8
      || WorkerProxyGatherFallbackCount % 300 == 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoWorkerProxyInputFallback reason=%s count=%llu step=%d entity=%u:%llu:%u source=WorkerInputSync"),
        Reason, WorkerProxyGatherFallbackCount,
        GetCurrentFixedStepIndex(), EntityRef.ProviderId,
        EntityRef.StableEntityId, EntityRef.LifecycleSerial);
    }
    return false;
  };
  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem = World
    ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>() : nullptr;
  if (!RuntimeSubsystem || !bStepInProgress
    || !BoundarySnapshot.bValid
    || BoundarySnapshot.Agents.IsEmpty()
    || BoundaryFormationFacts.Num() != BoundarySnapshot.Agents.Num()
    || BoundaryFacingFacts.Num() != BoundarySnapshot.Agents.Num()
    || BoundaryBusinessFacts.Num() != BoundarySnapshot.Agents.Num())
    return Reject(TEXT("bootstrap_or_invalid_cache"));
  FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const uint64 AppliedInputSequence =
    Proxy.GetMetrics().LastAppliedInputSequence;
  if (AppliedInputSequence == 0)
    return Reject(TEXT("proxy_not_applied"));
  const TConstArrayView<FCrowdStableEntityRef> StableEntityView =
    Proxy.GetStableEntityView();
  if (StableEntityView.Num() != BoundarySnapshot.Agents.Num())
    return Reject(TEXT("entity_set_count"));
  const FCrowdWorkerResultApplyDirtyBatch* DirtyBatch =
    Proxy.PeekDirtyBatch();
  if (!DirtyBatch
    || DirtyBatch->Generation != Proxy.GetMetrics().Generation
    || DirtyBatch->PublishSequence
      != Proxy.GetMetrics().LastConsumedPublishSequence
    || DirtyBatch->LastAppliedInputSequence != AppliedInputSequence)
    return Reject(TEXT("dirty_batch_missing_or_stale"));

  struct FValidatedDirtyRefresh
  {
    int32 Index = INDEX_NONE;
    bool bMovement = false;
    FCrowdWorkerMovementState Movement;
    bool bBusiness = false;
    FCrowdDemoRoundBoundaryBusinessFact Business;
  };
  TArray<FValidatedDirtyRefresh> ValidatedDirty;
  ValidatedDirty.Reserve(DirtyBatch->Records.Num());
  TMap<int32, int32> ValidatedIndexByStableSlot;
  ValidatedIndexByStableSlot.Reserve(DirtyBatch->Records.Num());
  for (const FCrowdWorkerResultApplyDirtyRecord& Dirty :
    DirtyBatch->Records)
  {
    const FCrowdStableEntityRef EntityRef =
      Dirty.DomainState.EntityRef;
    if (Dirty.StableSlot < 0
      || Dirty.StableSlot >= StableEntityView.Num()
      || StableEntityView[Dirty.StableSlot] != EntityRef
      || BoundarySnapshot.Agents[Dirty.StableSlot].
        AgentFacts.StableEntityRef != EntityRef
      || BoundaryBusinessFacts[Dirty.StableSlot].EntityRef != EntityRef
      || Dirty.DomainState.PublishSequence > DirtyBatch->PublishSequence
      || Dirty.DomainState.SourceInputSequence
        > DirtyBatch->LastAppliedInputSequence)
      return Reject(TEXT("dirty_record_contract"), EntityRef);
    int32* RefreshIndex =
      ValidatedIndexByStableSlot.Find(Dirty.StableSlot);
    if (!RefreshIndex)
    {
      const int32 NewIndex = ValidatedDirty.AddDefaulted();
      ValidatedIndexByStableSlot.Add(Dirty.StableSlot, NewIndex);
      RefreshIndex = ValidatedIndexByStableSlot.Find(Dirty.StableSlot);
      ValidatedDirty[NewIndex].Index = Dirty.StableSlot;
      ValidatedDirty[NewIndex].Business =
        BoundaryBusinessFacts[Dirty.StableSlot];
    }
    FValidatedDirtyRefresh& Refresh = ValidatedDirty[*RefreshIndex];
    if (Dirty.DomainState.Field == ECrowdWorkerField::Facing)
    {
      if (!FCrowdWorkerMovementStateCodec::Decode(
          Dirty.DomainState.State.Payload, Refresh.Movement))
        return Reject(TEXT("facing_dirty_invalid"), EntityRef);
      Refresh.bMovement = true;
      Refresh.bBusiness = true;
      Refresh.Business.YawDegrees = Refresh.Movement.YawDegrees;
    }
    else if (Dirty.DomainState.Field == ECrowdWorkerField::Combat
      && Refresh.Business.bHasCombatCapability)
    {
      FCrowdWorkerCombatState WorkerCombat;
      FCrowdDemoCombatAgentState Combat;
      if (!FCrowdWorkerCombatStateCodec::Decode(
          Dirty.DomainState.State.Payload, WorkerCombat)
        || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
          WorkerCombat.HostState, Combat)
        || !ApplyWorkerCombatState(Combat, Refresh.Business))
        return Reject(TEXT("combat_dirty_invalid"), EntityRef);
      Refresh.bBusiness = true;
    }
  }

  for (FValidatedDirtyRefresh& Refresh : ValidatedDirty)
  {
    if (Refresh.bMovement)
    {
      FCrowdMassSimulationStateFragment& State =
        BoundarySnapshot.Agents[Refresh.Index].State;
      State.Position = Refresh.Movement.Position;
      State.Velocity = Refresh.Movement.Velocity;
      State.YawDegrees = Refresh.Movement.YawDegrees;
      State.PlanRevision = GetCurrentPlanRevision();
      State.bInitialized = true;
    }
    if (Refresh.bBusiness)
      BoundaryBusinessFacts[Refresh.Index] = MoveTemp(Refresh.Business);
  }
  CurrentStepMassDirtyEntityRefs.Reset(ValidatedDirty.Num());
  for (const FValidatedDirtyRefresh& Refresh : ValidatedDirty)
  {
    CurrentStepMassDirtyEntityRefs.Add(
      StableEntityView[Refresh.Index]);
  }
  CurrentStepMassDirtyEntityRefs.Sort();
  BoundarySnapshot.FixedStepIndex = GetCurrentFixedStepIndex();
  BoundarySnapshot.PlanRevision = GetCurrentPlanRevision();
  const bool bFullSnapshotHash = !IsFullWorkerProductionMode()
    || FParse::Param(FCommandLine::Get(),
      TEXT("CrowdDemoFullBoundarySnapshotDiagnostic"))
    || GetCurrentFixedStepIndex()
      % BoundarySnapshotCheckpointCadenceTicks == 0;
  if (bFullSnapshotHash)
  {
    if (!FCrowdMassRuntimeBridge::RefreshBoundarySnapshot(BoundarySnapshot))
      return Reject(TEXT("snapshot_refresh"));
    ++FullBoundarySnapshotHashCount;
  }
  else
  {
    if (!FCrowdMassRuntimeBridge::AdvanceBoundarySnapshotEpochToken(
        BoundarySnapshot, WorkerProxySnapshotBaselineHash,
        GetCurrentFixedStepIndex(), GetCurrentPlanRevision(),
        AppliedInputSequence))
      return Reject(TEXT("snapshot_epoch_token"));
    ++BoundarySnapshotEpochTokenCount;
  }
  if (!Proxy.AcknowledgeDirtyBatch(DirtyBatch->PublishSequence))
    return Reject(TEXT("dirty_batch_ack"));
  ResetBoundaryDerivedStateAfterPublish();
  bCurrentStepUsedWorkerProxySnapshot = true;
  ++WorkerProxyGatherBypassCount;
  ++WorkerProxyInPlaceRefreshCount;
  if (WorkerProxyGatherBypassCount == 1
    || WorkerProxyGatherBypassCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerProxyInputCheckpoint bypassed_mass_gathers=%llu in_place_snapshot_refreshes=%llu full_snapshot_publishes=%llu full_snapshot_hashes=%llu epoch_tokens=%llu step=%d agents=%d source=WorkerInputSync"),
      WorkerProxyGatherBypassCount, WorkerProxyInPlaceRefreshCount,
      FullBoundarySnapshotPublishCount, FullBoundarySnapshotHashCount,
      BoundarySnapshotEpochTokenCount, GetCurrentFixedStepIndex(),
      BoundarySnapshot.Agents.Num());
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::BeginBoundaryTransaction(
  const double GatherMilliseconds)
{
  if (!IsInGameThread() || !IsBoundarySnapshotCurrent()
    || BoundaryOrchestrator.IsValid())
    return false;
  BoundaryOrchestrator =
    MakeShared<FCrowdDemoRoundWorkBatch>();
  if (!BoundaryOrchestrator->Begin(
      BoundarySnapshot, GatherMilliseconds, BoundaryGeneration))
    return false;
  CurrentBoundaryRequestStartSeconds = FPlatformTime::Seconds();
  return true;
}

float UCrowdDemoRoundSimPipelineSubsystem::
  GetCurrentBoundaryWallMilliseconds() const
{
  return CurrentBoundaryRequestStartSeconds > 0.0
    ? static_cast<float>(
        (FPlatformTime::Seconds()
          - CurrentBoundaryRequestStartSeconds) * 1000.0)
    : 0.0f;
}

ECrowdBoundaryTransactionState
UCrowdDemoRoundSimPipelineSubsystem::GetRoundWorkState() const
{
  return BoundaryOrchestrator.IsValid()
    ? BoundaryOrchestrator->GetState()
    : ECrowdBoundaryTransactionState::Idle;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryBusinessWork()
{
  const auto RejectBusinessStage =
    [this](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoBusinessWorkStageDetail reason=%s step=%d plan_revision=%d snapshot_agents=%d snapshot_valid=%d"),
      Reason,
      GetCurrentFixedStepIndex(),
      GetCurrentPlanRevision(),
      BoundarySnapshot.Agents.Num(),
      BoundarySnapshot.bValid ? 1 : 0);
    return false;
  };
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !IsBoundarySnapshotCurrent())
    return RejectBusinessStage(TEXT("invalid_boundary_state"));
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState,
        ESPMode::ThreadSafe>();
  }
  if (BoundaryFacingWorkState->bBusinessStaged)
    return RejectBusinessStage(TEXT("already_staged"));
  FCrowdDemoBoundaryBusinessWorkInput& Input =
    BoundaryFacingWorkState->BusinessInput;
  Input.Snapshot = BoundarySnapshot;
  Input.Facts = BoundaryBusinessFacts;
  Input.Rules = ActivePlan.Rules;
  if (!BuildProjectileSnapshot(Input.Projectiles))
    return RejectBusinessStage(TEXT("projectile_snapshot"));
  Input.RoundId = GetCurrentRoundId();
  Input.FixedStepIndex = GetCurrentFixedStepIndex();
  Input.PlanRevision = GetCurrentPlanRevision();
  Input.StepEndServerTimeSeconds =
    GetCurrentStepEndServerTimeSeconds();
  Input.FixedStepSeconds = GetCurrentFixedStepSeconds();
  Input.SimulationTimeSeconds =
    CalculateCanonicalSimulationTimeSeconds(
      Input.StepEndServerTimeSeconds,
      Input.FixedStepSeconds);
  const bool bVatShowcase =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
  const bool bRangedAttack =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat;
  if (!bVatShowcase && !bRangedAttack)
  {
    if (!FCrowdDemoBusinessScenarioContract::EvaluateNoBusiness(
        GetCurrentFixedStepIndex(),
        PreparedPlannerDecisionHash))
      return RejectBusinessStage(TEXT("no_business"));
  }
  else
  {
    TArray<FCrowdDemoScenarioAgentFact> PlannerAgents;
    PlannerAgents.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent
      : BoundarySnapshot.Agents)
    {
      FCrowdDemoScenarioAgentFact& Fact =
        PlannerAgents.AddDefaulted_GetRef();
      Fact.EntityRef = Agent.AgentFacts.StableEntityRef;
      Fact.Position = Agent.State.Position;
      Fact.Velocity = Agent.State.Velocity;
      Fact.Health = 100;
      Fact.Revision =
        static_cast<uint32>(
          FMath::Max(1, GetCurrentPlanRevision()));
    }
    FCrowdDemoPlannerDecisionBatch PlannerBatch;
    if (!FCrowdDemoBusinessScenarioContract::EvaluateAssigned(
        bVatShowcase
          ? CrowdDemoBusinessScenarios::VatShowcase
          : CrowdDemoBusinessScenarios::RangedProjectile,
        bVatShowcase
          ? CrowdDemoBusinessPlanners::VatShowcase
          : CrowdDemoBusinessPlanners::RangedAttack,
        GetCurrentFixedStepIndex(),
        static_cast<uint64>(GetCurrentFixedStepIndex()) + 1,
        PlannerAgents, PlannerBatch))
      return RejectBusinessStage(TEXT("assigned_planner"));
    PreparedPlannerDecisionHash = PlannerBatch.StableHash;
  }
  if (PreparedPlannerDecisionHash == 0)
    return RejectBusinessStage(TEXT("zero_hash"));
  BoundaryFacingWorkState->bBusinessStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundarySharedFlowWork(
  const FCrowdMassSharedFlowSampleInput& Input)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || Input.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.PlanRevision != GetCurrentPlanRevision()
    || Input.Agents.Num() != BoundarySnapshot.Agents.Num())
    return false;
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState,
        ESPMode::ThreadSafe>();
  }
  if (BoundaryFacingWorkState->bSharedFlowStaged)
    return false;
  BoundaryFacingWorkState->GraphInput.SharedFlow = Input;
  BoundaryFacingWorkState->bSharedFlowStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetTopologyWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionTopologyInput& Input,
  const FCrowdDemoTargetPolarTopology* CachedTopology,
  const FCrowdDemoTargetPolarTopologySummary* CachedSummary)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering)
    return false;
  for (const auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
    if (Slot.CohortKey == CohortKey)
      return false;
  auto& Slot =
    BoundaryFacingWorkState->TargetTopologySlots.AddDefaulted_GetRef();
  Slot.CohortKey = CohortKey;
  Slot.Input = Input;
  if (CachedTopology && CachedSummary
    && CachedTopology->bValid && CachedSummary->bValid)
  {
    Slot.Output.Topology =
      FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreTargetRegionTopology(
        *CachedTopology);
    Slot.Output.Summary = {
      CachedSummary->CellCount,
      CachedSummary->FeasibleCellCount,
      CachedSummary->EdgeCount,
      CachedSummary->CrossBandEdgeCount,
      CachedSummary->BoundsBlockedCellCount,
      CachedSummary->ObstacleBlockedCellCount,
      CachedSummary->TargetBlockedCellCount,
      CachedSummary->FeasibleGraphHash,
      CachedSummary->EnvironmentHash,
      CachedSummary->TopologyHash,
      CachedSummary->bValid};
    Slot.Output.bValid = Slot.Output.Topology.bValid
      && Slot.Output.Summary.bValid;
    Slot.bUseCachedTopology = Slot.Output.bValid;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetDemandWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionDemandInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (Slot.bDemandStaged) return false;
    Slot.DemandInput = Input;
    Slot.bDemandStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetPlanWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionPlanInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (!Slot.bDemandStaged || Slot.bPlanStaged) return false;
    Slot.PlanInput = Input;
    Slot.bPlanStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryTargetGuidanceWork(
  const uint32 CohortKey,
  const FCrowdMassTargetRegionGuidanceInput& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid())
    return false;
  for (auto& Slot : BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (Slot.CohortKey != CohortKey) continue;
    if (!Slot.bPlanStaged || Slot.bGuidanceStaged) return false;
    Slot.GuidanceInput = Input;
    Slot.bGuidanceStaged = true;
    return true;
  }
  return false;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryMovementWork(
  FCrowdMassMovementPipelineWorkInput&& Input)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || (BoundaryFacingWorkState.IsValid()
      && !BoundaryFacingWorkState->bSharedFlowStaged)
    || (BoundaryFacingWorkState.IsValid()
      && BoundaryFacingWorkState->bMovementStaged)
    || Input.Guidance.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Guidance.PlanRevision != GetCurrentPlanRevision())
    return false;
  if (!BoundaryFacingWorkState.IsValid())
  {
    BoundaryFacingWorkState =
      MakeShared<FCrowdDemoBoundaryFacingWorkState, ESPMode::ThreadSafe>();
  }
  BoundaryFacingWorkState->GraphInput.Movement = MoveTemp(Input);
  BoundaryFacingWorkState->bMovementStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryMovementWork(
  FCrowdMassMovementPipelineWorkOutput& OutOutput)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || BoundaryFacingWorkState->bMovementConsumed
    || !BoundaryFacingWorkState->GraphOutput.Movement.bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->GraphOutput.Movement);
  BoundaryFacingWorkState->bMovementConsumed = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryParticleWork(
  FCrowdMassParticlePipelineWorkInput&& Input)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bMovementStaged
    || BoundaryFacingWorkState->bParticleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision())
    return false;
  Input.PredictedMovements.Reset();
  BoundaryFacingWorkState->GraphInput.ParticleTemplate = MoveTemp(Input);
  BoundaryFacingWorkState->bParticleStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::StageBoundaryObstacleWork(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const float FixedStepSeconds)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bMovementStaged
    || BoundaryFacingWorkState->bObstacleStaged
    || FixedStepSeconds <= 0.0f)
    return false;
  BoundaryFacingWorkState->ObstacleConfig = Config;
  BoundaryFacingWorkState->ObstacleFixedStepSeconds =
    FixedStepSeconds;
  BoundaryFacingWorkState->bObstacleStaged = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryParticleWork(
  FCrowdMassParticlePipelineWorkOutput& OutOutput)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || BoundaryFacingWorkState->bParticleConsumed
    || !BoundaryFacingWorkState->GraphOutput.Particle.bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->GraphOutput.Particle);
  BoundaryFacingWorkState->bParticleConsumed = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  SubmitWorkerV2BoundaryInput()
{
  const auto RejectWorkerV2Input =
    [this](const TCHAR* Stage)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoWorkerV2InputRejected stage=%s step=%d"),
      Stage, GetCurrentFixedStepIndex());
    return false;
  };
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || BoundaryFacingWorkState->bWorkerV2InputSubmitted
    || !BoundaryFacingWorkState->bMovementShadowInputValid
    || !BoundaryFacingWorkState->GraphOutput.Movement.bCompleted
    || !BoundarySnapshot.bValid || !GetWorld()
    || NextWorkerV2MovementControlRevision == 0
    || NextWorkerV2TargetControlRevision == 0
    || NextWorkerV2TargetObjectiveRevision == 0
    || NextWorkerV2ProjectileControlRevision == 0)
    return RejectWorkerV2Input(TEXT("entry"));
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return RejectWorkerV2Input(TEXT("runtime"));
  const FCrowdWorkerBoundaryShadowSync& WorkerShadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  const bool bSubmitIntentOnly = WorkerShadow.IsStarted()
    && WorkerShadow.GetMetrics().FullResnapshotCount > 0;
  const bool bPublishMovementControl =
    !bSubmitIntentOnly
    || LastWorkerV2MovementControlGeneration
      != WorkerShadow.GetGeneration()
    || LastWorkerV2MovementControlPlanRevision
      != GetCurrentPlanRevision();
  if (GetWorld()->GetNetMode() == NM_Client)
  {
    const FCrowdAsyncSimulationRuntime& Runtime =
      RuntimeSubsystem->GetAsyncSimulationRuntime();
    const uint64 AppliedInputSequence =
      Runtime.GetMetrics().LastAppliedInputSequence;
    if (Runtime.GetState()
        != ECrowdAsyncSimulationRuntimeState::Running
      || AppliedInputSequence == 0)
      return true;
    BoundaryFacingWorkState->WorkerV2InputSequence =
      AppliedInputSequence;
    BoundaryFacingWorkState->bWorkerV2InputSubmitted = true;
    return true;
  }
  const bool bCaptureShadowExpectation =
    RuntimeSubsystem->GetWorkerMovementAuthority().GetMode()
      == ECrowdWorkerMovementAuthorityMode::Shadow;
  if (bCaptureShadowExpectation
    && PendingWorkerV2MovementExpectations.Num() >= 16)
    return RejectWorkerV2Input(TEXT("shadow_expectation_capacity"));
  ECrowdDemoWorkerTargetAuthorityMode TargetMode =
    ECrowdDemoWorkerTargetAuthorityMode::Shadow;
  TSet<FCrowdStableEntityRef> TargetCanaries;
  if (!ResolveWorkerTargetAuthority(
      BoundarySnapshot, TargetMode, TargetCanaries))
    return RejectWorkerV2Input(TEXT("target_authority"));
  FString MovementModeValue;
  const bool bMovementProduction =
    FParse::Value(
      FCommandLine::Get(),
      TEXT("CrowdWorkerMovementMode="),
      MovementModeValue)
    && MovementModeValue.Equals(
      TEXT("Production"), ESearchCase::IgnoreCase);
  const bool bHasTargetControl =
    !BoundaryFacingWorkState->TargetTopologySlots.IsEmpty();
  if (bHasTargetControl
    && TargetMode
      != ECrowdDemoWorkerTargetAuthorityMode::Shadow
    && !bMovementProduction)
    return RejectWorkerV2Input(TEXT("target_requires_movement_owner"));

  TMap<int32, FCrowdStableEntityRef> EntityRefByAgentId;
  EntityRefByAgentId.Reserve(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent :
    BoundarySnapshot.Agents)
  {
    if (Agent.Identity.AgentId == INDEX_NONE
      || !Agent.AgentFacts.StableEntityRef.IsValid()
      || EntityRefByAgentId.Contains(Agent.Identity.AgentId))
      return false;
    EntityRefByAgentId.Add(
      Agent.Identity.AgentId,
      Agent.AgentFacts.StableEntityRef);
  }

  FCrowdWorkerTargetControlResource TargetControl;
  uint64 TargetControlSemanticHash = 0;
  bool bPublishTargetControl = false;
  if (bHasTargetControl)
  {
    if (!BoundaryFacingWorkState->GraphOutput.SharedFlow.bValid)
      return false;
    TargetControl.Revision = NextWorkerV2TargetControlRevision;
    TargetControl.Cohorts.Reserve(
      BoundaryFacingWorkState->TargetTopologySlots.Num());
    for (const auto& Slot :
      BoundaryFacingWorkState->TargetTopologySlots)
    {
      if (!Slot.Output.bValid
        || !Slot.bDemandStaged
        || !Slot.bPlanStaged
        || !Slot.bGuidanceStaged)
        return false;
      FCrowdWorkerTargetCohortInput& Cohort =
        TargetControl.Cohorts.AddDefaulted_GetRef();
      Cohort.CohortKey = Slot.CohortKey;
      Cohort.TopologyRevision =
        FCrowdWorkerTargetControlResourceCodec::
          CalculateTopologyRevision(Slot.Output.Topology);
      if (Cohort.TopologyRevision == 0)
        return false;
      Cohort.TargetRevision = Slot.PlanInput.TargetRevision;
      Cohort.FixedStepIndex = Slot.PlanInput.FixedStepIndex;
      Cohort.Settings = Slot.DemandInput.Settings;
      Cohort.FlowConfig = Slot.DemandInput.FlowConfig;
      Cohort.FlowConfig.ObstacleSpecs.Sort(
        [](const FCrowdSharedFlowObstacleSpec& A,
          const FCrowdSharedFlowObstacleSpec& B)
        {
          return A.ObstacleId < B.ObstacleId;
        });
      Cohort.BootstrapPlan = Slot.PlanInput.PreviousPlan;
      Cohort.BootstrapExecution =
        Slot.PlanInput.PreviousExecution;
      const auto JoinFarFlow =
        [this, &Cohort](
          FCrowdTargetRegionTransportAgent& Agent,
          const bool bAllowVelocityFallback)
      {
        const int32* FlowIndex =
          BoundaryFacingWorkState->SharedFlowIndexByAgentId.Find(
            Agent.AgentId);
        if (!FlowIndex
          || !BoundaryFacingWorkState->GraphOutput.SharedFlow.
            Agents.IsValidIndex(*FlowIndex))
        {
          if (bAllowVelocityFallback)
          {
            Agent.FarFlowPreferredVelocity = Agent.Velocity;
            return true;
          }
          return false;
        }
        const FCrowdMassSharedFlowAgentOutput& Flow =
          BoundaryFacingWorkState->GraphOutput.SharedFlow.
            Agents[*FlowIndex];
        Agent.FarFlowPreferredVelocity =
          FCrowdTargetRegionTransportKernel::
            ComposeTargetAdvectedFarFlowVelocity(
              FVector2f(
                Flow.Candidate.PreferredVelocity.X,
                Flow.Candidate.PreferredVelocity.Y),
              Cohort.Settings.TargetVelocity,
              Agent.MaxSpeedCmps);
        return true;
      };
      Cohort.Agents.Reserve(Slot.DemandInput.Agents.Num());
      for (const FCrowdTargetRegionTransportAgent& Source :
        Slot.DemandInput.Agents)
      {
        const FCrowdStableEntityRef* EntityRef =
          EntityRefByAgentId.Find(Source.AgentId);
        if (!EntityRef)
          return false;
        FCrowdWorkerTargetAgentInput& TargetAgent =
          Cohort.Agents.AddDefaulted_GetRef();
        TargetAgent.EntityRef = *EntityRef;
        TargetAgent.Agent = Source;
        if (!JoinFarFlow(TargetAgent.Agent, false))
          return false;
      }
      Cohort.ExternalAgents = Slot.DemandInput.ExternalAgents;
      for (FCrowdTargetRegionTransportAgent& External :
        Cohort.ExternalAgents)
      {
        if (!JoinFarFlow(External, true))
          return false;
      }
      Cohort.Agents.Sort(
        [](const FCrowdWorkerTargetAgentInput& A,
          const FCrowdWorkerTargetAgentInput& B)
        {
          return A.EntityRef < B.EntityRef;
        });
      Cohort.ExternalAgents.Sort(
        [](const FCrowdTargetRegionTransportAgent& A,
          const FCrowdTargetRegionTransportAgent& B)
        {
          return A.AgentId < B.AgentId;
        });
    }
    TargetControl.Cohorts.Sort(
      [](const FCrowdWorkerTargetCohortInput& A,
        const FCrowdWorkerTargetCohortInput& B)
      {
        return A.CohortKey < B.CohortKey;
      });
    FCrowdWorkerTargetControlResource SemanticControl =
      TargetControl;
    SemanticControl.Revision = 1;
    for (FCrowdWorkerTargetCohortInput& Cohort :
      SemanticControl.Cohorts)
    {
      Cohort.TopologyRevision = 1;
      Cohort.TargetRevision = 0;
      Cohort.FixedStepIndex = 0;
      Cohort.Settings.TargetLocation = FVector2f::ZeroVector;
      Cohort.Settings.TargetVelocity = FVector2f::ZeroVector;
      Cohort.FlowConfig.GoalLocation = FVector::ZeroVector;
      Cohort.BootstrapPlan = {};
      Cohort.BootstrapExecution = {};
      const auto ClearDynamicAgent = [](
        FCrowdTargetRegionTransportAgent& Agent)
      {
        Agent.Location = FVector2f::ZeroVector;
        Agent.Velocity = FVector2f::ZeroVector;
        Agent.FarFlowPreferredVelocity = FVector2f::ZeroVector;
        Agent.bEngagedHold = false;
      };
      for (FCrowdWorkerTargetAgentInput& Agent : Cohort.Agents)
        ClearDynamicAgent(Agent.Agent);
      for (FCrowdTargetRegionTransportAgent& Agent :
        Cohort.ExternalAgents)
        ClearDynamicAgent(Agent);
    }
    FCrowdWorkerPayload SemanticPayload;
    if (!FCrowdWorkerTargetControlResourceCodec::Encode(
        SemanticControl, SemanticPayload))
      return RejectWorkerV2Input(TEXT("target_semantic_encode"));
    TargetControlSemanticHash = SemanticPayload.StableHash;
    bPublishTargetControl =
      TargetControlSemanticHash
        != LastWorkerV2TargetControlSemanticHash;
  }

  FCrowdWorkerProjectileControlResource ProjectileControl;
  uint64 ProjectileControlSemanticHash = 0;
  bool bPublishProjectileControl = false;
  const bool bHasProjectileControl =
    BoundaryFacingWorkState->BusinessInput.Rules.
      RangedCombatSettings.bEnabled != 0;
  if (bHasProjectileControl)
  {
    const auto RejectProjectileControl =
      [this](const TCHAR* Stage)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoWorkerProjectileControlRejected stage=%s step=%d"),
        Stage, GetCurrentFixedStepIndex());
      return false;
    };
    const FCrowdDemoBoundaryBusinessWorkInput& BusinessInput =
      BoundaryFacingWorkState->BusinessInput;
    if (!BoundaryFacingWorkState->BusinessOutput.bCompleted
      || !BoundaryFacingWorkState->BusinessOutput.bRequiresCommit
      || BusinessInput.Facts.Num()
        != BusinessInput.Snapshot.Agents.Num()
      || BusinessInput.FixedStepIndex
        != BoundarySnapshot.FixedStepIndex)
      return RejectProjectileControl(TEXT("business_input"));
    TArray<FCrowdDemoRangedCombatAgent> CombatAgents;
    CombatAgents.Reserve(BusinessInput.Facts.Num());
    for (int32 Index = 0; Index < BusinessInput.Facts.Num();
      ++Index)
    {
      const FCrowdDemoRoundBoundaryBusinessFact& Fact =
        BusinessInput.Facts[Index];
      const FCrowdMassBoundaryAgentRecord& Base =
        BusinessInput.Snapshot.Agents[Index];
      if (Fact.EntityRef
          != Base.AgentFacts.StableEntityRef
        || Fact.AgentId != Base.Identity.AgentId
        || !Fact.bHasCombatCapability)
        return RejectProjectileControl(TEXT("agent_join"));
      FCrowdDemoRangedCombatAgent& Agent =
        CombatAgents.AddDefaulted_GetRef();
      Agent.EntityRef = Fact.EntityRef;
      Agent.AgentId = Fact.AgentId;
      Agent.LifecycleSerial =
        static_cast<int32>(Fact.EntityRef.LifecycleSerial);
      Agent.FormationIndex = Fact.FormationIndex;
      Agent.FactionId = Base.AgentFacts.FactionKey;
      Agent.Position = Base.State.Position;
      Agent.Velocity = Base.State.Velocity;
      Agent.RadiusCm = Fact.RadiusCm;
      Agent.bAlive = Fact.Stats.bAlive;
      Agent.Combat = BuildBoundaryCombatState(Fact);
    }
    CombatAgents.Sort([](
      const FCrowdDemoRangedCombatAgent& A,
      const FCrowdDemoRangedCombatAgent& B)
    {
      return A.AgentId < B.AgentId;
    });
    const TArray<FCrowdDemoRangedCombatAgent> HostCombatAgents =
      CombatAgents;
    TArray<FCrowdProjectileSpawnRequest> SpawnRequests;
    FCrowdDemoProjectileStepSummary AttackSummary;
    if (!FCrowdDemoProjectileAdapters::BuildRangedAttackPlan(
        BusinessInput.RoundId, BusinessInput.FixedStepIndex,
        BusinessInput.Rules.RangedCombatSettings,
        CombatAgents, SpawnRequests, AttackSummary))
      return RejectProjectileControl(TEXT("attack_plan"));
    const FCrowdDemoProjectileStepSummary& LegacyAttack =
      BoundaryFacingWorkState->BusinessOutput.Commit.ProjectileSummary;
    if (AttackSummary.TargetAcquiredCount
          != LegacyAttack.TargetAcquiredCount
      || AttackSummary.CompletedWindupCount
          != LegacyAttack.CompletedWindupCount
      || AttackSummary.DuplicateFireCount
          != LegacyAttack.DuplicateFireCount
      || AttackSummary.InvalidTargetLifecycleCount
          != LegacyAttack.InvalidTargetLifecycleCount
      || AttackSummary.AttackStateHash
          != LegacyAttack.AttackStateHash)
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoWorkerProjectileAttackParityRejected step=%d acquired=%d/%d windup=%d/%d spawn_requests=%d legacy_spawned=%d duplicate=%d/%d invalid_lifecycle=%d/%d attack_hash=%u/%u"),
        GetCurrentFixedStepIndex(),
        AttackSummary.TargetAcquiredCount,
        LegacyAttack.TargetAcquiredCount,
        AttackSummary.CompletedWindupCount,
        LegacyAttack.CompletedWindupCount,
        SpawnRequests.Num(),
        LegacyAttack.SpawnedCount,
        AttackSummary.DuplicateFireCount,
        LegacyAttack.DuplicateFireCount,
        AttackSummary.InvalidTargetLifecycleCount,
        LegacyAttack.InvalidTargetLifecycleCount,
        AttackSummary.AttackStateHash,
        LegacyAttack.AttackStateHash);
      return RejectProjectileControl(TEXT("attack_plan_parity"));
    }
    ProjectileControl.Revision =
      NextWorkerV2ProjectileControlRevision;
    ProjectileControl.AnchorEntity =
      BoundarySnapshot.Agents[0].AgentFacts.StableEntityRef;
    ProjectileControl.bReplaceState =
      !bWorkerV2ProjectileStateBootstrapped;
    ProjectileControl.Input.FixedStepIndex =
      BusinessInput.FixedStepIndex;
    ProjectileControl.Input.ServerTimeSeconds =
      BusinessInput.SimulationTimeSeconds;
    ProjectileControl.Input.FixedStepSeconds =
      BusinessInput.FixedStepSeconds;
    ProjectileControl.Input.Profiles.Add(
      FCrowdDemoProjectileAdapters::BuildProfile(
        BusinessInput.Rules.RangedCombatSettings, 65536));
    ProjectileControl.Input.SpawnRequests =
      MoveTemp(SpawnRequests);
    if (!FCrowdDemoProjectileAdapters::BuildTargetSnapshots(
        BusinessInput.FixedStepSeconds, CombatAgents,
        ProjectileControl.Input.Targets))
      return RejectProjectileControl(TEXT("targets"));
    const FCrowdDemoFlowObstacleCollisionSnapshotProvider
      EnvironmentProvider(
        BusinessInput.Rules.FlowFieldConfig);
    if (!EnvironmentProvider.Gather(
        BusinessInput.FixedStepIndex,
        ProjectileControl.Input.EnvironmentBodies))
      return RejectProjectileControl(TEXT("environment"));
    if (ProjectileControl.bReplaceState)
      ProjectileControl.Input.CurrentStates =
        BusinessInput.Projectiles;
    ProjectileControl.EffectProfiles.Add(
      FCrowdDemoProjectileAdapters::BuildEffectProfile(
        BusinessInput.Rules.RangedCombatSettings));
    FCrowdDemoWorkerCombatHostInput HostCombatInput;
    HostCombatInput.RoundId = BusinessInput.RoundId;
    HostCombatInput.FixedStepIndex = BusinessInput.FixedStepIndex;
    HostCombatInput.PlanRevision = BusinessInput.PlanRevision;
    HostCombatInput.ServerTimeSeconds =
      BusinessInput.SimulationTimeSeconds;
    HostCombatInput.FixedStepSeconds =
      BusinessInput.FixedStepSeconds;
    HostCombatInput.AttackSettings =
      BusinessInput.Rules.RangedCombatSettings;
    HostCombatInput.HitSettings.FixedStepSeconds =
      BusinessInput.FixedStepSeconds;
    HostCombatInput.Agents = HostCombatAgents;
    if (!FCrowdDemoWorkerCombatHostInputCodec::Encode(
        HostCombatInput, ProjectileControl.HostCombatInput))
      return RejectProjectileControl(TEXT("combat_host_encode"));
    if (!ProjectileControl.IsValid())
      return RejectProjectileControl(TEXT("resource_validation"));
    if (!CalculateCrowdDemoWorkerProjectileControlSemanticHash(
        ProjectileControl, ProjectileControlSemanticHash))
      return RejectProjectileControl(TEXT("semantic_hash"));
    bPublishProjectileControl =
      !bMovementProduction
      || ProjectileControlSemanticHash
        != LastWorkerV2ProjectileControlSemanticHash;
  }

  const FCrowdMassMovementPipelineWorkOutput& MovementOutput =
    BoundaryFacingWorkState->GraphOutput.Movement;
  FCrowdWorkerMovementControlResource Control;
  if (bPublishMovementControl)
  {
    const FCrowdMassMovementPipelineWorkInput& MovementInput =
      BoundaryFacingWorkState->MovementShadowInput;
    const TArray<FCrowdMassMovementPipelineAgentOverlay>& Overlays =
      MovementInput.AgentOverlays;
    if (Overlays.Num() != BoundarySnapshot.Agents.Num())
      return RejectWorkerV2Input(TEXT("movement_overlay_count"));
    TMap<int32, const FCrowdComposedGuidance*> GuidanceByAgentId;
    for (const FCrowdComposedGuidance& Guidance :
      MovementOutput.Guidance.ComposedGuidance)
    {
      if (!Guidance.bValid
        || GuidanceByAgentId.Contains(Guidance.AgentId))
        return false;
      GuidanceByAgentId.Add(Guidance.AgentId, &Guidance);
    }
    TMap<int32, const FCrowdLocalPredictiveResult*> LocalByAgentId;
    if (MovementInput.bRunLocalPredictive)
    {
      for (const FCrowdLocalPredictiveResult& Local :
        MovementOutput.LocalPredictive.Results)
      {
        if (LocalByAgentId.Contains(Local.AgentId))
          return false;
        LocalByAgentId.Add(Local.AgentId, &Local);
      }
    }
    if (GuidanceByAgentId.Num() != Overlays.Num()
      || (MovementInput.bRunLocalPredictive
        && LocalByAgentId.Num() != Overlays.Num()))
      return false;
    Control.Revision = NextWorkerV2MovementControlRevision;
    Control.FixedStepIndex = MovementInput.Guidance.FixedStepIndex;
    Control.PlanRevision = MovementInput.Guidance.PlanRevision;
    Control.bRunLocalPredictive =
      MovementInput.bRunLocalPredictive;
    Control.bApplyEnvironmentMovementConstraint =
      BoundaryFacingWorkState->bObstacleStaged;
    Control.bRunParticleInteraction =
      BoundaryFacingWorkState->bParticleStaged;
    Control.Environment = MovementInput.Environment;
    Control.Environment.ObstacleSpecs.Sort(
      [](const FCrowdSharedFlowObstacleSpec& A,
        const FCrowdSharedFlowObstacleSpec& B)
      {
        return A.ObstacleId < B.ObstacleId;
      });
    Control.LocalPredictiveSettings =
      MovementInput.LocalPredictiveSettings;
    Control.PreviousGrantStates =
      MovementInput.PreviousGrantStates;
    Control.PreviousGrantStates.Sort(
      [](const FCrowdLocalPredictiveGrantState& A,
        const FCrowdLocalPredictiveGrantState& B)
      {
        return A.ComponentKey != B.ComponentKey
          ? A.ComponentKey < B.ComponentKey
          : A.GrantedAgentId < B.GrantedAgentId;
      });
    TMap<int32, const FCrowdParticleConstraintAgent*>
      ParticleAgentById;
    if (Control.bRunParticleInteraction)
    {
      const FCrowdMassParticleWorkInput& ParticleInput =
        BoundaryFacingWorkState->GraphInput.ParticleTemplate.Particle;
      Control.ParticleSettings = ParticleInput.Settings;
      Control.bParticleConstrainToFlowBounds =
        ParticleInput.Environment.bConstrainToFlowBounds;
      for (const FCrowdParticleConstraintAgent& Agent :
        ParticleInput.Agents)
      {
        if (ParticleAgentById.Contains(Agent.AgentId))
          return false;
        ParticleAgentById.Add(Agent.AgentId, &Agent);
        if (!EntityRefByAgentId.Contains(Agent.AgentId))
          Control.ExternalParticleAgents.Add(Agent);
      }
      Control.ExternalParticleAgents.Sort([](
        const FCrowdParticleConstraintAgent& A,
        const FCrowdParticleConstraintAgent& B)
      {
        return A.AgentId < B.AgentId;
      });
    }
    Control.Entries.Reserve(Overlays.Num());
    for (const FCrowdMassMovementPipelineAgentOverlay& Overlay :
      Overlays)
    {
      const FCrowdStableEntityRef* EntityRef =
        EntityRefByAgentId.Find(Overlay.AgentId);
      const FCrowdComposedGuidance* const* Guidance =
        GuidanceByAgentId.Find(Overlay.AgentId);
      const FCrowdLocalPredictiveResult* const* Local =
        MovementInput.bRunLocalPredictive
          ? LocalByAgentId.Find(Overlay.AgentId)
          : nullptr;
      if (!EntityRef || !Guidance
        || (MovementInput.bRunLocalPredictive && !Local))
        return false;
      FCrowdWorkerMovementControlEntry& Entry =
        Control.Entries.AddDefaulted_GetRef();
      Entry.EntityRef = *EntityRef;
      Entry.AgentId = Overlay.AgentId;
      Entry.InteractionLayer = Overlay.InteractionLayer;
      Entry.PreviousBlockedAgeSteps =
        FMath::Max(0, Overlay.PreviousBlockedAgeSteps);
      Entry.MaximumSpeedCmps = Overlay.MaximumSpeedCmps;
      if (const FCrowdParticleConstraintAgent* const* ParticleAgent =
        ParticleAgentById.Find(Overlay.AgentId))
      {
        Entry.InteractionLayer =
          (*ParticleAgent)->InteractionLayer;
        Entry.ParticleEnvironmentHardClearanceCm =
          (*ParticleAgent)->EnvironmentHardClearanceCm;
        Entry.ParticlePhysicalRadiusCm =
          (*ParticleAgent)->PhysicalRadiusCm;
        Entry.ParticleHardSafetyGapCm =
          (*ParticleAgent)->HardSafetyGapCm;
        Entry.ParticleSoftMarginCm =
          (*ParticleAgent)->SoftMarginCm;
        Entry.ParticleMobility =
          (*ParticleAgent)->Mobility;
      }
      Entry.AutonomousPreferredVelocity =
        (*Guidance)->AutonomousPreferredVelocity;
      const bool bTargetOwner =
        TargetMode
          == ECrowdDemoWorkerTargetAuthorityMode::Production
        || TargetCanaries.Contains(*EntityRef);
      Entry.bUseWorkerTargetGuidance =
        bHasTargetControl
        && bTargetOwner
        && (*Guidance)->SelectedProvider
          == ECrowdGuidanceProvider::TargetRegion;
      Entry.bUseAuthoritativePreferredVelocity =
        (*Guidance)->SelectedProvider
          == ECrowdGuidanceProvider::BusinessOverride
        || (*Guidance)->SelectedProvider
          == ECrowdGuidanceProvider::Stop;
      Entry.bUseLocalVelocity =
        MovementInput.bRunLocalPredictive;
      if (Local)
      {
        Entry.LocalVelocity = FVector(
          (*Local)->Velocity.X, (*Local)->Velocity.Y, 0.0f);
        Entry.bLocalVelocityValid = (*Local)->bValid;
      }
      Entry.bFreezeAtBoundaryLocation =
        Overlay.bFreezeAtBoundaryLocation;
      Entry.BoundaryLocation = Overlay.BoundaryLocation;
      Entry.bVerticalOverride = Overlay.bVerticalOverride;
      Entry.ProposedZ = Overlay.ProposedZ;
      Entry.VerticalVelocityCmps = Overlay.VerticalVelocityCmps;
      Entry.bParticleActive = Overlay.bParticleActive;
    }
    Control.Entries.Sort(
      [](const FCrowdWorkerMovementControlEntry& A,
        const FCrowdWorkerMovementControlEntry& B)
      {
        return A.EntityRef < B.EntityRef;
      });
  }
  TArray<FCrowdWorkerVersionedResourceInput> ResourceInputs;
  ResourceInputs.Reserve(
    (bPublishMovementControl ? 1 : 0)
      + (bPublishTargetControl ? 1 : 0)
      + (bPublishProjectileControl ? 1 : 0)
      + (!bSubmitIntentOnly && bHasTargetControl ? 1 : 0));
  if (bPublishMovementControl)
  {
    FCrowdWorkerVersionedResourceInput& ResourceInput =
      ResourceInputs.AddDefaulted_GetRef();
    ResourceInput.ResourceId =
      CrowdWorkerResourceIds::MovementControl;
    ResourceInput.Revision = Control.Revision;
    if (!FCrowdWorkerMovementControlResourceCodec::Encode(
        Control, ResourceInput.Payload))
      return RejectWorkerV2Input(TEXT("movement_encode"));
  }
  if (bPublishTargetControl)
  {
    FCrowdWorkerVersionedResourceInput& TargetInput =
      ResourceInputs.AddDefaulted_GetRef();
    TargetInput.ResourceId =
      CrowdWorkerResourceIds::TargetControl;
    TargetInput.Revision = TargetControl.Revision;
    if (!FCrowdWorkerTargetControlResourceCodec::Encode(
        TargetControl, TargetInput.Payload))
      return false;
  }
  TArray<FCrowdWorkerObjectiveRevisionDelta> TargetObjectives;
  const uint64 TargetObjectiveSemanticHash = bHasTargetControl
    ? CalculateTargetObjectiveSemanticHash(GetTargetFact()) : 0;
  const bool bPublishTargetObjective = bHasTargetControl
    && (!bSubmitIntentOnly
      || TargetObjectiveSemanticHash
        != LastWorkerV2TargetObjectiveSemanticHash);
  if (bPublishTargetObjective)
  {
    FCrowdWorkerObjectiveRevisionDelta& Objective =
      TargetObjectives.AddDefaulted_GetRef();
    if (!BuildTargetObjectiveRevisionDelta(
        GetTargetFact(), GetCurrentFixedStepIndex(),
        NextWorkerV2TargetObjectiveRevision, Objective))
      return RejectWorkerV2Input(TEXT("target_objective_encode"));
    if (!bSubmitIntentOnly)
    {
      FCrowdWorkerVersionedResourceInput& ObjectiveInput =
        ResourceInputs.AddDefaulted_GetRef();
      ObjectiveInput.ResourceId =
        CrowdWorkerResourceIds::ObjectiveRevision(
          Objective.ObjectiveId);
      ObjectiveInput.Revision = Objective.Revision;
      ObjectiveInput.Payload = Objective.Payload;
      TargetObjectives.Reset();
    }
  }
  if (bPublishProjectileControl)
  {
    FCrowdWorkerVersionedResourceInput& ProjectileInput =
      ResourceInputs.AddDefaulted_GetRef();
    ProjectileInput.ResourceId =
      CrowdWorkerResourceIds::ProjectileControl;
    ProjectileInput.Revision = ProjectileControl.Revision;
    if (!FCrowdWorkerProjectileControlResourceCodec::Encode(
        ProjectileControl, ProjectileInput.Payload))
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoWorkerProjectileControlRejected stage=encode step=%d"),
        GetCurrentFixedStepIndex());
      return false;
    }
  }
  TArray<FCrowdWorkerExternalGameplayInput>
    MovementProfileInputs;
  if (!bSubmitIntentOnly)
  {
    MovementProfileInputs.Reserve(Control.Entries.Num());
    for (const FCrowdWorkerMovementControlEntry& Entry :
      Control.Entries)
    {
      FCrowdWorkerExternalGameplayInput& ProfileInput =
        MovementProfileInputs.AddDefaulted_GetRef();
      ProfileInput.EntityRef = Entry.EntityRef;
      ProfileInput.InputTypeId = static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::
          MovementProfileRevision);
      ProfileInput.DirtyMask = 1;
      if (!FCrowdWorkerMovementProfileCodec::Encode(
          Entry, ProfileInput.FullState))
        return RejectWorkerV2Input(TEXT("movement_profile_encode"));
    }
  }
  TArray<FCrowdWorkerExternalGameplayInput>
    PlanRevisionInputs;
  if (bSubmitIntentOnly && bPublishMovementControl)
  {
    // A new plan changes input-owned state even when lifecycle and movement
    // profiles are unchanged. Publish this baseline once at the revision
    // boundary; Production planning overlays its Worker-owned Facing or
    // Movement state before solving, so Mass kinematics do not become a
    // normal-frame authority input.
    PlanRevisionInputs.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      FCrowdWorkerExternalGameplayInput& Input =
        PlanRevisionInputs.AddDefaulted_GetRef();
      Input.EntityRef = Agent.AgentFacts.StableEntityRef;
      Input.InputTypeId = static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::InputSnapshot);
      Input.DirtyMask = 1;
      if (!FCrowdWorkerBoundaryStateCodec::EncodeState(
          Agent, Input.FullState))
        return RejectWorkerV2Input(TEXT("plan_revision_encode"));
    }
  }
  const bool bWorkerInputAccepted = bSubmitIntentOnly
    ? FCrowdDemoWorkerInputSync::SubmitIntentBatch(
        *GetWorld(), GetCurrentFixedStepIndex(),
        GetCurrentPlanRevision(),
        GetCurrentStepEndServerTimeSeconds(), ResourceInputs,
        {}, {}, PlanRevisionInputs, nullptr, TargetObjectives)
    : FCrowdDemoWorkerInputSync::SubmitBoundarySnapshot(
        *GetWorld(), BoundarySnapshot,
        GetCurrentFixedStepSeconds(),
        GetCurrentStepEndServerTimeSeconds(),
        ResourceInputs, nullptr, MovementProfileInputs);
  if (!bWorkerInputAccepted)
    return RejectWorkerV2Input(TEXT("boundary_submit"));
  const uint64 AcceptedInputSequence =
    RuntimeSubsystem->GetWorkerShadowSync().GetMetrics().
      LastSubmittedInputSequence;
  if (AcceptedInputSequence == 0)
    return RejectWorkerV2Input(TEXT("accepted_sequence"));
  BoundaryFacingWorkState->WorkerV2InputSequence =
    AcceptedInputSequence;

  if (bHasTargetControl)
  {
    bWorkerV2TargetStateBootstrapped = true;
    if (bPublishTargetObjective)
    {
      LastWorkerV2TargetObjectiveSemanticHash =
        TargetObjectiveSemanticHash;
      ++WorkerV2TargetObjectivePublishCount;
      ++NextWorkerV2TargetObjectiveRevision;
      if (NextWorkerV2TargetObjectiveRevision == 0)
        return RejectWorkerV2Input(
          TEXT("target_objective_revision_overflow"));
    }
    else
    {
      ++WorkerV2TargetObjectiveReuseCount;
    }
  }

  if (bCaptureShadowExpectation)
  {
    const uint64 InputSequence = AcceptedInputSequence;
    if (InputSequence == 0
      || PendingWorkerV2MovementExpectations.Contains(
        InputSequence))
      return false;
    TMap<int32, const FCrowdMassPredictedMovement*> PredictedById;
    for (const FCrowdMassPredictedMovement& Predicted :
      MovementOutput.MovementPredict.Results)
    {
      if (!Predicted.bValid
        || PredictedById.Contains(Predicted.AgentId))
        return false;
      PredictedById.Add(Predicted.AgentId, &Predicted);
    }
    TMap<int32, const FCrowdMassFinalKinematicState*>
      ObstacleByAgentId;
    for (const FCrowdMassFinalKinematicState& Kinematic :
      BoundaryFacingWorkState->ObstacleKinematics)
    {
      if (!Kinematic.bValid
        || ObstacleByAgentId.Contains(Kinematic.AgentId))
        return false;
      ObstacleByAgentId.Add(Kinematic.AgentId, &Kinematic);
    }
    TArray<FCrowdDemoWorkerV2MovementExpectation>& Expectations =
      PendingWorkerV2MovementExpectations.Add(InputSequence);
    Expectations.Reserve(EntityRefByAgentId.Num());
    for (const TPair<int32, FCrowdStableEntityRef>& Pair :
      EntityRefByAgentId)
    {
      const FCrowdMassPredictedMovement* const* Predicted =
        PredictedById.Find(Pair.Key);
      if (!Predicted)
        return false;
      FCrowdDemoWorkerV2MovementExpectation& Expectation =
        Expectations.AddDefaulted_GetRef();
      Expectation.EntityRef = Pair.Value;
      if (const FCrowdMassFinalKinematicState* const* Kinematic =
        ObstacleByAgentId.Find(Pair.Key))
      {
        Expectation.Position = (*Kinematic)->Position;
        Expectation.Velocity = (*Kinematic)->Velocity;
      }
      else
      {
        Expectation.Position = (*Predicted)->PredictedPosition;
        Expectation.Velocity = (*Predicted)->Velocity;
      }
      Expectation.SimulationTimeSeconds =
        GetCurrentStepEndServerTimeSeconds();
    }
    Expectations.Sort(
      [](const FCrowdDemoWorkerV2MovementExpectation& A,
        const FCrowdDemoWorkerV2MovementExpectation& B)
      {
        return A.EntityRef < B.EntityRef;
      });
  }
  BoundaryFacingWorkState->bWorkerV2InputSubmitted = true;
  if (bPublishMovementControl)
  {
    LastWorkerV2MovementControlGeneration =
      WorkerShadow.GetGeneration();
    LastWorkerV2MovementControlPlanRevision =
      GetCurrentPlanRevision();
    ++WorkerV2MovementControlPublishCount;
    ++NextWorkerV2MovementControlRevision;
    if (NextWorkerV2MovementControlRevision == 0)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerV2MovementControlRevisionOverflow"));
      return false;
    }
  }
  else
    ++WorkerV2MovementControlReuseCount;
  if (WorkerV2MovementControlPublishCount
        + WorkerV2MovementControlReuseCount == 1
    || WorkerV2MovementControlReuseCount == 1
    || (WorkerV2MovementControlPublishCount
        + WorkerV2MovementControlReuseCount) % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerMovementProfileCheckpoint published=%llu reused=%llu generation=%llu plan_revision=%d next_revision=%llu source=PersistentRuntimeAuthority"),
      WorkerV2MovementControlPublishCount,
      WorkerV2MovementControlReuseCount,
      LastWorkerV2MovementControlGeneration,
      LastWorkerV2MovementControlPlanRevision,
      NextWorkerV2MovementControlRevision);
  }
  if (bPublishTargetControl)
  {
    LastWorkerV2TargetControlSemanticHash =
      TargetControlSemanticHash;
    ++WorkerV2TargetControlPublishCount;
    ++NextWorkerV2TargetControlRevision;
    if (NextWorkerV2TargetControlRevision == 0)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerV2TargetControlRevisionOverflow"));
      return false;
    }
  }
  else if (bHasTargetControl)
  {
    ++WorkerV2TargetControlReuseCount;
  }
  if (bHasTargetControl
    && (WorkerV2TargetControlPublishCount
        + WorkerV2TargetControlReuseCount == 1
      || WorkerV2TargetControlReuseCount == 1
      || (WorkerV2TargetControlPublishCount
          + WorkerV2TargetControlReuseCount) % 300 == 0))
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerTargetResourceCheckpoint published=%llu reused=%llu next_revision=%llu semantic_hash=%llu source=PersistentRuntimeAuthority"),
      WorkerV2TargetControlPublishCount,
      WorkerV2TargetControlReuseCount,
      NextWorkerV2TargetControlRevision,
      LastWorkerV2TargetControlSemanticHash);
  }
  if (bPublishProjectileControl)
  {
    bWorkerV2ProjectileStateBootstrapped = true;
    LastWorkerV2ProjectileControlSemanticHash =
      ProjectileControlSemanticHash;
    ++WorkerV2ProjectileControlPublishCount;
    ++NextWorkerV2ProjectileControlRevision;
    if (NextWorkerV2ProjectileControlRevision == 0)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerV2ProjectileControlRevisionOverflow"));
      return false;
    }
  }
  else if (bHasProjectileControl)
    ++WorkerV2ProjectileControlReuseCount;
  if (bHasProjectileControl
    && (WorkerV2ProjectileControlPublishCount
        + WorkerV2ProjectileControlReuseCount == 1
      || WorkerV2ProjectileControlReuseCount == 1
      || (WorkerV2ProjectileControlPublishCount
          + WorkerV2ProjectileControlReuseCount) % 300 == 0))
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerProjectileResourceCheckpoint published=%llu reused=%llu next_revision=%llu semantic_hash=%llu source=PersistentRuntimeAuthority"),
      WorkerV2ProjectileControlPublishCount,
      WorkerV2ProjectileControlReuseCount,
      NextWorkerV2ProjectileControlRevision,
      LastWorkerV2ProjectileControlSemanticHash);
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  TrySubmitWorkerV2ClockIntentEarly()
{
  check(IsInGameThread());
  if (!BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bBusinessStaged
    || BoundaryFacingWorkState->bWorkerV2InputSubmitted
    || !IsBoundarySnapshotCurrent() || !GetWorld()
    || GetWorld()->GetNetMode() == NM_Client)
    return true;

  // Bootstrap, plan changes and non-Production comparison modes still consume
  // the fully prepared Boundary input below. Once the immutable controls are
  // installed, ordinary Target frames carry only Clock plus the O(1) moving
  // objective fact; Projectile frames carry Clock alone.
  if (ActivePlan.Rules.Scenario
        != ECrowdDemoScenario::SimRoundSoftPressure)
    return true;
  const bool bTargetActive = IsTargetRegionExecutionActive();
  const bool bProjectileActive =
    ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
    && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0;
  if ((!bTargetActive && !bProjectileActive)
    || (bTargetActive && !bWorkerV2TargetStateBootstrapped)
    || (bProjectileActive
      && !bWorkerV2ProjectileStateBootstrapped))
    return true;

  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  const FCrowdWorkerBoundaryShadowSync& WorkerShadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  if (!WorkerShadow.IsStarted()
    || WorkerShadow.GetMetrics().FullResnapshotCount == 0
    || LastWorkerV2MovementControlGeneration
        != WorkerShadow.GetGeneration()
    || LastWorkerV2MovementControlPlanRevision
        != GetCurrentPlanRevision())
    return true;
  if (RuntimeSubsystem->GetWorkerMovementAuthority().GetMode()
        != ECrowdWorkerMovementAuthorityMode::Production
    || RuntimeSubsystem->GetWorkerBehaviorAuthority().GetMode()
        != ECrowdWorkerBehaviorAuthorityMode::Production)
    return true;

  ECrowdDemoWorkerParticleAuthorityMode ParticleMode =
    ECrowdDemoWorkerParticleAuthorityMode::Shadow;
  TSet<FCrowdStableEntityRef> ParticleCanaries;
  ECrowdDemoWorkerTargetAuthorityMode TargetMode =
    ECrowdDemoWorkerTargetAuthorityMode::Shadow;
  TSet<FCrowdStableEntityRef> TargetCanaries;
  ECrowdDemoWorkerProjectileAuthorityMode ProjectileMode =
    ECrowdDemoWorkerProjectileAuthorityMode::Shadow;
  ECrowdDemoWorkerCombatAuthorityMode CombatMode =
    ECrowdDemoWorkerCombatAuthorityMode::Shadow;
  if (!ResolveWorkerParticleAuthority(
      BoundarySnapshot, ParticleMode, ParticleCanaries)
    || (bTargetActive && !ResolveWorkerTargetAuthority(
      BoundarySnapshot, TargetMode, TargetCanaries))
    || (bProjectileActive
      && !ResolveWorkerProjectileAuthority(ProjectileMode))
    || (bProjectileActive
      && !ResolveWorkerCombatAuthority(CombatMode)))
    return false;
  if (ParticleMode
        != ECrowdDemoWorkerParticleAuthorityMode::Production
    || (bTargetActive && TargetMode
      != ECrowdDemoWorkerTargetAuthorityMode::Production)
    || (bProjectileActive && ProjectileMode
      != ECrowdDemoWorkerProjectileAuthorityMode::Production)
    || (bProjectileActive && CombatMode
      != ECrowdDemoWorkerCombatAuthorityMode::Production))
    return true;

  TArray<FCrowdWorkerObjectiveRevisionDelta> TargetObjectives;
  const uint64 TargetObjectiveSemanticHash = bTargetActive
    ? CalculateTargetObjectiveSemanticHash(GetTargetFact()) : 0;
  const bool bPublishTargetObjective = bTargetActive
    && TargetObjectiveSemanticHash
      != LastWorkerV2TargetObjectiveSemanticHash;
  if (bPublishTargetObjective)
  {
    FCrowdWorkerObjectiveRevisionDelta& Objective =
      TargetObjectives.AddDefaulted_GetRef();
    if (!BuildTargetObjectiveRevisionDelta(
        GetTargetFact(), GetCurrentFixedStepIndex(),
        NextWorkerV2TargetObjectiveRevision, Objective))
      return false;
  }
  const uint64 PreviousInputSequence =
    WorkerShadow.GetMetrics().LastSubmittedInputSequence;
  if (!FCrowdDemoWorkerInputSync::SubmitIntentBatch(
      *GetWorld(), GetCurrentFixedStepIndex(),
      GetCurrentPlanRevision(),
      GetCurrentStepEndServerTimeSeconds(), {}, {}, {}, {}, nullptr,
      TargetObjectives))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerEarlyClockIntentRejected step=%d previous_sequence=%llu"),
      GetCurrentFixedStepIndex(), PreviousInputSequence);
    return false;
  }
  const uint64 AcceptedInputSequence =
    RuntimeSubsystem->GetWorkerShadowSync().GetMetrics().
      LastSubmittedInputSequence;
  if (AcceptedInputSequence == 0
    || AcceptedInputSequence <= PreviousInputSequence)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerEarlyClockIntentSequence step=%d previous_sequence=%llu accepted_sequence=%llu"),
      GetCurrentFixedStepIndex(), PreviousInputSequence,
      AcceptedInputSequence);
    return false;
  }

  BoundaryFacingWorkState->WorkerV2InputSequence =
    AcceptedInputSequence;
  BoundaryFacingWorkState->bWorkerV2InputSubmitted = true;
  BoundaryFacingWorkState->bUseWorkerV2Target = bTargetActive;
  ++WorkerV2MovementControlReuseCount;
  if (bTargetActive)
  {
    ++WorkerV2TargetControlReuseCount;
    if (bPublishTargetObjective)
    {
      LastWorkerV2TargetObjectiveSemanticHash =
        TargetObjectiveSemanticHash;
      ++WorkerV2TargetObjectivePublishCount;
      ++NextWorkerV2TargetObjectiveRevision;
      if (NextWorkerV2TargetObjectiveRevision == 0)
        return false;
    }
    else
    {
      ++WorkerV2TargetObjectiveReuseCount;
    }
  }
  if (bProjectileActive)
    ++WorkerV2ProjectileControlReuseCount;
  ++WorkerV2EarlyClockIntentCount;
  if (WorkerV2EarlyClockIntentCount == 1
    || WorkerV2EarlyClockIntentCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerEarlyClockCheckpoint submitted=%llu input_sequence=%llu simulation_tick=%d generation=%llu plan_revision=%d objective_published=%llu objective_reused=%llu source=WorkerInputSync"),
      WorkerV2EarlyClockIntentCount, AcceptedInputSequence,
      GetCurrentFixedStepIndex(), WorkerShadow.GetGeneration(),
      GetCurrentPlanRevision(),
      WorkerV2TargetObjectivePublishCount,
      WorkerV2TargetObjectiveReuseCount);
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  DrainWorkerV2MovementShadowComparisons()
{
  if (PendingWorkerV2MovementExpectations.IsEmpty())
    return true;
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()
      ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
      : nullptr;
  if (!RuntimeSubsystem)
    return false;

  TArray<uint64> Sequences;
  PendingWorkerV2MovementExpectations.GetKeys(Sequences);
  Sequences.Sort();
  const TArray<FCrowdDemoWorkerV2MovementExpectation>* FirstBatch =
    PendingWorkerV2MovementExpectations.Find(Sequences[0]);
  if (!FirstBatch || FirstBatch->IsEmpty())
    return false;
  const FCrowdWorkerDomainProxyState* Latest =
    RuntimeSubsystem->GetWorkerResultApplyProxy().FindDomain(
      (*FirstBatch)[0].EntityRef,
      ECrowdWorkerField::Movement);
  if (!Latest || Latest->SourceInputSequence == 0)
    return true;
  const uint64 PublishedSequence = Latest->SourceInputSequence;
  for (const uint64 Sequence : Sequences)
  {
    if (Sequence >= PublishedSequence)
      break;
    if (const TArray<FCrowdDemoWorkerV2MovementExpectation>* Skipped =
      PendingWorkerV2MovementExpectations.Find(Sequence))
      WorkerV2MovementStageStaleSkipCount += Skipped->Num();
    PendingWorkerV2MovementExpectations.Remove(Sequence);
  }
  const TArray<FCrowdDemoWorkerV2MovementExpectation>* Expectations =
    PendingWorkerV2MovementExpectations.Find(PublishedSequence);
  if (!Expectations)
    return true;

  double BatchPositionErrorMaxCm = 0.0;
  double BatchVelocityErrorMaxCmps = 0.0;
  uint64 BatchMismatchCount = 0;
  for (const FCrowdDemoWorkerV2MovementExpectation& Expectation :
    *Expectations)
  {
    const FCrowdWorkerDomainProxyState* Actual =
      RuntimeSubsystem->GetWorkerResultApplyProxy().FindDomain(
        Expectation.EntityRef,
        ECrowdWorkerField::Movement);
    FCrowdWorkerMovementState ActualState;
    if (!Actual
      || Actual->SourceInputSequence != PublishedSequence
      || !FCrowdWorkerMovementStateCodec::Decode(
        Actual->State.Payload, ActualState))
      return true;
    const double PositionError = FVector::Distance(
      ActualState.Position, Expectation.Position);
    const double VelocityError = FVector::Distance(
      ActualState.Velocity, Expectation.Velocity);
    const double TimeError = FMath::Abs(
      ActualState.SimulationTimeSeconds
      - Expectation.SimulationTimeSeconds);
    ++WorkerV2MovementStageCompareCount;
    WorkerV2MovementStageLastExpectedInputSequence =
      PublishedSequence;
    WorkerV2MovementStagePositionErrorMaxCm = FMath::Max(
      WorkerV2MovementStagePositionErrorMaxCm, PositionError);
    WorkerV2MovementStageVelocityErrorMaxCmps = FMath::Max(
      WorkerV2MovementStageVelocityErrorMaxCmps, VelocityError);
    WorkerV2MovementStageTimeErrorMaxSeconds = FMath::Max(
      WorkerV2MovementStageTimeErrorMaxSeconds, TimeError);
    BatchPositionErrorMaxCm = FMath::Max(
      BatchPositionErrorMaxCm, PositionError);
    BatchVelocityErrorMaxCmps = FMath::Max(
      BatchVelocityErrorMaxCmps, VelocityError);
    if (PositionError > 0.001
      || VelocityError > 0.001
      || TimeError > 0.000001)
    {
      ++WorkerV2MovementStageMismatchCount;
      ++BatchMismatchCount;
    }
  }
  WorkerV2MovementStageLastPositionErrorMaxCm =
    BatchPositionErrorMaxCm;
  WorkerV2MovementStageLastVelocityErrorMaxCmps =
    BatchVelocityErrorMaxCmps;
  WorkerV2MovementStageLastMismatchCount =
    BatchMismatchCount;
  PendingWorkerV2MovementExpectations.Remove(PublishedSequence);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  DispatchBoundarySoftPressureWorkGraph(
    FCrowdMassFacingFinalizeWorkInput&& Input,
    TMap<int32, int32>&& PreviousSettleStepsByAgentId,
    TMap<int32, bool>&& TerminalOwnerByAgentId)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bBusinessStaged
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || !BoundaryFacingWorkState->bMovementStaged
    || !BoundaryFacingWorkState->bParticleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision()
    || PreviousSettleStepsByAgentId.Num() != BoundarySnapshot.Agents.Num()
    || TerminalOwnerByAgentId.Num() != BoundarySnapshot.Agents.Num())
    return false;
  const auto State = BoundaryFacingWorkState;
  State->PreviousSettleStepsByAgentId =
    MoveTemp(PreviousSettleStepsByAgentId);
  State->TerminalOwnerByAgentId = MoveTemp(TerminalOwnerByAgentId);
  State->GraphInput.FacingSettings = Input.Facing.Settings;
  State->GraphInput.FacingTemplates.Reserve(Input.Facing.Agents.Num());
  for (FCrowdFacingInput& FacingInput : Input.Facing.Agents)
  {
    FCrowdDemoRoundFacingTemplate& Template =
      State->GraphInput.FacingTemplates.AddDefaulted_GetRef();
    Template.Input = MoveTemp(FacingInput);
  }

  const uint64 SnapshotHash = BoundarySnapshot.StableHash;
  const auto AddHashTask = [this, SnapshotHash](
    const FCrowdBoundaryTaskKey Key,
    const TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites)
  {
    return BoundaryOrchestrator->AddTask(
      Key, Prerequisites,
      [SnapshotHash, Key]
      {
        uint64 Hash = FoldBoundaryHash(SnapshotHash, 1);
        Hash = FoldBoundaryHash(
          Hash, Key.StageId.Value);
        Hash = FoldBoundaryHash(Hash, Key.TaskTypeId.Value);
        Hash = FoldBoundaryHash(Hash, Key.ScopeKey);
        return FCrowdBoundaryTaskResult::Success(Hash);
      },
      false);
  };
  const FCrowdBoundaryTaskKey Business = {{1}, {101}, 0};
  const FCrowdBoundaryTaskKey SharedFlow = {{2}, {201}, 0};
  const FCrowdBoundaryTaskKey Movement = {{3}, {301}, 0};
  const FCrowdBoundaryTaskKey Particle = {{4}, {401}, 0};
  const FCrowdBoundaryTaskKey Facing = {{5}, {501}, 0};
  TArray<FCrowdBoundaryTaskKey> MovementDeps = {Business, SharedFlow};
  const bool bHasTargetWork =
    !State->bUseWorkerV2Target
    && !State->TargetTopologySlots.IsEmpty();
  const FCrowdBoundaryTaskKey ParticleDeps[] = {Movement};
  const FCrowdBoundaryTaskKey FacingDeps[] = {Particle};
  bool bRegistered =
    BoundaryOrchestrator->AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      SharedFlow, {},
      [State]
      {
        State->GraphOutput.SharedFlow =
          FCrowdMassSharedFlowWork::BuildPreferred(
            State->GraphInput.SharedFlow);
        State->SharedFlowIndexByAgentId.Reset();
        State->SharedFlowIndexByAgentId.Reserve(
          State->GraphOutput.SharedFlow.Agents.Num());
        for (int32 FlowIndex = 0;
          FlowIndex < State->GraphOutput.SharedFlow.Agents.Num();
          ++FlowIndex)
        {
          State->SharedFlowIndexByAgentId.Add(
            State->GraphOutput.SharedFlow.Agents[FlowIndex].AgentId,
            FlowIndex);
        }
        return State->GraphOutput.SharedFlow.bValid
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (bRegistered && bHasTargetWork)
  {
    State->TargetTopologySlots.Sort([](const auto& A, const auto& B)
    {
      return A.CohortKey < B.CohortKey;
    });
    for (int32 SlotIndex = 0;
      bRegistered && SlotIndex < State->TargetTopologySlots.Num();
      ++SlotIndex)
    {
      const uint32 CohortKey =
        State->TargetTopologySlots[SlotIndex].CohortKey;
      const FCrowdBoundaryTaskKey Topology = {
        {2}, {202}, CohortKey};
      const FCrowdBoundaryTaskKey Demand = {
        {2}, {203}, CohortKey};
      const FCrowdBoundaryTaskKey Plan = {
        {2}, {204}, CohortKey};
      const FCrowdBoundaryTaskKey Guidance = {
        {2}, {205}, CohortKey};
      const FCrowdBoundaryTaskKey DemandDeps[] = {
        SharedFlow, Topology};
      const FCrowdBoundaryTaskKey PlanDeps[] = {Demand};
      const FCrowdBoundaryTaskKey GuidanceDeps[] = {Plan};
      MovementDeps.Add(Guidance);
      bRegistered = BoundaryOrchestrator->AddTask(
        Topology, {},
        [State, SlotIndex]
        {
          auto& Slot = State->TargetTopologySlots[SlotIndex];
          if (!Slot.bUseCachedTopology)
          {
            Slot.Output =
              FCrowdMassTargetRegionWork::BuildTopology(Slot.Input);
          }
          return Slot.Output.bValid
            ? FCrowdBoundaryTaskResult::Success(
                FoldBoundaryHash(
                  Slot.Output.Summary.TopologyHash,
                  Slot.CohortKey))
            : FCrowdBoundaryTaskResult::Failure();
        })
        && BoundaryOrchestrator->AddTask(
          Demand, DemandDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bDemandStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionDemandInput DemandInput =
              Slot.DemandInput;
            DemandInput.Topology = Slot.Output.Topology;
            const auto JoinFarFlow =
              [State, &DemandInput](
                FCrowdTargetRegionTransportAgent& Agent)
            {
              const int32* FlowIndex =
                State->SharedFlowIndexByAgentId.Find(Agent.AgentId);
              if (!FlowIndex
                || !State->GraphOutput.SharedFlow.Agents.IsValidIndex(
                  *FlowIndex))
                return false;
              const FCrowdMassSharedFlowAgentOutput& Flow =
                State->GraphOutput.SharedFlow.Agents[*FlowIndex];
              Agent.FarFlowPreferredVelocity =
                FCrowdTargetRegionTransportKernel::
                  ComposeTargetAdvectedFarFlowVelocity(
                    FVector2f(
                      Flow.Candidate.PreferredVelocity.X,
                      Flow.Candidate.PreferredVelocity.Y),
                    DemandInput.Settings.TargetVelocity,
                    Agent.MaxSpeedCmps);
              return true;
            };
            for (FCrowdTargetRegionTransportAgent& Agent
              : DemandInput.Agents)
              if (!JoinFarFlow(Agent))
                return FCrowdBoundaryTaskResult::Failure();
            for (FCrowdTargetRegionTransportAgent& Agent
              : DemandInput.ExternalAgents)
            {
              // External cohort members are demand obstacles, not members of
              // this cohort's controllable flow set. During client bootstrap
              // they can become visible one correction frame before their
              // shared-flow input. Their gathered velocity is the immutable
              // boundary fallback for that transient frame.
              if (!JoinFarFlow(Agent))
                Agent.FarFlowPreferredVelocity = Agent.Velocity;
            }
            Slot.DemandOutput =
              FCrowdMassTargetRegionWork::BuildDemand(DemandInput);
            return Slot.DemandOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  Slot.DemandOutput.Demand.DemandHash)
              : FCrowdBoundaryTaskResult::Failure();
          })
        && BoundaryOrchestrator->AddTask(
          Plan, PlanDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bPlanStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionPlanInput PlanInput =
              Slot.PlanInput;
            PlanInput.Topology = Slot.Output.Topology;
            PlanInput.Demand = Slot.DemandOutput.Demand;
            Slot.PlanOutput =
              FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
            return Slot.PlanOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  FoldBoundaryHash(
                    Slot.PlanOutput.Plan.TransportHash,
                    Slot.PlanOutput.Execution.ExecutionHash))
              : FCrowdBoundaryTaskResult::Failure();
          })
        && BoundaryOrchestrator->AddTask(
          Guidance, GuidanceDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bGuidanceStaged)
              return FCrowdBoundaryTaskResult::Failure();
            FCrowdMassTargetRegionGuidanceInput GuidanceInput =
              Slot.GuidanceInput;
            GuidanceInput.Topology = Slot.Output.Topology;
            GuidanceInput.Demand = Slot.DemandOutput.Demand;
            GuidanceInput.Plan = Slot.PlanOutput.Plan;
            GuidanceInput.Execution = Slot.PlanOutput.Execution;
            for (FCrowdTargetRegionTransportAgent& Agent
              : GuidanceInput.Agents)
            {
              const int32* FlowIndex =
                State->SharedFlowIndexByAgentId.Find(Agent.AgentId);
              if (!FlowIndex
                || !State->GraphOutput.SharedFlow.Agents.IsValidIndex(
                  *FlowIndex))
                return FCrowdBoundaryTaskResult::Failure();
              const FCrowdMassSharedFlowAgentOutput& Flow =
                State->GraphOutput.SharedFlow.Agents[*FlowIndex];
              Agent.FarFlowPreferredVelocity =
                FCrowdTargetRegionTransportKernel::
                  ComposeTargetAdvectedFarFlowVelocity(
                    FVector2f(
                      Flow.Candidate.PreferredVelocity.X,
                      Flow.Candidate.PreferredVelocity.Y),
                    GuidanceInput.Settings.TargetVelocity,
                    Agent.MaxSpeedCmps);
            }
            Slot.GuidanceOutput =
              FCrowdMassTargetRegionWork::BuildGuidance(
                GuidanceInput);
            return Slot.GuidanceOutput.bValid
              ? FCrowdBoundaryTaskResult::Success(
                  Slot.GuidanceOutput.Summary.GuidanceHash)
              : FCrowdBoundaryTaskResult::Failure();
          });
    }
  }
  bRegistered = bRegistered
    && BoundaryOrchestrator->AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdDemoRoundWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, FCrowdMassGatherRecord*> RecordById;
        for (FCrowdMassGatherRecord& Record
          : MovementInput.Guidance.Records)
          RecordById.Add(Record.Identity.AgentId, &Record);
        for (const FCrowdGuidanceCandidate& Candidate
          : State->BusinessOutput.GuidanceCandidates)
        {
          FCrowdMassGatherRecord* const* Record =
            RecordById.Find(Candidate.AgentId);
          if (!Record)
            return FCrowdBoundaryTaskResult::Failure();
          (*Record)->Guidance.BusinessOverride = Candidate;
        }
        TMap<int32, FCrowdMassMovementPipelineAgentOverlay*>
          OverlayById;
        for (FCrowdMassMovementPipelineAgentOverlay& Overlay
          : MovementInput.AgentOverlays)
          OverlayById.Add(Overlay.AgentId, &Overlay);
        for (const FCrowdDemoPreparedReactiveMotionStep& Step
          : State->BusinessOutput.ReactiveSteps)
        {
          FCrowdMassMovementPipelineAgentOverlay* const* Overlay =
            OverlayById.Find(Step.AgentId);
          if (!Overlay)
            return FCrowdBoundaryTaskResult::Failure();
          if (Step.bActive)
          {
            (*Overlay)->bVerticalOverride = true;
            (*Overlay)->ProposedZ = Step.ProposedZ;
            (*Overlay)->VerticalVelocityCmps =
              Step.VerticalVelocityCmps;
          }
        }
        if (!State->bUseWorkerV2Target
          && !State->TargetTopologySlots.IsEmpty())
        {
          int32 JoinedTargetCount = 0;
          for (const auto& Slot : State->TargetTopologySlots)
          {
            for (const FCrowdTargetRegionGuidanceResult& Guidance
              : Slot.GuidanceOutput.Results)
            {
              FCrowdMassGatherRecord* const* Record =
                RecordById.Find(Guidance.AgentId);
              if (!Record)
                return FCrowdBoundaryTaskResult::Failure();
              const FVector DesiredVelocity(
                Guidance.DesiredVelocity.X,
                Guidance.DesiredVelocity.Y, 0.0f);
              const FVector DesiredLocation(
                Slot.GuidanceInput.Settings.TargetLocation.X,
                Slot.GuidanceInput.Settings.TargetLocation.Y,
                (*Record)->State.Position.Z);
              const float DesiredYaw = DesiredVelocity.IsNearlyZero()
                ? (*Record)->State.YawDegrees
                : DesiredVelocity.Rotation().Yaw;
              (*Record)->Guidance.TargetRegion =
                FCrowdGuidanceComposeKernel::BuildCandidate(
                  Guidance.AgentId,
                  ECrowdGuidanceProvider::TargetRegion,
                  MovementInput.Guidance.PlanRevision,
                  DesiredVelocity, DesiredLocation, DesiredYaw,
                  Guidance.Mode
                    != ECrowdTargetRegionGuidanceMode::Unrouted);
              ++JoinedTargetCount;
            }
          }
          if (JoinedTargetCount != RecordById.Num())
            return FCrowdBoundaryTaskResult::Failure();
        }
        State->MovementShadowInput = MovementInput;
        State->bMovementShadowInputValid = true;
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(
            MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Particle, ParticleDeps,
      [State]
      {
        FCrowdMassParticlePipelineWorkInput ParticleInput;
        if (!FCrowdDemoRoundWorkGraph::BuildParticleInput(
            State->GraphInput, State->GraphOutput.Movement,
            ParticleInput))
          return FCrowdBoundaryTaskResult::Failure();
        State->GraphOutput.Particle =
          FCrowdMassParticlePipelineWork::Run(ParticleInput);
        return State->GraphOutput.Particle.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Particle.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdDemoRoundWorkGraph::BuildFacingInput(
            State->GraphInput, State->GraphOutput.Movement,
            State->GraphOutput.Particle, FacingInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, const FCrowdParticleConstraintResult*> ParticleById;
        for (const FCrowdParticleConstraintResult& Result
          : State->GraphOutput.Particle.PublishPlan.PreparedResults)
          ParticleById.Add(Result.AgentId, &Result);
        for (FCrowdFacingInput& Agent : FacingInput.Facing.Agents)
        {
          const FCrowdParticleConstraintResult* const* ParticleResult =
            ParticleById.Find(Agent.AgentId);
          const int32* Previous =
            State->PreviousSettleStepsByAgentId.Find(Agent.AgentId);
          const bool* bTerminal =
            State->TerminalOwnerByAgentId.Find(Agent.AgentId);
          if (!ParticleResult || !Previous || !bTerminal)
            return FCrowdBoundaryTaskResult::Failure();
          const bool bSettledThisStep = *bTerminal
            && FVector2f((*ParticleResult)->CorrectedVelocity.X,
              (*ParticleResult)->CorrectedVelocity.Y).Size() <= 20.0f
            && (*ParticleResult)->RealizedCorrection.Size2D() <= 1.0f;
          const int32 Consecutive =
            bSettledThisStep ? *Previous + 1 : 0;
          Agent.bFinalPositionSettled = Consecutive >= 15;
          State->ConsecutiveSettleStepsByAgentId.Add(
            Agent.AgentId, Consecutive);
          State->FinalSettledByAgentId.Add(
            Agent.AgentId, Agent.bFinalPositionSettled);
        }
        State->FacingShadowInput = FacingInput.Facing;
        State->GraphOutput.FacingFinalize =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        State->Output = State->GraphOutput.FacingFinalize;
        State->bCompleted = State->Output.bCompleted;
        return State->bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->Output.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (!bRegistered || !BoundaryOrchestrator->Dispatch())
  {
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DispatchBoundaryFacingWork(
  FCrowdMassFacingFinalizeWorkInput&& Input,
  TMap<int32, int32>&& ConsecutiveSettleStepsByAgentId,
  TMap<int32, bool>&& FinalSettledByAgentId)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Gathering
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bBusinessStaged
    || !BoundaryFacingWorkState->bSharedFlowStaged
    || !BoundaryFacingWorkState->bMovementStaged
    || !BoundaryFacingWorkState->bObstacleStaged
    || Input.Snapshot.FixedStepIndex != GetCurrentFixedStepIndex()
    || Input.Snapshot.PlanRevision != GetCurrentPlanRevision())
    return false;
  BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId =
    MoveTemp(ConsecutiveSettleStepsByAgentId);
  BoundaryFacingWorkState->FinalSettledByAgentId =
    MoveTemp(FinalSettledByAgentId);
  const auto State = BoundaryFacingWorkState;
  State->GraphInput.FacingSettings = Input.Facing.Settings;
  State->GraphInput.FacingTemplates.Reserve(Input.Facing.Agents.Num());
  for (FCrowdFacingInput& FacingInput : Input.Facing.Agents)
  {
    FCrowdDemoRoundFacingTemplate& Template =
      State->GraphInput.FacingTemplates.AddDefaulted_GetRef();
    Template.Input = MoveTemp(FacingInput);
  }
  const FCrowdBoundaryTaskKey Business = {{1}, {101}, 0};
  const FCrowdBoundaryTaskKey SharedFlow = {{2}, {201}, 0};
  const FCrowdBoundaryTaskKey Movement = {{3}, {301}, 0};
  const FCrowdBoundaryTaskKey Constraint = {{4}, {402}, 0};
  const FCrowdBoundaryTaskKey Facing = {{5}, {501}, 0};
  const FCrowdBoundaryTaskKey MovementDeps[] = {Business, SharedFlow};
  const FCrowdBoundaryTaskKey ConstraintDeps[] = {Movement};
  const FCrowdBoundaryTaskKey FacingDeps[] = {Constraint};
  const bool bRegistered =
    BoundaryOrchestrator->AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      SharedFlow, {},
      [State]
      {
        State->GraphOutput.SharedFlow =
          FCrowdMassSharedFlowWork::BuildPreferred(
            State->GraphInput.SharedFlow);
        return State->GraphOutput.SharedFlow.bValid
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdDemoRoundWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, FCrowdMassGatherRecord*> RecordById;
        for (FCrowdMassGatherRecord& Record
          : MovementInput.Guidance.Records)
          RecordById.Add(Record.Identity.AgentId, &Record);
        for (const FCrowdGuidanceCandidate& Candidate
          : State->BusinessOutput.GuidanceCandidates)
        {
          FCrowdMassGatherRecord* const* Record =
            RecordById.Find(Candidate.AgentId);
          if (!Record) return FCrowdBoundaryTaskResult::Failure();
          (*Record)->Guidance.BusinessOverride = Candidate;
        }
        State->MovementShadowInput = MovementInput;
        State->bMovementShadowInputValid = true;
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBoundaryTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Constraint, ConstraintDeps,
      [State]
      {
        const auto& Predicted =
          State->GraphOutput.Movement.MovementPredict.Results;
        if (Predicted.Num()
          != State->BusinessInput.Snapshot.Agents.Num())
          return FCrowdBoundaryTaskResult::Failure();
        TMap<int32, const FCrowdMassPredictedMovement*> ById;
        for (const FCrowdMassPredictedMovement& Value : Predicted)
        {
          if (!Value.bValid || ById.Contains(Value.AgentId))
            return FCrowdBoundaryTaskResult::Failure();
          ById.Add(Value.AgentId, &Value);
        }
        uint64 Hash = 14695981039346656037ull;
        State->ObstacleKinematics.Reset();
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : State->BusinessInput.Snapshot.Agents)
        {
          const FCrowdMassPredictedMovement* const* Value =
            ById.Find(Agent.Identity.AgentId);
          if (!Value) return FCrowdBoundaryTaskResult::Failure();
          const FCrowdDemoSharedFlowConstraintResult Result =
            FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
              State->ObstacleConfig, (*Value)->StartPosition,
              (*Value)->PredictedPosition,
              State->ObstacleFixedStepSeconds, false);
          FCrowdMassFinalKinematicState& Kinematic =
            State->ObstacleKinematics.AddDefaulted_GetRef();
          Kinematic.AgentId = Agent.Identity.AgentId;
          Kinematic.Position = Result.Location;
          Kinematic.Velocity = Result.Velocity;
          Kinematic.bValid = true;
          State->ObstacleMaxReprojectDeltaCm = FMath::Max(
            State->ObstacleMaxReprojectDeltaCm,
            Result.FlowBoundsReprojectDeltaCm);
          Hash = FoldBoundaryHash(
            Hash, static_cast<uint32>(Kinematic.AgentId));
          Hash = FoldBoundaryHash(
            Hash, GetTypeHash(Kinematic.Position));
        }
        State->ObstacleKinematics.Sort([](const auto& A,
          const auto& B)
        {
          return A.AgentId < B.AgentId;
        });
        return State->ObstacleKinematics.Num() == Predicted.Num()
          ? FCrowdBoundaryTaskResult::Success(Hash)
          : FCrowdBoundaryTaskResult::Failure();
      })
    && BoundaryOrchestrator->AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdDemoRoundWorkGraph::
          BuildFacingInputFromKinematics(
            State->GraphInput, State->BusinessInput.Snapshot,
            State->GraphOutput.Movement,
            State->ObstacleKinematics, FacingInput))
          return FCrowdBoundaryTaskResult::Failure();
        State->FacingShadowInput = FacingInput.Facing;
        State->Output =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        State->GraphOutput.FacingFinalize = State->Output;
        State->bCompleted = State->Output.bCompleted;
        const uint64 Hash = FoldBoundaryHash(
          State->Output.Finalize.CommitPlan.StableHash,
          State->Output.Facing.StableHash);
        return State->bCompleted
          ? FCrowdBoundaryTaskResult::Success(Hash)
          : FCrowdBoundaryTaskResult::Failure();
      });
  if (!bRegistered || !BoundaryOrchestrator->Dispatch())
  {
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

ECrowdBoundaryPollResult
UCrowdDemoRoundSimPipelineSubsystem::TryPrepareRoundApply()
{
  LastBoundaryPrepareCheckpoint = 1;
  const auto RejectPrepare = [this](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryPrepareRejected step=%d reason=%s orchestrator=%d facing_state=%d transaction_state=%d"),
      GetCurrentFixedStepIndex(), Reason,
      BoundaryOrchestrator.IsValid() ? 1 : 0,
      BoundaryFacingWorkState.IsValid() ? 1 : 0,
      BoundaryOrchestrator.IsValid()
        ? static_cast<int32>(BoundaryOrchestrator->GetState()) : -1);
    return ECrowdBoundaryPollResult::Failed;
  };
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || !BoundaryFacingWorkState.IsValid())
    return RejectPrepare(TEXT("entry_state"));
  if (!DrainWorkerV2MovementShadowComparisons())
    return RejectPrepare(TEXT("movement_shadow_drain"));
  if (BoundaryPendingFrameCount > 0
    && BoundaryPendingFrameCount % 120 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoBoundaryPendingProbe step=%d state=%d pending_frames=%d worker_v2_submitted=%d movement_tail_submitted=%d movement_tail_completed=%d movement_tail_consumed=%d"),
      GetCurrentFixedStepIndex(),
      static_cast<int32>(BoundaryOrchestrator->GetState()),
      BoundaryPendingFrameCount,
      BoundaryFacingWorkState->bWorkerV2InputSubmitted ? 1 : 0,
      BoundaryFacingWorkState->bWorkerMovementTailSubmitted ? 1 : 0,
      BoundaryFacingWorkState->WorkerMovementTail.IsValid()
        && BoundaryFacingWorkState->WorkerMovementTail->
          bCompleted.Load() ? 1 : 0,
      BoundaryFacingWorkState->bWorkerMovementTailConsumed ? 1 : 0);
  }

  if (!BoundaryOrchestrator->Matches(
      BoundarySnapshot, BoundaryGeneration))
  {
    ++BoundaryStaleResultCount;
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryTransactionStale step=%d generation=%llu stale_count=%d"),
      GetCurrentFixedStepIndex(),
      static_cast<unsigned long long>(BoundaryGeneration),
      BoundaryStaleResultCount);
    return ECrowdBoundaryPollResult::Failed;
  }

  ECrowdBoundaryPollResult PollResult =
    ECrowdBoundaryPollResult::Failed;
  const ECrowdBoundaryTransactionState TransactionState =
    BoundaryOrchestrator->GetState();
  if (TransactionState
      == ECrowdBoundaryTransactionState::Working)
  {
    PollResult = BoundaryOrchestrator->Poll();
  }
  else if (TransactionState
      == ECrowdBoundaryTransactionState::Merging)
  {
    PollResult = ECrowdBoundaryPollResult::Ready;
  }
  if (PollResult == ECrowdBoundaryPollResult::Pending)
  {
    ++BoundaryPendingFrameCount;
    return PollResult;
  }
  if (PollResult == ECrowdBoundaryPollResult::Failed)
  {
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBoundaryWorkFailed step=%d state=%d tasks=%d"),
      GetCurrentFixedStepIndex(),
      static_cast<int32>(LastBoundaryTransactionResult.State),
      LastBoundaryTransactionResult.Tasks.Num());
    for (const FCrowdBoundaryCompletedTask& Task
      : LastBoundaryTransactionResult.Tasks)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoBoundaryTaskFailed step=%d stage=%d task_type=%d scope=%llu succeeded=%d off_gt=%d hash=%llu"),
        GetCurrentFixedStepIndex(),
        static_cast<int32>(Task.Key.StageId.Value),
        static_cast<int32>(Task.Key.TaskTypeId.Value),
        static_cast<unsigned long long>(Task.Key.ScopeKey),
        Task.Result.bSucceeded ? 1 : 0,
        Task.Result.bRanOffGameThread ? 1 : 0,
        static_cast<unsigned long long>(Task.Result.StableHash));
    }
    return PollResult;
  }
  LastBoundaryPrepareCheckpoint = 2;
  if (!BoundaryFacingWorkState->bWorkerV2InputSubmitted)
  {
    if (!SubmitWorkerV2BoundaryInput())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerV2PreparedInputSubmitFailed step=%d"),
        GetCurrentFixedStepIndex());
      BoundaryOrchestrator->Fail();
      LastBoundaryTransactionResult =
        BoundaryOrchestrator->BuildResult();
      return ECrowdBoundaryPollResult::Failed;
    }
  }
  UMassCrowdRuntimeSubsystem* WorkerV2RuntimeSubsystem =
    GetWorld()
      ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
      : nullptr;
  if (!WorkerV2RuntimeSubsystem)
    return RejectPrepare(TEXT("worker_runtime_missing"));
  TMap<FCrowdWorkerDirtyStateKey, FCrowdWorkerDomainProxyState>
    PendingPreparedDomains;
  uint64 PendingAppliedInputSequence = 0;
  if (const FCrowdDemoPreparedRoundCommitPlan* Pending =
      PeekPreparedRoundCommitPlan())
  {
    const FCrowdWorkerPreparedResultApply& Prepared =
      Pending->PreparedProxyResult;
    PendingAppliedInputSequence =
      Prepared.Batch.LastAppliedInputSequence;
    PendingPreparedDomains.Reserve(
      Prepared.Batch.StatePatches.Num());
    for (int32 PatchIndex = 0;
      PatchIndex < Prepared.Batch.StatePatches.Num(); ++PatchIndex)
    {
      if (!Prepared.StatePatchStableSlots.IsValidIndex(PatchIndex)
        || Prepared.StatePatchStableSlots[PatchIndex] == INDEX_NONE)
        continue;
      const FCrowdWorkerStatePatch& Patch =
        Prepared.Batch.StatePatches[PatchIndex];
      if (Patch.StateFieldId == 0)
        continue;
      const ECrowdWorkerField Field =
        static_cast<ECrowdWorkerField>(Patch.StateFieldId - 1);
      if (Field >= ECrowdWorkerField::Count)
        continue;
      FCrowdWorkerDomainProxyState State;
      State.EntityRef = Patch.EntityRef;
      State.Field = Field;
      State.State = Patch.State;
      State.WorkerEpoch = Patch.WorkerEpoch;
      State.SourceInputSequence = Patch.SourceInputSequence;
      State.PublishSequence = Prepared.Batch.PublishSequence;
      PendingPreparedDomains.Add(
        {Patch.EntityRef, Field}, MoveTemp(State));
    }
  }
  const auto FindResultDomain = [&PendingPreparedDomains](
    const FCrowdWorkerResultApplyProxy& Proxy,
    const FCrowdStableEntityRef& EntityRef,
    const ECrowdWorkerField Field)
      -> const FCrowdWorkerDomainProxyState*
  {
    if (const FCrowdWorkerDomainProxyState* Pending =
        PendingPreparedDomains.Find({EntityRef, Field}))
      return Pending;
    return Proxy.FindDomain(EntityRef, Field);
  };
  const ECrowdWorkerMovementAuthorityMode
    ConfiguredMovementMode =
      WorkerV2RuntimeSubsystem->GetWorkerMovementAuthority().GetMode();
  ECrowdDemoWorkerParticleAuthorityMode ParticleMode =
    ECrowdDemoWorkerParticleAuthorityMode::Shadow;
  TSet<FCrowdStableEntityRef> ParticleCanaries;
  ECrowdDemoWorkerTargetAuthorityMode TargetMode =
    ECrowdDemoWorkerTargetAuthorityMode::Shadow;
  TSet<FCrowdStableEntityRef> TargetCanaries;
  ECrowdDemoWorkerProjectileAuthorityMode ProjectileMode =
    ECrowdDemoWorkerProjectileAuthorityMode::Shadow;
  ECrowdDemoWorkerCombatAuthorityMode CombatMode =
    ECrowdDemoWorkerCombatAuthorityMode::Shadow;
  if (!ResolveWorkerParticleAuthority(
      BoundarySnapshot, ParticleMode, ParticleCanaries)
    || !ResolveWorkerTargetAuthority(
      BoundarySnapshot, TargetMode, TargetCanaries)
    || !ResolveWorkerProjectileAuthority(ProjectileMode)
    || !ResolveWorkerCombatAuthority(CombatMode)
    || (ParticleMode
        != ECrowdDemoWorkerParticleAuthorityMode::Shadow
      && ConfiguredMovementMode
        != ECrowdWorkerMovementAuthorityMode::Production)
    || (TargetMode
        != ECrowdDemoWorkerTargetAuthorityMode::Shadow
      && ConfiguredMovementMode
        != ECrowdWorkerMovementAuthorityMode::Production))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerDomainAuthorityConfig movement_mode=%d particle_mode=%d particle_canaries=%d target_mode=%d target_canaries=%d projectile_mode=%d combat_mode=%d"),
      static_cast<int32>(ConfiguredMovementMode),
      static_cast<int32>(ParticleMode),
      ParticleCanaries.Num(),
      static_cast<int32>(TargetMode),
      TargetCanaries.Num(),
      static_cast<int32>(ProjectileMode),
      static_cast<int32>(CombatMode));
    return ECrowdBoundaryPollResult::Failed;
  }
  const bool bRequireWorkerV2MovementReady =
    ConfiguredMovementMode
      != ECrowdWorkerMovementAuthorityMode::Shadow;
  const uint64 ExpectedWorkerV2Sequence =
    BoundaryFacingWorkState->WorkerV2InputSequence;
  bool bWorkerV2MovementReady = !bRequireWorkerV2MovementReady
    || ExpectedWorkerV2Sequence != 0;
  if (bRequireWorkerV2MovementReady)
  {
    const FCrowdWorkerResultApplyProxy& Proxy =
      WorkerV2RuntimeSubsystem->GetWorkerResultApplyProxy();
    if (FMath::Max(
        Proxy.GetMetrics().LastAppliedInputSequence,
        PendingAppliedInputSequence)
        < ExpectedWorkerV2Sequence)
      bWorkerV2MovementReady = false;
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      const FCrowdWorkerDomainProxyState* Movement =
        FindResultDomain(Proxy,
            Agent.AgentFacts.StableEntityRef,
            ECrowdWorkerField::Movement);
      if (!Movement
        || Movement->SourceInputSequence
          != ExpectedWorkerV2Sequence)
      {
        bWorkerV2MovementReady = false;
        break;
      }
      if (ParticleMode
          != ECrowdDemoWorkerParticleAuthorityMode::Shadow)
      {
        const FCrowdWorkerDomainProxyState* Particle =
          FindResultDomain(Proxy,
            Agent.AgentFacts.StableEntityRef,
            ECrowdWorkerField::Particle);
        if (!Particle
          || Particle->SourceInputSequence
            > ExpectedWorkerV2Sequence)
        {
          bWorkerV2MovementReady = false;
          break;
        }
      }
    }
    const bool bProjectileCombat =
      BoundaryFacingWorkState->BusinessInput.Rules.
        RangedCombatSettings.bEnabled != 0;
    if (bWorkerV2MovementReady && bProjectileCombat)
    {
      const FCrowdStableEntityRef AnchorEntity =
        BoundarySnapshot.Agents[0].AgentFacts.StableEntityRef;
      const FCrowdWorkerDomainProxyState* WorkerProjectile =
        FindResultDomain(Proxy,
          AnchorEntity, ECrowdWorkerField::Projectile);
      if (!WorkerProjectile
        || WorkerProjectile->SourceInputSequence
          < ExpectedWorkerV2Sequence)
      {
        bWorkerV2MovementReady = false;
      }
      else if (WorkerProjectile->SourceInputSequence
          != ExpectedWorkerV2Sequence)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerProjectileSequence expected_input=%llu actual_input=%llu"),
          ExpectedWorkerV2Sequence,
          WorkerProjectile->SourceInputSequence);
        return ECrowdBoundaryPollResult::Failed;
      }
      else
      {
        FCrowdWorkerProjectileState WorkerState;
        FCrowdDemoWorkerCombatHostResult WorkerCombatResult;
        const FCrowdDemoProjectileStepSummary& Legacy =
          BoundaryFacingWorkState->BusinessOutput.Commit.
            ProjectileSummary;
        if (!FCrowdWorkerProjectileStateCodec::Decode(
              WorkerProjectile->State.Payload, WorkerState)
          || !FCrowdDemoWorkerCombatHostResultCodec::Decode(
              WorkerState.HostCombatResult, WorkerCombatResult)
          || WorkerState.ControlRevision == 0
          || WorkerState.Prepared.FixedStepIndex
            != GetCurrentFixedStepIndex()
          || WorkerCombatResult.FixedStepIndex
            != GetCurrentFixedStepIndex()
          || WorkerCombatResult.AttackSummary.TargetAcquiredCount
            != Legacy.TargetAcquiredCount
          || WorkerCombatResult.AttackSummary.CompletedWindupCount
            != Legacy.CompletedWindupCount
          || WorkerCombatResult.AttackSummary.InvalidTargetLifecycleCount
            != Legacy.InvalidTargetLifecycleCount
          || WorkerCombatResult.AttackSummary.AttackStateHash
            != Legacy.AttackStateHash
          || WorkerCombatResult.HitSummary.InputHitCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.InputHitCount
          || WorkerCombatResult.HitSummary.AppliedHitCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.AppliedHitCount
          || WorkerCombatResult.HitSummary.DuplicateHitCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.DuplicateHitCount
          || WorkerCombatResult.HitSummary.StaleLifecycleCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.StaleLifecycleCount
          || WorkerCombatResult.HitSummary.MissingTargetCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.MissingTargetCount
          || WorkerCombatResult.HitSummary.AlreadyDeadCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.AlreadyDeadCount
          || WorkerCombatResult.HitSummary.DamageAppliedAgentCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.DamageAppliedAgentCount
          || WorkerCombatResult.HitSummary.ReactiveAgentCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.ReactiveAgentCount
          || WorkerCombatResult.HitSummary.DeathCount
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.DeathCount
          || WorkerCombatResult.HitSummary.StableHash
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.StableHash
          || WorkerState.Prepared.Summary.bValid
            != Legacy.bValid
          || WorkerState.Prepared.Summary.SpawnedCount
            != Legacy.SpawnedCount
          || WorkerState.Prepared.Summary.ActiveCount
            != Legacy.ActiveCount
          || WorkerState.Prepared.Summary.ImpactedCount
            != Legacy.ImpactedCount
          || WorkerState.Prepared.Summary.ExpiredCount
            != Legacy.ExpiredCount
          || WorkerState.Prepared.Summary.DuplicateFireCount
            != Legacy.DuplicateFireCount
          || WorkerState.Prepared.Summary.InvalidProjectileCount
            != Legacy.InvalidProjectileCount
          || WorkerState.Prepared.Summary.EnvironmentImpactCount
            != Legacy.EnvironmentImpactCount
          || WorkerState.Prepared.Summary.BroadphaseCandidateCount
            != Legacy.BroadphaseCandidateCount
          || WorkerState.Prepared.Summary.SweepTestCount
            != Legacy.SweepTestCount
          || WorkerState.Prepared.Summary.ProjectileStateHash
            != Legacy.ProjectileStateHash
          || WorkerState.Prepared.Summary.EventHash
            != Legacy.EventHash
          || WorkerState.Prepared.States.Num()
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              Projectiles.Num()
          || WorkerState.ResolvedHits.Hits.Num()
            != BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.InputHitCount)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoWorkerProjectileShadowMismatch step=%d expected_input=%llu worker_states=%d legacy_states=%d worker_resolved_hits=%d legacy_input_hits=%d legacy_applied_hits=%d worker_spawned=%d legacy_spawned=%d worker_active=%d legacy_active=%d worker_impacted=%d legacy_impacted=%d worker_expired=%d legacy_expired=%d worker_environment=%d legacy_environment=%d worker_projectile_hash=%u legacy_projectile_hash=%u worker_event_hash=%u legacy_event_hash=%u"),
            GetCurrentFixedStepIndex(),
            ExpectedWorkerV2Sequence,
            WorkerState.Prepared.States.Num(),
            BoundaryFacingWorkState->BusinessOutput.Commit.
              Projectiles.Num(),
            WorkerState.ResolvedHits.Hits.Num(),
            BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.InputHitCount,
            BoundaryFacingWorkState->BusinessOutput.Commit.
              HitSummary.AppliedHitCount,
            WorkerState.Prepared.Summary.SpawnedCount,
            Legacy.SpawnedCount,
            WorkerState.Prepared.Summary.ActiveCount,
            Legacy.ActiveCount,
            WorkerState.Prepared.Summary.ImpactedCount,
            Legacy.ImpactedCount,
            WorkerState.Prepared.Summary.ExpiredCount,
            Legacy.ExpiredCount,
            WorkerState.Prepared.Summary.EnvironmentImpactCount,
            Legacy.EnvironmentImpactCount,
            WorkerState.Prepared.Summary.ProjectileStateHash,
            Legacy.ProjectileStateHash,
            WorkerState.Prepared.Summary.EventHash,
            Legacy.EventHash);
          TArray<FCrowdProjectileState> WorkerProjectiles =
            WorkerState.Prepared.States;
          TArray<FCrowdProjectileState> LegacyProjectiles =
            BoundaryFacingWorkState->BusinessOutput.Commit.Projectiles;
          const auto ProjectileLess = [](
            const FCrowdProjectileState& A,
            const FCrowdProjectileState& B)
          {
            return A.ProjectileId < B.ProjectileId;
          };
          WorkerProjectiles.Sort(ProjectileLess);
          LegacyProjectiles.Sort(ProjectileLess);
          const int32 DiagnosticCount = FMath::Max(
            WorkerProjectiles.Num(), LegacyProjectiles.Num());
          for (int32 ProjectileIndex = 0;
            ProjectileIndex < DiagnosticCount; ++ProjectileIndex)
          {
            const FCrowdProjectileState* WorkerProjectileEntry =
              WorkerProjectiles.IsValidIndex(ProjectileIndex)
                ? &WorkerProjectiles[ProjectileIndex] : nullptr;
            const FCrowdProjectileState* LegacyProjectile =
              LegacyProjectiles.IsValidIndex(ProjectileIndex)
                ? &LegacyProjectiles[ProjectileIndex] : nullptr;
            UE_LOG(LogTemp, Display,
              TEXT("CrowdDemoWorkerProjectileShadowState step=%d index=%d worker_id=%llu legacy_id=%llu worker_spawn=%lld legacy_spawn=%lld worker_age=%d legacy_age=%d worker_pos=(%.3f,%.3f,%.3f) legacy_pos=(%.3f,%.3f,%.3f) worker_vel=(%.3f,%.3f,%.3f) legacy_vel=(%.3f,%.3f,%.3f) worker_last_hit=%llu legacy_last_hit=%llu worker_flags=%d%d%d legacy_flags=%d%d%d"),
              GetCurrentFixedStepIndex(), ProjectileIndex,
              WorkerProjectileEntry ? WorkerProjectileEntry->ProjectileId : 0,
              LegacyProjectile ? LegacyProjectile->ProjectileId : 0,
              WorkerProjectileEntry ? WorkerProjectileEntry->SpawnFixedStep : -1,
              LegacyProjectile ? LegacyProjectile->SpawnFixedStep : -1,
              WorkerProjectileEntry ? WorkerProjectileEntry->AgeFixedSteps : -1,
              LegacyProjectile ? LegacyProjectile->AgeFixedSteps : -1,
              WorkerProjectileEntry ? WorkerProjectileEntry->Position.X : 0.0,
              WorkerProjectileEntry ? WorkerProjectileEntry->Position.Y : 0.0,
              WorkerProjectileEntry ? WorkerProjectileEntry->Position.Z : 0.0,
              LegacyProjectile ? LegacyProjectile->Position.X : 0.0,
              LegacyProjectile ? LegacyProjectile->Position.Y : 0.0,
              LegacyProjectile ? LegacyProjectile->Position.Z : 0.0,
              WorkerProjectileEntry ? WorkerProjectileEntry->Velocity.X : 0.0,
              WorkerProjectileEntry ? WorkerProjectileEntry->Velocity.Y : 0.0,
              WorkerProjectileEntry ? WorkerProjectileEntry->Velocity.Z : 0.0,
              LegacyProjectile ? LegacyProjectile->Velocity.X : 0.0,
              LegacyProjectile ? LegacyProjectile->Velocity.Y : 0.0,
              LegacyProjectile ? LegacyProjectile->Velocity.Z : 0.0,
              WorkerProjectileEntry
                ? WorkerProjectileEntry->LastHitTarget.StableEntityId : 0,
              LegacyProjectile
                ? LegacyProjectile->LastHitTarget.StableEntityId : 0,
              WorkerProjectileEntry && WorkerProjectileEntry->bActive ? 1 : 0,
              WorkerProjectileEntry && WorkerProjectileEntry->bImpacted ? 1 : 0,
              WorkerProjectileEntry && WorkerProjectileEntry->bExpired ? 1 : 0,
              LegacyProjectile && LegacyProjectile->bActive ? 1 : 0,
              LegacyProjectile && LegacyProjectile->bImpacted ? 1 : 0,
              LegacyProjectile && LegacyProjectile->bExpired ? 1 : 0);
          }
          return ECrowdBoundaryPollResult::Failed;
        }
        int32 VerifiedCombatStateCount = 0;
        for (const FCrowdMassBoundaryAgentRecord& Agent :
          BoundarySnapshot.Agents)
        {
          const FCrowdWorkerDomainProxyState* WorkerCombat =
            FindResultDomain(Proxy,
              Agent.AgentFacts.StableEntityRef,
              ECrowdWorkerField::Combat);
          if (!WorkerCombat
            || WorkerCombat->SourceInputSequence
              < ExpectedWorkerV2Sequence)
          {
            bWorkerV2MovementReady = false;
            break;
          }
          if (WorkerCombat->SourceInputSequence
              != ExpectedWorkerV2Sequence)
          {
            UE_LOG(LogTemp, Error,
              TEXT("VIOLATION CrowdDemoWorkerCombatSequence agent=%d expected_input=%llu actual_input=%llu"),
              Agent.Identity.AgentId,
              ExpectedWorkerV2Sequence,
              WorkerCombat->SourceInputSequence);
            return ECrowdBoundaryPollResult::Failed;
          }
          FCrowdWorkerCombatState WorkerCombatState;
          FCrowdDemoCombatAgentState WorkerHostState;
          const FCrowdDemoRangedCombatAgent* LegacyAgent =
            BoundaryFacingWorkState->BusinessOutput.Commit.Agents.
              FindByPredicate(
                [&Agent](const FCrowdDemoRangedCombatAgent& Candidate)
                {
                  return Candidate.AgentId
                    == Agent.Identity.AgentId;
                });
          const FCrowdDemoPreparedReactiveMotionStep* LegacyReactive =
            BoundaryFacingWorkState->BusinessOutput.ReactiveSteps.
              FindByPredicate(
                [&Agent](
                  const FCrowdDemoPreparedReactiveMotionStep& Candidate)
                {
                  return Candidate.AgentId
                    == Agent.Identity.AgentId;
                });
          FCrowdWorkerPayload LegacyHostPayload;
          if (!LegacyAgent || !LegacyReactive
            || !FCrowdWorkerCombatStateCodec::Decode(
              WorkerCombat->State.Payload, WorkerCombatState)
            || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
              WorkerCombatState.HostState, WorkerHostState)
            || !FCrowdDemoWorkerCombatStatePayloadCodec::Encode(
              LegacyAgent->Combat, LegacyHostPayload)
            || WorkerCombatState.HostState != LegacyHostPayload
            || WorkerCombatState.bAlive
              != LegacyAgent->Combat.bAlive
            || WorkerCombatState.bReactiveActive
              != LegacyReactive->bActive
            || !WorkerCombatState.HorizontalReactiveVelocity.Equals(
              LegacyAgent->Combat.HorizontalReactiveVelocity, 0.0)
            || !FMath::IsNearlyEqual(
              WorkerCombatState.ProposedZ,
              LegacyReactive->ProposedZ, 0.0f)
            || !FMath::IsNearlyEqual(
              WorkerCombatState.VerticalVelocityCmps,
              LegacyReactive->VerticalVelocityCmps, 0.0f))
          {
            UE_LOG(LogTemp, Error,
              TEXT("VIOLATION CrowdDemoWorkerCombatShadowMismatch agent=%d step=%d expected_input=%llu worker=%d legacy=%d worker_host_hash=%llu legacy_host_hash=%llu health=%.3f/%.3f max_health=%.3f/%.3f lifecycle=%d/%d alive=%d/%d business=%d/%d business_revision=%d/%d business_enter=%d/%d target=%d:%d/%d:%d attack=%d/%d attack_enter=%d/%d cooldown=%d/%d locked_target=%d:%d/%d:%d locked_location=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) fire=%d/%d fire_issued=%d/%d reactive=%d/%d reactive_revision=%d/%d reactive_range=%d:%d/%d:%d restore=%d/%d apex=%d/%d landing=%d/%d hit=%llu/%llu hit_flash_revision=%d/%d hit_flash_start=%.6f/%.6f hit_flash_duration=%.6f/%.6f hit_flash_profile=%u/%u hit_flash_peak=%.6f/%.6f visual=%d/%d visual_revision=%d/%d visual_start=%.6f/%.6f visual_seed=%u/%u"),
              Agent.Identity.AgentId,
              GetCurrentFixedStepIndex(),
              ExpectedWorkerV2Sequence,
              WorkerCombat ? 1 : 0,
              LegacyAgent ? 1 : 0,
              WorkerCombatState.HostState.StableHash,
              LegacyHostPayload.StableHash,
              WorkerHostState.Health,
              LegacyAgent ? LegacyAgent->Combat.Health : -1.0f,
              WorkerHostState.MaxHealth,
              LegacyAgent ? LegacyAgent->Combat.MaxHealth : -1.0f,
              static_cast<int32>(WorkerHostState.LifecycleState),
              LegacyAgent ? static_cast<int32>(
                LegacyAgent->Combat.LifecycleState) : -1,
              WorkerHostState.bAlive ? 1 : 0,
              LegacyAgent && LegacyAgent->Combat.bAlive ? 1 : 0,
              static_cast<int32>(WorkerHostState.BusinessState),
              LegacyAgent
                ? static_cast<int32>(
                  LegacyAgent->Combat.BusinessState) : -1,
              WorkerHostState.BusinessStateRevision,
              LegacyAgent
                ? LegacyAgent->Combat.BusinessStateRevision : -1,
              WorkerHostState.BusinessStateEnterFixedStep,
              LegacyAgent
                ? LegacyAgent->Combat.BusinessStateEnterFixedStep : -1,
              WorkerHostState.TargetAgentId,
              WorkerHostState.TargetLifecycleSerial,
              LegacyAgent ? LegacyAgent->Combat.TargetAgentId : -1,
              LegacyAgent
                ? LegacyAgent->Combat.TargetLifecycleSerial : -1,
              static_cast<int32>(WorkerHostState.AttackPhase),
              LegacyAgent
                ? static_cast<int32>(
                  LegacyAgent->Combat.AttackPhase) : -1,
              WorkerHostState.AttackPhaseEnterFixedStep,
              LegacyAgent
                ? LegacyAgent->Combat.AttackPhaseEnterFixedStep : -1,
              WorkerHostState.CooldownEndFixedStep,
              LegacyAgent
                ? LegacyAgent->Combat.CooldownEndFixedStep : -1,
              WorkerHostState.LockedTargetAgentId,
              WorkerHostState.LockedTargetLifecycleSerial,
              LegacyAgent
                ? LegacyAgent->Combat.LockedTargetAgentId : -1,
              LegacyAgent
                ? LegacyAgent->Combat.LockedTargetLifecycleSerial : -1,
              WorkerHostState.LockedTargetLocation.X,
              WorkerHostState.LockedTargetLocation.Y,
              WorkerHostState.LockedTargetLocation.Z,
              LegacyAgent
                ? LegacyAgent->Combat.LockedTargetLocation.X : 0.0,
              LegacyAgent
                ? LegacyAgent->Combat.LockedTargetLocation.Y : 0.0,
              LegacyAgent
                ? LegacyAgent->Combat.LockedTargetLocation.Z : 0.0,
              WorkerHostState.FireSequence,
              LegacyAgent
                ? LegacyAgent->Combat.FireSequence : -1,
              WorkerHostState.bFireRequestIssued ? 1 : 0,
              LegacyAgent
                && LegacyAgent->Combat.bFireRequestIssued ? 1 : 0,
              static_cast<int32>(WorkerHostState.ReactiveMode),
              LegacyAgent
                ? static_cast<int32>(
                  LegacyAgent->Combat.ReactiveMode) : -1,
              WorkerHostState.ReactiveRevision,
              LegacyAgent
                ? LegacyAgent->Combat.ReactiveRevision : -1,
              WorkerHostState.ReactiveStartFixedStep,
              WorkerHostState.ReactiveEndFixedStep,
              LegacyAgent
                ? LegacyAgent->Combat.ReactiveStartFixedStep : -1,
              LegacyAgent
                ? LegacyAgent->Combat.ReactiveEndFixedStep : -1,
              static_cast<int32>(WorkerHostState.RestoreBusinessState),
              LegacyAgent ? static_cast<int32>(
                LegacyAgent->Combat.RestoreBusinessState) : -1,
              WorkerHostState.ApexCount,
              LegacyAgent ? LegacyAgent->Combat.ApexCount : -1,
              WorkerHostState.LandingCount,
              LegacyAgent ? LegacyAgent->Combat.LandingCount : -1,
              WorkerHostState.LastConsumedHitEventId,
              LegacyAgent
                ? LegacyAgent->Combat.LastConsumedHitEventId : 0,
              WorkerHostState.HitFlashRevision,
              LegacyAgent ? LegacyAgent->Combat.HitFlashRevision : -1,
              WorkerHostState.HitFlashStartServerTimeSeconds,
              LegacyAgent
                ? LegacyAgent->Combat.HitFlashStartServerTimeSeconds : -1.0,
              WorkerHostState.HitFlashDurationSeconds,
              LegacyAgent
                ? LegacyAgent->Combat.HitFlashDurationSeconds : -1.0,
              WorkerHostState.HitFlashProfileKey,
              LegacyAgent
                ? LegacyAgent->Combat.HitFlashProfileKey : 0,
              WorkerHostState.HitFlashPeakIntensity,
              LegacyAgent
                ? LegacyAgent->Combat.HitFlashPeakIntensity : -1.0,
              static_cast<int32>(WorkerHostState.VisualState),
              LegacyAgent
                ? static_cast<int32>(
                  LegacyAgent->Combat.VisualState) : -1,
              WorkerHostState.VisualRevision,
              LegacyAgent
                ? LegacyAgent->Combat.VisualRevision : -1,
              WorkerHostState.VisualStateStartServerTimeSeconds,
              LegacyAgent
                ? LegacyAgent->Combat.VisualStateStartServerTimeSeconds
                : -1.0,
              WorkerHostState.VisualPhaseSeed,
              LegacyAgent
                ? LegacyAgent->Combat.VisualPhaseSeed : 0);
            return ECrowdBoundaryPollResult::Failed;
          }
          ++VerifiedCombatStateCount;
        }
        if (!BoundaryFacingWorkState->bWorkerMovementTailSubmitted
          && (GetCurrentFixedStepIndex() == 0
            || GetCurrentFixedStepIndex() % 300 == 0))
        {
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoWorkerProjectileCheckpoint mode=%d step=%d expected_input=%llu states=%d hits=%d combat_states=%d projectile_hash=%u event_hash=%u source=PersistentRuntimeAuthority"),
            static_cast<int32>(ProjectileMode),
            GetCurrentFixedStepIndex(),
            ExpectedWorkerV2Sequence,
            WorkerState.Prepared.States.Num(),
            WorkerState.ResolvedHits.Hits.Num(),
            VerifiedCombatStateCount,
            WorkerState.Prepared.Summary.ProjectileStateHash,
            WorkerState.Prepared.Summary.EventHash);
        }
      }
    }
    if (!BoundaryFacingWorkState->bUseWorkerV2Target
      && bWorkerV2MovementReady
      && !BoundaryFacingWorkState->TargetTopologySlots.IsEmpty()
      && TargetMode
        != ECrowdDemoWorkerTargetAuthorityMode::Shadow)
    {
      struct FLegacyTargetFact
      {
        uint32 CohortKey = 0;
        int32 TargetRevision = INDEX_NONE;
        const FCrowdTargetRegionGuidanceResult* Guidance = nullptr;
        uint32 ExecutionHash = 0;
        uint32 GuidanceHash = 0;
      };
      TMap<int32, FLegacyTargetFact> LegacyByAgentId;
      for (const auto& Slot :
        BoundaryFacingWorkState->TargetTopologySlots)
      {
        for (const FCrowdTargetRegionGuidanceResult& Guidance :
          Slot.GuidanceOutput.Results)
        {
          if (LegacyByAgentId.Contains(Guidance.AgentId))
            return RejectPrepare(TEXT("target_guidance_duplicate"));
          LegacyByAgentId.Add(Guidance.AgentId, {
            Slot.CohortKey,
            Slot.PlanInput.TargetRevision,
            &Guidance,
            Slot.GuidanceOutput.Execution.ExecutionHash,
            Slot.GuidanceOutput.Summary.GuidanceHash});
        }
      }
      int32 VerifiedTargetOwnerCount = 0;
      for (const FCrowdMassBoundaryAgentRecord& Agent :
        BoundarySnapshot.Agents)
      {
        const FCrowdStableEntityRef EntityRef =
          Agent.AgentFacts.StableEntityRef;
        const bool bWorkerTargetOwner =
          TargetMode
            == ECrowdDemoWorkerTargetAuthorityMode::Production
          || TargetCanaries.Contains(EntityRef);
        if (!bWorkerTargetOwner)
          continue;
        const FLegacyTargetFact* Legacy =
          LegacyByAgentId.Find(Agent.Identity.AgentId);
        const FCrowdWorkerDomainProxyState* Worker =
          FindResultDomain(
            Proxy, EntityRef, ECrowdWorkerField::Target);
        FCrowdWorkerTargetState WorkerState;
        if (!Legacy || !Legacy->Guidance || !Worker
          || Worker->SourceInputSequence
            > ExpectedWorkerV2Sequence
          || !FCrowdWorkerTargetStateCodec::Decode(
            Worker->State.Payload, WorkerState))
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoWorkerTargetMissing agent=%d expected_input=%llu legacy=%d worker=%d worker_input=%llu"),
            Agent.Identity.AgentId,
            ExpectedWorkerV2Sequence,
            Legacy && Legacy->Guidance ? 1 : 0,
            Worker ? 1 : 0,
            Worker ? Worker->SourceInputSequence : 0);
          return ECrowdBoundaryPollResult::Failed;
        }
        if (WorkerState.CohortKey != Legacy->CohortKey
          || WorkerState.TargetRevision
            != Legacy->TargetRevision)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoWorkerTargetRevisionMismatch agent=%d expected_cohort=%u actual_cohort=%u expected_revision=%d actual_revision=%d"),
            Agent.Identity.AgentId,
            Legacy->CohortKey,
            WorkerState.CohortKey,
            Legacy->TargetRevision,
            WorkerState.TargetRevision);
          return ECrowdBoundaryPollResult::Failed;
        }
        if (TargetMode
            == ECrowdDemoWorkerTargetAuthorityMode::Canary)
        {
          const FVector LegacyVelocity(
            Legacy->Guidance->DesiredVelocity.X,
            Legacy->Guidance->DesiredVelocity.Y, 0.0);
          if (WorkerState.CurrentCellKey
                != Legacy->Guidance->CurrentCellKey
            || WorkerState.NextCellKey
                != Legacy->Guidance->NextCellKey
            || WorkerState.DemandRegionKey
                != Legacy->Guidance->DemandRegionKey
            || WorkerState.Mode != Legacy->Guidance->Mode
            || !WorkerState.DesiredVelocity.Equals(
              LegacyVelocity, 0.0)
            || WorkerState.ExecutionHash
                != Legacy->ExecutionHash
            || WorkerState.GuidanceHash
                != Legacy->GuidanceHash)
          {
            UE_LOG(LogTemp, Error,
              TEXT("VIOLATION CrowdDemoWorkerTargetCanaryMismatch agent=%d expected_input=%llu worker_input=%llu expected_velocity=%s actual_velocity=%s expected_mode=%d actual_mode=%d expected_execution=%u actual_execution=%u expected_guidance=%u actual_guidance=%u"),
              Agent.Identity.AgentId,
              ExpectedWorkerV2Sequence,
              Worker->SourceInputSequence,
              *LegacyVelocity.ToString(),
              *WorkerState.DesiredVelocity.ToString(),
              static_cast<int32>(Legacy->Guidance->Mode),
              static_cast<int32>(WorkerState.Mode),
              Legacy->ExecutionHash,
              WorkerState.ExecutionHash,
              Legacy->GuidanceHash,
              WorkerState.GuidanceHash);
            return ECrowdBoundaryPollResult::Failed;
          }
        }
        ++VerifiedTargetOwnerCount;
      }
      if (!BoundaryFacingWorkState->bWorkerMovementTailSubmitted
        && (GetCurrentFixedStepIndex() == 0
        || GetCurrentFixedStepIndex() % 300 == 0)
        )
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoWorkerTargetCheckpoint mode=%d canaries=%d verified=%d expected_input=%llu cohorts=%d source=PersistentRuntimeAuthority"),
          static_cast<int32>(TargetMode),
          TargetCanaries.Num(),
          VerifiedTargetOwnerCount,
          ExpectedWorkerV2Sequence,
          BoundaryFacingWorkState->TargetTopologySlots.Num());
      }
    }
  }
  if (!bWorkerV2MovementReady)
  {
    ++BoundaryPendingFrameCount;
    if (BoundaryPendingFrameCount % 120 == 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoBoundaryPendingReason reason=worker_v2_movement step=%d expected_input=%llu mode=%d"),
        GetCurrentFixedStepIndex(),
        ExpectedWorkerV2Sequence,
        static_cast<int32>(
          WorkerV2RuntimeSubsystem->GetWorkerMovementAuthority().
            GetMode()));
    }
    return ECrowdBoundaryPollResult::Pending;
  }
  LastBoundaryPrepareCheckpoint = 3;
  if (!BoundaryFacingWorkState->bWorkerMovementTailSubmitted)
  {
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      GetWorld()
        ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
        : nullptr;
    if (!RuntimeSubsystem)
      return RejectPrepare(TEXT("movement_runtime_missing"));
    FCrowdWorkerMovementAuthority& MovementAuthority =
      RuntimeSubsystem->GetWorkerMovementAuthority();
    const ECrowdWorkerMovementAuthorityMode MovementMode =
      MovementAuthority.GetMode();
    if (MovementMode
        != ECrowdWorkerMovementAuthorityMode::Shadow)
    {
      const uint64 WorkSequence = NextWorkerTaskSequence++;
      if (WorkSequence == 0 || NextWorkerTaskSequence == 0)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoWorkerTaskSequenceOverflow"));
        return ECrowdBoundaryPollResult::Failed;
      }
      auto Tail =
        MakeShared<FCrowdDemoWorkerMovementTailExecution,
          ESPMode::ThreadSafe>();
      Tail->bUsesWorkerV2PreparedMovement = true;
      FCrowdMassMovementPipelineWorkOutput PreparedMovement;
      if (!BuildWorkerV2PreparedMovement(
          BoundarySnapshot,
          [&FindResultDomain, RuntimeSubsystem](
            const FCrowdStableEntityRef& EntityRef,
            const ECrowdWorkerField Field)
          {
            return FindResultDomain(
              RuntimeSubsystem->GetWorkerResultApplyProxy(),
              EntityRef, Field);
          },
          MovementAuthority, ExpectedWorkerV2Sequence,
          MovementMode,
          BoundaryFacingWorkState->MovementShadowInput.
            FixedStepSeconds,
          BoundaryFacingWorkState->GraphOutput.Movement,
          BoundaryFacingWorkState->ObstacleKinematics,
          PreparedMovement))
        return RejectPrepare(TEXT("movement_merge"));
      TArray<FCrowdMassFinalKinematicState>
        WorkerParticleKinematics;
      if (ParticleMode
          != ECrowdDemoWorkerParticleAuthorityMode::Shadow)
      {
        if (!BoundaryFacingWorkState->GraphOutput.Particle.bCompleted
          || !BuildWorkerV2ParticleKinematics(
            BoundarySnapshot,
            [&FindResultDomain, RuntimeSubsystem](
              const FCrowdStableEntityRef& EntityRef,
              const ECrowdWorkerField Field)
            {
              return FindResultDomain(
                RuntimeSubsystem->GetWorkerResultApplyProxy(),
                EntityRef, Field);
            },
            ExpectedWorkerV2Sequence,
            FMath::Max(
              RuntimeSubsystem->GetWorkerResultApplyProxy().
                GetMetrics().LastAppliedInputSequence,
              PendingAppliedInputSequence),
            ParticleMode,
            ParticleCanaries,
            PreparedMovement,
            BoundaryFacingWorkState->GraphOutput.Particle.
              PublishPlan.FinalKinematics,
            WorkerParticleKinematics))
          return RejectPrepare(TEXT("particle_decode_or_shape"));
        Tail->bUsesWorkerV2PreparedParticle = true;
      }
      bool bDirectProductionTail = false;
      if (MovementMode
          == ECrowdWorkerMovementAuthorityMode::Production
        && ParticleMode
          == ECrowdDemoWorkerParticleAuthorityMode::Production)
      {
        TArray<FCrowdFacingResult> WorkerFacings =
          BoundaryFacingWorkState->Output.Facing.Summary.Results;
        if (WorkerFacings.Num() != BoundarySnapshot.Agents.Num()
          || WorkerParticleKinematics.Num()
            != BoundarySnapshot.Agents.Num())
          return RejectPrepare(TEXT("direct_tail_shape"));
        TMap<int32, FCrowdFacingResult*> FacingByAgentId;
        TMap<int32, FCrowdMassFinalKinematicState*>
          KinematicByAgentId;
        for (FCrowdFacingResult& Facing : WorkerFacings)
        {
          if (FacingByAgentId.Contains(Facing.AgentId))
            return RejectPrepare(TEXT("worker_facing_duplicate"));
          FacingByAgentId.Add(Facing.AgentId, &Facing);
        }
        for (FCrowdMassFinalKinematicState& Kinematic :
          WorkerParticleKinematics)
        {
          if (KinematicByAgentId.Contains(Kinematic.AgentId))
            return RejectPrepare(TEXT("particle_kinematic_duplicate"));
          KinematicByAgentId.Add(Kinematic.AgentId, &Kinematic);
        }
        const FCrowdWorkerResultApplyProxy& Proxy =
          RuntimeSubsystem->GetWorkerResultApplyProxy();
        for (const FCrowdMassBoundaryAgentRecord& Agent :
          BoundarySnapshot.Agents)
        {
          FCrowdFacingResult* const* Facing =
            FacingByAgentId.Find(Agent.Identity.AgentId);
          FCrowdMassFinalKinematicState* const* Kinematic =
            KinematicByAgentId.Find(Agent.Identity.AgentId);
          const FCrowdWorkerDomainProxyState* WorkerFacing =
            FindResultDomain(Proxy,
              Agent.AgentFacts.StableEntityRef,
              ECrowdWorkerField::Facing);
          FCrowdWorkerMovementState WorkerState;
          if (!Facing || !Kinematic || !WorkerFacing
            || WorkerFacing->SourceInputSequence
              != ExpectedWorkerV2Sequence
            || !FCrowdWorkerMovementStateCodec::Decode(
              WorkerFacing->State.Payload, WorkerState)
            || !WorkerState.Position.Equals(
              (*Kinematic)->Position, 0.001)
            || !WorkerState.Velocity.Equals(
              (*Kinematic)->Velocity, 0.001))
            return RejectPrepare(TEXT("particle_movement_mismatch"));
          (*Facing)->ResolvedYawDegrees = WorkerState.YawDegrees;
          (*Kinematic)->Position = WorkerState.Position;
          (*Kinematic)->Velocity = WorkerState.Velocity;
          (*Kinematic)->bValid = true;
        }
        FCrowdMassMovementFinalizeWorkInput FinalizeInput;
        TArray<FCrowdMassCommitTarget> CommitTargets;
        if (!FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
            BoundarySnapshot, WorkerParticleKinematics,
            WorkerFacings, FinalizeInput, CommitTargets))
          return RejectPrepare(TEXT("finalize_input"));
        FCrowdMassMovementFinalizeWorkOutput Finalize =
          FCrowdMassMovementFinalizeWork::BuildCommitPlan(
            FinalizeInput);
        if (!Finalize.bCompleted
          || !Finalize.CommitPlan.bValid)
          return RejectPrepare(TEXT("finalize_work"));
        Tail->GraphOutput.Movement = MoveTemp(PreparedMovement);
        Tail->ObstacleKinematics =
          MoveTemp(WorkerParticleKinematics);
        Tail->Output = BoundaryFacingWorkState->Output;
        Tail->Output.Facing.Summary.Results =
          MoveTemp(WorkerFacings);
        Tail->Output.Finalize = MoveTemp(Finalize);
        Tail->Output.CommitTargets = MoveTemp(CommitTargets);
        Tail->Output.bCompleted = true;
        Tail->ConsecutiveSettleStepsByAgentId =
          BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId;
        Tail->FinalSettledByAgentId =
          BoundaryFacingWorkState->FinalSettledByAgentId;
        Tail->StableHash = CalculateMovementTailHash(
          Tail->GraphOutput, Tail->Output,
          Tail->ObstacleKinematics);
        if (Tail->StableHash == 0)
          return RejectPrepare(TEXT("movement_tail_hash"));
        Tail->bCompleted.Store(true);
        BoundaryFacingWorkState->WorkerMovementTail = Tail;
        BoundaryFacingWorkState->WorkerMovementSequence =
          WorkSequence;
        BoundaryFacingWorkState->bWorkerMovementTailSubmitted = true;
        bDirectProductionTail = true;
      }
      if (!bDirectProductionTail)
      {
      FCrowdDemoRoundWorkGraphInput GraphInput =
        BoundaryFacingWorkState->GraphInput;
      TMap<int32, int32> PreviousSettle =
        BoundaryFacingWorkState->PreviousSettleStepsByAgentId;
      TMap<int32, bool> TerminalOwners =
        BoundaryFacingWorkState->TerminalOwnerByAgentId;
      FCrowdMassBoundarySnapshot Snapshot = BoundarySnapshot;
      const bool bUseObstacleConstraint =
        BoundaryFacingWorkState->bObstacleStaged;
      const FCrowdDemoSharedFlowFieldConfig ObstacleConfig =
        BoundaryFacingWorkState->ObstacleConfig;
      const float ObstacleFixedStepSeconds =
        BoundaryFacingWorkState->ObstacleFixedStepSeconds;
      if (!FCrowdDemoWorkerInputSync::SubmitShadowWork(
          *GetWorld(),
          FCrowdDemoWorkerInputSync::EShadowKernel::Movement,
          WorkSequence, 0,
          [Tail,
            GraphInput = MoveTemp(GraphInput),
            PreparedMovement = MoveTemp(PreparedMovement),
            WorkerParticleKinematics =
              MoveTemp(WorkerParticleKinematics),
            PreviousSettle = MoveTemp(PreviousSettle),
            TerminalOwners = MoveTemp(TerminalOwners),
            Snapshot = MoveTemp(Snapshot),
            bUseObstacleConstraint,
            ObstacleConfig,
            ObstacleFixedStepSeconds]() mutable
          {
            const bool bCompleted =
              Tail->bUsesWorkerV2PreparedParticle
              ? RunWorkerFacingTailFromKinematics(
                MoveTemp(GraphInput),
                MoveTemp(PreparedMovement),
                MoveTemp(WorkerParticleKinematics),
                MoveTemp(PreviousSettle),
                MoveTemp(TerminalOwners),
                MoveTemp(Snapshot), *Tail)
              : RunWorkerDownstreamTail(
                MoveTemp(GraphInput),
                MoveTemp(PreparedMovement),
                MoveTemp(PreviousSettle),
                MoveTemp(TerminalOwners),
                MoveTemp(Snapshot),
                bUseObstacleConstraint, true, ObstacleConfig,
                ObstacleFixedStepSeconds, *Tail);
            Tail->bCompleted.Store(true);
            return bCompleted ? Tail->StableHash : 0;
          },
          false))
        return RejectPrepare(TEXT("movement_tail_submit"));
      BoundaryFacingWorkState->WorkerMovementTail = Tail;
      BoundaryFacingWorkState->WorkerMovementSequence =
        WorkSequence;
      BoundaryFacingWorkState->bWorkerMovementTailSubmitted = true;
      }
    }
  }
  if (BoundaryFacingWorkState->bWorkerMovementTailSubmitted
    && !BoundaryFacingWorkState->bWorkerMovementTailConsumed)
  {
    const auto& Tail = BoundaryFacingWorkState->WorkerMovementTail;
    if (!Tail.IsValid() || !Tail->bCompleted.Load())
    {
      ++BoundaryPendingFrameCount;
      if (BoundaryPendingFrameCount % 120 == 0)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoBoundaryPendingReason reason=movement_tail step=%d tail_valid=%d"),
          GetCurrentFixedStepIndex(), Tail.IsValid() ? 1 : 0);
      }
      return ECrowdBoundaryPollResult::Pending;
    }
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      GetWorld()
        ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
        : nullptr;
    if (!RuntimeSubsystem)
      return RejectPrepare(TEXT("tail_runtime_missing"));
    FCrowdWorkerMovementAuthority& MovementAuthority =
      RuntimeSubsystem->GetWorkerMovementAuthority();
    const ECrowdWorkerMovementAuthorityMode MovementMode =
      MovementAuthority.GetMode();
    const bool bRequireBoundaryComparison =
      MovementMode
        != ECrowdWorkerMovementAuthorityMode::Production
      && !Tail->bUsesWorkerV2PreparedMovement;
    const uint64 BoundaryTailHash =
      bRequireBoundaryComparison
        ? CalculateMovementTailHash(
          BoundaryFacingWorkState->GraphOutput,
          BoundaryFacingWorkState->Output,
          BoundaryFacingWorkState->ObstacleKinematics)
        : 0;
    if (Tail->StableHash == 0
      || (bRequireBoundaryComparison
        && Tail->StableHash != BoundaryTailHash)
      || !Tail->Output.bCompleted
      || Tail->Output.Finalize.CommitPlan.Records.Num()
        != BoundarySnapshot.Agents.Num())
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerMovementTailMismatch step=%d expected=%llu actual=%llu records=%d boundary_movement=%u worker_movement=%u boundary_predict=%u worker_predict=%u boundary_local=%u worker_local=%u boundary_particle=%u worker_particle=%u boundary_facing=%u worker_facing=%u"),
        GetCurrentFixedStepIndex(),
        BoundaryTailHash,
        Tail->StableHash,
        Tail->Output.Finalize.CommitPlan.Records.Num(),
        BoundaryFacingWorkState->GraphOutput.Movement.StableHash,
        Tail->GraphOutput.Movement.StableHash,
        BoundaryFacingWorkState->GraphOutput.Movement.
          MovementPredict.StableHash,
        Tail->GraphOutput.Movement.MovementPredict.StableHash,
        BoundaryFacingWorkState->GraphOutput.Movement.
          LocalPredictive.StableHash,
        Tail->GraphOutput.Movement.LocalPredictive.StableHash,
        BoundaryFacingWorkState->GraphOutput.Particle.StableHash,
        Tail->GraphOutput.Particle.StableHash,
        BoundaryFacingWorkState->Output.StableHash,
        Tail->Output.StableHash);
      BoundaryOrchestrator->Fail();
      LastBoundaryTransactionResult =
        BoundaryOrchestrator->BuildResult();
      return ECrowdBoundaryPollResult::Failed;
    }

    TMap<FCrowdStableEntityRef, const FCrowdMassCommitRecord*>
      BoundaryCommitByRef;
    for (const FCrowdMassCommitRecord& Record
      : BoundaryFacingWorkState->Output.Finalize.CommitPlan.Records)
      BoundaryCommitByRef.Add(Record.EntityRef, &Record);
    const uint64 MovementVersion =
      BoundaryFacingWorkState->WorkerMovementSequence;
    if (MovementVersion == 0)
      return RejectPrepare(TEXT("movement_version"));
    const uint64 ExpectedWorkerV2InputSequence =
      BoundaryFacingWorkState->WorkerV2InputSequence;
    for (const FCrowdMassCommitRecord& Record
      : Tail->Output.Finalize.CommitPlan.Records)
    {
      if (MovementMode
          != ECrowdWorkerMovementAuthorityMode::Shadow
        && !MovementAuthority.IsWorkerOwner(Record.EntityRef))
        continue;
      FCrowdWorkerMovementSample PreviousSample;
      const bool bHasPrevious = MovementAuthority.Sample(
        Record.EntityRef,
        GetCurrentStepStartServerTimeSeconds(),
        PreviousSample);
      FCrowdWorkerMovementState MovementState;
      MovementState.Position = Record.Movement.Position;
      MovementState.Velocity = Record.Movement.Velocity;
      MovementState.YawDegrees = Record.Movement.YawDegrees;
      MovementState.SimulationTimeSeconds =
        GetCurrentStepEndServerTimeSeconds();
      MovementState.CorrectionRevision =
        bHasPrevious ? PreviousSample.CorrectionRevision : 0;
      FCrowdWorkerStatePatch Patch;
      Patch.EntityRef = Record.EntityRef;
      Patch.Generation =
        MovementAuthority.GetMetrics().Generation;
      Patch.WorkerEpoch = MovementVersion;
      Patch.SourceInputSequence = MovementVersion;
      Patch.DirtyMask = CrowdWorkerMovementFields::Movement;
      Patch.State.StateRevision = MovementVersion;
      if (!FCrowdWorkerMovementStateCodec::Encode(
          MovementState, Patch.State.Payload))
        return RejectPrepare(TEXT("movement_authority_encode"));
      Patch.RecalculateStableHash();
      if (MovementAuthority.AcceptPatch(Patch)
          != ECrowdWorkerMovementAcceptResult::Accepted)
        return RejectPrepare(TEXT("movement_authority_accept"));
      if (MovementMode
          == ECrowdWorkerMovementAuthorityMode::Shadow)
      {
        const FCrowdMassCommitRecord* const* BoundaryRecord =
          BoundaryCommitByRef.Find(Record.EntityRef);
        if (!BoundaryRecord)
          return RejectPrepare(TEXT("shadow_boundary_record"));
        if (const FCrowdWorkerDomainProxyState* Autonomous =
          FindResultDomain(
            RuntimeSubsystem->GetWorkerResultApplyProxy(),
            Record.EntityRef, ECrowdWorkerField::Facing))
        {
          if (Autonomous->SourceInputSequence
              == ExpectedWorkerV2InputSequence)
          {
            FCrowdWorkerMovementState AutonomousState;
            if (!FCrowdWorkerMovementStateCodec::Decode(
                Autonomous->State.Payload, AutonomousState))
              return RejectPrepare(TEXT("shadow_autonomous_decode"));
            const double PositionError = FVector::Distance(
              AutonomousState.Position,
              (*BoundaryRecord)->Movement.Position);
            const double VelocityError = FVector::Distance(
              AutonomousState.Velocity,
              (*BoundaryRecord)->Movement.Velocity);
            const double YawError = FMath::Abs(
              FMath::FindDeltaAngleDegrees(
                AutonomousState.YawDegrees,
                (*BoundaryRecord)->Movement.YawDegrees));
            ++WorkerV2MovementShadowCompareCount;
            WorkerV2MovementPositionErrorMaxCm = FMath::Max(
              WorkerV2MovementPositionErrorMaxCm, PositionError);
            WorkerV2MovementVelocityErrorMaxCmps = FMath::Max(
              WorkerV2MovementVelocityErrorMaxCmps, VelocityError);
            WorkerV2MovementYawErrorMaxDegrees = FMath::Max(
              WorkerV2MovementYawErrorMaxDegrees, YawError);
            if (PositionError > 0.001
              || VelocityError > 0.001
              || YawError > 0.001)
              ++WorkerV2MovementShadowMismatchCount;
          }
        }
        FCrowdWorkerMovementState ExpectedState = MovementState;
        ExpectedState.Position = (*BoundaryRecord)->Movement.Position;
        ExpectedState.Velocity = (*BoundaryRecord)->Movement.Velocity;
        ExpectedState.YawDegrees =
          (*BoundaryRecord)->Movement.YawDegrees;
        if (!MovementAuthority.CompareShadow(
            Record.EntityRef, ExpectedState,
            0.001, 0.001, 0.001))
          return RejectPrepare(TEXT("movement_shadow_compare"));
      }
    }
    if (MovementMode
        == ECrowdWorkerMovementAuthorityMode::Production)
    {
      BoundaryFacingWorkState->GraphOutput.Movement =
        Tail->GraphOutput.Movement;
      if (Tail->GraphOutput.Particle.bCompleted)
      {
        BoundaryFacingWorkState->GraphOutput.Particle =
          Tail->GraphOutput.Particle;
      }
      BoundaryFacingWorkState->ObstacleKinematics =
        Tail->ObstacleKinematics;
      PreparedRuntimeFinalKinematics =
        Tail->ObstacleKinematics;
      bPreparedRuntimeFinalKinematicsWorkerOwned =
        Tail->bUsesWorkerV2PreparedParticle;
      BoundaryFacingWorkState->Output = Tail->Output;
      if (!Tail->ConsecutiveSettleStepsByAgentId.IsEmpty())
      {
        BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId =
          Tail->ConsecutiveSettleStepsByAgentId;
        BoundaryFacingWorkState->FinalSettledByAgentId =
          Tail->FinalSettledByAgentId;
      }
    }
    else if (MovementMode
        == ECrowdWorkerMovementAuthorityMode::Canary)
    {
      TMap<FCrowdStableEntityRef, const FCrowdMassCommitRecord*>
        WorkerCommitByRef;
      for (const FCrowdMassCommitRecord& Record
        : Tail->Output.Finalize.CommitPlan.Records)
        WorkerCommitByRef.Add(Record.EntityRef, &Record);
      for (FCrowdMassCommitRecord& Record
        : BoundaryFacingWorkState->Output.Finalize.CommitPlan.Records)
      {
        if (!MovementAuthority.IsWorkerOwner(Record.EntityRef))
          continue;
        const FCrowdMassCommitRecord* const* WorkerRecord =
          WorkerCommitByRef.Find(Record.EntityRef);
        if (!WorkerRecord)
          return RejectPrepare(TEXT("canary_worker_record"));
        Record = **WorkerRecord;
      }
      for (const TPair<int32, int32>& Value
        : Tail->ConsecutiveSettleStepsByAgentId)
      {
        const FCrowdMassBoundaryAgentRecord* Agent =
          BoundarySnapshot.Agents.FindByPredicate(
            [&Value](const FCrowdMassBoundaryAgentRecord& Candidate)
            {
              return Candidate.Identity.AgentId == Value.Key;
            });
        if (Agent && MovementAuthority.IsWorkerOwner(
            Agent->AgentFacts.StableEntityRef))
        {
          BoundaryFacingWorkState->
            ConsecutiveSettleStepsByAgentId.Add(
              Value.Key, Value.Value);
          BoundaryFacingWorkState->FinalSettledByAgentId.Add(
            Value.Key,
            Tail->FinalSettledByAgentId.FindRef(Value.Key));
        }
      }
    }
    BoundaryFacingWorkState->bWorkerMovementTailConsumed = true;
    if (GetCurrentFixedStepIndex() == 0
      || GetCurrentFixedStepIndex() % 300 == 0)
    {
      const FCrowdWorkerMovementAuthorityMetrics& MovementMetrics =
        MovementAuthority.GetMetrics();
      const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
        RuntimeSubsystem->GetAsyncSimulationRuntime().GetMetrics();
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoWorkerMovementCheckpoint mode=%d particle_mode=%d particle_canaries=%d particle_domain_tail=%d step=%d accepted=%llu corrections=%llu rejected_echo=%llu shadow_compares=%llu shadow_mismatches=%llu v2_shadow_compares=%llu v2_shadow_mismatches=%llu v2_position_error_max_cm=%.3f v2_velocity_error_max_cmps=%.3f v2_yaw_error_max_deg=%.3f v2_movement_stage_compares=%llu v2_movement_stage_mismatches=%llu v2_movement_stage_stale_skips=%llu v2_movement_stage_last_input=%llu v2_movement_stage_position_error_max_cm=%.3f v2_movement_stage_velocity_error_max_cmps=%.3f v2_movement_stage_time_error_max_s=%.6f v2_movement_stage_last_mismatches=%llu v2_movement_stage_last_position_error_max_cm=%.3f v2_movement_stage_last_velocity_error_max_cmps=%.3f canaries=%d states=%d boundary_hash_required=%d production_submitted=%llu production_completed=%llu production_domain_tail=%d source=PersistentRuntimeAuthority"),
        static_cast<int32>(MovementMode),
        static_cast<int32>(ParticleMode),
        ParticleCanaries.Num(),
        Tail->bUsesWorkerV2PreparedParticle ? 1 : 0,
        GetCurrentFixedStepIndex(),
        MovementMetrics.AcceptedPatchCount,
        MovementMetrics.AcceptedCorrectionCount,
        MovementMetrics.RejectedEchoCount,
        MovementMetrics.ShadowCompareCount,
        MovementMetrics.ShadowMismatchCount,
        WorkerV2MovementShadowCompareCount,
        WorkerV2MovementShadowMismatchCount,
        WorkerV2MovementPositionErrorMaxCm,
        WorkerV2MovementVelocityErrorMaxCmps,
        WorkerV2MovementYawErrorMaxDegrees,
        WorkerV2MovementStageCompareCount,
        WorkerV2MovementStageMismatchCount,
        WorkerV2MovementStageStaleSkipCount,
        WorkerV2MovementStageLastExpectedInputSequence,
        WorkerV2MovementStagePositionErrorMaxCm,
        WorkerV2MovementStageVelocityErrorMaxCmps,
        WorkerV2MovementStageTimeErrorMaxSeconds,
        WorkerV2MovementStageLastMismatchCount,
        WorkerV2MovementStageLastPositionErrorMaxCm,
        WorkerV2MovementStageLastVelocityErrorMaxCmps,
        MovementMetrics.CanaryEntityCount,
        MovementMetrics.StateCount,
        bRequireBoundaryComparison ? 1 : 0,
        RuntimeMetrics.SubmittedProductionWorkCount,
        RuntimeMetrics.CompletedProductionWorkCount,
        MovementMode
          == ECrowdWorkerMovementAuthorityMode::Production
          ? 1 : 0);
    }
  }
  LastBoundaryPrepareCheckpoint = 4;
  const auto FailCompletedWork = [this](const TCHAR* Reason)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCompletedWorkRejected step=%d reason=%s"),
      GetCurrentFixedStepIndex(), Reason);
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return ECrowdBoundaryPollResult::Failed;
  };
  if (!BoundaryFacingWorkState->bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFacingWorkIncomplete step=%d facing=%d finalize=%d"),
      GetCurrentFixedStepIndex(),
      BoundaryFacingWorkState->Output.Facing.bCompleted ? 1 : 0,
      BoundaryFacingWorkState->Output.Finalize.bCompleted ? 1 : 0);
    return FailCompletedWork(TEXT("facing_incomplete"));
  }
  if (!BoundaryFacingWorkState->BusinessOutput.bCompleted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBusinessWorkIncomplete step=%d"),
      GetCurrentFixedStepIndex());
    return FailCompletedWork(TEXT("business_incomplete"));
  }
  LastBoundaryPrepareCheckpoint = 5;
  FCrowdDemoOwnedSharedFlowShadowInput SharedFlowShadowInput;
  if (!SharedFlowShadowInput.Capture(
      BoundaryFacingWorkState->GraphInput.SharedFlow)
    || !BoundaryFacingWorkState->GraphOutput.SharedFlow.bValid
    || !BoundaryFacingWorkState->bMovementShadowInputValid
    || !BoundaryFacingWorkState->GraphOutput.Movement.bCompleted
    || !BoundaryFacingWorkState->Output.Facing.bCompleted
    || BoundaryFacingWorkState->FacingShadowInput.Agents.IsEmpty()
    || !GetWorld())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoKernelShadowInputInvalid step=%d"),
      GetCurrentFixedStepIndex());
    return FailCompletedWork(TEXT("shadow_input"));
  }
  const int32 ShardSize =
    1 + GetCurrentFixedStepIndex() % 64;
  const bool bReverseDispatchOrder =
    (GetCurrentFixedStepIndex() & 1) != 0;
  FCrowdMassFacingWorkInput FacingShadowInput =
    BoundaryFacingWorkState->FacingShadowInput;
  FCrowdDemoBoundaryBusinessWorkInput BusinessShadowInput =
    BoundaryFacingWorkState->BusinessInput;
  if (!BoundaryFacingWorkState->bWorkerMovementTailSubmitted)
  {
    const uint64 WorkSequence = NextWorkerTaskSequence++;
    if (WorkSequence == 0 || NextWorkerTaskSequence == 0)
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerTaskSequenceOverflow"));
      return FailCompletedWork(TEXT("shadow_task_sequence"));
    }
    FCrowdMassMovementPipelineWorkInput MovementShadowInput =
      BoundaryFacingWorkState->MovementShadowInput;
    UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
      GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
    if (!RuntimeSubsystem)
      return FailCompletedWork(TEXT("shadow_runtime"));
    FCrowdWorkerMovementAuthority& MovementAuthority =
      RuntimeSubsystem->GetWorkerMovementAuthority();
    const bool bProductionMovement =
      MovementAuthority.GetMode()
        == ECrowdWorkerMovementAuthorityMode::Production;
    const bool bReplayLegacyMovementTail =
      MovementAuthority.GetMode()
        != ECrowdWorkerMovementAuthorityMode::Shadow;
    if (MovementAuthority.GetMode()
        != ECrowdWorkerMovementAuthorityMode::Shadow)
    {
      for (FCrowdMassGatherRecord& Record
        : MovementShadowInput.Guidance.Records)
      {
        if (!MovementAuthority.IsWorkerOwner(
            Record.AgentFacts.StableEntityRef))
          continue;
        FCrowdWorkerMovementSample Sample;
        if (MovementAuthority.Sample(
            Record.AgentFacts.StableEntityRef,
            GetCurrentStepStartServerTimeSeconds(), Sample))
        {
          const bool bAuthorityCorrection =
            !Record.State.Position.Equals(Sample.Position, 0.001)
            || !Record.State.Velocity.Equals(
              Sample.Velocity, 0.001)
            || FMath::Abs(FMath::FindDeltaAngleDegrees(
              Record.State.YawDegrees, Sample.YawDegrees)) > 0.001f;
          if (bAuthorityCorrection)
          {
            FCrowdWorkerMovementState CorrectedState;
            CorrectedState.Position = Record.State.Position;
            CorrectedState.Velocity = Record.State.Velocity;
            CorrectedState.YawDegrees = Record.State.YawDegrees;
            CorrectedState.SimulationTimeSeconds =
              GetCurrentStepStartServerTimeSeconds();
            CorrectedState.CorrectionRevision =
              Sample.CorrectionRevision + 1;
            FCrowdWorkerCorrectionDelta Correction;
            Correction.InputSequence = WorkSequence;
            Correction.EntityRef =
              Record.AgentFacts.StableEntityRef;
            Correction.CorrectionRevision =
              CorrectedState.CorrectionRevision;
            Correction.DirtyMask =
              CrowdWorkerMovementFields::Movement;
            if (!FCrowdWorkerMovementStateCodec::Encode(
                CorrectedState, Correction.FullState)
              || MovementAuthority.ApplyCorrection(
                Correction,
                MovementAuthority.GetMetrics().Generation,
                WorkSequence)
                != ECrowdWorkerMovementAcceptResult::
                  AcceptedCorrection)
              return FailCompletedWork(TEXT("shadow_correction"));
          }
          else
          {
            Record.State.Position = Sample.Position;
            Record.State.Velocity = Sample.Velocity;
            Record.State.YawDegrees = Sample.YawDegrees;
          }
        }
        else if (GetCurrentFixedStepIndex() > 0)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoWorkerMovementStateMissing step=%d agent=%d"),
            GetCurrentFixedStepIndex(), Record.Identity.AgentId);
          return FailCompletedWork(TEXT("shadow_movement_state"));
        }
      }
    }
    const uint64 ExpectedMovementTailHash =
      bProductionMovement
        ? 0
        : CalculateMovementTailHash(
          BoundaryFacingWorkState->GraphOutput,
          BoundaryFacingWorkState->Output,
          BoundaryFacingWorkState->ObstacleKinematics);
    auto MovementTail =
      MakeShared<FCrowdDemoWorkerMovementTailExecution,
        ESPMode::ThreadSafe>();
    if (!bReplayLegacyMovementTail)
    {
      MovementTail->GraphOutput =
        BoundaryFacingWorkState->GraphOutput;
      MovementTail->Output =
        BoundaryFacingWorkState->Output;
      MovementTail->ObstacleKinematics =
        BoundaryFacingWorkState->ObstacleKinematics;
      MovementTail->ConsecutiveSettleStepsByAgentId =
        BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId;
      MovementTail->FinalSettledByAgentId =
        BoundaryFacingWorkState->FinalSettledByAgentId;
      MovementTail->StableHash = CalculateMovementTailHash(
        MovementTail->GraphOutput, MovementTail->Output,
        MovementTail->ObstacleKinematics);
    }
    BoundaryFacingWorkState->WorkerMovementTail = MovementTail;
    BoundaryFacingWorkState->WorkerMovementSequence = WorkSequence;
    if (!bReplayLegacyMovementTail)
    {
      MovementTail->bCompleted.Store(true);
      BoundaryFacingWorkState->bWorkerMovementTailSubmitted = true;
      ++BoundaryPendingFrameCount;
      return ECrowdBoundaryPollResult::Pending;
    }
    else
    {
    FCrowdDemoRoundWorkGraphInput MovementGraphInput =
      BoundaryFacingWorkState->GraphInput;
    TMap<int32, int32> MovementPreviousSettle =
      BoundaryFacingWorkState->PreviousSettleStepsByAgentId;
    TMap<int32, bool> MovementTerminalOwners =
      BoundaryFacingWorkState->TerminalOwnerByAgentId;
    FCrowdMassBoundarySnapshot MovementSnapshot =
      BoundarySnapshot;
    const bool bUseObstacleConstraint =
      BoundaryFacingWorkState->bObstacleStaged;
    const FCrowdDemoSharedFlowFieldConfig MovementObstacleConfig =
      BoundaryFacingWorkState->ObstacleConfig;
    const float MovementObstacleFixedStepSeconds =
      BoundaryFacingWorkState->ObstacleFixedStepSeconds;
    bool bSupportingShadowSubmitted = true;
    if (!bProductionMovement && bReplayLegacyMovementTail)
    {
      bSupportingShadowSubmitted =
        FCrowdDemoWorkerInputSync::SubmitShadowWork(
          *GetWorld(),
          FCrowdDemoWorkerInputSync::EShadowKernel::SharedFlow,
          WorkSequence,
          BoundaryFacingWorkState->GraphOutput.SharedFlow.StableHash,
          [Input = MoveTemp(SharedFlowShadowInput),
            ShardSize, bReverseDispatchOrder]
          {
            return Input.Execute(
              ShardSize, bReverseDispatchOrder);
          })
        && FCrowdDemoWorkerInputSync::SubmitShadowWork(
          *GetWorld(),
          FCrowdDemoWorkerInputSync::EShadowKernel::Facing,
          WorkSequence,
          BoundaryFacingWorkState->Output.Facing.StableHash,
          [Input = MoveTemp(FacingShadowInput),
            ShardSize, bReverseDispatchOrder]
          {
            const FCrowdMassFacingWorkOutput Output =
              FCrowdMassFacingWork::ResolveSharded(
                Input, ShardSize, bReverseDispatchOrder);
            return Output.bCompleted ? Output.StableHash : 0;
          })
        && FCrowdDemoWorkerInputSync::SubmitShadowWork(
          *GetWorld(),
          FCrowdDemoWorkerInputSync::EShadowKernel::Business,
          WorkSequence,
          BoundaryFacingWorkState->BusinessOutput.StableHash,
          [Input = MoveTemp(BusinessShadowInput)]
          {
            const FCrowdDemoBoundaryBusinessWorkOutput Output =
              RunBoundaryBusinessWork(Input);
            return Output.bCompleted ? Output.StableHash : 0;
          });
    }
    if (!bSupportingShadowSubmitted
      || !FCrowdDemoWorkerInputSync::SubmitShadowWork(
      *GetWorld(),
      FCrowdDemoWorkerInputSync::EShadowKernel::Movement,
      WorkSequence,
      ExpectedMovementTailHash,
      [MovementTail,
        GraphInput = MoveTemp(MovementGraphInput),
        Input = MoveTemp(MovementShadowInput),
        Previous = MoveTemp(MovementPreviousSettle),
        Terminal = MoveTemp(MovementTerminalOwners),
        Snapshot = MoveTemp(MovementSnapshot),
        bUseObstacleConstraint,
        ObstacleConfig = MovementObstacleConfig,
        ObstacleFixedStepSeconds =
          MovementObstacleFixedStepSeconds,
        bReplayLegacyMovementTail]() mutable
      {
        if (!bReplayLegacyMovementTail)
        {
          MovementTail->bCompleted.Store(true);
          return MovementTail->StableHash;
        }
        const bool bCompleted = RunWorkerMovementTail(
          MoveTemp(GraphInput), MoveTemp(Input),
          MoveTemp(Previous), MoveTemp(Terminal),
          MoveTemp(Snapshot), bUseObstacleConstraint,
          ObstacleConfig, ObstacleFixedStepSeconds,
          *MovementTail);
        MovementTail->bCompleted.Store(true);
        return bCompleted ? MovementTail->StableHash : 0;
      },
      !bProductionMovement))
    {
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoKernelShadowSubmitRejected step=%d"),
        GetCurrentFixedStepIndex());
      return FailCompletedWork(TEXT("shadow_submit"));
    }
    BoundaryFacingWorkState->bWorkerMovementTailSubmitted = true;
    ++BoundaryPendingFrameCount;
    return ECrowdBoundaryPollResult::Pending;
    }
  }
  PreparedBusinessGuidanceCandidates =
    BoundaryFacingWorkState->BusinessOutput.GuidanceCandidates;
  PreparedReactiveMotionSteps =
    BoundaryFacingWorkState->BusinessOutput.ReactiveSteps;
  PreparedRuntimeSharedFlowOutputs =
    BoundaryFacingWorkState->GraphOutput.SharedFlow.Agents;
  PreparedTargetRegionGuidanceCandidates.Reset();
  int32 TargetGuidanceResultCount = 0;
  int32 NonZeroTargetGuidanceCount = 0;
  if (BoundaryFacingWorkState->bUseWorkerV2Target)
  {
    for (const FCrowdMassBoundaryAgentRecord& Base :
      BoundarySnapshot.Agents)
    {
      const FCrowdWorkerDomainProxyState* Worker =
        FindResultDomain(
          WorkerV2RuntimeSubsystem->GetWorkerResultApplyProxy(),
            Base.AgentFacts.StableEntityRef,
            ECrowdWorkerField::Target);
      FCrowdWorkerTargetState WorkerState;
      if (!Worker
        || Worker->SourceInputSequence == 0
        || Worker->SourceInputSequence
          > ExpectedWorkerV2Sequence
        || !FCrowdWorkerTargetStateCodec::Decode(
          Worker->State.Payload, WorkerState)
        || WorkerState.TargetRevision
          != GetTargetFact().TargetRevision)
        return FailCompletedWork(TEXT("target_worker_state"));
      ++TargetGuidanceResultCount;
      if (!WorkerState.DesiredVelocity.IsNearlyZero(0.1f))
        ++NonZeroTargetGuidanceCount;
      PreparedTargetRegionGuidanceCandidates.Add(
        FCrowdGuidanceComposeKernel::BuildCandidate(
          Base.Identity.AgentId,
          ECrowdGuidanceProvider::TargetRegion,
          GetCurrentPlanRevision(),
          WorkerState.DesiredVelocity,
          FVector(
            GetTargetFact().Location.X,
            GetTargetFact().Location.Y,
            Base.State.Position.Z),
          WorkerState.DesiredVelocity.IsNearlyZero()
            ? Base.State.YawDegrees
            : WorkerState.DesiredVelocity.Rotation().Yaw,
          WorkerState.Mode
            != ECrowdTargetRegionGuidanceMode::Unrouted));
    }
  }
  else for (const auto& Slot :
    BoundaryFacingWorkState->TargetTopologySlots)
  {
    if (!Slot.Output.bValid || !Slot.DemandOutput.bValid
      || !Slot.PlanOutput.bValid || !Slot.GuidanceOutput.bValid)
      return FailCompletedWork(TEXT("target_slot_output"));
    if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      const FCrowdDemoTargetRegionCapabilityCohortRuntime* Runtime =
        TargetRegionCapabilityCohorts.FindByPredicate(
          [&Slot](const auto& Value)
          {
            return Value.Cohort.CapabilityProfileKey
              == Slot.CohortKey;
          });
      if (!Runtime)
        return FailCompletedWork(TEXT("target_cohort_runtime"));
    }
    for (const auto& Result : Slot.GuidanceOutput.Results)
    {
      FCrowdTargetRegionGuidanceResult EffectiveResult = Result;
      const FCrowdMassBoundaryAgentRecord* Base =
        BoundarySnapshot.Agents.FindByPredicate(
          [&Result](const auto& Agent)
          {
            return Agent.Identity.AgentId == Result.AgentId;
          });
      if (!Base)
        return FailCompletedWork(TEXT("target_base_agent"));
      if (TargetMode
          == ECrowdDemoWorkerTargetAuthorityMode::Production)
      {
        const FCrowdWorkerDomainProxyState* Worker =
          FindResultDomain(
            WorkerV2RuntimeSubsystem->GetWorkerResultApplyProxy(),
              Base->AgentFacts.StableEntityRef,
              ECrowdWorkerField::Target);
        FCrowdWorkerTargetState WorkerState;
        if (!Worker
          || !FCrowdWorkerTargetStateCodec::Decode(
            Worker->State.Payload, WorkerState)
          || WorkerState.CohortKey != Slot.CohortKey
          || WorkerState.TargetRevision
            != Slot.PlanInput.TargetRevision)
          return FailCompletedWork(TEXT("target_production_state"));
        EffectiveResult.CurrentCellKey =
          WorkerState.CurrentCellKey;
        EffectiveResult.NextCellKey = WorkerState.NextCellKey;
        EffectiveResult.DemandRegionKey =
          WorkerState.DemandRegionKey;
        EffectiveResult.Mode = WorkerState.Mode;
        EffectiveResult.DesiredVelocity = FVector2f(
          WorkerState.DesiredVelocity.X,
          WorkerState.DesiredVelocity.Y);
      }
      ++TargetGuidanceResultCount;
      if (!EffectiveResult.DesiredVelocity.IsNearlyZero(0.1f))
        ++NonZeroTargetGuidanceCount;
      const FVector DesiredVelocity(
        EffectiveResult.DesiredVelocity.X,
        EffectiveResult.DesiredVelocity.Y, 0.0f);
      PreparedTargetRegionGuidanceCandidates.Add(
        FCrowdGuidanceComposeKernel::BuildCandidate(
          EffectiveResult.AgentId,
          ECrowdGuidanceProvider::TargetRegion,
          GetCurrentPlanRevision(), DesiredVelocity,
          FVector(
            Slot.GuidanceInput.Settings.TargetLocation.X,
            Slot.GuidanceInput.Settings.TargetLocation.Y,
            Base->State.Position.Z),
          DesiredVelocity.IsNearlyZero()
            ? Base->State.YawDegrees
            : DesiredVelocity.Rotation().Yaw,
          EffectiveResult.Mode
            != ECrowdTargetRegionGuidanceMode::Unrouted));
    }
  }
  PreparedTargetRegionGuidanceCandidates.Sort(
    [](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
  if (GetCurrentFixedStepIndex() == 0
    || GetCurrentFixedStepIndex() % 300 == 0)
  {
    int32 TargetProviderCount = 0;
    int32 NonZeroComposedCount = 0;
    for (const FCrowdComposedGuidance& Guidance
      : BoundaryFacingWorkState->GraphOutput.Movement.Guidance.
        ComposedGuidance)
    {
      if (Guidance.SelectedProvider
        == ECrowdGuidanceProvider::TargetRegion)
        ++TargetProviderCount;
      if (!Guidance.AutonomousPreferredVelocity.IsNearlyZero(0.1f))
        ++NonZeroComposedCount;
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoBoundaryGuidance step=%d target_results=%d target_nonzero=%d selected_target=%d composed_nonzero=%d source=MassPipeline"),
      GetCurrentFixedStepIndex(), TargetGuidanceResultCount,
      NonZeroTargetGuidanceCount, TargetProviderCount,
      NonZeroComposedCount);
  }
  if (TargetMode
      == ECrowdDemoWorkerTargetAuthorityMode::Production)
    PreparedTargetResourceSlots.Reset();
  else
    PreparedTargetResourceSlots =
      BoundaryFacingWorkState->TargetTopologySlots;
  if (!PreparePendingTargetResourcePlan())
    return FailCompletedWork(TEXT("target_resource_plan"));
  if (BoundaryFacingWorkState->bObstacleStaged)
  {
    PreparedObstacleMaxReprojectDeltaCm =
      BoundaryFacingWorkState->ObstacleMaxReprojectDeltaCm;
    PreparedRuntimeComposedGuidance =
      BoundaryFacingWorkState->GraphOutput.Movement.Guidance.
        ComposedGuidance;
    PreparedRuntimePredictedMovements =
      BoundaryFacingWorkState->GraphOutput.Movement.MovementPredict.
        Results;
    PreparedRuntimeFinalKinematics =
      BoundaryFacingWorkState->ObstacleKinematics;
    bPreparedRuntimeFinalKinematicsWorkerOwned =
      BoundaryFacingWorkState->bWorkerMovementTailConsumed
      && BoundaryFacingWorkState->WorkerMovementTail.IsValid()
      && BoundaryFacingWorkState->WorkerMovementTail->
        bUsesWorkerV2PreparedParticle;
  }
  if ((ProjectileMode
      != ECrowdDemoWorkerProjectileAuthorityMode::Shadow
      || CombatMode
        != ECrowdDemoWorkerCombatAuthorityMode::Shadow)
    && BoundaryFacingWorkState->BusinessInput.Rules.
      RangedCombatSettings.bEnabled != 0)
  {
    FCrowdDemoPreparedCombatBoundaryCommit& Commit =
      BoundaryFacingWorkState->BusinessOutput.Commit;
    const FCrowdStableEntityRef AnchorEntity =
      BoundarySnapshot.Agents[0].AgentFacts.StableEntityRef;
    const FCrowdWorkerDomainProxyState* Worker =
      FindResultDomain(
        WorkerV2RuntimeSubsystem->GetWorkerResultApplyProxy(),
        AnchorEntity, ECrowdWorkerField::Projectile);
    FCrowdWorkerProjectileState WorkerState;
    FCrowdDemoWorkerCombatHostResult WorkerCombatResult;
    if (!Worker
      || Worker->SourceInputSequence != ExpectedWorkerV2Sequence
      || !FCrowdWorkerProjectileStateCodec::Decode(
        Worker->State.Payload, WorkerState)
      || !FCrowdDemoWorkerCombatHostResultCodec::Decode(
        WorkerState.HostCombatResult, WorkerCombatResult)
      || WorkerCombatResult.FixedStepIndex
        != GetCurrentFixedStepIndex())
      return FailCompletedWork(TEXT("projectile_state"));
    if (ProjectileMode
        != ECrowdDemoWorkerProjectileAuthorityMode::Shadow)
    {
      Commit.Projectiles = WorkerState.Prepared.States;
      Commit.ProjectileEvents.Reset();
      FCrowdDemoProjectileAdapters::AppendVisualEvents(
        WorkerState.Prepared.Events, Commit.ProjectileEvents);
    }
    if (CombatMode
        != ECrowdDemoWorkerCombatAuthorityMode::Shadow)
    {
      TArray<FCrowdDemoPreparedReactiveMotionStep>
        WorkerReactiveSteps;
      WorkerReactiveSteps.Reserve(Commit.Agents.Num());
      for (FCrowdDemoRangedCombatAgent& Agent : Commit.Agents)
      {
        const FCrowdWorkerDomainProxyState* CombatProxy =
          FindResultDomain(
            WorkerV2RuntimeSubsystem->GetWorkerResultApplyProxy(),
              Agent.EntityRef, ECrowdWorkerField::Combat);
        FCrowdWorkerCombatState CombatState;
        FCrowdDemoCombatAgentState HostState;
        if (!CombatProxy
          || CombatProxy->SourceInputSequence
            != ExpectedWorkerV2Sequence
          || !FCrowdWorkerCombatStateCodec::Decode(
            CombatProxy->State.Payload, CombatState)
          || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
            CombatState.HostState, HostState)
          || HostState.AgentId != Agent.AgentId
          || HostState.LifecycleSerial != Agent.LifecycleSerial)
          return FailCompletedWork(TEXT("combat_state"));
        Agent.Combat = HostState;
        Agent.bAlive = HostState.bAlive;
        FCrowdDemoPreparedReactiveMotionStep& Reactive =
          WorkerReactiveSteps.AddDefaulted_GetRef();
        Reactive.AgentId = Agent.AgentId;
        Reactive.LifecycleSerial = Agent.LifecycleSerial;
        Reactive.bActive = CombatState.bReactiveActive;
        Reactive.ProposedZ = CombatState.ProposedZ;
        Reactive.VerticalVelocityCmps =
          CombatState.VerticalVelocityCmps;
      }
      WorkerReactiveSteps.Sort([](const auto& A, const auto& B)
      {
        return A.AgentId < B.AgentId;
      });
      PreparedReactiveMotionSteps = MoveTemp(WorkerReactiveSteps);
      Commit.HitSummary = WorkerCombatResult.HitSummary;
    }
    if (ProjectileMode
          != ECrowdDemoWorkerProjectileAuthorityMode::Shadow
      || CombatMode
          != ECrowdDemoWorkerCombatAuthorityMode::Shadow)
    {
      Commit.ProjectileSummary =
        WorkerCombatResult.AttackSummary;
      FCrowdDemoProjectileAdapters::MergeSummary(
        WorkerState.Prepared.Summary,
        Commit.ProjectileSummary);
    }
    Commit.StableHash = CalculateCombatCommitStableHash(
      BoundarySnapshot.StableHash, Commit.FixedStepIndex,
      Commit.Agents, Commit.ProjectileSummary,
      Commit.HitSummary);
    Commit.bValid = Commit.StableHash != 0;
    if (!Commit.bValid)
      return FailCompletedWork(TEXT("combat_commit_hash"));
  }
  if (BoundaryFacingWorkState->BusinessOutput.bRequiresCommit
    && !SetPreparedCombatBoundaryCommit(
      MoveTemp(BoundaryFacingWorkState->BusinessOutput.Commit)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoBusinessPreparedCommitRejected step=%d"),
      GetCurrentFixedStepIndex());
    return FailCompletedWork(TEXT("business_prepare_commit"));
  }
  if (GetCurrentFixedStepIndex() == 0)
  {
    FCrowdWorkerConsistencyEvidence ParticleEvidence;
    ParticleEvidence.Domain = ECrowdWorkerConsistencyDomain::
      ParticleInteractionIsland;
    ParticleEvidence.Generation = BoundaryGeneration;
    ParticleEvidence.DomainKey = 1;
    ParticleEvidence.InputEpoch = 1;
    ParticleEvidence.EnvironmentRevision =
      BoundaryFacingWorkState->GraphOutput.SharedFlow.StableHash;
    ParticleEvidence.EntityCount = BoundarySnapshot.Agents.Num();
    ParticleEvidence.bStableMembership = true;
    ParticleEvidence.bNetworkSemanticsFrozen = true;
    ParticleEvidence.bClosedInteractionBoundary = false;

    FCrowdWorkerConsistencyEvidence TargetEvidence;
    TargetEvidence.Domain =
      ECrowdWorkerConsistencyDomain::TargetCohort;
    TargetEvidence.Generation = BoundaryGeneration;
    TargetEvidence.DomainKey = 2;
    TargetEvidence.InputEpoch = 1;
    TargetEvidence.EnvironmentRevision =
      BoundaryFacingWorkState->GraphOutput.SharedFlow.StableHash;
    TargetEvidence.EntityCount = BoundarySnapshot.Agents.Num();
    TargetEvidence.bStableMembership = true;
    TargetEvidence.bAtomicPlan =
      !BoundaryFacingWorkState->TargetTopologySlots.IsEmpty();
    TargetEvidence.bNetworkSemanticsFrozen = false;

    FCrowdWorkerConsistencyEvidence CombatEvidence;
    CombatEvidence.Domain =
      ECrowdWorkerConsistencyDomain::CombatEventBoundary;
    CombatEvidence.Generation = BoundaryGeneration;
    CombatEvidence.DomainKey = 3;
    CombatEvidence.InputEpoch = 1;
    CombatEvidence.EntityCount = BoundarySnapshot.Agents.Num();
    CombatEvidence.FirstEventSequence = 1;
    CombatEvidence.LastEventSequence = 1;
    CombatEvidence.EventCount = 1;
    CombatEvidence.bStableMembership = true;
    CombatEvidence.bOrderedEvents = true;
    CombatEvidence.bIdempotencyProven = true;
    CombatEvidence.bRollbackProven = false;
    CombatEvidence.bNetworkSemanticsFrozen = true;

    const FCrowdWorkerConsistencyEvaluation ParticleEvaluation =
      FCrowdWorkerConsistencyDomainEvaluator::Evaluate(
        ParticleEvidence);
    const FCrowdWorkerConsistencyEvaluation TargetEvaluation =
      FCrowdWorkerConsistencyDomainEvaluator::Evaluate(
        TargetEvidence);
    const FCrowdWorkerConsistencyEvaluation CombatEvaluation =
      FCrowdWorkerConsistencyDomainEvaluator::Evaluate(
        CombatEvidence);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoConsistencyDomainCheckpoint particle_decision=%d particle_failure=%d target_decision=%d target_failure=%d combat_decision=%d combat_failure=%d source=FailClosedEvaluator"),
      static_cast<int32>(ParticleEvaluation.Decision),
      static_cast<int32>(ParticleEvaluation.Failure),
      static_cast<int32>(TargetEvaluation.Decision),
      static_cast<int32>(TargetEvaluation.Failure),
      static_cast<int32>(CombatEvaluation.Decision),
      static_cast<int32>(CombatEvaluation.Failure));
  }
  LastBoundaryPrepareCheckpoint = 6;
  return ECrowdBoundaryPollResult::Ready;
}

bool FCrowdDemoPreparedTargetResourcePlan::ValidatePrepareInput(
  const FCrowdDemoTargetResourcePrepareValidationInput& Input)
{
  if (Input.OwnerId == 0 || Input.ResourceRevision == 0
    || !Input.bResourceReferenceValid)
    return false;
  auto HasInvalidOrDuplicate = [](TConstArrayView<uint64> Values)
  {
    TSet<uint64> Unique;
    Unique.Reserve(Values.Num());
    for (const uint64 Value : Values)
    {
      if (Value == 0 || Unique.Contains(Value))
        return true;
      Unique.Add(Value);
    }
    return false;
  };
  return !HasInvalidOrDuplicate(Input.SlotKeys)
    && !HasInvalidOrDuplicate(Input.EntityKeys)
    && !HasInvalidOrDuplicate(Input.EntityFieldKeys);
}

bool UCrowdDemoRoundSimPipelineSubsystem::PreparePendingTargetResourcePlan()
{
  check(IsInGameThread());
    FCrowdDemoPreparedRoundCommitPlan* Pending =
      PeekPreparedRoundCommitPlan();
  if (!Pending || !Pending->PreparedTargetResourcePlan.IsValid())
    return false;
  FCrowdDemoPreparedTargetResourcePlan& Prepared =
    *Pending->PreparedTargetResourcePlan;
  if (Prepared.BuildCount != 0 || Prepared.bValid)
    return false;
  ++Prepared.BuildCount;

  FCrowdDemoTargetResourcePrepareValidationInput ValidationInput;
  ValidationInput.OwnerId = reinterpret_cast<uint64>(this);
  ValidationInput.ResourceRevision =
    static_cast<uint64>(
      static_cast<uint32>(RuntimeSharedFlowResource.Field.Config.Revision))
      + 1ull;
  ValidationInput.bResourceReferenceValid = true;
  for (const auto& Slot : PreparedTargetResourceSlots)
  {
    ValidationInput.SlotKeys.Add(
      (static_cast<uint64>(Slot.CohortKey) << 1) | 1ull);
    ValidationInput.bResourceReferenceValid =
      ValidationInput.bResourceReferenceValid
      && Slot.DemandInput.SharedFlowField == &RuntimeSharedFlowResource.Field;
    if (!Slot.bDemandStaged || !Slot.bPlanStaged || !Slot.bGuidanceStaged
      || Slot.PlanInput.FixedStepIndex != GetCurrentFixedStepIndex()
      || Slot.PlanInput.TargetRevision != TargetFact.TargetRevision)
      return false;
    for (const auto& Agent : Slot.GuidanceInput.Agents)
    {
      if (Agent.AgentId == INDEX_NONE)
        return false;
      ValidationInput.EntityKeys.Add(
        static_cast<uint64>(static_cast<uint32>(Agent.AgentId)) + 1ull);
    }
    for (const auto& Result : Slot.GuidanceOutput.Results)
    {
      if (Result.AgentId == INDEX_NONE)
        return false;
      const uint64 EntityKey =
        static_cast<uint64>(static_cast<uint32>(Result.AgentId)) + 1ull;
      ValidationInput.EntityFieldKeys.Add(
        FoldBoundaryHash(EntityKey, 0x544152474554ull));
    }
  }
  if (!FCrowdDemoPreparedTargetResourcePlan::ValidatePrepareInput(
      ValidationInput))
    return false;

  uint64 BaseStateHash = 14695981039346656037ull;
  if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
  {
    for (const auto& Runtime : TargetRegionCapabilityCohorts)
      BaseStateHash = FoldBoundaryHash(
        BaseStateHash, CalculateTargetResourceCohortStateHash(Runtime));
  }
  else
  {
    BaseStateHash = CalculateHomogeneousTargetResourceStateHash(
      PreparedTargetRegionTopology, PreparedTargetRegionDemand,
      PreparedTargetRegionPlan, TargetRegionQuotaExecution,
      TargetRegionPlanValidation, TargetRegionGuidanceSummary,
      TargetRegionTopologyRoundHash, TargetRegionDemandRoundHash,
      TargetRegionTransportRoundHash, TargetRegionGuidanceRoundHash,
      TargetRegionValidationRoundHash, TargetRegionPlanRebuildCount,
      TargetRegionInvalidStepCount);
  }

  for (const auto& Slot : PreparedTargetResourceSlots)
  {
    auto PublishTargetOutputs = [&Slot](auto& Runtime)
    {
      Runtime.Topology = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionTopology(Slot.Output.Topology);
      Runtime.TopologySummary = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionTopologySummary(Slot.Output.Summary);
      Runtime.Demand = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionDemand(Slot.DemandOutput.Demand);
      Runtime.Plan = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionPlan(Slot.PlanOutput.Plan);
      Runtime.QuotaExecution = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionExecution(Slot.GuidanceOutput.Execution);
      Runtime.LastPlanReplacement = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionReplacement(Slot.PlanOutput.Replacement);
      Runtime.Validation = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionValidation(Slot.PlanOutput.Validation);
      Runtime.Guidance.Reset(Slot.GuidanceOutput.Results.Num());
      for (const auto& Result : Slot.GuidanceOutput.Results)
        Runtime.Guidance.Add(FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionGuidance(Result));
      Runtime.GuidanceSummary = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionGuidanceSummary(Slot.GuidanceOutput.Summary);
    };
    if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
    {
      const int32 DestinationIndex =
        TargetRegionCapabilityCohorts.IndexOfByPredicate(
          [&Slot](const auto& Value)
          {
            return Value.Cohort.CapabilityProfileKey == Slot.CohortKey;
          });
      if (!TargetRegionCapabilityCohorts.IsValidIndex(DestinationIndex))
        return false;
      FCrowdDemoPreparedTargetResourceCohortApply& Apply =
        Prepared.CohortApplies.AddDefaulted_GetRef();
      Apply.DestinationIndex = DestinationIndex;
      Apply.CohortKey = Slot.CohortKey;
      Apply.BaseStateHash = CalculateTargetResourceCohortStateHash(
        TargetRegionCapabilityCohorts[DestinationIndex]);
      Apply.PreparedRuntime = TargetRegionCapabilityCohorts[DestinationIndex];

      const FCrowdDemoTargetRegionFlowPlan PreviousPlan =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionPlan(
          Slot.PlanInput.PreviousPlan);
      const FCrowdDemoTargetRegionQuotaExecutionState PreviousExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
          Slot.PlanInput.PreviousExecution);
      const FCrowdDemoTargetRegionFlowPlan NewPlan =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionPlan(
          Slot.PlanOutput.Plan);
      const FCrowdDemoTargetRegionQuotaExecutionState NewPlanExecution =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionExecution(
          Slot.PlanOutput.Execution);
      const FCrowdDemoTargetRegionPlanValidationResult NewValidation =
        FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
          Slot.PlanOutput.Validation);
      FCrowdMassTargetRegionPlanInput PreviousValidationInput = Slot.PlanInput;
      PreviousValidationInput.Topology = Slot.Output.Topology;
      PreviousValidationInput.Demand = Slot.DemandOutput.Demand;
      const FCrowdDemoTargetRegionPlanValidationResult PreviousValidation =
        Slot.PlanOutput.RebuildReason != 0
          ? FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoTargetRegionValidation(
              FCrowdMassTargetRegionWork::ValidateExecution(
                PreviousValidationInput))
          : NewValidation;
      PublishTargetOutputs(Apply.PreparedRuntime);
      const uint32 Step = static_cast<uint32>(GetCurrentFixedStepIndex());
      Apply.PreparedRuntime.TopologyRoundHash = FoldTargetDiagnosticHash(
        FoldTargetDiagnosticHash(
          Apply.PreparedRuntime.TopologyRoundHash, Step),
        Apply.PreparedRuntime.Topology.TopologyHash);
      Apply.PreparedRuntime.DemandRoundHash = FoldTargetDiagnosticHash(
        FoldTargetDiagnosticHash(Apply.PreparedRuntime.DemandRoundHash, Step),
        Apply.PreparedRuntime.Demand.DemandHash);
      if (Slot.PlanOutput.RebuildReason != 0)
      {
        Apply.PreparedRuntime.SolverMillisecondsSamples.Add(
          static_cast<float>(Slot.PlanOutput.SolverMilliseconds));
        ++Apply.PreparedRuntime.PlanRebuildCount;
      }
      if (IsTargetRegionPlanLifecycleDiagnosticEnabled())
      {
        FCrowdDemoTargetRegionPlanLifecycleBoundaryInput DiagnosticInput;
        DiagnosticInput.FixedStepIndex = GetCurrentFixedStepIndex();
        DiagnosticInput.CapabilityProfileKey = Slot.CohortKey;
        DiagnosticInput.PlanLifetimeSteps =
          ActivePlan.Rules.TargetRegionTransportSettings.PlanLifetimeSteps;
        DiagnosticInput.TargetRevision = TargetFact.TargetRevision;
        DiagnosticInput.TargetLocation = FVector2f(
          TargetFact.Location.X, TargetFact.Location.Y);
        DiagnosticInput.SelectedReason = Slot.PlanOutput.RebuildReason;
        DiagnosticInput.Topology = Apply.PreparedRuntime.Topology;
        DiagnosticInput.Demand = Apply.PreparedRuntime.Demand;
        DiagnosticInput.PreviousPlan = PreviousPlan;
        DiagnosticInput.NewPlan = NewPlan;
        DiagnosticInput.PreviousExecution = PreviousExecution;
        DiagnosticInput.NewExecution = NewPlanExecution;
        DiagnosticInput.PreviousValidation = PreviousValidation;
        DiagnosticInput.Agents = Apply.PreparedRuntime.Agents;
        const uint32 ConditionMask =
          FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::
            ComputeConditionMask(DiagnosticInput);
        const int32 SelectedByKernel = static_cast<int32>(
          FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::
            SelectReason(ConditionMask));
        if (SelectedByKernel != Slot.PlanOutput.RebuildReason)
        {
          UE_LOG(LogTemp, Error,
            TEXT("VIOLATION CrowdDemoTargetRegionPlanLifecycleReason step=%d profile_key=%u commit=%d kernel=%d mask=%u"),
            GetCurrentFixedStepIndex(), Slot.CohortKey,
            Slot.PlanOutput.RebuildReason, SelectedByKernel, ConditionMask);
        }
        FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(
          DiagnosticInput, Apply.PreparedRuntime.PlanLifecycle);
      }
      Apply.PreparedRuntime.Validation = NewValidation;
      Apply.PreparedRuntime.ValidationRoundHash = FoldTargetDiagnosticHash(
        FoldTargetDiagnosticHash(
          Apply.PreparedRuntime.ValidationRoundHash, Step),
        NewValidation.ValidationHash);
      Apply.PreparedRuntime.TransportRoundHash = FoldTargetDiagnosticHash(
        FoldTargetDiagnosticHash(
          Apply.PreparedRuntime.TransportRoundHash, Step),
        FoldTargetDiagnosticHash(
          NewPlan.TransportHash, NewPlanExecution.ExecutionHash));
      Apply.PreparedRuntime.GuidanceRoundHash = FoldTargetDiagnosticHash(
        FoldTargetDiagnosticHash(
          Apply.PreparedRuntime.GuidanceRoundHash, Step),
        Apply.PreparedRuntime.GuidanceSummary.GuidanceHash);
      if (!NewPlan.bValid || !NewValidation.bValid
        || !Apply.PreparedRuntime.GuidanceSummary.bValid)
      {
        Apply.PreparedRuntime.bRoundValid = false;
        if (Apply.PreparedRuntime.LastInvalidStep
          != GetCurrentFixedStepIndex())
        {
          ++Apply.PreparedRuntime.InvalidStepCount;
          Apply.PreparedRuntime.LastInvalidStep = GetCurrentFixedStepIndex();
        }
        if (!NewValidation.bValid)
          ++Apply.PreparedRuntime.ValidationFailureCount;
        if (!Apply.PreparedRuntime.GuidanceSummary.bValid)
          ++Apply.PreparedRuntime.GuidanceUnroutedStepCount;
      }
    }
    else
    {
      if (Prepared.HomogeneousApply.bSet)
        return false;
      auto& Apply = Prepared.HomogeneousApply;
      Apply.Topology = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionTopology(Slot.Output.Topology);
      Apply.TopologySummary = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionTopologySummary(Slot.Output.Summary);
      Apply.Demand = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionDemand(Slot.DemandOutput.Demand);
      Apply.Plan = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionPlan(Slot.PlanOutput.Plan);
      Apply.QuotaExecution = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionExecution(Slot.GuidanceOutput.Execution);
      Apply.Validation = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionValidation(Slot.PlanOutput.Validation);
      Apply.Guidance.Reserve(Slot.GuidanceOutput.Results.Num());
      for (const auto& Result : Slot.GuidanceOutput.Results)
        Apply.Guidance.Add(FCrowdDemoMassCrowdRuntimeAdapter::
          BuildDemoTargetRegionGuidance(Result));
      Apply.GuidanceSummary = FCrowdDemoMassCrowdRuntimeAdapter::
        BuildDemoTargetRegionGuidanceSummary(Slot.GuidanceOutput.Summary);
      Apply.SolverMilliseconds =
        static_cast<float>(Slot.PlanOutput.SolverMilliseconds);
      Apply.RebuildReason = Slot.PlanOutput.RebuildReason;
      Apply.bSet = true;
    }
  }

  Prepared.CommitToken.OwnerId = reinterpret_cast<uint64>(this);
  Prepared.CommitToken.OwnerRevision = TargetResourceOwnerRevision;
  Prepared.CommitToken.Generation = BoundaryGeneration;
  Prepared.CommitToken.BaseStateHash = BaseStateHash;
  Prepared.CommitToken.PreparedStateHash =
    CalculatePreparedTargetResourceHash(PreparedTargetResourceSlots);
  Prepared.CommitToken.ResourceId = reinterpret_cast<uint64>(
    &RuntimeSharedFlowResource.Field);
  Prepared.CommitToken.ResourceRevision =
    RuntimeSharedFlowResource.Field.Config.Revision;
  Prepared.CommitToken.ResourceBuildHash =
    RuntimeSharedFlowResource.Field.BuildHash;
  Prepared.CommitToken.ResourceRebuildCount =
    RuntimeSharedFlowResource.FieldRebuildCount;
  Prepared.CommitToken.PlanRevision = GetCurrentPlanRevision();
  Prepared.CommitToken.FixedStepIndex = GetCurrentFixedStepIndex();
  Prepared.CommitToken.TargetRevision = TargetFact.TargetRevision;
  Prepared.bValid = Prepared.CommitToken.IsValid();
  PreparedTargetResourceSlots.Reset();
  return Prepared.bValid;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  FinalValidatePreparedTargetResourcePlan(
    const FCrowdDemoPreparedTargetResourcePlan& Prepared) const
{
  if (!Prepared.bValid || Prepared.BuildCount != 1
    || Prepared.ApplyCount != 0 || !Prepared.CommitToken.IsValid()
    || Prepared.CommitToken.OwnerId != reinterpret_cast<uint64>(this)
    || Prepared.CommitToken.OwnerRevision != TargetResourceOwnerRevision
    || Prepared.CommitToken.Generation != BoundaryGeneration
    || Prepared.CommitToken.ResourceId != reinterpret_cast<uint64>(
      &RuntimeSharedFlowResource.Field)
    || Prepared.CommitToken.ResourceRevision
      != RuntimeSharedFlowResource.Field.Config.Revision
    || Prepared.CommitToken.ResourceBuildHash
      != RuntimeSharedFlowResource.Field.BuildHash
    || Prepared.CommitToken.ResourceRebuildCount
      != RuntimeSharedFlowResource.FieldRebuildCount
    || Prepared.CommitToken.PlanRevision != GetCurrentPlanRevision()
    || Prepared.CommitToken.FixedStepIndex != GetCurrentFixedStepIndex()
    || Prepared.CommitToken.TargetRevision != TargetFact.TargetRevision)
    return false;

  uint64 CurrentStateHash = 14695981039346656037ull;
  if (ActivePlan.Rules.bEnableHeterogeneousProfiles != 0)
  {
    for (const auto& Runtime : TargetRegionCapabilityCohorts)
      CurrentStateHash = FoldBoundaryHash(
        CurrentStateHash, CalculateTargetResourceCohortStateHash(Runtime));
    for (const auto& Apply : Prepared.CohortApplies)
    {
      if (!TargetRegionCapabilityCohorts.IsValidIndex(Apply.DestinationIndex)
        || TargetRegionCapabilityCohorts[Apply.DestinationIndex].Cohort.
          CapabilityProfileKey != Apply.CohortKey
        || CalculateTargetResourceCohortStateHash(
          TargetRegionCapabilityCohorts[Apply.DestinationIndex])
            != Apply.BaseStateHash)
        return false;
    }
  }
  else
  {
    CurrentStateHash = CalculateHomogeneousTargetResourceStateHash(
      PreparedTargetRegionTopology, PreparedTargetRegionDemand,
      PreparedTargetRegionPlan, TargetRegionQuotaExecution,
      TargetRegionPlanValidation, TargetRegionGuidanceSummary,
      TargetRegionTopologyRoundHash, TargetRegionDemandRoundHash,
      TargetRegionTransportRoundHash, TargetRegionGuidanceRoundHash,
      TargetRegionValidationRoundHash, TargetRegionPlanRebuildCount,
      TargetRegionInvalidStepCount);
  }
  return CurrentStateHash == Prepared.CommitToken.BaseStateHash;
}

void UCrowdDemoRoundSimPipelineSubsystem::
  ApplyPreparedTargetResourcePlanNoFail(
    FCrowdDemoPreparedTargetResourcePlan& Prepared)
{
  check(IsInGameThread());
  checkf(Prepared.bValid && Prepared.BuildCount == 1
      && Prepared.ApplyCount == 0,
    TEXT("Target/resource prepared plan escaped owner barrier validation"));
  for (auto& Apply : Prepared.CohortApplies)
    TargetRegionCapabilityCohorts[Apply.DestinationIndex] =
      MoveTemp(Apply.PreparedRuntime);
  if (Prepared.HomogeneousApply.bSet)
  {
    auto& Apply = Prepared.HomogeneousApply;
    PreparedTargetRegionTopology = MoveTemp(Apply.Topology);
    TargetRegionTopologySummary = MoveTemp(Apply.TopologySummary);
    PreparedTargetRegionDemand = MoveTemp(Apply.Demand);
    PreparedTargetRegionPlan = MoveTemp(Apply.Plan);
    TargetRegionQuotaExecution = MoveTemp(Apply.QuotaExecution);
    TargetRegionPlanValidation = MoveTemp(Apply.Validation);
    PreparedTargetRegionGuidance = MoveTemp(Apply.Guidance);
    TargetRegionGuidanceSummary = MoveTemp(Apply.GuidanceSummary);
    RecordTargetRegionTopologyStep();
    RecordTargetRegionDemandStep();
    RecordTargetRegionTransportStep(
      Apply.SolverMilliseconds, Apply.RebuildReason);
    RecordTargetRegionValidationStep();
    RecordTargetRegionGuidanceStep();
    RecordOpenCohortMovementGuidance(PreparedTargetRegionGuidance);
  }
  ++Prepared.ApplyCount;
  ++TargetResourceOwnerRevision;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeBoundaryFacingWork(
  FCrowdMassFacingFinalizeWorkOutput& OutOutput,
  TMap<int32, int32>& OutConsecutiveSettleStepsByAgentId,
  TMap<int32, bool>& OutFinalSettledByAgentId)
{
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  OutOutput = MoveTemp(BoundaryFacingWorkState->Output);
  OutConsecutiveSettleStepsByAgentId = MoveTemp(
    BoundaryFacingWorkState->ConsecutiveSettleStepsByAgentId);
  OutFinalSettledByAgentId = MoveTemp(
    BoundaryFacingWorkState->FinalSettledByAgentId);
  BoundaryFacingWorkState.Reset();
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedMovementBoundaryCommit(
  FCrowdDemoPreparedMovementBoundaryCommit&& Commit)
{
  if (PreparedMovementBoundaryCommit.bValid
    || !Commit.bValid
    || Commit.StableHash == 0
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || !Commit.Facing.bCompleted
    || !Commit.Finalize.bCompleted
    || !Commit.Finalize.CommitPlan.bValid
    || Commit.Finalize.CommitPlan.FixedStepIndex
      != GetCurrentFixedStepIndex()
    || Commit.Finalize.CommitPlan.PlanRevision
      != GetCurrentPlanRevision()
    || Commit.Finalize.CommitPlan.Records.Num()
      != BoundarySnapshot.Agents.Num()
    || Commit.ConsecutiveSettleStepsByAgentId.Num()
      != BoundarySnapshot.Agents.Num()
    || Commit.FinalSettledByAgentId.Num()
      != BoundarySnapshot.Agents.Num())
    return false;
  PreparedMovementBoundaryCommit = MoveTemp(Commit);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ValidateRoundApplyPlan(
  const TConstArrayView<FCrowdMassCommitTarget> ResolvedTargets)
{
  if (!IsInGameThread() || !BoundaryOrchestrator.IsValid()
    || BoundaryOrchestrator->GetState()
      != ECrowdBoundaryTransactionState::Merging)
    return false;
  const double ValidateStartSeconds = FPlatformTime::Seconds();
  if (!IsPreparedMovementBoundaryCommitCurrent()
    || PreparedMovementBoundaryCommit.StableHash == 0
    || PreparedPlannerDecisionHash == 0
    || !FCrowdMassRuntimeBridge::ValidateCommitTargets(
      PreparedMovementBoundaryCommit.Finalize.CommitPlan, ResolvedTargets))
    return false;
  if (IsPreparedCombatBoundaryCommitCurrent()
    && PreparedCombatBoundaryCommit.StableHash == 0)
    return false;

  uint64 ApplyPlanHash = FoldBoundaryHash(
    BoundarySnapshot.StableHash,
    PreparedMovementBoundaryCommit.Finalize.CommitPlan.StableHash);
  ApplyPlanHash = FoldBoundaryHash(
    ApplyPlanHash, PreparedMovementBoundaryCommit.StableHash);
  ApplyPlanHash = FoldBoundaryHash(
    ApplyPlanHash, PreparedPlannerDecisionHash);
  if (IsPreparedCombatBoundaryCommitCurrent())
  {
    ApplyPlanHash = FoldBoundaryHash(
      ApplyPlanHash, PreparedCombatBoundaryCommit.StableHash);
  }
  uint64 FlowDiagnosticHash = 14695981039346656037ull;
  for (const FCrowdMassSharedFlowAgentOutput& Output
    : PreparedRuntimeSharedFlowOutputs)
  {
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash, static_cast<uint64>(Output.AgentId));
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash,
      static_cast<uint64>(Output.Sample.StableCellKey));
    FlowDiagnosticHash = FoldBoundaryHash(
      FlowDiagnosticHash,
      static_cast<uint64>(Output.Candidate.StableHash));
  }
  ApplyPlanHash = FoldBoundaryHash(ApplyPlanHash, FlowDiagnosticHash);
  const FCrowdDemoPreparedRoundCommitPlan* Pending =
    PeekPreparedRoundCommitPlan();
  if (!Pending || !Pending->PreparedTargetResourcePlan.IsValid()
    || !Pending->PreparedTargetResourcePlan->bValid)
    return false;
  ApplyPlanHash = FoldBoundaryHash(
    ApplyPlanHash,
    Pending->PreparedTargetResourcePlan->CommitToken.PreparedStateHash);
  if (IsPreparedParticleDiagnosticCommitCurrent())
  {
    uint64 ParticleHash = FoldBoundaryHash(
      14695981039346656037ull,
      PreparedParticleDiagnosticCommit.AppliedStateHash);
    ParticleHash = FoldBoundaryHash(
      ParticleHash,
      PreparedParticleDiagnosticCommit.CandidateSummary.CandidateHash);
    if (ParticleHash == 0)
      return false;
    ApplyPlanHash = FoldBoundaryHash(ApplyPlanHash, ParticleHash);
  }
  const double ValidateMilliseconds =
    (FPlatformTime::Seconds() - ValidateStartSeconds) * 1000.0;
  if (!BoundaryOrchestrator->MarkApplyPlanValidated(
      ApplyPlanHash, 0.0, ValidateMilliseconds))
  {
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::MarkRoundApplyCommitted(
  const double CommitMilliseconds)
{
  if (!BoundaryOrchestrator.IsValid()
    || !BoundaryOrchestrator->MarkCommitted(CommitMilliseconds))
    return false;
  LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
  float CriticalPathMilliseconds = 0.0f;
  for (const FCrowdBoundaryCompletedTask& Task
    : LastBoundaryTransactionResult.Tasks)
  {
    BoundaryWorkerQueueMsSamples.Add(static_cast<float>(
      Task.Timings.QueueMilliseconds));
    BoundaryWorkerRunMsSamples.Add(static_cast<float>(
      Task.Timings.ExecutionMilliseconds));
    CriticalPathMilliseconds = FMath::Max(
      CriticalPathMilliseconds,
      static_cast<float>(Task.Timings.EndToEndMilliseconds));
  }
  BoundaryWorkerCriticalPathMsSamples.Add(CriticalPathMilliseconds);
  if (LastBoundaryTransactionResult.bSucceeded
    && PreparedObstacleMaxReprojectDeltaCm >= 0.0f)
  {
    RecordNavigationDomainReprojectDelta(
      PreparedObstacleMaxReprojectDeltaCm);
  }
  if (LastBoundaryTransactionResult.bSucceeded
    && (GetCurrentFixedStepIndex() == 0
      || GetCurrentFixedStepIndex() % 300 == 0))
  {
    const FCrowdBoundaryPhaseTimings& Timings =
      LastBoundaryTransactionResult.Timings;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoBoundaryTransaction step=%d plan_revision=%d snapshot_hash=%llu commit_hash=%llu gather_ms=%.3f queue_ms=%.3f work_ms=%.3f wait_ms=%.3f merge_ms=%.3f validate_ms=%.3f commit_ms=%.3f worker_critical_ms=%.3f tasks=%d pending_frames=%d stale_results=%d ordinary_block_wait_count=%d source=MassPipeline"),
      GetCurrentFixedStepIndex(), GetCurrentPlanRevision(),
      static_cast<unsigned long long>(
        LastBoundaryTransactionResult.SnapshotHash),
      static_cast<unsigned long long>(
        LastBoundaryTransactionResult.CommitPlanHash),
      Timings.GatherMilliseconds, Timings.QueueMilliseconds,
      Timings.WorkMilliseconds, Timings.WaitMilliseconds,
      Timings.MergeMilliseconds, Timings.ValidateMilliseconds,
      Timings.CommitMilliseconds,
      CriticalPathMilliseconds,
      LastBoundaryTransactionResult.Tasks.Num(),
      BoundaryPendingFrameCount, BoundaryStaleResultCount,
      BoundaryOrdinaryBlockWaitCount);
  }
  return LastBoundaryTransactionResult.bSucceeded;
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedCombatBoundaryCommit(
  FCrowdDemoPreparedCombatBoundaryCommit&& Commit)
{
  if (PreparedCombatBoundaryCommit.bValid
    || !Commit.bValid
    || Commit.StableHash == 0
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || Commit.Agents.Num() != BoundarySnapshot.Agents.Num())
    return false;
  Commit.Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Commit.Agents.Num(); ++Index)
  {
    if (Commit.Agents[Index].AgentId
        != BoundarySnapshot.Agents[Index].Identity.AgentId
      || static_cast<uint32>(Commit.Agents[Index].LifecycleSerial)
        != BoundarySnapshot.Agents[Index].AgentFacts.StableEntityRef.LifecycleSerial
      || (Index > 0
        && Commit.Agents[Index - 1].AgentId
          >= Commit.Agents[Index].AgentId))
      return false;
  }
  PreparedCombatBoundaryCommit = MoveTemp(Commit);
  return true;
}

const FCrowdDemoRoundBoundaryFormationFact*
UCrowdDemoRoundSimPipelineSubsystem::FindBoundaryFormationFact(
  const int32 AgentId) const
{
  return BoundaryFormationFacts.FindByPredicate(
    [AgentId](const FCrowdDemoRoundBoundaryFormationFact& Value)
    {
      return Value.AgentId == AgentId;
    });
}

bool UCrowdDemoRoundSimPipelineSubsystem::EnsureDynamicSharedFlowField(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& TargetLocation)
{
  FCrowdMassSharedFlowBuildInput Input;
  Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
  Input.TargetLocation = TargetLocation;
  Input.bDynamicTarget = true;
  Input.bForceIntegrationRefresh = bDynamicFlowIntegrationCacheInvalidated;
  const FCrowdMassSharedFlowBuildOutput Output =
    FCrowdMassSharedFlowWork::EnsureResource(
      Input, RuntimeSharedFlowResource);
  if (!Output.bValid) return false;
  DynamicFlowAnchorCellKey = Output.DynamicAnchorCellKey;
  SharedFlowFieldRebuildCount = RuntimeSharedFlowResource.FieldRebuildCount;
  DynamicFlowIntegrationRebuildCount =
    RuntimeSharedFlowResource.IntegrationRebuildCount;
  bDynamicFlowIntegrationCacheInvalidated = false;
  if (Output.bFieldRebuilt || Output.bIntegrationRebuilt
    || !SharedFlowField.IsValid()
    || SharedFlowField.BuildHash != RuntimeSharedFlowResource.Field.BuildHash)
  {
    SharedFlowField = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(
      RuntimeSharedFlowResource.Field);
  }

  auto Fold = [](uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= 16777619u;
    return Hash;
  };
  const int32 FixedStepIndex = GetCurrentFixedStepIndex();
  if (DynamicFlowRoundHashFixedStepIndex != FixedStepIndex)
  {
    DynamicFlowRoundHash = Fold(
      DynamicFlowRoundHash, static_cast<uint32>(FixedStepIndex));
    DynamicFlowRoundHash = Fold(
      DynamicFlowRoundHash, static_cast<uint32>(DynamicFlowAnchorCellKey));
    DynamicFlowRoundHash = Fold(
      DynamicFlowRoundHash, SharedFlowField.TopologyHash);
    DynamicFlowRoundHash = Fold(
      DynamicFlowRoundHash, SharedFlowField.IntegrationHash);
    DynamicFlowRoundHashFixedStepIndex = FixedStepIndex;
  }
  LastCompareMetrics.FlowFieldRevision = SharedFlowField.Config.Revision;
  LastCompareMetrics.FlowFieldBuildHash = SharedFlowField.BuildHash;
  LastCompareMetrics.FlowFieldRebuildCount = SharedFlowFieldRebuildCount;
  LastCompareMetrics.FlowBlockedCellCount = SharedFlowField.BlockedCellCount;
  return SharedFlowField.IsValid();
}


bool UCrowdDemoRoundSimPipelineSubsystem::EnsureBidirectionalSwapFlowFields()
{
  if (!IsBidirectionalSwap()) return false;
  bool bAllValid = true;
  for (int32 CohortId = 0; CohortId < 2; ++CohortId)
  {
    const FCrowdDemoSharedFlowFieldConfig Config =
      FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortId);
    FCrowdMassSharedFlowBuildInput Input;
    Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
    FCrowdMassSharedFlowResource& Resource =
      RuntimeBidirectionalSwapFlowResources[CohortId];
    const FCrowdMassSharedFlowBuildOutput Output =
      FCrowdMassSharedFlowWork::EnsureResource(Input, Resource);
    bAllValid &= Output.bValid;
    FCrowdDemoSharedFlowField& Field = BidirectionalSwapFlowFields[CohortId];
    if (Output.bValid && (Output.bFieldRebuilt || !Field.IsValid()
      || Field.BuildHash != Resource.Field.BuildHash))
      Field = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(Resource.Field);
  }
  return bAllValid;
}

const FCrowdSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindRuntimeBidirectionalSwapFlowField(
  const int32 FormationIndex) const
{
  const int32 CohortId =
    FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(FormationIndex);
  return CohortId >= 0 && CohortId < RuntimeBidirectionalSwapFlowResources.Num()
    ? &RuntimeBidirectionalSwapFlowResources[CohortId].Field : nullptr;
}

const FCrowdDemoSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindBidirectionalSwapFlowField(
  const int32 FormationIndex) const
{
  const int32 CohortId =
    FCrowdDemoBidirectionalSwapKernel::CohortIdForFormationIndex(FormationIndex);
  return CohortId >= 0 && CohortId < BidirectionalSwapFlowFields.Num()
    ? &BidirectionalSwapFlowFields[CohortId] : nullptr;
}


void UCrowdDemoRoundSimPipelineSubsystem::RecordFlowConnectivityStep(
  const int32 RecoveredCount,
  const int32 DesiredSegmentViolationCount,
  const int32 SourceAttachmentSuccessCount,
  const int32 UnreachableSampleCount)
{
  FCrowdDemoSharedFlowMetrics& Metrics = LastCompareMetrics.SharedFlowMetrics;
  Metrics.FlowRecoveredFromRasterMismatchCount += FMath::Max(0, RecoveredCount);
  Metrics.FlowDesiredSegmentHardObstacleViolationCount +=
    FMath::Max(0, DesiredSegmentViolationCount);
  Metrics.SourceAttachmentSuccessCount += FMath::Max(0, SourceAttachmentSuccessCount);
  Metrics.NavigationUnreachableSampleCount += FMath::Max(0, UnreachableSampleCount);
}

FCrowdDemoSharedFlowMetrics UCrowdDemoRoundSimPipelineSubsystem::BuildSharedFlowMetrics(
  const TConstArrayView<FCrowdDemoRoundAgentState> States) const
{
  FCrowdDemoSharedFlowMetrics Metrics = LastCompareMetrics.SharedFlowMetrics;
  Metrics.SharedFlowFieldBuildHash = SharedFlowField.BuildHash;
  Metrics.SharedFlowConnectivityContractVersion =
    SharedFlowField.Config.ConnectivityContractVersion;
  Metrics.SharedFlowValidDirectedEdgeCount = SharedFlowField.ValidDirectedEdgeCount;
  Metrics.NavigationHardClearanceCm = GetRules().GetParticleEnvironmentHardClearanceCm();
  Metrics.NavigationCenterAnchorCount = SharedFlowField.NavigationCenterAnchorCount;
  Metrics.NavigationConnectionPointCount = SharedFlowField.NavigationConnectionPointCount;
  Metrics.NavigationSafeIntervalCount = SharedFlowField.NavigationSafeIntervalCount;
  Metrics.NavigationInternalEdgeCount = SharedFlowField.NavigationInternalEdgeCount;
  Metrics.NavigationDirectedEdgeCount = SharedFlowField.ValidDirectedEdgeCount;
  Metrics.CenterInvalidButConnectedCellCount =
    SharedFlowField.CenterInvalidButConnectedCellCount;
  Metrics.GoalAttachmentCount = SharedFlowField.GoalAttachmentCount;
  Metrics.NavigationV2Hash = SharedFlowField.Config.ConnectivityContractVersion >= 2
    ? SharedFlowField.BuildHash : 0;
  uint32 Hash = 2166136261u;
  for (const FCrowdDemoRoundAgentState& State : States)
  {
    Hash = FoldHash(Hash, static_cast<uint32>(State.AgentId));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Location.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.X)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.Velocity.Y)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.YawDegrees)));
    Hash = FoldHash(Hash, static_cast<uint32>(FMath::RoundToInt(State.RadiusCm)));
  }
  Metrics.AgentStateHash = Hash;
  return Metrics;
}

int32 UCrowdDemoRoundSimPipelineSubsystem::GetCurrentFixedStepIndex() const
{
  if (!IsActive() || CurrentFixedStepSeconds <= 0.0f)
  {
    return INDEX_NONE;
  }
  return FMath::Max(0, FMath::RoundToInt(
    ((bStepInProgress ? CurrentStepStartServerTimeSeconds : SimulatedServerTimeSeconds)
      - ActivePlan.StartServerTimeSeconds)
    / CurrentFixedStepSeconds));
}
void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex,
  TArray<FCrowdDemoSoftPressureRollbackAgentState>&& Agents)
{
  if (!IsActive() || !IsFlowScenario(GetRules().Scenario))
    return;
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  FCrowdDemoSoftPressureRollbackSnapshot& Snapshot =
    SoftPressureRollbackHistory.FindOrAdd(FixedStepIndex);
  Snapshot.FixedStepIndex = FixedStepIndex;
  Snapshot.bMovementFactsComplete = false;
  Snapshot.bCombatFactsComplete = false;
  Snapshot.bSnapshotReadyForReplay = false;
  Snapshot.Agents = MoveTemp(Agents);
  bool bAgentSetValid = Snapshot.Agents.Num() == BoundarySnapshot.Agents.Num();
  if (!bAgentSetValid)
  {
    Snapshot.bMovementFactsComplete = false;
    return;
  }
  for (int32 Index = 0; Index < Snapshot.Agents.Num(); ++Index)
  {
    bAgentSetValid &= Snapshot.Agents[Index].AgentId != INDEX_NONE
      && Snapshot.Agents[Index].AgentId
        == BoundarySnapshot.Agents[Index].Identity.AgentId
      && (Index == 0
        || Snapshot.Agents[Index - 1].AgentId < Snapshot.Agents[Index].AgentId);
  }
  Snapshot.bMovementFactsComplete = bAgentSetValid;
  Snapshot.LocalPredictiveResults = PreparedLocalPredictiveResults;
  Snapshot.LocalPredictiveGrantStates = LocalPredictiveGrantStates;
  Snapshot.LocalPredictiveSummary = LastLocalPredictiveSummary;
  Snapshot.LocalPredictiveRoundHash = LocalPredictiveRoundHash;
  Snapshot.LocalPredictiveSampleCount = LocalPredictiveSampleCount;
  Snapshot.LocalPredictiveInvalidStepCount = LocalPredictiveInvalidStepCount;
  Snapshot.GuidanceCandidateRoundHash = GuidanceCandidateRoundHash;
  Snapshot.GuidanceComposeRoundHash = GuidanceComposeRoundHash;
  Snapshot.GuidanceComposeSampleCount = GuidanceComposeSampleCount;
  Snapshot.ParticleCandidateSummary = LastParticleCandidateSummary;
  Snapshot.ParticleAppliedSummary = LastParticleAppliedSummary;
  Snapshot.ParticleSolverMsSampleCount = ParticleSolverMillisecondsSamples.Num();
  Snapshot.ParticleCandidateStateHash = ParticleCandidateStateHash;
  Snapshot.ParticleAppliedStateHash = ParticleAppliedStateHash;
  Snapshot.ParticleInvalidStepCount = ParticleInvalidStepCount;
  Snapshot.ParticleGlobalFallbackStepCount = ParticleGlobalFallbackStepCount;
  Snapshot.ParticleStepCount = ParticleStepCount;
  Snapshot.CrossProfileHardViolationCount = CrossProfileHardViolationCount;
  Snapshot.CrossProfileSweptViolationCount = CrossProfileSweptViolationCount;
  Snapshot.ParticleSettlingWindowCount = ParticleSettlingWindowCount;
  Snapshot.ParticleSettlingSteps = ParticleSettlingSteps;
  Snapshot.ParticlePreviousSoftErrorP95 = ParticlePreviousSoftErrorP95;
  Snapshot.bParticleConstraintRunFailure = bParticleConstraintRunFailure;
  Snapshot.ParticleFailureFixture = ParticleFailureFixture;
  Snapshot.OpenSpawnRelaxationRuntime = OpenSpawnRelaxationRuntime;
  Snapshot.OpenCohortMovementProgress = OpenCohortMovementProgress;
  Snapshot.BidirectionalSwapProgress = BidirectionalSwapProgress;
  Snapshot.ValidCorridorTransitProgress = ValidCorridorTransitProgress;
  Snapshot.TargetFact = TargetFact;
  Snapshot.DynamicFlowAnchorCellKey = DynamicFlowAnchorCellKey;
  Snapshot.DynamicFlowIntegrationRebuildCount = DynamicFlowIntegrationRebuildCount;
  Snapshot.DynamicFlowRoundHash = DynamicFlowRoundHash;
  Snapshot.DynamicFlowRoundHashFixedStepIndex =
    DynamicFlowRoundHashFixedStepIndex;
  Snapshot.TargetRegionPlanResourceKey = PreparedTargetRegionPlan.bValid
    ? MakeTargetPlanResourceKey(0, PreparedTargetRegionPlan)
    : 0;
  if (PreparedTargetRegionPlan.bValid)
    TargetRegionPlanResources.FindOrAdd(Snapshot.TargetRegionPlanResourceKey) =
      PreparedTargetRegionPlan;
  Snapshot.TargetRegionQuotaExecution = TargetRegionQuotaExecution;
  Snapshot.TargetRegionPlanValidation = TargetRegionPlanValidation;
  Snapshot.TargetRegionTopologyRoundHash = TargetRegionTopologyRoundHash;
  Snapshot.TargetRegionDemandRoundHash = TargetRegionDemandRoundHash;
  Snapshot.TargetRegionTransportRoundHash = TargetRegionTransportRoundHash;
  Snapshot.TargetRegionGuidanceRoundHash = TargetRegionGuidanceRoundHash;
  Snapshot.TargetRegionPlanRebuildCount = TargetRegionPlanRebuildCount;
  Snapshot.TargetRegionLifetimeRebuildCount = TargetRegionLifetimeRebuildCount;
  Snapshot.TargetRegionTargetRebuildCount = TargetRegionTargetRebuildCount;
  Snapshot.TargetRegionEnvironmentRebuildCount = TargetRegionEnvironmentRebuildCount;
  Snapshot.TargetRegionMembershipRebuildCount = TargetRegionMembershipRebuildCount;
  Snapshot.TargetRegionDemandSatisfiedRebuildCount = TargetRegionDemandSatisfiedRebuildCount;
  Snapshot.TargetRegionPathInvalidRebuildCount = TargetRegionPathInvalidRebuildCount;
  Snapshot.TargetRegionSolverMsSampleCount = TargetRegionSolverMillisecondsSamples.Num();
  Snapshot.bTargetRegionRoundValid = bTargetRegionRoundValid;
  Snapshot.TargetRegionInvalidStepCount = TargetRegionInvalidStepCount;
  Snapshot.TargetRegionLastInvalidStep = TargetRegionLastInvalidStep;
  Snapshot.TargetRegionValidationFailureCount = TargetRegionValidationFailureCount;
  Snapshot.TargetRegionValidationRoundHash = TargetRegionValidationRoundHash;
  Snapshot.TargetRegionGuidanceUnroutedStepCount = TargetRegionGuidanceUnroutedStepCount;
  Snapshot.TargetRegionGuidanceUnroutedAgentSampleCount = TargetRegionGuidanceUnroutedAgentSampleCount;
  Snapshot.TargetRegionGuidanceUnroutedAgentMax = TargetRegionGuidanceUnroutedAgentMax;
  Snapshot.TargetRegionGuidanceFirstFailureStep = TargetRegionGuidanceFirstFailureStep;
  Snapshot.TargetRegionGuidanceFirstFailureAgentId = TargetRegionGuidanceFirstFailureAgentId;
  Snapshot.bTargetRegionFailureFixtureValid = bTargetRegionFailureFixtureValid;
  Snapshot.TargetRegionFailureFixtureStep = TargetRegionFailureFixtureStep;
  Snapshot.TargetRegionFailureFixtureKind = TargetRegionFailureFixtureKind;
  Snapshot.TargetRegionFailureFixtureAgentId = TargetRegionFailureFixtureAgentId;
  Snapshot.TargetRegionFailureFixtureCellKey = TargetRegionFailureFixtureCellKey;
  Snapshot.TargetRegionFailureFixtureHash = TargetRegionFailureFixtureHash;
  Snapshot.TargetRegionCapabilityCohorts.Reset(TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime
    : TargetRegionCapabilityCohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRollbackState& State =
      Snapshot.TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    State.Cohort = Runtime.Cohort;
    State.DemandRegionPhaseOffset = Runtime.DemandRegionPhaseOffset;
    State.PlanResourceKey = Runtime.Plan.bValid
      ? MakeTargetPlanResourceKey(Runtime.Cohort.CapabilityProfileKey, Runtime.Plan)
      : 0;
    if (Runtime.Plan.bValid)
      TargetRegionPlanResources.FindOrAdd(State.PlanResourceKey) = Runtime.Plan;
    State.QuotaExecution = Runtime.QuotaExecution;
    State.LastPlanReplacement = Runtime.LastPlanReplacement;
    State.Validation = Runtime.Validation;
    State.TopologyRoundHash = Runtime.TopologyRoundHash;
    State.DemandRoundHash = Runtime.DemandRoundHash;
    State.TransportRoundHash = Runtime.TransportRoundHash;
    State.GuidanceRoundHash = Runtime.GuidanceRoundHash;
    State.ValidationRoundHash = Runtime.ValidationRoundHash;
    State.PlanRebuildCount = Runtime.PlanRebuildCount;
    State.InvalidStepCount = Runtime.InvalidStepCount;
    State.ValidationFailureCount = Runtime.ValidationFailureCount;
    State.GuidanceUnroutedStepCount = Runtime.GuidanceUnroutedStepCount;
    State.LastInvalidStep = Runtime.LastInvalidStep;
    State.SolverMsSampleCount = Runtime.SolverMillisecondsSamples.Num();
    State.PlanLifecycle = Runtime.PlanLifecycle;
    State.TargetEngagedHoldAgentIds = Runtime.TargetEngagedHoldAgentIds;
    State.TargetEngagementAcquireCount = Runtime.TargetEngagementAcquireCount;
    State.TargetEngagementReleaseCount = Runtime.TargetEngagementReleaseCount;
    State.TargetEngagementSuppressedRetreatCount =
      Runtime.TargetEngagementSuppressedRetreatCount;
    State.bRoundValid = Runtime.bRoundValid;
  }
  Snapshot.CapabilityProfileSummary = CapabilityProfileSummary;
  Snapshot.CapabilityCohortRebuildCount = CapabilityCohortRebuildCount;
  Snapshot.TargetRegionPlanLifecycleSummary = TargetRegionPlanLifecycleSummary;
  Snapshot.TargetRegionPlanLifecycleFixture = TargetRegionPlanLifecycleFixture;
  Snapshot.FlowGoalReachedAgentIds = FlowGoalReachedAgentIds;
  Snapshot.FlowWallPassAgentIds = FlowWallPassAgentIds;
  Snapshot.FlowCorridorExitAgentIds = FlowCorridorExitAgentIds;
  Snapshot.FlowTurnExitAgentIds = FlowTurnExitAgentIds;
  Snapshot.FlowLowSpeedSecondsByAgentId = FlowLowSpeedSecondsByAgentId;
  Snapshot.FlowCorridorDeadlockAgentIds = FlowCorridorDeadlockAgentIds;
  Snapshot.CompareMetrics = LastCompareMetrics;
  const bool bProjectileSnapshotBuilt =
    BuildProjectileSnapshot(Snapshot.Projectiles);
  checkf(bProjectileSnapshotBuilt,
    TEXT("Mass projectile authority unavailable during checkpoint"));
  Snapshot.ProjectileMetrics = ProjectileMetrics;
  if (IsSoftPressureRouteDiagnosticEnabled())
    Snapshot.RouteDiagnosticCheckpoint =
      FCrowdDemoSoftPressureRouteDiagnosticKernel::MakeCheckpoint(
        SoftPressureRouteDiagnosticRuntime);
  if (IsTargetStabilityDiagnosticEnabled())
    Snapshot.TargetStabilityCheckpoint =
      FCrowdDemoTargetStabilityDiagnosticKernel::MakeCheckpoint(
        TargetStabilityRuntime);
  SoftPressureRollbackHistory.Remove(FixedStepIndex - 128);
}

bool UCrowdDemoRoundSimPipelineSubsystem::CompleteSoftPressureRollbackCombatState(
  const int32 FixedStepIndex,
  const TConstArrayView<FCrowdDemoPreparedCombatRollbackFact> CombatStates)
{
  FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    SoftPressureRollbackHistory.Find(FixedStepIndex);
  if (!Snapshot || !Snapshot->bMovementFactsComplete
    || Snapshot->bCombatFactsComplete || Snapshot->bSnapshotReadyForReplay
    || Snapshot->Agents.Num() != CombatStates.Num())
    return false;

  TArray<FCrowdDemoPreparedCombatRollbackFact> SortedStates(CombatStates);
  SortedStates.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
  {
    if (SortedStates[Index].AgentId == INDEX_NONE
      || Snapshot->Agents[Index].AgentId != SortedStates[Index].AgentId
      || (Index > 0 && SortedStates[Index - 1].AgentId == SortedStates[Index].AgentId))
      return false;
  }
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
    Snapshot->Agents[Index].Combat = SortedStates[Index].Combat;
  Snapshot->bCombatFactsComplete = true;
  Snapshot->bSnapshotReadyForReplay = true;
  return true;
}

const FCrowdDemoSoftPressureRollbackSnapshot*
UCrowdDemoRoundSimPipelineSubsystem::FindSoftPressureRollbackSnapshot(
  const int32 FixedStepIndex) const
{
  return SoftPressureRollbackHistory.Find(FixedStepIndex);
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSoftPressureRollbackSnapshotReadyForReplay(
  const int32 FixedStepIndex) const
{
  const FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    SoftPressureRollbackHistory.Find(FixedStepIndex);
  return Snapshot && Snapshot->bMovementFactsComplete
    && Snapshot->bCombatFactsComplete && Snapshot->bSnapshotReadyForReplay;
}

void UCrowdDemoRoundSimPipelineSubsystem::RestoreSoftPressureRuntime(
  const FCrowdDemoSoftPressureRollbackSnapshot& Snapshot)
{
  PreparedLocalPredictiveResults = Snapshot.LocalPredictiveResults;
  LocalPredictiveGrantStates = Snapshot.LocalPredictiveGrantStates;
  LastLocalPredictiveSummary = Snapshot.LocalPredictiveSummary;
  LocalPredictiveRoundHash = Snapshot.LocalPredictiveRoundHash;
  LocalPredictiveSampleCount = Snapshot.LocalPredictiveSampleCount;
  LocalPredictiveInvalidStepCount = Snapshot.LocalPredictiveInvalidStepCount;
  GuidanceCandidateRoundHash = Snapshot.GuidanceCandidateRoundHash;
  GuidanceComposeRoundHash = Snapshot.GuidanceComposeRoundHash;
  GuidanceComposeSampleCount = Snapshot.GuidanceComposeSampleCount;
  LastParticleCandidateSummary = Snapshot.ParticleCandidateSummary;
  LastParticleAppliedSummary = Snapshot.ParticleAppliedSummary;
  ParticleSolverMillisecondsSamples.SetNum(FMath::Min(
    ParticleSolverMillisecondsSamples.Num(), Snapshot.ParticleSolverMsSampleCount));
  ParticleCandidateStateHash = Snapshot.ParticleCandidateStateHash;
  ParticleAppliedStateHash = Snapshot.ParticleAppliedStateHash;
  ParticleInvalidStepCount = Snapshot.ParticleInvalidStepCount;
  ParticleGlobalFallbackStepCount = Snapshot.ParticleGlobalFallbackStepCount;
  ParticleStepCount = Snapshot.ParticleStepCount;
  CrossProfileHardViolationCount = Snapshot.CrossProfileHardViolationCount;
  CrossProfileSweptViolationCount = Snapshot.CrossProfileSweptViolationCount;
  ParticleSettlingWindowCount = Snapshot.ParticleSettlingWindowCount;
  ParticleSettlingSteps = Snapshot.ParticleSettlingSteps;
  ParticlePreviousSoftErrorP95 = Snapshot.ParticlePreviousSoftErrorP95;
  bParticleConstraintRunFailure = Snapshot.bParticleConstraintRunFailure;
  ParticleFailureFixture = Snapshot.ParticleFailureFixture;
  OpenSpawnRelaxationRuntime = Snapshot.OpenSpawnRelaxationRuntime;
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  OpenCohortMovementProgress = Snapshot.OpenCohortMovementProgress;
  BidirectionalSwapProgress = Snapshot.BidirectionalSwapProgress;
  ValidCorridorTransitProgress = Snapshot.ValidCorridorTransitProgress;
  TargetFact = Snapshot.TargetFact;
  DynamicFlowAnchorCellKey = Snapshot.DynamicFlowAnchorCellKey;
  RuntimeSharedFlowResource.DynamicAnchorCellKey =
    Snapshot.DynamicFlowAnchorCellKey;
  DynamicFlowIntegrationRebuildCount = Snapshot.DynamicFlowIntegrationRebuildCount;
  RuntimeSharedFlowResource.IntegrationRebuildCount =
    Snapshot.DynamicFlowIntegrationRebuildCount;
  DynamicFlowRoundHash = Snapshot.DynamicFlowRoundHash;
  DynamicFlowRoundHashFixedStepIndex =
    Snapshot.DynamicFlowRoundHashFixedStepIndex;
  bDynamicFlowIntegrationCacheInvalidated = true;
  PreparedTargetRegionTopology = {};
  TargetRegionTopologySummary = {};
  PreparedTargetRegionAgents.Reset();
  PreparedTargetRegionDemand = {};
  PreparedTargetRegionPlan = {};
  if (const FCrowdDemoTargetRegionFlowPlan* Resource =
    TargetRegionPlanResources.Find(Snapshot.TargetRegionPlanResourceKey))
  {
    PreparedTargetRegionPlan = *Resource;
  }
  else if (Snapshot.TargetRegionPlanResourceKey != 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoRollbackPlanResourceMissing key=%llu step=%d"),
      static_cast<unsigned long long>(Snapshot.TargetRegionPlanResourceKey),
      Snapshot.FixedStepIndex);
  }
  TargetRegionQuotaExecution = Snapshot.TargetRegionQuotaExecution;
  TargetRegionPlanValidation = Snapshot.TargetRegionPlanValidation;
  PreparedTargetRegionGuidance.Reset();
  TargetRegionGuidanceSummary = {};
  TargetRegionTopologyRoundHash = Snapshot.TargetRegionTopologyRoundHash;
  TargetRegionDemandRoundHash = Snapshot.TargetRegionDemandRoundHash;
  TargetRegionTransportRoundHash = Snapshot.TargetRegionTransportRoundHash;
  TargetRegionGuidanceRoundHash = Snapshot.TargetRegionGuidanceRoundHash;
  TargetRegionPlanRebuildCount = Snapshot.TargetRegionPlanRebuildCount;
  TargetRegionLifetimeRebuildCount = Snapshot.TargetRegionLifetimeRebuildCount;
  TargetRegionTargetRebuildCount = Snapshot.TargetRegionTargetRebuildCount;
  TargetRegionEnvironmentRebuildCount = Snapshot.TargetRegionEnvironmentRebuildCount;
  TargetRegionMembershipRebuildCount = Snapshot.TargetRegionMembershipRebuildCount;
  TargetRegionDemandSatisfiedRebuildCount = Snapshot.TargetRegionDemandSatisfiedRebuildCount;
  TargetRegionPathInvalidRebuildCount = Snapshot.TargetRegionPathInvalidRebuildCount;
  TargetRegionSolverMillisecondsSamples.SetNum(FMath::Min(
    TargetRegionSolverMillisecondsSamples.Num(), Snapshot.TargetRegionSolverMsSampleCount));
  bTargetRegionRoundValid = Snapshot.bTargetRegionRoundValid;
  TargetRegionInvalidStepCount = Snapshot.TargetRegionInvalidStepCount;
  TargetRegionLastInvalidStep = Snapshot.TargetRegionLastInvalidStep;
  TargetRegionValidationFailureCount = Snapshot.TargetRegionValidationFailureCount;
  TargetRegionValidationRoundHash = Snapshot.TargetRegionValidationRoundHash;
  TargetRegionGuidanceUnroutedStepCount = Snapshot.TargetRegionGuidanceUnroutedStepCount;
  TargetRegionGuidanceUnroutedAgentSampleCount = Snapshot.TargetRegionGuidanceUnroutedAgentSampleCount;
  TargetRegionGuidanceUnroutedAgentMax = Snapshot.TargetRegionGuidanceUnroutedAgentMax;
  TargetRegionGuidanceFirstFailureStep = Snapshot.TargetRegionGuidanceFirstFailureStep;
  TargetRegionGuidanceFirstFailureAgentId = Snapshot.TargetRegionGuidanceFirstFailureAgentId;
  bTargetRegionFailureFixtureValid = Snapshot.bTargetRegionFailureFixtureValid;
  TargetRegionFailureFixtureStep = Snapshot.TargetRegionFailureFixtureStep;
  TargetRegionFailureFixtureKind = Snapshot.TargetRegionFailureFixtureKind;
  TargetRegionFailureFixtureAgentId = Snapshot.TargetRegionFailureFixtureAgentId;
  TargetRegionFailureFixtureCellKey = Snapshot.TargetRegionFailureFixtureCellKey;
  TargetRegionFailureFixtureHash = Snapshot.TargetRegionFailureFixtureHash;
  TMap<uint32, TArray<float>> SolverSamplesByProfile;
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Existing
    : TargetRegionCapabilityCohorts)
  {
    SolverSamplesByProfile.Add(
      Existing.Cohort.CapabilityProfileKey, Existing.SolverMillisecondsSamples);
  }
  TargetRegionCapabilityCohorts.Reset(Snapshot.TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRollbackState& State
    : Snapshot.TargetRegionCapabilityCohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime =
      TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    Runtime.Cohort = State.Cohort;
    Runtime.DemandRegionPhaseOffset = State.DemandRegionPhaseOffset;
    if (const FCrowdDemoTargetRegionFlowPlan* Resource =
      TargetRegionPlanResources.Find(State.PlanResourceKey))
      Runtime.Plan = *Resource;
    else if (State.PlanResourceKey != 0)
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoRollbackCohortPlanResourceMissing profile=%u key=%llu step=%d"),
        State.Cohort.CapabilityProfileKey,
        static_cast<unsigned long long>(State.PlanResourceKey),
        Snapshot.FixedStepIndex);
    Runtime.QuotaExecution = State.QuotaExecution;
    Runtime.LastPlanReplacement = State.LastPlanReplacement;
    Runtime.Validation = State.Validation;
    Runtime.TopologyRoundHash = State.TopologyRoundHash;
    Runtime.DemandRoundHash = State.DemandRoundHash;
    Runtime.TransportRoundHash = State.TransportRoundHash;
    Runtime.GuidanceRoundHash = State.GuidanceRoundHash;
    Runtime.ValidationRoundHash = State.ValidationRoundHash;
    Runtime.PlanRebuildCount = State.PlanRebuildCount;
    Runtime.InvalidStepCount = State.InvalidStepCount;
    Runtime.ValidationFailureCount = State.ValidationFailureCount;
    Runtime.GuidanceUnroutedStepCount = State.GuidanceUnroutedStepCount;
    Runtime.LastInvalidStep = State.LastInvalidStep;
    if (TArray<float>* Samples = SolverSamplesByProfile.Find(
      State.Cohort.CapabilityProfileKey))
    {
      Samples->SetNum(FMath::Min(Samples->Num(), State.SolverMsSampleCount));
      Runtime.SolverMillisecondsSamples = MoveTemp(*Samples);
    }
    Runtime.PlanLifecycle = State.PlanLifecycle;
    Runtime.TargetEngagedHoldAgentIds = State.TargetEngagedHoldAgentIds;
    Runtime.TargetEngagementAcquireCount = State.TargetEngagementAcquireCount;
    Runtime.TargetEngagementReleaseCount = State.TargetEngagementReleaseCount;
    Runtime.TargetEngagementSuppressedRetreatCount =
      State.TargetEngagementSuppressedRetreatCount;
    Runtime.bRoundValid = State.bRoundValid;
  }
  CapabilityProfileSummary = Snapshot.CapabilityProfileSummary;
  CapabilityCohortRebuildCount = Snapshot.CapabilityCohortRebuildCount;
  TargetRegionPlanLifecycleSummary = Snapshot.TargetRegionPlanLifecycleSummary;
  TargetRegionPlanLifecycleFixture = Snapshot.TargetRegionPlanLifecycleFixture;
  FlowGoalReachedAgentIds = Snapshot.FlowGoalReachedAgentIds;
  FlowWallPassAgentIds = Snapshot.FlowWallPassAgentIds;
  FlowCorridorExitAgentIds = Snapshot.FlowCorridorExitAgentIds;
  FlowTurnExitAgentIds = Snapshot.FlowTurnExitAgentIds;
  FlowLowSpeedSecondsByAgentId = Snapshot.FlowLowSpeedSecondsByAgentId;
  FlowCorridorDeadlockAgentIds = Snapshot.FlowCorridorDeadlockAgentIds;
  LastCompareMetrics = Snapshot.CompareMetrics;
  const bool bProjectileCapacityReady =
    PrepareProjectileFinalApply(Snapshot.Projectiles.Num());
  checkf(bProjectileCapacityReady,
    TEXT("Mass projectile rollback capacity validation failed"));
  ApplyProjectileFinalState(Snapshot.Projectiles);
  ProjectileMetrics = Snapshot.ProjectileMetrics;
  if (IsSoftPressureRouteDiagnosticEnabled())
  {
    FCrowdDemoSoftPressureRouteDiagnosticKernel::RestoreCheckpoint(
      Snapshot.RouteDiagnosticCheckpoint, SoftPressureRouteDiagnosticRuntime);
    SoftPressureRouteDiagnosticSummary = {};
  }
  if (IsTargetStabilityDiagnosticEnabled())
  {
    FCrowdDemoTargetStabilityDiagnosticKernel::RestoreCheckpoint(
      Snapshot.TargetStabilityCheckpoint, TargetStabilityRuntime);
    TargetStabilitySummary = {};
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::InitializeOpenSpawnRelaxation(
  const FCrowdDemoOpenSpawnRelaxationLayout& Layout)
{
  OpenSpawnRelaxationLayout = Layout;
  OpenSpawnRelaxationRuntime =
    FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(Layout);
}

bool UCrowdDemoRoundSimPipelineSubsystem::PrepareOpenSpawnRelaxationBoundary()
{
  if (!IsOpenSpawnRelaxation())
    return false;
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(
    GetCurrentFixedStepIndex(), OpenSpawnRelaxationLayout, OpenSpawnRelaxationRuntime);
  TArray<int32> ExpectedAgentIds;
  ExpectedAgentIds.Reserve(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
    ExpectedAgentIds.Add(Agent.Identity.AgentId);
  if (!FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      GetCurrentFixedStepIndex(), ExpectedAgentIds, OpenSpawnRelaxationRuntime,
      PreparedOpenSpawnBoundaryFacts))
  {
    PreparedOpenSpawnBoundaryFacts.Reset();
    PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
    return false;
  }
  PreparedOpenSpawnBoundaryFixedStepIndex = GetCurrentFixedStepIndex();
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::ConsumeOpenSpawnBoundaryResets(
  const TConstArrayView<int32> AgentIds)
{
  if (!ArePreparedOpenSpawnBoundaryFactsCurrent()) return false;
  for (const int32 AgentId : AgentIds)
  {
    const FCrowdDemoPreparedOpenSpawnBoundaryFact* Fact =
      FindPreparedOpenSpawnBoundaryFact(AgentId);
    if (!Fact || !Fact->bPendingBoundaryReset) return false;
  }
  return FCrowdDemoOpenSpawnRelaxationKernel::ConsumePendingBoundaryResets(
    AgentIds, OpenSpawnRelaxationRuntime);
}

bool UCrowdDemoRoundSimPipelineSubsystem::ArePreparedOpenSpawnBoundaryFactsCurrent() const
{
  if (!IsOpenSpawnRelaxation()) return true;
  if (PreparedOpenSpawnBoundaryFixedStepIndex != GetCurrentFixedStepIndex()) return false;
  TArray<int32> ExpectedAgentIds;
  ExpectedAgentIds.Reserve(BoundarySnapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
    ExpectedAgentIds.Add(Agent.Identity.AgentId);
  return FCrowdDemoOpenSpawnRelaxationKernel::ValidatePreparedBoundaryFacts(
    GetCurrentFixedStepIndex(), ExpectedAgentIds, PreparedOpenSpawnBoundaryFacts);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordOpenSpawnRelaxationParticleStep(
  TConstArrayView<FCrowdDemoParticleSoftPairInfluence> Influences,
  const float MaxActualCorrectionCm,
  const float SoftErrorCmP95)
{
  if (!IsOpenSpawnRelaxation())
    return;
  FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
    GetCurrentFixedStepIndex(), Influences, MaxActualCorrectionCm,
    SoftErrorCmP95, OpenSpawnRelaxationRuntime);
}

void UCrowdDemoRoundSimPipelineSubsystem::SetCapabilityCohorts(
  TArray<FCrowdDemoCapabilityCohort>&& Cohorts,
  const FCrowdDemoCapabilityProfileSummary& Summary)
{
  TargetRegionCapabilityCohorts.Reset(Cohorts.Num());
  Cohorts.Sort([](const auto& A, const auto& B)
  {
    return A.CapabilityProfileKey < B.CapabilityProfileKey;
  });
  TArray<FCrowdDemoCapabilityDemandPhase> Phases;
  uint32 PhaseHash = 0;
  const bool bPhasesValid =
    FCrowdDemoCapabilityProfileKernel::BuildDemandRegionPhaseOffsets(
      Cohorts, GetRules().TargetRegionTransportSettings.DemandRegionCount,
      Phases, PhaseHash);
  for (FCrowdDemoCapabilityCohort& Cohort : Cohorts)
  {
    FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime =
      TargetRegionCapabilityCohorts.AddDefaulted_GetRef();
    Runtime.Cohort = MoveTemp(Cohort);
    if (bPhasesValid)
    {
      const FCrowdDemoCapabilityDemandPhase* Phase = Phases.FindByPredicate(
        [&Runtime](const FCrowdDemoCapabilityDemandPhase& Candidate)
        {
          return Candidate.CapabilityProfileKey == Runtime.Cohort.CapabilityProfileKey;
        });
      Runtime.DemandRegionPhaseOffset = Phase
        ? Phase->DemandRegionPhaseOffset : 0;
    }
  }
  CapabilityProfileSummary = Summary;
  if (!bPhasesValid)
  {
    CapabilityProfileSummary.bValid = false;
    ++CapabilityProfileSummary.InvalidProfileCount;
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoCapabilityDemandPhaseInvalid cohorts=%d regions=%d hash=%u"),
      Cohorts.Num(), GetRules().TargetRegionTransportSettings.DemandRegionCount, PhaseHash);
  }
  ++CapabilityCohortRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeTargetRegionPlanLifecycleDiagnostic()
{
  if (!IsTargetRegionPlanLifecycleDiagnosticEnabled())
  {
    TargetRegionPlanLifecycleSummary = {};
    TargetRegionPlanLifecycleFixture = {};
    return;
  }
  uint32 MissingCohortKey = 0;
  int32 MissingRegionKey = INDEX_NONE;
  bool bFoundMissing = false;
  TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime> Runtimes;
  int32 ExpectedRebuildCount = 0;
  Runtimes.Reserve(TargetRegionCapabilityCohorts.Num());
  for (const FCrowdDemoTargetRegionCapabilityCohortRuntime& Runtime
    : TargetRegionCapabilityCohorts)
  {
    Runtimes.Add(Runtime.PlanLifecycle);
    ExpectedRebuildCount += Runtime.PlanRebuildCount;
    if (!bFoundMissing)
    {
      for (const FCrowdDemoTargetDemandRegion& Region : Runtime.Demand.Regions)
      {
        if (Region.bFeasible
          && Region.CurrentPopulation < Region.DesiredPopulation)
        {
          MissingCohortKey = Runtime.Cohort.CapabilityProfileKey;
          MissingRegionKey = Region.StableRegionKey;
          bFoundMissing = true;
          break;
        }
      }
    }
  }
  TargetRegionPlanLifecycleSummary =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
      Runtimes, MissingCohortKey, MissingRegionKey,
      TargetRegionPlanLifecycleFixture);
  TargetRegionPlanLifecycleSummary.bValid =
    TargetRegionPlanLifecycleSummary.bValid
    && TargetRegionPlanLifecycleSummary.RebuildCount == ExpectedRebuildCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionTopologyStep()
{
  TargetRegionTopologyRoundHash = FoldHash(
    FoldHash(TargetRegionTopologyRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    PreparedTargetRegionTopology.TopologyHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionDemandStep()
{
  TargetRegionDemandRoundHash = FoldHash(
    FoldHash(TargetRegionDemandRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    PreparedTargetRegionDemand.DemandHash);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionTransportStep(
  const float SolverMilliseconds, const int32 RebuildReason)
{
  TargetRegionTransportRoundHash = FoldHash(
    FoldHash(TargetRegionTransportRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    FoldHash(PreparedTargetRegionPlan.TransportHash,
      TargetRegionQuotaExecution.ExecutionHash));
  if (RebuildReason != 0)
  {
    ++TargetRegionPlanRebuildCount;
    switch (RebuildReason)
    {
      case 1: ++TargetRegionLifetimeRebuildCount; break;
      case 2: ++TargetRegionTargetRebuildCount; break;
      case 3: ++TargetRegionEnvironmentRebuildCount; break;
      case 4: ++TargetRegionMembershipRebuildCount; break;
      case 5: ++TargetRegionDemandSatisfiedRebuildCount; break;
      case 6: ++TargetRegionPathInvalidRebuildCount; break;
      default: break;
    }
    TargetRegionSolverMillisecondsSamples.Add(FMath::Max(0.0f, SolverMilliseconds));
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionGuidanceStep()
{
  TargetRegionGuidanceRoundHash = FoldHash(
    FoldHash(TargetRegionGuidanceRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    TargetRegionGuidanceSummary.GuidanceHash);
  if (!TargetRegionGuidanceSummary.bValid)
  {
    bTargetRegionRoundValid = false;
    if (TargetRegionLastInvalidStep != GetCurrentFixedStepIndex())
    {
      TargetRegionLastInvalidStep = GetCurrentFixedStepIndex();
      ++TargetRegionInvalidStepCount;
    }
    ++TargetRegionGuidanceUnroutedStepCount;
    TargetRegionGuidanceUnroutedAgentSampleCount += TargetRegionGuidanceSummary.UnroutedAgentCount;
    TargetRegionGuidanceUnroutedAgentMax = FMath::Max(
      TargetRegionGuidanceUnroutedAgentMax, TargetRegionGuidanceSummary.UnroutedAgentCount);
    if (TargetRegionGuidanceFirstFailureStep == INDEX_NONE)
    {
      TargetRegionGuidanceFirstFailureStep = GetCurrentFixedStepIndex();
      TargetRegionGuidanceFirstFailureAgentId = TargetRegionGuidanceSummary.FirstUnroutedAgentId;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetRegionValidationStep()
{
  TargetRegionValidationRoundHash = FoldHash(
    FoldHash(TargetRegionValidationRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    TargetRegionPlanValidation.ValidationHash);
  if (!TargetRegionPlanValidation.bValid)
  {
    bTargetRegionRoundValid = false;
    if (TargetRegionLastInvalidStep != GetCurrentFixedStepIndex())
    {
      TargetRegionLastInvalidStep = GetCurrentFixedStepIndex();
      ++TargetRegionInvalidStepCount;
    }
    ++TargetRegionValidationFailureCount;
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::PinTargetRegionFailureFixture(
  const int32 Kind, const int32 AgentId, const int32 CellKey, const uint32 FixtureHash)
{
  if (bTargetRegionFailureFixtureValid || FixtureHash == 0) return;
  bTargetRegionFailureFixtureValid = true;
  TargetRegionFailureFixtureStep = GetCurrentFixedStepIndex();
  TargetRegionFailureFixtureKind = Kind;
  TargetRegionFailureFixtureAgentId = AgentId;
  TargetRegionFailureFixtureCellKey = CellKey;
  TargetRegionFailureFixtureHash = FixtureHash;
}

float UCrowdDemoRoundSimPipelineSubsystem::GetTargetRegionSolverMsP95() const
{
  return Percentile(TargetRegionSolverMillisecondsSamples, 0.95f);
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsTargetStabilityDiagnosticEnabled() const
{
  return bTargetStabilityDiagnosticPlanEnabled && IsActive();
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetStabilityStep(
  const FCrowdDemoTargetStabilityStepSample& Step)
{
  if (!IsTargetStabilityDiagnosticEnabled()) return;
  FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(Step, TargetStabilityRuntime);
  TargetStabilitySummary = {};
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeTargetStabilityDiagnostic()
{
  if (!IsTargetStabilityDiagnosticEnabled()) return;
  FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(
    TargetStabilityRuntime, TargetStabilitySummary);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackOutcome(
  const bool bHit,
  const bool bAgentMismatch,
  const int32 ReplayedSteps)
{
  SoftPressureRollbackSnapshotHitCount += bHit ? 1 : 0;
  SoftPressureRollbackSnapshotMissCount += bHit ? 0 : 1;
  SoftPressureRollbackAgentMismatchCount += bAgentMismatch ? 1 : 0;
  SoftPressureRollbackReplayedStepCount += FMath::Max(0, ReplayedSteps);
  const TCHAR* Role = GetWorld() && GetWorld()->GetNetMode() == NM_Client
    ? TEXT("client") : TEXT("server");
  if (bHit && !bAgentMismatch)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoRoundRollback role=%s round_id=%d hit=1 miss=0 mismatch=0 replayed_steps=%d totals=%d,%d,%d,%d"),
      Role, GetCurrentRoundId(), FMath::Max(0, ReplayedSteps),
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
  else
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoRoundRollback role=%s round_id=%d hit=0 miss=1 mismatch=%d replayed_steps=0 totals=%d,%d,%d,%d VIOLATION"),
      Role, GetCurrentRoundId(), bAgentMismatch ? 1 : 0,
      SoftPressureRollbackSnapshotHitCount, SoftPressureRollbackSnapshotMissCount,
      SoftPressureRollbackAgentMismatchCount, SoftPressureRollbackReplayedStepCount);
  }
}



void UCrowdDemoRoundSimPipelineSubsystem::RecordFlowAgentSamples(
  const TConstArrayView<FCrowdDemoRoundFlowAgentSample> Samples,
  const bool bClient)
{
  int32 UnreachableCount = 0;
  int32 PenetrationCount = 0;
  const FVector Goal = FVector(GetRules().FlowFieldConfig.GoalLocation);
  for (const FCrowdDemoRoundFlowAgentSample& Sample : Samples)
  {
    UnreachableCount += Sample.bUnreachable ? 1 : 0;
    PenetrationCount += Sample.bPenetrating ? 1 : 0;
    const bool bInsideCorridorZone = Sample.Location.Y > -2050.0f
      && Sample.Location.Y < -650.0f;
    float& LowSpeedSeconds = FlowLowSpeedSecondsByAgentId.FindOrAdd(Sample.AgentId);
    if (bInsideCorridorZone && Sample.Velocity.Size2D() < 10.0f)
    {
      LowSpeedSeconds += GetCurrentFixedStepSeconds();
      if (LowSpeedSeconds > 1.5f)
      {
        FlowCorridorDeadlockAgentIds.Add(Sample.AgentId);
      }
    }
    else
    {
      LowSpeedSeconds = 0.0f;
    }
    if (FVector::DistSquared2D(Sample.Location, Goal) <= FMath::Square(140.0f))
    {
      FlowGoalReachedAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -1950.0f)
    {
      FlowWallPassAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > -650.0f)
    {
      FlowCorridorExitAgentIds.Add(Sample.AgentId);
    }
    if (Sample.Location.Y > 750.0f)
    {
      FlowTurnExitAgentIds.Add(Sample.AgentId);
    }
  }
  LastCompareMetrics.FlowUnreachableAgentCount = UnreachableCount;
  LastCompareMetrics.FlowGoalReachedCount = FlowGoalReachedAgentIds.Num();
  LastCompareMetrics.FlowWallPassCount = FlowWallPassAgentIds.Num();
  LastCompareMetrics.FlowCorridorExitCount = FlowCorridorExitAgentIds.Num();
  LastCompareMetrics.FlowTurnExitCount = FlowTurnExitAgentIds.Num();
  LastCompareMetrics.CorridorDeadlockAgentCount = FlowCorridorDeadlockAgentIds.Num();
  if (bClient)
  {
    LastCompareMetrics.ClientSimObstaclePenetrationCount += PenetrationCount;
  }
  else
  {
    LastCompareMetrics.ServerObstaclePenetrationCount += PenetrationCount;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::TryBeginFixedStep(const float TargetServerTimeSeconds)
{
  if (!bPlanActive || bStepInProgress)
  {
    return false;
  }
  const float RoundEnd = ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds;
  const float RemainingRoundSeconds =
    RoundEnd - SimulatedServerTimeSeconds;
  if (RemainingRoundSeconds <= KINDA_SMALL_NUMBER)
  {
    return false;
  }
  // A replicated SimulationTick is the indivisible simulation clock. Never
  // submit a clipped tail step: accumulated float error can otherwise create
  // a second intent for the same absolute Tick, leaving work queued while the
  // Runtime correctly refuses to advance another epoch. Snap only the final
  // sub-quantum remainder to the declared Round end.
  if (RemainingRoundSeconds
      < CurrentFixedStepSeconds - KINDA_SMALL_NUMBER)
  {
    SimulatedServerTimeSeconds = RoundEnd;
    return false;
  }
  float StepEnd =
    SimulatedServerTimeSeconds + CurrentFixedStepSeconds;
  const float TerminalRemainderSeconds = RoundEnd - StepEnd;
  const float TerminalSnapToleranceSeconds = FMath::Max(
    KINDA_SMALL_NUMBER, CurrentFixedStepSeconds * 0.05f);
  if (TerminalRemainderSeconds > 0.0f
    && TerminalRemainderSeconds <= TerminalSnapToleranceSeconds)
  {
    // Checkpoint publication runs before FinishFixedStep. Absorb only float
    // accumulation noise into the final boundary timestamp so that boundary
    // can publish its Round result; the simulated delta remains one quantum.
    StepEnd = RoundEnd;
  }
  if (StepEnd > TargetServerTimeSeconds + KINDA_SMALL_NUMBER)
  {
    return false;
  }
  CurrentStepStartServerTimeSeconds = SimulatedServerTimeSeconds;
  CurrentStepEndServerTimeSeconds = StepEnd;
  CurrentStepMassAccessCounts = {};
  bCurrentStepUsedWorkerProxySnapshot = false;
  bCurrentStepUsedBootstrapBoundarySnapshot = false;
  bCurrentStepWorkerDirtyMassApplied = false;
  CurrentStepWorkerDirtyMassPublishSequence = 0;
  CurrentStepWorkerDirtyMassEntityCount = 0;
  CurrentStepMassDirtyEntityRefs.Reset();
  bStepInProgress = true;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
TryRecordCanonicalGatherRead()
{
  if (!CurrentStepMassAccessCounts.
      TryRecordCanonicalGatherRead(bStepInProgress))
  {
    ++MassAccessContractViolationCount;
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
TryRecordBootstrapMassRead()
{
  if (bStepInProgress || !bPlanActive
    || LastBootstrapMassReadPlanRevision == GetCurrentPlanRevision())
  {
    ++MassAccessContractViolationCount;
    return false;
  }
  LastBootstrapMassReadPlanRevision = GetCurrentPlanRevision();
  ++BootstrapMassReadCount;
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
TryBeginAtomicCommitWrite()
{
  const bool bAutonomousCommitAllowed =
    (bCurrentStepUsedWorkerProxySnapshot
      || bCurrentStepUsedBootstrapBoundarySnapshot)
    && bStepInProgress
    && CurrentStepMassAccessCounts.CanonicalGatherReadCount == 0
    && CurrentStepMassAccessCounts.IntermediateReadCount == 0
    && CurrentStepMassAccessCounts.CommitWriteCount == 0;
  if (bAutonomousCommitAllowed)
  {
    ++CurrentStepMassAccessCounts.CommitWriteCount;
    return true;
  }
  if (!CurrentStepMassAccessCounts.TryBeginAtomicCommitWrite(
      bStepInProgress))
  {
    ++MassAccessContractViolationCount;
    return false;
  }
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordAuthorityMassWrite()
{
  ++AuthorityMassWriteCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordDirtyMassApply(
  const int32 EntityCount)
{
  ++DirtyMassApplyBatchCount;
  DirtyMassApplyEntityCount += static_cast<uint64>(FMath::Max(0, EntityCount));
  if (DirtyMassApplyBatchCount == 1
    || DirtyMassApplyBatchCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoDirtyMassApplyCheckpoint step=%d batches=%llu last_entities=%d total_entities=%llu stable_entities=%d source=WorkerResultApply"),
      GetCurrentFixedStepIndex(), DirtyMassApplyBatchCount, EntityCount,
      DirtyMassApplyEntityCount, BoundarySnapshot.Agents.Num());
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::FinishFixedStep()
{
  if (bStepInProgress)
  {
    const bool bMassAccessSatisfied =
      (bCurrentStepUsedWorkerProxySnapshot
        || bCurrentStepUsedBootstrapBoundarySnapshot)
        ? CurrentStepMassAccessCounts.CanonicalGatherReadCount == 0
          && CurrentStepMassAccessCounts.IntermediateReadCount == 0
          && CurrentStepMassAccessCounts.CommitWriteCount == 1
        : CurrentStepMassAccessCounts.IsOrdinaryStepContractSatisfied();
    if (!bMassAccessSatisfied)
    {
      ++MassAccessContractViolationCount;
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoMassAccessContract gather_read=%u intermediate_read=%u commit_write=%u step=%d violation_count=%llu"),
        CurrentStepMassAccessCounts.CanonicalGatherReadCount,
        CurrentStepMassAccessCounts.IntermediateReadCount,
        CurrentStepMassAccessCounts.CommitWriteCount,
        GetCurrentFixedStepIndex(),
        MassAccessContractViolationCount);
      return;
    }
    SimulatedServerTimeSeconds = CurrentStepEndServerTimeSeconds;
    ++PlanApplyBoundarySequence;
    bStepInProgress = false;
    CurrentBoundaryRequestStartSeconds = 0.0;
    PreparedMovementBoundaryCommit = {};
    PreparedCombatBoundaryCommit = {};
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::FailFixedStep()
{
  if (BoundaryOrchestrator.IsValid())
  {
    BoundaryOrchestrator->Fail();
    LastBoundaryTransactionResult = BoundaryOrchestrator->BuildResult();
  }
  ++BoundaryGeneration;
  if (BoundaryGeneration == 0)
    BoundaryGeneration = 1;
  bStepInProgress = false;
  bPlanActive = false;
  CurrentBoundaryRequestStartSeconds = 0.0;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  PendingWorkerV2MovementExpectations.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
  PreparedMovementBoundaryCommit = {};
  PreparedCombatBoundaryCommit = {};
}

void UCrowdDemoRoundSimPipelineSubsystem::
InvalidateInFlightBoundaryForAuthoritativeState()
{
  check(IsInGameThread());
  const bool bDiscardedInFlight =
    bStepInProgress && BoundaryOrchestrator.IsValid();
  ++BoundaryGeneration;
  if (BoundaryGeneration == 0)
    BoundaryGeneration = 1;
  if (bDiscardedInFlight)
    ++BoundaryStaleResultCount;

  // Worker closures own only immutable snapshots and thread-safe work state,
  // so dropping the GT mailbox does not cancel or dereference their work.
  // Their generation is no longer reachable from the current plan and their
  // eventual result therefore cannot be committed.
  bStepInProgress = false;
  CurrentBoundaryRequestStartSeconds = 0.0;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryOrchestrator.Reset();
  BoundaryFacingWorkState.Reset();
  PendingWorkerV2MovementExpectations.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
  PreparedTargetResourceSlots.Reset();
  PreparedMovementBoundaryCommit = {};
  PreparedCombatBoundaryCommit = {};
  PreparedRuntimeSharedFlowOutputs.Reset();
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
  PreparedRuntimeComposedGuidance.Reset();
  PreparedRuntimePredictedMovements.Reset();
  PreparedRuntimeParticleResults.Reset();
  PreparedRuntimeFinalKinematics.Reset();
  bPreparedRuntimeFinalKinematicsWorkerOwned = false;
  PreparedRuntimeFacingResults.Reset();
  PreparedFacingRollbackFacts.Reset();
  PreparedParticleDiagnosticCommit = {};
  PreparedOpenSpawnBoundaryFacts.Reset();
  PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;

  if (bDiscardedInFlight)
  {
    UE_LOG(LogTemp, Verbose,
      TEXT("CrowdDemoBoundaryAuthoritativeInvalidation generation=%llu stale_count=%d source=MassPipeline"),
      static_cast<unsigned long long>(BoundaryGeneration),
      BoundaryStaleResultCount);
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsRoundSimScenarioActive() const
{
  return bPlanActive && IsCrowdDemoRoundSimScenario(ActivePlan.Rules.Scenario);
}

bool UCrowdDemoRoundSimPipelineSubsystem::SetPreparedParticleDiagnosticCommit(
  FCrowdDemoPreparedParticleDiagnosticCommit&& Commit)
{
  if (!Commit.bValid
    || Commit.FixedStepIndex != GetCurrentFixedStepIndex()
    || Commit.PlanRevision != GetCurrentPlanRevision()
    || Commit.AgentCount != BoundarySnapshot.Agents.Num()
    || PreparedParticleDiagnosticCommit.bValid)
    return false;
  PreparedParticleDiagnosticCommit = MoveTemp(Commit);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::CommitPreparedParticleDiagnostics()
{
  if (!IsMovementFinalizeAppliedCurrent()
    || !IsPreparedParticleDiagnosticCommitCurrent())
    return false;

  FCrowdDemoPreparedParticleDiagnosticCommit Commit =
    MoveTemp(PreparedParticleDiagnosticCommit);
  PreparedParticleDiagnosticCommit = {};
  if (Commit.bRecordStabilityStep)
    RecordTargetStabilityStep(Commit.StabilityStep);
  if (Commit.bFinalizeStabilityDiagnostic)
    FinalizeTargetStabilityDiagnostic();
  if (Commit.bRecordCrossProfileViolations)
    RecordCrossProfileParticleViolations(
      Commit.CrossProfileHardViolationCount,
      Commit.CrossProfileSweptViolationCount);
  if (Commit.bRecordRouteStep)
    RecordSoftPressureRouteStep(Commit.RouteSamples);
  if (Commit.bFinalizeRouteDiagnostic)
    FinalizeSoftPressureRouteDiagnostic(Commit.RouteCounterfactual);
  if (Commit.bRecordOpenSpawnStep)
    RecordOpenSpawnRelaxationParticleStep(
      Commit.OpenSpawnSoftPairInfluences,
      Commit.OpenSpawnMaxAgentCorrectionCm,
      Commit.OpenSpawnSoftErrorCmP95);
  if (Commit.bRecordFailureFixture)
    RecordParticleFailureFixture(Commit.FailureFixture);
  RecordParticleConstraintSummary(
    Commit.CandidateSummary, Commit.AppliedSummary,
    Commit.AppliedStateHash, !Commit.CandidateSummary.bValid,
    Commit.SolverMilliseconds);
  return true;
}


void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleConstraintSummary(
  const FCrowdDemoParticleConstraintSummary& CandidateSummary,
  const FCrowdDemoParticleConstraintSummary& AppliedSummary,
  const uint32 AppliedStateHash,
  const bool bGlobalFallback,
  const float SolverMilliseconds)
{
  if (!IsActive() || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure)
    return;
  LastParticleCandidateSummary = CandidateSummary;
  LastParticleAppliedSummary = AppliedSummary;
  ParticleSolverMillisecondsSamples.Add(FMath::Max(0.0f, SolverMilliseconds));
  ParticleCandidateStateHash = CandidateSummary.CandidateHash;
  ParticleAppliedStateHash = AppliedStateHash;
  if (!CandidateSummary.bValid || !AppliedSummary.bValid)
  {
    ++ParticleInvalidStepCount;
    if (!bParticleConstraintRunFailure)
    {
      bParticleConstraintRunFailure = true;
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoParticleInvalid role=%s round_id=%d fixed_step=%d hard=%d swept=%d obstacle=%d bounds=%d candidate_hash=%u applied_hash=%u action=early_round_failure VIOLATION"),
        GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        GetCurrentRoundId(), GetCurrentFixedStepIndex(),
        CandidateSummary.HardPairViolationCount + AppliedSummary.HardPairViolationCount,
        CandidateSummary.SweptPairViolationCount + AppliedSummary.SweptPairViolationCount,
        CandidateSummary.ObstaclePenetrationCount + AppliedSummary.ObstaclePenetrationCount,
        CandidateSummary.BoundsViolationCount + AppliedSummary.BoundsViolationCount,
        CandidateSummary.CandidateHash, AppliedStateHash);
    }
  }
  if (bGlobalFallback) ++ParticleGlobalFallbackStepCount;

  FCrowdDemoParticleSettlingTracker SettlingTracker;
  SettlingTracker.StepCount = ParticleStepCount;
  SettlingTracker.ConsecutiveSettledSampleCount = ParticleSettlingWindowCount;
  SettlingTracker.SettlingSteps = ParticleSettlingSteps;
  SettlingTracker.PreviousSoftErrorCmP95 = ParticlePreviousSoftErrorP95;
  FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
    SettlingTracker, AppliedSummary.MaxAgentCorrectionCm, AppliedSummary.SoftErrorCmP95);
  ParticleStepCount = SettlingTracker.StepCount;
  ParticleSettlingWindowCount = SettlingTracker.ConsecutiveSettledSampleCount;
  ParticleSettlingSteps = SettlingTracker.SettlingSteps;
  ParticlePreviousSoftErrorP95 = SettlingTracker.PreviousSoftErrorCmP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordLocalPredictiveStep(
  TArray<FCrowdDemoLocalPredictiveResult>&& Results,
  TArray<FCrowdDemoLocalPredictiveGrantState>&& GrantStates,
  const FCrowdDemoLocalPredictiveSummary& Summary)
{
  if (!IsActive()
    || GetRules().Scenario != ECrowdDemoScenario::SimRoundSoftPressure
    || GetRules().LocalPredictiveSettings.bEnabled == 0)
    return;

  Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  GrantStates.Sort([](const auto& A, const auto& B)
  {
    return A.ComponentKey != B.ComponentKey
      ? A.ComponentKey < B.ComponentKey
      : A.GrantedAgentId < B.GrantedAgentId;
  });
  PreparedLocalPredictiveResults = MoveTemp(Results);
  LocalPredictiveGrantStates = MoveTemp(GrantStates);
  LastLocalPredictiveSummary = Summary;
  LocalPredictiveRoundHash = FoldHash(
    FoldHash(LocalPredictiveRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    Summary.CandidateHash);
  ++LocalPredictiveSampleCount;
  if (!Summary.bValid)
  {
    const bool bFirstInvalidStep = LocalPredictiveInvalidStepCount == 0;
    ++LocalPredictiveInvalidStepCount;
    if (bFirstInvalidStep)
    {
      const FCrowdDemoLocalPredictiveResult* FirstInvalid =
        PreparedLocalPredictiveResults.FindByPredicate(
          [](const FCrowdDemoLocalPredictiveResult& Result)
          {
            return !Result.bValid;
          });
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoLocalPredictiveInvalid role=%s round_id=%d fixed_step=%d infeasible=%d quantization=%d joint=%d first_agent=%d neighbors=%d constraints=%d component=%u blocked_age=%d hash=%u VIOLATION"),
        GetWorld() && GetWorld()->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
        GetCurrentRoundId(), GetCurrentFixedStepIndex(), Summary.InfeasibleAgentCount,
        Summary.QuantizationFailureCount, Summary.JointValidationFailureCount,
        FirstInvalid ? FirstInvalid->AgentId : INDEX_NONE,
        FirstInvalid ? FirstInvalid->NeighborCount : 0,
        FirstInvalid ? FirstInvalid->ConstraintCount : 0,
        FirstInvalid ? FirstInvalid->ComponentKey : 0,
        FirstInvalid ? FirstInvalid->NextBlockedAgeSteps : 0,
        Summary.CandidateHash);
      if (FirstInvalid)
      {
        const TArray<int32> WitnessAgentIds = {FirstInvalid->AgentId};
        FCrowdDemoLocalPredictiveComponentFixture FailureFixture;
        if (BuildCurrentLocalPredictiveComponentFixture(
          WitnessAgentIds, FailureFixture))
        {
          const FCrowdDemoLocalPredictiveAgent* InputAgent =
            FailureFixture.Agents.FindByPredicate(
              [FirstInvalid](const FCrowdDemoLocalPredictiveAgent& Agent)
              {
                return Agent.AgentId == FirstInvalid->AgentId;
              });
          const FCrowdDemoLocalPredictiveResult* InitialResult =
            FailureFixture.Trace.InitialIndependentResults.FindByPredicate(
              [FirstInvalid](const FCrowdDemoLocalPredictiveResult& Result)
              {
                return Result.AgentId == FirstInvalid->AgentId;
              });
          const FCrowdDemoLocalPredictiveResult* CompletedResult =
            FailureFixture.Trace.CompletedIndependentResults.FindByPredicate(
              [FirstInvalid](const FCrowdDemoLocalPredictiveResult& Result)
              {
                return Result.AgentId == FirstInvalid->AgentId;
              });
          const FCrowdDemoLocalPredictiveComponentTrace* Component =
            FailureFixture.Trace.Components.FindByPredicate(
              [FirstInvalid](const FCrowdDemoLocalPredictiveComponentTrace& Value)
              {
                return Value.AgentIds.Contains(FirstInvalid->AgentId);
              });
          UE_LOG(LogTemp, Display,
            TEXT("CrowdDemoLocalPredictiveFailureFixture fixed_step=%d agent=%d fixture_agents=%d fixture_pairs=%d component_agents=%d input_pos=(%.3f,%.3f) input_velocity=(%.3f,%.3f) preferred=(%.3f,%.3f) radius=%.3f gap=%.3f max_speed=%.3f initial_valid=%d initial_velocity=(%.3f,%.3f) completed_valid=%d completed_velocity=(%.3f,%.3f) final_velocity=(%.3f,%.3f) common_valid=%d full_joint_safe=%d coherent=%d preferred_recovery=%d safe_alpha_q15=%d hash=%u"),
            FailureFixture.FixedStepIndex, FirstInvalid->AgentId,
            FailureFixture.Agents.Num(), FailureFixture.ConflictPairs.Num(),
            Component ? Component->AgentIds.Num() : 0,
            InputAgent ? InputAgent->Position.X : 0.0f,
            InputAgent ? InputAgent->Position.Y : 0.0f,
            InputAgent ? InputAgent->Velocity.X : 0.0f,
            InputAgent ? InputAgent->Velocity.Y : 0.0f,
            InputAgent ? InputAgent->PreferredVelocity.X : 0.0f,
            InputAgent ? InputAgent->PreferredVelocity.Y : 0.0f,
            InputAgent ? InputAgent->PhysicalRadiusCm : 0.0f,
            InputAgent ? InputAgent->HardSafetyGapCm : 0.0f,
            InputAgent ? InputAgent->MaxSpeedCmps : 0.0f,
            InitialResult && InitialResult->bValid ? 1 : 0,
            InitialResult ? InitialResult->Velocity.X : 0.0f,
            InitialResult ? InitialResult->Velocity.Y : 0.0f,
            CompletedResult && CompletedResult->bValid ? 1 : 0,
            CompletedResult ? CompletedResult->Velocity.X : 0.0f,
            CompletedResult ? CompletedResult->Velocity.Y : 0.0f,
            FirstInvalid->Velocity.X, FirstInvalid->Velocity.Y,
            Component && Component->bCommonVelocityValid ? 1 : 0,
            Component && Component->bFullJointVelocitySafe ? 1 : 0,
            Component && Component->bCoherentTranslationApplied ? 1 : 0,
            Component && Component->bJointPreferredRecoveryApplied ? 1 : 0,
            Component ? Component->SafeAlphaQ15 : 0,
            FailureFixture.StableHash);
          LocalPredictiveComponentFixture = MoveTemp(FailureFixture);
        }
        else
        {
          UE_LOG(LogTemp, Error,
            TEXT("CrowdDemoLocalPredictiveFailureFixture fixed_step=%d agent=%d built=0 VIOLATION"),
            GetCurrentFixedStepIndex(), FirstInvalid->AgentId);
        }
      }
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordGuidanceComposeStep(
  TArray<FCrowdDemoComposedGuidance>&& Results)
{
  if (!IsActive()) return;
  Results.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 CandidateStepHash = 2166136261u;
  uint32 ComposeStepHash = 2166136261u;
  for (const FCrowdDemoComposedGuidance& Result : Results)
  {
    CandidateStepHash = FoldHash(CandidateStepHash, Result.CandidateSetHash);
    ComposeStepHash = FoldHash(ComposeStepHash, Result.StableHash);
  }
  GuidanceCandidateRoundHash = FoldHash(
    FoldHash(GuidanceCandidateRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    CandidateStepHash);
  GuidanceComposeRoundHash = FoldHash(
    FoldHash(GuidanceComposeRoundHash, static_cast<uint32>(GetCurrentFixedStepIndex())),
    ComposeStepHash);
  ++GuidanceComposeSampleCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordLocalPredictiveDiagnosticFrame(
  FCrowdDemoLocalPredictiveDiagnosticFrame&& Frame)
{
  if (!IsTargetStabilityDiagnosticEnabled() && Frame.Summary.bValid) return;
  LocalPredictiveDiagnosticFrame = MoveTemp(Frame);
}

bool UCrowdDemoRoundSimPipelineSubsystem::BuildCurrentLocalPredictiveComponentFixture(
  const TConstArrayView<int32> WitnessAgentIds,
  FCrowdDemoLocalPredictiveComponentFixture& OutFixture) const
{
  const FCrowdDemoLocalPredictiveDiagnosticFrame& Frame =
    LocalPredictiveDiagnosticFrame;
  return FCrowdDemoLocalPredictiveInteractionKernel::BuildComponentFixture(
    Frame.FixedStepIndex, Frame.Agents, GetRules().FlowFieldConfig, Frame.Settings,
    Frame.PreviousGrantStates, Frame.ConflictPairs, Frame.GrantStates,
    Frame.Results, Frame.Summary, Frame.Trace, WitnessAgentIds, OutFixture);
}

bool UCrowdDemoRoundSimPipelineSubsystem::BuildProjectileSnapshot(
  TArray<FCrowdProjectileState>& OutProjectiles) const
{
  const UWorld* World = GetWorld();
  if (!World)
  {
    OutProjectiles.Reset();
    return true;
  }
  const UCrowdDemoMassSubsystem* MassSubsystem = World
    ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  return MassSubsystem
    && MassSubsystem->GatherProjectileStates(OutProjectiles);
}

bool UCrowdDemoRoundSimPipelineSubsystem::PrepareProjectileFinalApply(
  const int32 RequiredActiveCount)
{
  UWorld* World = GetWorld();
  if (!World)
    return RequiredActiveCount == 0;
  UCrowdDemoMassSubsystem* MassSubsystem = World
    ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  return MassSubsystem
    && MassSubsystem->PrepareProjectileCapacity(RequiredActiveCount);
}

void UCrowdDemoRoundSimPipelineSubsystem::ApplyProjectileFinalState(
  const TConstArrayView<FCrowdProjectileState> Projectiles)
{
  UWorld* World = GetWorld();
  UCrowdDemoMassSubsystem* MassSubsystem = World
    ? World->GetSubsystem<UCrowdDemoMassSubsystem>() : nullptr;
  if (!MassSubsystem && Projectiles.IsEmpty())
    return;
  check(MassSubsystem);
  MassSubsystem->ApplyProjectileStates(Projectiles);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordProjectileStep(
  const FCrowdDemoProjectileStepSummary& Summary,
  const TConstArrayView<FCrowdDemoProjectileVisualEvent> Events)
{
  if (!IsRangedProjectileCombat())
    return;
  ProjectileMetrics.bValid = ProjectileMetrics.bValid != 0 && Summary.bValid ? 1 : 0;
  ProjectileMetrics.TargetAcquiredCount += Summary.TargetAcquiredCount;
  ProjectileMetrics.CompletedWindupCount += Summary.CompletedWindupCount;
  ProjectileMetrics.ProjectileSpawnedCount += Summary.SpawnedCount;
  ProjectileMetrics.ProjectileActiveCount = Summary.ActiveCount;
  ProjectileMetrics.ProjectileImpactedCount += Summary.ImpactedCount;
  ProjectileMetrics.ProjectileExpiredCount += Summary.ExpiredCount;
  ProjectileMetrics.DuplicateFireCount += Summary.DuplicateFireCount;
  ProjectileMetrics.DuplicateHitCount += Summary.DuplicateHitCount;
  ProjectileMetrics.InvalidTargetLifecycleCount += Summary.InvalidTargetLifecycleCount;
  ProjectileMetrics.InvalidProjectileCount += Summary.InvalidProjectileCount;
  const uint32 Step = static_cast<uint32>(GetCurrentFixedStepIndex());
  ProjectileMetrics.AttackStateHash = FoldHash(
    FoldHash(ProjectileMetrics.AttackStateHash, Step), Summary.AttackStateHash);
  ProjectileMetrics.ProjectileStateHash = FoldHash(
    FoldHash(ProjectileMetrics.ProjectileStateHash, Step), Summary.ProjectileStateHash);
  ProjectileMetrics.EventHash = FoldHash(
    FoldHash(ProjectileMetrics.EventHash, Step), Summary.EventHash);
  for (const FCrowdDemoProjectileVisualEvent& Event : Events)
  {
    switch (Event.Kind)
    {
      case ECrowdDemoProjectileVisualEventKind::Spawn:
        ++ProjectileMetrics.VisualSpawnEventCount;
        break;
      case ECrowdDemoProjectileVisualEventKind::Impact:
        ++ProjectileMetrics.VisualImpactEventCount;
        break;
      case ECrowdDemoProjectileVisualEventKind::Expire:
        ++ProjectileMetrics.VisualExpireEventCount;
        break;
      default:
        ProjectileMetrics.bValid = 0;
        break;
    }
  }
  if (GetWorld() && GetWorld()->GetNetMode() != NM_Client && !Events.IsEmpty())
    OutgoingProjectileVisualEvents.Append(Events.GetData(), Events.Num());
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordProjectileHitResponse(
  const FCrowdDemoHitResponseSummary& Summary)
{
  if (!IsRangedProjectileCombat())
    return;
  ProjectileMetrics.bValid = ProjectileMetrics.bValid != 0 && Summary.bValid ? 1 : 0;
  ProjectileMetrics.DuplicateHitCount += Summary.DuplicateHitCount;
  ProjectileMetrics.DamageAppliedCount += Summary.AppliedHitCount;
}

FCrowdDemoProjectileMetrics
UCrowdDemoRoundSimPipelineSubsystem::BuildProjectileMetrics() const
{
  FCrowdDemoProjectileMetrics Result = ProjectileMetrics;
  const bool bLifecycleConserved =
    Result.ProjectileSpawnedCount
      == Result.ProjectileActiveCount + Result.ProjectileImpactedCount
        + Result.ProjectileExpiredCount;
  const bool bEventsConserved =
    Result.VisualSpawnEventCount == Result.ProjectileSpawnedCount
    && Result.VisualImpactEventCount == Result.ProjectileImpactedCount
    && Result.VisualExpireEventCount == Result.ProjectileExpiredCount;
  Result.bValid = Result.bValid != 0
    && Result.CompletedWindupCount == Result.ProjectileSpawnedCount
    && bLifecycleConserved && bEventsConserved
    && Result.DuplicateFireCount == 0
    && Result.DuplicateHitCount == 0
    && Result.InvalidProjectileCount == 0 ? 1 : 0;
  return Result;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueProjectileVisualEvents(
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents)
{
  if (OutgoingProjectileVisualEvents.IsEmpty())
    return false;
  OutEvents = MoveTemp(OutgoingProjectileVisualEvents);
  OutgoingProjectileVisualEvents.Reset();
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordParticleFailureFixture(
  const FCrowdDemoParticleFailureFixture& Fixture)
{
  if (!ParticleFailureFixture.bValid && Fixture.bValid)
    ParticleFailureFixture = Fixture;
}

void UCrowdDemoRoundSimPipelineSubsystem::StopAfterParticleConstraintFailure()
{
  if (GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && bParticleConstraintRunFailure)
  {
    bPlanActive = false;
    bStepInProgress = false;
  }
}

float UCrowdDemoRoundSimPipelineSubsystem::GetParticleSolverMsP95() const
{
  return Percentile(ParticleSolverMillisecondsSamples, 0.95f);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPerformanceStage(
  const ECrowdDemoRoundPerformanceStage Stage,
  const float Milliseconds)
{
  const uint8 Index = static_cast<uint8>(Stage);
  if (Index < static_cast<uint8>(ECrowdDemoRoundPerformanceStage::Count)
    && Milliseconds >= 0.0f)
  {
    RoundPerformanceStageMsSamples[Index].Add(Milliseconds);
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetTopologyPerformance(
  const bool bBuilt)
{
  if (bBuilt) ++PerformanceTargetTopologyBuildCount;
  else ++PerformanceTargetTopologyCacheHitCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordTargetDemandPerformance(
  const bool bFullBuild)
{
  if (bFullBuild) ++PerformanceTargetDemandFullBuildCount;
  else ++PerformanceTargetDemandPopulationUpdateCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordFixedStepPerformance(
  const float Milliseconds)
{
  if (Milliseconds < 0.0f)
  {
    return;
  }
  FixedStepPipelineMsSamples.Add(Milliseconds);
  if (PendingRollbackReplaySteps > 0)
  {
    PendingRollbackReplayMilliseconds += Milliseconds;
    --PendingRollbackReplaySteps;
    if (PendingRollbackReplaySteps == 0)
    {
      RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
      PendingRollbackReplayMilliseconds = 0.0f;
    }
  }
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordPipelineFramePerformance(
  const int32 ExecutedSteps,
  const float TargetServerTimeSeconds,
  const bool bHitFixedStepLimit,
  const bool bHitCatchupCpuBudget)
{
  if (!IsActive())
  {
    return;
  }
  FixedStepsPerGameFrameSamples.Add(static_cast<float>(FMath::Max(0, ExecutedSteps)));
  if (ExecutedSteps > 1)
  {
    ++PerformanceCatchupFrameCount;
  }
  if (bHitFixedStepLimit)
  {
    ++PerformanceMaxFixedStepsPerFrameHitCount;
  }
  if (bHitCatchupCpuBudget)
  {
    ++PerformanceCatchupCpuBudgetHitCount;
    ++PerformanceCatchupCpuBudgetConsecutiveCount;
    PerformanceCatchupCpuBudgetConsecutiveMax = FMath::Max(
      PerformanceCatchupCpuBudgetConsecutiveMax,
      PerformanceCatchupCpuBudgetConsecutiveCount);
  }
  else
  {
    PerformanceCatchupCpuBudgetConsecutiveCount = 0;
  }
  const float BacklogMilliseconds =
    FMath::Max(
      0.0f, TargetServerTimeSeconds - GetScheduledServerTimeSeconds())
      * 1000.0f;
  FixedStepBacklogMsSamples.Add(BacklogMilliseconds);
  PerformanceFixedStepBacklogMsMax = FMath::Max(
    PerformanceFixedStepBacklogMsMax, BacklogMilliseconds);
}

void UCrowdDemoRoundSimPipelineSubsystem::BeginRollbackReplayPerformance(
  const int32 ReplayedSteps,
  const float ApplyMilliseconds,
  const bool bZeroErrorReplay)
{
  if (PendingRollbackReplaySteps > 0)
  {
    RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
  }
  PendingRollbackReplaySteps = FMath::Max(0, ReplayedSteps);
  PendingRollbackReplayMilliseconds = FMath::Max(0.0f, ApplyMilliseconds);
  if (bZeroErrorReplay)
  {
    ++PerformanceZeroErrorRollbackReplayCount;
  }
  if (PendingRollbackReplaySteps == 0)
  {
    RollbackReplayMsSamples.Add(PendingRollbackReplayMilliseconds);
    PendingRollbackReplayMilliseconds = 0.0f;
  }
}

FCrowdDemoRoundPerformanceMetrics
UCrowdDemoRoundSimPipelineSubsystem::BuildRoundPerformanceMetrics() const
{
  FCrowdDemoRoundPerformanceMetrics Result;
  const auto MaxSample = [](const TArray<float>& Samples)
  {
    float MaxValue = -1.0f;
    for (const float Value : Samples)
    {
      MaxValue = FMath::Max(MaxValue, Value);
    }
    return MaxValue;
  };
  const auto FillStage = [this, &MaxSample](
      const ECrowdDemoRoundPerformanceStage Stage, float& OutP95, float& OutMax)
  {
    const TArray<float>& Samples =
      RoundPerformanceStageMsSamples[static_cast<uint8>(Stage)];
    OutP95 = Percentile(Samples, 0.95f);
    OutMax = MaxSample(Samples);
  };

  Result.FixedStepPipelineMsP50 = Percentile(FixedStepPipelineMsSamples, 0.50f);
  Result.FixedStepPipelineMsP95 = Percentile(FixedStepPipelineMsSamples, 0.95f);
  Result.FixedStepPipelineMsMax = MaxSample(FixedStepPipelineMsSamples);
  FillStage(ECrowdDemoRoundPerformanceStage::BusinessPrepare,
    Result.BusinessPrepareStageMsP95, Result.BusinessPrepareStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::SharedFlow,
    Result.SharedFlowStageMsP95, Result.SharedFlowStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetTopology,
    Result.TargetTopologyStageMsP95, Result.TargetTopologyStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetDemand,
    Result.TargetDemandStageMsP95, Result.TargetDemandStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetPlan,
    Result.TargetPlanStageMsP95, Result.TargetPlanStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::TargetGuidance,
    Result.TargetGuidanceStageMsP95, Result.TargetGuidanceStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::GuidanceCompose,
    Result.GuidanceComposeStageMsP95, Result.GuidanceComposeStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::LocalPredictive,
    Result.LocalPredictiveStageMsP95, Result.LocalPredictiveStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::Particle,
    Result.ParticleStageMsP95, Result.ParticleStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::FacingFinalize,
    Result.FacingFinalizeStageMsP95, Result.FacingFinalizeStageMsMax);
  FillStage(ECrowdDemoRoundPerformanceStage::Commit,
    Result.CommitStageMsP95, Result.CommitStageMsMax);
  Result.TargetTopologyBuildCount = PerformanceTargetTopologyBuildCount;
  Result.TargetTopologyCacheHitCount = PerformanceTargetTopologyCacheHitCount;
  Result.TargetDemandFullBuildCount = PerformanceTargetDemandFullBuildCount;
  Result.TargetDemandPopulationUpdateCount = PerformanceTargetDemandPopulationUpdateCount;
  Result.FixedStepsPerGameFrameP50 = Percentile(FixedStepsPerGameFrameSamples, 0.50f);
  Result.FixedStepsPerGameFrameP95 = Percentile(FixedStepsPerGameFrameSamples, 0.95f);
  Result.FixedStepsPerGameFrameMax = FMath::Max(
    0, FMath::RoundToInt(MaxSample(FixedStepsPerGameFrameSamples)));
  Result.CatchupFrameCount = PerformanceCatchupFrameCount;
  Result.CatchupCpuBudgetHitCount = PerformanceCatchupCpuBudgetHitCount;
  Result.CatchupCpuBudgetConsecutiveMax = PerformanceCatchupCpuBudgetConsecutiveMax;
  Result.MaxFixedStepsPerFrameHitCount = PerformanceMaxFixedStepsPerFrameHitCount;
  Result.BoundaryPendingFrameCount = BoundaryPendingFrameCount;
  Result.BoundaryStaleResultCount = BoundaryStaleResultCount;
  Result.OrdinaryBlockWaitCount = BoundaryOrdinaryBlockWaitCount;
  Result.FixedStepBacklogMsMax = PerformanceFixedStepBacklogMsMax;
  Result.FixedStepBacklogMsP95 =
    Percentile(FixedStepBacklogMsSamples, 0.95f);
  Result.WorkerQueueMsP95 =
    Percentile(BoundaryWorkerQueueMsSamples, 0.95f);
  Result.WorkerRunMsP95 =
    Percentile(BoundaryWorkerRunMsSamples, 0.95f);
  Result.WorkerCriticalPathMsP95 =
    Percentile(BoundaryWorkerCriticalPathMsSamples, 0.95f);
  const double WallElapsedSeconds = FPlatformTime::Seconds() - PerformanceRoundWallStartSeconds;
  if (WallElapsedSeconds > UE_SMALL_NUMBER)
  {
    Result.SimulationRealtimeFactor = FMath::Max(
      0.0f, SimulatedServerTimeSeconds - PerformanceRoundSimStartSeconds)
      / static_cast<float>(WallElapsedSeconds);
  }
  Result.RollbackReplayMsP95 = Percentile(RollbackReplayMsSamples, 0.95f);
  Result.RollbackReplayMsMax = MaxSample(RollbackReplayMsSamples);
  Result.RollbackReplaySampleCount = RollbackReplayMsSamples.Num();
  Result.ZeroErrorRollbackReplayCount = PerformanceZeroErrorRollbackReplayCount;
  return Result;
}

bool UCrowdDemoRoundSimPipelineSubsystem::IsSoftPressureRouteDiagnosticEnabled() const
{
  static const bool bEnabled = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoSoftPressureRouteDiagnostic"));
  return IsActive()
    && GetRules().Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    && (bEnabled || IsOpenCohortMovement());
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRouteStep(
  const TConstArrayView<FCrowdDemoSoftPressureRouteStepSample> Samples)
{
  if (!IsSoftPressureRouteDiagnosticEnabled()) return;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    Samples, SoftPressureRouteDiagnosticRuntime);
  SoftPressureRouteDiagnosticSummary = {};
}

void UCrowdDemoRoundSimPipelineSubsystem::FinalizeSoftPressureRouteDiagnostic(
  const FCrowdDemoSoftPressureRouteCounterfactual& Counterfactual)
{
  if (!IsSoftPressureRouteDiagnosticEnabled()
    || SoftPressureRouteDiagnosticSummary.bValid) return;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    SoftPressureRouteDiagnosticRuntime, Counterfactual,
    SoftPressureRouteDiagnosticSummary);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundStart(TConstArrayView<FCrowdDemoRoundAgentState> States)
{
  RoundInitialOverlapPairCount = CountOverlapPairs(States, OverlapRadiusCm);
  RoundInitialSevereOverlapPairCount = CountOverlapPairs(States, SevereOverlapRadiusCm);
  LastCompareMetrics.InitialOverlapPairCount = RoundInitialOverlapPairCount;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordCheckpointComparison(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  TConstArrayView<FCrowdDemoRoundAgentState> ServerStates,
  const int32 StateFrameRevision)
{
  TMap<int32, const FCrowdDemoRoundAgentState*> ClientById;
  for (const FCrowdDemoRoundAgentState& State : ClientStates)
  {
    ClientById.Add(State.AgentId, &State);
  }
  TArray<float> PositionErrors;
  TArray<float> YawErrors;
  TArray<float> VelocityErrors;
  float MaxPositionError = 0.0f;
  int32 MaxAgentId = INDEX_NONE;
  for (const FCrowdDemoRoundAgentState& ServerState : ServerStates)
  {
    const FCrowdDemoRoundAgentState* const* ClientStatePtr = ClientById.Find(ServerState.AgentId);
    if (!ClientStatePtr)
    {
      continue;
    }
    const FCrowdDemoRoundAgentState& ClientState = **ClientStatePtr;
    const float PositionError = FVector::Dist2D(FVector(ClientState.Location), FVector(ServerState.Location));
    PositionErrors.Add(PositionError);
    YawErrors.Add(FMath::Abs(FMath::FindDeltaAngleDegrees(ClientState.YawDegrees, ServerState.YawDegrees)));
    VelocityErrors.Add(FVector::Dist2D(FVector(ClientState.Velocity), FVector(ServerState.Velocity)));
    if (PositionError > MaxPositionError)
    {
      MaxPositionError = PositionError;
      MaxAgentId = ServerState.AgentId;
    }
  }
  LastCorrectionMetrics.CorrectionFrameRevision = StateFrameRevision;
  LastCorrectionMetrics.CorrectionFrameLatestRevisionApplied =
    StateFrameRevision;
  LastCorrectionMetrics.CorrectionAgentCount = ServerStates.Num();
  LastCorrectionMetrics.CorrectionPositionErrorCmP50 = Percentile(PositionErrors, 0.50f);
  LastCorrectionMetrics.CorrectionPositionErrorCmP95 = Percentile(PositionErrors, 0.95f);
  LastCorrectionMetrics.CorrectionPositionErrorCmMax = MaxPositionError;
  LastCorrectionMetrics.CorrectionYawErrorDegP95 = Percentile(YawErrors, 0.95f);
  LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95 = Percentile(VelocityErrors, 0.95f);
  LastCorrectionMetrics.CorrectionErrorAgentIdMax = MaxAgentId;
  LastCorrectionMetrics.RoundTimeDeltaMs = 0.0f;
  LastCorrectionMetrics.CorrectionFrameAgeMsP95 = 0.0f;
  LastCorrectionMetrics.CorrectionFrameReplayMsP95 = -1.0f;
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundResultComparisonAndApplied(
  TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
  const FCrowdDemoRoundResultPacket& Packet)
{
  const FCrowdDemoRoundCheckpointFrameMetrics PreviousCorrectionMetrics = LastCorrectionMetrics;
  RecordCheckpointComparison(
    ClientStates, Packet.Agents, Packet.StateFrameRevision);
  LastCompareMetrics.RoundId = Packet.RoundId;
  LastCompareMetrics.Revision = Packet.Revision;
  LastCompareMetrics.CheckpointRevision = Packet.CheckpointRevision;
  LastCompareMetrics.SimPositionErrorCmP50 = LastCorrectionMetrics.CorrectionPositionErrorCmP50;
  LastCompareMetrics.SimPositionErrorCmP95 = LastCorrectionMetrics.CorrectionPositionErrorCmP95;
  LastCompareMetrics.SimPositionErrorCmMax = LastCorrectionMetrics.CorrectionPositionErrorCmMax;
  CrossRoundPositionErrorSeries.Record(LastCompareMetrics.SimPositionErrorCmP95);
  LastCompareMetrics.CrossRoundPositionErrorCmP95Max = CrossRoundPositionErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundPositionErrorGrowthCm = CrossRoundPositionErrorSeries.GetExpansionFromFirst();
  CrossRoundCorrectionIntervalErrorSeries.Record(
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95);
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max =
    CrossRoundCorrectionIntervalErrorSeries.GetMax();
  LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm =
    CrossRoundCorrectionIntervalErrorSeries.GetExpansionFromFirst();
  LastCompareMetrics.SimYawErrorDegP95 = LastCorrectionMetrics.CorrectionYawErrorDegP95;
  LastCompareMetrics.SimVelocityErrorCmpsP95 = LastCorrectionMetrics.CorrectionVelocityErrorCmpsP95;
  LastCompareMetrics.SimOverlapPairDelta = CountOverlapPairs(ClientStates, OverlapRadiusCm) - Packet.OverlapPairCount;
  LastCorrectionMetrics = PreviousCorrectionMetrics;
  LastAppliedCheckpointStateFrameRevision = Packet.StateFrameRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  ++LastCompareMetrics.CorrectionAppliedCount;
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundCheckpoint role=client round_id=%d revision=%d checkpoint_revision=%d completed_round_count=%d correction_applied_count=%d sim_position_error_cm_p50=%.3f sim_position_error_cm_p95=%.3f sim_position_error_cm_max=%.3f correction_interval_position_error_cm_p95=%.3f correction_interval_position_error_cm_max=%.3f cross_round_position_error_cm_p95_max=%.3f cross_round_position_error_growth_cm=%.3f cross_round_correction_interval_error_cm_p95_max=%.3f cross_round_correction_interval_error_growth_cm=%.3f sim_yaw_error_deg_p95=%.3f sim_velocity_error_cmps_p95=%.3f sim_overlap_pair_delta=%d source=MassPipeline"),
    Packet.RoundId,
    Packet.Revision,
    Packet.CheckpointRevision,
    LastCompareMetrics.CompletedRoundCount,
    LastCompareMetrics.CorrectionAppliedCount,
    LastCompareMetrics.SimPositionErrorCmP50,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.SimPositionErrorCmMax,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmMax,
    LastCompareMetrics.CrossRoundPositionErrorCmP95Max,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorCmP95Max,
    LastCompareMetrics.CrossRoundCorrectionIntervalErrorGrowthCm,
    LastCompareMetrics.SimYawErrorDegP95,
    LastCompareMetrics.SimVelocityErrorCmpsP95,
    LastCompareMetrics.SimOverlapPairDelta);
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoFlowCheckpoint role=client round_id=%d agents=%d flow_field_revision=%d flow_field_build_hash=%u flow_field_rebuild_count=%d flow_unreachable_agent_count=%d flow_goal_reached_count=%d flow_wall_pass_count=%d flow_corridor_exit_count=%d flow_turn_exit_count=%d corridor_deadlock_agent_count=%d client_sim_obstacle_penetration_count=%d sim_position_error_cm_p95=%.3f correction_interval_position_error_cm_p95=%.3f cross_round_position_error_growth_cm=%.3f source=MassPipeline"),
    Packet.RoundId,
    Packet.Agents.Num(),
    LastCompareMetrics.FlowFieldRevision,
    LastCompareMetrics.FlowFieldBuildHash,
    LastCompareMetrics.FlowFieldRebuildCount,
    LastCompareMetrics.FlowUnreachableAgentCount,
    LastCompareMetrics.FlowGoalReachedCount,
    LastCompareMetrics.FlowWallPassCount,
    LastCompareMetrics.FlowCorridorExitCount,
    LastCompareMetrics.FlowTurnExitCount,
    LastCompareMetrics.CorridorDeadlockAgentCount,
    LastCompareMetrics.ClientSimObstaclePenetrationCount,
    LastCompareMetrics.SimPositionErrorCmP95,
    LastCompareMetrics.CorrectionIntervalPositionErrorCmP95,
    LastCompareMetrics.CrossRoundPositionErrorGrowthCm);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordRoundInitialState(
  const uint32 InputHash,
  const uint32 InitialStateHash)
{
  RoundInputHash = InputHash;
  RoundInitialStateHash = InitialStateHash;
  ++RoundResetCount;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoRoundInitialState role=%s round_id=%d input_hash=%u state_hash=%u reset_count=%d source=MassPipeline"),
    GetWorld() && GetWorld()->GetNetMode() == NM_Client
      ? TEXT("client") : TEXT("server"),
    GetCurrentRoundId(), RoundInputHash, RoundInitialStateHash,
    RoundResetCount);
}

void UCrowdDemoRoundSimPipelineSubsystem::RecordNavigationDomainReprojectDelta(const float DeltaCm)
{
  LastCompareMetrics.SharedFlowMetrics.NavigationDomainReprojectDeltaCmMax = FMath::Max(
    LastCompareMetrics.SharedFlowMetrics.NavigationDomainReprojectDeltaCmMax, DeltaCm);
}

bool UCrowdDemoRoundSimPipelineSubsystem::ShouldBuildRoundResult() const
{
  const float BoundaryTime = bStepInProgress ? CurrentStepEndServerTimeSeconds : SimulatedServerTimeSeconds;
  return bPlanActive
    && LastBuiltResultRoundId < ActivePlan.RoundId
    && (bParticleConstraintRunFailure
      || BoundaryTime + KINDA_SMALL_NUMBER >= ActivePlan.StartServerTimeSeconds + ActivePlan.DurationSeconds);
}

void UCrowdDemoRoundSimPipelineSubsystem::EnqueueOutgoingRoundResult(FCrowdDemoRoundResultPacket&& Packet)
{
  OutgoingRoundResults.Add(MoveTemp(Packet));
}

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueOutgoingRoundResult(FCrowdDemoRoundResultPacket& OutPacket)
{
  if (OutgoingRoundResults.IsEmpty())
  {
    return false;
  }
  OutPacket = MoveTemp(OutgoingRoundResults[0]);
  OutgoingRoundResults.RemoveAt(0, 1, EAllowShrinking::No);
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::MarkRoundResultBuilt(const int32 CheckpointRevision)
{
  LastBuiltResultRoundId = ActivePlan.RoundId;
  LastCheckpointRevision = CheckpointRevision;
  ++LastCompareMetrics.CompletedRoundCount;
  LastCompareMetrics.CheckpointRevision = CheckpointRevision;
  LastCompletedRoundMetrics = LastCompareMetrics;
}

void UCrowdDemoRoundSimPipelineSubsystem::MergeNetworkCorrectionMetrics(
  const FCrowdDemoRoundCheckpointFrameMetrics& NetworkMetrics)
{
  LastCorrectionMetrics.RoundCheckpointHeaderReceivedCount = NetworkMetrics.RoundCheckpointHeaderReceivedCount;
  LastCorrectionMetrics.CorrectionFrameChunkReceivedCount = NetworkMetrics.CorrectionFrameChunkReceivedCount;
  LastCorrectionMetrics.CorrectionFrameCompleteCount = NetworkMetrics.CorrectionFrameCompleteCount;
  LastCorrectionMetrics.CorrectionFramePublishedCount = NetworkMetrics.CorrectionFramePublishedCount;
  LastCorrectionMetrics.CorrectionFrameReceivedCount = NetworkMetrics.CorrectionFrameReceivedCount;
  LastCorrectionMetrics.CorrectionFrameRevisionGapCount = NetworkMetrics.CorrectionFrameRevisionGapCount;
  LastCorrectionMetrics.CorrectionFrameChunksPerFrame = NetworkMetrics.CorrectionFrameChunksPerFrame;
  LastCorrectionMetrics.CorrectionFrameChunkSize = NetworkMetrics.CorrectionFrameChunkSize;
  LastCorrectionMetrics.CorrectionIntervalMsP95 = NetworkMetrics.CorrectionIntervalMsP95;
  LastCorrectionMetrics.CorrectionFrameAssemblyMsP95 = NetworkMetrics.CorrectionFrameAssemblyMsP95;
}

void UCrowdDemoRoundSimPipelineSubsystem::LogStageOnce(const TCHAR* StageName, const int32 AgentCount)
{
  const FName StageKey(StageName);
  if (LoggedStages.Contains(StageKey))
  {
    return;
  }
  LoggedStages.Add(StageKey);
  const UWorld* World = GetWorld();
  UE_LOG(
    LogTemp,
    Display,
    TEXT("CrowdDemoRoundPipeline role=%s stage=%s order_revision=%d fixed_step=%.4f agents=%d source=MassProcessor"),
    World && World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
    StageName,
    GetCurrentPlanRevision(),
    CurrentFixedStepSeconds,
    AgentCount);
}

int32 UCrowdDemoRoundSimPipelineSubsystem::CountOverlapPairs(
  TConstArrayView<FCrowdDemoRoundAgentState> States,
  const float RadiusCm)
{
  int32 Count = 0;
  const float RadiusSquared = FMath::Square(RadiusCm);
  for (int32 A = 0; A < States.Num(); ++A)
  {
    for (int32 B = A + 1; B < States.Num(); ++B)
    {
      if (FVector::DistSquared2D(FVector(States[A].Location), FVector(States[B].Location)) < RadiusSquared)
      {
        ++Count;
      }
    }
  }
  return Count;
}

float UCrowdDemoRoundSimPipelineSubsystem::Percentile(TArray<float> Values, const float Quantile)
{
  if (Values.IsEmpty())
  {
    return -1.0f;
  }
  Values.Sort();
  const int32 Index = FMath::Clamp(FMath::CeilToInt(Values.Num() * Quantile) - 1, 0, Values.Num() - 1);
  return Values[Index];
}
