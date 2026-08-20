#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "CrowdDemoBusinessScenarioContract.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoSoftPressureRouteDiagnosticKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "MassCrowdSharedFlowWork.h"
#include "MassCrowdMovementPredictWork.h"
#include "MassCrowdFacingWork.h"
#include "MassCrowdFacingFinalizeWork.h"
#include "MassCrowdMovementFinalizeWork.h"
#include "CrowdParticleConstraintKernel.h"
#include "MassCrowdRuntimeBridge.h"
#include "MassCrowdMovementPipelineWork.h"
#include "MassCrowdParticlePipelineWork.h"
#include "MassCrowdTargetRegionWork.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerMovementControlResource.h"
#include "Mass/CrowdDemoTargetFactKernel.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"
#include "Mass/CrowdDemoTargetRegionPlanLifecycleDiagnosticKernel.h"
#include "Mass/CrowdDemoTargetStabilityDiagnosticKernel.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/Function.h"
#include "CrowdDemoRoundSimPipelineSubsystem.generated.h"

struct FCrowdDemoPreparedWorkerMassApplyPlan;

// Demo-owned payload for the Runtime Owner Commit Barrier. Runtime owns the
// generic prepared-result token and barrier protocol; this plan owns only the
// Round host state and validation tokens needed by the adapter callbacks.
struct FCrowdDemoPreparedRoundCommitPlan
{
  FCrowdWorkerPreparedResultApply PreparedProxyResult;
  TSharedPtr<FCrowdDemoPreparedWorkerMassApplyPlan> PreparedMassPlan;
  FCrowdWorkerResultCommitToken WorkerCommitToken;
  int32 PlanRevision = INDEX_NONE;
  int32 FixedStepIndex = INDEX_NONE;
  double ApplyStartSeconds = 0.0;

  bool IsValid() const
  {
    return PreparedProxyResult.IsValid()
      && PreparedMassPlan.IsValid()
      && WorkerCommitToken.Matches(PreparedProxyResult)
      && PlanRevision != INDEX_NONE
      && FixedStepIndex != INDEX_NONE
      && FMath::IsFinite(ApplyStartSeconds)
      && ApplyStartSeconds > 0.0;
  }
};

struct FCrowdDemoRoundFacingTemplate
{
  FCrowdFacingInput Input;
};

struct FCrowdDemoRoundWorkGraphInput
{
  FCrowdMassSharedFlowSampleInput SharedFlow;
  FCrowdMassMovementPipelineWorkInput Movement;
  FCrowdMassParticlePipelineWorkInput ParticleTemplate;
  FCrowdFacingSettings FacingSettings;
  TArray<FCrowdDemoRoundFacingTemplate> FacingTemplates;
};

struct FCrowdDemoRoundWorkGraphOutput
{
  FCrowdMassSharedFlowSampleOutput SharedFlow;
  FCrowdMassMovementPipelineWorkOutput Movement;
  FCrowdMassParticlePipelineWorkOutput Particle;
  FCrowdMassFacingFinalizeWorkOutput FacingFinalize;
  uint64 StableHash = 0;
  bool bCompleted = false;
};

// Demo-local immutable joins between the retained pure compute kernels.
class FCrowdDemoRoundWorkGraph
{
public:
  static bool BuildMovementInput(
    const FCrowdDemoRoundWorkGraphInput& Input,
    const FCrowdMassSharedFlowSampleOutput& SharedFlow,
    FCrowdMassMovementPipelineWorkInput& OutMovement);
  static bool BuildParticleInput(
    const FCrowdDemoRoundWorkGraphInput& Input,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    FCrowdMassParticlePipelineWorkInput& OutParticle);
  static bool BuildFacingInput(
    const FCrowdDemoRoundWorkGraphInput& Input,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    const FCrowdMassParticlePipelineWorkOutput& Particle,
    FCrowdMassFacingFinalizeWorkInput& OutFacing);
  static bool BuildFacingInputFromKinematics(
    const FCrowdDemoRoundWorkGraphInput& Input,
    const FCrowdMassBoundarySnapshot& Snapshot,
    const FCrowdMassMovementPipelineWorkOutput& Movement,
    TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
    FCrowdMassFacingFinalizeWorkInput& OutFacing);
};

struct FCrowdDemoRoundFlowAgentSample
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bUnreachable = false;
  bool bPenetrating = false;
};

enum class ECrowdDemoRoundPerformanceStage : uint8
{
  BusinessPrepare,
  SharedFlow,
  TargetTopology,
  TargetDemand,
  TargetPlan,
  TargetGuidance,
  GuidanceCompose,
  LocalPredictive,
  Particle,
  FacingFinalize,
  Commit,
  Count
};


struct FCrowdDemoPreparedReactiveMotionStep
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  bool bActive = false;
  float ProposedZ = 0.0f;
  float VerticalVelocityCmps = 0.0f;
};

struct FCrowdDemoPreparedCombatBoundaryCommit
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdDemoRangedCombatAgent> Agents;
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> ProjectileEvents;
  FCrowdDemoProjectileStepSummary ProjectileSummary;
  FCrowdDemoHitResponseSummary HitSummary;
  uint64 StableHash = 0;
  bool bProjectileCombat = false;
  bool bValid = false;
};

struct FCrowdDemoTargetRegionCapabilityCohortRuntime
{
  FCrowdDemoCapabilityCohort Cohort;
  int32 DemandRegionPhaseOffset = 0;
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionFlowPlan Plan;
  FCrowdDemoTargetRegionQuotaExecutionState QuotaExecution;
  FCrowdDemoTargetRegionPlanReplacementSummary LastPlanReplacement;
  FCrowdDemoTargetRegionPlanValidationResult Validation;
  TArray<FCrowdDemoTargetRegionGuidanceResult> Guidance;
  FCrowdDemoTargetRegionGuidanceSummary GuidanceSummary;
  uint32 TopologyRoundHash = 2166136261u;
  uint32 DemandRoundHash = 2166136261u;
  uint32 TransportRoundHash = 2166136261u;
  uint32 GuidanceRoundHash = 2166136261u;
  uint32 ValidationRoundHash = 2166136261u;
  int32 PlanRebuildCount = 0;
  int32 InvalidStepCount = 0;
  int32 ValidationFailureCount = 0;
  int32 GuidanceUnroutedStepCount = 0;
  int32 LastInvalidStep = INDEX_NONE;
  TArray<float> SolverMillisecondsSamples;
  FCrowdDemoTargetRegionPlanLifecycleRuntime PlanLifecycle;
  TSet<int32> TargetEngagedHoldAgentIds;
  int32 TargetEngagementAcquireCount = 0;
  int32 TargetEngagementReleaseCount = 0;
  int32 TargetEngagementSuppressedRetreatCount = 0;
  bool bRoundValid = true;
};

struct FCrowdDemoPreparedSteeringGuidance
{
  int32 AgentId = INDEX_NONE;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
};

struct FCrowdDemoRoundBoundaryFormationFact
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  float RadiusCm = 42.0f;
};

struct FCrowdDemoRoundBoundaryFacingFact
{
  int32 AgentId = INDEX_NONE;
  int32 ConsecutiveFinalSettleSteps = 0;
};

// Immutable Demo-owned business overlay gathered alongside the Runtime base
// snapshot. It is deliberately not part of the plugin public API.
struct FCrowdDemoRoundBoundaryBusinessFact
{
  FCrowdStableEntityRef EntityRef;
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  FVector LocalOffset = FVector::ZeroVector;
  float RadiusCm = 0.0f;
  float YawDegrees = 0.0f;
  bool bHasCombatCapability = false;
  FCrowdDemoMassStatsFragment Stats;
  FCrowdDemoBusinessStateFragment Business;
  FCrowdDemoRangedAttackFragment Attack;
  FCrowdDemoReactiveMotionFragment ReactiveMotion;
  FCrowdDemoHitFlashFragment HitFlash;
  FCrowdDemoMassVisualFragment Visual;
};

// Demo-owned immutable input/output for the BusinessPrepare worker. These
// types intentionally stay out of the plugin public API.
struct FCrowdDemoBoundaryBusinessWorkInput
{
  FCrowdMassBoundarySnapshot Snapshot;
  TArray<FCrowdDemoRoundBoundaryBusinessFact> Facts;
  FCrowdDemoRoundRules Rules;
  TArray<FCrowdProjectileState> Projectiles;
  int32 RoundId = INDEX_NONE;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  float StepEndServerTimeSeconds = 0.0f;
  float SimulationTimeSeconds = 0.0f;
  float FixedStepSeconds = 0.0f;
};

struct FCrowdDemoBoundaryBusinessWorkOutput
{
  FCrowdDemoPreparedCombatBoundaryCommit Commit;
  TArray<FCrowdGuidanceCandidate> GuidanceCandidates;
  TArray<FCrowdDemoPreparedReactiveMotionStep> ReactiveSteps;
  uint64 StableHash = 0;
  bool bRequiresCommit = false;
  bool bCompleted = false;
};

struct FCrowdDemoPreparedFacingRollbackFact
{
  int32 AgentId = INDEX_NONE;
  FCrowdMassFacingFragment Facing;
};

struct FCrowdDemoWorkerMovementTailExecution
{
  FCrowdDemoRoundWorkGraphOutput GraphOutput;
  FCrowdMassFacingFinalizeWorkOutput Output;
  TArray<FCrowdMassFinalKinematicState> ObstacleKinematics;
  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  uint64 StableHash = 0;
  bool bUsesWorkerV2PreparedMovement = false;
  bool bUsesWorkerV2PreparedParticle = false;
  TAtomic<bool> bCompleted{false};
};

struct FCrowdDemoWorkerV2MovementExpectation
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  double SimulationTimeSeconds = 0.0;
};

struct FCrowdDemoBoundaryFacingWorkState
{
  struct FTargetTopologySlot
  {
    uint32 CohortKey = 0;
    FCrowdMassTargetRegionTopologyInput Input;
    FCrowdMassTargetRegionTopologyOutput Output;
    FCrowdMassTargetRegionDemandInput DemandInput;
    FCrowdMassTargetRegionDemandOutput DemandOutput;
    FCrowdMassTargetRegionPlanInput PlanInput;
    FCrowdMassTargetRegionPlanOutput PlanOutput;
    FCrowdMassTargetRegionGuidanceInput GuidanceInput;
    FCrowdMassTargetRegionGuidanceOutput GuidanceOutput;
    bool bUseCachedTopology = false;
    bool bDemandStaged = false;
    bool bPlanStaged = false;
    bool bGuidanceStaged = false;
  };
  FCrowdDemoRoundWorkGraphInput GraphInput;
  FCrowdDemoRoundWorkGraphOutput GraphOutput;
  FCrowdMassMovementPipelineWorkInput MovementShadowInput;
  TMap<int32, int32> SharedFlowIndexByAgentId;
  FCrowdDemoBoundaryBusinessWorkInput BusinessInput;
  FCrowdDemoBoundaryBusinessWorkOutput BusinessOutput;
  FCrowdMassFacingWorkInput FacingShadowInput;
  FCrowdDemoSharedFlowFieldConfig ObstacleConfig;
  TArray<FCrowdMassFinalKinematicState> ObstacleKinematics;
  float ObstacleFixedStepSeconds = 0.0f;
  float ObstacleMaxReprojectDeltaCm = 0.0f;
  TArray<FTargetTopologySlot> TargetTopologySlots;
  FCrowdMassFacingFinalizeWorkOutput Output;
  TSharedPtr<FCrowdDemoWorkerMovementTailExecution,
    ESPMode::ThreadSafe> WorkerMovementTail;
  TMap<int32, int32> PreviousSettleStepsByAgentId;
  TMap<int32, bool> TerminalOwnerByAgentId;
  TMap<int32, int32> ConsecutiveSettleStepsByAgentId;
  TMap<int32, bool> FinalSettledByAgentId;
  bool bSharedFlowStaged = false;
  bool bBusinessStaged = false;
  bool bMovementStaged = false;
  bool bParticleStaged = false;
  bool bObstacleStaged = false;
  bool bMovementConsumed = false;
  bool bMovementShadowInputValid = false;
  bool bWorkerV2InputSubmitted = false;
  bool bUseWorkerV2Target = false;
  bool bUseWorkerNativeScenarioBusiness = false;
  uint64 WorkerV2InputSequence = 0;
  bool bWorkerMovementTailSubmitted = false;
  bool bWorkerMovementTailConsumed = false;
  uint64 WorkerMovementSequence = 0;
  bool bParticleConsumed = false;
  bool bCompleted = false;
};


struct FCrowdDemoRoundErrorSeries
{
  void Reset() { CheckpointP95Samples.Reset(); }
  void Record(const float ErrorCm) { CheckpointP95Samples.Add(ErrorCm); }
  float GetMax() const;
  float GetExpansionFromFirst() const;
  bool IsExpanding(const float ToleranceCm) const { return GetExpansionFromFirst() > ToleranceCm; }

private:
  TArray<float> CheckpointP95Samples;
};

struct FCrowdDemoRoundMassAccessCounts
{
  void Reset()
  {
    CanonicalGatherReadCount = 0;
    IntermediateReadCount = 0;
    CommitWriteCount = 0;
  }
  bool TryRecordCanonicalGatherRead(const bool bStepInProgress)
  {
    if (!bStepInProgress || CanonicalGatherReadCount != 0
      || CommitWriteCount != 0)
      return false;
    ++CanonicalGatherReadCount;
    return true;
  }
  bool TryRecordIntermediateRead(const bool bStepInProgress)
  {
    if (!bStepInProgress || CommitWriteCount != 0)
      return false;
    ++IntermediateReadCount;
    return true;
  }
  bool TryBeginAtomicCommitWrite(const bool bStepInProgress)
  {
    if (!bStepInProgress || CanonicalGatherReadCount != 1
      || IntermediateReadCount != 0 || CommitWriteCount != 0)
      return false;
    ++CommitWriteCount;
    return true;
  }
  bool IsOrdinaryStepContractSatisfied() const
  {
    return CanonicalGatherReadCount == 1
      && IntermediateReadCount == 0 && CommitWriteCount == 1;
  }
  uint8 CanonicalGatherReadCount = 0;
  uint8 IntermediateReadCount = 0;
  uint8 CommitWriteCount = 0;
};

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSimPipelineSubsystem : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  uint64 AllocateWorkerResultConsumerFrameSequence()
  {
    return ++WorkerResultConsumerFrameSequence;
  }

  bool QueuePreparedRoundCommitPlan(
    FCrowdDemoPreparedRoundCommitPlan&& Pending)
  {
    if (PreparedRoundCommitPlan.IsSet()
      || !Pending.IsValid())
      return false;
    PreparedRoundCommitPlan.Emplace(MoveTemp(Pending));
    return true;
  }

  FCrowdDemoPreparedRoundCommitPlan* PeekPreparedRoundCommitPlan()
  {
    return PreparedRoundCommitPlan.IsSet()
      ? &PreparedRoundCommitPlan.GetValue() : nullptr;
  }

  const FCrowdDemoPreparedRoundCommitPlan*
    PeekPreparedRoundCommitPlan() const
  {
    return PreparedRoundCommitPlan.IsSet()
      ? &PreparedRoundCommitPlan.GetValue() : nullptr;
  }

  void ClearPreparedRoundCommitPlan()
  {
    PreparedRoundCommitPlan.Reset();
  }

  void QueueBootstrap(const FCrowdDemoRoundBootstrapPacket& Packet);
  void QueueRoundPlan(const FCrowdDemoRoundPlanPacket& Packet);
  void QueueRoundResult(const FCrowdDemoRoundResultPacket& Packet);

  bool PeekBootstrap(FCrowdDemoRoundBootstrapPacket& OutPacket) const;
  void MarkBootstrapApplied(int32 AgentCount);
  bool PopDueRoundPlan(float BoundaryServerTimeSeconds, FCrowdDemoRoundPlanPacket& OutPacket);
  bool HasDueRoundPlan(float BoundaryServerTimeSeconds) const;
  bool HasDueAuthorityInput(float BoundaryServerTimeSeconds) const;
  bool PopRoundResultForBoundary(FCrowdDemoRoundResultPacket& OutPacket);

  void ActivatePlan(const FCrowdDemoRoundPlanPacket& Packet, int32 AgentCount, bool bLate);
  bool TryClaimPlanApplyBoundary();
  void EnsureFormationIndexCache(TConstArrayView<int32> AgentIds);
  const TMap<int32, int32>& GetFormationIndexByAgentId() const { return FormationIndexByAgentId; }
  uint64 GetFormationMembershipHash() const { return FormationMembershipHash; }
  int32 GetFormationCacheRebuildCount() const { return FormationCacheRebuildCount; }
  bool TryBeginFixedStep(float TargetServerTimeSeconds);
  void FinishFixedStep();
  void FailFixedStep();
  bool TryRecordCanonicalGatherRead();
  bool TryRecordBootstrapMassRead();
  bool NeedsBootstrapBoundarySnapshot() const
  {
    return bPlanActive
      && LastBootstrapMassReadPlanRevision != GetCurrentPlanRevision();
  }
  bool TryBeginAtomicCommitWrite();
  void RecordAuthorityMassWrite();
  const FCrowdDemoRoundMassAccessCounts& GetCurrentStepMassAccessCounts() const
  { return CurrentStepMassAccessCounts; }
  uint64 GetAuthorityMassWriteCount() const
  { return AuthorityMassWriteCount; }
  void InvalidateInFlightBoundaryForAuthoritativeState();
  bool IsStepInProgress() const { return bStepInProgress; }
  bool IsActive() const { return bPlanActive; }
  bool IsRoundSimScenarioActive() const;
  float GetCurrentFixedStepSeconds() const { return CurrentFixedStepSeconds; }
  float GetCurrentStepStartServerTimeSeconds() const { return CurrentStepStartServerTimeSeconds; }
  float GetCurrentStepEndServerTimeSeconds() const { return CurrentStepEndServerTimeSeconds; }
  float GetSimulatedServerTimeSeconds() const { return SimulatedServerTimeSeconds; }
  float GetScheduledServerTimeSeconds() const
  {
    return bStepInProgress
      ? CurrentStepEndServerTimeSeconds
      : SimulatedServerTimeSeconds;
  }
  int32 GetCurrentRoundId() const { return ActivePlan.RoundId; }
  int32 GetCurrentPlanRevision() const { return ActivePlan.Revision; }
  const FCrowdDemoRoundPlanPacket& GetActivePlan() const { return ActivePlan; }
  const FCrowdDemoRoundRules& GetRules() const { return ActivePlan.Rules; }
  bool PublishBoundarySnapshot(
    FCrowdMassBoundarySnapshot&& Snapshot,
    TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
    TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts,
    TArray<FCrowdDemoRoundBoundaryBusinessFact>&& BusinessFacts);
  // Fixture compatibility. Production gather must use the complete overload.
  bool PublishBoundarySnapshot(
    FCrowdMassBoundarySnapshot&& Snapshot,
    TArray<FCrowdDemoRoundBoundaryFormationFact>&& FormationFacts,
    TArray<FCrowdDemoRoundBoundaryFacingFact>&& FacingFacts);
  bool TryPublishWorkerProxyBoundarySnapshot();
  bool TryUseBootstrapBoundarySnapshot()
  {
    if (!bBootstrapBoundarySnapshotPending
      || !IsBoundarySnapshotCurrent())
      return false;
    CurrentStepMassDirtyEntityRefs.Reset(BoundarySnapshot.Agents.Num());
    for (const FCrowdMassBoundaryAgentRecord& Agent : BoundarySnapshot.Agents)
      CurrentStepMassDirtyEntityRefs.Add(Agent.AgentFacts.StableEntityRef);
    bBootstrapBoundarySnapshotPending = false;
    bCurrentStepUsedBootstrapBoundarySnapshot = true;
    return true;
  }
  const FCrowdMassBoundarySnapshot& GetBoundarySnapshot() const
  { return BoundarySnapshot; }
  const FCrowdDemoRoundBoundaryFormationFact* FindBoundaryFormationFact(
    int32 AgentId) const;
  const TArray<FCrowdDemoRoundBoundaryFormationFact>&
    GetBoundaryFormationFacts() const
  { return BoundaryFormationFacts; }
  const TArray<FCrowdDemoRoundBoundaryFacingFact>&
    GetBoundaryFacingFacts() const
  { return BoundaryFacingFacts; }
  const TArray<FCrowdDemoRoundBoundaryBusinessFact>&
    GetBoundaryBusinessFacts() const
  { return BoundaryBusinessFacts; }
  TConstArrayView<FCrowdStableEntityRef>
    GetCurrentStepMassDirtyEntityRefs() const
  { return CurrentStepMassDirtyEntityRefs; }
  void RecordDirtyMassApply(int32 EntityCount);
  bool MarkCurrentStepWorkerDirtyMassApplied(
    uint64 PublishSequence,
    int32 EntityCount)
  {
    if (!bStepInProgress
      || CurrentStepMassAccessCounts.CommitWriteCount != 1
      || bCurrentStepWorkerDirtyMassApplied
      || PublishSequence == 0
      || EntityCount < 0)
      return false;
    bCurrentStepWorkerDirtyMassApplied = true;
    CurrentStepWorkerDirtyMassPublishSequence = PublishSequence;
    CurrentStepWorkerDirtyMassEntityCount = EntityCount;
    return true;
  }
  bool IsCurrentStepWorkerDirtyMassApplied() const
  { return bCurrentStepWorkerDirtyMassApplied; }
  uint64 GetCurrentStepWorkerDirtyMassPublishSequence() const
  { return CurrentStepWorkerDirtyMassPublishSequence; }
  int32 GetCurrentStepWorkerDirtyMassEntityCount() const
  { return CurrentStepWorkerDirtyMassEntityCount; }
  bool IsBoundarySnapshotCurrent() const
  {
    return BoundarySnapshot.bValid
      && BoundarySnapshot.FixedStepIndex == GetCurrentFixedStepIndex()
      && BoundarySnapshot.PlanRevision == GetCurrentPlanRevision();
  }
  bool BeginWorkerBootstrapPreparation(double GatherMilliseconds);
  float GetCurrentBoundaryWallMilliseconds() const;
  bool StageBoundaryBusinessWork();
  bool StageBoundarySharedFlowWork(
    const FCrowdMassSharedFlowSampleInput& Input);
  bool StageBoundaryTargetTopologyWork(
    uint32 CohortKey,
    const FCrowdMassTargetRegionTopologyInput& Input,
    const FCrowdDemoTargetPolarTopology* CachedTopology = nullptr,
    const FCrowdDemoTargetPolarTopologySummary* CachedSummary = nullptr);
  bool StageBoundaryTargetDemandWork(
    uint32 CohortKey,
    const FCrowdMassTargetRegionDemandInput& Input);
  bool StageBoundaryTargetPlanWork(
    uint32 CohortKey,
    const FCrowdMassTargetRegionPlanInput& Input);
  bool StageBoundaryTargetGuidanceWork(
    uint32 CohortKey,
    const FCrowdMassTargetRegionGuidanceInput& Input);
  bool StageBoundaryMovementWork(
    FCrowdMassMovementPipelineWorkInput&& Input);
  bool StageBoundaryParticleWork(
    FCrowdMassParticlePipelineWorkInput&& Input);
  bool StageBoundaryObstacleWork(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    float FixedStepSeconds);
  bool DispatchBoundarySoftPressureWorkGraph(
    FCrowdMassFacingFinalizeWorkInput&& Input,
    TMap<int32, int32>&& PreviousSettleStepsByAgentId,
    TMap<int32, bool>&& TerminalOwnerByAgentId);
  bool DispatchBoundaryFacingWork(
    FCrowdMassFacingFinalizeWorkInput&& Input,
    TMap<int32, int32>&& ConsecutiveSettleStepsByAgentId,
    TMap<int32, bool>&& FinalSettledByAgentId);
  bool CanUseFullWorkerProductionFastPath() const;
  bool TrySubmitFullWorkerProductionIntent();
  bool SubmitPreparedWorkerBootstrapInput();
  bool IsCurrentStepFullWorkerProductionFastPath() const
  { return bCurrentStepFullWorkerProductionFastPath; }
  uint64 GetCurrentStepFullWorkerInputSequence() const
  { return CurrentStepFullWorkerInputSequence; }
  bool MarkFullWorkerProductionResultCommitted(
    double CommitMilliseconds);
  void ObserveCommittedWorkerScenarioState(
    const FCrowdWorkerResultApplyProxy& Proxy,
    uint64 Generation,
    uint64 PublishSequence,
    int64 AbsoluteSimulationTick);
  void SetPreparedTargetRegionGuidanceCandidates(
    TArray<FCrowdGuidanceCandidate>&& Values)
  { PreparedTargetRegionGuidanceCandidates = MoveTemp(Values); }
  const TArray<FCrowdGuidanceCandidate>&
    GetPreparedTargetRegionGuidanceCandidates() const
  { return PreparedTargetRegionGuidanceCandidates; }
  void SetPreparedBusinessGuidanceCandidates(
    TArray<FCrowdGuidanceCandidate>&& Values)
  { PreparedBusinessGuidanceCandidates = MoveTemp(Values); }
  const TArray<FCrowdGuidanceCandidate>&
    GetPreparedBusinessGuidanceCandidates() const
  { return PreparedBusinessGuidanceCandidates; }
  void SetPreparedReactiveMotionSteps(
    TArray<FCrowdDemoPreparedReactiveMotionStep>&& Values)
  { PreparedReactiveMotionSteps = MoveTemp(Values); }
  const TArray<FCrowdDemoPreparedReactiveMotionStep>&
    GetPreparedReactiveMotionSteps() const
  { return PreparedReactiveMotionSteps; }
  bool IsRangedProjectileCombat() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
      && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0;
  }

  bool BuildProjectileSnapshot(
    TArray<FCrowdProjectileState>& OutProjectiles) const;
  bool PrepareProjectileFinalApply(int32 RequiredActiveCount);
  void ApplyProjectileFinalState(
    TConstArrayView<FCrowdProjectileState> Projectiles);
  void RecordProjectileStep(
    const FCrowdDemoProjectileStepSummary& Summary,
    TConstArrayView<FCrowdDemoProjectileVisualEvent> Events);
  void RecordProjectileHitResponse(const FCrowdDemoHitResponseSummary& Summary);
  FCrowdDemoProjectileMetrics BuildProjectileMetrics() const;
  bool DequeueProjectileVisualEvents(TArray<FCrowdDemoProjectileVisualEvent>& OutEvents);
  bool DequeueT7PresentationEvents(
    TArray<FCrowdDemoT7PresentationEvent>& OutEvents);

  void RecordParticleConstraintSummary(
    const FCrowdDemoParticleConstraintSummary& CandidateSummary,
    const FCrowdDemoParticleConstraintSummary& AppliedSummary,
    uint32 AppliedStateHash,
    bool bGlobalFallback,
    float SolverMilliseconds);
  const TArray<FCrowdDemoLocalPredictiveResult>& GetPreparedLocalPredictiveResults() const
  { return PreparedLocalPredictiveResults; }
  const TArray<FCrowdDemoLocalPredictiveGrantState>& GetLocalPredictiveGrantStates() const
  { return LocalPredictiveGrantStates; }
  const FCrowdDemoLocalPredictiveSummary& GetLastLocalPredictiveSummary() const
  { return LastLocalPredictiveSummary; }
  uint32 GetLocalPredictiveRoundHash() const { return LocalPredictiveRoundHash; }
  int32 GetLocalPredictiveSampleCount() const { return LocalPredictiveSampleCount; }
  int32 GetLocalPredictiveInvalidStepCount() const
  { return LocalPredictiveInvalidStepCount; }
  uint32 GetGuidanceCandidateRoundHash() const { return GuidanceCandidateRoundHash; }
  uint32 GetGuidanceComposeRoundHash() const { return GuidanceComposeRoundHash; }
  int32 GetGuidanceComposeSampleCount() const { return GuidanceComposeSampleCount; }
  void RecordGuidanceComposeStep(TArray<FCrowdDemoComposedGuidance>&& Results);
  void RecordLocalPredictiveStep(
    TArray<FCrowdDemoLocalPredictiveResult>&& Results,
    TArray<FCrowdDemoLocalPredictiveGrantState>&& GrantStates,
    const FCrowdDemoLocalPredictiveSummary& Summary);
  void RecordLocalPredictiveDiagnosticFrame(
    FCrowdDemoLocalPredictiveDiagnosticFrame&& Frame);
  const FCrowdDemoLocalPredictiveDiagnosticFrame& GetLocalPredictiveDiagnosticFrame() const
  { return LocalPredictiveDiagnosticFrame; }
  void SetLocalPredictiveComponentFixture(
    FCrowdDemoLocalPredictiveComponentFixture&& Fixture)
  { LocalPredictiveComponentFixture = MoveTemp(Fixture); }
  const FCrowdDemoLocalPredictiveComponentFixture& GetLocalPredictiveComponentFixture() const
  { return LocalPredictiveComponentFixture; }
  bool BuildCurrentLocalPredictiveComponentFixture(
    TConstArrayView<int32> WitnessAgentIds,
    FCrowdDemoLocalPredictiveComponentFixture& OutFixture) const;
  void RecordCrossProfileParticleViolations(int32 HardCount, int32 SweptCount)
  {
    CrossProfileHardViolationCount += FMath::Max(0, HardCount);
    CrossProfileSweptViolationCount += FMath::Max(0, SweptCount);
  }
  int32 GetCrossProfileHardViolationCount() const
  { return CrossProfileHardViolationCount; }
  int32 GetCrossProfileSweptViolationCount() const
  { return CrossProfileSweptViolationCount; }
  void RecordParticleFailureFixture(const FCrowdDemoParticleFailureFixture& Fixture);
  void RecordParticleAppliedStateHash(uint32 AppliedStateHash)
  { ParticleAppliedStateHash = AppliedStateHash; }
  const FCrowdDemoParticleFailureFixture& GetParticleFailureFixture() const
  { return ParticleFailureFixture; }
  const FCrowdDemoParticleConstraintSummary& GetLastParticleCandidateSummary() const
  {
    return LastParticleCandidateSummary;
  }
  const FCrowdDemoParticleConstraintSummary& GetLastParticleAppliedSummary() const
  { return LastParticleAppliedSummary; }
  uint32 GetParticleCandidateStateHash() const { return ParticleCandidateStateHash; }
  uint32 GetParticleAppliedStateHash() const { return ParticleAppliedStateHash; }
  int32 GetParticleInvalidStepCount() const { return ParticleInvalidStepCount; }
  int32 GetParticleGlobalFallbackStepCount() const { return ParticleGlobalFallbackStepCount; }
  int32 GetParticleSettlingSteps() const { return ParticleSettlingSteps; }
  int32 GetSoftPressureRollbackSnapshotHitCount() const
  { return SoftPressureRollbackSnapshotHitCount; }
  int32 GetSoftPressureRollbackSnapshotMissCount() const
  { return SoftPressureRollbackSnapshotMissCount; }
  int32 GetSoftPressureRollbackAgentMismatchCount() const
  { return SoftPressureRollbackAgentMismatchCount; }
  int32 GetSoftPressureRollbackReplayedStepCount() const
  { return SoftPressureRollbackReplayedStepCount; }
  int32 GetZeroErrorCorrectionFastPathCount() const
  { return PerformanceZeroErrorRollbackReplayCount; }
  bool HasParticleConstraintRunFailure() const { return bParticleConstraintRunFailure; }
  void StopAfterParticleConstraintFailure();
  float GetParticleSolverMsP95() const;
  void RecordPerformanceStage(ECrowdDemoRoundPerformanceStage Stage, float Milliseconds);
  void RecordTargetTopologyPerformance(bool bBuilt);
  void RecordTargetDemandPerformance(bool bFullBuild);
  void RecordFixedStepPerformance(float Milliseconds);
  void RecordPipelineFramePerformance(
    int32 ExecutedSteps,
    float TargetServerTimeSeconds,
    bool bHitFixedStepLimit,
    bool bHitCatchupCpuBudget);
  void BeginRollbackReplayPerformance(
    int32 ReplayedSteps,
    float ApplyMilliseconds,
    bool bZeroErrorReplay);
  FCrowdDemoRoundPerformanceMetrics BuildRoundPerformanceMetrics() const;
  bool IsSoftPressureRouteDiagnosticEnabled() const;
  void RecordSoftPressureRouteStep(
    TConstArrayView<FCrowdDemoSoftPressureRouteStepSample> Samples);
  void FinalizeSoftPressureRouteDiagnostic(
    const FCrowdDemoSoftPressureRouteCounterfactual& Counterfactual);
  const FCrowdDemoSoftPressureRouteDiagnosticRuntime& GetSoftPressureRouteDiagnosticRuntime() const
  { return SoftPressureRouteDiagnosticRuntime; }
  const FCrowdDemoSoftPressureRouteDiagnosticSummary& GetSoftPressureRouteDiagnosticSummary() const
  { return SoftPressureRouteDiagnosticSummary; }
  bool HasFlowGoalReached(int32 AgentId) const
  { return FlowGoalReachedAgentIds.Contains(AgentId); }
  FCrowdDemoTargetFact& GetTargetFact() { return TargetFact; }
  const FCrowdDemoTargetFact& GetTargetFact() const { return TargetFact; }
  bool IsTargetStabilityDiagnosticEnabled() const;
  void RecordTargetStabilityStep(const FCrowdDemoTargetStabilityStepSample& Step);
  void FinalizeTargetStabilityDiagnostic();
  const FCrowdDemoTargetStabilitySummary& GetTargetStabilitySummary() const
  { return TargetStabilitySummary; }
  FCrowdDemoTargetPolarTopology& GetPreparedTargetRegionTopology()
  { return PreparedTargetRegionTopology; }
  const FCrowdDemoTargetPolarTopology& GetPreparedTargetRegionTopology() const
  { return PreparedTargetRegionTopology; }
  FCrowdDemoTargetPolarTopologySummary& GetTargetRegionTopologySummary()
  { return TargetRegionTopologySummary; }
  TArray<FCrowdDemoTargetRegionTransportAgent>& GetPreparedTargetRegionAgents()
  { return PreparedTargetRegionAgents; }
  FCrowdDemoTargetRegionDemandResult& GetPreparedTargetRegionDemand()
  { return PreparedTargetRegionDemand; }
  const FCrowdDemoTargetRegionDemandResult& GetPreparedTargetRegionDemand() const
  { return PreparedTargetRegionDemand; }
  FCrowdDemoTargetRegionFlowPlan& GetPreparedTargetRegionPlan()
  { return PreparedTargetRegionPlan; }
  const FCrowdDemoTargetRegionFlowPlan& GetPreparedTargetRegionPlan() const
  { return PreparedTargetRegionPlan; }
  FCrowdDemoTargetRegionQuotaExecutionState& GetTargetRegionQuotaExecution()
  { return TargetRegionQuotaExecution; }
  const FCrowdDemoTargetRegionQuotaExecutionState& GetTargetRegionQuotaExecution() const
  { return TargetRegionQuotaExecution; }
  FCrowdDemoTargetRegionPlanValidationResult& GetTargetRegionPlanValidation()
  { return TargetRegionPlanValidation; }
  const FCrowdDemoTargetRegionPlanValidationResult& GetTargetRegionPlanValidation() const
  { return TargetRegionPlanValidation; }
  TArray<FCrowdDemoTargetRegionGuidanceResult>& GetPreparedTargetRegionGuidance()
  { return PreparedTargetRegionGuidance; }
  FCrowdDemoTargetRegionGuidanceSummary& GetTargetRegionGuidanceSummary()
  { return TargetRegionGuidanceSummary; }
  void RecordTargetRegionTopologyStep();
  void RecordTargetRegionDemandStep();
  void RecordTargetRegionTransportStep(float SolverMilliseconds, int32 RebuildReason);
  void RecordTargetRegionGuidanceStep();
  void RecordTargetRegionValidationStep();
  uint32 GetTargetRegionTopologyRoundHash() const { return TargetRegionTopologyRoundHash; }
  uint32 GetTargetRegionDemandRoundHash() const { return TargetRegionDemandRoundHash; }
  uint32 GetTargetRegionTransportRoundHash() const { return TargetRegionTransportRoundHash; }
  uint32 GetTargetRegionGuidanceRoundHash() const { return TargetRegionGuidanceRoundHash; }
  int32 GetTargetRegionPlanRebuildCount() const { return TargetRegionPlanRebuildCount; }
  int32 GetTargetRegionLifetimeRebuildCount() const { return TargetRegionLifetimeRebuildCount; }
  int32 GetTargetRegionTargetRebuildCount() const { return TargetRegionTargetRebuildCount; }
  int32 GetTargetRegionEnvironmentRebuildCount() const { return TargetRegionEnvironmentRebuildCount; }
  int32 GetTargetRegionMembershipRebuildCount() const { return TargetRegionMembershipRebuildCount; }
  int32 GetTargetRegionDemandSatisfiedRebuildCount() const { return TargetRegionDemandSatisfiedRebuildCount; }
  int32 GetTargetRegionPathInvalidRebuildCount() const { return TargetRegionPathInvalidRebuildCount; }
  float GetTargetRegionSolverMsP95() const;
  bool IsTargetRegionRoundValid() const { return bTargetRegionRoundValid; }
  int32 GetTargetRegionInvalidStepCount() const { return TargetRegionInvalidStepCount; }
  int32 GetTargetRegionValidationFailureCount() const { return TargetRegionValidationFailureCount; }
  uint32 GetTargetRegionValidationRoundHash() const { return TargetRegionValidationRoundHash; }
  int32 GetTargetRegionGuidanceUnroutedStepCount() const { return TargetRegionGuidanceUnroutedStepCount; }
  int32 GetTargetRegionGuidanceUnroutedAgentSampleCount() const { return TargetRegionGuidanceUnroutedAgentSampleCount; }
  int32 GetTargetRegionGuidanceUnroutedAgentMax() const { return TargetRegionGuidanceUnroutedAgentMax; }
  int32 GetTargetRegionGuidanceFirstFailureStep() const { return TargetRegionGuidanceFirstFailureStep; }
  int32 GetTargetRegionGuidanceFirstFailureAgentId() const { return TargetRegionGuidanceFirstFailureAgentId; }
  void PinTargetRegionFailureFixture(int32 Kind, int32 AgentId, int32 CellKey, uint32 FixtureHash);
  bool HasTargetRegionFailureFixture() const { return bTargetRegionFailureFixtureValid; }
  int32 GetTargetRegionFailureFixtureStep() const { return TargetRegionFailureFixtureStep; }
  int32 GetTargetRegionFailureFixtureKind() const { return TargetRegionFailureFixtureKind; }
  int32 GetTargetRegionFailureFixtureAgentId() const { return TargetRegionFailureFixtureAgentId; }
  int32 GetTargetRegionFailureFixtureCellKey() const { return TargetRegionFailureFixtureCellKey; }
  uint32 GetTargetRegionFailureFixtureHash() const { return TargetRegionFailureFixtureHash; }
  void SetCapabilityCohorts(
    TArray<FCrowdDemoCapabilityCohort>&& Cohorts,
    const FCrowdDemoCapabilityProfileSummary& Summary);
  TArray<FCrowdDemoTargetRegionCapabilityCohortRuntime>& GetCapabilityCohorts()
  { return TargetRegionCapabilityCohorts; }
  const TArray<FCrowdDemoTargetRegionCapabilityCohortRuntime>& GetCapabilityCohorts() const
  { return TargetRegionCapabilityCohorts; }
  const FCrowdDemoCapabilityProfileSummary& GetCapabilityProfileSummary() const
  { return CapabilityProfileSummary; }
  int32 GetCapabilityCohortRebuildCount() const { return CapabilityCohortRebuildCount; }
  bool IsTargetRegionPlanLifecycleDiagnosticEnabled() const
  { return bTargetRegionPlanLifecycleDiagnosticPlanEnabled && IsActive(); }
  void FinalizeTargetRegionPlanLifecycleDiagnostic();
  const FCrowdDemoTargetRegionPlanLifecycleSummary& GetTargetRegionPlanLifecycleSummary() const
  { return TargetRegionPlanLifecycleSummary; }
  const FCrowdDemoTargetRegionPlanLifecycleFixture& GetTargetRegionPlanLifecycleFixture() const
  { return TargetRegionPlanLifecycleFixture; }
  void RecordNavigationDomainReprojectDelta(float DeltaCm);
  bool EnsureSharedFlowField(const FCrowdDemoSharedFlowFieldConfig& Config);
  bool EnsureDynamicSharedFlowField(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& TargetLocation);
  const FCrowdDemoSharedFlowField& GetSharedFlowField() const { return SharedFlowField; }
  int32 GetDynamicFlowAnchorCellKey() const { return DynamicFlowAnchorCellKey; }
  int32 GetDynamicFlowIntegrationRebuildCount() const
  { return DynamicFlowIntegrationRebuildCount; }
  uint32 GetDynamicFlowRoundHash() const { return DynamicFlowRoundHash; }
  void RecordFlowConnectivityStep(
    int32 RecoveredCount,
    int32 DesiredSegmentViolationCount,
    int32 SourceAttachmentSuccessCount,
    int32 UnreachableSampleCount);
  FCrowdDemoSharedFlowMetrics BuildSharedFlowMetrics(TConstArrayView<FCrowdDemoRoundAgentState> States) const;
  int32 GetCurrentFixedStepIndex() const;
  void RecordSoftPressureRollbackOutcome(bool bHit, bool bAgentMismatch, int32 ReplayedSteps);
  bool IsOpenSpawnRelaxation() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenSpawnRelaxation;
  }
  bool IsOpenCohortMovement() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase == ECrowdDemoSoftPressureTestCase::OpenCohortMovement;
  }
  void InitializeOpenCohortMovement(
    const FCrowdDemoOpenCohortMovementLayout& Layout)
  { OpenCohortMovementLayout = Layout; }
  const FCrowdDemoOpenCohortMovementLayout& GetOpenCohortMovementLayout() const
  { return OpenCohortMovementLayout; }
  void RecordOpenCohortMovementGuidance(
    TConstArrayView<FCrowdDemoTargetRegionGuidanceResult> Guidance)
  {
    if (!IsOpenCohortMovement()) return;
    FCrowdDemoOpenCohortMovementKernel::UpdateProgress(
      Guidance, OpenCohortMovementLayout.Agents.Num(), GetCurrentFixedStepIndex(),
      OpenCohortMovementProgress);
  }
  const FCrowdDemoOpenCohortMovementProgress& GetOpenCohortMovementProgress() const
  { return OpenCohortMovementProgress; }
  bool IsBidirectionalSwap() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::BidirectionalSwap;
  }
  void InitializeBidirectionalSwap(const FCrowdDemoBidirectionalSwapLayout& Layout)
  {
    BidirectionalSwapLayout = Layout;
    BidirectionalSwapProgress = {};
  }
  bool EnsureBidirectionalSwapFlowFields();
  const FCrowdDemoSharedFlowField* FindBidirectionalSwapFlowField(
    int32 FormationIndex) const;
  const FCrowdSharedFlowField* FindRuntimeBidirectionalSwapFlowField(
    int32 FormationIndex) const;
  void RecordBidirectionalSwapStep(
    TConstArrayView<FCrowdDemoBidirectionalSwapStepAgent> Agents)
  {
    if (!IsBidirectionalSwap()) return;
    FCrowdDemoBidirectionalSwapKernel::UpdateProgress(
      Agents, GetCurrentFixedStepIndex(), BidirectionalSwapProgress);
  }
  const FCrowdDemoBidirectionalSwapLayout& GetBidirectionalSwapLayout() const
  { return BidirectionalSwapLayout; }
  const FCrowdDemoBidirectionalSwapProgress& GetBidirectionalSwapProgress() const
  { return BidirectionalSwapProgress; }
  bool IsValidCorridorTransit() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::ValidCorridorTransit;
  }
  bool IsHeterogeneousTransit() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit;
  }
  bool IsCorridorTransitProgressScenario() const
  { return IsValidCorridorTransit() || IsHeterogeneousTransit(); }
  bool IsTargetRegionExecutionActive() const
  {
    if (!IsActive()
      || ActivePlan.Rules.Scenario != ECrowdDemoScenario::SimRoundSoftPressure
      || ActivePlan.Rules.TargetRegionTransportSettings.bEnabled == 0)
      return false;
    return !IsHeterogeneousTransit()
      || ValidCorridorTransitProgress.CompletedAgentIds.Num()
        >= FCrowdDemoValidCorridorTransitKernel::AgentCount;
  }
  void InitializeValidCorridorTransit(
    const FCrowdDemoValidCorridorTransitLayout& Layout)
  {
    ValidCorridorTransitLayout = Layout;
    ValidCorridorTransitProgress = {};
  }
  void RecordValidCorridorTransitStep(
    TConstArrayView<FCrowdDemoValidCorridorTransitStepAgent> Agents)
  {
    if (!IsCorridorTransitProgressScenario()) return;
    FCrowdDemoValidCorridorTransitKernel::UpdateProgress(
      Agents, GetCurrentFixedStepIndex(), ValidCorridorTransitProgress);
  }
  const FCrowdDemoValidCorridorTransitLayout& GetValidCorridorTransitLayout() const
  { return ValidCorridorTransitLayout; }
  const FCrowdDemoValidCorridorTransitProgress& GetValidCorridorTransitProgress() const
  { return ValidCorridorTransitProgress; }
  void InitializeOpenSpawnRelaxation(const FCrowdDemoOpenSpawnRelaxationLayout& Layout);
  bool PrepareOpenSpawnRelaxationBoundary();
  bool ConsumeOpenSpawnBoundaryResets(TConstArrayView<int32> AgentIds);
  bool ArePreparedOpenSpawnBoundaryFactsCurrent() const;
  const TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact>&
    GetPreparedOpenSpawnBoundaryFacts() const
  { return PreparedOpenSpawnBoundaryFacts; }
  const FCrowdDemoPreparedOpenSpawnBoundaryFact* FindPreparedOpenSpawnBoundaryFact(
    int32 AgentId) const
  {
    return PreparedOpenSpawnBoundaryFacts.FindByPredicate(
      [AgentId](const auto& Fact) { return Fact.AgentId == AgentId; });
  }
  void RecordOpenSpawnRelaxationParticleStep(
    TConstArrayView<FCrowdDemoParticleSoftPairInfluence> Influences,
    float MaxActualCorrectionCm,
    float SoftErrorCmP95);
  const FCrowdDemoOpenSpawnRelaxationLayout& GetOpenSpawnRelaxationLayout() const
  { return OpenSpawnRelaxationLayout; }
  FCrowdDemoOpenSpawnRelaxationRuntime& GetOpenSpawnRelaxationRuntime()
  { return OpenSpawnRelaxationRuntime; }
  const FCrowdDemoOpenSpawnRelaxationRuntime& GetOpenSpawnRelaxationRuntime() const
  { return OpenSpawnRelaxationRuntime; }
  void RecordFlowAgentSamples(TConstArrayView<FCrowdDemoRoundFlowAgentSample> Samples, bool bClient);

  void RecordRoundStart(TConstArrayView<FCrowdDemoRoundAgentState> States);
  void RecordRoundInitialState(uint32 InputHash, uint32 InitialStateHash);
  uint32 GetRoundInputHash() const { return RoundInputHash; }
  uint32 GetRoundInitialStateHash() const { return RoundInitialStateHash; }
  int32 GetRoundResetCount() const { return RoundResetCount; }
  int32 GetRoundTransitionOrderViolationCount() const
  { return RoundTransitionOrderViolationCount; }
  void RecordCheckpointComparison(
    TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
    TConstArrayView<FCrowdDemoRoundAgentState> ServerStates,
    int32 StateFrameRevision);
  void RecordRoundResultComparisonAndApplied(
    TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
    const FCrowdDemoRoundResultPacket& Packet);
  void SetSimulatedServerTimeForCheckpoint(float ServerTimeSeconds)
  { SimulatedServerTimeSeconds = ServerTimeSeconds; }

  bool ShouldBuildRoundResult() const;
  int32 AllocateCheckpointStateFrameRevision()
  { return NextCheckpointStateFrameRevision++; }
  int32 AllocateCheckpointRevision() { return ++LastCheckpointRevision; }
  void EnqueueOutgoingRoundResult(FCrowdDemoRoundResultPacket&& Packet);
  bool DequeueOutgoingRoundResult(FCrowdDemoRoundResultPacket& OutPacket);
  void MarkRoundResultBuilt(int32 CheckpointRevision);
  int32 GetRoundInitialOverlapPairCount() const { return RoundInitialOverlapPairCount; }
  int32 GetRoundInitialSevereOverlapPairCount() const { return RoundInitialSevereOverlapPairCount; }

  int32 GetLastAppliedCheckpointStateFrameRevision() const
  { return LastAppliedCheckpointStateFrameRevision; }
  const FCrowdDemoRoundCompareMetrics& GetLastCompareMetrics() const { return LastCompareMetrics; }
  const FCrowdDemoRoundCompareMetrics& GetLastCompletedRoundMetrics() const { return LastCompletedRoundMetrics; }
  const FCrowdDemoRoundCheckpointFrameMetrics& GetLastCorrectionMetrics() const { return LastCorrectionMetrics; }
  void MergeNetworkCorrectionMetrics(const FCrowdDemoRoundCheckpointFrameMetrics& NetworkMetrics);

  void LogStageOnce(const TCHAR* StageName, int32 AgentCount);

private:
  bool SubmitWorkerV2BoundaryInput();
  bool DrainWorkerV2MovementShadowComparisons();
  void ResetBoundaryDerivedStateAfterPublish();
  uint64 WorkerResultConsumerFrameSequence = 0;
  TOptional<FCrowdDemoPreparedRoundCommitPlan> PreparedRoundCommitPlan;
  uint64 WorkerProxyGatherBypassCount = 0;
  uint64 WorkerProxyGatherFallbackCount = 0;
  uint64 WorkerProxyInPlaceRefreshCount = 0;
  uint64 FullBoundarySnapshotPublishCount = 0;
  uint64 FullBoundarySnapshotHashCount = 0;
  uint64 BoundarySnapshotEpochTokenCount = 0;
  uint64 WorkerProxySnapshotBaselineHash = 0;
  bool bCurrentStepUsedWorkerProxySnapshot = false;
  bool bCurrentStepUsedBootstrapBoundarySnapshot = false;
  bool bCurrentStepWorkerDirtyMassApplied = false;
  uint64 CurrentStepWorkerDirtyMassPublishSequence = 0;
  int32 CurrentStepWorkerDirtyMassEntityCount = 0;
  bool bCurrentStepFullWorkerProductionFastPath = false;
  uint64 CurrentStepFullWorkerInputSequence = 0;
  uint64 FullWorkerProductionFastPathStepCount = 0;
  bool bBootstrapBoundarySnapshotPending = false;
  int32 LastBootstrapMassReadPlanRevision = INDEX_NONE;
  uint64 BootstrapMassReadCount = 0;
  FCrowdDemoRoundMassAccessCounts CurrentStepMassAccessCounts;
  uint64 AuthorityMassWriteCount = 0;
  uint64 MassAccessContractViolationCount = 0;
  FCrowdDemoRoundBootstrapPacket PendingBootstrap;
  TMap<int32, FCrowdDemoRoundPlanPacket> PendingPlans;
  TMap<int32, FCrowdDemoRoundResultPacket> PendingResults;
  TArray<FCrowdDemoRoundResultPacket> OutgoingRoundResults;
  FCrowdDemoRoundPlanPacket ActivePlan;
  FCrowdDemoRoundCompareMetrics LastCompareMetrics;
  FCrowdDemoRoundCompareMetrics LastCompletedRoundMetrics;
  FCrowdDemoRoundCheckpointFrameMetrics LastCorrectionMetrics;
  FCrowdMassBoundarySnapshot BoundarySnapshot;
  TArray<FCrowdDemoRoundBoundaryFormationFact> BoundaryFormationFacts;
  TArray<FCrowdDemoRoundBoundaryFacingFact> BoundaryFacingFacts;
  TArray<FCrowdDemoRoundBoundaryBusinessFact> BoundaryBusinessFacts;
  TArray<FCrowdStableEntityRef> CurrentStepMassDirtyEntityRefs;
  uint64 DirtyMassApplyBatchCount = 0;
  uint64 DirtyMassApplyEntityCount = 0;
  uint64 PreparedPlannerDecisionHash = 0;
  TSharedPtr<FCrowdDemoBoundaryFacingWorkState, ESPMode::ThreadSafe>
    BoundaryFacingWorkState;
  float PreparedObstacleMaxReprojectDeltaCm = -1.0f;
  TArray<FCrowdGuidanceCandidate> PreparedTargetRegionGuidanceCandidates;
  TArray<FCrowdGuidanceCandidate> PreparedBusinessGuidanceCandidates;
  TArray<FCrowdDemoPreparedReactiveMotionStep> PreparedReactiveMotionSteps;
  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> PreparedOpenSpawnBoundaryFacts;
  int32 PreparedOpenSpawnBoundaryFixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoProjectileVisualEvent> OutgoingProjectileVisualEvents;
  TArray<FCrowdDemoT7PresentationEvent> OutgoingT7PresentationEvents;
  FCrowdDemoProjectileMetrics ProjectileMetrics;
  TMap<int32, int32> FormationIndexByAgentId;
  FCrowdDemoSharedFlowField SharedFlowField;
  int32 DynamicFlowAnchorCellKey = INDEX_NONE;
  int32 DynamicFlowIntegrationRebuildCount = 0;
  uint32 DynamicFlowRoundHash = 2166136261u;
  int32 DynamicFlowRoundHashFixedStepIndex = INDEX_NONE;
  bool bDynamicFlowIntegrationCacheInvalidated = false;
  FCrowdDemoTargetFact TargetFact;
  bool bTargetStabilityDiagnosticPlanEnabled = false;
  bool bTargetRegionPlanLifecycleDiagnosticPlanEnabled = false;
  FCrowdDemoTargetStabilityRuntime TargetStabilityRuntime;
  FCrowdDemoTargetStabilitySummary TargetStabilitySummary;
  FCrowdDemoTargetPolarTopology PreparedTargetRegionTopology;
  FCrowdDemoTargetPolarTopologySummary TargetRegionTopologySummary;
  TArray<FCrowdDemoTargetRegionTransportAgent> PreparedTargetRegionAgents;
  FCrowdDemoTargetRegionDemandResult PreparedTargetRegionDemand;
  FCrowdDemoTargetRegionFlowPlan PreparedTargetRegionPlan;
  FCrowdDemoTargetRegionQuotaExecutionState TargetRegionQuotaExecution;
  FCrowdDemoTargetRegionPlanValidationResult TargetRegionPlanValidation;
  TArray<FCrowdDemoTargetRegionGuidanceResult> PreparedTargetRegionGuidance;
  FCrowdDemoTargetRegionGuidanceSummary TargetRegionGuidanceSummary;
  uint32 TargetRegionTopologyRoundHash = 2166136261u;
  uint32 TargetRegionDemandRoundHash = 2166136261u;
  uint32 TargetRegionTransportRoundHash = 2166136261u;
  uint32 TargetRegionGuidanceRoundHash = 2166136261u;
  int32 TargetRegionPlanRebuildCount = 0;
  int32 TargetRegionLifetimeRebuildCount = 0;
  int32 TargetRegionTargetRebuildCount = 0;
  int32 TargetRegionEnvironmentRebuildCount = 0;
  int32 TargetRegionMembershipRebuildCount = 0;
  int32 TargetRegionDemandSatisfiedRebuildCount = 0;
  int32 TargetRegionPathInvalidRebuildCount = 0;
  TArray<float> TargetRegionSolverMillisecondsSamples;
  bool bTargetRegionRoundValid = true;
  int32 TargetRegionInvalidStepCount = 0;
  int32 TargetRegionLastInvalidStep = INDEX_NONE;
  int32 TargetRegionValidationFailureCount = 0;
  uint32 TargetRegionValidationRoundHash = 2166136261u;
  int32 TargetRegionGuidanceUnroutedStepCount = 0;
  int32 TargetRegionGuidanceUnroutedAgentSampleCount = 0;
  int32 TargetRegionGuidanceUnroutedAgentMax = 0;
  int32 TargetRegionGuidanceFirstFailureStep = INDEX_NONE;
  int32 TargetRegionGuidanceFirstFailureAgentId = INDEX_NONE;
  bool bTargetRegionFailureFixtureValid = false;
  int32 TargetRegionFailureFixtureStep = INDEX_NONE;
  int32 TargetRegionFailureFixtureKind = 0;
  int32 TargetRegionFailureFixtureAgentId = INDEX_NONE;
  int32 TargetRegionFailureFixtureCellKey = INDEX_NONE;
  uint32 TargetRegionFailureFixtureHash = 0;
  TArray<FCrowdDemoTargetRegionCapabilityCohortRuntime> TargetRegionCapabilityCohorts;
  FCrowdDemoCapabilityProfileSummary CapabilityProfileSummary;
  int32 CapabilityCohortRebuildCount = 0;
  FCrowdDemoTargetRegionPlanLifecycleSummary TargetRegionPlanLifecycleSummary;
  FCrowdDemoTargetRegionPlanLifecycleFixture TargetRegionPlanLifecycleFixture;
  TMap<uint64, FCrowdDemoTargetRegionFlowPlan> TargetRegionPlanResources;
  int32 SoftPressureRollbackSnapshotHitCount = 0;
  int32 SoftPressureRollbackSnapshotMissCount = 0;
  int32 SoftPressureRollbackAgentMismatchCount = 0;
  int32 SoftPressureRollbackReplayedStepCount = 0;
  TSet<int32> FlowGoalReachedAgentIds;
  TSet<int32> FlowWallPassAgentIds;
  TSet<int32> FlowCorridorExitAgentIds;
  TSet<int32> FlowTurnExitAgentIds;
  TMap<int32, float> FlowLowSpeedSecondsByAgentId;
  TSet<int32> FlowCorridorDeadlockAgentIds;
  int32 SharedFlowFieldRebuildCount = 0;
  TSet<FName> LoggedStages;
  FCrowdDemoParticleConstraintSummary LastParticleCandidateSummary;
  FCrowdDemoParticleConstraintSummary LastParticleAppliedSummary;
  TArray<FCrowdDemoLocalPredictiveResult> PreparedLocalPredictiveResults;
  TArray<FCrowdDemoLocalPredictiveGrantState> LocalPredictiveGrantStates;
  FCrowdDemoLocalPredictiveSummary LastLocalPredictiveSummary;
  FCrowdDemoLocalPredictiveDiagnosticFrame LocalPredictiveDiagnosticFrame;
  FCrowdDemoLocalPredictiveComponentFixture LocalPredictiveComponentFixture;
  uint32 LocalPredictiveRoundHash = 2166136261u;
  int32 LocalPredictiveSampleCount = 0;
  int32 LocalPredictiveInvalidStepCount = 0;
  uint32 GuidanceCandidateRoundHash = 2166136261u;
  uint32 GuidanceComposeRoundHash = 2166136261u;
  int32 GuidanceComposeSampleCount = 0;
  TArray<float> ParticleSolverMillisecondsSamples;
  TArray<float> RoundPerformanceStageMsSamples[
    static_cast<uint8>(ECrowdDemoRoundPerformanceStage::Count)];
  TArray<float> FixedStepPipelineMsSamples;
  TArray<float> FixedStepsPerGameFrameSamples;
  TArray<float> FixedStepBacklogMsSamples;
  TArray<float> BoundaryWorkerQueueMsSamples;
  TArray<float> BoundaryWorkerRunMsSamples;
  TArray<float> BoundaryWorkerCriticalPathMsSamples;
  TArray<float> RollbackReplayMsSamples;
  int32 PerformanceCatchupFrameCount = 0;
  int32 PerformanceCatchupCpuBudgetHitCount = 0;
  int32 PerformanceCatchupCpuBudgetConsecutiveCount = 0;
  int32 PerformanceCatchupCpuBudgetConsecutiveMax = 0;
  int32 PerformanceMaxFixedStepsPerFrameHitCount = 0;
  float PerformanceFixedStepBacklogMsMax = 0.0f;
  double PerformanceRoundWallStartSeconds = 0.0;
  double CurrentBoundaryRequestStartSeconds = 0.0;
  float PerformanceRoundSimStartSeconds = 0.0f;
  int32 PendingRollbackReplaySteps = 0;
  float PendingRollbackReplayMilliseconds = 0.0f;
  int32 PerformanceZeroErrorRollbackReplayCount = 0;
  int32 PerformanceTargetTopologyBuildCount = 0;
  int32 PerformanceTargetTopologyCacheHitCount = 0;
  int32 PerformanceTargetDemandFullBuildCount = 0;
  int32 PerformanceTargetDemandPopulationUpdateCount = 0;
  uint32 ParticleCandidateStateHash = 2166136261u;
  uint32 ParticleAppliedStateHash = 2166136261u;
  int32 ParticleInvalidStepCount = 0;
  int32 ParticleGlobalFallbackStepCount = 0;
  int32 ParticleStepCount = 0;
  FCrowdDemoOpenSpawnRelaxationLayout OpenSpawnRelaxationLayout;
  FCrowdDemoOpenSpawnRelaxationRuntime OpenSpawnRelaxationRuntime;
  FCrowdDemoOpenCohortMovementLayout OpenCohortMovementLayout;
  FCrowdDemoOpenCohortMovementProgress OpenCohortMovementProgress;
  FCrowdDemoBidirectionalSwapLayout BidirectionalSwapLayout;
  FCrowdDemoBidirectionalSwapProgress BidirectionalSwapProgress;
  TStaticArray<FCrowdDemoSharedFlowField, 2> BidirectionalSwapFlowFields;
  TStaticArray<FCrowdMassSharedFlowResource, 2>
    RuntimeBidirectionalSwapFlowResources;
  FCrowdDemoValidCorridorTransitLayout ValidCorridorTransitLayout;
  FCrowdDemoValidCorridorTransitProgress ValidCorridorTransitProgress;
  TMap<FCrowdStableEntityRef, FCrowdWorkerMovementControlEntry>
    WorkerScenarioMovementProfiles;
  TSet<FCrowdStableEntityRef> WorkerScenarioFrozenProfiles;
  uint64 LastScenarioObservationGeneration = 0;
  uint64 LastScenarioObservationPublishSequence = 0;
  int64 LastScenarioObservationAbsoluteTick = INDEX_NONE;
  int64 OpenSpawnScenarioAbsoluteOriginTick = INDEX_NONE;
  int32 OpenSpawnScenarioLastCommandTick = 0;
  int64 VatShowcaseScenarioAbsoluteOriginTick = INDEX_NONE;
  int32 VatShowcaseLastMovementHalfCycle = INDEX_NONE;
  TMap<int32, uint32> VatShowcaseLastPresentationSignatureByAgentId;
  bool bValidCorridorTransitHoldCommandSubmitted = false;
  int32 CrossProfileHardViolationCount = 0;
  int32 CrossProfileSweptViolationCount = 0;
  int32 ParticleSettlingWindowCount = 0;
  int32 ParticleSettlingSteps = INDEX_NONE;
  float ParticlePreviousSoftErrorP95 = -1.0f;
  bool bParticleConstraintRunFailure = false;
  FCrowdDemoParticleFailureFixture ParticleFailureFixture;
  FCrowdDemoSoftPressureRouteDiagnosticRuntime SoftPressureRouteDiagnosticRuntime;
  FCrowdDemoSoftPressureRouteDiagnosticSummary SoftPressureRouteDiagnosticSummary;
  TArray<float> CorrectionIntervalPositionP95Samples;
  TArray<float> CorrectionIntervalPositionMaxSamples;
  FCrowdDemoRoundErrorSeries CrossRoundPositionErrorSeries;
  FCrowdDemoRoundErrorSeries CrossRoundCorrectionIntervalErrorSeries;
  float SimulatedServerTimeSeconds = 0.0f;
  float CurrentFixedStepSeconds = 1.0f / 30.0f;
  float CurrentStepStartServerTimeSeconds = 0.0f;
  float CurrentStepEndServerTimeSeconds = 0.0f;
  int32 LastBuiltResultRoundId = 0;
  int32 LastCheckpointRevision = 0;
  int32 NextCheckpointStateFrameRevision = 1;
  int32 LastAppliedCheckpointStateFrameRevision = 0;
  int32 RoundInitialOverlapPairCount = 0;
  int32 RoundInitialSevereOverlapPairCount = 0;
  uint32 RoundInputHash = 0;
  uint32 RoundInitialStateHash = 0;
  int32 RoundResetCount = 0;
  int32 RoundTransitionOrderViolationCount = 0;
  uint64 PlanApplyBoundarySequence = 0;
  uint64 LastClaimedPlanApplyBoundarySequence = MAX_uint64;
  uint64 BoundaryGeneration = 1;
  uint64 TargetResourceOwnerRevision = 1;
  uint64 NextWorkerTaskSequence = 1;
  uint64 NextWorkerV2MovementControlRevision = 1;
  uint64 LastWorkerV2MovementControlGeneration = 0;
  int32 LastWorkerV2MovementControlPlanRevision = INDEX_NONE;
  bool bLastWorkerV2MovementControlTargetActive = false;
  uint64 WorkerV2MovementControlPublishCount = 0;
  uint64 WorkerV2MovementControlReuseCount = 0;
  uint64 NextWorkerV2TargetControlRevision = 1;
  uint64 NextWorkerV2TargetObjectiveRevision = 1;
  uint64 NextWorkerV2ProjectileControlRevision = 1;
  uint64 LastWorkerV2TargetControlSemanticHash = 0;
  uint64 LastWorkerV2TargetObjectiveSemanticHash = 0;
  uint64 LastWorkerV2ProjectileControlSemanticHash = 0;
  uint64 WorkerV2TargetControlPublishCount = 0;
  uint64 WorkerV2TargetControlReuseCount = 0;
  uint64 WorkerV2TargetObjectivePublishCount = 0;
  uint64 WorkerV2TargetObjectiveReuseCount = 0;
  uint64 WorkerV2ProjectileControlPublishCount = 0;
  uint64 WorkerV2ProjectileControlReuseCount = 0;
  uint64 WorkerV2EarlyClockIntentCount = 0;
  bool bWorkerV2TargetStateBootstrapped = false;
  bool bWorkerV2ProjectileStateBootstrapped = false;
  int32 BoundaryPendingFrameCount = 0;
  uint64 WorkerV2MovementShadowCompareCount = 0;
  uint64 WorkerV2MovementShadowMismatchCount = 0;
  double WorkerV2MovementPositionErrorMaxCm = 0.0;
  double WorkerV2MovementVelocityErrorMaxCmps = 0.0;
  double WorkerV2MovementYawErrorMaxDegrees = 0.0;
  uint64 WorkerV2MovementStageCompareCount = 0;
  uint64 WorkerV2MovementStageMismatchCount = 0;
  uint64 WorkerV2MovementStageStaleSkipCount = 0;
  uint64 WorkerV2MovementStageLastExpectedInputSequence = 0;
  double WorkerV2MovementStagePositionErrorMaxCm = 0.0;
  double WorkerV2MovementStageVelocityErrorMaxCmps = 0.0;
  double WorkerV2MovementStageTimeErrorMaxSeconds = 0.0;
  double WorkerV2MovementStageLastPositionErrorMaxCm = 0.0;
  double WorkerV2MovementStageLastVelocityErrorMaxCmps = 0.0;
  uint64 WorkerV2MovementStageLastMismatchCount = 0;
  TMap<uint64, TArray<FCrowdDemoWorkerV2MovementExpectation>>
    PendingWorkerV2MovementExpectations;
  int32 BoundaryStaleResultCount = 0;
  int32 BoundaryOrdinaryBlockWaitCount = 0;
  uint64 FormationMembershipHash = 0;
  int32 FormationMembershipCount = 0;
  int32 FormationCacheRebuildCount = 0;
  int32 RoundResultPipelineQueuedCount = 0;
  int32 RoundResultBoundaryAppliedCount = 0;
  bool bBootstrapApplied = false;
  bool bPlanActive = false;
  bool bStepInProgress = false;

  static int32 CountOverlapPairs(TConstArrayView<FCrowdDemoRoundAgentState> States, float RadiusCm);
  static float Percentile(TArray<float> Values, float Quantile);
};
