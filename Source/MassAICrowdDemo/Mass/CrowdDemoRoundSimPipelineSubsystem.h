#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoHardSeparationPbdKernel.h"
#include "Mass/CrowdDemoSeparationKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"
#include "Mass/CrowdDemoLocalPredictiveInteractionKernel.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"
#include "Mass/CrowdDemoBidirectionalSwapKernel.h"
#include "Mass/CrowdDemoValidCorridorTransitKernel.h"
#include "Mass/CrowdDemoProjectileKernel.h"
#include "Mass/CrowdDemoCapabilityProfileKernel.h"
#include "Mass/CrowdDemoSoftPressureRouteDiagnosticKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoTrafficSchedulingKernel.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoElasticCrowdKernel.h"
#include "Mass/CrowdDemoElasticShadowKernel.h"
#include "Mass/CrowdDemoJointVelocityKernel.h"
#include "Mass/CrowdDemoPursuitPositioningKernel.h"
#include "Mass/CrowdDemoTargetApproachKernel.h"
#include "Mass/CrowdDemoTargetInfluenceKernel.h"
#include "Mass/CrowdDemoTargetInfluenceExecutionDiagnosticKernel.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"
#include "Mass/CrowdDemoTargetStabilityDiagnosticKernel.h"
#include "Mass/CrowdDemoTargetSlotLayoutKernel.h"
#include "Mass/CrowdDemoSf3DeterminismHash.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowdDemoRoundSimPipelineSubsystem.generated.h"

struct FCrowdDemoRoundFlowAgentSample
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bUnreachable = false;
  bool bPenetrating = false;
};

enum class ECrowdDemoFlowReachabilityStage : uint8
{
  StepStart,
  MovementPredict,
  ObstacleConstraint,
  HardPbd,
  ObstacleReproject,
  MovementFinalize
};

struct FCrowdDemoFlowReachabilityStageSample
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  ECrowdDemoFlowLocationStatus Status = ECrowdDemoFlowLocationStatus::OutOfBounds;
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  bool bContinuousPenetrating = false;
};

struct FCrowdDemoFlowReachabilityWitness
{
  bool bValid = false;
  ECrowdDemoFlowReachabilityStage Stage = ECrowdDemoFlowReachabilityStage::StepStart;
  ECrowdDemoFlowLocationStatus PreviousStatus = ECrowdDemoFlowLocationStatus::Reachable;
  ECrowdDemoFlowLocationStatus NextStatus = ECrowdDemoFlowLocationStatus::Reachable;
  int32 PreviousStableCellKey = INDEX_NONE;
  int32 NextStableCellKey = INDEX_NONE;
  FVector WorldDelta = FVector::ZeroVector;
  bool bBlocked = false;
  bool bContinuousPenetrating = false;
  float NearestReachableCellDistanceCm = -1.0f;
};

struct FCrowdDemoOrcaReferenceDifferentialSummary
{
  int32 SampleCount = 0;
  int32 CurrentExactCount = 0;
  int32 ReferenceExactCount = 0;
  int32 CurrentMissReferenceHitCount = 0;
  int32 BothMissOracleHitCount = 0;
  int32 CurrentHitReferenceMissCount = 0;
  int32 AllExactMissCount = 0;
  int32 ContinuousHitQuantizedMissCount = 0;
  int32 ThreeByThreeRecoveredCount = 0;
  int32 OracleWitnessAvailableCount = 0;
  int32 BestEffortUsedCount = 0;
  uint32 MinimumFixtureHash = 0;
  int32 MinimumFixtureConstraintCount = 0;
};

struct FCrowdDemoSf3RollbackAgentState
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  float RadiusCm = 42.0f;
  FCrowdDemoPortalAdmissionFragment Admission;
  FCrowdDemoPassingBandFragment Band;
  FCrowdDemoRoundFlowSampleFragment FlowSample;
  FCrowdDemoPositionAssignmentFragment PositionAssignment;
  FCrowdDemoPursuitSteeringStateFragment PursuitSteering;
  FCrowdDemoPursuitGuidanceFragment PursuitGuidance;
};

struct FCrowdDemoSoftPressureRollbackAgentState
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  float RadiusCm = 42.0f;
  float SimulatedServerTimeSeconds = 0.0f;
  int32 PlanRevision = 0;
  bool bInitialized = false;
  FCrowdDemoRoundFlowSampleFragment FlowSample;
  FCrowdDemoTargetApproachFragment TargetApproach;
  FCrowdDemoOpenSpawnRelaxationFragment OpenSpawnRelaxation;
  FCrowdDemoCombatNetState Combat;
};

struct FCrowdDemoSoftPressureRollbackCombatState
{
  int32 AgentId = INDEX_NONE;
  FCrowdDemoCombatNetState Combat;
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
  bool bRoundValid = true;
};

struct FCrowdDemoSoftPressureRollbackSnapshot
{
  int32 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoSoftPressureRollbackAgentState> Agents;
  TArray<FCrowdDemoLocalPredictiveResult> LocalPredictiveResults;
  TArray<FCrowdDemoLocalPredictiveGrantState> LocalPredictiveGrantStates;
  FCrowdDemoLocalPredictiveSummary LocalPredictiveSummary;
  uint32 LocalPredictiveRoundHash = 2166136261u;
  int32 LocalPredictiveSampleCount = 0;
  int32 LocalPredictiveInvalidStepCount = 0;
  FCrowdDemoParticleConstraintSummary ParticleCandidateSummary;
  FCrowdDemoParticleConstraintSummary ParticleAppliedSummary;
  int32 ParticleSolverMsSampleCount = 0;
  uint32 ParticleCandidateStateHash = 2166136261u;
  uint32 ParticleAppliedStateHash = 2166136261u;
  int32 ParticleInvalidStepCount = 0;
  int32 ParticleGlobalFallbackStepCount = 0;
  int32 ParticleStepCount = 0;
  int32 CrossProfileHardViolationCount = 0;
  int32 CrossProfileSweptViolationCount = 0;
  int32 ParticleSettlingWindowCount = 0;
  int32 ParticleSettlingSteps = INDEX_NONE;
  float ParticlePreviousSoftErrorP95 = -1.0f;
  bool bParticleConstraintRunFailure = false;
  FCrowdDemoParticleFailureFixture ParticleFailureFixture;
  FCrowdDemoOpenSpawnRelaxationLayout OpenSpawnRelaxationLayout;
  FCrowdDemoOpenSpawnRelaxationRuntime OpenSpawnRelaxationRuntime;
  FCrowdDemoOpenCohortMovementLayout OpenCohortMovementLayout;
  FCrowdDemoOpenCohortMovementProgress OpenCohortMovementProgress;
  FCrowdDemoBidirectionalSwapLayout BidirectionalSwapLayout;
  FCrowdDemoBidirectionalSwapProgress BidirectionalSwapProgress;
  FCrowdDemoValidCorridorTransitLayout ValidCorridorTransitLayout;
  FCrowdDemoValidCorridorTransitProgress ValidCorridorTransitProgress;
  FCrowdDemoSoftPressureRouteDiagnosticCheckpoint RouteDiagnosticCheckpoint;
  FCrowdDemoTargetInfluenceExecutionCheckpoint TargetInfluenceExecutionCheckpoint;
  FCrowdDemoTargetStabilityCheckpoint TargetStabilityCheckpoint;
  FCrowdDemoTargetFact TargetFact;
  int32 DynamicFlowAnchorCellKey = INDEX_NONE;
  int32 DynamicFlowIntegrationRebuildCount = 0;
  uint32 DynamicFlowRoundHash = 2166136261u;
  FCrowdDemoTargetSlotLayout TargetSlotLayout;
  FCrowdDemoTargetSlotLayoutSummary TargetSlotLayoutSummary;
  TArray<FCrowdDemoTargetApproachResult> TargetApproachDecisions;
  TArray<FCrowdDemoTargetApproachResult> TargetApproachGuidance;
  FCrowdDemoTargetApproachSummary TargetApproachSummary;
  uint32 TargetApproachCommitHash = 2166136261u;
  TArray<FCrowdDemoTargetInfluenceResult> TargetInfluenceResults;
  FCrowdDemoTargetInfluenceSummary TargetInfluenceSummary;
  uint32 TargetInfluenceRoundHash = 2166136261u;
  FCrowdDemoTargetPolarTopology TargetRegionTopology;
  FCrowdDemoTargetPolarTopologySummary TargetRegionTopologySummary;
  TArray<FCrowdDemoTargetRegionTransportAgent> TargetRegionAgents;
  FCrowdDemoTargetRegionDemandResult TargetRegionDemand;
  FCrowdDemoTargetRegionFlowPlan TargetRegionPlan;
  FCrowdDemoTargetRegionPlanValidationResult TargetRegionPlanValidation;
  TArray<FCrowdDemoTargetRegionGuidanceResult> TargetRegionGuidance;
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
  int32 TargetRegionSolverMsSampleCount = 0;
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
  TSet<int32> FlowGoalReachedAgentIds;
  TSet<int32> FlowWallPassAgentIds;
  TSet<int32> FlowCorridorExitAgentIds;
  TSet<int32> FlowTurnExitAgentIds;
  TMap<int32, float> FlowLowSpeedSecondsByAgentId;
  TSet<int32> FlowCorridorDeadlockAgentIds;
  FCrowdDemoRoundCompareMetrics CompareMetrics;
  TArray<FCrowdDemoProjectileState> Projectiles;
  FCrowdDemoProjectileMetrics ProjectileMetrics;
};

struct FCrowdDemoPreparedSteeringGuidance
{
  int32 AgentId = INDEX_NONE;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
};

struct FCrowdDemoSteeringFirstRuntimeDiagnostic
{
  TArray<int32> StateCounts;
  TArray<float> DistanceP50;
  TArray<float> DistanceP95;
  TArray<float> PreferredForwardP50;
  TArray<float> PreferredForwardP95;
  TArray<float> OrcaForwardP50;
  TArray<float> OrcaForwardP95;
  TArray<float> FinalForwardP50;
  TArray<float> FinalForwardP95;
  int32 PursuitOutsideHandoffCount = 0;
  int32 PursuitInvalidFlowCount = 0;
  TArray<int32> OrcaConstraintSourceMatrix;
  TArray<int32> OrcaInfeasibleCounts;
  TArray<int32> OrcaFallbackStopCounts;
  TArray<int32> ReacquireReasonCounts;
  float CommitArrivalErrorCmP95 = 0.0f;
  int32 CommitNoProgressStepsMax = 0;
  float CommitObstacleCorrectionCmP95 = 0.0f;
  float CommitPbdCorrectionCmP95 = 0.0f;
  int32 CommitPreferredNonzeroOrcaZeroCount = 0;
  float CommitRouteForwardSpeedCmpsP50 = 0.0f;
  float CommitRouteForwardSpeedCmpsP95 = 0.0f;
  int32 StablePhysicalDisplacedCount = 0;
  float StablePhysicalDisplacementCmP95 = 0.0f;
  float StablePhysicalDisplacementCmMax = 0.0f;
  int32 ReservePhysicalDisplacedCount = 0;
  float ReservePhysicalDisplacementCmP95 = 0.0f;
  float ReservePhysicalDisplacementCmMax = 0.0f;
  int32 PhysicallySatisfiedPositionCount = 0;
};

struct FCrowdDemoSf3GoalAgentDiagnostic
{
  int32 AgentId = INDEX_NONE;
  FVector FinalLocation = FVector::ZeroVector;
  FVector FinalVelocity = FVector::ZeroVector;
  FVector2f LastPreferredVelocity = FVector2f::ZeroVector;
  float RadiusCm = 0.0f;
  int32 IntegrationCost = MAX_int32;
  bool bEverReached = false;
  bool bHadReachedNeighbor = false;
  int32 ProcessedAgentSteps = 0;
  int32 PreferredFeasibleCount = 0;
  int32 LpFeasibleCount = 0;
  int32 InfeasibleCount = 0;
  int32 FallbackStopCount = 0;
  int32 StopFeasibleCount = 0;
  int32 StopViolationCount = 0;
  int32 ConstraintsAgainstReached = 0;
  int32 ConstraintsAgainstNonReached = 0;
  int32 LpFailedZeroFeasibleCount = 0;
  int32 LpFailedOracleFeasibleCount = 0;
  int32 LpFailedOracleNoWitnessCount = 0;
  int32 ContinuousFeasibleQuantizedFailureCount = 0;
  int32 GenuineSpeedCircleEmptyCount = 0;
  float CurrentStoppedSeconds = 0.0f;
  float MaxStoppedSeconds = 0.0f;
  TArray<float> NeighborCounts;
  TArray<float> ConstraintCounts;
  TArray<float> OracleCandidateCounts;
};

struct FCrowdDemoSf3RollbackSnapshot
{
  int32 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoSf3RollbackAgentState> Agents;
  TArray<FCrowdDemoTrafficPortalRuntime> Portals;
  FCrowdDemoPursuitTargetFact TargetFact;
    TArray<FCrowdDemoPositionCandidate> PositionCandidates;
    TArray<FCrowdDemoPositionAssignment> PositionAssignments;
    TArray<FCrowdDemoHoldingCandidate> HoldingCandidates;
    FCrowdDemoTransitCapacityResult TransitCapacitySelection;
    TArray<FCrowdDemoHoldingPositionCompatibility> HoldingCompatibilities;
  TArray<FCrowdDemoHoldingAssignment> HoldingAssignments;
  TArray<FCrowdDemoCommitRequest> CommitRequests;
  FCrowdDemoCommitGateResult CommitGateResult;
  TArray<FCrowdDemoPreparedSteeringGuidance> SteeringGuidance;
  FCrowdDemoHoldingSummary HoldingSummary;
  TArray<FCrowdDemoFrontApproachRoute> PositionApproachRoutes;
  TArray<FCrowdDemoFrontPhaseReservationRequest> FrontPhaseReservationRequests;
  FCrowdDemoFrontPhaseReservationResult FrontPhaseReservationResult;
  TArray<FCrowdDemoFrontPhaseReservationDecisionRecord> FrontPhaseReservationDecisions;
  FCrowdDemoFrontAdmissionResult FrontAdmissionResult;
  TArray<FCrowdDemoFrontReservationWaitEdge> FrontReservationWaitEdges;
  FCrowdDemoFrontReservationWaitGraphSummary FrontReservationWaitGraphSummary;
  FCrowdDemoFrontReservationWaitGraphFixture FrontReservationWaitGraphFixture;
  FCrowdDemoPositioningSummary PositioningSummary;
  FCrowdDemoPositionIngressSummary PositionIngressSummary;
  FCrowdDemoPositionIngressFixture PositionIngressFixture;
  TMap<int32, int32> PositionIngressLowSpeedSteps;
  TSet<int32> PositionPromotedAgentIds;
  int32 PositionCandidateBuiltRevision = INDEX_NONE;
  int32 PositionAssignmentRevision = 0;
  uint32 HoldingCompatibilityInputHash = 0;
  uint32 JointAssignmentInputHash = 0;
  FCrowdDemoResidualPositioningSummary ResidualPositioningSummary;
  FCrowdDemoHoldingMatchingResult HoldingMatchingResult;
  FCrowdDemoHoldingHallFixture HoldingHallFixture;
  FCrowdDemoHallGeometryFixture HallGeometryFixture;
  FCrowdDemoJointPositioningResult JointPositioningResult;
  FCrowdDemoJointCommitResidualResult JointCommitResidualResult;
  FCrowdDemoSf4UnfinishedBoundaryFixture UnfinishedBoundaryFixture;
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture PhysicalUnsatisfiedBoundaryFixture;
  FCrowdDemoTransitJointDiagnosticFixture TransitJointDiagnosticFixture;
  TArray<FCrowdDemoJointVelocityAgent> TransitCapacityShadowAgents;
  TArray<FCrowdDemoJointVelocityPair> TransitCapacityShadowPairs;
  TArray<FCrowdDemoJointVelocityComponent> TransitCapacityShadowComponents;
  TArray<FCrowdDemoJointVelocityComponentResult> TransitCapacityShadowResults;
  FCrowdDemoTransitCapacityShadowSummary TransitCapacityShadowSummary;
  int32 TransitCapacityShadowSolverMsSampleCount = 0;
  TArray<FCrowdDemoElasticCrowdAgent> ElasticCrowdShadowAgents;
  TArray<FCrowdDemoElasticCrowdResult> ElasticCrowdShadowResults;
  FCrowdDemoElasticCrowdSummary ElasticCrowdShadowSummary;
  FCrowdDemoElasticShadowParallelState ElasticParallelState;
  TStaticArray<int64, 8> ElasticBaselineDesiredForward = {};
  TStaticArray<int64, 8> ElasticBaselineActualForward = {};
  TStaticArray<int64, 8> ElasticTwinDesiredForward = {};
  TStaticArray<int64, 8> ElasticTwinActualForward = {};
  TMap<int32, int32> ElasticZeroProgressSteps;
  int32 ElasticSpacingDeficitSampleCount = 0;
  int32 ElasticTransitDeficitSampleCount = 0;
  int32 ElasticRecoveryErrorSampleCount = 0;
  int32 ElasticSolverMsSampleCount = 0;
  FCrowdDemoElasticShadowFailureFixture ElasticFailureFixture;
  FCrowdDemoTransitCapacityFailureFixture TransitCapacityFailureFixture;
  uint32 SteeringStateHash = 2166136261u;
  uint32 TrafficRoundHash = 2166136261u;
  uint32 PortalRoundHash = 2166136261u;
  uint32 OrcaRoundHash = 2166136261u;
  uint32 PriorityOrcaRoundHash = 2166136261u;
  int32 TrafficFixedStepIndex = 0;
  FCrowdDemoTrafficMetrics TrafficMetrics;
  int32 TrafficQueueSampleCount = 0;
  int32 TrafficOccupiedSampleCount = 0;
  int32 BandLateralErrorSampleCount = 0;
  int32 OrcaNeighborSampleCount = 0;
  int32 OrcaConstraintSampleCount = 0;
  int32 OrcaSolverMsSampleCount = 0;
  int32 OrcaOracleRecoveryMsSampleCount = 0;
  int32 PhaseReservationHeldStepSampleCount = 0;
  FCrowdDemoParticleConstraintSummary ParticleCandidateSummary;
  FCrowdDemoParticleConstraintSummary ParticleAppliedSummary;
  int32 ParticleSolverMsSampleCount = 0;
  uint32 ParticleCandidateStateHash = 2166136261u;
  uint32 ParticleAppliedStateHash = 2166136261u;
  int32 ParticleInvalidStepCount = 0;
  int32 ParticleGlobalFallbackStepCount = 0;
  int32 ParticleStepCount = 0;
  int32 ParticleSettlingWindowCount = 0;
  int32 ParticleSettlingSteps = INDEX_NONE;
  float ParticlePreviousSoftErrorP95 = -1.0f;
  bool bParticleConstraintRunFailure = false;
  FCrowdDemoParticleFailureFixture ParticleFailureFixture;
  TMap<int32, FCrowdDemoSf3GoalAgentDiagnostic> GoalDiagnostics;
  TMap<int32, FCrowdDemoFlowReachabilityStageSample> FlowReachabilityPreviousStage;
  int32 FlowReachabilityPreviousStep = INDEX_NONE;
};

struct FCrowdDemoPositioningRuntimeDiagnostic
{
  int32 SlotCommitCount = 0;
  int32 ReserveCommitCount = 0;
  int32 PortalOwnedCount = 0;
  int32 OutsideComposeRangeCount = 0;
  int32 GuidanceActiveCount = 0;
  int32 ArrivalSpeedRejectedCount = 0;
  int32 ErrorLe30Count = 0;
  int32 Error31To100Count = 0;
  int32 Error101To300Count = 0;
  int32 ErrorOver300Count = 0;
  int32 PreviousOrcaFallbackCount = 0;
  int32 PreviousOrcaInfeasibleCount = 0;
  int32 PreviousPbdCorrectedCount = 0;
  float SpeedP95 = 0.0f;
  float GuidanceSpeedP95 = 0.0f;
  float OrcaSpeedP95 = 0.0f;
  float ObstacleSpeedP95 = 0.0f;
  int32 OrcaAdjustedCount = 0;
  int32 OrcaZeroCount = 0;
  int32 ObstacleHitCount = 0;
  float OrcaConstraintP95 = 0.0f;
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

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoRoundSimPipelineSubsystem : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  void QueueBootstrap(const FCrowdDemoRoundBootstrapPacket& Packet);
  void QueueRoundPlan(const FCrowdDemoRoundPlanPacket& Packet);
  void QueueRoundResult(const FCrowdDemoRoundResultPacket& Packet);
  void QueueCorrectionFrame(const FCrowdDemoCorrectionFrame& Frame, float ReceiveServerTimeSeconds);

  bool PeekBootstrap(FCrowdDemoRoundBootstrapPacket& OutPacket) const;
  void MarkBootstrapApplied(int32 AgentCount);
  bool PopDueRoundPlan(float BoundaryServerTimeSeconds, FCrowdDemoRoundPlanPacket& OutPacket);
  bool HasDueRoundPlan(float BoundaryServerTimeSeconds) const;
  bool PopCorrectionForBoundary(FCrowdDemoCorrectionFrame& OutFrame, float& OutReceiveServerTimeSeconds);
  bool PopRoundResultForBoundary(FCrowdDemoRoundResultPacket& OutPacket);

  void ActivatePlan(const FCrowdDemoRoundPlanPacket& Packet, int32 AgentCount, bool bLate);
  bool TryClaimPlanApplyBoundary();
  void EnsureFormationIndexCache(TConstArrayView<int32> AgentIds);
  const TMap<int32, int32>& GetFormationIndexByAgentId() const { return FormationIndexByAgentId; }
  uint64 GetFormationMembershipHash() const { return FormationMembershipHash; }
  int32 GetFormationCacheRebuildCount() const { return FormationCacheRebuildCount; }
  bool TryBeginFixedStep(float TargetServerTimeSeconds);
  void FinishFixedStep();
  bool IsActive() const { return bPlanActive; }
  bool IsRoundSimScenarioActive() const;
  float GetCurrentFixedStepSeconds() const { return CurrentFixedStepSeconds; }
  float GetCurrentStepStartServerTimeSeconds() const { return CurrentStepStartServerTimeSeconds; }
  float GetCurrentStepEndServerTimeSeconds() const { return CurrentStepEndServerTimeSeconds; }
  float GetSimulatedServerTimeSeconds() const { return SimulatedServerTimeSeconds; }
  int32 GetCurrentRoundId() const { return ActivePlan.RoundId; }
  int32 GetCurrentPlanRevision() const { return ActivePlan.Revision; }
  const FCrowdDemoRoundPlanPacket& GetActivePlan() const { return ActivePlan; }
  const FCrowdDemoRoundRules& GetRules() const { return ActivePlan.Rules; }
  bool IsRangedProjectileCombat() const
  {
    return IsActive()
      && ActivePlan.Rules.Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && ActivePlan.Rules.SoftPressureTestCase
        == ECrowdDemoSoftPressureTestCase::RangedProjectileCombat
      && ActivePlan.Rules.RangedCombatSettings.bEnabled != 0;
  }

  TArray<FCrowdDemoProjectileState>& GetPreparedProjectiles()
  { return PreparedProjectiles; }
  const TArray<FCrowdDemoProjectileState>& GetPreparedProjectiles() const
  { return PreparedProjectiles; }
  void SetPendingProjectileHitFacts(TArray<FCrowdDemoHitFact>&& HitFacts)
  { PendingProjectileHitFacts = MoveTemp(HitFacts); }
  TArray<FCrowdDemoHitFact> ConsumePendingProjectileHitFacts()
  { return MoveTemp(PendingProjectileHitFacts); }
  void RecordProjectileStep(
    const FCrowdDemoProjectileStepSummary& Summary,
    TConstArrayView<FCrowdDemoProjectileVisualEvent> Events);
  void RecordProjectileHitResponse(const FCrowdDemoHitResponseSummary& Summary);
  FCrowdDemoProjectileMetrics BuildProjectileMetrics() const;
  bool DequeueProjectileVisualEvents(TArray<FCrowdDemoProjectileVisualEvent>& OutEvents);

  TArray<FCrowdDemoSeparationKernelAgent>& GetPreparedSeparationAgents() { return PreparedSeparationAgents; }
  TArray<FCrowdDemoSeparationKernelResult>& GetPreparedSeparationResults() { return PreparedSeparationResults; }
  TMap<int32, int32>& GetPreparedResultIndexByAgentId() { return PreparedResultIndexByAgentId; }
  void SetLastSeparationSummary(int32 GridCells, int32 AppliedAgents, int32 OverlapPairs, int32 SevereOverlapPairs);
  TArray<FCrowdDemoHardSeparationPbdAgent>& GetPreparedPbdAgents() { return PreparedPbdAgents; }
  TArray<FCrowdDemoHardSeparationPbdPair>& GetPreparedPbdPairs() { return PreparedPbdPairs; }
  TArray<FCrowdDemoHardSeparationPbdResult>& GetPreparedPbdResults() { return PreparedPbdResults; }
  TMap<int32, int32>& GetPreparedPbdResultIndexByAgentId() { return PreparedPbdResultIndexByAgentId; }
  void RecordHardSeparationPbdSummary(const FCrowdDemoHardSeparationPbdSummary& Summary, float SolverMilliseconds);
  void RecordPbdSafetyDeltas(float ObstacleReprojectDeltaCm, float FinalSafetyDeltaCm);
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
  bool HasParticleConstraintRunFailure() const { return bParticleConstraintRunFailure; }
  void StopAfterParticleConstraintFailure();
  float GetParticleSolverMsP95() const;
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
  FCrowdDemoTargetFact& GetTargetApproachFact() { return TargetApproachFact; }
  const FCrowdDemoTargetFact& GetTargetApproachFact() const { return TargetApproachFact; }
  FCrowdDemoTargetSlotLayout& GetPreparedTargetSlotLayout() { return PreparedTargetSlotLayout; }
  const FCrowdDemoTargetSlotLayout& GetPreparedTargetSlotLayout() const
  { return PreparedTargetSlotLayout; }
  FCrowdDemoTargetSlotLayoutSummary& GetTargetSlotLayoutSummary()
  { return TargetSlotLayoutSummary; }
  const FCrowdDemoTargetSlotLayoutSummary& GetTargetSlotLayoutSummary() const
  { return TargetSlotLayoutSummary; }
  TArray<FCrowdDemoTargetApproachResult>& GetPreparedTargetApproachResults()
  { return PreparedTargetApproachDecisions; }
  const TArray<FCrowdDemoTargetApproachResult>& GetPreparedTargetApproachResults() const
  { return PreparedTargetApproachDecisions; }
  TArray<FCrowdDemoTargetApproachResult>& GetPreparedTargetApproachGuidance()
  { return PreparedTargetApproachGuidance; }
  const TArray<FCrowdDemoTargetApproachResult>& GetPreparedTargetApproachGuidance() const
  { return PreparedTargetApproachGuidance; }
  const FCrowdDemoTargetApproachSummary& GetTargetApproachSummary() const
  { return TargetApproachSummary; }
  void SetTargetApproachSummary(const FCrowdDemoTargetApproachSummary& Summary)
  { TargetApproachSummary = Summary; }
  uint32 GetTargetApproachCommitHash() const { return TargetApproachCommitHash; }
  void SetTargetApproachCommitHash(uint32 Hash) { TargetApproachCommitHash = Hash; }
  TArray<FCrowdDemoTargetInfluenceResult>& GetPreparedTargetInfluenceResults()
  { return PreparedTargetInfluenceResults; }
  const TArray<FCrowdDemoTargetInfluenceResult>& GetPreparedTargetInfluenceResults() const
  { return PreparedTargetInfluenceResults; }
  const FCrowdDemoTargetInfluenceSummary& GetTargetInfluenceSummary() const
  { return TargetInfluenceSummary; }
  uint32 GetTargetInfluenceRoundHash() const { return TargetInfluenceRoundHash; }
  void RecordTargetInfluenceStep(
    TArray<FCrowdDemoTargetInfluenceResult>&& Results,
    const FCrowdDemoTargetInfluenceSummary& Summary);
  bool IsTargetInfluenceExecutionDiagnosticEnabled() const;
  void RecordTargetInfluenceExecutionStep(
    TConstArrayView<FCrowdDemoTargetInfluenceExecutionSample> Samples,
    const FCrowdDemoTargetPolarEnvironmentSummary& Environment);
  const FCrowdDemoTargetInfluenceExecutionRuntime& GetTargetInfluenceExecutionRuntime() const
  { return TargetInfluenceExecutionRuntime; }
  const FCrowdDemoTargetInfluenceExecutionSummary& GetTargetInfluenceExecutionSummary() const
  { return TargetInfluenceExecutionSummary; }
  void PinTargetInfluenceExecutionDiagnosticForRoundResult();
  const FCrowdDemoTargetInfluenceExecutionRuntime& GetLastCompletedTargetInfluenceExecutionRuntime() const
  { return LastCompletedTargetInfluenceExecutionRuntime; }
  const FCrowdDemoTargetInfluenceExecutionSummary& GetLastCompletedTargetInfluenceExecutionSummary() const
  { return LastCompletedTargetInfluenceExecutionSummary; }
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
  bool EnsureTrafficFlowFields();
  const FCrowdDemoSharedFlowField* FindTrafficFlowField(int32 CohortId) const;
  TArray<FCrowdDemoTrafficAgent>& GetPreparedTrafficAgents() { return PreparedTrafficAgents; }
  TArray<FCrowdDemoTrafficCell>& GetPreparedTrafficCells() { return PreparedTrafficCells; }
  TArray<FCrowdDemoTrafficPortalRuntime>& GetPreparedTrafficPortals() { return PreparedTrafficPortals; }
  FCrowdDemoPortalExtractionSummary& GetPortalExtractionSummary() { return PortalExtractionSummary; }
  const FCrowdDemoPortalExtractionSummary& GetPortalExtractionSummary() const { return PortalExtractionSummary; }
  TArray<FCrowdDemoPortalCandidate>& GetPreparedPortalCandidates() { return PreparedPortalCandidates; }
  TArray<FCrowdDemoPortalDecision>& GetPreparedPortalDecisions() { return PreparedPortalDecisions; }
  TArray<FCrowdDemoOrcaAgent>& GetPreparedOrcaAgents() { return PreparedOrcaAgents; }
  TArray<FCrowdDemoOrcaResult>& GetPreparedOrcaResults() { return PreparedOrcaResults; }
  FCrowdDemoTrafficStepSummary& GetPreparedTrafficSummary() { return LastTrafficStepSummary; }
  FCrowdDemoPursuitTargetFact& GetPursuitTargetFact() { return PursuitTargetFact; }
  FCrowdDemoPursuitPositioningSettings& GetPursuitPositioningSettings() { return PursuitPositioningSettings; }
  TArray<FCrowdDemoPositionCandidate>& GetPreparedPositionCandidates() { return PreparedPositionCandidates; }
  TArray<FCrowdDemoPositionAssignment>& GetPreparedPositionAssignments() { return PreparedPositionAssignments; }
  TArray<FCrowdDemoHoldingCandidate>& GetPreparedHoldingCandidates() { return PreparedHoldingCandidates; }
  FCrowdDemoTransitCapacityResult& GetTransitCapacitySelection() { return TransitCapacitySelection; }
  void RecordTransitCapacitySelection();
  TArray<FCrowdDemoHoldingPositionCompatibility>& GetPreparedHoldingCompatibilities() { return PreparedHoldingCompatibilities; }
  TArray<FCrowdDemoHoldingAssignment>& GetPreparedHoldingAssignments() { return PreparedHoldingAssignments; }
  TArray<FCrowdDemoCommitRequest>& GetPreparedCommitRequests() { return PreparedCommitRequests; }
  FCrowdDemoCommitGateResult& GetPreparedCommitGateResult() { return PreparedCommitGateResult; }
  TArray<FCrowdDemoPreparedSteeringGuidance>& GetPreparedSteeringGuidance() { return PreparedSteeringGuidance; }
  FCrowdDemoHoldingSummary& GetHoldingSummary() { return HoldingSummary; }
  uint32 GetHoldingCompatibilityInputHash() const { return HoldingCompatibilityInputHash; }
  void SetHoldingCompatibilityInputHash(uint32 Hash) { HoldingCompatibilityInputHash = Hash; }
  uint32 GetJointAssignmentInputHash() const { return JointAssignmentInputHash; }
  void SetJointAssignmentInputHash(uint32 Hash) { JointAssignmentInputHash = Hash; }
  void RecordSteeringFirstMetrics(uint32 SteeringHash, int32 PursuitCount,
    int32 HoldingCount, int32 CommitCount, int32 StableCount, int32 ReserveCount,
    int32 ReacquireCount, int32 ArrivedCount, int32 ReleaseCount,
    int32 CommitReleaseCount, int32 NoProgressCount, int32 GhostOwnerCount);
  void RecordSteeringFirstRuntimeDiagnostic(
    const FCrowdDemoSteeringFirstRuntimeDiagnostic& Diagnostic);
  void RecordResidualPositioningSummary(
    const FCrowdDemoResidualPositioningSummary& Summary);
  const FCrowdDemoResidualPositioningSummary& GetResidualPositioningSummary() const
  { return ResidualPositioningSummary; }
  FCrowdDemoHoldingMatchingResult& GetHoldingMatchingResult() { return HoldingMatchingResult; }
  void RecordHoldingMatchingDiagnostic(int32 PositionValidCount, int32 GreedyCount);
  FCrowdDemoHoldingHallFixture& GetHoldingHallFixture() { return HoldingHallFixture; }
  const FCrowdDemoHoldingHallFixture& GetHoldingHallFixture() const { return HoldingHallFixture; }
  void RecordHoldingHallDiagnostic();
  FCrowdDemoHallGeometryFixture& GetHallGeometryFixture() { return HallGeometryFixture; }
  const FCrowdDemoHallGeometryFixture& GetHallGeometryFixture() const { return HallGeometryFixture; }
  void RecordHallGeometryDiagnostic();
  FCrowdDemoJointPositioningResult& GetJointPositioningResult() { return JointPositioningResult; }
  void RecordJointPositioningDiagnostic();
  FCrowdDemoJointCommitResidualResult& GetJointCommitResidualResult() { return JointCommitResidualResult; }
  void RecordJointCommitResidualDiagnostic();
  FCrowdDemoSf4UnfinishedBoundaryFixture& GetUnfinishedBoundaryFixture()
  { return UnfinishedBoundaryFixture; }
  void RecordUnfinishedBoundaryDiagnostic();
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture& GetPhysicalUnsatisfiedBoundaryFixture()
  { return PhysicalUnsatisfiedBoundaryFixture; }
  void RecordPhysicalUnsatisfiedBoundaryDiagnostic();
  TArray<FCrowdDemoFrontApproachRoute>& GetPreparedPositionApproachRoutes() { return PreparedPositionApproachRoutes; }
  TArray<FCrowdDemoFrontPhaseReservationRequest>& GetPreparedFrontPhaseReservationRequests() { return PreparedFrontPhaseReservationRequests; }
  FCrowdDemoFrontPhaseReservationResult& GetPreparedFrontPhaseReservationResult() { return PreparedFrontPhaseReservationResult; }
  TArray<FCrowdDemoFrontPhaseReservationDecisionRecord>& GetPreparedFrontPhaseReservationDecisions() { return PreparedFrontPhaseReservationDecisions; }
  FCrowdDemoFrontAdmissionResult& GetPreparedFrontAdmissionResult() { return PreparedFrontAdmissionResult; }
  TArray<FCrowdDemoFrontReservationWaitEdge>& GetPreparedFrontReservationWaitEdges() { return PreparedFrontReservationWaitEdges; }
  FCrowdDemoFrontReservationWaitGraphSummary& GetFrontReservationWaitGraphSummary() { return FrontReservationWaitGraphSummary; }
  FCrowdDemoFrontReservationWaitGraphFixture& GetFrontReservationWaitGraphFixture() { return FrontReservationWaitGraphFixture; }
  FCrowdDemoPositioningSummary& GetPositioningSummary() { return LastPositioningSummary; }
  const TMap<int32, int32>& GetPositionIngressLowSpeedSteps() const { return PositionIngressLowSpeedStepsByAgentId; }
  bool IsPositionCandidateDirty() const { return PositionCandidateBuiltRevision != PursuitTargetFact.Revision; }
  void MarkPositionCandidatesBuilt() { PositionCandidateBuiltRevision = PursuitTargetFact.Revision; }
  int32 AllocatePositionAssignmentRevision() { return ++PositionAssignmentRevision; }
  void RecordPositioningMetrics(const FCrowdDemoPositioningSummary& Summary,
    int32 StableOccupiedCount, int32 ReserveHoldCount, int32 ChurnCount,
    float ArrivalErrorP95);
  void RecordPositioningDiagnostic(const FCrowdDemoPositioningRuntimeDiagnostic& Diagnostic);
  void RecordPositionIngressDiagnostic(
    const FCrowdDemoPositionIngressSummary& Summary,
    const FCrowdDemoPositionIngressFixture& Fixture,
    TMap<int32, int32>&& LowSpeedStepsByAgentId);
  void RecordPositionPromotionTransitions(TConstArrayView<int32> AgentIds);
  void RecordFrontAdmission(const FCrowdDemoFrontAdmissionResult& Result);
  void RecordFrontPhaseReservationSchedule(
    TConstArrayView<FCrowdDemoFrontPhaseReservationRequest> Requests,
    TConstArrayView<FCrowdDemoFrontPhaseReservationDecisionRecord> Decisions,
    uint32 DecisionHash);
  void RecordFrontPhaseReservationTransitions(
    int32 TransitionCount,
    TConstArrayView<int32> HeldSteps);
  void RecordFrontReservationWaitGraph(
    const FCrowdDemoFrontReservationWaitGraphSummary& Summary,
    const FCrowdDemoFrontReservationWaitGraphFixture& Fixture);
  void RecordSf4ReservationOrcaDiagnostic(
    const FCrowdDemoSf4ReservationOrcaDiagnosticFixture& Fixture);
  void RecordTransitJointDiagnostic(
    const FCrowdDemoTransitJointDiagnosticFixture& Fixture);
  void RecordTransitCapacityShadow(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
    TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
    TConstArrayView<FCrowdDemoJointVelocityComponentResult> Results,
    const FCrowdDemoTransitCapacityShadowSummary& Summary);
  void RecordTransitCapacityFailureFixture(
    const FCrowdDemoTransitCapacityFailureFixture& Fixture);
  void RecordElasticCrowdShadow(
    const FCrowdDemoElasticShadowTwinResult& Twin,
    int32 ZeroProgressStepMax,
    float SolverMs);
  void RecordElasticParallelRollout(
    const FCrowdDemoElasticShadowParallelState& State,
    const FCrowdDemoElasticShadowTwinResult& Step);
  void RecordElasticCrowdFailureFixture(
    const FCrowdDemoElasticShadowFailureFixture& Fixture);
  void RecordTrafficStep(const FCrowdDemoTrafficStepSummary& TrafficSummary, const FCrowdDemoOrcaSummary& OrcaSummary, float OrcaSolverMs);
  void RecordSf3OverlapSample(int32 OverlapPairs, int32 SevereOverlapPairs, int32 ResidualPbdPairs, int32 ObstaclePenetrations);
  void RecordFlowConnectivityStep(
    int32 RecoveredCount,
    int32 DesiredSegmentViolationCount,
    int32 SourceAttachmentSuccessCount,
    int32 UnreachableSampleCount);
  FCrowdDemoTrafficMetrics BuildTrafficMetrics(TConstArrayView<FCrowdDemoRoundAgentState> States) const;
  bool IsSf3DeterminismDiagnosticEnabled() const;
  bool IsSf3GoalCongestionDiagnosticEnabled() const;
  bool IsSf3FlowReachabilityDiagnosticEnabled() const;
  bool IsSf3OrcaReferenceDiagnosticEnabled() const;
  bool IsSf4IngressDiagnosticEnabled() const;
  bool IsSf4ReservationOrcaDiagnosticEnabled() const;
  bool HasCapturedSf4ReservationOrcaDiagnostic() const;
  const FCrowdDemoSf4ReservationOrcaDiagnosticFixture&
    GetSf4ReservationOrcaDiagnosticFixture() const { return Sf4ReservationOrcaDiagnosticFixture; }
  bool IsTransitJointDiagnosticEnabled() const;
  bool HasCapturedTransitJointDiagnostic() const;
  FCrowdDemoAdaptiveSpacingSettings GetTransitJointDiagnosticSettings() const;
  bool IsTransitCapacityShadowEnabled() const;
  bool IsElasticCrowdShadowEnabled() const;
  TArray<FCrowdDemoElasticCrowdAgent>& GetElasticCrowdShadowAgents()
  { return ElasticCrowdShadowAgents; }
  TMap<int32, int32>& GetElasticZeroProgressSteps()
  { return ElasticZeroProgressSteps; }
  const FCrowdDemoElasticShadowFailureFixture& GetElasticCrowdFailureFixture() const
  { return LastCompletedElasticFailureFixture; }
  FCrowdDemoElasticShadowParallelState& GetElasticParallelState()
  { return ElasticParallelState; }
  FCrowdDemoAdaptiveSpacingSettings GetTransitCapacityShadowSettings() const;
  const FCrowdDemoTransitCapacityFailureFixture& GetTransitCapacityFailureFixture() const
  { return LastCompletedTransitCapacityFailureFixture; }
  const FCrowdDemoTransitJointDiagnosticFixture& GetTransitJointDiagnosticFixture() const
  { return LastCompletedTransitJointDiagnosticFixture; }
  void RecordSf3OrcaReferenceDifferential(
    const FCrowdDemoOrcaReferenceDifferentialSummary& Summary);
  void RecordSf3FlowReachabilityStage(
    ECrowdDemoFlowReachabilityStage Stage,
    TConstArrayView<FCrowdDemoFlowReachabilityStageSample> Samples);
  void RecordSf3GoalOrcaStep(
    TConstArrayView<FCrowdDemoOrcaAgent> Agents,
    TConstArrayView<FCrowdDemoOrcaResult> Results);
  static int32 Sf3GoalDistanceBucket(float DistanceCm);
  static int32 Sf3FlowRegionBucket(const FVector& Location, int32 IntegrationCost, float GoalDistanceCm);
  int32 GetCurrentFixedStepIndex() const;
  void RecordSf3StageHash(ECrowdDemoSf3DeterminismStage Stage, uint32 Hash, int32 ItemCount, TConstArrayView<int32> StableKeys = {});
  void LogSf3DiagnosticBoundary(int32 CorrectionRevision, const TCHAR* Phase, int32 FixedStepOverride = INDEX_NONE) const;
  const FCrowdDemoSf3StageHash& GetSf3StageHash(ECrowdDemoSf3DeterminismStage Stage) const;
  void RecordSf3RollbackSnapshot(int32 FixedStepIndex, TArray<FCrowdDemoSf3RollbackAgentState>&& Agents);
  const FCrowdDemoSf3RollbackSnapshot* FindSf3RollbackSnapshot(int32 FixedStepIndex) const;
  void RestoreSf3PortalRuntime(const FCrowdDemoSf3RollbackSnapshot& Snapshot);
  void RecordSoftPressureRollbackSnapshot(
    int32 FixedStepIndex, TArray<FCrowdDemoSoftPressureRollbackAgentState>&& Agents);
  bool CompleteSoftPressureRollbackCombatState(
    int32 FixedStepIndex,
    TConstArrayView<FCrowdDemoSoftPressureRollbackCombatState> CombatStates);
  const FCrowdDemoSoftPressureRollbackSnapshot* FindSoftPressureRollbackSnapshot(
    int32 FixedStepIndex) const;
  void RestoreSoftPressureRuntime(const FCrowdDemoSoftPressureRollbackSnapshot& Snapshot);
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
  void PrepareOpenSpawnRelaxationBoundary();
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
  bool RecordSf3CompletedRoundHash(uint32 AgentStateHash);
  void RecordFlowAgentSamples(TConstArrayView<FCrowdDemoRoundFlowAgentSample> Samples, bool bClient);

  void RecordRoundStart(TConstArrayView<FCrowdDemoRoundAgentState> States);
  void RecordRoundInitialState(uint32 InputHash, uint32 InitialStateHash);
  uint32 GetRoundInputHash() const { return RoundInputHash; }
  uint32 GetRoundInitialStateHash() const { return RoundInitialStateHash; }
  int32 GetRoundResetCount() const { return RoundResetCount; }
  int32 GetRoundTransitionOrderViolationCount() const
  { return RoundTransitionOrderViolationCount; }
  void RecordCorrectionComparisonAndApplied(
    TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
    const FCrowdDemoCorrectionFrame& Frame,
    float CurrentServerTimeSeconds);
  void RecordRoundResultComparisonAndApplied(
    TConstArrayView<FCrowdDemoRoundAgentState> ClientStates,
    const FCrowdDemoRoundResultPacket& Packet);
  void SetSimulatedServerTimeForCorrection(float ServerTimeSeconds) { SimulatedServerTimeSeconds = ServerTimeSeconds; }

  bool ShouldBuildCorrectionFrame() const;
  bool ShouldBuildRoundResult() const;
  int32 AllocateCorrectionRevision() { return NextCorrectionRevision++; }
  int32 AllocateCheckpointRevision() { return ++LastCheckpointRevision; }
  void EnqueueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame&& Frame);
  void EnqueueOutgoingRoundResult(FCrowdDemoRoundResultPacket&& Packet);
  bool DequeueOutgoingCorrectionFrame(FCrowdDemoCorrectionFrame& OutFrame);
  bool DequeueOutgoingRoundResult(FCrowdDemoRoundResultPacket& OutPacket);
  void MarkCorrectionFrameBuilt(float ServerTimeSeconds);
  void MarkRoundResultBuilt(int32 CheckpointRevision);
  int32 GetRoundInitialOverlapPairCount() const { return RoundInitialOverlapPairCount; }
  int32 GetRoundInitialSevereOverlapPairCount() const { return RoundInitialSevereOverlapPairCount; }
  int32 GetLastSeparationGridCellCount() const { return LastSeparationGridCellCount; }
  int32 GetLastSeparationAppliedAgentCount() const { return LastSeparationAppliedAgentCount; }
  int32 GetLastSeparationOverlapPairCount() const { return LastSeparationOverlapPairCount; }
  int32 GetLastSeparationSevereOverlapPairCount() const { return LastSeparationSevereOverlapPairCount; }
  int32 GetLastPbdCorrectedAgentCount() const { return LastCompareMetrics.PbdCorrectedAgentCount; }
  int32 GetLastPbdCorrectedPairCount() const { return LastCompareMetrics.PbdCorrectedPairCount; }
  float GetLastPbdMaxPairCorrectionCm() const { return LastCompareMetrics.PbdMaxPairCorrectionCm; }
  float GetLastPbdMaxAgentTotalCorrectionCm() const { return LastCompareMetrics.PbdMaxAgentTotalCorrectionCm; }
  float GetLastPbdMaxObstacleReprojectDeltaCm() const { return LastCompareMetrics.PbdMaxObstacleReprojectDeltaCm; }
  float GetLastPbdMaxFinalSafetyDeltaCm() const { return LastCompareMetrics.PbdMaxFinalSafetyDeltaCm; }
  float GetPbdSolverMsP95() const { return LastCompareMetrics.PbdSolverMsP95; }

  int32 GetLastAppliedCorrectionRevision() const { return LastAppliedCorrectionRevision; }
  const FCrowdDemoRoundCompareMetrics& GetLastCompareMetrics() const { return LastCompareMetrics; }
  const FCrowdDemoRoundCompareMetrics& GetLastCompletedRoundMetrics() const { return LastCompletedRoundMetrics; }
  const FCrowdDemoHoldingHallFixture& GetLastCompletedHoldingHallFixture() const
  { return LastCompletedHoldingHallFixture; }
  const FCrowdDemoHallGeometryFixture& GetLastCompletedHallGeometryFixture() const
  { return LastCompletedHallGeometryFixture; }
  const FCrowdDemoJointPositioningResult& GetLastCompletedJointPositioningResult() const
  { return LastCompletedJointPositioningResult; }
  const FCrowdDemoJointCommitResidualResult& GetLastCompletedJointCommitResidualResult() const
  { return LastCompletedJointCommitResidualResult; }
  const FCrowdDemoCorrectionFrameMetrics& GetLastCorrectionMetrics() const { return LastCorrectionMetrics; }
  void MergeNetworkCorrectionMetrics(const FCrowdDemoCorrectionFrameMetrics& NetworkMetrics);

  void LogStageOnce(const TCHAR* StageName, int32 AgentCount);

private:
  FCrowdDemoRoundBootstrapPacket PendingBootstrap;
  TMap<int32, FCrowdDemoRoundPlanPacket> PendingPlans;
  TMap<int32, TPair<FCrowdDemoCorrectionFrame, float>> PendingCorrections;
  TMap<int32, FCrowdDemoRoundResultPacket> PendingResults;
  TArray<FCrowdDemoCorrectionFrame> OutgoingCorrectionFrames;
  TArray<FCrowdDemoRoundResultPacket> OutgoingRoundResults;
  FCrowdDemoRoundPlanPacket ActivePlan;
  FCrowdDemoRoundCompareMetrics LastCompareMetrics;
  FCrowdDemoRoundCompareMetrics LastCompletedRoundMetrics;
  FCrowdDemoHoldingHallFixture LastCompletedHoldingHallFixture;
  FCrowdDemoHallGeometryFixture LastCompletedHallGeometryFixture;
  FCrowdDemoJointPositioningResult LastCompletedJointPositioningResult;
  FCrowdDemoJointCommitResidualResult LastCompletedJointCommitResidualResult;
  FCrowdDemoCorrectionFrameMetrics LastCorrectionMetrics;
  TArray<FCrowdDemoProjectileState> PreparedProjectiles;
  TArray<FCrowdDemoHitFact> PendingProjectileHitFacts;
  TArray<FCrowdDemoProjectileVisualEvent> OutgoingProjectileVisualEvents;
  FCrowdDemoProjectileMetrics ProjectileMetrics;
  TArray<FCrowdDemoSeparationKernelAgent> PreparedSeparationAgents;
  TArray<FCrowdDemoSeparationKernelResult> PreparedSeparationResults;
  TMap<int32, int32> PreparedResultIndexByAgentId;
  TArray<FCrowdDemoHardSeparationPbdAgent> PreparedPbdAgents;
  TArray<FCrowdDemoHardSeparationPbdPair> PreparedPbdPairs;
  TArray<FCrowdDemoHardSeparationPbdResult> PreparedPbdResults;
  TMap<int32, int32> PreparedPbdResultIndexByAgentId;
  TMap<int32, int32> FormationIndexByAgentId;
  FCrowdDemoSharedFlowField SharedFlowField;
  int32 DynamicFlowAnchorCellKey = INDEX_NONE;
  int32 DynamicFlowIntegrationRebuildCount = 0;
  uint32 DynamicFlowRoundHash = 2166136261u;
  bool bDynamicFlowIntegrationCacheInvalidated = false;
  TMap<int32, FCrowdDemoSharedFlowField> TrafficFlowFields;
  TArray<FCrowdDemoTrafficAgent> PreparedTrafficAgents;
  TArray<FCrowdDemoTrafficCell> PreparedTrafficCells;
  TArray<FCrowdDemoTrafficPortalRuntime> PreparedTrafficPortals;
  FCrowdDemoPortalExtractionSummary PortalExtractionSummary;
  TArray<FCrowdDemoPortalCandidate> PreparedPortalCandidates;
  TArray<FCrowdDemoPortalDecision> PreparedPortalDecisions;
  TArray<FCrowdDemoOrcaAgent> PreparedOrcaAgents;
  TArray<FCrowdDemoOrcaResult> PreparedOrcaResults;
  FCrowdDemoPursuitTargetFact PursuitTargetFact;
  FCrowdDemoTargetFact TargetApproachFact;
  FCrowdDemoTargetSlotLayout PreparedTargetSlotLayout;
  FCrowdDemoTargetSlotLayoutSummary TargetSlotLayoutSummary;
  TArray<FCrowdDemoTargetApproachResult> PreparedTargetApproachDecisions;
  TArray<FCrowdDemoTargetApproachResult> PreparedTargetApproachGuidance;
  FCrowdDemoTargetApproachSummary TargetApproachSummary;
  uint32 TargetApproachCommitHash = 2166136261u;
  TArray<FCrowdDemoTargetInfluenceResult> PreparedTargetInfluenceResults;
  FCrowdDemoTargetInfluenceSummary TargetInfluenceSummary;
  uint32 TargetInfluenceRoundHash = 2166136261u;
  FCrowdDemoTargetInfluenceExecutionRuntime TargetInfluenceExecutionRuntime;
  FCrowdDemoTargetInfluenceExecutionSummary TargetInfluenceExecutionSummary;
  FCrowdDemoTargetInfluenceExecutionRuntime LastCompletedTargetInfluenceExecutionRuntime;
  FCrowdDemoTargetInfluenceExecutionSummary LastCompletedTargetInfluenceExecutionSummary;
  bool bTargetInfluenceExecutionDiagnosticPlanEnabled = false;
  bool bTargetStabilityDiagnosticPlanEnabled = false;
  FCrowdDemoTargetStabilityRuntime TargetStabilityRuntime;
  FCrowdDemoTargetStabilitySummary TargetStabilitySummary;
  FCrowdDemoTargetPolarTopology PreparedTargetRegionTopology;
  FCrowdDemoTargetPolarTopologySummary TargetRegionTopologySummary;
  TArray<FCrowdDemoTargetRegionTransportAgent> PreparedTargetRegionAgents;
  FCrowdDemoTargetRegionDemandResult PreparedTargetRegionDemand;
  FCrowdDemoTargetRegionFlowPlan PreparedTargetRegionPlan;
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
  FCrowdDemoPursuitPositioningSettings PursuitPositioningSettings;
  TArray<FCrowdDemoPositionCandidate> PreparedPositionCandidates;
  TArray<FCrowdDemoPositionAssignment> PreparedPositionAssignments;
  TArray<FCrowdDemoHoldingCandidate> PreparedHoldingCandidates;
  FCrowdDemoTransitCapacityResult TransitCapacitySelection;
  TArray<FCrowdDemoHoldingPositionCompatibility> PreparedHoldingCompatibilities;
  TArray<FCrowdDemoHoldingAssignment> PreparedHoldingAssignments;
  TArray<FCrowdDemoCommitRequest> PreparedCommitRequests;
  FCrowdDemoCommitGateResult PreparedCommitGateResult;
  TArray<FCrowdDemoPreparedSteeringGuidance> PreparedSteeringGuidance;
  FCrowdDemoHoldingSummary HoldingSummary;
  uint32 HoldingCompatibilityInputHash = 0;
  uint32 JointAssignmentInputHash = 0;
  FCrowdDemoResidualPositioningSummary ResidualPositioningSummary;
  FCrowdDemoHoldingMatchingResult HoldingMatchingResult;
  FCrowdDemoHoldingHallFixture HoldingHallFixture;
  FCrowdDemoHallGeometryFixture HallGeometryFixture;
  FCrowdDemoJointPositioningResult JointPositioningResult;
  FCrowdDemoJointCommitResidualResult JointCommitResidualResult;
  FCrowdDemoSf4UnfinishedBoundaryFixture UnfinishedBoundaryFixture;
  FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture PhysicalUnsatisfiedBoundaryFixture;
  uint32 SteeringStateHash = 2166136261u;
  TArray<FCrowdDemoFrontApproachRoute> PreparedPositionApproachRoutes;
  TArray<FCrowdDemoFrontPhaseReservationRequest> PreparedFrontPhaseReservationRequests;
  FCrowdDemoFrontPhaseReservationResult PreparedFrontPhaseReservationResult;
  TArray<FCrowdDemoFrontPhaseReservationDecisionRecord> PreparedFrontPhaseReservationDecisions;
  FCrowdDemoFrontAdmissionResult PreparedFrontAdmissionResult;
  TArray<FCrowdDemoFrontReservationWaitEdge> PreparedFrontReservationWaitEdges;
  FCrowdDemoFrontReservationWaitGraphSummary FrontReservationWaitGraphSummary;
  FCrowdDemoFrontReservationWaitGraphFixture FrontReservationWaitGraphFixture;
  FCrowdDemoSf4ReservationOrcaDiagnosticFixture Sf4ReservationOrcaDiagnosticFixture;
  int32 Sf4ReservationOrcaCapturedRoundId = INDEX_NONE;
  FCrowdDemoTransitJointDiagnosticFixture TransitJointDiagnosticFixture;
  FCrowdDemoTransitJointDiagnosticFixture LastCompletedTransitJointDiagnosticFixture;
  int32 TransitJointDiagnosticCapturedRoundId = INDEX_NONE;
  TArray<FCrowdDemoJointVelocityAgent> TransitCapacityShadowAgents;
  TArray<FCrowdDemoJointVelocityPair> TransitCapacityShadowPairs;
  TArray<FCrowdDemoJointVelocityComponent> TransitCapacityShadowComponents;
  TArray<FCrowdDemoJointVelocityComponentResult> TransitCapacityShadowResults;
  FCrowdDemoTransitCapacityShadowSummary TransitCapacityShadowSummary;
  TArray<float> TransitCapacityShadowSolverMsSamples;
  TArray<FCrowdDemoElasticCrowdAgent> ElasticCrowdShadowAgents;
  TArray<FCrowdDemoElasticCrowdResult> ElasticCrowdShadowResults;
  FCrowdDemoElasticCrowdSummary ElasticCrowdShadowSummary;
  FCrowdDemoElasticShadowParallelState ElasticParallelState;
  TStaticArray<int64, 8> ElasticBaselineDesiredForward = {};
  TStaticArray<int64, 8> ElasticBaselineActualForward = {};
  TStaticArray<int64, 8> ElasticTwinDesiredForward = {};
  TStaticArray<int64, 8> ElasticTwinActualForward = {};
  TMap<int32, int32> ElasticZeroProgressSteps;
  TArray<float> ElasticSpacingDeficitSamples;
  TArray<float> ElasticTransitDeficitSamples;
  TArray<float> ElasticRecoveryErrorSamples;
  TArray<float> ElasticSolverMsSamples;
  FCrowdDemoElasticShadowFailureFixture ElasticFailureFixture;
  FCrowdDemoElasticShadowFailureFixture LastCompletedElasticFailureFixture;
  FCrowdDemoTransitCapacityFailureFixture TransitCapacityFailureFixture;
  FCrowdDemoTransitCapacityFailureFixture LastCompletedTransitCapacityFailureFixture;
  FCrowdDemoPositioningSummary LastPositioningSummary;
  FCrowdDemoPositionIngressSummary LastPositionIngressSummary;
  FCrowdDemoPositionIngressFixture MinimumPositionIngressFixture;
  TMap<int32, int32> PositionIngressLowSpeedStepsByAgentId;
  TSet<int32> PositionPromotedAgentIds;
  int32 PositionCandidateBuiltRevision = INDEX_NONE;
  int32 PositionAssignmentRevision = 0;
  FCrowdDemoTrafficStepSummary LastTrafficStepSummary;
  FCrowdDemoOrcaSummary LastOrcaSummary;
  uint32 TrafficRoundHash = 2166136261u;
  uint32 PortalRoundHash = 2166136261u;
  uint32 OrcaRoundHash = 2166136261u;
  uint32 PriorityOrcaRoundHash = 2166136261u;
  int32 TrafficFixedStepIndex = 0;
  TArray<float> TrafficQueueSamples;
  TArray<float> TrafficOccupiedSamples;
  TArray<float> BandLateralErrorSamples;
  TArray<float> OrcaNeighborSamples;
  TArray<float> OrcaConstraintSamples;
  TArray<float> OrcaSolverMsSamples;
  TArray<float> OrcaOracleRecoveryMsSamples;
  TArray<float> PhaseReservationHeldStepSamples;
  uint32 FirstCompletedSf3AgentStateHash = 0;
  TStaticArray<FCrowdDemoSf3StageHash, static_cast<int32>(ECrowdDemoSf3DeterminismStage::Count)> Sf3StageHashes;
  TMap<int32, TStaticArray<FCrowdDemoSf3StageHash, static_cast<int32>(ECrowdDemoSf3DeterminismStage::Count)>> Sf3StageHashHistory;
  TMap<int32, FCrowdDemoSf3RollbackSnapshot> Sf3RollbackHistory;
  TMap<int32, FCrowdDemoSoftPressureRollbackSnapshot> SoftPressureRollbackHistory;
  int32 SoftPressureRollbackSnapshotHitCount = 0;
  int32 SoftPressureRollbackSnapshotMissCount = 0;
  int32 SoftPressureRollbackAgentMismatchCount = 0;
  int32 SoftPressureRollbackReplayedStepCount = 0;
  TMap<int32, FCrowdDemoSf3GoalAgentDiagnostic> Sf3GoalDiagnostics;
  TMap<int32, FCrowdDemoFlowReachabilityStageSample> FlowReachabilityPreviousStage;
  TStaticArray<FCrowdDemoFlowReachabilityWitness, 3> FlowReachabilityWitnesses;
  TSet<int32> FlowFinalInvalidAgentIds;
  int32 FlowReachabilityPreviousStep = INDEX_NONE;
  TSet<int32> FlowGoalReachedAgentIds;
  TSet<int32> FlowWallPassAgentIds;
  TSet<int32> FlowCorridorExitAgentIds;
  TSet<int32> FlowTurnExitAgentIds;
  TMap<int32, float> FlowLowSpeedSecondsByAgentId;
  TSet<int32> FlowCorridorDeadlockAgentIds;
  int32 SharedFlowFieldRebuildCount = 0;
  TSet<FName> LoggedStages;
  TArray<float> FlowSeparationOverlapPairSamples;
  TArray<float> FlowSeparationSevereOverlapPairSamples;
  TArray<float> PbdSolverMillisecondsSamples;
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
  TArray<float> ParticleSolverMillisecondsSamples;
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
  FCrowdDemoValidCorridorTransitLayout ValidCorridorTransitLayout;
  FCrowdDemoValidCorridorTransitProgress ValidCorridorTransitProgress;
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
  float LastCorrectionBuildServerTimeSeconds = -1000.0f;
  int32 LastBuiltResultRoundId = 0;
  int32 LastCheckpointRevision = 0;
  int32 NextCorrectionRevision = 1;
  int32 LastAppliedCorrectionRevision = 0;
  int32 RoundInitialOverlapPairCount = 0;
  int32 RoundInitialSevereOverlapPairCount = 0;
  uint32 RoundInputHash = 0;
  uint32 RoundInitialStateHash = 0;
  int32 RoundResetCount = 0;
  int32 RoundTransitionOrderViolationCount = 0;
  int32 LastSeparationGridCellCount = 0;
  int32 LastSeparationAppliedAgentCount = 0;
  int32 LastSeparationOverlapPairCount = 0;
  int32 LastSeparationSevereOverlapPairCount = 0;
  uint64 PlanApplyBoundarySequence = 0;
  uint64 LastClaimedPlanApplyBoundarySequence = MAX_uint64;
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
