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
#include "MassCrowdWorkerFlowBinding.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "MassCrowdWorkerNavigationObjective.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerProjectileDomain.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tasks/Task.h"

namespace
{
struct FCrowdBootstrapStageId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBootstrapStageId&) const = default;
  auto operator<=>(const FCrowdBootstrapStageId&) const = default;
};

struct FCrowdBootstrapTaskTypeId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBootstrapTaskTypeId&) const = default;
  auto operator<=>(const FCrowdBootstrapTaskTypeId&) const = default;
};

struct FCrowdBootstrapTaskKey
{
  FCrowdBootstrapStageId StageId;
  FCrowdBootstrapTaskTypeId TaskTypeId;
  uint64 ScopeKey = 0;
  bool operator==(const FCrowdBootstrapTaskKey&) const = default;
  bool operator<(const FCrowdBootstrapTaskKey& Other) const
  {
    if (StageId != Other.StageId) return StageId < Other.StageId;
    if (TaskTypeId != Other.TaskTypeId) return TaskTypeId < Other.TaskTypeId;
    return ScopeKey < Other.ScopeKey;
  }
  bool IsValid() const
  { return StageId.IsValid() && TaskTypeId.IsValid(); }
};

struct FCrowdBootstrapTaskResult
{
  uint64 StableHash = 14695981039346656037ull;
  bool bSucceeded = false;
  static FCrowdBootstrapTaskResult Success(uint64 Hash)
  {
    FCrowdBootstrapTaskResult Result;
    Result.StableHash = Hash;
    Result.bSucceeded = Hash != 0;
    return Result;
  }
  static FCrowdBootstrapTaskResult Failure() { return {}; }
};

using FCrowdBootstrapTaskBody =
  TUniqueFunction<FCrowdBootstrapTaskResult()>;

class FCrowdDemoBootstrapSynchronousGraph
{
public:
  bool AddTask(
    const FCrowdBootstrapTaskKey Key,
    const TConstArrayView<FCrowdBootstrapTaskKey> Prerequisites,
    FCrowdBootstrapTaskBody&& Body,
    const bool bRequireOffGameThread = true)
  {
    (void)bRequireOffGameThread;
    if (!IsInGameThread() || !Key.IsValid() || !Body
      || Nodes.ContainsByPredicate(
        [&Key](const FNode& Node) { return Node.Key == Key; }))
      return false;
    FNode& Node = Nodes.AddDefaulted_GetRef();
    Node.Key = Key;
    Node.Prerequisites = TArray<FCrowdBootstrapTaskKey>(Prerequisites);
    Node.Prerequisites.Sort();
    for (int32 Index = 0; Index < Node.Prerequisites.Num(); ++Index)
      if (!Node.Prerequisites[Index].IsValid()
        || Node.Prerequisites[Index] == Key
        || (Index > 0
          && !(Node.Prerequisites[Index - 1]
            < Node.Prerequisites[Index])))
        return false;
    Node.Body = MoveTemp(Body);
    return true;
  }

  bool Run()
  {
    if (!IsInGameThread() || Nodes.IsEmpty()) return false;
    TSet<int32> Completed;
    while (Completed.Num() < Nodes.Num())
    {
      int32 Selected = INDEX_NONE;
      for (int32 Index = 0; Index < Nodes.Num(); ++Index)
      {
        if (Completed.Contains(Index)) continue;
        bool bReady = true;
        for (const FCrowdBootstrapTaskKey& Prerequisite
          : Nodes[Index].Prerequisites)
        {
          const int32 PrerequisiteIndex = Nodes.IndexOfByPredicate(
            [&Prerequisite](const FNode& Node)
            { return Node.Key == Prerequisite; });
          if (PrerequisiteIndex == INDEX_NONE
            || !Completed.Contains(PrerequisiteIndex))
          {
            bReady = false;
            break;
          }
        }
        if (!bReady) continue;
        if (Selected == INDEX_NONE
          || Nodes[Index].Key < Nodes[Selected].Key)
          Selected = Index;
      }
      if (Selected == INDEX_NONE)
      {
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoBootstrapGraphRejected stage=schedule completed=%d nodes=%d reason=no_ready_task"),
          Completed.Num(), Nodes.Num());
        return false;
      }
      FCrowdBootstrapTaskResult Result = Nodes[Selected].Body();
      if (!Result.bSucceeded)
      {
        const FCrowdBootstrapTaskKey& Key = Nodes[Selected].Key;
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoBootstrapGraphRejected stage=execute completed=%d nodes=%d task_stage=%u task_type=%u scope=%llu reason=task_failure"),
          Completed.Num(), Nodes.Num(), Key.StageId.Value,
          Key.TaskTypeId.Value, Key.ScopeKey);
        return false;
      }
      Completed.Add(Selected);
    }
    return true;
  }

private:
  struct FNode
  {
    FCrowdBootstrapTaskKey Key;
    TArray<FCrowdBootstrapTaskKey> Prerequisites;
    FCrowdBootstrapTaskBody Body;
  };
  TArray<FNode> Nodes;
};
}

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
    const double EffectiveSimulationTimeSeconds,
    const double FixedSimulationQuantumSeconds,
    const uint64 ResourceRevision,
    FCrowdWorkerObjectiveRevisionDelta& OutDelta)
  {
    OutDelta = {};
    int32 EffectiveFixedStepIndex = INDEX_NONE;
    if (ResourceRevision == 0
      || !FCrowdWorkerTargetObjectiveClock::
        ResolveEffectiveFixedStepIndex(
          EffectiveSimulationTimeSeconds,
          FixedSimulationQuantumSeconds,
          EffectiveFixedStepIndex))
      return false;
    FCrowdWorkerTargetObjectiveRevision Revision;
    Revision.TargetRevision = Fact.TargetRevision;
    Revision.EffectiveFixedStepIndex = EffectiveFixedStepIndex;
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

  FCrowdDemoCombatNetState BuildT7PresentationCombatNetState(
    const FCrowdDemoCombatAgentState& State)
  {
    FCrowdDemoCombatNetState Result;
    Result.Health = State.Health;
    Result.MaxHealth = State.MaxHealth;
    Result.LifecycleState = State.LifecycleState;
    Result.bAlive = State.bAlive ? 1 : 0;
    Result.BusinessState = State.BusinessState;
    Result.BusinessStateRevision = State.BusinessStateRevision;
    Result.BusinessStateEnterFixedStep =
      State.BusinessStateEnterFixedStep;
    Result.TargetAgentId = State.TargetAgentId;
    Result.TargetLifecycleSerial = State.TargetLifecycleSerial;
    Result.AttackPhase = State.AttackPhase;
    Result.AttackPhaseEnterFixedStep = State.AttackPhaseEnterFixedStep;
    Result.CooldownEndFixedStep = State.CooldownEndFixedStep;
    Result.LockedTargetAgentId = State.LockedTargetAgentId;
    Result.LockedTargetLifecycleSerial =
      State.LockedTargetLifecycleSerial;
    Result.LockedTargetLocation = FVector_NetQuantize10(
      State.LockedTargetLocation);
    Result.FireSequence = State.FireSequence;
    Result.bFireRequestIssued = State.bFireRequestIssued ? 1 : 0;
    Result.ReactiveMode = State.ReactiveMode;
    Result.HorizontalReactiveVelocity = FVector_NetQuantize10(
      State.HorizontalReactiveVelocity);
    Result.VerticalReactiveVelocityCmps =
      State.VerticalReactiveVelocityCmps;
    Result.ReactiveStartFixedStep = State.ReactiveStartFixedStep;
    Result.ReactiveEndFixedStep = State.ReactiveEndFixedStep;
    Result.ReactiveRevision = State.ReactiveRevision;
    Result.RestoreBusinessState = State.RestoreBusinessState;
    Result.ApexCount = State.ApexCount;
    Result.LandingCount = State.LandingCount;
    Result.HitFlashRevision = State.HitFlashRevision;
    Result.HitFlashStartServerTimeSeconds =
      State.HitFlashStartServerTimeSeconds;
    Result.HitFlashDurationSeconds = State.HitFlashDurationSeconds;
    Result.HitFlashProfileKey = State.HitFlashProfileKey;
    Result.HitFlashPeakIntensity = State.HitFlashPeakIntensity;
    Result.LastConsumedHitEventId = State.LastConsumedHitEventId;
    Result.VisualState = State.VisualState;
    Result.VisualRevision = State.VisualRevision;
    Result.VisualStateStartServerTimeSeconds =
      State.VisualStateStartServerTimeSeconds;
    Result.VisualPhaseSeed = State.VisualPhaseSeed;
    return Result;
  }

  uint32 BuildT7PresentationStateSignature(
    const FCrowdDemoCombatNetState& State,
    const int32 LifecycleSerial)
  {
    uint32 Hash = GetTypeHash(LifecycleSerial);
    Hash = HashCombineFast(Hash, GetTypeHash(State.Health));
    Hash = HashCombineFast(Hash, GetTypeHash(State.bAlive));
    Hash = HashCombineFast(Hash,
      GetTypeHash(static_cast<uint8>(State.LifecycleState)));
    Hash = HashCombineFast(Hash,
      GetTypeHash(static_cast<uint8>(State.BusinessState)));
    Hash = HashCombineFast(Hash, GetTypeHash(State.BusinessStateRevision));
    Hash = HashCombineFast(Hash,
      GetTypeHash(static_cast<uint8>(State.AttackPhase)));
    Hash = HashCombineFast(Hash,
      GetTypeHash(static_cast<uint8>(State.ReactiveMode)));
    Hash = HashCombineFast(Hash, GetTypeHash(State.ReactiveRevision));
    Hash = HashCombineFast(Hash, GetTypeHash(State.ApexCount));
    Hash = HashCombineFast(Hash, GetTypeHash(State.LandingCount));
    Hash = HashCombineFast(Hash, GetTypeHash(State.HitFlashRevision));
    Hash = HashCombineFast(Hash,
      GetTypeHash(static_cast<uint8>(State.VisualState)));
    Hash = HashCombineFast(Hash, GetTypeHash(State.VisualRevision));
    return Hash;
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

  FCrowdDemoBoundaryBusinessWorkOutput
    BuildWorkerNativeScenarioBusinessBootstrap(
      const FCrowdDemoBoundaryBusinessWorkInput& Input)
  {
    FCrowdDemoBoundaryBusinessWorkOutput Output;
    if (!Input.Snapshot.bValid
      || Input.FixedStepIndex != Input.Snapshot.FixedStepIndex
      || Input.PlanRevision != Input.Snapshot.PlanRevision
      || Input.Facts.Num() != Input.Snapshot.Agents.Num()
      || Input.FixedStepSeconds <= 0.0f)
      return Output;
    Output.bRequiresCommit = false;
    Output.StableHash = FoldBoundaryHash(
      FoldBoundaryHash(Input.Snapshot.StableHash, 0x57374e42u),
      static_cast<uint32>(Input.FixedStepIndex));
    Output.bCompleted = Output.StableHash != 0;
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
  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =
    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
               : nullptr;
  check(SharedFlowRuntimeSubsystem);
  SharedFlowRuntimeSubsystem->ResetSharedFlowDynamicState();
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
  bCurrentStepFullWorkerProductionFastPath = false;
  CurrentStepFullWorkerInputSequence = 0;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryFacingWorkState.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  WorkerScenarioMovementProfiles.Reset();
  WorkerScenarioFrozenProfiles.Reset();
  LastScenarioObservationGeneration = 0;
  LastScenarioObservationPublishSequence = 0;
  LastScenarioObservationAbsoluteTick = INDEX_NONE;
  OpenSpawnScenarioAbsoluteOriginTick = INDEX_NONE;
  OpenSpawnScenarioLastCommandTick = 0;
  VatShowcaseScenarioAbsoluteOriginTick = INDEX_NONE;
  VatShowcaseLastMovementHalfCycle = INDEX_NONE;
  VatShowcaseLastPresentationSignatureByAgentId.Reset();
  bValidCorridorTransitHoldCommandSubmitted = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  bLastWorkerV2MovementControlTargetActive = false;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedPlannerDecisionHash = 0;
  PreparedReactiveMotionSteps.Reset();
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
  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =
    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
               : nullptr;
  check(SharedFlowRuntimeSubsystem);
  SharedFlowRuntimeSubsystem->ResetSharedFlowDynamicState();
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
    OutgoingT7PresentationEvents.Reset();
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
  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =
    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
               : nullptr;
  if (!SharedFlowRuntimeSubsystem) return false;
  FCrowdMassSharedFlowBuildOutput Output;
  if (!SharedFlowRuntimeSubsystem->EnsureSharedFlowResource(
      Input, Output))
    return false;
  const FCrowdMassSharedFlowResource& RuntimeSharedFlowResource =
    SharedFlowRuntimeSubsystem->GetSharedFlowResource();
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
  BoundaryFacingWorkState.Reset();
  PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
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


bool UCrowdDemoRoundSimPipelineSubsystem::
  BeginWorkerBootstrapPreparation(const double GatherMilliseconds)
{
  if (!IsInGameThread() || !IsBoundarySnapshotCurrent()
    || bCurrentStepFullWorkerProductionFastPath
    || BoundaryFacingWorkState.IsValid())
    return false;
  CurrentBoundaryRequestStartSeconds = FPlatformTime::Seconds()
    - FMath::Max(0.0, GatherMilliseconds) / 1000.0;
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
  if (!IsInGameThread() || !IsBoundarySnapshotCurrent())
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
  BoundaryFacingWorkState->bUseWorkerNativeScenarioBusiness =
    bVatShowcase && IsFullWorkerProductionMode();
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
  if (!IsInGameThread()
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
)
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
      CachedSummary->NavigationBlockedCellCount,
      CachedSummary->TotalFeasibleCapacity,
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
  if (!IsInGameThread()
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
  const bool bWorkerNativeVatBootstrap =
    BoundaryFacingWorkState.IsValid()
    && BoundaryFacingWorkState->bUseWorkerNativeScenarioBusiness;
  if (!IsInGameThread() || !BoundaryFacingWorkState.IsValid()
    || BoundaryFacingWorkState->bWorkerV2InputSubmitted
    || !BoundaryFacingWorkState->bMovementShadowInputValid
    || (!bWorkerNativeVatBootstrap
      && !BoundaryFacingWorkState->GraphOutput.Movement.bCompleted)
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
  const bool bHasTargetControl =
    !BoundaryFacingWorkState->TargetTopologySlots.IsEmpty();
  const bool bMovementTargetActivityChanged =
    bLastWorkerV2MovementControlTargetActive != bHasTargetControl;
  const bool bPublishMovementControl =
    !bSubmitIntentOnly
    || LastWorkerV2MovementControlGeneration
      != WorkerShadow.GetGeneration()
    || LastWorkerV2MovementControlPlanRevision
      != GetCurrentPlanRevision()
    || bMovementTargetActivityChanged;
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
  FString MovementModeValue;
  const bool bMovementProduction =
    FParse::Value(
      FCommandLine::Get(),
      TEXT("CrowdWorkerMovementMode="),
      MovementModeValue)
    && MovementModeValue.Equals(
      TEXT("Production"), ESearchCase::IgnoreCase);
  const bool bCaptureShadowExpectation =
    !bMovementProduction
    && RuntimeSubsystem->GetWorkerMovementAuthority().GetMode()
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
  TMap<int32, const FCrowdDemoBidirectionalSwapLayoutAgent*>
    ExplicitFlowFixtureByAgentId;
  if (IsBidirectionalSwap())
  {
    if (!BidirectionalSwapLayout.bValid
      || !EnsureBidirectionalSwapFlowResources())
      return RejectWorkerV2Input(TEXT("flow_fixture"));
    ExplicitFlowFixtureByAgentId.Reserve(
      BidirectionalSwapLayout.Agents.Num());
    for (const FCrowdDemoBidirectionalSwapLayoutAgent& Agent :
      BidirectionalSwapLayout.Agents)
    {
      if (!Agent.ObjectiveRef.IsValid()
        || !FCrowdDemoBidirectionalSwapKernel::IsCohortKeyValid(
          Agent.CohortKey)
        || !CrowdWorkerResourceIds::IsFlowResource(
          Agent.FlowResourceId)
        || !EntityRefByAgentId.Contains(Agent.AgentId)
        || ExplicitFlowFixtureByAgentId.Contains(Agent.AgentId))
        return RejectWorkerV2Input(TEXT("flow_fixture_binding"));
      ExplicitFlowFixtureByAgentId.Add(Agent.AgentId, &Agent);
    }
    if (ExplicitFlowFixtureByAgentId.Num()
      != BoundarySnapshot.Agents.Num())
      return RejectWorkerV2Input(TEXT("flow_fixture_membership"));
  }

  FCrowdWorkerTargetControlResource TargetControl;
  TSet<FCrowdStableEntityRef> WorkerTargetGuidanceEntities;
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
    for (const FCrowdWorkerTargetCohortInput& Cohort :
      TargetControl.Cohorts)
    {
      for (const FCrowdWorkerTargetAgentInput& Agent : Cohort.Agents)
      {
        if (!Agent.EntityRef.IsValid()
          || WorkerTargetGuidanceEntities.Contains(Agent.EntityRef))
          return RejectWorkerV2Input(TEXT("target_guidance_membership"));
        WorkerTargetGuidanceEntities.Add(Agent.EntityRef);
      }
    }
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
  const bool bVatShowcase =
    BoundaryFacingWorkState->BusinessInput.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && BoundaryFacingWorkState->BusinessInput.Rules.
      SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
  const bool bHasProjectileControl =
    bVatShowcase
    || BoundaryFacingWorkState->BusinessInput.Rules.
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
      || (!bVatShowcase
        && !BoundaryFacingWorkState->BusinessOutput.bRequiresCommit)
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
    if (bVatShowcase)
    {
      AttackSummary.bValid = true;
    }
    else
    {
      if (!FCrowdDemoProjectileAdapters::BuildRangedAttackPlan(
          BusinessInput.RoundId, BusinessInput.FixedStepIndex,
          BusinessInput.Rules.RangedCombatSettings,
          CombatAgents, SpawnRequests, AttackSummary))
        return RejectProjectileControl(TEXT("attack_plan"));
      const FCrowdDemoProjectileStepSummary& LegacyAttack =
        BoundaryFacingWorkState->BusinessOutput.Commit.
          ProjectileSummary;
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
    HostCombatInput.bVatShowcase = bVatShowcase;
    if (bVatShowcase)
    {
      constexpr int32 ShowcaseEventSteps[] = {30, 60, 90};
      for (const FCrowdDemoRangedCombatAgent& Agent :
        HostCombatAgents)
      {
        for (const int32 EventStep : ShowcaseEventSteps)
        {
          const ECrowdDemoVatInjectedHit InjectedHit =
            FCrowdDemoVatShowcasePlanner::SelectInjectedHit(
              Agent.FormationIndex, EventStep);
          if (InjectedHit == ECrowdDemoVatInjectedHit::None)
            continue;
          FCrowdDemoWorkerInjectedHitCommand& Command =
            HostCombatInput.InjectedHitCommands.
              AddDefaulted_GetRef();
          Command.ApplyFixedStep = EventStep;
          Command.TargetEntity = Agent.EntityRef;
          Command.HitEventId =
            (static_cast<uint64>(BusinessInput.RoundId) << 32)
            | (static_cast<uint64>(EventStep) << 16)
            | static_cast<uint32>(Agent.AgentId);
          Command.Damage =
            InjectedHit == ECrowdDemoVatInjectedHit::Death
            ? 1000.0f : 10.0f;
          Command.HorizontalImpulseCmps =
            InjectedHit == ECrowdDemoVatInjectedHit::Knockback
            ? 500.0f : 0.0f;
          Command.VerticalImpulseCmps =
            InjectedHit == ECrowdDemoVatInjectedHit::KnockUp
            ? 650.0f : 0.0f;
          Command.HitFlashProfileKey = 1;
        }
      }
    }
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
    if (!bWorkerNativeVatBootstrap)
      for (const FCrowdComposedGuidance& Guidance :
        MovementOutput.Guidance.ComposedGuidance)
      {
        if (!Guidance.bValid
          || GuidanceByAgentId.Contains(Guidance.AgentId))
          return false;
        GuidanceByAgentId.Add(Guidance.AgentId, &Guidance);
      }
    TMap<int32, const FCrowdLocalPredictiveResult*> LocalByAgentId;
    if (MovementInput.bRunLocalPredictive
      && !bWorkerNativeVatBootstrap)
    {
      for (const FCrowdLocalPredictiveResult& Local :
        MovementOutput.LocalPredictive.Results)
      {
        if (LocalByAgentId.Contains(Local.AgentId))
          return false;
        LocalByAgentId.Add(Local.AgentId, &Local);
      }
    }
    if ((!bWorkerNativeVatBootstrap
        && GuidanceByAgentId.Num() != Overlays.Num())
      || (MovementInput.bRunLocalPredictive
        && !bWorkerNativeVatBootstrap
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
      const FCrowdMassBoundaryAgentRecord* BoundaryAgent =
        BoundarySnapshot.Agents.FindByPredicate(
          [&Overlay](const FCrowdMassBoundaryAgentRecord& Agent)
          {
            return Agent.Identity.AgentId == Overlay.AgentId;
          });
      const FCrowdComposedGuidance* const* Guidance =
        bWorkerNativeVatBootstrap
          ? nullptr : GuidanceByAgentId.Find(Overlay.AgentId);
      const FCrowdLocalPredictiveResult* const* Local =
        MovementInput.bRunLocalPredictive
          && !bWorkerNativeVatBootstrap
          ? LocalByAgentId.Find(Overlay.AgentId)
          : nullptr;
      if (!EntityRef || !BoundaryAgent
        || (!bWorkerNativeVatBootstrap && !Guidance)
        || (MovementInput.bRunLocalPredictive
          && !bWorkerNativeVatBootstrap && !Local))
        return false;
      FCrowdWorkerMovementControlEntry& Entry =
        Control.Entries.AddDefaulted_GetRef();
      Entry.EntityRef = *EntityRef;
      Entry.AgentId = Overlay.AgentId;
      Entry.InteractionLayer = Overlay.InteractionLayer;
      Entry.PreviousBlockedAgeSteps =
        FMath::Max(0, Overlay.PreviousBlockedAgeSteps);
      Entry.MaximumSpeedCmps = Overlay.MaximumSpeedCmps;
      Entry.ParticleEnvironmentHardClearanceCm =
        BoundaryAgent->Properties.HardSafetyGapCm;
      Entry.ParticlePhysicalRadiusCm =
        BoundaryAgent->Properties.PhysicalRadiusCm;
      Entry.ParticleHardSafetyGapCm =
        BoundaryAgent->Properties.HardSafetyGapCm;
      Entry.ParticleSoftMarginCm =
        BoundaryAgent->Properties.SoftMarginCm;
      Entry.ParticleMobility =
        BoundaryAgent->Properties.Mobility;
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
      Entry.AutonomousPreferredVelocity = bWorkerNativeVatBootstrap
        ? FVector::ZeroVector
        : (*Guidance)->AutonomousPreferredVelocity;
      const bool bTargetOwner =
        TargetMode
          == ECrowdDemoWorkerTargetAuthorityMode::Production
        || TargetCanaries.Contains(*EntityRef);
      Entry.bUseWorkerTargetGuidance =
        bTargetOwner
        && WorkerTargetGuidanceEntities.Contains(*EntityRef);
      Entry.bUseAuthoritativePreferredVelocity =
        !bWorkerNativeVatBootstrap
        && ((*Guidance)->SelectedProvider
          == ECrowdGuidanceProvider::BusinessOverride
        || (*Guidance)->SelectedProvider
          == ECrowdGuidanceProvider::Stop);
      if (ExplicitFlowFixtureByAgentId.Contains(Overlay.AgentId))
      {
        Entry.AutonomousPreferredVelocity = FVector::ZeroVector;
        Entry.bUseWorkerTargetGuidance = false;
        Entry.bUseAuthoritativePreferredVelocity = false;
      }
      if (IsOpenSpawnRelaxation())
      {
        Entry.AutonomousPreferredVelocity = FVector::ZeroVector;
        Entry.bUseWorkerTargetGuidance = false;
        Entry.bUseAuthoritativePreferredVelocity = true;
      }
      else if (bVatShowcase)
      {
        const FCrowdDemoRoundBoundaryBusinessFact* BusinessFact =
          BoundaryFacingWorkState->BusinessInput.Facts.
            FindByPredicate(
              [&Overlay](
                const FCrowdDemoRoundBoundaryBusinessFact& Fact)
              {
                return Fact.AgentId == Overlay.AgentId;
              });
        if (!BusinessFact)
          return RejectWorkerV2Input(
            TEXT("vat_movement_profile_join"));
        const bool bMovingFormation =
          FCrowdDemoVatShowcasePlanner::ResolveInitialState(
            BusinessFact->FormationIndex)
          == ECrowdDemoVatPlannedState::Moving;
        Entry.AutonomousPreferredVelocity = bMovingFormation
          ? FVector(60.0f, 0.0f, 0.0f)
          : FVector::ZeroVector;
        Entry.bUseWorkerTargetGuidance = false;
        Entry.bUseAuthoritativePreferredVelocity = true;
      }
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
    if (IsHeterogeneousTransit() && bHasTargetControl)
    {
      int32 WorkerTargetGuidanceCount = 0;
      for (const FCrowdWorkerMovementControlEntry& Entry : Control.Entries)
        WorkerTargetGuidanceCount += Entry.bUseWorkerTargetGuidance ? 1 : 0;
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoT6MovementHandoff target_mode=%d target_membership=%d movement_entries=%d worker_target_guidance=%d source=WorkerInputSync"),
        static_cast<int32>(TargetMode),
        WorkerTargetGuidanceEntities.Num(), Control.Entries.Num(),
        WorkerTargetGuidanceCount);
    }
  }
  const bool bHasPrimaryTargetParticle =
    Control.ExternalParticleAgents.ContainsByPredicate([](const auto& Agent)
    {
      return Agent.AgentId
        == CrowdWorkerTargetConstants::PrimaryTargetParticleAgentId;
    });
  const bool bRequiresTargetObjective =
    bHasTargetControl || bHasPrimaryTargetParticle;
  TArray<FCrowdWorkerVersionedResourceInput> ResourceInputs;
  ResourceInputs.Reserve(
    (bPublishMovementControl ? 1 : 0)
      + (bPublishTargetControl ? 1 : 0)
      + (bPublishProjectileControl ? 1 : 0)
      + (!bSubmitIntentOnly && bRequiresTargetObjective ? 1 : 0)
      + (ExplicitFlowFixtureByAgentId.IsEmpty() ? 0 : 4));
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
  if (!ExplicitFlowFixtureByAgentId.IsEmpty())
  {
    const uint32 CohortKeys[] = {
      FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
      FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey};
    for (const uint32 CohortKey : CohortKeys)
    {
      const FCrowdSharedFlowField* Flow =
        FindRuntimeBidirectionalSwapFlowField(CohortKey);
      const FCrowdWorkerObjectiveRef ObjectiveRef =
        FCrowdDemoBidirectionalSwapKernel::ObjectiveForCohort(CohortKey);
      const uint64 FlowResourceId =
        FCrowdDemoBidirectionalSwapKernel::FlowResourceForCohort(CohortKey);
      const FCrowdDemoSharedFlowFieldConfig FlowConfig =
        FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortKey);
      if (!Flow || !Flow->IsValid() || !ObjectiveRef.IsValid()
        || !CrowdWorkerResourceIds::IsFlowResource(FlowResourceId)
        || FlowConfig.Revision <= 0)
        return RejectWorkerV2Input(TEXT("flow_fixture_resource"));

      FCrowdWorkerNavigationObjectiveResource Objective;
      Objective.GoalLocation = FVector(FlowConfig.GoalLocation);
      FCrowdWorkerPayload ObjectiveIdentity;
      if (!FCrowdWorkerNavigationObjectiveResourceCodec::Encode(
          Objective, ObjectiveIdentity))
        return RejectWorkerV2Input(TEXT("flow_fixture_objective_encode"));
      const uint64 ObjectiveResourceId = ObjectiveRef.ResolveResourceId();
      uint64 ObjectiveRevision = 0;
      bool bPublishObjective = false;
      if (!RuntimeSubsystem->ResolveWorkerResourceRevision(
          ObjectiveResourceId,
          static_cast<uint64>(FlowConfig.Revision),
          ObjectiveIdentity.StableHash,
          ObjectiveRevision, bPublishObjective))
        return RejectWorkerV2Input(TEXT("flow_fixture_objective_revision"));
      if (bPublishObjective)
      {
        FCrowdWorkerVersionedResourceInput& ObjectiveInput =
          ResourceInputs.AddDefaulted_GetRef();
        ObjectiveInput.ResourceId = ObjectiveResourceId;
        ObjectiveInput.Revision = ObjectiveRevision;
        ObjectiveInput.Payload = MoveTemp(ObjectiveIdentity);
      }

      FCrowdWorkerPayload FlowIdentity;
      if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
          *Flow, FlowIdentity))
        return RejectWorkerV2Input(TEXT("flow_fixture_flow_encode"));
      uint64 FlowRevision = 0;
      bool bPublishFlow = false;
      if (!RuntimeSubsystem->ResolveWorkerResourceRevision(
          FlowResourceId,
          static_cast<uint64>(Flow->Config.Revision),
          FlowIdentity.StableHash,
          FlowRevision, bPublishFlow)
        || FlowRevision > static_cast<uint64>(MAX_int32))
        return RejectWorkerV2Input(TEXT("flow_fixture_flow_revision"));
      if (bPublishFlow)
      {
        FCrowdSharedFlowField PublishedFlow = *Flow;
        PublishedFlow.Config.Revision = static_cast<int32>(FlowRevision);
        FCrowdWorkerVersionedResourceInput& FlowInput =
          ResourceInputs.AddDefaulted_GetRef();
        FlowInput.ResourceId = FlowResourceId;
        FlowInput.Revision = FlowRevision;
        if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
            PublishedFlow, FlowInput.Payload))
          return RejectWorkerV2Input(
            TEXT("flow_fixture_flow_publish_encode"));
      }
    }
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
  const uint64 TargetObjectiveSemanticHash = bRequiresTargetObjective
    ? CalculateTargetObjectiveSemanticHash(GetTargetFact()) : 0;
  const bool bPublishTargetObjective = bRequiresTargetObjective
    && (!bSubmitIntentOnly
      || TargetObjectiveSemanticHash
        != LastWorkerV2TargetObjectiveSemanticHash);
  if (bPublishTargetObjective)
  {
    FCrowdWorkerObjectiveRevisionDelta& Objective =
      TargetObjectives.AddDefaulted_GetRef();
    if (!BuildTargetObjectiveRevisionDelta(
        GetTargetFact(), GetCurrentStepEndServerTimeSeconds(),
        GetCurrentFixedStepSeconds(),
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
  if (!bSubmitIntentOnly || bMovementTargetActivityChanged)
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
  TArray<FCrowdWorkerExternalGameplayInput> FlowBindingInputs;
  uint64 FlowBindingMappingHash = 14695981039346656037ull;
  if (bPublishMovementControl
    && !ExplicitFlowFixtureByAgentId.IsEmpty())
  {
    FlowBindingInputs.Reserve(ExplicitFlowFixtureByAgentId.Num());
    for (const FCrowdDemoBidirectionalSwapLayoutAgent& LayoutAgent :
      BidirectionalSwapLayout.Agents)
    {
      const FCrowdStableEntityRef* EntityRef =
        EntityRefByAgentId.Find(LayoutAgent.AgentId);
      if (!EntityRef)
        return RejectWorkerV2Input(TEXT("flow_binding_entity"));
      FCrowdWorkerFlowBinding Binding;
      Binding.EntityRef = *EntityRef;
      Binding.ObjectiveRef = LayoutAgent.ObjectiveRef;
      Binding.CohortKey = LayoutAgent.CohortKey;
      Binding.FlowResourceId = LayoutAgent.FlowResourceId;
      FCrowdWorkerExternalGameplayInput& Input =
        FlowBindingInputs.AddDefaulted_GetRef();
      Input.EntityRef = *EntityRef;
      Input.InputTypeId = static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::FlowBindingRevision);
      Input.DirtyMask = 1;
      if (!FCrowdWorkerFlowBindingCodec::Encode(
          Binding, Input.FullState))
        return RejectWorkerV2Input(TEXT("flow_binding_encode"));
      FlowBindingMappingHash ^= Input.FullState.StableHash;
      FlowBindingMappingHash *= 1099511628211ull;
    }
    FlowBindingInputs.Sort([](
      const FCrowdWorkerExternalGameplayInput& A,
      const FCrowdWorkerExternalGameplayInput& B)
    {
      return A.EntityRef < B.EntityRef;
    });
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
  if (bSubmitIntentOnly && !MovementProfileInputs.IsEmpty())
    PlanRevisionInputs.Append(MoveTemp(MovementProfileInputs));
  if (bSubmitIntentOnly && !FlowBindingInputs.IsEmpty())
    PlanRevisionInputs.Append(MoveTemp(FlowBindingInputs));
  TArray<FCrowdWorkerExternalGameplayInput> BootstrapExternalInputs;
  if (!bSubmitIntentOnly)
  {
    BootstrapExternalInputs.Append(MovementProfileInputs);
    BootstrapExternalInputs.Append(FlowBindingInputs);
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
        ResourceInputs, nullptr, BootstrapExternalInputs);
  if (!bWorkerInputAccepted)
    return RejectWorkerV2Input(TEXT("boundary_submit"));
  const uint64 AcceptedInputSequence =
    RuntimeSubsystem->GetWorkerShadowSync().GetMetrics().
      LastSubmittedInputSequence;
  if (AcceptedInputSequence == 0)
    return RejectWorkerV2Input(TEXT("accepted_sequence"));
  BoundaryFacingWorkState->WorkerV2InputSequence =
    AcceptedInputSequence;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoWorkerBootstrapInputAccepted step=%d input_sequence=%llu worker_native_vat=%d movement_resource=%d target_resource=%d combat_resource=%d source=WorkerInputSync"),
    GetCurrentFixedStepIndex(), AcceptedInputSequence,
    bWorkerNativeVatBootstrap ? 1 : 0,
    bPublishMovementControl ? 1 : 0,
    bPublishTargetControl ? 1 : 0,
    bPublishProjectileControl ? 1 : 0);
  if (!ExplicitFlowFixtureByAgentId.IsEmpty()
    && bPublishMovementControl)
  {
    const FCrowdSharedFlowField* NorthFlow =
      FindRuntimeBidirectionalSwapFlowField(
        FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey);
    const FCrowdSharedFlowField* SouthFlow =
      FindRuntimeBidirectionalSwapFlowField(
        FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey);
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoT3FlowBindingCheckpoint bindings=%d cohort_keys=%u,%u objective_refs=%llu,%llu flow_resources=%llu,%llu flow_revisions=%d,%d flow_hashes=%u,%u mapping_hash=%llu authoritative_preferred_velocity=0 worker_planning_sample=1 t3_bypass=0 formation_flow_selection=0 source=GenericFlowBinding"),
      ExplicitFlowFixtureByAgentId.Num(),
      FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
      FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey,
      FCrowdDemoBidirectionalSwapKernel::NorthObjectiveId,
      FCrowdDemoBidirectionalSwapKernel::SouthObjectiveId,
      FCrowdDemoBidirectionalSwapKernel::NorthFlowResourceId,
      FCrowdDemoBidirectionalSwapKernel::SouthFlowResourceId,
      NorthFlow ? NorthFlow->Config.Revision : 0,
      SouthFlow ? SouthFlow->Config.Revision : 0,
      NorthFlow ? NorthFlow->BuildHash : 0,
      SouthFlow ? SouthFlow->BuildHash : 0,
      FlowBindingMappingHash);
  }
  if (bPublishMovementControl
    && (IsOpenSpawnRelaxation() || IsValidCorridorTransit()
      || bVatShowcase))
  {
    WorkerScenarioMovementProfiles.Reset();
    WorkerScenarioFrozenProfiles.Reset();
    for (const FCrowdWorkerMovementControlEntry& Entry :
      Control.Entries)
    {
      WorkerScenarioMovementProfiles.Add(Entry.EntityRef, Entry);
      if (Entry.bFreezeAtBoundaryLocation)
        WorkerScenarioFrozenProfiles.Add(Entry.EntityRef);
    }
    if (bVatShowcase)
      VatShowcaseLastMovementHalfCycle = 0;
  }

  if (bHasTargetControl)
  {
    bWorkerV2TargetStateBootstrapped = true;
  }
  if (bRequiresTargetObjective)
  {
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
    bLastWorkerV2MovementControlTargetActive =
      bHasTargetControl;
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
  SubmitPreparedWorkerBootstrapInput()
{
  check(IsInGameThread());
  if (!IsFullWorkerProductionMode()
    || !BoundaryFacingWorkState.IsValid()
    || !BoundaryFacingWorkState->bCompleted
    || bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence != 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerBootstrapRequiresFullProduction step=%d prepared=%d"),
      GetCurrentFixedStepIndex(),
      BoundaryFacingWorkState.IsValid()
        && BoundaryFacingWorkState->bCompleted ? 1 : 0);
    return false;
  }
  if (!SubmitWorkerV2BoundaryInput())
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerBootstrapResourceSubmitRejected step=%d"),
      GetCurrentFixedStepIndex());
    return false;
  }
  const uint64 AcceptedInputSequence =
    BoundaryFacingWorkState->WorkerV2InputSequence;
  if (AcceptedInputSequence == 0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerBootstrapSequenceMissing step=%d"),
      GetCurrentFixedStepIndex());
    return false;
  }
  CurrentStepFullWorkerInputSequence = AcceptedInputSequence;
  bCurrentStepFullWorkerProductionFastPath = true;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoWorkerBootstrapCutover step=%d input_sequence=%llu source=WorkerInputSync"),
    GetCurrentFixedStepIndex(), AcceptedInputSequence);
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  CanUseFullWorkerProductionFastPath() const
{
  if (!IsFullWorkerProductionMode()
    || !bPlanActive || !bStepInProgress
    || bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence != 0
    || !IsBoundarySnapshotCurrent() || !GetWorld()
    || GetWorld()->GetNetMode() == NM_Client)
    return false;

  const UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
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
      != GetCurrentPlanRevision()
    || RuntimeSubsystem->GetWorkerMovementAuthority().GetMode()
      != ECrowdWorkerMovementAuthorityMode::Production
    || RuntimeSubsystem->GetWorkerBehaviorAuthority().GetMode()
      != ECrowdWorkerBehaviorAuthorityMode::Production)
    return false;

  const bool bTargetActive = IsTargetRegionExecutionActive();
  const bool bProjectileActive =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && (ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
      || (ActivePlan.Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
        && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0));
  return (!bTargetActive || bWorkerV2TargetStateBootstrapped)
    && (!bProjectileActive || bWorkerV2ProjectileStateBootstrapped);
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  TrySubmitFullWorkerProductionIntent()
{
  check(IsInGameThread());
  if (!CanUseFullWorkerProductionFastPath())
    return false;

  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  const FCrowdWorkerBoundaryShadowSync& WorkerShadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  const bool bTargetActive = IsTargetRegionExecutionActive();
  const bool bProjectileActive =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && (ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse
      || (ActivePlan.Rules.SoftPressureTestCase
          == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
        && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0));

  const bool bDynamicTargetFlow = bTargetActive
    && (ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::PursuitAndSettleMoving
      || ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving);
  if (bDynamicTargetFlow
    && !EnsureDynamicSharedFlowField(
      ActivePlan.Rules.FlowFieldConfig,
      FVector(
        GetTargetFact().Location.X,
        GetTargetFact().Location.Y,
        ActivePlan.Rules.FlowFieldConfig.GoalLocation.Z)))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFullWorkerDynamicFlowRefreshFailed step=%d"),
      GetCurrentFixedStepIndex());
    return false;
  }

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
        GetTargetFact(), GetCurrentStepEndServerTimeSeconds(),
        GetCurrentFixedStepSeconds(),
        NextWorkerV2TargetObjectiveRevision, Objective))
      return false;
  }

  FCrowdDemoOpenSpawnRelaxationRuntime CandidateOpenSpawnRuntime =
    OpenSpawnRelaxationRuntime;
  TMap<FCrowdStableEntityRef, FCrowdWorkerMovementControlEntry>
    CandidateScenarioProfiles = WorkerScenarioMovementProfiles;
  TSet<FCrowdStableEntityRef> CandidateFrozenProfiles;
  TArray<int32> ConsumedOpenSpawnResetAgentIds;
  TArray<FCrowdWorkerExternalGameplayInput> ScenarioInputs;
  int32 ScenarioCommandTick = INDEX_NONE;
  int32 CandidateVatMovementHalfCycle =
    VatShowcaseLastMovementHalfCycle;
  const bool bVatShowcase =
    ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse;
  if (IsOpenSpawnRelaxation())
  {
    if (CandidateScenarioProfiles.Num() != BoundarySnapshot.Agents.Num()
      || LastScenarioObservationAbsoluteTick == INDEX_NONE
      || OpenSpawnScenarioAbsoluteOriginTick == INDEX_NONE)
      return false;

    const int64 AbsoluteCommandTick =
      LastScenarioObservationAbsoluteTick + 1;
    const int64 ScenarioCommandTick64 = AbsoluteCommandTick
      - OpenSpawnScenarioAbsoluteOriginTick;
    if (ScenarioCommandTick64 < OpenSpawnScenarioLastCommandTick
      || ScenarioCommandTick64 > MAX_int32)
      return false;
    ScenarioCommandTick = static_cast<int32>(ScenarioCommandTick64);
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(
      ScenarioCommandTick, OpenSpawnRelaxationLayout,
      CandidateOpenSpawnRuntime);

    TMap<FCrowdStableEntityRef, FCrowdWorkerMovementControlEntry>
      ChangedProfiles;
    for (const FCrowdStableEntityRef& EntityRef :
      WorkerScenarioFrozenProfiles)
    {
      FCrowdWorkerMovementControlEntry* Profile =
        CandidateScenarioProfiles.Find(EntityRef);
      if (!Profile) return false;
      Profile->bFreezeAtBoundaryLocation = false;
      ChangedProfiles.Add(EntityRef, *Profile);
    }
    for (const FCrowdDemoOpenSpawnRelaxationAgentState& State :
      CandidateOpenSpawnRuntime.Agents)
    {
      const FCrowdMassBoundaryAgentRecord* BoundaryAgent =
        BoundarySnapshot.Agents.FindByPredicate(
          [&State](const FCrowdMassBoundaryAgentRecord& Agent)
          {
            return Agent.Identity.AgentId == State.AgentId;
          });
      if (!BoundaryAgent) return false;
      FCrowdWorkerMovementControlEntry* Profile =
        CandidateScenarioProfiles.Find(
          BoundaryAgent->AgentFacts.StableEntityRef);
      if (!Profile) return false;
      if (Profile->bParticleActive != State.bParticleActive
        || State.bPendingBoundaryReset)
      {
        Profile->bParticleActive = State.bParticleActive;
        Profile->bFreezeAtBoundaryLocation =
          State.bPendingBoundaryReset;
        Profile->BoundaryLocation = State.BoundaryResetLocation;
        ChangedProfiles.Add(Profile->EntityRef, *Profile);
      }
      if (State.bPendingBoundaryReset)
      {
        CandidateFrozenProfiles.Add(Profile->EntityRef);
        ConsumedOpenSpawnResetAgentIds.Add(State.AgentId);
      }
    }
    TArray<FCrowdStableEntityRef> ChangedRefs;
    ChangedProfiles.GetKeys(ChangedRefs);
    ChangedRefs.Sort();
    ScenarioInputs.Reserve(ChangedRefs.Num());
    for (const FCrowdStableEntityRef& EntityRef : ChangedRefs)
    {
      const FCrowdWorkerMovementControlEntry* Profile =
        ChangedProfiles.Find(EntityRef);
      if (!Profile) return false;
      FCrowdWorkerExternalGameplayInput& Input =
        ScenarioInputs.AddDefaulted_GetRef();
      Input.EntityRef = EntityRef;
      Input.InputTypeId = static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::
          MovementProfileRevision);
      Input.DirtyMask = 1;
      if (!FCrowdWorkerMovementProfileCodec::Encode(
          *Profile, Input.FullState))
        return false;
    }
    int32 ActiveParticipantCount = 0;
    for (const FCrowdDemoOpenSpawnRelaxationAgentState& State :
      CandidateOpenSpawnRuntime.Agents)
      ActiveParticipantCount += State.bParticleActive ? 1 : 0;
    if (!ScenarioInputs.IsEmpty())
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoScenarioControlCheckpoint scenario=T1 worker_absolute_tick=%lld scenario_tick=%d profiles=%d active=%d phase=%d source=WorkerInputSync"),
        AbsoluteCommandTick, ScenarioCommandTick,
        ScenarioInputs.Num(), ActiveParticipantCount,
        static_cast<int32>(CandidateOpenSpawnRuntime.Phase));
    }
  }
  else if (bVatShowcase)
  {
    if (CandidateScenarioProfiles.Num() != BoundarySnapshot.Agents.Num()
      || LastScenarioObservationAbsoluteTick == INDEX_NONE
      || VatShowcaseScenarioAbsoluteOriginTick == INDEX_NONE)
      return false;
    const int64 AbsoluteCommandTick =
      LastScenarioObservationAbsoluteTick + 1;
    const int64 ScenarioTick = AbsoluteCommandTick
      - VatShowcaseScenarioAbsoluteOriginTick;
    FCrowdDemoVatMotionSettings MotionSettings;
    if (ScenarioTick < 0 || ScenarioTick > MAX_int32
      || MotionSettings.HalfCycleFixedSteps <= 0)
      return false;
    CandidateVatMovementHalfCycle =
      static_cast<int32>(ScenarioTick)
      / MotionSettings.HalfCycleFixedSteps;
    if (CandidateVatMovementHalfCycle
        > VatShowcaseLastMovementHalfCycle)
    {
      const float Direction =
        (CandidateVatMovementHalfCycle & 1) == 0
        ? 1.0f : -1.0f;
      for (const FCrowdDemoRoundBoundaryBusinessFact& Fact :
        BoundaryBusinessFacts)
      {
        if (FCrowdDemoVatShowcasePlanner::ResolveInitialState(
            Fact.FormationIndex)
          != ECrowdDemoVatPlannedState::Moving)
          continue;
        FCrowdWorkerMovementControlEntry* Profile =
          CandidateScenarioProfiles.Find(Fact.EntityRef);
        if (!Profile) return false;
        Profile->AutonomousPreferredVelocity = FVector(
          Direction * MotionSettings.MoveSpeedCmps,
          0.0f, 0.0f);
        Profile->bUseWorkerTargetGuidance = false;
        Profile->bUseAuthoritativePreferredVelocity = true;
        FCrowdWorkerExternalGameplayInput& Input =
          ScenarioInputs.AddDefaulted_GetRef();
        Input.EntityRef = Fact.EntityRef;
        Input.InputTypeId = static_cast<uint16>(
          ECrowdWorkerExternalGameplayInputType::
            MovementProfileRevision);
        Input.DirtyMask = 1;
        if (!FCrowdWorkerMovementProfileCodec::Encode(
            *Profile, Input.FullState))
          return false;
      }
      ScenarioInputs.Sort([](
        const FCrowdWorkerExternalGameplayInput& A,
        const FCrowdWorkerExternalGameplayInput& B)
      {
        return A.EntityRef < B.EntityRef;
      });
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoScenarioControlCheckpoint scenario=T7 worker_absolute_tick=%lld scenario_tick=%lld command=movement_half_cycle half_cycle=%d profiles=%d source=WorkerInputSync"),
        AbsoluteCommandTick, ScenarioTick,
        CandidateVatMovementHalfCycle, ScenarioInputs.Num());
    }
  }
  else if (IsValidCorridorTransit()
    && FCrowdDemoValidCorridorTransitKernel::
      ShouldHoldCompletedGroup(ValidCorridorTransitProgress)
    && !bValidCorridorTransitHoldCommandSubmitted)
  {
    if (CandidateScenarioProfiles.Num() != BoundarySnapshot.Agents.Num())
      return false;
    TArray<FCrowdStableEntityRef> EntityRefs;
    CandidateScenarioProfiles.GetKeys(EntityRefs);
    EntityRefs.Sort();
    ScenarioInputs.Reserve(EntityRefs.Num());
    for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
    {
      FCrowdWorkerMovementControlEntry* Profile =
        CandidateScenarioProfiles.Find(EntityRef);
      if (!Profile) return false;
      Profile->AutonomousPreferredVelocity = FVector::ZeroVector;
      Profile->bUseWorkerTargetGuidance = false;
      Profile->bUseAuthoritativePreferredVelocity = true;
      FCrowdWorkerExternalGameplayInput& Input =
        ScenarioInputs.AddDefaulted_GetRef();
      Input.EntityRef = EntityRef;
      Input.InputTypeId = static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::
          MovementProfileRevision);
      Input.DirtyMask = 1;
      if (!FCrowdWorkerMovementProfileCodec::Encode(
          *Profile, Input.FullState))
        return false;
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoScenarioControlCheckpoint scenario=T4 worker_absolute_tick=%lld command=group_exit_hold profiles=%d source=WorkerInputSync"),
      LastScenarioObservationAbsoluteTick + 1,
      ScenarioInputs.Num());
  }

  const uint64 PreviousInputSequence =
    WorkerShadow.GetMetrics().LastSubmittedInputSequence;
  CurrentBoundaryRequestStartSeconds = FPlatformTime::Seconds();
  if (!FCrowdDemoWorkerInputSync::SubmitIntentBatch(
      *GetWorld(), GetCurrentFixedStepIndex(),
      GetCurrentPlanRevision(),
      GetCurrentStepEndServerTimeSeconds(), {}, {}, {}, ScenarioInputs,
      nullptr,
      TargetObjectives))
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoFullWorkerProductionIntentRejected step=%d previous_sequence=%llu"),
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
      TEXT("VIOLATION CrowdDemoFullWorkerProductionIntentSequence step=%d previous_sequence=%llu accepted_sequence=%llu"),
      GetCurrentFixedStepIndex(), PreviousInputSequence,
      AcceptedInputSequence);
    return false;
  }
  if (IsOpenSpawnRelaxation())
  {
    if (!ConsumedOpenSpawnResetAgentIds.IsEmpty()
      && !FCrowdDemoOpenSpawnRelaxationKernel::
        ConsumePendingBoundaryResets(
          ConsumedOpenSpawnResetAgentIds,
          CandidateOpenSpawnRuntime))
      return false;
    OpenSpawnRelaxationRuntime = MoveTemp(CandidateOpenSpawnRuntime);
    WorkerScenarioMovementProfiles =
      MoveTemp(CandidateScenarioProfiles);
    WorkerScenarioFrozenProfiles =
      MoveTemp(CandidateFrozenProfiles);
    OpenSpawnScenarioLastCommandTick = ScenarioCommandTick;
  }
  else if (IsValidCorridorTransit() && !ScenarioInputs.IsEmpty())
  {
    WorkerScenarioMovementProfiles =
      MoveTemp(CandidateScenarioProfiles);
    bValidCorridorTransitHoldCommandSubmitted = true;
  }
  else if (bVatShowcase && !ScenarioInputs.IsEmpty())
  {
    WorkerScenarioMovementProfiles =
      MoveTemp(CandidateScenarioProfiles);
    VatShowcaseLastMovementHalfCycle =
      CandidateVatMovementHalfCycle;
  }

  CurrentStepFullWorkerInputSequence = AcceptedInputSequence;
  bCurrentStepFullWorkerProductionFastPath = true;
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
      TEXT("CrowdDemoFullWorkerProductionFastPathCheckpoint submitted=%llu input_sequence=%llu simulation_tick=%d generation=%llu plan_revision=%d objective_published=%llu objective_reused=%llu source=PersistentRuntimeAuthority"),
      WorkerV2EarlyClockIntentCount, AcceptedInputSequence,
      GetCurrentFixedStepIndex(), WorkerShadow.GetGeneration(),
      GetCurrentPlanRevision(), WorkerV2TargetObjectivePublishCount,
      WorkerV2TargetObjectiveReuseCount);
  }
  return true;
}

void UCrowdDemoRoundSimPipelineSubsystem::
  ObserveCommittedWorkerScenarioState(
    const FCrowdWorkerResultApplyProxy& Proxy,
    const uint64 Generation,
    const uint64 PublishSequence,
    const int64 AbsoluteSimulationTick)
{
  check(IsInGameThread());
  if (Generation == 0 || PublishSequence == 0
    || AbsoluteSimulationTick < 0
    || AbsoluteSimulationTick > MAX_int32)
    return;
  if (Generation < LastScenarioObservationGeneration)
    return;
  if (Generation > LastScenarioObservationGeneration)
  {
    LastScenarioObservationGeneration = Generation;
    LastScenarioObservationPublishSequence = 0;
    LastScenarioObservationAbsoluteTick = INDEX_NONE;
  }
  if (PublishSequence <= LastScenarioObservationPublishSequence
    || (LastScenarioObservationAbsoluteTick != INDEX_NONE
      && AbsoluteSimulationTick
        < LastScenarioObservationAbsoluteTick))
    return;

  const auto DecodeMovement = [&Proxy](
    const FCrowdStableEntityRef& EntityRef,
    FCrowdWorkerMovementState& OutState)
  {
    const FCrowdWorkerDomainProxyState* Domain =
      Proxy.FindDomain(EntityRef, ECrowdWorkerField::Facing);
    if (!Domain)
      Domain = Proxy.FindDomain(
        EntityRef, ECrowdWorkerField::Movement);
    return Domain && FCrowdWorkerMovementStateCodec::Decode(
      Domain->State.Payload, OutState);
  };

  bool bObserved = false;
  const int32 FixedStepIndex =
    static_cast<int32>(AbsoluteSimulationTick);
  if (IsOpenCohortMovement())
  {
    TArray<FCrowdDemoTargetRegionGuidanceResult> Results;
    Results.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      const FCrowdWorkerDomainProxyState* Domain = Proxy.FindDomain(
        Agent.AgentFacts.StableEntityRef,
        ECrowdWorkerField::Target);
      FCrowdWorkerTargetState State;
      if (!Domain || !FCrowdWorkerTargetStateCodec::Decode(
          Domain->State.Payload, State))
      {
        Results.Reset();
        break;
      }
      FCrowdDemoTargetRegionGuidanceResult& Result =
        Results.AddDefaulted_GetRef();
      Result.AgentId = Agent.Identity.AgentId;
      Result.CurrentCellKey = State.CurrentCellKey;
      Result.NextCellKey = State.NextCellKey;
      Result.DemandRegionKey = State.DemandRegionKey;
      Result.Mode = static_cast<ECrowdDemoTargetRegionGuidanceMode>(
        State.Mode);
      Result.DesiredVelocity = FVector2f(
        State.DesiredVelocity.X, State.DesiredVelocity.Y);
    }
    if (!Results.IsEmpty())
    {
      FCrowdDemoOpenCohortMovementKernel::UpdateProgress(
        Results, OpenCohortMovementLayout.Agents.Num(),
        FixedStepIndex, OpenCohortMovementProgress);
      bObserved = true;
    }
  }
  else if (IsBidirectionalSwap())
  {
    TArray<FCrowdDemoBidirectionalSwapStepAgent> Results;
    Results.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      const FCrowdDemoBidirectionalSwapLayoutAgent* LayoutAgent =
        BidirectionalSwapLayout.Agents.FindByPredicate(
          [&Agent](const FCrowdDemoBidirectionalSwapLayoutAgent& Value)
          {
            return Value.AgentId == Agent.Identity.AgentId;
          });
      FCrowdWorkerMovementState State;
      const FCrowdDemoSharedFlowField* Field = LayoutAgent
        ? FindBidirectionalSwapFlowField(LayoutAgent->CohortKey) : nullptr;
      if (!LayoutAgent || !Field || !DecodeMovement(
          Agent.AgentFacts.StableEntityRef, State))
      {
        Results.Reset();
        break;
      }
      FCrowdDemoBidirectionalSwapStepAgent& Result =
        Results.AddDefaulted_GetRef();
      Result.AgentId = Agent.Identity.AgentId;
      Result.FormationIndex = LayoutAgent->FormationIndex;
      Result.CohortKey = LayoutAgent->CohortKey;
      Result.Location = State.Position;
      Result.Velocity = State.Velocity;
      Result.FlowStatus = FCrowdDemoSharedFlowFieldKernel::Sample(
        *Field, State.Position).Status;
    }
    if (!Results.IsEmpty())
    {
      FCrowdDemoBidirectionalSwapKernel::UpdateProgress(
        Results, FixedStepIndex, BidirectionalSwapProgress);
      bObserved = true;
    }
  }
  else if (IsCorridorTransitProgressScenario())
  {
    TArray<FCrowdDemoValidCorridorTransitStepAgent> Results;
    Results.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      FCrowdWorkerMovementState State;
      if (!DecodeMovement(
          Agent.AgentFacts.StableEntityRef, State))
      {
        Results.Reset();
        break;
      }
      FCrowdDemoValidCorridorTransitStepAgent& Result =
        Results.AddDefaulted_GetRef();
      Result.AgentId = Agent.Identity.AgentId;
      Result.Location = State.Position;
      Result.Velocity = State.Velocity;
      Result.FlowStatus = FCrowdDemoSharedFlowFieldKernel::Sample(
        SharedFlowField, State.Position).Status;
    }
    if (!Results.IsEmpty())
    {
      const bool bWasValid = ValidCorridorTransitProgress.bValid;
      FCrowdDemoValidCorridorTransitKernel::UpdateProgress(
        Results, FixedStepIndex, ValidCorridorTransitProgress);
      if (bWasValid && !ValidCorridorTransitProgress.bValid)
      {
        FString AgentIds;
        for (const FCrowdDemoValidCorridorTransitStepAgent& Result : Results)
          AgentIds += FString::Printf(TEXT("%s%d"),
            AgentIds.IsEmpty() ? TEXT("") : TEXT(","), Result.AgentId);
        UE_LOG(LogTemp, Error,
          TEXT("CrowdDemoCorridorProgressRejected scenario=%d fixed_step=%d agents=%d ids=[%s] source=WorkerResultApply"),
          static_cast<int32>(ActivePlan.Rules.SoftPressureTestCase),
          FixedStepIndex, Results.Num(), *AgentIds);
      }
      bObserved = true;
    }
  }
  else if (ActivePlan.Rules.Scenario
      == ECrowdDemoScenario::SimRoundSoftPressure
    && ActivePlan.Rules.SoftPressureTestCase
      == ECrowdDemoSoftPressureTestCase::MultiStateVatHitResponse)
  {
    int32 IdleCount = 0;
    int32 MovingCount = 0;
    int32 AttackingCount = 0;
    int32 HitReactCount = 0;
    int32 DeadCount = 0;
    int32 KnockbackCount = 0;
    int32 KnockUpCount = 0;
    int32 ValidCount = 0;
    TArray<FCrowdDemoCombatAgentState> CommittedStates;
    CommittedStates.Reserve(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      const FCrowdWorkerDomainProxyState* Domain = Proxy.FindDomain(
        Agent.AgentFacts.StableEntityRef,
        ECrowdWorkerField::Combat);
      FCrowdWorkerCombatState WorkerCombat;
      FCrowdDemoCombatAgentState State;
      if (!Domain
        || !FCrowdWorkerCombatStateCodec::Decode(
          Domain->State.Payload, WorkerCombat)
        || !FCrowdDemoWorkerCombatStatePayloadCodec::Decode(
          WorkerCombat.HostState, State)
        || State.AgentId != Agent.Identity.AgentId
        || State.LifecycleSerial != static_cast<int32>(
          Agent.AgentFacts.StableEntityRef.LifecycleSerial))
      {
        ValidCount = 0;
        break;
      }
      ++ValidCount;
      IdleCount += State.BusinessState
        == ECrowdDemoBusinessState::Idle ? 1 : 0;
      MovingCount += State.BusinessState
        == ECrowdDemoBusinessState::Moving ? 1 : 0;
      AttackingCount += State.BusinessState
        == ECrowdDemoBusinessState::Attacking ? 1 : 0;
      HitReactCount += State.BusinessState
        == ECrowdDemoBusinessState::HitReact ? 1 : 0;
      DeadCount += State.BusinessState
        == ECrowdDemoBusinessState::Dead ? 1 : 0;
      KnockbackCount += State.ReactiveMode
        == ECrowdDemoReactiveMotionMode::Knockback ? 1 : 0;
      KnockUpCount += State.ReactiveMode
        == ECrowdDemoReactiveMotionMode::KnockUp ? 1 : 0;
      CommittedStates.Add(State);
    }
    if (ValidCount == BoundarySnapshot.Agents.Num()
      && ValidCount > 0)
    {
      if (VatShowcaseScenarioAbsoluteOriginTick == INDEX_NONE)
      {
        VatShowcaseScenarioAbsoluteOriginTick =
          AbsoluteSimulationTick;
      }
      const int64 ScenarioObservationTick = AbsoluteSimulationTick
        - VatShowcaseScenarioAbsoluteOriginTick;
      if (ScenarioObservationTick >= 0
        && ScenarioObservationTick <= MAX_int32)
      {
        const int32 PresentationFixedStep = static_cast<int32>(
          ScenarioObservationTick);
        const float PresentationServerTimeSeconds =
          ActivePlan.StartServerTimeSeconds
          + static_cast<float>(PresentationFixedStep + 1)
            * CurrentFixedStepSeconds;
        for (const FCrowdDemoCombatAgentState& State : CommittedStates)
        {
          FCrowdDemoCombatNetState Combat =
            BuildT7PresentationCombatNetState(State);
          const ECrowdDemoVisualState ProjectedVisualState =
            FCrowdDemoCombatStateKernel::ResolveVisualState(
              State, FVector::ZeroVector, true);
          if (Combat.VisualState != ProjectedVisualState)
          {
            Combat.VisualState = ProjectedVisualState;
            Combat.VisualStateStartServerTimeSeconds =
              PresentationServerTimeSeconds;
          }
          const uint32 Signature = BuildT7PresentationStateSignature(
            Combat, State.LifecycleSerial);
          const uint32* PreviousSignature =
            VatShowcaseLastPresentationSignatureByAgentId.Find(
              State.AgentId);
          if (PreviousSignature && *PreviousSignature == Signature)
            continue;
          VatShowcaseLastPresentationSignatureByAgentId.Add(
            State.AgentId, Signature);
          FCrowdDemoT7PresentationEvent& Event =
            OutgoingT7PresentationEvents.AddDefaulted_GetRef();
          Event.RoundId = ActivePlan.RoundId;
          Event.AgentId = State.AgentId;
          Event.LifecycleSerial = State.LifecycleSerial;
          Event.FixedStepIndex = PresentationFixedStep;
          Event.ServerTimeSeconds = PresentationServerTimeSeconds;
          Event.Combat = Combat;
        }
      }
      if (ScenarioObservationTick == 0
        || ScenarioObservationTick == 30
        || ScenarioObservationTick == 60
        || ScenarioObservationTick == 90)
      {
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoT7ObserverCheckpoint worker_absolute_tick=%lld scenario_tick=%lld valid=%d idle=%d moving=%d attacking=%d hit_react=%d dead=%d knockback=%d knockup=%d source=CommittedWorkerResult"),
          AbsoluteSimulationTick, ScenarioObservationTick,
          ValidCount, IdleCount, MovingCount, AttackingCount,
          HitReactCount, DeadCount, KnockbackCount, KnockUpCount);
      }
      bObserved = true;
    }
  }
  else if (IsOpenSpawnRelaxation())
  {
    TArray<int32> AgentIds;
    TArray<FVector> Locations;
    TMap<int32, FVector> ParticleOffsets;
    for (const FCrowdMassBoundaryAgentRecord& Agent :
      BoundarySnapshot.Agents)
    {
      FCrowdWorkerMovementState Movement;
      if (!DecodeMovement(
          Agent.AgentFacts.StableEntityRef, Movement))
      {
        AgentIds.Reset();
        break;
      }
      AgentIds.Add(Agent.Identity.AgentId);
      Locations.Add(Movement.Position);
      const FCrowdWorkerDomainProxyState* ParticleDomain =
        Proxy.FindDomain(Agent.AgentFacts.StableEntityRef,
          ECrowdWorkerField::Particle);
      FCrowdWorkerParticleState Particle;
      if (ParticleDomain
        && FCrowdWorkerParticleStateCodec::Decode(
          ParticleDomain->State.Payload, Particle))
        ParticleOffsets.Add(
          Agent.Identity.AgentId, Particle.PositionOffset);
    }
    if (!AgentIds.IsEmpty())
    {
      if (OpenSpawnScenarioAbsoluteOriginTick == INDEX_NONE)
      {
        OpenSpawnScenarioAbsoluteOriginTick =
          AbsoluteSimulationTick - OpenSpawnScenarioLastCommandTick;
      }
      TArray<float> OffsetSizes;
      TArray<float> SoftErrors;
      TArray<FCrowdDemoParticleSoftPairInfluence> Influences;
      TSet<int32> ActiveAgentIds;
      for (const FCrowdDemoOpenSpawnRelaxationAgentState& State :
        OpenSpawnRelaxationRuntime.Agents)
      {
        if (State.bParticleActive)
          ActiveAgentIds.Add(State.AgentId);
      }
      for (const TPair<int32, FVector>& Pair : ParticleOffsets)
      {
        if (ActiveAgentIds.Contains(Pair.Key))
          OffsetSizes.Add(Pair.Value.Size2D());
      }
      float MaxCorrection = 0.0f;
      for (const float Value : OffsetSizes)
        MaxCorrection = FMath::Max(MaxCorrection, Value);
      const float SoftPairDistance =
        ActivePlan.Rules.ParticleProfile.PhysicalRadiusCm * 2.0f
        + ActivePlan.Rules.ParticleProfile.HardSafetyGapCm
        + ActivePlan.Rules.ParticleProfile.SoftMarginCm * 2.0f;
      for (int32 A = 0; A < AgentIds.Num(); ++A)
      {
        if (!ActiveAgentIds.Contains(AgentIds[A]))
          continue;
        const FVector OffsetA = ParticleOffsets.FindRef(AgentIds[A]);
        for (int32 B = A + 1; B < AgentIds.Num(); ++B)
        {
          if (!ActiveAgentIds.Contains(AgentIds[B]))
            continue;
          const FVector OffsetB = ParticleOffsets.FindRef(AgentIds[B]);
          const float Distance =
            FVector::Dist2D(Locations[A], Locations[B]);
          const float SoftError =
            FMath::Max(0.0f, SoftPairDistance - Distance);
          if (SoftError > 0.0f)
            SoftErrors.Add(SoftError);
          if ((OffsetA.IsNearlyZero() && OffsetB.IsNearlyZero())
            || Distance > SoftPairDistance + 1.0f)
            continue;
          FCrowdDemoParticleSoftPairInfluence& Influence =
            Influences.AddDefaulted_GetRef();
          Influence.MinAgentId = FMath::Min(
            AgentIds[A], AgentIds[B]);
          Influence.MaxAgentId = FMath::Max(
            AgentIds[A], AgentIds[B]);
          Influence.RealizedCorrectionA = OffsetA;
          Influence.RealizedCorrectionB = OffsetB;
        }
      }
      const float SoftP95 = Percentile(SoftErrors, 0.95f);
      FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
        FixedStepIndex, Influences, MaxCorrection, SoftP95,
        OpenSpawnRelaxationRuntime);
      FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
        AgentIds, Locations, OpenSpawnRelaxationRuntime);
      const int64 ScenarioObservationTick = AbsoluteSimulationTick
        - OpenSpawnScenarioAbsoluteOriginTick;
      if (ScenarioObservationTick >= 0
        && ScenarioObservationTick % 30 == 0)
      {
        int32 NonzeroOffsetCount = 0;
        for (const float Value : OffsetSizes)
          NonzeroOffsetCount += Value > KINDA_SMALL_NUMBER ? 1 : 0;
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoT1ObserverCheckpoint worker_absolute_tick=%lld scenario_tick=%lld phase=%d active=%d offsets=%d influences=%d cumulative_edges=%d layer_max=%d correction_max=%.3f soft_p95=%.3f settle_window=%d source=CommittedWorkerResult"),
          AbsoluteSimulationTick, ScenarioObservationTick,
          static_cast<int32>(OpenSpawnRelaxationRuntime.Phase),
          ActiveAgentIds.Num(), NonzeroOffsetCount, Influences.Num(),
          OpenSpawnRelaxationRuntime.CumulativeInfluenceEdges.Num(),
          OpenSpawnRelaxationRuntime.PressurePropagationLayerMax,
          MaxCorrection, SoftP95,
          OpenSpawnRelaxationRuntime.InsertSettling.
            ConsecutiveSettledSampleCount);
      }
      bObserved = true;
    }
  }

  if (bObserved)
  {
    LastScenarioObservationPublishSequence = PublishSequence;
    LastScenarioObservationAbsoluteTick = AbsoluteSimulationTick;
  }
}

bool UCrowdDemoRoundSimPipelineSubsystem::
  MarkFullWorkerProductionResultCommitted(
    const double CommitMilliseconds)
{
  check(IsInGameThread());
  if (!bStepInProgress
    || !bCurrentStepFullWorkerProductionFastPath
    || CurrentStepFullWorkerInputSequence == 0
    || !bCurrentStepWorkerDirtyMassApplied
    || CurrentStepMassAccessCounts.CommitWriteCount != 1)
    return false;
  ++FullWorkerProductionFastPathStepCount;
  if (FullWorkerProductionFastPathStepCount == 1
    || FullWorkerProductionFastPathStepCount % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoFullWorkerProductionCommitCheckpoint count=%llu step=%d input_sequence=%llu publish_sequence=%llu dirty_entities=%d commit_ms=%.3f source=WorkerResultApply"),
      FullWorkerProductionFastPathStepCount,
      GetCurrentFixedStepIndex(), CurrentStepFullWorkerInputSequence,
      CurrentStepWorkerDirtyMassPublishSequence,
      CurrentStepWorkerDirtyMassEntityCount, CommitMilliseconds);
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
  FCrowdDemoBootstrapSynchronousGraph BootstrapGraph;
  if (!IsInGameThread()
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

  if (State->bUseWorkerNativeScenarioBusiness)
  {
    State->BusinessOutput =
      BuildWorkerNativeScenarioBusinessBootstrap(
        State->BusinessInput);
    if (!State->BusinessOutput.bCompleted)
      return false;
    State->MovementShadowInput = State->GraphInput.Movement;
    State->bMovementShadowInputValid = true;
    State->bCompleted = true;
    return true;
  }

  const uint64 SnapshotHash = BoundarySnapshot.StableHash;
  const auto AddHashTask = [&BootstrapGraph, SnapshotHash](
    const FCrowdBootstrapTaskKey Key,
    const TConstArrayView<FCrowdBootstrapTaskKey> Prerequisites)
  {
    return BootstrapGraph.AddTask(
      Key, Prerequisites,
      [SnapshotHash, Key]
      {
        uint64 Hash = FoldBoundaryHash(SnapshotHash, 1);
        Hash = FoldBoundaryHash(
          Hash, Key.StageId.Value);
        Hash = FoldBoundaryHash(Hash, Key.TaskTypeId.Value);
        Hash = FoldBoundaryHash(Hash, Key.ScopeKey);
        return FCrowdBootstrapTaskResult::Success(Hash);
      },
      false);
  };
  const FCrowdBootstrapTaskKey Business = {{1}, {101}, 0};
  const FCrowdBootstrapTaskKey SharedFlow = {{2}, {201}, 0};
  const FCrowdBootstrapTaskKey Movement = {{3}, {301}, 0};
  const FCrowdBootstrapTaskKey Particle = {{4}, {401}, 0};
  const FCrowdBootstrapTaskKey Facing = {{5}, {501}, 0};
  TArray<FCrowdBootstrapTaskKey> MovementDeps = {Business, SharedFlow};
  const bool bHasTargetWork =
    !State->bUseWorkerV2Target
    && !State->TargetTopologySlots.IsEmpty();
  const FCrowdBootstrapTaskKey ParticleDeps[] = {Movement};
  const FCrowdBootstrapTaskKey FacingDeps[] = {Particle};
  bool bRegistered =
    BootstrapGraph.AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          State->bUseWorkerNativeScenarioBusiness
          ? BuildWorkerNativeScenarioBusinessBootstrap(
              State->BusinessInput)
          : RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBootstrapTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
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
          ? FCrowdBootstrapTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
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
      const FCrowdBootstrapTaskKey Topology = {
        {2}, {202}, CohortKey};
      const FCrowdBootstrapTaskKey Demand = {
        {2}, {203}, CohortKey};
      const FCrowdBootstrapTaskKey Plan = {
        {2}, {204}, CohortKey};
      const FCrowdBootstrapTaskKey Guidance = {
        {2}, {205}, CohortKey};
      const FCrowdBootstrapTaskKey DemandDeps[] = {
        SharedFlow, Topology};
      const FCrowdBootstrapTaskKey PlanDeps[] = {Demand};
      const FCrowdBootstrapTaskKey GuidanceDeps[] = {Plan};
      MovementDeps.Add(Guidance);
      bRegistered = BootstrapGraph.AddTask(
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
            ? FCrowdBootstrapTaskResult::Success(
                FoldBoundaryHash(
                  Slot.Output.Summary.TopologyHash,
                  Slot.CohortKey))
            : FCrowdBootstrapTaskResult::Failure();
        })
        && BootstrapGraph.AddTask(
          Demand, DemandDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bDemandStaged)
              return FCrowdBootstrapTaskResult::Failure();
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
                return FCrowdBootstrapTaskResult::Failure();
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
            if (!Slot.DemandOutput.bValid)
            {
              UE_LOG(LogTemp, Error,
                TEXT("CrowdDemoBootstrapTargetDemandRejected cohort=%u agents=%d external_agents=%d topology_valid=%d cells=%d feasible_cells=%d capacity=%d demand_states=%d feasible_regions=%d desired=%d assignable=%d overflow=%d supply=%d source_attachment_failures=%d membership_hash=%u demand_hash=%u reason=demand_invalid"),
                Slot.CohortKey,
                DemandInput.Agents.Num(),
                DemandInput.ExternalAgents.Num(),
                DemandInput.Topology.bValid ? 1 : 0,
                DemandInput.Topology.Cells.Num(),
                Slot.Output.Summary.FeasibleCellCount,
                Slot.Output.Summary.TotalFeasibleCapacity,
                Slot.DemandOutput.Demand.AgentStates.Num(),
                Slot.DemandOutput.Demand.FeasibleRegionCount,
                Slot.DemandOutput.Demand.DesiredPopulationTotal,
                Slot.DemandOutput.Demand.AssignablePopulation,
                Slot.DemandOutput.Demand.OverflowPopulation,
                Slot.DemandOutput.Demand.SupplyAgentCount,
                Slot.DemandOutput.Demand.SourceAttachmentFailureCount,
                Slot.DemandOutput.Demand.MembershipHash,
                Slot.DemandOutput.Demand.DemandHash);
              for (const FCrowdTargetRegionTransportAgent& Agent :
                DemandInput.Agents)
              {
                UE_LOG(LogTemp, Error,
                  TEXT("CrowdDemoBootstrapTargetDemandAgent cohort=%u agent=%d location=(%.3f,%.3f) velocity=(%.3f,%.3f) far_flow=(%.3f,%.3f) max_speed=%.3f radius=%.3f hard_gap=%.3f soft_margin=%.3f engaged_hold=%d"),
                  Slot.CohortKey, Agent.AgentId,
                  Agent.Location.X, Agent.Location.Y,
                  Agent.Velocity.X, Agent.Velocity.Y,
                  Agent.FarFlowPreferredVelocity.X,
                  Agent.FarFlowPreferredVelocity.Y,
                  Agent.MaxSpeedCmps,
                  Agent.PhysicalRadiusCm,
                  Agent.HardSafetyGapCm,
                  Agent.SoftMarginCm,
                  Agent.bEngagedHold ? 1 : 0);
              }
            }
            return Slot.DemandOutput.bValid
              ? FCrowdBootstrapTaskResult::Success(
                  Slot.DemandOutput.Demand.DemandHash)
              : FCrowdBootstrapTaskResult::Failure();
          })
        && BootstrapGraph.AddTask(
          Plan, PlanDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bPlanStaged)
              return FCrowdBootstrapTaskResult::Failure();
            FCrowdMassTargetRegionPlanInput PlanInput =
              Slot.PlanInput;
            PlanInput.Topology = Slot.Output.Topology;
            PlanInput.Demand = Slot.DemandOutput.Demand;
            Slot.PlanOutput =
              FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
            return Slot.PlanOutput.bValid
              ? FCrowdBootstrapTaskResult::Success(
                  FoldBoundaryHash(
                    Slot.PlanOutput.Plan.TransportHash,
                    Slot.PlanOutput.Execution.ExecutionHash))
              : FCrowdBootstrapTaskResult::Failure();
          })
        && BootstrapGraph.AddTask(
          Guidance, GuidanceDeps,
          [State, SlotIndex]
          {
            auto& Slot = State->TargetTopologySlots[SlotIndex];
            if (!Slot.bGuidanceStaged)
              return FCrowdBootstrapTaskResult::Failure();
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
                return FCrowdBootstrapTaskResult::Failure();
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
              ? FCrowdBootstrapTaskResult::Success(
                  Slot.GuidanceOutput.Summary.GuidanceHash)
              : FCrowdBootstrapTaskResult::Failure();
          });
    }
  }
  bRegistered = bRegistered
    && BootstrapGraph.AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdDemoRoundWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBootstrapTaskResult::Failure();
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
            return FCrowdBootstrapTaskResult::Failure();
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
            return FCrowdBootstrapTaskResult::Failure();
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
                return FCrowdBootstrapTaskResult::Failure();
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
            return FCrowdBootstrapTaskResult::Failure();
        }
        State->MovementShadowInput = MovementInput;
        State->bMovementShadowInputValid = true;
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(
            MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBootstrapTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      Particle, ParticleDeps,
      [State]
      {
        FCrowdMassParticlePipelineWorkInput ParticleInput;
        if (!FCrowdDemoRoundWorkGraph::BuildParticleInput(
            State->GraphInput, State->GraphOutput.Movement,
            ParticleInput))
          return FCrowdBootstrapTaskResult::Failure();
        State->GraphOutput.Particle =
          FCrowdMassParticlePipelineWork::Run(ParticleInput);
        return State->GraphOutput.Particle.bCompleted
          ? FCrowdBootstrapTaskResult::Success(
              State->GraphOutput.Particle.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdDemoRoundWorkGraph::BuildFacingInput(
            State->GraphInput, State->GraphOutput.Movement,
            State->GraphOutput.Particle, FacingInput))
          return FCrowdBootstrapTaskResult::Failure();
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
            return FCrowdBootstrapTaskResult::Failure();
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
          ? FCrowdBootstrapTaskResult::Success(
              State->Output.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      });
  if (!bRegistered || !BootstrapGraph.Run())
  {
    return false;
  }
  return true;
}

bool UCrowdDemoRoundSimPipelineSubsystem::DispatchBoundaryFacingWork(
  FCrowdMassFacingFinalizeWorkInput&& Input,
  TMap<int32, int32>&& ConsecutiveSettleStepsByAgentId,
  TMap<int32, bool>&& FinalSettledByAgentId)
{
  FCrowdDemoBootstrapSynchronousGraph BootstrapGraph;
  if (!IsInGameThread()
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
  const FCrowdBootstrapTaskKey Business = {{1}, {101}, 0};
  const FCrowdBootstrapTaskKey SharedFlow = {{2}, {201}, 0};
  const FCrowdBootstrapTaskKey Movement = {{3}, {301}, 0};
  const FCrowdBootstrapTaskKey Constraint = {{4}, {402}, 0};
  const FCrowdBootstrapTaskKey Facing = {{5}, {501}, 0};
  const FCrowdBootstrapTaskKey MovementDeps[] = {Business, SharedFlow};
  const FCrowdBootstrapTaskKey ConstraintDeps[] = {Movement};
  const FCrowdBootstrapTaskKey FacingDeps[] = {Constraint};
  const bool bRegistered =
    BootstrapGraph.AddTask(
      Business, {},
      [State]
      {
        State->BusinessOutput =
          State->bUseWorkerNativeScenarioBusiness
          ? BuildWorkerNativeScenarioBusinessBootstrap(
              State->BusinessInput)
          : RunBoundaryBusinessWork(State->BusinessInput);
        return State->BusinessOutput.bCompleted
          ? FCrowdBootstrapTaskResult::Success(
              State->BusinessOutput.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      SharedFlow, {},
      [State]
      {
        State->GraphOutput.SharedFlow =
          FCrowdMassSharedFlowWork::BuildPreferred(
            State->GraphInput.SharedFlow);
        return State->GraphOutput.SharedFlow.bValid
          ? FCrowdBootstrapTaskResult::Success(
              State->GraphOutput.SharedFlow.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      Movement, MovementDeps,
      [State]
      {
        FCrowdMassMovementPipelineWorkInput MovementInput;
        if (!FCrowdDemoRoundWorkGraph::BuildMovementInput(
            State->GraphInput, State->GraphOutput.SharedFlow,
            MovementInput))
          return FCrowdBootstrapTaskResult::Failure();
        TMap<int32, FCrowdMassGatherRecord*> RecordById;
        for (FCrowdMassGatherRecord& Record
          : MovementInput.Guidance.Records)
          RecordById.Add(Record.Identity.AgentId, &Record);
        for (const FCrowdGuidanceCandidate& Candidate
          : State->BusinessOutput.GuidanceCandidates)
        {
          FCrowdMassGatherRecord* const* Record =
            RecordById.Find(Candidate.AgentId);
          if (!Record) return FCrowdBootstrapTaskResult::Failure();
          (*Record)->Guidance.BusinessOverride = Candidate;
        }
        State->MovementShadowInput = MovementInput;
        State->bMovementShadowInputValid = true;
        State->GraphOutput.Movement =
          FCrowdMassMovementPipelineWork::Run(MovementInput);
        return State->GraphOutput.Movement.bCompleted
          ? FCrowdBootstrapTaskResult::Success(
              State->GraphOutput.Movement.StableHash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      Constraint, ConstraintDeps,
      [State]
      {
        const auto& Predicted =
          State->GraphOutput.Movement.MovementPredict.Results;
        if (Predicted.Num()
          != State->BusinessInput.Snapshot.Agents.Num())
          return FCrowdBootstrapTaskResult::Failure();
        TMap<int32, const FCrowdMassPredictedMovement*> ById;
        for (const FCrowdMassPredictedMovement& Value : Predicted)
        {
          if (!Value.bValid || ById.Contains(Value.AgentId))
            return FCrowdBootstrapTaskResult::Failure();
          ById.Add(Value.AgentId, &Value);
        }
        uint64 Hash = 14695981039346656037ull;
        State->ObstacleKinematics.Reset();
        for (const FCrowdMassBoundaryAgentRecord& Agent
          : State->BusinessInput.Snapshot.Agents)
        {
          const FCrowdMassPredictedMovement* const* Value =
            ById.Find(Agent.Identity.AgentId);
          if (!Value) return FCrowdBootstrapTaskResult::Failure();
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
          ? FCrowdBootstrapTaskResult::Success(Hash)
          : FCrowdBootstrapTaskResult::Failure();
      })
    && BootstrapGraph.AddTask(
      Facing, FacingDeps,
      [State]
      {
        FCrowdMassFacingFinalizeWorkInput FacingInput;
        if (!FCrowdDemoRoundWorkGraph::
          BuildFacingInputFromKinematics(
            State->GraphInput, State->BusinessInput.Snapshot,
            State->GraphOutput.Movement,
            State->ObstacleKinematics, FacingInput))
          return FCrowdBootstrapTaskResult::Failure();
        State->FacingShadowInput = FacingInput.Facing;
        State->Output =
          FCrowdMassFacingFinalizeWork::Run(FacingInput);
        State->GraphOutput.FacingFinalize = State->Output;
        State->bCompleted = State->Output.bCompleted;
        const uint64 Hash = FoldBoundaryHash(
          State->Output.Finalize.CommitPlan.StableHash,
          State->Output.Facing.StableHash);
        return State->bCompleted
          ? FCrowdBootstrapTaskResult::Success(Hash)
          : FCrowdBootstrapTaskResult::Failure();
      });
  if (!bRegistered || !BootstrapGraph.Run())
  {
    return false;
  }
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
  UMassCrowdRuntimeSubsystem* SharedFlowRuntimeSubsystem =
    GetWorld() ? GetWorld()->GetSubsystem<UMassCrowdRuntimeSubsystem>()
               : nullptr;
  if (!SharedFlowRuntimeSubsystem) return false;
  FCrowdMassSharedFlowBuildOutput Output;
  if (!SharedFlowRuntimeSubsystem->EnsureSharedFlowResource(
      Input, Output))
    return false;
  const FCrowdMassSharedFlowResource& RuntimeSharedFlowResource =
    SharedFlowRuntimeSubsystem->GetSharedFlowResource();
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
  if (Output.bIntegrationRebuilt)
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoDynamicSharedFlowCheckpoint step=%d anchor_cell=%d field_revision=%d build_hash=%u integration_hash=%u integration_rebuild_count=%d source=RuntimeSharedFlowOwner"),
      GetCurrentFixedStepIndex(), DynamicFlowAnchorCellKey,
      RuntimeSharedFlowResource.Field.Config.Revision,
      RuntimeSharedFlowResource.Field.BuildHash,
      RuntimeSharedFlowResource.Field.IntegrationHash,
      DynamicFlowIntegrationRebuildCount);
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


bool UCrowdDemoRoundSimPipelineSubsystem::EnsureBidirectionalSwapFlowResources()
{
  if (!IsBidirectionalSwap()) return false;
  bool bAllValid = true;
  const uint32 CohortKeys[] = {
    FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey,
    FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey};
  for (int32 CohortIndex = 0; CohortIndex < 2; ++CohortIndex)
  {
    const uint32 CohortKey = CohortKeys[CohortIndex];
    const FCrowdDemoSharedFlowFieldConfig Config =
      FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(CohortKey);
    FCrowdMassSharedFlowBuildInput Input;
    Input.Config = FCrowdDemoMassCrowdRuntimeAdapter::BuildCoreFlowConfig(Config);
    FCrowdMassSharedFlowResource& Resource =
      RuntimeBidirectionalSwapFlowResources[CohortIndex];
    const FCrowdMassSharedFlowBuildOutput Output =
      FCrowdMassSharedFlowWork::EnsureResource(Input, Resource);
    bAllValid &= Output.bValid;
    FCrowdDemoSharedFlowField& Field =
      BidirectionalSwapFlowFields[CohortIndex];
    if (Output.bValid && (Output.bFieldRebuilt || !Field.IsValid()
      || Field.BuildHash != Resource.Field.BuildHash))
      Field = FCrowdDemoMassCrowdRuntimeAdapter::BuildDemoFlowField(Resource.Field);
  }
  return bAllValid;
}

const FCrowdSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindRuntimeBidirectionalSwapFlowField(
  const uint32 CohortKey) const
{
  const int32 CohortIndex =
    CohortKey == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 0
    : CohortKey == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey ? 1
    : INDEX_NONE;
  return CohortIndex >= 0
      && CohortIndex < RuntimeBidirectionalSwapFlowResources.Num()
    ? &RuntimeBidirectionalSwapFlowResources[CohortIndex].Field : nullptr;
}

const FCrowdDemoSharedFlowField*
UCrowdDemoRoundSimPipelineSubsystem::FindBidirectionalSwapFlowField(
  const uint32 CohortKey) const
{
  const int32 CohortIndex =
    CohortKey == FCrowdDemoBidirectionalSwapKernel::NorthboundCohortKey ? 0
    : CohortKey == FCrowdDemoBidirectionalSwapKernel::SouthboundCohortKey ? 1
    : INDEX_NONE;
  return CohortIndex >= 0 && CohortIndex < BidirectionalSwapFlowFields.Num()
    ? &BidirectionalSwapFlowFields[CohortIndex] : nullptr;
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
  bCurrentStepFullWorkerProductionFastPath = false;
  CurrentStepFullWorkerInputSequence = 0;
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
      }
}

void UCrowdDemoRoundSimPipelineSubsystem::FailFixedStep()
{
  ++BoundaryGeneration;
  if (BoundaryGeneration == 0)
    BoundaryGeneration = 1;
  bStepInProgress = false;
  bPlanActive = false;
  bCurrentStepFullWorkerProductionFastPath = false;
  CurrentStepFullWorkerInputSequence = 0;
  CurrentBoundaryRequestStartSeconds = 0.0;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryFacingWorkState.Reset();
  PendingWorkerV2MovementExpectations.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  bLastWorkerV2MovementControlTargetActive = false;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
}

void UCrowdDemoRoundSimPipelineSubsystem::
InvalidateInFlightBoundaryForAuthoritativeState()
{
  check(IsInGameThread());
  const bool bDiscardedInFlight = bStepInProgress;
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
  bCurrentStepFullWorkerProductionFastPath = false;
  CurrentStepFullWorkerInputSequence = 0;
  CurrentBoundaryRequestStartSeconds = 0.0;
  BoundarySnapshot = {};
  WorkerProxySnapshotBaselineHash = 0;
  BoundaryFormationFacts.Reset();
  BoundaryFacingFacts.Reset();
  BoundaryBusinessFacts.Reset();
  BoundaryFacingWorkState.Reset();
  PendingWorkerV2MovementExpectations.Reset();
  ClearPreparedRoundCommitPlan();
  bWorkerV2TargetStateBootstrapped = false;
  bWorkerV2ProjectileStateBootstrapped = false;
  LastWorkerV2MovementControlGeneration = 0;
  LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  bLastWorkerV2MovementControlTargetActive = false;
  LastWorkerV2TargetControlSemanticHash = 0;
  LastWorkerV2TargetObjectiveSemanticHash = 0;
  LastWorkerV2ProjectileControlSemanticHash = 0;
  PreparedTargetRegionGuidanceCandidates.Reset();
  PreparedBusinessGuidanceCandidates.Reset();
  PreparedReactiveMotionSteps.Reset();
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

bool UCrowdDemoRoundSimPipelineSubsystem::DequeueT7PresentationEvents(
  TArray<FCrowdDemoT7PresentationEvent>& OutEvents)
{
  if (OutgoingT7PresentationEvents.IsEmpty())
    return false;
  OutEvents = MoveTemp(OutgoingT7PresentationEvents);
  OutgoingT7PresentationEvents.Reset();
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
