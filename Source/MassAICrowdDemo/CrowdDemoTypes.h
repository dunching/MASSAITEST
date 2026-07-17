#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "CrowdDemoTypes.generated.h"

UENUM()
enum class ECrowdDemoAnimState : uint8
{
  Idle = 0,
  Move = 1,
  Attack = 2,
  HitReact = 3,
  Death = 4
};

UENUM()
enum class ECrowdDemoBusinessState : uint8
{
  Idle = 0,
  Moving = 1,
  Attacking = 2,
  HitReact = 3,
  Dead = 4
};

UENUM()
enum class ECrowdDemoAttackPhase : uint8
{
  None = 0,
  AcquireTarget = 1,
  Windup = 2,
  Fire = 3,
  Recovery = 4,
  Cooldown = 5
};

UENUM()
enum class ECrowdDemoReactiveMotionMode : uint8
{
  None = 0,
  Knockback = 1,
  KnockUp = 2,
  LandingRecovery = 3
};

UENUM()
enum class ECrowdDemoVisualState : uint8
{
  Idle = 0,
  Move = 1,
  Attack = 2,
  HitReact = 3,
  Death = 4
};

UENUM()
enum class ECrowdDemoRoundFrameKind : uint8
{
  Correction = 0,
  RoundResultCheckpoint = 1
};

UENUM()
enum class ECrowdDemoLifecycleState : uint8
{
  Spawning = 0,
  Alive = 1,
  Dying = 2,
  Dead = 3
};

UENUM(BlueprintType)
enum class ECrowdDemoScenario : uint8
{
  SimRoundObstacle = 0,
  SimRoundSoftPressure = 1,
  SimRoundCrowdTraffic = 2,
  SimRoundPursuitPositioning = 3
};

UENUM(BlueprintType)
enum class ECrowdDemoRoundStartPolicy : uint8
{
  ResetToStableInitialState = 0,
  ContinueFromCheckpoint = 1
};

UENUM(BlueprintType)
enum class ECrowdDemoSoftPressureTestCase : uint8
{
  CorridorRoute = 0,
  PursuitAndSettle = 1,
  PursuitAndSettleMoving = 2,
  OpenSpawnRelaxation = 3,
  OpenCohortMovement = 4,
  BidirectionalSwap = 5,
  ValidCorridorTransit = 6,
  HeterogeneousTransit = 7,
  HeterogeneousTargetStatic = 8,
  HeterogeneousTargetMoving = 9,
  MultiStateVatHitResponse = 10,
  RangedProjectileCombat = 11
};

UENUM()
enum class ECrowdDemoProjectileVisualEventKind : uint8
{
  Spawn = 0,
  Impact = 1,
  Expire = 2
};

USTRUCT(BlueprintType)
struct FCrowdDemoProjectileVisualEvent
{
  GENERATED_BODY()

  UPROPERTY() ECrowdDemoProjectileVisualEventKind Kind = ECrowdDemoProjectileVisualEventKind::Spawn;
  UPROPERTY() uint64 ProjectileId = 0;
  UPROPERTY() int32 FixedStepIndex = INDEX_NONE;
  UPROPERTY() float ServerTimeSeconds = 0.0f;
  UPROPERTY() FVector_NetQuantize10 Position = FVector::ZeroVector;
  UPROPERTY() FVector_NetQuantize10 Velocity = FVector::ZeroVector;
  UPROPERTY() float RadiusCm = 12.0f;
};

inline bool IsCrowdDemoRoundSimScenario(const ECrowdDemoScenario Scenario)
{
  return Scenario == ECrowdDemoScenario::SimRoundObstacle
    || Scenario == ECrowdDemoScenario::SimRoundSoftPressure
    || Scenario == ECrowdDemoScenario::SimRoundCrowdTraffic
    || Scenario == ECrowdDemoScenario::SimRoundPursuitPositioning;
}

USTRUCT(BlueprintType)
struct FCrowdDemoEntityState
{
  GENERATED_BODY()

  UPROPERTY()
  int32 Id = INDEX_NONE;

  UPROPERTY()
  int32 LifecycleSerial = 0;

  UPROPERTY()
  FVector_NetQuantize10 Location = FVector::ZeroVector;

  UPROPERTY()
  FVector_NetQuantize10 Velocity = FVector::ZeroVector;

  UPROPERTY()
  int16 YawCentidegrees = 0;

  UPROPERTY()
  ECrowdDemoAnimState AnimState = ECrowdDemoAnimState::Idle;

  UPROPERTY()
  uint8 VatClipIndex = 0;

  UPROPERTY()
  uint8 VatPhaseByte = 0;

  UPROPERTY()
  uint8 VatPlayRateByte = 128;

  UPROPERTY()
  float ServerTimeSeconds = 0.0f;

  UPROPERTY()
  float LastReceiveWorldTimeSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetApproachNetState
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  uint8 State = 0;

  UPROPERTY()
  int32 TargetId = INDEX_NONE;

  UPROPERTY()
  int32 TargetRevision = INDEX_NONE;

  UPROPERTY()
  int32 SlotLayoutRevision = INDEX_NONE;

  UPROPERTY()
  int32 AssignedSlotId = INDEX_NONE;

  UPROPERTY()
  int32 RingEnterFixedStep = INDEX_NONE;

  UPROPERTY()
  int32 StateEnterFixedStep = 0;

  UPROPERTY()
  int32 StableSettleSteps = 0;

  UPROPERTY()
  int32 StateRevision = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoCombatNetState
{
  GENERATED_BODY()

  UPROPERTY() float Health = 100.0f;
  UPROPERTY() float MaxHealth = 100.0f;
  UPROPERTY() ECrowdDemoLifecycleState LifecycleState = ECrowdDemoLifecycleState::Alive;
  UPROPERTY() uint8 bAlive = 1;

  UPROPERTY() ECrowdDemoBusinessState BusinessState = ECrowdDemoBusinessState::Idle;
  UPROPERTY() int32 BusinessStateRevision = 0;
  UPROPERTY() int32 BusinessStateEnterFixedStep = 0;
  UPROPERTY() int32 TargetAgentId = INDEX_NONE;
  UPROPERTY() int32 TargetLifecycleSerial = 0;

  UPROPERTY() ECrowdDemoAttackPhase AttackPhase = ECrowdDemoAttackPhase::None;
  UPROPERTY() int32 AttackPhaseEnterFixedStep = 0;
  UPROPERTY() int32 CooldownEndFixedStep = 0;
  UPROPERTY() int32 LockedTargetAgentId = INDEX_NONE;
  UPROPERTY() int32 LockedTargetLifecycleSerial = 0;
  UPROPERTY() FVector_NetQuantize10 LockedTargetLocation = FVector::ZeroVector;
  UPROPERTY() int32 FireSequence = 0;
  UPROPERTY() uint8 bFireRequestIssued = 0;

  UPROPERTY() ECrowdDemoReactiveMotionMode ReactiveMode = ECrowdDemoReactiveMotionMode::None;
  UPROPERTY() FVector_NetQuantize10 HorizontalReactiveVelocity = FVector::ZeroVector;
  UPROPERTY() float VerticalReactiveVelocityCmps = 0.0f;
  UPROPERTY() int32 ReactiveStartFixedStep = INDEX_NONE;
  UPROPERTY() int32 ReactiveEndFixedStep = INDEX_NONE;
  UPROPERTY() int32 ReactiveRevision = 0;
  UPROPERTY() ECrowdDemoBusinessState RestoreBusinessState = ECrowdDemoBusinessState::Idle;
  UPROPERTY() int32 ApexCount = 0;
  UPROPERTY() int32 LandingCount = 0;

  UPROPERTY() int32 HitFlashRevision = 0;
  UPROPERTY() float HitFlashStartServerTimeSeconds = 0.0f;
  UPROPERTY() float HitFlashDurationSeconds = 0.0f;
  UPROPERTY() uint32 HitFlashProfileKey = 0;
  UPROPERTY() float HitFlashPeakIntensity = 0.0f;
  UPROPERTY() uint64 LastConsumedHitEventId = 0;

  UPROPERTY() ECrowdDemoVisualState VisualState = ECrowdDemoVisualState::Idle;
  UPROPERTY() int32 VisualRevision = 0;
  UPROPERTY() float VisualStateStartServerTimeSeconds = 0.0f;
  UPROPERTY() uint32 VisualPhaseSeed = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundAgentState
{
  GENERATED_BODY()

  UPROPERTY()
  int32 AgentId = INDEX_NONE;

  UPROPERTY()
  int32 LifecycleSerial = 0;

  UPROPERTY()
  FVector_NetQuantize10 Location = FVector::ZeroVector;

  UPROPERTY()
  FVector_NetQuantize10 Velocity = FVector::ZeroVector;

  UPROPERTY()
  float YawDegrees = 0.0f;

  UPROPERTY()
  float RadiusCm = 42.0f;

  UPROPERTY()
  FCrowdDemoTargetApproachNetState TargetApproach;

  UPROPERTY()
  FCrowdDemoCombatNetState Combat;

};

USTRUCT(BlueprintType)
struct FCrowdDemoCrowdAggregateState
{
  GENERATED_BODY()

  UPROPERTY()
  int32 AgentCount = 0;

  UPROPERTY()
  FVector_NetQuantize10 CrowdCenter = FVector::ZeroVector;

  UPROPERTY()
  float CrowdYawDegrees = 0.0f;

  UPROPERTY()
  FVector_NetQuantize10 CrowdVelocity = FVector::ZeroVector;

  UPROPERTY()
  float PlanPhase = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoCorrectionFrame
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  ECrowdDemoRoundFrameKind FrameKind = ECrowdDemoRoundFrameKind::Correction;

  UPROPERTY()
  int32 CorrectionRevision = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 RoundRevision = 0;

  UPROPERTY()
  int32 SourceCheckpointRevision = 0;

  UPROPERTY()
  float ServerTimeSeconds = 0.0f;

  UPROPERTY()
  int32 AgentCount = 0;

  UPROPERTY()
  FCrowdDemoCrowdAggregateState CrowdState;

  UPROPERTY()
  TArray<FCrowdDemoRoundAgentState> AgentStates;
};

USTRUCT(BlueprintType)
struct FCrowdDemoCorrectionFrameHeader
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  ECrowdDemoRoundFrameKind FrameKind = ECrowdDemoRoundFrameKind::Correction;

  UPROPERTY()
  int32 CorrectionRevision = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 RoundRevision = 0;

  UPROPERTY()
  int32 SourceCheckpointRevision = 0;

  UPROPERTY()
  float ServerTimeSeconds = 0.0f;

  UPROPERTY()
  int32 AgentCount = 0;

  UPROPERTY()
  int32 ChunkCount = 0;

  UPROPERTY()
  int32 ChunkSize = 0;

  UPROPERTY()
  FCrowdDemoCrowdAggregateState CrowdState;
};

USTRUCT()
struct FCrowdDemoCorrectionFrameChunk
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 StableKey = 0;

  UPROPERTY()
  int32 CorrectionRevision = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 RoundRevision = 0;

  UPROPERTY()
  int32 ChunkIndex = 0;

  UPROPERTY()
  int32 StartAgentIndex = 0;

  UPROPERTY()
  int32 AgentCountInChunk = 0;

  UPROPERTY()
  FCrowdDemoCorrectionFrameHeader Header;

  UPROPERTY()
  TArray<FCrowdDemoRoundAgentState> Agents;
};

struct FCrowdDemoPendingCorrectionAssembly
{
  FCrowdDemoCorrectionFrameHeader Header;
  TArray<uint8> ReceivedChunks;
  TArray<FCrowdDemoRoundAgentState> AgentBuffer;
  int32 ReceivedChunkCount = 0;
  int32 ReceivedAgentCount = 0;
  double FirstReceiveWorldSeconds = -1.0;
  double LastReceiveWorldSeconds = -1.0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoCorrectionFrameMetrics
{
  GENERATED_BODY()

  UPROPERTY()
  int32 CorrectionFrameRevision = 0;

  UPROPERTY()
  int32 CorrectionFrameAppliedCount = 0;

  UPROPERTY()
  int32 CorrectionFrameHeaderReceivedCount = 0;

  UPROPERTY()
  int32 CorrectionFrameChunkReceivedCount = 0;

  UPROPERTY()
  int32 LatestChunkRevisionSeen = 0;

  UPROPERTY()
  int32 CorrectionChunkReceivedCount = 0;

  UPROPERTY()
  int32 CorrectionUniqueChunkCount = 0;

  UPROPERTY()
  int32 CorrectionExpectedChunkCount = 0;

  UPROPERTY()
  int32 CorrectionAssemblyCompleteCount = 0;

  UPROPERTY()
  int32 CorrectionAssemblySupersededCount = 0;

  UPROPERTY()
  int32 CorrectionFrameCompleteCount = 0;

  UPROPERTY()
  int32 CorrectionFramePublishedCount = 0;

  UPROPERTY()
  int32 CorrectionFrameReceivedCount = 0;

  UPROPERTY()
  int32 CorrectionFrameDroppedOldCount = 0;

  UPROPERTY()
  int32 CorrectionFrameDroppedMismatchCount = 0;

  UPROPERTY()
  int32 CorrectionFrameFuturePendingCount = 0;

  UPROPERTY()
  int32 CorrectionFrameFutureDropCount = 0;

  UPROPERTY()
  int32 CorrectionFrameIncompleteDropCount = 0;

  UPROPERTY()
  int32 CorrectionFrameStaleDropCount = 0;

  UPROPERTY()
  int32 CorrectionFrameReplayToNowCount = 0;

  UPROPERTY()
  int32 CorrectionFrameLatestRevisionSeen = 0;

  UPROPERTY()
  int32 CorrectionFrameLatestRevisionApplied = 0;

  UPROPERTY()
  int32 CorrectionFrameRevisionGapCount = 0;

  UPROPERTY()
  int32 CorrectionFrameChunksPerFrame = 0;

  UPROPERTY()
  int32 CorrectionFrameChunkSize = 0;

  UPROPERTY()
  int32 CorrectionAgentCount = 0;

  UPROPERTY()
  float CorrectionPositionErrorCmP50 = -1.0f;

  UPROPERTY()
  float CorrectionPositionErrorCmP95 = -1.0f;

  UPROPERTY()
  float CorrectionPositionErrorCmMax = -1.0f;

  UPROPERTY()
  float CorrectionYawErrorDegP95 = -1.0f;

  UPROPERTY()
  float CorrectionVelocityErrorCmpsP95 = -1.0f;

  UPROPERTY()
  float CorrectionIntervalMsP95 = -1.0f;

  UPROPERTY()
  float CorrectionFrameAssemblyMsP95 = -1.0f;

  UPROPERTY()
  float CorrectionFrameAgeMsP50 = -1.0f;

  UPROPERTY()
  float CorrectionFrameAgeMsP95 = -1.0f;

  UPROPERTY()
  float CorrectionFrameReplayMsP95 = -1.0f;

  UPROPERTY()
  int32 CorrectionErrorAgentIdMax = INDEX_NONE;

  UPROPERTY()
  int32 YawErrorAgentIdMax = INDEX_NONE;

  UPROPERTY()
  FVector_NetQuantize10 CorrectionErrorVectorMean = FVector::ZeroVector;

  UPROPERTY()
  float CorrectionErrorVectorStdDev = -1.0f;

  UPROPERTY()
  float CohortCenterErrorCm = -1.0f;

  UPROPERTY()
  float CohortYawErrorDeg = -1.0f;

  UPROPERTY()
  float ResidualErrorAfterCenterAlignP95 = -1.0f;

  UPROPERTY()
  float ResidualErrorAfterRigidAlignP95 = -1.0f;

  UPROPERTY()
  float RoundTimeDeltaMs = -1.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoSharedFlowObstacleSpec
{
  GENERATED_BODY()

  UPROPERTY()
  int32 ObstacleId = 0;

  UPROPERTY()
  FVector_NetQuantize10 Center = FVector::ZeroVector;

  UPROPERTY()
  FVector_NetQuantize10 Extent = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FCrowdDemoSharedFlowFieldConfig
{
  GENERATED_BODY()

  UPROPERTY()
  int32 Revision = 0;

  UPROPERTY()
  FVector_NetQuantize10 BoundsMin = FVector(-3000.0f, -3400.0f, 0.0f);

  UPROPERTY()
  FVector_NetQuantize10 BoundsMax = FVector(3000.0f, 2200.0f, 0.0f);

  UPROPERTY()
  float CellSizeCm = 100.0f;

  UPROPERTY()
  float AgentInflateCm = 48.0f;

  UPROPERTY()
  int32 ConnectivityContractVersion = 0;

  UPROPERTY()
  FVector_NetQuantize10 GoalLocation = FVector(2200.0f, 1600.0f, 60.0f);

  UPROPERTY()
  TArray<FCrowdDemoSharedFlowObstacleSpec> ObstacleSpecs;
};

USTRUCT(BlueprintType)
struct FCrowdDemoParticleProfile
{
  GENERATED_BODY()

  UPROPERTY() float PhysicalRadiusCm = 42.0f;
  UPROPERTY() float HardSafetyGapCm = 10.0f;
  UPROPERTY() float SoftMarginCm = 17.0f;
  UPROPERTY() float Mobility = 1.0f;

  float GetNavigationHardClearanceCm() const
  {
    return PhysicalRadiusCm + HardSafetyGapCm;
  }

  float GetWallSoftDistanceCm() const
  {
    return GetNavigationHardClearanceCm() + SoftMarginCm;
  }
};

USTRUCT(BlueprintType)
struct FCrowdDemoTrafficCohortRule
{
  GENERATED_BODY()

  UPROPERTY() int32 CohortId = 0;
  UPROPERTY() int32 FirstFormationIndex = 0;
  UPROPERTY() int32 AgentCount = 0;
  UPROPERTY() FVector_NetQuantize10 SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f);
  UPROPERTY() int32 FormationColumns = 0;
  UPROPERTY() FCrowdDemoSharedFlowFieldConfig FlowFieldConfig;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTrafficSettings
{
  GENERATED_BODY()

  UPROPERTY() float CellSizeCm = 100.0f;
  UPROPERTY() int32 PortalMaxWidthCells = 8;
  UPROPERTY() int32 PortalMergeDistanceCells = 3;
  UPROPERTY() int32 PortalClearanceWindowCells = 3;
  UPROPERTY() int32 PortalApproachDepthCells = 3;
  UPROPERTY() int32 PortalExitDepthCells = 2;
  UPROPERTY() int32 ReservationTimeoutSteps = 60;
  UPROPERTY() int32 TransitTimeoutSteps = 120;
  UPROPERTY() int32 WaitEpochSteps = 30;
  UPROPERTY() int32 MinGreenSteps = 30;
  UPROPERTY() int32 MaxGreenSteps = 90;
  UPROPERTY() int32 ClearanceSteps = 15;
  UPROPERTY() int32 DensityComfortCount = 2;
  UPROPERTY() int32 DensitySaturationCount = 6;
  UPROPERTY() float DensityMinimumSpeedScale = 0.35f;
  UPROPERTY() int32 MaxBandCount = 3;
  UPROPERTY() float BandSpacingCm = 70.0f;
  UPROPERTY() float BandLateralSpeedCmps = 180.0f;
  UPROPERTY() float HoldingGapCm = 10.0f;
  UPROPERTY() float HoldingTargetGainPerSecond = 2.0f;
  UPROPERTY() float HoldingStopToleranceCm = 10.0f;
  UPROPERTY() float HoldingSlowdownDistanceCm = 100.0f;
  UPROPERTY() float BandLateralGainPerSecond = 2.0f;
  UPROPERTY() float PortalClearingMinimumSpeedScale = 0.5f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoOrcaSettings
{
  GENERATED_BODY()

  UPROPERTY() float NeighborDistanceCm = 600.0f;
  UPROPERTY() int32 MaxNeighbors = 24;
  UPROPERTY() float TimeHorizonSeconds = 1.25f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float VelocityQuantumCmps = 1.0f;
  UPROPERTY() float ConstraintEpsilonCmps = 0.1f;
  UPROPERTY() float DistanceBucketCm = 10.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTrafficMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint32 TrafficFieldHash = 0;
  UPROPERTY() uint32 PortalDecisionHash = 0;
  UPROPERTY() uint32 OrcaVelocityHash = 0;
  UPROPERTY() uint32 AgentStateHash = 0;
  UPROPERTY() int32 TrafficPortalCount = 0;
  UPROPERTY() int32 RawPortalCandidateCount = 0;
  UPROPERTY() int32 ExtractedPortalCount = 0;
  UPROPERTY() float TrafficPortalQueueCountP50 = 0.0f;
  UPROPERTY() float TrafficPortalQueueCountP95 = 0.0f;
  UPROPERTY() int32 TrafficPortalQueueCountMax = 0;
  UPROPERTY() float TrafficPortalOccupiedCountP95 = 0.0f;
  UPROPERTY() int32 TrafficPortalOccupiedCountMax = 0;
  UPROPERTY() int32 TrafficAdmissionGrantedCount = 0;
  UPROPERTY() int32 TrafficAdmissionDeniedCount = 0;
  UPROPERTY() int32 PortalBindCount = 0;
  UPROPERTY() int32 PortalRebindCount = 0;
  UPROPERTY() int32 PortalReleaseCount = 0;
  UPROPERTY() int32 InvalidSideCandidateCount = 0;
  UPROPERTY() int32 WrongSpanCandidateCount = 0;
  UPROPERTY() int32 ReservedToInsideCount = 0;
  UPROPERTY() int32 InsideToExitedCount = 0;
  UPROPERTY() int32 PortalZeroThroughputStepCount = 0;
  UPROPERTY() float TrafficAdmissionWaitMsP95 = 0.0f;
  UPROPERTY() float TrafficAdmissionWaitMsMax = 0.0f;
  UPROPERTY() float TrafficPortalThroughputP50 = 0.0f;
  UPROPERTY() float TrafficPortalThroughputP95 = 0.0f;
  UPROPERTY() float TrafficZeroThroughputWhileQueuedMsMax = 0.0f;
  UPROPERTY() int32 TrafficBandAssignmentCount = 0;
  UPROPERTY() int32 TrafficBandReassignmentCount = 0;
  UPROPERTY() int32 HoldingTargetCount = 0;
  UPROPERTY() int32 HoldingTargetAllocationFailureCount = 0;
  UPROPERTY() int32 HoldingTargetOverlapCount = 0;
  UPROPERTY() float BandLateralErrorP50 = 0.0f;
  UPROPERTY() float BandLateralErrorP95 = 0.0f;
  UPROPERTY() float BandLateralErrorMax = 0.0f;
  UPROPERTY() int32 ReservedPositiveAxialVelocityCount = 0;
  UPROPERTY() int32 ReservedZeroVelocityCount = 0;
  UPROPERTY() int32 TrafficDirectionEpochChangeCount = 0;
  UPROPERTY() float TrafficDensityAgentCountP95 = 0.0f;
  UPROPERTY() int32 TrafficDensityAgentCountMax = 0;
  UPROPERTY() int32 OrcaProcessedAgentCount = 0;
  UPROPERTY() float OrcaNeighborCountP50 = 0.0f;
  UPROPERTY() float OrcaNeighborCountP95 = 0.0f;
  UPROPERTY() int32 OrcaNeighborCountMax = 0;
  UPROPERTY() float OrcaConstraintCountP50 = 0.0f;
  UPROPERTY() float OrcaConstraintCountP95 = 0.0f;
  UPROPERTY() int32 OrcaConstraintCountMax = 0;
  UPROPERTY() int32 OrcaConstraintCutoffCircleCount = 0;
  UPROPERTY() int32 OrcaConstraintLeftLegCount = 0;
  UPROPERTY() int32 OrcaConstraintRightLegCount = 0;
  UPROPERTY() int32 OrcaConstraintPenetrationCount = 0;
  UPROPERTY() int32 OrcaNoConstraintCount = 0;
  UPROPERTY() int32 OrcaPreferredFeasibleCount = 0;
  UPROPERTY() int32 OrcaLpFeasibleCount = 0;
  UPROPERTY() int32 OrcaSingleConstraintOutsideSpeedCircleCount = 0;
  UPROPERTY() int32 OrcaMultiConstraintEmptyCount = 0;
  UPROPERTY() int32 OrcaQuantizationDestroyedCount = 0;
  UPROPERTY() int32 OrcaFallbackFlowFeasibleCount = 0;
  UPROPERTY() int32 OrcaFallbackPortalFeasibleCount = 0;
  UPROPERTY() int32 OrcaStopFeasibleCount = 0;
  UPROPERTY() int32 OrcaStopViolationCount = 0;
  UPROPERTY() int32 OrcaFormalLpFeasibleCount = 0;
  UPROPERTY() int32 OrcaFormalLpQuantizedRecoveredCount = 0;
  UPROPERTY() int32 OrcaFormalLpQuantizedGeometryRecoveredCount = 0;
  UPROPERTY() int32 OrcaFormalLpMissedZeroRecoveredCount = 0;
  UPROPERTY() int32 OrcaFormalLpMissedOracleRecoveredCount = 0;
  UPROPERTY() int32 OrcaContinuousFeasibleQuantizedEmptyCount = 0;
  UPROPERTY() int32 OrcaTrueNoFeasibleWitnessCount = 0;
  UPROPERTY() int32 OrcaOracleInvocationCount = 0;
  UPROPERTY() int32 OrcaCalledAfterContinuousFailureCount = 0;
  UPROPERTY() int32 OrcaCalledAfterQuantizationFailureCount = 0;
  UPROPERTY() int32 OrcaQuantizedWitnessUsedCount = 0;
  UPROPERTY() int32 OrcaNeighborhood3x3RecoveredCount = 0;
  UPROPERTY() int32 OrcaOracleNoWitnessCount = 0;
  UPROPERTY() int32 OrcaTrueNoWitnessReachableFlowCount = 0;
  UPROPERTY() int32 OrcaTrueNoWitnessInvalidFlowCount = 0;
  UPROPERTY() int32 OrcaTrueNoWitnessGoalNearCount = 0;
  UPROPERTY() int32 OrcaTrueNoWitnessCorridorCount = 0;
  UPROPERTY() int32 OrcaReferenceSampleCount = 0;
  UPROPERTY() int32 OrcaReferenceCurrentExactCount = 0;
  UPROPERTY() int32 OrcaReferenceExactCount = 0;
  UPROPERTY() int32 OrcaCurrentMissReferenceHitCount = 0;
  UPROPERTY() int32 OrcaBothMissOracleHitCount = 0;
  UPROPERTY() int32 OrcaCurrentHitReferenceMissCount = 0;
  UPROPERTY() int32 OrcaAllExactMissCount = 0;
  UPROPERTY() int32 OrcaReferenceContinuousHitQuantizedMissCount = 0;
  UPROPERTY() int32 OrcaReferenceThreeByThreeRecoveredCount = 0;
  UPROPERTY() int32 OrcaReferenceOracleWitnessAvailableCount = 0;
  UPROPERTY() int32 OrcaReferenceBestEffortUsedCount = 0;
  UPROPERTY() uint32 OrcaReferenceMinimumFixtureHash = 0;
  UPROPERTY() int32 OrcaReferenceMinimumFixtureConstraintCount = 0;
  UPROPERTY() int32 OrcaParallelBranchCount = 0;
  UPROPERTY() int32 OrcaNearParallelBranchCount = 0;
  UPROPERTY() int32 OrcaRedundantParallelCount = 0;
  UPROPERTY() int32 OrcaStricterParallelCount = 0;
  UPROPERTY() int32 OrcaTrueParallelContradictionCount = 0;
  UPROPERTY() int32 OrcaNumericalToleranceAcceptanceCount = 0;
  UPROPERTY() float OrcaOracleRecoveryMsP95 = 0.0f;
  UPROPERTY() float OrcaSolverMsP50 = 0.0f;
  UPROPERTY() float OrcaSolverMsMax = 0.0f;
  UPROPERTY() TArray<int32> OrcaProcessedByAdmissionState;
  UPROPERTY() TArray<int32> OrcaFormalLpMissedByAdmissionState;
  UPROPERTY() TArray<int32> OrcaQuantizedEmptyByAdmissionState;
  UPROPERTY() TArray<int32> OrcaInfeasibleByAdmissionState;
  UPROPERTY() int32 OrcaAdjustedAgentCount = 0;
  UPROPERTY() int32 OrcaInfeasibleAgentCount = 0;
  UPROPERTY() int32 OrcaFallbackStopCount = 0;
  UPROPERTY() int32 OrcaStopSatisfiesConstraintCount = 0;
  UPROPERTY() int32 OrcaStopViolatesConstraintCount = 0;
  UPROPERTY() uint32 PriorityOrcaHash = 2166136261u;
  UPROPERTY() int32 PriorityOrcaEqualPairCount = 0;
  UPROPERTY() int32 PriorityOrcaAsymmetricPairCount = 0;
  UPROPERTY() int32 PriorityOrcaHighSide25Count = 0;
  UPROPERTY() int32 PriorityOrcaLowSide75Count = 0;
  UPROPERTY() int32 PriorityOrcaResponsibilitySumViolationCount = 0;
  UPROPERTY() int32 WaitingOrcaInfeasibleCount = 0;
  UPROPERTY() int32 ApproachOrcaInfeasibleCount = 0;
  UPROPERTY() int32 ReservedOrcaInfeasibleCount = 0;
  UPROPERTY() int32 InsideOrcaInfeasibleCount = 0;
  UPROPERTY() int32 WaitingOrcaFallbackStopCount = 0;
  UPROPERTY() int32 ApproachOrcaFallbackStopCount = 0;
  UPROPERTY() int32 ReservedOrcaFallbackStopCount = 0;
  UPROPERTY() int32 InsideOrcaFallbackStopCount = 0;
  UPROPERTY() float OrcaSolverMsP95 = 0.0f;
  UPROPERTY() int32 ReservationTimeoutCount = 0;
  UPROPERTY() int32 TransitTimeoutCount = 0;
  UPROPERTY() int32 PortalCapacityViolationCount = 0;
  UPROPERTY() int32 ResidualPbdPenetrationPairCount = 0;
  UPROPERTY() int32 FinalObstaclePenetrationCount = 0;
  UPROPERTY() int32 Cohort0GoalReachedCount = 0;
  UPROPERTY() int32 Cohort1GoalReachedCount = 0;
  UPROPERTY() int32 FlowReachableFinalCount = 0;
  UPROPERTY() int32 FlowOutOfBoundsFinalCount = 0;
  UPROPERTY() int32 FlowBlockedRasterFinalCount = 0;
  UPROPERTY() int32 FlowUnreachableFreeFinalCount = 0;
  UPROPERTY() int32 FlowReachableToOutOfBoundsCount = 0;
  UPROPERTY() int32 FlowReachableToBlockedCellCount = 0;
  UPROPERTY() int32 FlowReachableToUnreachableFreeCount = 0;
  UPROPERTY() int32 FlowTransitionAtPredictCount = 0;
  UPROPERTY() int32 FlowTransitionAtObstacleConstraintCount = 0;
  UPROPERTY() int32 FlowTransitionAtPbdCount = 0;
  UPROPERTY() int32 FlowTransitionAtObstacleReprojectCount = 0;
  UPROPERTY() int32 FlowFinalInvalidAgentCount = 0;
  UPROPERTY() int32 FlowInvalidToReachableRecoveryCount = 0;
  UPROPERTY() int32 ContinuousLegalButBlockedCellCount = 0;
  UPROPERTY() int32 ContinuousLegalButUnreachableFreeCount = 0;
  UPROPERTY() int32 ContinuousLegalButOutOfBoundsCount = 0;
  UPROPERTY() int32 BlockedCellAndPenetratingCount = 0;
  UPROPERTY() int32 BlockedCellButNotPenetratingCount = 0;
  UPROPERTY() int32 InvalidFlowPreferredZeroCount = 0;
  UPROPERTY() int32 InvalidFlowFinalZeroVelocityCount = 0;
  UPROPERTY() int32 InvalidFlowDeadlockCount = 0;
  UPROPERTY() int32 FlowRecoveredFromRasterMismatchCount = 0;
  UPROPERTY() int32 FlowDesiredSegmentHardObstacleViolationCount = 0;
  UPROPERTY() uint32 SharedFlowFieldBuildHash = 0;
  UPROPERTY() int32 SharedFlowConnectivityContractVersion = 0;
  UPROPERTY() int32 SharedFlowValidDirectedEdgeCount = 0;
  UPROPERTY() float NavigationHardClearanceCm = 0.0f;
  UPROPERTY() int32 NavigationCenterAnchorCount = 0;
  UPROPERTY() int32 NavigationConnectionPointCount = 0;
  UPROPERTY() int32 NavigationSafeIntervalCount = 0;
  UPROPERTY() int32 NavigationInternalEdgeCount = 0;
  UPROPERTY() int32 NavigationDirectedEdgeCount = 0;
  UPROPERTY() int32 CenterInvalidButConnectedCellCount = 0;
  UPROPERTY() int32 SourceAttachmentSuccessCount = 0;
  UPROPERTY() int32 GoalAttachmentCount = 0;
  UPROPERTY() int32 NavigationUnreachableSampleCount = 0;
  UPROPERTY() uint32 NavigationV2Hash = 0;
  UPROPERTY() float FlowNearestReachableDistanceCmMax = 0.0f;
  UPROPERTY() float NavigationDomainReprojectDeltaCmMax = 0.0f;
  UPROPERTY() int32 PositionCandidateCount = 0;
  UPROPERTY() int32 HoldingCandidateCount = 0;
  UPROPERTY() int32 TransitCapacityPositionCount = 0;
  UPROPERTY() int32 TransitCapacityHoldingCount = 0;
  UPROPERTY() int32 TransitCapacityPositionDeficit = 0;
  UPROPERTY() int32 TransitCapacityHoldingDeficit = 0;
  UPROPERTY() uint32 TransitCapacitySelectionHash = 2166136261u;
  UPROPERTY() uint8 bTransitCapacitySelectionApplied = 0;
  UPROPERTY() int32 HoldingCompatibilityEdgeCount = 0;
  UPROPERTY() int32 HoldingAssignedAgentCount = 0;
  UPROPERTY() int32 HoldingArrivedAgentCount = 0;
  UPROPERTY() int32 HoldingAllocationFailureCount = 0;
  UPROPERTY() int32 HoldingSelectedCompatibilityValidCount = 0;
  UPROPERTY() int32 HoldingSelectedCompatibilityInvalidCount = 0;
  UPROPERTY() int32 HoldingDuplicateCompatibilityKeyCount = 0;
  UPROPERTY() int32 CommitRequestCount = 0;
  UPROPERTY() int32 CommitGrantedCount = 0;
  UPROPERTY() int32 CommitHeldCount = 0;
  UPROPERTY() int32 CommitInvalidCount = 0;
  UPROPERTY() int32 CommitInvalidPositionCount = 0;
  UPROPERTY() int32 CommitTargetRevisionMismatchCount = 0;
  UPROPERTY() int32 CommitCompatibilityMissingCount = 0;
  UPROPERTY() int32 CommitCompatibilityRejectedCount = 0;
  UPROPERTY() int32 CommitUniqueAgentCount = 0;
  UPROPERTY() int32 CommitMaxHeldSteps = 0;
  UPROPERTY() int32 CommitConflictCount = 0;
  UPROPERTY() int32 SteeringStatePursuitCount = 0;
  UPROPERTY() int32 SteeringStateHoldingCount = 0;
  UPROPERTY() int32 SteeringStateCommitCount = 0;
  UPROPERTY() int32 SteeringStateStableCount = 0;
  UPROPERTY() int32 SteeringStateReserveCount = 0;
  UPROPERTY() int32 SteeringStateReacquireCount = 0;
  UPROPERTY() TArray<int32> SteeringStateFinalCounts;
  UPROPERTY() TArray<float> SteeringStateDistanceCmP50;
  UPROPERTY() TArray<float> SteeringStateDistanceCmP95;
  UPROPERTY() TArray<float> SteeringStatePreferredForwardCmpsP50;
  UPROPERTY() TArray<float> SteeringStatePreferredForwardCmpsP95;
  UPROPERTY() TArray<float> SteeringStateOrcaForwardCmpsP50;
  UPROPERTY() TArray<float> SteeringStateOrcaForwardCmpsP95;
  UPROPERTY() TArray<float> SteeringStateFinalForwardCmpsP50;
  UPROPERTY() TArray<float> SteeringStateFinalForwardCmpsP95;
  UPROPERTY() int32 PursuitOutsideHandoffCount = 0;
  UPROPERTY() int32 PursuitInvalidFlowCount = 0;
  UPROPERTY() int32 HoldingFinalDistanceNotReadyCount = 0;
  UPROPERTY() int32 HoldingFinalSpeedNotReadyCount = 0;
  UPROPERTY() int32 HoldingFinalReadyConflictCount = 0;
  UPROPERTY() int32 HoldingFinalReadyGrantedCount = 0;
  UPROPERTY() int32 HoldingFinalTargetRejectCount = 0;
  UPROPERTY() int32 HoldingFinalFlowRejectCount = 0;
  UPROPERTY() int32 HoldingFinalObstacleRejectCount = 0;
  UPROPERTY() int32 HoldingFinalStableBlockerRejectCount = 0;
  UPROPERTY() int32 HoldingFinalReserveBlockerRejectCount = 0;
  UPROPERTY() int32 HoldingFinalActiveCommitConflictCount = 0;
  UPROPERTY() int32 HoldingFinalSelectedConflictCount = 0;
  UPROPERTY() int32 CommitGateYieldableStableConflictCount = 0;
  UPROPERTY() int32 CommitGateYieldableReserveConflictCount = 0;
  UPROPERTY() int32 CommitGateHardConflictHeldCount = 0;
  UPROPERTY() int32 CommitPreferredNonzeroOrcaZeroCount = 0;
  UPROPERTY() float CommitRouteForwardSpeedCmpsP50 = 0.0f;
  UPROPERTY() float CommitRouteForwardSpeedCmpsP95 = 0.0f;
  UPROPERTY() int32 StablePhysicalDisplacedCount = 0;
  UPROPERTY() float StablePhysicalDisplacementCmP95 = 0.0f;
  UPROPERTY() float StablePhysicalDisplacementCmMax = 0.0f;
  UPROPERTY() int32 ReservePhysicalDisplacedCount = 0;
  UPROPERTY() float ReservePhysicalDisplacementCmP95 = 0.0f;
  UPROPERTY() float ReservePhysicalDisplacementCmMax = 0.0f;
  UPROPERTY() int32 PhysicallySatisfiedPositionCount = 0;
  UPROPERTY() TArray<int32> SteeringStateOrcaConstraintSourceMatrix;
  UPROPERTY() TArray<int32> SteeringStateOrcaInfeasibleCounts;
  UPROPERTY() TArray<int32> SteeringStateOrcaFallbackStopCounts;
  UPROPERTY() TArray<int32> SteeringReacquireReasonCounts;
  UPROPERTY() float SteeringCommitArrivalErrorCmP95 = 0.0f;
  UPROPERTY() int32 SteeringCommitNoProgressStepsMax = 0;
  UPROPERTY() float SteeringCommitObstacleCorrectionCmP95 = 0.0f;
  UPROPERTY() float SteeringCommitPbdCorrectionCmP95 = 0.0f;
  UPROPERTY() int32 HoldingReleaseCount = 0;
  UPROPERTY() int32 CommitReleaseCount = 0;
  UPROPERTY() int32 GhostOwnerCount = 0;
  UPROPERTY() int32 PositioningNoProgressAgentCount = 0;
  UPROPERTY() uint32 HoldingCandidateHash = 2166136261u;
  UPROPERTY() uint32 HoldingAssignmentHash = 2166136261u;
  UPROPERTY() uint32 CommitDecisionHash = 2166136261u;
  UPROPERTY() uint32 SteeringStateHash = 2166136261u;
  UPROPERTY() int32 ResidualUnfinishedAgentCount = 0;
  UPROPERTY() int32 ResidualRemainingPositionCount = 0;
  UPROPERTY() int32 ResidualCompatibleEdgeCount = 0;
  UPROPERTY() int32 ResidualMaximumMatchingCount = 0;
  UPROPERTY() int32 ResidualAgentWithoutHoldingCount = 0;
  UPROPERTY() int32 ResidualAgentWithoutPositionEdgeCount = 0;
  UPROPERTY() int32 ResidualAgentWithoutCommitRouteCount = 0;
  UPROPERTY() int32 ResidualStableBlockerEdgeRejectCount = 0;
  UPROPERTY() int32 ResidualReserveBlockerEdgeRejectCount = 0;
  UPROPERTY() int32 ResidualTargetRejectCount = 0;
  UPROPERTY() int32 ResidualObstacleRejectCount = 0;
  UPROPERTY() int32 ResidualFlowRejectCount = 0;
  UPROPERTY() int32 ResidualRevisionRejectCount = 0;
  UPROPERTY() int32 ResidualCurrentMatching = 0;
  UPROPERTY() int32 ResidualNoStableMatching = 0;
  UPROPERTY() int32 ResidualNoReserveMatching = 0;
  UPROPERTY() int32 ResidualBestSingleBlockerRemovalGain = 0;
  UPROPERTY() int32 ResidualBlockerCriticalCount = 0;
  UPROPERTY() int32 ResidualTargetLimitedCount = 0;
  UPROPERTY() int32 ResidualGeometryLimitedCount = 0;
  UPROPERTY() uint32 ResidualCapacityHash = 2166136261u;
  UPROPERTY() int32 ResidualPositionValidCount = 0;
  UPROPERTY() int32 ResidualHoldingMatchingCount = 0;
  UPROPERTY() int32 ResidualJointFeasibleCount = 0;
  UPROPERTY() int32 ResidualGreedyHoldingCount = 0;
  UPROPERTY() uint32 ResidualHoldingMatchingHash = 2166136261u;
  UPROPERTY() int32 HoldingHallCurrentMatchingCount = 0;
  UPROPERTY() int32 HoldingHallNoStableOwnerMatchingCount = 0;
  UPROPERTY() int32 HoldingHallNoReserveOwnerMatchingCount = 0;
  UPROPERTY() int32 HoldingHallNoCommitOwnerMatchingCount = 0;
  UPROPERTY() int32 HoldingHallAgentCount = 0;
  UPROPERTY() int32 HoldingHallAvailableHoldingCount = 0;
  UPROPERTY() int32 HoldingHallDeficiency = 0;
  UPROPERTY() int32 HoldingHallMissingCompatibilityRecordCount = 0;
  UPROPERTY() int32 HoldingHallFlowRejectCount = 0;
  UPROPERTY() int32 HoldingHallTargetRejectCount = 0;
  UPROPERTY() int32 HoldingHallObstacleRejectCount = 0;
  UPROPERTY() int32 HoldingHallRevisionRejectCount = 0;
  UPROPERTY() int32 HoldingHallStableOwnerRejectCount = 0;
  UPROPERTY() int32 HoldingHallReserveOwnerRejectCount = 0;
  UPROPERTY() uint32 HoldingHallFixtureHash = 2166136261u;
  UPROPERTY() int32 HoldingHallFullDeficiency = 0;
  UPROPERTY() int32 HoldingHallOwnerReleaseStableMatchingCount = 0;
  UPROPERTY() int32 HoldingHallOwnerReleaseReserveMatchingCount = 0;
  UPROPERTY() int32 HoldingHallOwnerReleaseCommitMatchingCount = 0;
  UPROPERTY() int32 HoldingHallPhysicalStableRemovalMatchingCount = 0;
  UPROPERTY() int32 HoldingHallPhysicalReserveRemovalMatchingCount = 0;
  UPROPERTY() int32 HallGeometryAgentId = INDEX_NONE;
  UPROPERTY() int32 HallGeometryPositionId = INDEX_NONE;
  UPROPERTY() int32 HallGeometryBestHoldingId = INDEX_NONE;
  UPROPERTY() int32 HallGeometryBestBlockerAgentId = INDEX_NONE;
  UPROPERTY() float HallGeometryBestClearanceMarginCm = 0.0f;
  UPROPERTY() int32 HallGeometryNonNegativeMarginHoldingCount = 0;
  UPROPERTY() int32 HallGeometryTargetOnlyRejectCount = 0;
  UPROPERTY() int32 HallGeometryStableOnlyRejectCount = 0;
  UPROPERTY() int32 HallGeometryMultiLabelRejectCount = 0;
  UPROPERTY() int32 HallGeometrySelfBlockerCount = 0;
  UPROPERTY() int32 HallGeometryWitnessPositionBlockerCount = 0;
  UPROPERTY() int32 HallGeometryDuplicateBlockerCount = 0;
  UPROPERTY() int32 HallGeometryStaleBlockerCount = 0;
  UPROPERTY() int32 HallGeometryRadiusSemanticsErrorCount = 0;
  UPROPERTY() int32 HallGeometryEndpointContactCount = 0;
  UPROPERTY() int32 HallGeometryFormalMismatchCount = 0;
  UPROPERTY() uint32 HallGeometryFixtureHash = 2166136261u;
  UPROPERTY() int32 JointPositioningMaximumCardinality = 0;
  UPROPERTY() int32 JointPositioningHardLockedCount = 0;
  UPROPERTY() int32 JointPositioningReusedCombinationCount = 0;
  UPROPERTY() int32 JointPositioningUnmatchedAgentCount = 0;
  UPROPERTY() int32 JointPositioningDuplicateHoldingCount = 0;
  UPROPERTY() int32 JointPositioningDuplicatePositionCount = 0;
  UPROPERTY() uint32 JointPositioningHash = 2166136261u;
  UPROPERTY() int32 JointCommitResidualCandidateCount = 0;
  UPROPERTY() int32 JointCommitResidualFeasibleCount = 0;
  UPROPERTY() int32 JointCommitResidualInfeasibleCount = 0;
  UPROPERTY() uint32 JointCommitResidualHash = 2166136261u;
  UPROPERTY() int32 Sf4UnfinishedBoundaryAgentCount = 0;
  UPROPERTY() uint32 Sf4UnfinishedBoundaryHash = 2166136261u;
  UPROPERTY() int32 Sf4PhysicalUnsatisfiedAgentCount = 0;
  UPROPERTY() int32 Sf4PhysicalUnsatisfiedTotalAgentCount = 0;
  UPROPERTY() int32 Sf4PhysicalUnsatisfiedSatisfiedCount = 0;
  UPROPERTY() int32 Sf4PhysicalUnsatisfiedCountClosed = 0;
  UPROPERTY() uint32 Sf4PhysicalUnsatisfiedHash = 2166136261u;
  UPROPERTY() int32 TransitCapacityShadowComponentCount = 0;
  UPROPERTY() int32 TransitCapacityShadowMaximumComponentSize = 0;
  UPROPERTY() int32 TransitCapacityShadowComponent2Count = 0;
  UPROPERTY() int32 TransitCapacityShadowComponent5Count = 0;
  UPROPERTY() int32 TransitCapacityShadowComponent8Count = 0;
  UPROPERTY() int32 TransitCapacityShadowComponent12Count = 0;
  UPROPERTY() int32 TransitCapacityShadowComponent20Count = 0;
  UPROPERTY() int32 TransitCapacityShadowOversizeCount = 0;
  UPROPERTY() int32 TransitCapacityShadowSolvedCount = 0;
  UPROPERTY() int32 TransitCapacityShadowInfeasibleCount = 0;
  UPROPERTY() int32 TransitCapacityShadowHardInfeasibleCount = 0;
  UPROPERTY() int32 TransitCapacityShadowIterationLimitCount = 0;
  UPROPERTY() int32 TransitCapacityShadowClearanceNotAchievedCount = 0;
  UPROPERTY() int32 TransitCapacityShadowNoForwardGainCount = 0;
  UPROPERTY() int32 TransitCapacityShadowInvalidInputCount = 0;
  UPROPERTY() int32 TransitCapacityShadowNumericalFailureCount = 0;
  UPROPERTY() int32 TransitCapacityShadowQuantizedFailureCount = 0;
  UPROPERTY() int32 TransitCapacityShadowYieldingAgentCount = 0;
  UPROPERTY() int32 TransitCapacityShadowDirectRelevantAgentCount = 0;
  UPROPERTY() int32 TransitCapacityShadowHardSafetyClosureAgentCount = 0;
  UPROPERTY() int32 TransitCapacityShadowHardPairViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowJointCandidateHardPairViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowBaselineHardPairViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowObstacleViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowFlowBoundsViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowTargetViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowJointCandidateFlowBoundsViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowJointCandidateObstacleViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowJointCandidateTargetViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowBaselineFlowBoundsViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowBaselineObstacleViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowBaselineTargetViolationCount = 0;
  UPROPERTY() int32 TransitCapacityShadowPairDoubleOwnerCount = 0;
  UPROPERTY() int32 TransitCapacityShadowForwardSpeedRatioQ15 = 0;
  UPROPERTY() float TransitCapacityShadowPreferredSpacingDeficitCmMax = 0.0f;
  UPROPERTY() float TransitCapacityShadowApertureDeficitCmMax = 0.0f;
  UPROPERTY() float TransitCapacityShadowClearanceDeficitCmMax = 0.0f;
  UPROPERTY() float TransitCapacityShadowJointCandidateClearanceDeficitCmMax = 0.0f;
  UPROPERTY() float TransitCapacityShadowBaselineClearanceDeficitCmMax = 0.0f;
  UPROPERTY() float TransitCapacityShadowMaximumYieldDisplacementCm = 0.0f;
  UPROPERTY() float TransitCapacityShadowSolverMsP95 = -1.0f;
  UPROPERTY() uint32 TransitCapacityShadowHash = 2166136261u;
  UPROPERTY() int32 ElasticSpacingPairCount = 0;
  UPROPERTY() int32 ElasticInfluencedAgentCount = 0;
  UPROPERTY() int32 ElasticPropagationLayerMax = 0;
  UPROPERTY() float ElasticSpacingDeficitCmP95 = 0.0f;
  UPROPERTY() float ElasticSpacingDeficitCmMax = 0.0f;
  UPROPERTY() float ElasticTransitDeficitCmP95 = 0.0f;
  UPROPERTY() float ElasticTransitDeficitCmMax = 0.0f;
  UPROPERTY() int32 ElasticSourceForwardRatioQ15 = 0;
  UPROPERTY() int32 ElasticBaselineSourceForwardRatioQ15 = 0;
  UPROPERTY() int32 ElasticZeroProgressStepMax = 0;
  UPROPERTY() float ElasticRecoveryErrorCmP95 = 0.0f;
  UPROPERTY() int32 ElasticHardPairViolationCount = 0;
  UPROPERTY() int32 ElasticObstacleViolationCount = 0;
  UPROPERTY() int32 ElasticFlowBoundsViolationCount = 0;
  UPROPERTY() int32 ElasticTargetViolationCount = 0;
  UPROPERTY() int32 ElasticInvalidInputCount = 0;
  UPROPERTY() float ElasticSolverMsP95 = -1.0f;
  UPROPERTY() uint32 ElasticShadowHash = 2166136261u;
  UPROPERTY() int32 ElasticFailureFixtureAgentCount = 0;
  UPROPERTY() uint32 ElasticFailureFixtureHash = 2166136261u;
  UPROPERTY() TArray<uint32> ElasticBaselineStageHashes;
  UPROPERTY() TArray<uint32> ElasticTwinStageHashes;
  UPROPERTY() TArray<int32> ElasticBaselineStageHardPairCounts;
  UPROPERTY() TArray<int32> ElasticTwinStageHardPairCounts;
  UPROPERTY() TArray<int32> ElasticBaselineStageTargetCounts;
  UPROPERTY() TArray<int32> ElasticTwinStageTargetCounts;
  UPROPERTY() TArray<int32> ElasticBaselineStageSourceForwardQ15;
  UPROPERTY() TArray<int32> ElasticTwinStageSourceForwardQ15;
  UPROPERTY() int32 ElasticObstacleClippedCount = 0;
  UPROPERTY() int32 ElasticObstacleSlideCount = 0;
  UPROPERTY() int32 ElasticObstacleStoppedCount = 0;
  UPROPERTY() float ElasticObstacleConstraintDeltaCmMax = 0.0f;
  UPROPERTY() int32 ElasticOrcaInfeasibleCount = 0;
  UPROPERTY() int32 ElasticOrcaFallbackStopCount = 0;
  UPROPERTY() int32 ElasticOrcaStopViolationCount = 0;
  UPROPERTY() int32 ElasticFailureFixedStep = INDEX_NONE;
  UPROPERTY() int32 ElasticFailureStage = INDEX_NONE;
  UPROPERTY() int32 ElasticFailureKind = 0;
  UPROPERTY() int32 ElasticFailureAttribution = 0;
  UPROPERTY() int32 ElasticFailureClosureAgentCount = 0;
  UPROPERTY() int32 ElasticFailureFixtureTooLarge = 0;
  UPROPERTY() int32 ElasticParallelCompletedSteps = 0;
  UPROPERTY() int32 ElasticParallelEligibleRecoveryCount = 0;
  UPROPERTY() int32 ElasticParallelBaselineRecoveryCompletedCount = 0;
  UPROPERTY() int32 ElasticParallelRecoveryCompletedCount = 0;
  UPROPERTY() int32 ElasticParallelBaselineImprovedCount = 0;
  UPROPERTY() int32 ElasticParallelImprovedCount = 0;
  UPROPERTY() int32 ElasticParallelBaselinePermanentHoleCount = 0;
  UPROPERTY() int32 ElasticParallelPermanentHoleCount = 0;
  UPROPERTY() float ElasticParallelBaselineRecoveryTimeP95 = -1.0f;
  UPROPERTY() float ElasticParallelRecoveryTimeP95 = -1.0f;
  UPROPERTY() float ElasticParallelBaselineEndErrorCmP95 = -1.0f;
  UPROPERTY() float ElasticParallelEndErrorCmP95 = -1.0f;
  UPROPERTY() uint32 ElasticParallelHash = 2166136261u;
  UPROPERTY() int32 ElasticParallelBaselineSourceForwardQ15 = 32767;
  UPROPERTY() int32 ElasticParallelSourceForwardQ15 = 32767;
  UPROPERTY() int32 ElasticParallelBaselineHardPairViolationCount = 0;
  UPROPERTY() int32 ElasticParallelHardPairViolationCount = 0;
  UPROPERTY() int32 ElasticParallelBaselineObstaclePenetrationCount = 0;
  UPROPERTY() int32 ElasticParallelObstaclePenetrationCount = 0;
  UPROPERTY() int32 ElasticParallelBaselineTargetViolationCount = 0;
  UPROPERTY() int32 ElasticParallelTargetViolationCount = 0;
  UPROPERTY() int32 ElasticParallelBaselineOrcaStopViolationCount = 0;
  UPROPERTY() int32 ElasticParallelOrcaStopViolationCount = 0;
  UPROPERTY() int32 TransitCapacityFailureFixtureAgentCount = 0;
  UPROPERTY() int32 TransitCapacityFailureFixturePairCount = 0;
  UPROPERTY() int32 TransitCapacityFailureFixtureStatus = 0;
  UPROPERTY() uint32 TransitCapacityFailureFixtureHash = 2166136261u;
  UPROPERTY() int32 PositionFrontCapacity = 0;
  UPROPERTY() int32 PositionReserveCapacity = 0;
  UPROPERTY() int32 PositionAssignedCount = 0;
  UPROPERTY() int32 PositionUnassignedCount = 0;
  UPROPERTY() int32 PositionStableOccupiedCount = 0;
  UPROPERTY() int32 PositionReserveHoldCount = 0;
  UPROPERTY() int32 PositionAssignmentReusedCount = 0;
  UPROPERTY() int32 PositionAssignmentChangedCount = 0;
  UPROPERTY() int32 PositionAssignmentChurnCount = 0;
  UPROPERTY() int32 PositionInvalidatedCount = 0;
  UPROPERTY() int32 PositionPromotionCount = 0;
  UPROPERTY() int32 PositionPromotionTransitionCount = 0;
  UPROPERTY() int32 PositionPromotionAgentCount = 0;
  UPROPERTY() int32 PositionFrontAdmissionGrantCount = 0;
  UPROPERTY() int32 PositionFrontAdmissionRequeueCount = 0;
  UPROPERTY() uint32 PositionFrontAdmissionDecisionHash = 2166136261u;
  UPROPERTY() int32 PhaseReservationRequestCount = 0;
  UPROPERTY() int32 PhaseReservationGrantedCount = 0;
  UPROPERTY() int32 PhaseReservationHeldCount = 0;
  UPROPERTY() int32 PhaseReservationInvalidCount = 0;
  UPROPERTY() int32 PhaseReservationTargetExclusionRejectCount = 0;
  UPROPERTY() int32 PhaseReservationRouteConflictCount = 0;
  UPROPERTY() int32 PhaseReservationTransitionCount = 0;
  UPROPERTY() float PhaseReservationHeldStepsP95 = 0.0f;
  UPROPERTY() uint32 PhaseReservationDecisionHash = 2166136261u;
  UPROPERTY() int32 PhaseReservationClientHashMatch = 0;
  UPROPERTY() int32 PhaseReservationUniqueBlockedRequestCount = 0;
  UPROPERTY() int32 PhaseReservationUniqueBlockerCount = 0;
  UPROPERTY() int32 PhaseReservationWaitEdgeCount = 0;
  UPROPERTY() int32 PhaseReservationReciprocalEdgeCount = 0;
  UPROPERTY() int32 PhaseReservationCycleCount = 0;
  UPROPERTY() int32 PhaseReservationMaxCycleSize = 0;
  UPROPERTY() int32 PhaseReservationStalledBlockerCount = 0;
  UPROPERTY() int32 PhaseReservationProgressingBlockerCount = 0;
  UPROPERTY() int32 PhaseReservationStaleOwnerCount = 0;
  UPROPERTY() int32 PhaseReservationBlockerRadialCount = 0;
  UPROPERTY() int32 PhaseReservationBlockerAngularCount = 0;
  UPROPERTY() int32 PhaseReservationBlockerRadialCommitCount = 0;
  UPROPERTY() int32 PhaseReservationAtomicHandoffCycleCount = 0;
  UPROPERTY() int32 PhaseReservationMaxAtomicHandoffSetSize = 0;
  UPROPERTY() uint32 PhaseReservationWaitGraphHash = 2166136261u;
  UPROPERTY() int32 PhaseReservationWaitGraphClientHashMatch = 0;
  UPROPERTY() uint32 PhaseReservationWaitGraphFixtureHash = 0;
  UPROPERTY() int32 PhaseReservationWaitGraphFixtureAgentCount = 0;
  UPROPERTY() int32 PhaseReservationWaitGraphFixtureEdgeCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaFixtureValid = 0;
  UPROPERTY() int32 Sf4ReservationOrcaFixtureTooLarge = 0;
  UPROPERTY() int32 Sf4ReservationOrcaFixtureAgentCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaCoreConstraintCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaActiveConflictCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaActiveDisjointContainedCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaActiveOutsideCorridorCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaWaitingCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaStableCount = 0;
  UPROPERTY() int32 Sf4ReservationOrcaOtherCount = 0;
  UPROPERTY() uint32 Sf4ReservationOrcaFixtureHash = 0;
  UPROPERTY() int32 Sf4ReservationOrcaClientHashMatch = 0;
  UPROPERTY() int32 TransitJointFixtureValid = 0;
  UPROPERTY() int32 TransitJointFixtureTooLarge = 0;
  UPROPERTY() int32 TransitJointFixtureAgentCount = 0;
  UPROPERTY() int32 TransitJointFixturePairCount = 0;
  UPROPERTY() int32 TransitJointFixtureConstraintCount = 0;
  UPROPERTY() int32 TransitJointPriorityForwardSpeedCmps = 0;
  UPROPERTY() int32 TransitJointFinalSpeedCmps = 0;
  UPROPERTY() int32 TransitJointForwardSpeedCmps = 0;
  UPROPERTY() int32 TransitJointDownstreamZeroStage = 0;
  UPROPERTY() int32 TransitJointSafeForward = 0;
  UPROPERTY() uint32 TransitJointFixtureHash = 0;
  UPROPERTY() int32 TransitJointClientHashMatch = 0;
  UPROPERTY() float PositionPromotionWaitStepsP95 = 0.0f;
  UPROPERTY() float PositionArrivalErrorCmP95 = 0.0f;
  UPROPERTY() int32 PositionExitAfterOccupiedCount = 0;
  UPROPERTY() int32 PositionCandidateOverlapCount = 0;
  UPROPERTY() int32 PositionCandidateUnreachableCount = 0;
  UPROPERTY() int32 TargetRevisionCount = 0;
  UPROPERTY() int32 TargetReanchorCount = 0;
  UPROPERTY() float TargetFollowErrorCmP95 = 0.0f;
  UPROPERTY() float TargetStopToSettleSeconds = -1.0f;
  UPROPERTY() uint32 PositionCandidateHash = 0;
  UPROPERTY() uint32 PositionAssignmentHash = 0;
  UPROPERTY() uint32 TargetFactHash = 0;
  UPROPERTY() int32 PositionSlotCommitCount = 0;
  UPROPERTY() int32 PositionReserveCommitCount = 0;
  UPROPERTY() int32 PositionUnsettledPortalOwnedCount = 0;
  UPROPERTY() int32 PositionUnsettledOutsideComposeRangeCount = 0;
  UPROPERTY() int32 PositionUnsettledGuidanceActiveCount = 0;
  UPROPERTY() int32 PositionUnsettledArrivalSpeedRejectedCount = 0;
  UPROPERTY() int32 PositionUnsettledErrorLe30Count = 0;
  UPROPERTY() int32 PositionUnsettledError31To100Count = 0;
  UPROPERTY() int32 PositionUnsettledError101To300Count = 0;
  UPROPERTY() int32 PositionUnsettledErrorOver300Count = 0;
  UPROPERTY() int32 PositionUnsettledPreviousOrcaFallbackCount = 0;
  UPROPERTY() int32 PositionUnsettledPreviousOrcaInfeasibleCount = 0;
  UPROPERTY() int32 PositionUnsettledPreviousPbdCorrectedCount = 0;
  UPROPERTY() float PositionUnsettledSpeedCmpsP95 = 0.0f;
  UPROPERTY() float PositionUnsettledGuidanceSpeedCmpsP95 = 0.0f;
  UPROPERTY() float PositionUnsettledOrcaSpeedCmpsP95 = 0.0f;
  UPROPERTY() float PositionUnsettledObstacleSpeedCmpsP95 = 0.0f;
  UPROPERTY() int32 PositionUnsettledOrcaAdjustedCount = 0;
  UPROPERTY() int32 PositionUnsettledOrcaZeroCount = 0;
  UPROPERTY() int32 PositionUnsettledObstacleHitCount = 0;
  UPROPERTY() float PositionUnsettledOrcaConstraintP95 = 0.0f;
  UPROPERTY() int32 PositionIngressSlotCommitCount = 0;
  UPROPERTY() int32 PositionIngressErrorOver300Count = 0;
  UPROPERTY() int32 PositionIngressTargetBlockedCount = 0;
  UPROPERTY() int32 PositionIngressStableBlockedCount = 0;
  UPROPERTY() int32 PositionIngressReserveBlockedCount = 0;
  UPROPERTY() int32 PositionIngressCommitBlockedCount = 0;
  UPROPERTY() int32 PositionIngressStableBlockerPairCount = 0;
  UPROPERTY() int32 PositionIngressReserveBlockerPairCount = 0;
  UPROPERTY() int32 PositionIngressCommitBlockerPairCount = 0;
  UPROPERTY() float PositionIngressSectorDeltaP50 = 0.0f;
  UPROPERTY() float PositionIngressSectorDeltaP95 = 0.0f;
  UPROPERTY() float PositionIngressSectorDeltaMax = 0.0f;
  UPROPERTY() float PositionIngressRadialDeltaP50 = 0.0f;
  UPROPERTY() float PositionIngressRadialDeltaP95 = 0.0f;
  UPROPERTY() float PositionIngressRadialDeltaMax = 0.0f;
  UPROPERTY() int32 PositionIngressUnblockedAlternativeFrontCount = 0;
  UPROPERTY() int32 PositionIngressSameSideAlternativeFrontCount = 0;
  UPROPERTY() int32 PositionIngressNoAlternativeFrontCount = 0;
  UPROPERTY() int32 PositionIngressOrcaFromStableCount = 0;
  UPROPERTY() int32 PositionIngressOrcaFromReserveCount = 0;
  UPROPERTY() int32 PositionIngressOrcaFromCommitCount = 0;
  UPROPERTY() int32 PositionIngressOrcaFromOtherCount = 0;
  UPROPERTY() float PositionIngressPreferredSpeedP95 = 0.0f;
  UPROPERTY() float PositionIngressOrcaSpeedP95 = 0.0f;
  UPROPERTY() float PositionIngressObstacleSpeedP95 = 0.0f;
  UPROPERTY() float PositionIngressFinalSpeedP95 = 0.0f;
  UPROPERTY() int32 PositionIngressLowSpeedStepsMax = 0;
  UPROPERTY() int32 PositionIngressTargetExclusionCrossingCount = 0;
  UPROPERTY() int32 PositionIngressOrderInversionCount = 0;
  UPROPERTY() int32 PositionIngressPbdPushAwayCount = 0;
  UPROPERTY() int32 PositionIngressObstaclePushAwayCount = 0;
  UPROPERTY() uint32 PositionIngressMinimumFixtureHash = 0;
  UPROPERTY() int32 PositionIngressMinimumFixtureConstraintCount = 0;
  UPROPERTY() uint32 PositionIngressEvaluationHash = 0;
  UPROPERTY() int32 PositionFrontAssignedWaitingCount = 0;
  UPROPERTY() int32 PositionFrontRadialStageCount = 0;
  UPROPERTY() int32 PositionFrontAngularAlignCount = 0;
  UPROPERTY() int32 PositionFrontRadialCommitCount = 0;
  UPROPERTY() int32 PositionFrontGateInvalidCount = 0;
  UPROPERTY() int32 PositionFrontRadialCommitBlockedCount = 0;
  UPROPERTY() uint32 PositionFrontRouteHash = 2166136261u;
  UPROPERTY() float PositionFrontRadialPreferredSpeedP95 = 0.0f;
  UPROPERTY() float PositionFrontRadialOrcaSpeedP95 = 0.0f;
  UPROPERTY() float PositionFrontRadialFinalSpeedP95 = 0.0f;
  UPROPERTY() float PositionFrontRadialOrcaForwardSpeedP50 = 0.0f;
  UPROPERTY() float PositionFrontRadialOrcaForwardSpeedMin = 0.0f;
  UPROPERTY() float PositionFrontRadialFinalForwardSpeedP50 = 0.0f;
  UPROPERTY() float PositionFrontRadialFinalForwardSpeedMin = 0.0f;
  UPROPERTY() float PositionFrontRadialOrcaConstraintP95 = 0.0f;
  UPROPERTY() int32 PositionFrontRadialConstraintFromActiveCount = 0;
  UPROPERTY() int32 PositionFrontRadialConstraintFromWaitingCount = 0;
  UPROPERTY() int32 PositionFrontRadialConstraintFromReserveCommitCount = 0;
  UPROPERTY() int32 PositionFrontRadialConstraintFromStableCount = 0;
  UPROPERTY() int32 PositionFrontRadialConstraintFromOtherCount = 0;
  UPROPERTY() float PositionFrontRadialErrorP50 = 0.0f;
  UPROPERTY() float PositionFrontRadialErrorP95 = 0.0f;
  UPROPERTY() float PositionFrontRadialErrorMax = 0.0f;
  UPROPERTY() int32 PositionFrontRadialErrorImprovedCount = 0;
  UPROPERTY() int32 PositionFrontRadialQuantizedProgressStallCount = 0;
  UPROPERTY() int32 PositionFrontComposeBoundarySwitchCount = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoElasticCrowdSettings
{
  GENERATED_BODY()

  UPROPERTY() float FixedStepSeconds = 1.0f / 30.0f;
  UPROPERTY() float HardSafetyGapCm = 10.0f;
  UPROPERTY() float PreferredSpacingGapCm = 34.0f;
  UPROPERTY() float SpacingGainPerSecond = 2.0f;
  UPROPERTY() float MaxSpacingResponseCmps = 120.0f;
  UPROPERTY() float TransitHorizonSeconds = 0.75f;
  UPROPERTY() float TransitInfluenceFalloffCm = 34.0f;
  UPROPERTY() float TransitGainPerSecond = 2.0f;
  UPROPERTY() float MaxTransitYieldSpeedCmps = 260.0f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float VelocityQuantumCmps = 1.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetApproachRuleSettings
{
  GENERATED_BODY()

  UPROPERTY() uint8 bEnabled = 0;
  UPROPERTY() float TransitionRingRadiusCm = 600.0f;
  UPROPERTY() float RingEnterToleranceCm = 10.0f;
  UPROPERTY() float RingExitToleranceCm = 40.0f;
  UPROPERTY() float ApproachSlowdownDistanceCm = 200.0f;
  UPROPERTY() float SlotArrivalToleranceCm = 20.0f;
  UPROPERTY() float SlotArrivalSpeedToleranceCmps = 20.0f;
  UPROPERTY() float SlotExitToleranceCm = 40.0f;
  UPROPERTY() float SlotArriveGainPerSecond = 2.0f;
  UPROPERTY() float SlotOccupiedGainPerSecond = 0.5f;
  UPROPERTY() float FreeSettleAttractionGainPerSecond = 1.0f;
  UPROPERTY() float FreeSettleMaxSpeedCmps = 180.0f;
  UPROPERTY() float TargetPhysicalRadiusCm = 100.0f;
  UPROPERTY() float TargetHardSafetyGapCm = 10.0f;
  UPROPERTY() float TargetSoftMarginCm = 17.0f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float VelocityQuantumCmps = 1.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetMotionRule
{
  GENERATED_BODY()

  UPROPERTY() int32 TargetId = 1;
  UPROPERTY() int32 TargetRevision = 1;
  UPROPERTY() FVector_NetQuantize10 InitialLocation = FVector(0.0f, 2200.0f, 60.0f);
  UPROPERTY() FVector_NetQuantize10 LinearVelocity = FVector::ZeroVector;
  UPROPERTY() float InitialYawDegrees = 0.0f;
  UPROPERTY() float YawRateDegreesPerSecond = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetInfluenceRuleSettings
{
  GENERATED_BODY()

  UPROPERTY() uint8 bEnabled = 0;
  UPROPERTY() float DefaultMinimumCombatCenterDistanceCm = 100.0f;
  UPROPERTY() float DefaultMaximumCombatCenterDistanceCm = 850.0f;
  UPROPERTY() float InfluenceBlendWidthCm = 300.0f;
  UPROPERTY() float RadialGainPerSecond = 2.0f;
  UPROPERTY() float MaxRadialSpeedCmps = 300.0f;
  UPROPERTY() float TargetPhysicalRadiusCm = 100.0f;
  UPROPERTY() float TargetHardSafetyGapCm = 10.0f;
  UPROPERTY() float TargetSoftMarginCm = 17.0f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float VelocityQuantumCmps = 1.0f;
  UPROPERTY() int32 AngularSectorCount = 16;
  UPROPERTY() float RadialBandWidthCm = 100.0f;
  UPROPERTY() int32 DensitySmoothingPassCount = 1;
  UPROPERTY() int32 DensityMinimumDifference = 1;
  UPROPERTY() float DensitySpeedPerExcessAgentCmps = 20.0f;
  UPROPERTY() float MaximumDensityTangentialSpeedCmps = 120.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetRegionTransportRuleSettings
{
  GENERATED_BODY()

  UPROPERTY() uint8 bEnabled = 0;
  UPROPERTY() float RadialBandWidthCm = 100.0f;
  UPROPERTY() float TransportSpeedCmps = 300.0f;
  UPROPERTY() int32 DemandRegionCount = 16;
  UPROPERTY() int32 PlanLifetimeSteps = 15;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetSlotRule
{
  GENERATED_BODY()

  UPROPERTY() int32 SlotId = INDEX_NONE;
  UPROPERTY() uint8 bFunctional = 0;
  UPROPERTY() FVector_NetQuantize10 TargetRelativeOffset = FVector::ZeroVector;
  UPROPERTY() uint32 RequiredCapabilityMask = 0;
  UPROPERTY() int32 StablePriority = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetSlotBandRule
{
  GENERATED_BODY()

  UPROPERTY() int32 BandId = INDEX_NONE;
  UPROPERTY() uint8 bFunctional = 0;
  UPROPERTY() int32 Capacity = 0;
  UPROPERTY() float PreferredSurfaceDistanceCm = 0.0f;
  UPROPERTY() float MinimumCenterDistanceCm = 0.0f;
  UPROPERTY() float MaximumCenterDistanceCm = 0.0f;
  UPROPERTY() float StartAngleDegrees = 0.0f;
  UPROPERTY() uint32 RequiredCapabilityMask = 0;
  UPROPERTY() int32 StablePriorityBase = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetSlotLayoutRuleSettings
{
  GENERATED_BODY()

  UPROPERTY() int32 SourceRevision = 1;
  UPROPERTY() TArray<FCrowdDemoTargetSlotBandRule> Bands;
  UPROPERTY() float TargetHardSafetyGapCm = 10.0f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float AngleQuantumDegrees = 0.01f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRangedCombatSettings
{
  GENERATED_BODY()

  UPROPERTY() uint8 bEnabled = 0;
  UPROPERTY() int32 ShooterCount = 10;
  UPROPERTY() int32 WindupFixedSteps = 15;
  UPROPERTY() int32 RecoveryFixedSteps = 12;
  UPROPERTY() int32 CooldownFixedSteps = 30;
  UPROPERTY() float ProjectileSpeedCmps = 1800.0f;
  UPROPERTY() float ProjectileRadiusCm = 12.0f;
  UPROPERTY() int32 ProjectileLifetimeFixedSteps = 60;
  UPROPERTY() float MuzzleForwardOffsetCm = 70.0f;
  UPROPERTY() float Damage = 20.0f;
  UPROPERTY() float HorizontalImpulseCmps = 0.0f;
  UPROPERTY() float VerticalImpulseCmps = 0.0f;
  UPROPERTY() float PositionQuantumCm = 1.0f;
  UPROPERTY() float VelocityQuantumCmps = 1.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoProjectileMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() int32 TargetAcquiredCount = 0;
  UPROPERTY() int32 CompletedWindupCount = 0;
  UPROPERTY() int32 ProjectileSpawnedCount = 0;
  UPROPERTY() int32 ProjectileActiveCount = 0;
  UPROPERTY() int32 ProjectileImpactedCount = 0;
  UPROPERTY() int32 ProjectileExpiredCount = 0;
  UPROPERTY() int32 DuplicateFireCount = 0;
  UPROPERTY() int32 DuplicateHitCount = 0;
  UPROPERTY() int32 DamageAppliedCount = 0;
  UPROPERTY() int32 VisualSpawnEventCount = 0;
  UPROPERTY() int32 VisualImpactEventCount = 0;
  UPROPERTY() int32 VisualExpireEventCount = 0;
  UPROPERTY() int32 InvalidTargetLifecycleCount = 0;
  UPROPERTY() int32 InvalidProjectileCount = 0;
  UPROPERTY() uint32 AttackStateHash = 2166136261u;
  UPROPERTY() uint32 ProjectileStateHash = 2166136261u;
  UPROPERTY() uint32 EventHash = 2166136261u;
};

USTRUCT(BlueprintType)
struct FCrowdDemoLocalPredictiveRuleSettings
{
  GENERATED_BODY()

  UPROPERTY() uint8 bEnabled = 1;
  UPROPERTY() float TimeHorizonSeconds = 1.25f;
  UPROPERTY() float SpatialCellSizeCm = 600.0f;
  UPROPERTY() float ConstraintEpsilonCmps = 0.1f;
  UPROPERTY() float RequestedProgressThresholdCmps = 30.0f;
  UPROPERTY() float BlockedProgressThresholdCmps = 10.0f;
  UPROPERTY() float GrantedResponsibility = 0.25f;
  UPROPERTY() int32 GrantDurationSteps = 30;
  UPROPERTY() int32 JointIterationCount = 64;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundRules
{
  GENERATED_BODY()

  UPROPERTY()
  ECrowdDemoRoundStartPolicy RoundStartPolicy =
    ECrowdDemoRoundStartPolicy::ResetToStableInitialState;

  UPROPERTY()
  ECrowdDemoScenario Scenario = ECrowdDemoScenario::SimRoundObstacle;

  UPROPERTY()
  int32 RandomSeed = 1337;

  UPROPERTY()
  float FixedStepSeconds = 1.0f / 30.0f;

  UPROPERTY()
  float MaxSpeedCmPerSecond = 260.0f;

  UPROPERTY()
  FVector_NetQuantize10 SpawnOrigin = FVector(0.0f, -2900.0f, 60.0f);

  UPROPERTY()
  int32 FormationColumns = 0;

  UPROPERTY()
  float FormationSpacingCm = 95.0f;

  UPROPERTY()
  uint8 bEnableSeparation = 0;

  UPROPERTY()
  float SeparationCellSizeCm = 96.0f;

  UPROPERTY()
  float SeparationRadiusCm = 78.0f;

  UPROPERTY()
  float HardSeparationRadiusCm = 42.0f;

  UPROPERTY()
  float SeparationSpeedCmPerSecond = 120.0f;

  UPROPERTY()
  float HardSeparationSpeedCmPerSecond = 260.0f;

  UPROPERTY()
  float SeparationMaxOffsetCm = 320.0f;

  UPROPERTY()
  uint8 bEnableObstacle = 0;

  UPROPERTY()
  uint8 bEnableHardSeparationPbd = 0;

  UPROPERTY()
  int32 HardSeparationPbdIterations = 3;

  UPROPERTY()
  float HardSeparationPbdMaxCorrectionCm = 24.0f;

  UPROPERTY()
  int32 ParticleConstraintIterations = 8;

  UPROPERTY()
  int32 ParticleSafetyIterations = 8;

  UPROPERTY()
  float ParticleSoftResponsePerSecond = 8.0f;

  UPROPERTY()
  float ParticleSoftMaxCorrectionCm = 8.0f;

  UPROPERTY()
  float ParticleHardMaxCorrectionCm = 24.0f;

  UPROPERTY()
  float ParticlePositionQuantumCm = 1.0f;

  UPROPERTY()
  float ParticleVelocityQuantumCmps = 1.0f;

  UPROPERTY()
  FCrowdDemoLocalPredictiveRuleSettings LocalPredictiveSettings;

  UPROPERTY()
  FCrowdDemoSharedFlowFieldConfig FlowFieldConfig;

  UPROPERTY()
  FCrowdDemoParticleProfile ParticleProfile;

  UPROPERTY()
  FCrowdDemoTrafficSettings TrafficSettings;

  UPROPERTY()
  FCrowdDemoOrcaSettings OrcaSettings;

  UPROPERTY()
  FCrowdDemoElasticCrowdSettings ElasticCrowdSettings;

  UPROPERTY()
  ECrowdDemoSoftPressureTestCase SoftPressureTestCase = ECrowdDemoSoftPressureTestCase::CorridorRoute;

  UPROPERTY()
  uint8 bEnableHeterogeneousProfiles = 0;

  UPROPERTY()
  FCrowdDemoTargetApproachRuleSettings TargetApproachSettings;

  UPROPERTY()
  FCrowdDemoTargetMotionRule TargetMotion;

  UPROPERTY()
  FCrowdDemoTargetInfluenceRuleSettings TargetInfluenceSettings;

  UPROPERTY()
  FCrowdDemoTargetRegionTransportRuleSettings TargetRegionTransportSettings;

  UPROPERTY()
  FCrowdDemoTargetSlotLayoutRuleSettings TargetSlotLayoutSettings;

  UPROPERTY()
  FCrowdDemoRangedCombatSettings RangedCombatSettings;

  UPROPERTY()
  TArray<FCrowdDemoTargetSlotRule> TargetSlots;

  UPROPERTY()
  TArray<FCrowdDemoTrafficCohortRule> TrafficCohorts;

  float GetParticleEnvironmentHardClearanceCm() const
  {
    const float PhysicalClearance = ParticleProfile.GetNavigationHardClearanceCm();
    return bEnableHeterogeneousProfiles != 0
      ? FMath::Max(PhysicalClearance, FlowFieldConfig.AgentInflateCm)
      : PhysicalClearance;
  }

};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundBootstrapPacket
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 Revision = 0;

  UPROPERTY()
  float ServerTimeSeconds = 0.0f;

  UPROPERTY()
  TArray<FCrowdDemoRoundAgentState> Agents;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundPlanPacket
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 Revision = 0;

  UPROPERTY()
  int32 PreviousCheckpointRevision = 0;

  UPROPERTY()
  float StartServerTimeSeconds = 0.0f;

  UPROPERTY()
  float DurationSeconds = 1.5f;

  UPROPERTY()
  FCrowdDemoRoundRules Rules;
};

USTRUCT(BlueprintType)
struct FCrowdDemoSoftPressureRouteMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() uint32 DiagnosticHash = 0;
  UPROPERTY() int32 SelectedBranch = 0;
  UPROPERTY() int32 SelectedAgentCount = 0;
  UPROPERTY() int32 NeverReachedAgentCount = 0;
  UPROPERTY() int32 ReachedThenLeftAgentCount = 0;
  UPROPERTY() int32 GoalBoundaryTransitionCount = 0;
  UPROPERTY() int32 ZeroToMaxSpeedTransitionCount = 0;
  UPROPERTY() int32 MaxToZeroSpeedTransitionCount = 0;
  UPROPERTY() int32 CorridorEverStalledAgentCount = 0;
  UPROPERTY() int32 CorridorFinalDeadlockAgentCount = 0;
  UPROPERTY() int32 FlowContractViolationCount = 0;
  UPROPERTY() int32 FailureOwnedFlowContractViolationCount = 0;
  UPROPERTY() int32 CorridorFailureAgentCount = 0;
  UPROPERTY() int32 GoalFailureAgentCount = 0;
  UPROPERTY() float InsideGoalCountP50 = 0.0f;
  UPROPERTY() float InsideGoalCountP95 = 0.0f;
  UPROPERTY() float InsideGoalCountMax = 0.0f;
  UPROPERTY() float NeverReachedDistanceCmP50 = 0.0f;
  UPROPERTY() float NeverReachedDistanceCmP95 = 0.0f;
  UPROPERTY() float NeverReachedDistanceCmMax = 0.0f;
  UPROPERTY() float NeverReachedDesiredForwardCmpsP50 = 0.0f;
  UPROPERTY() float NeverReachedDesiredForwardCmpsP95 = 0.0f;
  UPROPERTY() float NeverReachedAppliedForwardCmpsP50 = 0.0f;
  UPROPERTY() float NeverReachedAppliedForwardCmpsP95 = 0.0f;
  UPROPERTY() float NeverReachedSoftOppositionCmpsP50 = 0.0f;
  UPROPERTY() float NeverReachedSoftOppositionCmpsP95 = 0.0f;
  UPROPERTY() float BaselineNeverReachedForwardCmps = 0.0f;
  UPROPERTY() float StickyNeverReachedForwardCmps = 0.0f;
  UPROPERTY() float SoftDisabledNeverReachedForwardCmps = 0.0f;
  UPROPERTY() uint8 bStickyCounterfactualValid = 0;
  UPROPERTY() uint8 bSoftDisabledCounterfactualValid = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoCapabilityProfileMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint32 CapabilityProfileKey = 0;
  UPROPERTY() int32 DemandRegionPhaseOffset = 0;
  UPROPERTY() int32 AgentCount = 0;
  UPROPERTY() int32 FeasibleRegionCount = 0;
  UPROPERTY() int32 FeasibleRegionCoverageCount = 0;
  UPROPERTY() int32 InsideBandCount = 0;
  UPROPERTY() int32 DistanceBandInsideCount = 0;
  UPROPERTY() int32 BelowBandCount = 0;
  UPROPERTY() int32 AboveBandCount = 0;
  UPROPERTY() float OutsideBandErrorCmMax = 0.0f;
  UPROPERTY() float OutsideBandProgressCmpsMin = 0.0f;
  UPROPERTY() float OutsideBandProgressCmpsMax = 0.0f;
  UPROPERTY() int32 RoutedAgentCount = 0;
  UPROPERTY() int32 UnroutedAgentCount = 0;
  UPROPERTY() int32 MaximumRegionPopulation = 0;
  UPROPERTY() uint32 TopologyHash = 0;
  UPROPERTY() uint32 DemandHash = 0;
  UPROPERTY() uint32 TransportHash = 0;
  UPROPERTY() uint32 GuidanceHash = 0;
  UPROPERTY() uint32 ValidationHash = 0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoParticleMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint32 RoundInputHash = 0;
  UPROPERTY() uint32 RoundInitialStateHash = 0;
  UPROPERTY() int32 RoundResetCount = 0;
  UPROPERTY() int32 RoundTransitionOrderViolationCount = 0;
  UPROPERTY() uint32 DynamicFlowTopologyHash = 0;
  UPROPERTY() int32 DynamicFlowAnchorCellKey = INDEX_NONE;
  UPROPERTY() uint32 DynamicFlowIntegrationHash = 0;
  UPROPERTY() int32 DynamicFlowIntegrationRebuildCount = 0;
  UPROPERTY() uint32 DynamicFlowRoundHash = 0;

  UPROPERTY() uint8 bLocalPredictiveValid = 0;
  UPROPERTY() uint32 LocalPredictiveHash = 0;
  UPROPERTY() int32 LocalPredictiveSampleCount = 0;
  UPROPERTY() int32 LocalPredictiveProcessedAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveCandidatePairCount = 0;
  UPROPERTY() int32 LocalPredictiveConflictPairCount = 0;
  UPROPERTY() int32 LocalPredictiveComponentCount = 0;
  UPROPERTY() int32 LocalPredictiveMaxComponentSize = 0;
  UPROPERTY() int32 LocalPredictiveAdjustedAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveGrantedAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveYieldingAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveInfeasibleAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveQuantizationFailureCount = 0;
  UPROPERTY() uint8 bLocalPredictiveComponentFixtureValid = 0;
  UPROPERTY() uint32 LocalPredictiveComponentFixtureHash = 0;
  UPROPERTY() int32 LocalPredictiveComponentFixtureAgentCount = 0;
  UPROPERTY() int32 LocalPredictiveComponentFixtureWitnessCount = 0;
  UPROPERTY() int32 LocalPredictiveFullJointSafeComponentCount = 0;
  UPROPERTY() int32 LocalPredictiveJointValidationFailureCount = 0;
  UPROPERTY() int32 LocalPredictiveJointComponentResolutionCount = 0;
  UPROPERTY() int32 LocalPredictiveCoherentTranslationComponentCount = 0;
  UPROPERTY() int32 LocalPredictiveCoherentTranslationAgentCount = 0;
  UPROPERTY() float LocalPredictiveCoherentTranslationMaxCmps = 0.0f;
  UPROPERTY() int32 LocalPredictiveJointPreferredRecoveryComponentCount = 0;
  UPROPERTY() int32 LocalPredictiveJointPreferredRecoveryAgentCount = 0;
  UPROPERTY() float LocalPredictiveJointPreferredRecoveryMaxGainCmps = 0.0f;
  UPROPERTY() int32 LocalPredictiveEnvironmentConstraintCount = 0;
  UPROPERTY() int32 LocalPredictiveGrantSwitchCount = 0;
  UPROPERTY() int32 LocalPredictiveBlockedAgeMax = 0;
  UPROPERTY() int32 LocalPredictiveInvalidStepCount = 0;

  UPROPERTY() uint8 bTargetApproachValid = 0;
  UPROPERTY() uint32 TargetFactHash = 0;
  UPROPERTY() uint32 TargetApproachHash = 0;
  UPROPERTY() uint32 TargetAgentInputHash = 0;
  UPROPERTY() uint32 TargetAgentFineKinematicHash = 0;
  UPROPERTY() uint32 TargetAgentConfigHash = 0;
  UPROPERTY() uint32 TargetAgentTemporalHash = 0;
  UPROPERTY() uint32 TargetSettingsHash = 0;
  UPROPERTY() uint32 TargetSlotInputHash = 0;
  UPROPERTY() uint32 TargetFullInputHash = 0;
  UPROPERTY() uint32 TargetOwnerStateHash = 0;
  UPROPERTY() uint32 TargetTransitionHash = 0;
  UPROPERTY() uint32 TargetGuidanceHash = 0;
  UPROPERTY() uint32 TargetGuidanceLocationHash = 0;
  UPROPERTY() uint32 TargetGuidanceVelocityHash = 0;
  UPROPERTY() int32 TargetSlotLayoutRevision = INDEX_NONE;
  UPROPERTY() uint32 TargetSlotLayoutTopologyHash = 0;
  UPROPERTY() uint32 TargetSlotLayoutWorldHash = 0;
  UPROPERTY() uint32 TargetSlotLayoutFullInputHash = 0;
  UPROPERTY() int32 SlotLayoutCandidateCount = 0;
  UPROPERTY() int32 SlotLayoutFunctionalCount = 0;
  UPROPERTY() int32 SlotLayoutFillCount = 0;
  UPROPERTY() int32 SlotRejectedTargetClearanceCount = 0;
  UPROPERTY() int32 SlotRejectedPairSpacingCount = 0;
  UPROPERTY() int32 SlotRejectedObstacleCount = 0;
  UPROPERTY() int32 SlotRejectedBoundsCount = 0;
  UPROPERTY() int32 SlotRejectedUnreachableCount = 0;
  UPROPERTY() int32 SlotRejectedIngressSegmentCount = 0;
  UPROPERTY() uint32 TargetApproachScheduleHash = 0;
  UPROPERTY() uint32 TargetApproachCommitHash = 0;
  UPROPERTY() int32 SlotOwnerReleaseCount = 0;
  UPROPERTY() int32 SlotOwnerReusedCount = 0;
  UPROPERTY() int32 SlotOwnerConflictCount = 0;
  UPROPERTY() int32 SlotLayoutRevisionMismatchCount = 0;
  UPROPERTY() int32 RingEnteredCount = 0;
  UPROPERTY() int32 RingWaitingCount = 0;
  UPROPERTY() int32 FunctionalSlotCapacity = 0;
  UPROPERTY() int32 FunctionalSlotOccupied = 0;
  UPROPERTY() int32 FillSlotCapacity = 0;
  UPROPERTY() int32 FillSlotOccupied = 0;
  UPROPERTY() int32 SlotIngressCount = 0;
  UPROPERTY() int32 SlotOccupiedCount = 0;
  UPROPERTY() int32 FreeSettleCount = 0;
  UPROPERTY() int32 FreeSettledCount = 0;
  UPROPERTY() int32 DuplicateSlotOwnerCount = 0;
  UPROPERTY() int32 InvalidSlotOwnerCount = 0;
  UPROPERTY() int32 TargetApproachStateTransitionCount = 0;
  UPROPERTY() int32 TargetApproachPhaseOscillationCount = 0;

  UPROPERTY() uint8 bTargetInfluenceValid = 0;
  UPROPERTY() int32 TargetInfluenceAgentCount = 0;
  UPROPERTY() int32 TargetInsideEffectiveBandCount = 0;
  UPROPERTY() int32 TargetOutsideMaxCount = 0;
  UPROPERTY() int32 TargetInsideMinCount = 0;
  UPROPERTY() float TargetRadialErrorCmP50 = 0.0f;
  UPROPERTY() float TargetRadialErrorCmP95 = 0.0f;
  UPROPERTY() float TargetRadialErrorCmMax = 0.0f;
  UPROPERTY() float TargetRelativeSpeedCmpsP95 = 0.0f;
  UPROPERTY() float TargetFollowLagCmP95 = 0.0f;
  UPROPERTY() int32 OccupiedAngularSectorCount = 0;
  UPROPERTY() int32 AngularCoverageQ15 = 0;
  UPROPERTY() int32 MaxAngularSectorPopulation = 0;
  UPROPERTY() int32 OccupiedRadialBandCount = 0;
  UPROPERTY() uint32 TargetDensityFieldHash = 0;
  UPROPERTY() int32 TargetDensityContributingAgentCount = 0;
  UPROPERTY() int32 TargetDensityOccupiedCellCount = 0;
  UPROPERTY() int32 TargetDensityMaxCellPopulation = 0;
  UPROPERTY() int32 TargetDensityGuidedAgentCount = 0;
  UPROPERTY() int32 TargetDensityClockwiseAgentCount = 0;
  UPROPERTY() int32 TargetDensityCounterClockwiseAgentCount = 0;
  UPROPERTY() float TargetDensityTangentialSpeedCmpsP95 = 0.0f;
  UPROPERTY() float TargetDensityTangentialSpeedCmpsMax = 0.0f;
  UPROPERTY() int32 TargetLargestEmptySectorRun = 0;
  UPROPERTY() uint32 TargetInfluenceHash = 0;
  UPROPERTY() uint8 bTargetInfluenceExecutionDiagnosticValid = 0;
  UPROPERTY() int32 TargetInfluenceExecutionValidSampleCount = 0;
  UPROPERTY() int32 TargetInfluenceExecutionRequestedAgentCount = 0;
  UPROPERTY() int32 TargetInfluenceExecutionBelowThresholdSampleCount = 0;
  UPROPERTY() float TargetDensityRequestedTangentialCmpsP50 = 0.0f;
  UPROPERTY() float TargetDensityRequestedTangentialCmpsP95 = 0.0f;
  UPROPERTY() float TargetDensityRequestedTangentialCmpsMax = 0.0f;
  UPROPERTY() float TargetDensityPredictTangentialCmpsP50 = 0.0f;
  UPROPERTY() float TargetDensityPredictTangentialCmpsP95 = 0.0f;
  UPROPERTY() float TargetDensityPredictTangentialCmpsMax = 0.0f;
  UPROPERTY() float TargetDensityAppliedTangentialCmpsP50 = 0.0f;
  UPROPERTY() float TargetDensityAppliedTangentialCmpsP95 = 0.0f;
  UPROPERTY() float TargetDensityAppliedTangentialCmpsMax = 0.0f;
  UPROPERTY() float TargetDensityRequestedToAppliedRatioP50 = 0.0f;
  UPROPERTY() float TargetDensityRequestedToAppliedRatioP95 = 0.0f;
  UPROPERTY() float TargetDensityLostTangentialCmpsP50 = 0.0f;
  UPROPERTY() float TargetDensityLostTangentialCmpsP95 = 0.0f;
  UPROPERTY() float TargetDensityLostTangentialCmpsMax = 0.0f;
  UPROPERTY() int32 TargetDensityDirectionFlipAgentCount = 0;
  UPROPERTY() int32 TargetDensityDirectionFlipCount = 0;
  UPROPERTY() int32 TargetDensityAngularSectorTransitionCount = 0;
  UPROPERTY() int32 TargetDensityRadialBandTransitionCount = 0;
  UPROPERTY() int32 TargetDensityEnvironmentOpposedAgentCount = 0;
  UPROPERTY() int32 TargetDensityParticleOpposedAgentCount = 0;
  UPROPERTY() TArray<int32> TargetFeasibleSectorCountByRadialBand;
  UPROPERTY() int32 TargetOccupiedFeasibleSectorCount = 0;
  UPROPERTY() int32 TargetOccupiedInfeasiblePolarCellCount = 0;
  UPROPERTY() int32 TargetFeasibleButUnoccupiedSectorCount = 0;
  UPROPERTY() int32 TargetLargestEmptyFeasibleSectorRun = 0;
  UPROPERTY() int32 TargetFlowBoundsInfeasibleCellCount = 0;
  UPROPERTY() int32 TargetObstacleInfeasibleCellCount = 0;
  UPROPERTY() uint32 TargetInfluenceExecutionDiagnosticHash = 0;

  UPROPERTY() uint8 bTargetStabilityDiagnosticValid = 0;
  UPROPERTY() int32 TargetStabilityPrimaryCause = 0;
  UPROPERTY() uint32 TargetStabilityDiagnosticHash = 0;
  UPROPERTY() int32 TargetStabilitySampleStepCount = 0;
  UPROPERTY() int32 TargetStabilityWindowStepCount = 0;
  UPROPERTY() int32 TargetStabilityAgentCount = 0;
  UPROPERTY() int32 TargetStabilityInsideBandMin = 0;
  UPROPERTY() int32 TargetStabilityCoverageMin = 0;
  UPROPERTY() int32 TargetStabilityCoverageRequired = 0;
  UPROPERTY() int32 TargetStabilityContendedStepCount = 0;
  UPROPERTY() int32 TargetStabilityContendedGroupCount = 0;
  UPROPERTY() int32 TargetStabilityMergeBlockedAgentCount = 0;
  UPROPERTY() int32 TargetStabilityMergeBlockedMaxSteps = 0;
  UPROPERTY() int32 TargetStabilityTerminalChatterAgentCount = 0;
  UPROPERTY() int32 TargetStabilityTerminalChatterCount = 0;
  UPROPERTY() int32 TargetStabilityAttractionRejectionCycleCount = 0;
  UPROPERTY() int32 TargetStabilityParticleSettledWindowCount = 0;
  UPROPERTY() int32 TargetStabilityParticleSettledMaxSteps = 0;
  UPROPERTY() float TargetStabilityTargetRelativeSpeedCmpsP95 = 0.0f;
  UPROPERTY() float TargetStabilityTargetRelativeSpeedCmpsMax = 0.0f;
  UPROPERTY() float TargetStabilityPositionPeakToPeakCmP95 = 0.0f;
  UPROPERTY() float TargetStabilityPositionPeakToPeakCmMax = 0.0f;
  UPROPERTY() int32 TargetStabilityFirstWitnessStep = INDEX_NONE;
  UPROPERTY() int32 TargetStabilityFirstWitnessAgentId = INDEX_NONE;
  UPROPERTY() int32 TargetStabilityFirstWitnessNextCellKey = INDEX_NONE;
  UPROPERTY() int32 TargetStabilityFinalMissingRegionCount = 0;
  UPROPERTY() uint32 TargetStabilityFirstMissingCohortKey = 0;
  UPROPERTY() int32 TargetStabilityFirstMissingRegionKey = INDEX_NONE;
  UPROPERTY() int32 TargetStabilityFirstMissingRegionStage = 0;
  UPROPERTY() int32 TargetStabilityRegionDemandGapStepCount = 0;
  UPROPERTY() int32 TargetStabilityRegionPlanQuotaGapStepCount = 0;
  UPROPERTY() int32 TargetStabilityRegionGuidanceGapStepCount = 0;
  UPROPERTY() int32 TargetStabilityRegionTerminalRetentionGapStepCount = 0;
  UPROPERTY() int32 TargetStabilityRegionTerminalEnterCount = 0;
  UPROPERTY() int32 TargetStabilityRegionTerminalExitCount = 0;
  UPROPERTY() int32 TargetStabilityFinalSubQuantumSupplyAgentCount = 0;
  UPROPERTY() int32 TargetStabilityFirstSubQuantumSupplyAgentId = INDEX_NONE;
  UPROPERTY() float TargetStabilityMinimumExecutableSpeedCmps = 0.0f;

  UPROPERTY() uint8 bTargetRegionTransportValid = 0;
  UPROPERTY() int32 TargetTransportFeasibleCellCount = 0;
  UPROPERTY() int32 TargetTransportEdgeCount = 0;
  UPROPERTY() int32 TargetTransportFeasibleRegionCount = 0;
  UPROPERTY() int32 TargetTransportRawRegionCoverageCount = 0;
  UPROPERTY() int32 TargetTransportFeasibleRegionCoverageCount = 0;
  UPROPERTY() int32 TargetTransportInsideEffectiveBandCount = 0;
  UPROPERTY() int32 TargetTransportMaximumRegionPopulation = 0;
  UPROPERTY() int32 TargetTransportDesiredPopulation = 0;
  UPROPERTY() int32 TargetTransportRoutedAgentCount = 0;
  UPROPERTY() int32 TargetTransportUnroutedAgentCount = 0;
  UPROPERTY() int32 TargetGuidanceUnroutedStepCount = 0;
  UPROPERTY() int32 TargetGuidanceUnroutedAgentSampleCount = 0;
  UPROPERTY() int32 TargetGuidanceUnroutedAgentMax = 0;
  UPROPERTY() int32 TargetGuidanceFirstFailureStep = INDEX_NONE;
  UPROPERTY() int32 TargetGuidanceFirstFailureAgentId = INDEX_NONE;
  UPROPERTY() int32 TargetTransportInvalidStepCount = 0;
  UPROPERTY() int32 TargetTransportValidationFailureCount = 0;
  UPROPERTY() uint32 TargetTransportValidationHash = 0;
  UPROPERTY() uint8 bTargetTransportFailureFixtureValid = 0;
  UPROPERTY() int32 TargetTransportFailureFixtureStep = INDEX_NONE;
  UPROPERTY() int32 TargetTransportFailureFixtureKind = 0;
  UPROPERTY() int32 TargetTransportFailureFixtureAgentId = INDEX_NONE;
  UPROPERTY() int32 TargetTransportFailureFixtureCellKey = INDEX_NONE;
  UPROPERTY() uint32 TargetTransportFailureFixtureHash = 0;
  UPROPERTY() int64 TargetTransportTotalPhysicalCost = 0;
  UPROPERTY() int64 TargetTransportChangedQuotaUnitCount = 0;
  UPROPERTY() int32 TargetTransportPlanEpoch = 0;
  UPROPERTY() int32 TargetTransportPlanRebuildCount = 0;
  UPROPERTY() int32 TargetTransportLifetimeRebuildCount = 0;
  UPROPERTY() int32 TargetTransportTargetRebuildCount = 0;
  UPROPERTY() int32 TargetTransportEnvironmentRebuildCount = 0;
  UPROPERTY() int32 TargetTransportMembershipRebuildCount = 0;
  UPROPERTY() int32 TargetTransportDemandSatisfiedRebuildCount = 0;
  UPROPERTY() int32 TargetTransportPathInvalidRebuildCount = 0;
  UPROPERTY() float TargetTransportSolverMsP95 = -1.0f;
  UPROPERTY() uint32 TargetTransportTopologyHash = 0;
  UPROPERTY() uint32 TargetTransportDemandHash = 0;
  UPROPERTY() uint32 TargetTransportPlanHash = 0;
  UPROPERTY() uint32 TargetTransportGuidanceHash = 0;

  UPROPERTY() uint8 bCapabilityProfilesValid = 0;
  UPROPERTY() int32 CapabilityProfileCount = 0;
  UPROPERTY() uint32 CapabilityMembershipHash = 0;
  UPROPERTY() int32 CapabilityCohortRebuildCount = 0;
  UPROPERTY() int32 CrossProfileHardViolationCount = 0;
  UPROPERTY() int32 CrossProfileSweptViolationCount = 0;
  UPROPERTY() TArray<FCrowdDemoCapabilityProfileMetrics> CapabilityProfiles;

  UPROPERTY() int32 SoftPairCount = 0;
  UPROPERTY() int32 SoftViolatingPairCount = 0;
  UPROPERTY() float SoftErrorCmP50 = 0.0f;
  UPROPERTY() float SoftErrorCmP95 = 0.0f;
  UPROPERTY() float SoftErrorCmMax = 0.0f;
  UPROPERTY() int32 HardPairViolationCount = 0;
  UPROPERTY() int32 SweptPairViolationCount = 0;
  UPROPERTY() int32 PressureInfluencedAgentCount = 0;
  UPROPERTY() int32 FirstInfluencedIterationMax = 0;
  UPROPERTY() int32 ParticleCorrectedAgentCount = 0;
  UPROPERTY() float MaxAgentCorrectionCm = 0.0f;
  UPROPERTY() int32 SettlingSteps = INDEX_NONE;
  UPROPERTY() int32 ObstaclePenetrationCount = 0;
  UPROPERTY() float ParticleSolverMsP95 = -1.0f;
  UPROPERTY() int32 BoundsViolationCount = 0;
  UPROPERTY() int32 EnvironmentSoftContactCount = 0;
  UPROPERTY() int32 EnvironmentSoftAppliedAgentCount = 0;
  UPROPERTY() float EnvironmentSoftErrorCmP50 = 0.0f;
  UPROPERTY() float EnvironmentSoftErrorCmP95 = 0.0f;
  UPROPERTY() float EnvironmentSoftErrorCmMax = 0.0f;
  UPROPERTY() float EnvironmentSoftRequestedCorrectionCmMax = 0.0f;
  UPROPERTY() float EnvironmentSoftRealizedCorrectionCmMax = 0.0f;
  UPROPERTY() int32 UnifiedHardConstraintCount = 0;
  UPROPERTY() float UnifiedHardResidualCmMax = 0.0f;
  UPROPERTY() int32 UnifiedHardInfeasibleCount = 0;
  UPROPERTY() int32 ParticleInvalidStepCount = 0;
  UPROPERTY() int32 ParticleGlobalFallbackStepCount = 0;
  UPROPERTY() uint32 ParticleCandidateHash = 0;
  UPROPERTY() uint32 ParticleAppliedStateHash = 0;
  UPROPERTY() int32 ParticleFailureFixtureStep = INDEX_NONE;
  UPROPERTY() int32 ParticleFailureMinAgentId = INDEX_NONE;
  UPROPERTY() int32 ParticleFailureMaxAgentId = INDEX_NONE;
  UPROPERTY() uint32 ParticleFailureFixtureHash = 0;
  UPROPERTY() int32 RollbackSnapshotHitCount = 0;
  UPROPERTY() int32 RollbackSnapshotMissCount = 0;
  UPROPERTY() int32 RollbackAgentMismatchCount = 0;
  UPROPERTY() int32 RollbackReplayedStepCount = 0;
  UPROPERTY() uint8 bT1Valid = 0;
  UPROPERTY() uint8 T1Phase = 0;
  UPROPERTY() int32 T1PhaseTransitionCount = 0;
  UPROPERTY() int32 T1ActiveAgentCount = 0;
  UPROPERTY() int32 T1BatchActivationCount = 0;
  UPROPERTY() int32 T1InsertedAgentId = INDEX_NONE;
  UPROPERTY() int32 T1RemovedAgentId = INDEX_NONE;
  UPROPERTY() int32 T1PressurePropagationLayerMax = INDEX_NONE;
  UPROPERTY() int32 T1InfluencedAgentCount = 0;
  UPROPERTY() TArray<int32> T1LayerAgentCounts;
  UPROPERTY() TArray<int32> T1ActiveCountTransitions;
  UPROPERTY() int32 T1InsertSettlingStep = INDEX_NONE;
  UPROPERTY() int32 T1PostRemovalSettlingStep = INDEX_NONE;
  UPROPERTY() int32 T1OldLayoutReturnedAgentCount = 0;
  UPROPERTY() int32 T1NewEquilibriumDisplacedAgentCount = 0;
  UPROPERTY() int32 T1ExternalPreferredNonzeroCount = 0;
  UPROPERTY() uint32 T1ParticipationHash = 0;
  UPROPERTY() uint32 T1PropagationHash = 0;
  UPROPERTY() uint32 T1PhaseHash = 0;
  UPROPERTY() uint8 bT2Valid = 0;
  UPROPERTY() uint32 T2LayoutHash = 0;
  UPROPERTY() uint32 T2RouteDiagnosticHash = 0;
  UPROPERTY() uint32 T2ProgressHash = 0;
  UPROPERTY() float T2AppliedForwardCmpsP50 = 0.0f;
  UPROPERTY() float T2AppliedForwardCmpsP95 = 0.0f;
  UPROPERTY() int32 T2FlowContractViolationCount = 0;
  UPROPERTY() int32 T2FinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T2FlowApproachEnteredCount = 0;
  UPROPERTY() int32 T2TransportHandoffCount = 0;
  UPROPERTY() int32 T2InsideEffectiveBandCount = 0;
  UPROPERTY() int32 T2FeasibleRegionCount = 0;
  UPROPERTY() int32 T2FeasibleRegionCoverageCount = 0;
  UPROPERTY() int32 T2PlanUnroutedCount = 0;
  UPROPERTY() int32 T2GuidanceUnroutedCount = 0;
  UPROPERTY() int32 T2TransportValidationFailureCount = 0;
  UPROPERTY() int32 T2TerminalSettledCount = 0;
  UPROPERTY() int32 T2TerminalSettledStep = INDEX_NONE;
  UPROPERTY() uint8 bT3Valid = 0;
  UPROPERTY() uint32 T3LayoutHash = 0;
  UPROPERTY() uint32 T3Cohort0FlowHash = 0;
  UPROPERTY() uint32 T3Cohort1FlowHash = 0;
  UPROPERTY() uint32 T3ProgressHash = 0;
  UPROPERTY() int32 T3Cohort0AgentCount = 0;
  UPROPERTY() int32 T3Cohort1AgentCount = 0;
  UPROPERTY() int32 T3Cohort0CenterCrossedCount = 0;
  UPROPERTY() int32 T3Cohort1CenterCrossedCount = 0;
  UPROPERTY() int32 T3Cohort0CompletedCount = 0;
  UPROPERTY() int32 T3Cohort1CompletedCount = 0;
  UPROPERTY() int32 T3CompletedCount = 0;
  UPROPERTY() int32 T3ThroughputDifference = 0;
  UPROPERTY() int32 T3FinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T3UnreachableSampleCount = 0;
  UPROPERTY() int32 T3LastFixedStep = INDEX_NONE;
  UPROPERTY() int32 T3CompletionStepMax = INDEX_NONE;
  UPROPERTY() uint8 bT4Valid = 0;
  UPROPERTY() uint32 T4LayoutHash = 0;
  UPROPERTY() uint32 T4FlowHash = 0;
  UPROPERTY() uint32 T4ProgressHash = 0;
  UPROPERTY() int32 T4WallPassedCount = 0;
  UPROPERTY() int32 T4CorridorExitedCount = 0;
  UPROPERTY() int32 T4CompletedCount = 0;
  UPROPERTY() int32 T4FinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T4UnreachableSampleCount = 0;
  UPROPERTY() int32 T4LastFixedStep = INDEX_NONE;
  UPROPERTY() int32 T4CompletionStepMax = INDEX_NONE;
  UPROPERTY() uint8 bT6TransitValid = 0;
  UPROPERTY() uint32 T6TransitLayoutHash = 0;
  UPROPERTY() uint32 T6TransitFlowHash = 0;
  UPROPERTY() uint32 T6TransitProgressHash = 0;
  UPROPERTY() int32 T6TransitWallPassedCount = 0;
  UPROPERTY() int32 T6TransitCorridorExitedCount = 0;
  UPROPERTY() int32 T6TransitCompletedCount = 0;
  UPROPERTY() int32 T6TransitFinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T6TransitUnreachableSampleCount = 0;
  UPROPERTY() int32 T6TransitLastFixedStep = INDEX_NONE;
  UPROPERTY() int32 T6TransitCompletionStepMax = INDEX_NONE;
  UPROPERTY() FCrowdDemoSoftPressureRouteMetrics RouteMetrics;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundResultPacket
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 Revision = 0;

  UPROPERTY()
  int32 CheckpointRevision = 0;

  UPROPERTY()
  int32 StateFrameRevision = 0;

  UPROPERTY()
  float EndServerTimeSeconds = 0.0f;

  UPROPERTY()
  TArray<FCrowdDemoRoundAgentState> Agents;

  UPROPERTY()
  int32 OverlapPairCount = 0;

  UPROPERTY()
  int32 InitialOverlapPairCount = 0;

  UPROPERTY()
  int32 SevereOverlapPairCount = 0;

  UPROPERTY()
  int32 InitialSevereOverlapPairCount = 0;

  UPROPERTY()
  int32 SeparationAppliedAgentCount = 0;

  UPROPERTY()
  int32 SeparationGridCellCount = 0;

  UPROPERTY()
  int32 ObstaclePenetrationCount = 0;

  UPROPERTY()
  int32 ArrivalCount = 0;

  UPROPERTY()
  int32 PbdCorrectedAgentCount = 0;

  UPROPERTY()
  int32 PbdCorrectedPairCount = 0;

  UPROPERTY()
  float PbdMaxPairCorrectionCm = 0.0f;

  UPROPERTY()
  float PbdMaxAgentTotalCorrectionCm = 0.0f;

  UPROPERTY()
  float PbdMaxObstacleReprojectDeltaCm = 0.0f;

  UPROPERTY()
  float PbdMaxFinalSafetyDeltaCm = 0.0f;

  UPROPERTY()
  float PbdSolverMsP95 = -1.0f;

  UPROPERTY()
  FCrowdDemoTrafficMetrics TrafficMetrics;

  UPROPERTY()
  FCrowdDemoParticleMetrics ParticleMetrics;

  UPROPERTY()
  FCrowdDemoProjectileMetrics ProjectileMetrics;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundResultHeader
{
  GENERATED_BODY()

  UPROPERTY() uint8 bValid = 0;
  UPROPERTY() int32 RoundId = 0;
  UPROPERTY() int32 Revision = 0;
  UPROPERTY() int32 CheckpointRevision = 0;
  UPROPERTY() int32 StateFrameRevision = 0;
  UPROPERTY() float EndServerTimeSeconds = 0.0f;
  UPROPERTY() int32 AgentCount = 0;
  UPROPERTY() int32 OverlapPairCount = 0;
  UPROPERTY() int32 InitialOverlapPairCount = 0;
  UPROPERTY() int32 SevereOverlapPairCount = 0;
  UPROPERTY() int32 InitialSevereOverlapPairCount = 0;
  UPROPERTY() int32 SeparationAppliedAgentCount = 0;
  UPROPERTY() int32 SeparationGridCellCount = 0;
  UPROPERTY() int32 ObstaclePenetrationCount = 0;
  UPROPERTY() int32 ArrivalCount = 0;
  UPROPERTY() int32 PbdCorrectedAgentCount = 0;
  UPROPERTY() int32 PbdCorrectedPairCount = 0;
  UPROPERTY() float PbdMaxPairCorrectionCm = 0.0f;
  UPROPERTY() float PbdMaxAgentTotalCorrectionCm = 0.0f;
  UPROPERTY() float PbdMaxObstacleReprojectDeltaCm = 0.0f;
  UPROPERTY() float PbdMaxFinalSafetyDeltaCm = 0.0f;
  UPROPERTY() float PbdSolverMsP95 = -1.0f;
  UPROPERTY() FCrowdDemoTrafficMetrics TrafficMetrics;
  UPROPERTY() FCrowdDemoParticleMetrics ParticleMetrics;
  UPROPERTY() FCrowdDemoProjectileMetrics ProjectileMetrics;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundCompareMetrics
{
  GENERATED_BODY()

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 Revision = 0;

  UPROPERTY()
  int32 CheckpointRevision = 0;

  UPROPERTY()
  int32 CurrentRoundId = 0;

  UPROPERTY()
  int32 CompletedRoundCount = 0;

  UPROPERTY()
  int32 CorrectionAppliedCount = 0;

  UPROPERTY()
  float SimPositionErrorCmP50 = -1.0f;

  UPROPERTY()
  float SimPositionErrorCmP95 = -1.0f;

  UPROPERTY()
  float SimPositionErrorCmMax = -1.0f;

  UPROPERTY()
  float CorrectionIntervalPositionErrorCmP95 = -1.0f;

  UPROPERTY()
  float CorrectionIntervalPositionErrorCmMax = -1.0f;

  UPROPERTY()
  float CrossRoundPositionErrorCmP95Max = -1.0f;

  UPROPERTY()
  float CrossRoundPositionErrorGrowthCm = 0.0f;

  UPROPERTY()
  float CrossRoundCorrectionIntervalErrorCmP95Max = -1.0f;

  UPROPERTY()
  float CrossRoundCorrectionIntervalErrorGrowthCm = 0.0f;

  UPROPERTY()
  float SimYawErrorDegP95 = -1.0f;

  UPROPERTY()
  float SimVelocityErrorCmpsP95 = -1.0f;

  UPROPERTY()
  int32 SimOverlapPairDelta = 0;

  UPROPERTY()
  int32 CorrectionEntitiesCount = 0;

  UPROPERTY()
  float CorrectionMaxCm = 0.0f;

  UPROPERTY()
  float RoundBoundaryCenterJumpCmP95 = -1.0f;

  UPROPERTY()
  float RoundBoundaryYawJumpDegP95 = -1.0f;

  UPROPERTY()
  float RoundBoundaryVelocityJumpCmpsP95 = -1.0f;

  UPROPERTY()
  int32 RoundPlanRevisionSeen = 0;

  UPROPERTY()
  int32 RoundPlanAppliedCount = 0;

  UPROPERTY()
  int32 RoundPlanGapCount = 0;

  UPROPERTY()
  int32 RoundPlanLateCount = 0;

  UPROPERTY()
  int32 RoundBootstrapAgentCount = 0;

  UPROPERTY()
  int32 SyntheticSkippedCheckpointCount = 0;

  UPROPERTY()
  int32 InitialOverlapPairCount = 0;

  UPROPERTY()
  float OverlapPairCountP50 = -1.0f;

  UPROPERTY()
  float OverlapPairCountP95 = -1.0f;

  UPROPERTY()
  int32 OverlapPairCountMax = 0;

  UPROPERTY()
  float SevereOverlapPairCountP50 = -1.0f;

  UPROPERTY()
  float SevereOverlapPairCountP95 = -1.0f;

  UPROPERTY()
  int32 SevereOverlapPairCountMax = 0;

  UPROPERTY()
  int32 FlowFieldRevision = 0;

  UPROPERTY()
  uint32 FlowFieldBuildHash = 0;

  UPROPERTY()
  int32 FlowFieldRebuildCount = 0;

  UPROPERTY()
  int32 FlowBlockedCellCount = 0;

  UPROPERTY()
  int32 FlowUnreachableAgentCount = 0;

  UPROPERTY()
  int32 FlowGoalReachedCount = 0;

  UPROPERTY()
  int32 FlowWallPassCount = 0;

  UPROPERTY()
  int32 FlowCorridorExitCount = 0;

  UPROPERTY()
  int32 FlowTurnExitCount = 0;

  UPROPERTY()
  int32 ServerObstaclePenetrationCount = 0;

  UPROPERTY()
  int32 ClientSimObstaclePenetrationCount = 0;

  UPROPERTY()
  int32 SoftSeparationAppliedAgentCount = 0;

  UPROPERTY()
  int32 PbdCorrectedAgentCount = 0;

  UPROPERTY()
  int32 PbdCorrectedPairCount = 0;

  UPROPERTY()
  float PbdMaxPairCorrectionCm = 0.0f;

  UPROPERTY()
  float PbdMaxAgentTotalCorrectionCm = 0.0f;

  UPROPERTY()
  float PbdMaxObstacleReprojectDeltaCm = 0.0f;

  UPROPERTY()
  float PbdMaxFinalSafetyDeltaCm = 0.0f;

  UPROPERTY()
  float PbdSolverMsP95 = -1.0f;

  UPROPERTY()
  FCrowdDemoTrafficMetrics TrafficMetrics;
  FCrowdDemoParticleMetrics ParticleMetrics;
  int32 ServerClientParticleHashMatch = 0;

  UPROPERTY()
  int32 CorridorDeadlockAgentCount = 0;
};

struct FCrowdDemoSummaryMetrics
{
  int32 Agents = 0;
  int32 VisibleInstances = 0;
  float ServerFrameMsP95 = -1.0f;
  float CrowdSolverMsP95 = -1.0f;
  float ReplicationSampleAgeMsP95 = -1.0f;
  float DisplayToAuthoritativeCmP95 = -1.0f;
  int32 SimCurrentRoundId = 0;
  int32 SimCompletedRoundCount = 0;
  int32 SimCorrectionAppliedCount = 0;
  int32 SimCheckpointRevision = 0;
  int32 CorrectionFrameRevision = 0;
  int32 CorrectionFrameAppliedCount = 0;
  int32 CorrectionFrameHeaderReceivedCount = 0;
  int32 CorrectionFrameChunkReceivedCount = 0;
  int32 LatestChunkRevisionSeen = 0;
  int32 CorrectionChunkReceivedCount = 0;
  int32 CorrectionUniqueChunkCount = 0;
  int32 CorrectionExpectedChunkCount = 0;
  int32 CorrectionAssemblyCompleteCount = 0;
  int32 CorrectionAssemblySupersededCount = 0;
  int32 CorrectionFrameCompleteCount = 0;
  int32 CorrectionFramePublishedCount = 0;
  int32 CorrectionFrameReceivedCount = 0;
  int32 CorrectionFrameDroppedOldCount = 0;
  int32 CorrectionFrameDroppedMismatchCount = 0;
  int32 CorrectionFrameFuturePendingCount = 0;
  int32 CorrectionFrameFutureDropCount = 0;
  int32 CorrectionFrameIncompleteDropCount = 0;
  int32 CorrectionFrameStaleDropCount = 0;
  int32 CorrectionFrameReplayToNowCount = 0;
  int32 CorrectionFrameLatestRevisionSeen = 0;
  int32 CorrectionFrameLatestRevisionApplied = 0;
  int32 CorrectionFrameRevisionGapCount = 0;
  int32 CorrectionFrameChunksPerFrame = 0;
  int32 CorrectionFrameChunkSize = 0;
  int32 CorrectionAgentCount = 0;
  float CorrectionPositionErrorCmP50 = -1.0f;
  float CorrectionPositionErrorCmP95 = -1.0f;
  float CorrectionPositionErrorCmMax = -1.0f;
  float CorrectionYawErrorDegP95 = -1.0f;
  float CorrectionVelocityErrorCmpsP95 = -1.0f;
  float CorrectionIntervalMsP95 = -1.0f;
  float CorrectionFrameAssemblyMsP95 = -1.0f;
  float CorrectionFrameAgeMsP50 = -1.0f;
  float CorrectionFrameAgeMsP95 = -1.0f;
  float CorrectionFrameReplayMsP95 = -1.0f;
  int32 CorrectionErrorAgentIdMax = INDEX_NONE;
  int32 YawErrorAgentIdMax = INDEX_NONE;
  FVector CorrectionErrorVectorMean = FVector::ZeroVector;
  float CorrectionErrorVectorStdDev = -1.0f;
  float CohortCenterErrorCm = -1.0f;
  float CohortYawErrorDeg = -1.0f;
  float ResidualErrorAfterCenterAlignP95 = -1.0f;
  float ResidualErrorAfterRigidAlignP95 = -1.0f;
  float RoundTimeDeltaMs = -1.0f;
  float RoundVisualCorrectionOffsetCmP95 = -1.0f;
  float RoundVisualYawOffsetDegP95 = -1.0f;
  int32 RoundVisualSmoothingActiveCount = 0;
  float SimPositionErrorCmP50 = -1.0f;
  float SimPositionErrorCmP95 = -1.0f;
  float SimPositionErrorCmMax = -1.0f;
  float CorrectionIntervalPositionErrorCmP95 = -1.0f;
  float CorrectionIntervalPositionErrorCmMax = -1.0f;
  float CrossRoundPositionErrorCmP95Max = -1.0f;
  float CrossRoundPositionErrorGrowthCm = 0.0f;
  float CrossRoundCorrectionIntervalErrorCmP95Max = -1.0f;
  float CrossRoundCorrectionIntervalErrorGrowthCm = 0.0f;
  float SimYawErrorDegP95 = -1.0f;
  float SimVelocityErrorCmpsP95 = -1.0f;
  int32 SimOverlapPairDelta = 0;
  int32 SimCorrectionEntitiesCount = 0;
  float SimCorrectionMaxCm = 0.0f;
  float RoundBoundaryCenterJumpCmP95 = -1.0f;
  float RoundBoundaryYawJumpDegP95 = -1.0f;
  float RoundBoundaryVelocityJumpCmpsP95 = -1.0f;
  int32 RoundPlanRevisionSeen = 0;
  int32 RoundPlanAppliedCount = 0;
  int32 RoundPlanGapCount = 0;
  int32 RoundPlanLateCount = 0;
  int32 RoundBootstrapAgentCount = 0;
  int32 SyntheticSkippedCheckpointCount = 0;
  int32 InitialOverlapPairCount = 0;
  float OverlapPairCountP50 = -1.0f;
  float OverlapPairCountP95 = -1.0f;
  int32 OverlapPairCountMax = 0;
  float SevereOverlapPairCountP50 = -1.0f;
  float SevereOverlapPairCountP95 = -1.0f;
  int32 SevereOverlapPairCountMax = 0;
  int32 FlowFieldRevision = 0;
  uint32 FlowFieldBuildHash = 0;
  int32 FlowFieldRebuildCount = 0;
  int32 FlowBlockedCellCount = 0;
  int32 FlowUnreachableAgentCount = 0;
  int32 FlowGoalReachedCount = 0;
  int32 FlowWallPassCount = 0;
  int32 FlowCorridorExitCount = 0;
  int32 FlowTurnExitCount = 0;
  int32 ServerObstaclePenetrationCount = 0;
  int32 ClientSimObstaclePenetrationCount = 0;
  int32 SoftSeparationAppliedAgentCount = 0;
  int32 PbdCorrectedAgentCount = 0;
  int32 PbdCorrectedPairCount = 0;
  float PbdMaxPairCorrectionCm = 0.0f;
  float PbdMaxAgentTotalCorrectionCm = 0.0f;
  float PbdMaxObstacleReprojectDeltaCm = 0.0f;
  float PbdMaxFinalSafetyDeltaCm = 0.0f;
  float PbdSolverMsP95 = -1.0f;

  FCrowdDemoTrafficMetrics TrafficMetrics;
  int32 CorridorDeadlockAgentCount = 0;
};
