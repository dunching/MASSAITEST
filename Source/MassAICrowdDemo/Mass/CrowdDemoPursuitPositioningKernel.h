#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoPursuitTargetFact
{
  int32 TargetId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float RadiusCm = 0.0f;
  int32 Revision = 0;
  int32 StableMotionStep = 0;
};

struct FCrowdDemoPursuitPositioningSettings
{
  float AllowedDistanceMinCm = 80.0f;
  float AllowedDistanceMaxCm = 700.0f;
  float PreferredDistanceMinCm = 120.0f;
  float PreferredDistanceMaxCm = 360.0f;
  float SafetyGapCm = 10.0f;
  float PositionQuantumCm = 1.0f;
  int32 AngularSectorCount = 32;
  int32 MaxAssignmentProposalRounds = 64;
  int32 ReserveRolePenalty = 400;
  int32 ReassignmentPenalty = 250;
  int32 ExistingAssignmentReuseBonus = 1000;
  int32 ApproachSectorChangePenalty = 20;
  int32 FrontAdmissionWaveSize = 2;
  int32 FrontAdmissionTimeoutSteps = 180;
  float FrontAdmissionHoldRangeCm = 1200.0f;
  float FrontApproachRadialToleranceCm = 30.0f;
  float FrontApproachAngularCommitToleranceRadians = 0.10f;
  int32 FrontApproachNoProgressTimeoutSteps = 180;
  float HoldingGapCm = 10.0f;
  int32 HoldingRadialBandCount = 4;
  float HoldingToleranceCm = 30.0f;
  float HoldingReadinessSpeedCmps = 20.0f;
  float HoldingArriveSlowdownDistanceCm = 200.0f;
  float StableHoldGainPerSecond = 1.0f;
  float SteeringVelocityQuantumCmps = 1.0f;
};

enum class ECrowdDemoPositionRole : uint8
{
  Front,
  Reserve
};

enum class ECrowdDemoPursuitPositionState : uint8
{
  Pursuit,
  FrontAssignedWaiting,
  FrontCommitGranted,
  SlotCommit,
  StableOccupied,
  ReserveCommit,
  ReserveHold,
  Reacquire
};

enum class ECrowdDemoFrontApproachPhase : uint8
{
  None,
  RadialStage,
  AngularAlign,
  RadialCommit
};

struct FCrowdDemoFrontApproachRoute
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  FVector2f OuterGate = FVector2f::ZeroVector;
  FVector2f EntryAxis = FVector2f::ZeroVector;
  FVector2f TargetToCandidateDirection = FVector2f::ZeroVector;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
  int32 CandidateSector = INDEX_NONE;
  int32 CurrentSector = INDEX_NONE;
  int32 TurnDirectionKey = 0;
  ECrowdDemoFrontApproachPhase Phase = ECrowdDemoFrontApproachPhase::None;
  int32 RouteRevision = 0;
  int32 RouteErrorBucket = MAX_int32;
  float RadialErrorCm = 0.0f;
  float AngularErrorRadians = 0.0f;
  bool bGateReachable = false;
  bool bArcReachable = false;
  bool bRadialCommitClear = false;
  bool bTargetExclusionClear = false;
  uint32 RouteHash = 0;
  TArray<FVector2f> RoutePoints;
};

struct FCrowdDemoFrontAdmissionAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 PositionId = INDEX_NONE;
  ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
  ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
  ECrowdDemoFrontApproachPhase ApproachPhase = ECrowdDemoFrontApproachPhase::None;
  int32 CommitGrantedStep = INDEX_NONE;
  bool bRouteValid = false;
  int32 NoProgressSteps = 0;
  TArray<FVector2f> RoutePoints;
};

struct FCrowdDemoFrontAdmissionResult
{
  TArray<int32> GrantedAgentIds;
  TArray<int32> RequeuedAgentIds;
  int32 ActiveCommitCount = 0;
  int32 WaitingCount = 0;
  uint32 DecisionHash = 0;
};

struct FCrowdDemoFrontPhaseReservationRequest
{
  int32 AgentId = INDEX_NONE;
  int32 CommitGrantedStep = INDEX_NONE;
  float RadiusCm = 42.0f;
  ECrowdDemoFrontApproachPhase CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequestedPhase = ECrowdDemoFrontApproachPhase::None;
  bool bHasRequest = false;
  bool bRequestValid = false;
  bool bTargetExclusionClear = false;
  TArray<FVector2f> CurrentReservationPoints;
  TArray<FVector2f> RequestedReservationPoints;
};

struct FCrowdDemoFrontPhaseReservationBlockPair
{
  int32 RequesterAgentId = INDEX_NONE;
  int32 BlockerAgentId = INDEX_NONE;
  bool bBlockerGrantedPath = false;
};

enum class ECrowdDemoFrontPhaseReservationDecision : uint8
{
  None,
  Granted,
  Held,
  Invalid
};

enum class ECrowdDemoFrontPhaseReservationReason : uint8
{
  None,
  RouteConflict,
  TargetExclusion,
  InvalidRoute,
  AdmissionRequeue
};

struct FCrowdDemoFrontPhaseReservationDecisionRecord
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoFrontApproachPhase CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequestedPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontPhaseReservationDecision Decision = ECrowdDemoFrontPhaseReservationDecision::None;
  ECrowdDemoFrontPhaseReservationReason Reason = ECrowdDemoFrontPhaseReservationReason::None;
};

struct FCrowdDemoFrontPhaseReservationState
{
  ECrowdDemoFrontApproachPhase CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequestedPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontPhaseReservationDecision Decision = ECrowdDemoFrontPhaseReservationDecision::None;
  ECrowdDemoFrontPhaseReservationReason InvalidReason = ECrowdDemoFrontPhaseReservationReason::None;
  int32 AppliedRevision = INDEX_NONE;
  int32 HeldSteps = 0;
};

enum class ECrowdDemoFrontReservationConflictSegmentKind : uint8
{
  RequestedVsCurrent,
  RequestedVsGranted
};

struct FCrowdDemoFrontReservationWaitAgent
{
  int32 AgentId = INDEX_NONE;
  float RadiusCm = 42.0f;
  ECrowdDemoFrontApproachPhase CurrentPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequestedPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontPhaseReservationDecision Decision = ECrowdDemoFrontPhaseReservationDecision::None;
  bool bHasRequest = false;
  bool bRequestValid = false;
  bool bTargetExclusionClear = false;
  bool bActiveMember = true;
  int32 HeldSteps = 0;
  int32 NoProgressSteps = 0;
  int32 RouteForwardVelocityBucket = 0;
  TArray<FVector2f> CurrentReservationPoints;
  TArray<FVector2f> RequestedReservationPoints;
};

struct FCrowdDemoFrontReservationWaitEdge
{
  int32 RequesterAgentId = INDEX_NONE;
  ECrowdDemoFrontApproachPhase RequesterCurrentPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequesterRequestedPhase = ECrowdDemoFrontApproachPhase::None;
  int32 BlockerAgentId = INDEX_NONE;
  ECrowdDemoFrontApproachPhase BlockerCurrentPhase = ECrowdDemoFrontApproachPhase::None;
  bool bBlockerHasRequest = false;
  int32 RequesterHeldSteps = 0;
  int32 BlockerNoProgressSteps = 0;
  int32 BlockerRouteForwardVelocityBucket = 0;
  ECrowdDemoFrontReservationConflictSegmentKind SegmentKind =
    ECrowdDemoFrontReservationConflictSegmentKind::RequestedVsCurrent;
};

struct FCrowdDemoFrontReservationWaitGraphSummary
{
  int32 UniqueBlockedRequestCount = 0;
  int32 UniqueBlockerCount = 0;
  int32 WaitEdgeCount = 0;
  int32 ReciprocalEdgeCount = 0;
  int32 CycleCount = 0;
  int32 MaxCycleSize = 0;
  int32 StalledBlockerCount = 0;
  int32 ProgressingBlockerCount = 0;
  int32 StaleOwnerCount = 0;
  int32 BlockerRadialCount = 0;
  int32 BlockerAngularCount = 0;
  int32 BlockerRadialCommitCount = 0;
  int32 AtomicHandoffCycleCount = 0;
  int32 MaxAtomicHandoffSetSize = 0;
  uint32 WaitGraphHash = 2166136261u;
};

struct FCrowdDemoFrontReservationWaitGraphFixture
{
  bool bValid = false;
  FCrowdDemoPursuitTargetFact Target;
  FCrowdDemoPursuitPositioningSettings Settings;
  TArray<FCrowdDemoFrontReservationWaitAgent> Agents;
  TArray<FCrowdDemoFrontReservationWaitEdge> Edges;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoFrontPhaseReservationResult
{
  TArray<int32> GrantedAgentIds;
  TArray<int32> HeldAgentIds;
  TArray<int32> InvalidAgentIds;
  TArray<FCrowdDemoFrontPhaseReservationBlockPair> BlockingPairs;
  uint32 DecisionHash = 2166136261u;
};

struct FCrowdDemoPositionCandidate
{
  int32 PositionId = INDEX_NONE;
  int32 TargetId = INDEX_NONE;
  ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
  FVector2f LocalOffset = FVector2f::ZeroVector;
  FVector2f WorldLocation = FVector2f::ZeroVector;
  int32 StableCellKey = INDEX_NONE;
  int32 RadialBand = INDEX_NONE;
  int32 AngularSector = INDEX_NONE;
  int32 Capacity = 1;
  bool bReachable = false;
  bool bClearanceValid = false;
};

struct FCrowdDemoPositioningAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 ExistingPositionId = INDEX_NONE;
  ECrowdDemoPositionRole ExistingRole = ECrowdDemoPositionRole::Reserve;
  ECrowdDemoPursuitPositionState ExistingState = ECrowdDemoPursuitPositionState::Pursuit;
  int32 PreferredApproachSector = INDEX_NONE;
};

struct FCrowdDemoPositionAssignment
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
  ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
  FVector2f DesiredLocation = FVector2f::ZeroVector;
  int32 IntegerCost = MAX_int32;
  bool bReused = false;
};

struct FCrowdDemoPositioningSummary
{
  int32 CandidateCount = 0;
  int32 FrontCapacity = 0;
  int32 ReserveCapacity = 0;
  int32 AssignedCount = 0;
  int32 UnassignedCount = 0;
  int32 ReusedCount = 0;
  int32 ChangedCount = 0;
  int32 PromotionCount = 0;
  int32 InvalidatedCount = 0;
  int32 CandidateOverlapCount = 0;
  int32 CandidateUnreachableCount = 0;
  uint32 CandidateHash = 0;
  uint32 AssignmentHash = 0;
};

enum class ECrowdDemoPursuitSteeringState : uint8
{
  Pursuit,
  Holding,
  Commit,
  StableOccupied,
  ReserveHold,
  Reacquire
};

struct FCrowdDemoHoldingCandidate
{
  int32 HoldingId = INDEX_NONE;
  int32 TargetId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  int32 RadialBand = INDEX_NONE;
  int32 AngularSector = INDEX_NONE;
  FVector2f WorldLocation = FVector2f::ZeroVector;
  bool bReachable = false;
  bool bClearanceValid = false;
};

struct FCrowdDemoHoldingAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 WaitEpoch = 0;
  int32 PositionId = INDEX_NONE;
  FVector2f AssignedPosition = FVector2f::ZeroVector;
  ECrowdDemoPositionRole PositionRole = ECrowdDemoPositionRole::Reserve;
  int32 PositionIngressCost = MAX_int32;
  int32 ExistingHoldingId = INDEX_NONE;
  int32 ExistingTargetRevision = INDEX_NONE;
  ECrowdDemoPursuitSteeringState ExistingState = ECrowdDemoPursuitSteeringState::Pursuit;
  bool bPositionValid = false;
  bool bExistingOwnerHardValid = false;
};

struct FCrowdDemoHoldingPositionCompatibility
{
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 QuantizedRouteCostCm = MAX_int32;
  int32 PositionIngressCost = MAX_int32;
  bool bFlowReachable = false;
  bool bTargetClear = false;
  bool bObstacleClear = false;
  bool bStableBlockerClear = false;
  bool bCompatible = false;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoHoldingAssignment
{
  int32 AgentId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  FVector2f HoldingLocation = FVector2f::ZeroVector;
  FVector2f AssignedPosition = FVector2f::ZeroVector;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  int32 IntegerCost = MAX_int32;
  uint32 CompatibilityHash = 0;
  bool bCompatibilityValid = false;
  bool bReused = false;
};

struct FCrowdDemoHoldingSummary
{
  int32 CandidateCount = 0;
  int32 CandidateOverlapCount = 0;
  int32 CandidateInvalidCount = 0;
  int32 AssignedCount = 0;
  int32 UnassignedCount = 0;
  int32 ReusedCount = 0;
  int32 ReacquireCount = 0;
  int32 CompatibilityCount = 0;
  int32 SelectedCompatibilityValidCount = 0;
  int32 SelectedCompatibilityInvalidCount = 0;
  int32 DuplicateCompatibilityKeyCount = 0;
  uint32 CandidateHash = 2166136261u;
  uint32 CompatibilityHash = 2166136261u;
  uint32 AssignmentHash = 2166136261u;
};

struct FCrowdDemoHoldingMatchingInput
{
  TArray<FCrowdDemoHoldingAgent> Agents;
  TArray<FCrowdDemoHoldingCandidate> Holdings;
  TArray<FCrowdDemoHoldingPositionCompatibility> Compatibility;
  int32 TargetRevision = INDEX_NONE;
};

struct FCrowdDemoHoldingMatchingResult
{
  TArray<FCrowdDemoHoldingAssignment> Assignments;
  int32 MaximumCardinality = 0;
  int32 ReusedOwnerCount = 0;
  int64 TotalRouteCost = 0;
  int32 UnmatchedAgentCount = 0;
  int32 HardLockedOwnerCount = 0;
  int32 SoftOwnerMovedCount = 0;
  int32 InvalidHardOwnerCount = 0;
  uint32 MatchingHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoHoldingHallEdge
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  bool bCompatibilityRecordPresent = false;
  bool bFlowClear = false;
  bool bTargetClear = false;
  bool bObstacleClear = false;
  bool bRevisionValid = false;
  TArray<int32> StableBlockerAgentIds;
  TArray<int32> ReserveBlockerAgentIds;
  bool bCompatible = false;
};

struct FCrowdDemoHoldingHallSummary
{
  int32 CurrentMatchingCount = 0;
  int32 NoStableOwnerMatchingCount = 0;
  int32 NoReserveOwnerMatchingCount = 0;
  int32 NoCommitOwnerMatchingCount = 0;
  int32 HallAgentCount = 0;
  int32 HallAvailableHoldingCount = 0;
  int32 HallDeficiency = 0;
  int32 MissingCompatibilityRecordCount = 0;
  int32 FlowRejectCount = 0;
  int32 TargetRejectCount = 0;
  int32 ObstacleRejectCount = 0;
  int32 RevisionRejectCount = 0;
  int32 StableOwnerRejectCount = 0;
  int32 ReserveOwnerRejectCount = 0;
  int32 FullHallDeficiency = 0;
  int32 OwnerReleaseStableMatchingCount = 0;
  int32 OwnerReleaseReserveMatchingCount = 0;
  int32 OwnerReleaseCommitMatchingCount = 0;
  int32 PhysicalStableBlockerRemovalMatchingCount = 0;
  int32 PhysicalReserveBlockerRemovalMatchingCount = 0;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
  bool bExact = false;
};

struct FCrowdDemoHoldingHallFixture
{
  int32 TargetRevision = INDEX_NONE;
  TArray<int32> AgentIds;
  TArray<int32> AvailableHoldingIds;
  TArray<FCrowdDemoHoldingHallEdge> Edges;
  FCrowdDemoHoldingHallSummary Summary;
};

struct FCrowdDemoHoldingPathBlockerFact
{
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 BlockerAgentId = INDEX_NONE;
  int32 BlockerPositionId = INDEX_NONE;
  ECrowdDemoPursuitPositionState BlockerState = ECrowdDemoPursuitPositionState::Pursuit;
  FVector2f SegmentStart = FVector2f::ZeroVector;
  FVector2f SegmentEnd = FVector2f::ZeroVector;
  FVector2f BlockerCenter = FVector2f::ZeroVector;
  float AgentRadiusCm = 0.0f;
  float BlockerRadiusCm = 0.0f;
  float SafetyGapCm = 0.0f;
  float RequiredClearanceCm = 0.0f;
  float ActualClosestDistanceCm = 0.0f;
  float ClearanceMarginCm = 0.0f;
  float ClosestPointT = 0.0f;
  bool bEndpointContact = false;
  bool bTargetRejected = false;
  bool bStableRejected = false;
  bool bReserveRejected = false;
  bool bFormalClassificationMismatch = false;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoHallGeometryFixture
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  FVector2f PositionLocation = FVector2f::ZeroVector;
  int32 HoldingCandidateCount = 0;
  int32 BestHoldingId = INDEX_NONE;
  float BestClearanceMarginCm = -MAX_flt;
  int32 BestBlockerAgentId = INDEX_NONE;
  int32 RejectedByStableCount = 0;
  int32 RejectedByTargetCount = 0;
  int32 RejectedByReserveCount = 0;
  int32 NonNegativeMarginHoldingCount = 0;
  int32 TargetOnlyRejectCount = 0;
  int32 StableOnlyRejectCount = 0;
  int32 MultiLabelRejectCount = 0;
  int32 SelfBlockerCount = 0;
  int32 BlockerUsesWitnessPositionCount = 0;
  int32 DuplicateBlockerCount = 0;
  int32 StaleBlockerCount = 0;
  int32 RadiusSemanticsErrorCount = 0;
  int32 EndpointContactCount = 0;
  int32 FormalClassificationMismatchCount = 0;
  FCrowdDemoHoldingPathBlockerFact BestFact;
  uint32 FixtureHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoJointPositioningAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 WaitEpoch = 0;
  int32 ExistingHoldingId = INDEX_NONE;
  int32 ExistingPositionId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  bool bExistingHardOwnerValid = false;
};

struct FCrowdDemoJointAgentHoldingEdge
{
  int32 AgentId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 QuantizedCurrentToHoldingCostCm = MAX_int32;
  bool bLocallyReachable = false;
};

struct FCrowdDemoJointPositioningAssignment
{
  int32 AgentId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  bool bHardLocked = false;
  bool bReusedHolding = false;
  bool bReusedPosition = false;
};

struct FCrowdDemoJointPositioningResult
{
  TArray<FCrowdDemoJointPositioningAssignment> Assignments;
  int32 MaximumCardinality = 0;
  int32 HardLockedCount = 0;
  int32 ReusedCombinationCount = 0;
  int32 UnmatchedAgentCount = 0;
  int32 DuplicateHoldingCount = 0;
  int32 DuplicatePositionCount = 0;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoJointCommitResidualDecision
{
  int32 AgentId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 RemainingAgentCountAfterGrant = 0;
  int32 ResidualMatchingAfterGrant = 0;
  bool bGrantFeasible = false;
};

struct FCrowdDemoJointCommitResidualResult
{
  TArray<FCrowdDemoJointCommitResidualDecision> Decisions;
  int32 CandidateCount = 0;
  int32 FeasibleCount = 0;
  int32 InfeasibleCount = 0;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoResidualPositioningAgent
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  bool bHasHolding = false;
};

struct FCrowdDemoResidualPositioningEdge
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  bool bCurrentToHoldingReachable = false;
  bool bFlowClear = false;
  bool bTargetClear = false;
  bool bObstacleClear = false;
  bool bRevisionValid = false;
  TArray<int32> StableBlockerAgentIds;
  TArray<int32> ReserveBlockerAgentIds;
};

struct FCrowdDemoResidualPositioningSummary
{
  int32 UnfinishedAgentCount = 0;
  int32 RemainingPositionCount = 0;
  int32 CompatibleEdgeCount = 0;
  int32 MaximumMatchingCount = 0;
  int32 AgentWithoutHoldingCount = 0;
  int32 AgentWithoutPositionEdgeCount = 0;
  int32 AgentWithoutCommitRouteCount = 0;
  int32 StableBlockerEdgeRejectCount = 0;
  int32 ReserveBlockerEdgeRejectCount = 0;
  int32 TargetRejectCount = 0;
  int32 ObstacleRejectCount = 0;
  int32 FlowRejectCount = 0;
  int32 RevisionRejectCount = 0;
  int32 CurrentMatching = 0;
  int32 NoStableMatching = 0;
  int32 NoReserveMatching = 0;
  int32 BestSingleBlockerRemovalGain = 0;
  int32 BlockerCriticalCount = 0;
  int32 TargetLimitedCount = 0;
  int32 GeometryLimitedCount = 0;
  uint32 ResidualCapacityHash = 2166136261u;
};

struct FCrowdDemoCommitRequest
{
  int32 AgentId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 WaitEpoch = 0;
  int32 PositionFillCost = MAX_int32;
  int32 QuantizedCommitCostCm = MAX_int32;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f HoldingLocation = FVector2f::ZeroVector;
  FVector2f AssignedPosition = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  bool bPositionValid = false;
  bool bCompatibilityFound = false;
  bool bCompatibilityValid = false;
  bool bAlreadyCommit = false;
};

enum class ECrowdDemoCommitDecision : uint8
{
  None,
  Granted,
  Held,
  Reacquire
};

enum class ECrowdDemoCommitRejectReason : uint32
{
  None = 0,
  InvalidPosition = 1u << 0,
  TargetRevision = 1u << 1,
  CompatibilityMissing = 1u << 2,
  CompatibilityRejected = 1u << 3,
  HoldingDistance = 1u << 4,
  HoldingSpeed = 1u << 5,
  TargetExclusion = 1u << 6,
  Flow = 1u << 7,
  Obstacle = 1u << 8,
  StableBlocker = 1u << 9,
  ReserveBlocker = 1u << 10,
  ActiveCommitConflict = 1u << 11,
  SelectedCommitConflict = 1u << 12,
  JointResidualCapacity = 1u << 13
};

struct FCrowdDemoCommitDecisionRecord
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoCommitDecision Decision = ECrowdDemoCommitDecision::None;
  uint32 RejectReasonMask = 0;
  uint32 YieldableConflictMask = 0;
};

struct FCrowdDemoSf4UnfinishedAgentDiagnosticInput
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Destination = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f OrcaVelocity = FVector2f::ZeroVector;
  FVector2f FinalVelocity = FVector2f::ZeroVector;
  TArray<int32> OrcaConstraintSourceCounts;
  uint32 CommitRejectReasonMask = 0;
  int32 NoProgressSteps = 0;
};

struct FCrowdDemoSf4UnfinishedAgentDiagnostic
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  int32 DistanceCm = 0;
  FIntPoint PreferredVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint OrcaVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint FinalVelocityCmps = FIntPoint::ZeroValue;
  TArray<int32> OrcaConstraintSourceCounts;
  uint32 CommitRejectReasonMask = 0;
  int32 NoProgressSteps = 0;
};

struct FCrowdDemoSf4UnfinishedBoundaryFixture
{
  TArray<FCrowdDemoSf4UnfinishedAgentDiagnostic> Agents;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoSf4PhysicalUnsatisfiedAgentInput
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  int32 PositionId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 InvalidReason = 0;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Destination = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f OrcaVelocity = FVector2f::ZeroVector;
  FVector2f ObstacleVelocity = FVector2f::ZeroVector;
  FVector2f PbdVelocity = FVector2f::ZeroVector;
  FVector2f ReprojectVelocity = FVector2f::ZeroVector;
  FVector2f FinalVelocity = FVector2f::ZeroVector;
  uint32 CommitRejectReasonMask = 0;
  uint32 CommitYieldableConflictMask = 0;
  bool bPhysicallySatisfied = false;
};

struct FCrowdDemoSf4PhysicalUnsatisfiedAgentDiagnostic
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoPursuitSteeringState State = ECrowdDemoPursuitSteeringState::Pursuit;
  int32 PositionId = INDEX_NONE;
  int32 HoldingId = INDEX_NONE;
  int32 InvalidReason = 0;
  int32 DistanceCm = 0;
  FIntPoint PreferredVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint OrcaVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint ObstacleVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint PbdVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint ReprojectVelocityCmps = FIntPoint::ZeroValue;
  FIntPoint FinalVelocityCmps = FIntPoint::ZeroValue;
  uint32 CommitRejectReasonMask = 0;
  uint32 CommitYieldableConflictMask = 0;
};

struct FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture
{
  TArray<FCrowdDemoSf4PhysicalUnsatisfiedAgentDiagnostic> Agents;
  int32 TotalAgentCount = 0;
  int32 PhysicallySatisfiedCount = 0;
  uint32 StableHash = 2166136261u;
  bool bCountClosed = false;
  bool bValid = false;
};

struct FCrowdDemoCommitGateResult
{
  TArray<FCrowdDemoCommitDecisionRecord> Decisions;
  TArray<int32> GrantedAgentIds;
  int32 ActiveCommitCount = 0;
  int32 ReadyRequestCount = 0;
  int32 HeldCount = 0;
  int32 ReacquireCount = 0;
  int32 InvalidPositionCount = 0;
  int32 TargetRevisionMismatchCount = 0;
  int32 CompatibilityMissingCount = 0;
  int32 CompatibilityRejectedCount = 0;
  int32 HoldingDistanceNotReadyCount = 0;
  int32 HoldingSpeedNotReadyCount = 0;
  int32 ReadyConflictHeldCount = 0;
  int32 ReadyGrantedCount = 0;
  int32 ReadyTargetRejectCount = 0;
  int32 ReadyFlowRejectCount = 0;
  int32 ReadyObstacleRejectCount = 0;
  int32 ReadyStableBlockerRejectCount = 0;
  int32 ReadyReserveBlockerRejectCount = 0;
  int32 ReadyActiveCommitConflictCount = 0;
  int32 ReadySelectedConflictCount = 0;
  int32 YieldableStableConflictCount = 0;
  int32 YieldableReserveConflictCount = 0;
  int32 HardConflictHeldCount = 0;
  uint32 DecisionHash = 2166136261u;
};

struct FCrowdDemoPositionIngressAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f FinalVelocity = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f OrcaVelocity = FVector2f::ZeroVector;
  FVector2f ObstacleVelocity = FVector2f::ZeroVector;
  FVector2f PbdCorrection = FVector2f::ZeroVector;
  FVector2f ObstacleCorrection = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
  int32 PositionId = INDEX_NONE;
  FVector2f AssignedLocation = FVector2f::ZeroVector;
  ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
  ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
  ECrowdDemoFrontApproachPhase ApproachPhase = ECrowdDemoFrontApproachPhase::None;
  TArray<int32> OrcaConstraintOtherAgentIds;
  int32 PreviousLowSpeedSteps = 0;
  float RadialErrorCm = 0.0f;
  int32 ComposeBoundarySwitchCount = 0;
  bool bRadialErrorImproved = false;
  bool bQuantizedProgressStall = false;
};

struct FCrowdDemoPositionIngressBlocker
{
  int32 AgentId = INDEX_NONE;
  int32 PositionId = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
  FVector2f Location = FVector2f::ZeroVector;
  float RadiusCm = 42.0f;
};

struct FCrowdDemoPositionIngressEvaluation
{
  int32 AgentId = INDEX_NONE;
  int32 AssignedPositionId = INDEX_NONE;
  float DirectPathLengthCm = 0.0f;
  float AssignedSectorDelta = 0.0f;
  float AssignedRadialDeltaCm = 0.0f;
  bool bTargetBlocked = false;
  bool bStableBlocked = false;
  bool bReserveBlocked = false;
  bool bCommitBlocked = false;
  bool bHasUnblockedAlternativeFront = false;
  bool bHasSameSideOccupiedFront = false;
  bool bIngressOrderInversion = false;
  bool bPbdPushesAway = false;
  bool bObstaclePushesAway = false;
  int32 OrcaConstraintsFromStable = 0;
  int32 OrcaConstraintsFromReserve = 0;
  int32 OrcaConstraintsFromCommit = 0;
  int32 OrcaConstraintsFromOther = 0;
  int32 LowSpeedSteps = 0;
  float PreferredSpeedCmps = 0.0f;
  float OrcaSpeedCmps = 0.0f;
  float ObstacleSpeedCmps = 0.0f;
  float FinalSpeedCmps = 0.0f;
  TArray<FCrowdDemoPositionIngressBlocker> DirectBlockers;
};

struct FCrowdDemoPositionIngressFixture
{
  bool bValid = false;
  FCrowdDemoPursuitTargetFact Target;
  FCrowdDemoPositionIngressAgent Agent;
  FCrowdDemoPositionCandidate AssignedCandidate;
  TArray<FCrowdDemoPositionIngressBlocker> Blockers;
  uint32 StableHash = 0;
  int32 ConstraintCount = 0;
};

struct FCrowdDemoPositionIngressSummary
{
  int32 SlotCommitCount = 0;
  int32 SlotCommitErrorOver300Count = 0;
  int32 DirectPathTargetBlockedCount = 0;
  int32 DirectPathStableBlockedCount = 0;
  int32 DirectPathReserveBlockedCount = 0;
  int32 DirectPathCommitBlockedCount = 0;
  int32 StableBlockerPairCount = 0;
  int32 ReserveBlockerPairCount = 0;
  int32 CommitBlockerPairCount = 0;
  float AssignedSectorDeltaP50 = 0.0f;
  float AssignedSectorDeltaP95 = 0.0f;
  float AssignedSectorDeltaMax = 0.0f;
  float AssignedRadialDeltaP50 = 0.0f;
  float AssignedRadialDeltaP95 = 0.0f;
  float AssignedRadialDeltaMax = 0.0f;
  int32 UnblockedAlternativeFrontCount = 0;
  int32 SameSideAlternativeFrontCount = 0;
  int32 NoAlternativeFrontCount = 0;
  int32 OrcaConstraintsFromStableCount = 0;
  int32 OrcaConstraintsFromReserveCount = 0;
  int32 OrcaConstraintsFromCommitCount = 0;
  int32 OrcaConstraintsFromOtherCount = 0;
  float SlotCommitPreferredSpeedP95 = 0.0f;
  float SlotCommitOrcaSpeedP95 = 0.0f;
  float SlotCommitObstacleSpeedP95 = 0.0f;
  float SlotCommitFinalSpeedP95 = 0.0f;
  int32 SlotCommitLowSpeedStepsMax = 0;
  int32 TargetExclusionCrossingCount = 0;
  int32 IngressOrderInversionCount = 0;
  int32 PbdPushAwayCount = 0;
  int32 ObstaclePushAwayCount = 0;
  uint32 MinimumFixtureHash = 0;
  int32 MinimumFixtureConstraintCount = 0;
  uint32 EvaluationHash = 0;
  int32 FrontAssignedWaitingCount = 0;
  int32 RadialStageCount = 0;
  int32 AngularAlignCount = 0;
  int32 RadialCommitCount = 0;
  int32 GateInvalidCount = 0;
  int32 RadialCommitBlockedCount = 0;
  uint32 RouteHash = 2166136261u;
  float RadialPreferredSpeedP95 = 0.0f;
  float RadialOrcaSpeedP95 = 0.0f;
  float RadialFinalSpeedP95 = 0.0f;
  float RadialOrcaForwardSpeedP50 = 0.0f;
  float RadialOrcaForwardSpeedMin = 0.0f;
  float RadialFinalForwardSpeedP50 = 0.0f;
  float RadialFinalForwardSpeedMin = 0.0f;
  float RadialOrcaConstraintP95 = 0.0f;
  int32 RadialConstraintFromActiveCount = 0;
  int32 RadialConstraintFromWaitingCount = 0;
  int32 RadialConstraintFromReserveCommitCount = 0;
  int32 RadialConstraintFromStableCount = 0;
  int32 RadialConstraintFromOtherCount = 0;
  float RadialErrorP50 = 0.0f;
  float RadialErrorP95 = 0.0f;
  float RadialErrorMax = 0.0f;
  int32 RadialErrorImprovedCount = 0;
  int32 RadialQuantizedProgressStallCount = 0;
  int32 ComposeBoundarySwitchCount = 0;
};

class MASSAICROWDDEMO_API FCrowdDemoPursuitPositioningKernel
{
public:
  static void BuildCandidates(
    const FCrowdDemoPursuitTargetFact& Target,
    float AgentRadiusCm,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowField& FlowField,
    TArray<FCrowdDemoPositionCandidate>& OutCandidates,
    FCrowdDemoPositioningSummary& OutSummary);

  static void Assign(
    TConstArrayView<FCrowdDemoPositioningAgent> Agents,
    TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TArray<FCrowdDemoPositionAssignment>& OutAssignments,
    FCrowdDemoPositioningSummary& OutSummary);

  static bool SegmentIntersectsSafetyCircle(
    FVector2f SegmentStart,
    FVector2f SegmentEnd,
    FVector2f CircleCenter,
    float SafetyRadiusCm);

  static void EvaluateIngress(
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
    TConstArrayView<FCrowdDemoPositionIngressAgent> Agents,
    TArray<FCrowdDemoPositionIngressEvaluation>& OutEvaluations,
    FCrowdDemoPositionIngressSummary& OutSummary,
    FCrowdDemoPositionIngressFixture& OutMinimumFixture);

  static void ScheduleFrontAdmission(
    FVector2f EntryAxis,
    int32 FixedStepIndex,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoPositionCandidate> Candidates,
    TConstArrayView<FCrowdDemoFrontAdmissionAgent> Agents,
    FCrowdDemoFrontAdmissionResult& OutResult);

  static bool ShouldComposePositionGuidance(
    ECrowdDemoPursuitPositionState State,
    ECrowdDemoFrontApproachPhase ApproachPhase,
    bool bPortalOwns,
    float DistanceToAssignedPositionCm,
    float ComposeRangeCm);

  static void ScheduleFrontPhaseReservations(
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoFrontPhaseReservationRequest> Requests,
    FCrowdDemoFrontPhaseReservationResult& OutResult);

  static void BuildFrontPhaseReservationPoints(
    const FCrowdDemoFrontApproachRoute& Route,
    ECrowdDemoFrontApproachPhase Phase,
    FVector2f CurrentLocation,
    TArray<FVector2f>& OutPoints);

  static FVector2f BuildFrontPhaseDesiredVelocity(
    const FCrowdDemoFrontApproachRoute& Route,
    ECrowdDemoFrontApproachPhase CommittedPhase,
    FVector2f CurrentLocation,
    float MaxSpeedCmps);

  static bool ApplyFrontPhaseReservationDecision(
    const FCrowdDemoFrontPhaseReservationDecisionRecord& Decision,
    int32 Revision,
    FCrowdDemoFrontPhaseReservationState& InOutState);

  static bool FrontReservationPathsConflict(
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FVector2f> A,
    float ARadiusCm,
    TConstArrayView<FVector2f> B,
    float BRadiusCm);

  static void AnalyzeFrontReservationWaitGraph(
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoFrontReservationWaitAgent> Agents,
    TConstArrayView<FCrowdDemoFrontPhaseReservationBlockPair> BlockingPairs,
    TArray<FCrowdDemoFrontReservationWaitEdge>& OutEdges,
    FCrowdDemoFrontReservationWaitGraphSummary& OutSummary,
    FCrowdDemoFrontReservationWaitGraphFixture& OutFixture);

  static FCrowdDemoFrontApproachRoute BuildFrontApproachRoute(
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowField& FlowField,
    int32 AgentId,
    float AgentRadiusCm,
    FVector2f CurrentLocation,
    const FCrowdDemoPositionCandidate& Candidate,
    TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers,
    float MaxSpeedCmps,
    int32 RouteRevision);

  static void BuildHoldingCandidates(
    const FCrowdDemoPursuitTargetFact& Target,
    float AgentRadiusCm,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowField& FlowField,
    TConstArrayView<FCrowdDemoPositionCandidate> PositionCandidates,
    TArray<FCrowdDemoHoldingCandidate>& OutCandidates,
    FCrowdDemoHoldingSummary& OutSummary);

  static FCrowdDemoHoldingPositionCompatibility EvaluateHoldingPositionCompatibility(
    const FCrowdDemoPursuitTargetFact& Target,
    float AgentRadiusCm,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowField& FlowField,
    const FCrowdDemoHoldingCandidate& Holding,
    const FCrowdDemoPositionCandidate& Position,
    TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers);

  static void AssignHoldingPositions(
    const FCrowdDemoPursuitTargetFact& Target,
    TConstArrayView<FCrowdDemoHoldingAgent> Agents,
    TConstArrayView<FCrowdDemoHoldingCandidate> HoldingCandidates,
    TConstArrayView<FCrowdDemoHoldingPositionCompatibility> Compatibility,
    TArray<FCrowdDemoHoldingAssignment>& OutAssignments,
    FCrowdDemoHoldingSummary& OutSummary);

  static void MatchHoldingPositions(
    const FCrowdDemoHoldingMatchingInput& Input,
    FCrowdDemoHoldingMatchingResult& OutResult);

  static void AnalyzeHoldingHallDeficiency(
    const FCrowdDemoHoldingMatchingInput& MatchingInput,
    TConstArrayView<FCrowdDemoHoldingHallEdge> DiagnosticEdges,
    FCrowdDemoHoldingHallFixture& OutFixture);

  static void AnalyzeHoldingHallGeometry(
    int32 AgentId,
    const FCrowdDemoPositionCandidate& Position,
    float AgentRadiusCm,
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
    TConstArrayView<FCrowdDemoPositionIngressBlocker> Blockers,
    TConstArrayView<FCrowdDemoHoldingPositionCompatibility> FormalCompatibility,
    FCrowdDemoHallGeometryFixture& OutFixture);

  static void PlanJointHoldingPositions(
    int32 TargetRevision,
    TConstArrayView<FCrowdDemoJointPositioningAgent> Agents,
    TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
    TConstArrayView<FCrowdDemoPositionCandidate> Positions,
    TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
    TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
    FCrowdDemoJointPositioningResult& OutResult);

  static void EvaluateJointCommitResidualProtection(
    int32 TargetRevision,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoJointPositioningAgent> Agents,
    TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
    TConstArrayView<FCrowdDemoPositionCandidate> Positions,
    TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
    TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
    const FCrowdDemoJointPositioningResult& JointPlan,
    FCrowdDemoJointCommitResidualResult& OutResult);

  static void ApplyJointResidualCommitGate(
    int32 TargetRevision,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    TConstArrayView<FCrowdDemoJointPositioningAgent> Agents,
    TConstArrayView<FCrowdDemoHoldingCandidate> Holdings,
    TConstArrayView<FCrowdDemoPositionCandidate> Positions,
    TConstArrayView<FCrowdDemoJointAgentHoldingEdge> AgentHoldingEdges,
    TConstArrayView<FCrowdDemoHoldingPositionCompatibility> HoldingPositionEdges,
    const FCrowdDemoJointPositioningResult& JointPlan,
    FCrowdDemoCommitGateResult& InOutGate,
    FCrowdDemoJointCommitResidualResult& OutResidual);

  static void AnalyzeResidualPositioning(
    TConstArrayView<FCrowdDemoResidualPositioningAgent> Agents,
    TConstArrayView<int32> RemainingPositionIds,
    TConstArrayView<FCrowdDemoResidualPositioningEdge> Edges,
    FCrowdDemoResidualPositioningSummary& OutSummary);

  static bool PositioningSegmentConflictsWithBlocker(
    FVector2f Start,
    FVector2f End,
    float AgentRadiusCm,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoPositionIngressBlocker& Blocker);

  static void ScheduleCommitGate(
    const FCrowdDemoPursuitTargetFact& Target,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowField& FlowField,
    TConstArrayView<FCrowdDemoCommitRequest> Requests,
    TConstArrayView<FCrowdDemoPositionIngressBlocker> OccupiedBlockers,
    FCrowdDemoCommitGateResult& OutResult);

  static void BuildUnfinishedBoundaryFixture(
    TConstArrayView<FCrowdDemoSf4UnfinishedAgentDiagnosticInput> Inputs,
    FCrowdDemoSf4UnfinishedBoundaryFixture& OutFixture);
  static void BuildPhysicalUnsatisfiedBoundaryFixture(
    TConstArrayView<FCrowdDemoSf4PhysicalUnsatisfiedAgentInput> Inputs,
    FCrowdDemoSf4PhysicalUnsatisfiedBoundaryFixture& OutFixture);

  static FVector2f BuildSteeringFirstPreferredVelocity(
    ECrowdDemoPursuitSteeringState State,
    FVector2f CurrentLocation,
    FVector2f FlowPreferredVelocity,
    FVector2f HoldingLocation,
    FVector2f AssignedPosition,
    float MaxSpeedCmps,
    const FCrowdDemoPursuitPositioningSettings& Settings);

  static bool ShouldEnterHolding(
    FVector2f CurrentLocation,
    FVector2f HoldingLocation,
    ECrowdDemoFlowLocationStatus FlowStatus,
    const FCrowdDemoPursuitPositioningSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig);

  static int32 ComputePositionFillCost(
    const FCrowdDemoPursuitTargetFact& Target,
    FVector2f EntryAxis,
    const FCrowdDemoPositionCandidate& Position);
};
