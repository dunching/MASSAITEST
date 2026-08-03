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
  SimRoundSoftPressure = 1
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
    || Scenario == ECrowdDemoScenario::SimRoundSoftPressure;
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
  FCrowdDemoCombatNetState Combat;

};

USTRUCT(BlueprintType)
struct FCrowdDemoCrowdAggregateState
{
  GENERATED_BODY()

  UPROPERTY() int32 AgentCount = 0;
  UPROPERTY() FVector_NetQuantize10 CrowdCenter = FVector::ZeroVector;
  UPROPERTY() float CrowdYawDegrees = 0.0f;
  UPROPERTY() FVector_NetQuantize10 CrowdVelocity = FVector::ZeroVector;
  UPROPERTY() float PlanPhase = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundCheckpointFrame
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 StateFrameRevision = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 RoundRevision = 0;

  UPROPERTY()
  int32 CheckpointRevision = 0;

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
struct FCrowdDemoRoundCheckpointHeader
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 StateFrameRevision = 0;

  UPROPERTY()
  int32 RoundId = 0;

  UPROPERTY()
  int32 RoundRevision = 0;

  UPROPERTY()
  int32 CheckpointRevision = 0;

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
struct FCrowdDemoRoundCheckpointChunk
{
  GENERATED_BODY()

  UPROPERTY()
  uint8 bValid = 0;

  UPROPERTY()
  int32 StableKey = 0;

  UPROPERTY()
  int32 StateFrameRevision = 0;

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
  FCrowdDemoRoundCheckpointHeader Header;

  UPROPERTY()
  TArray<FCrowdDemoRoundAgentState> Agents;
};

struct FCrowdDemoPendingRoundCheckpointAssembly
{
  FCrowdDemoRoundCheckpointHeader Header;
  TArray<uint8> ReceivedChunks;
  TArray<FCrowdDemoRoundAgentState> AgentBuffer;
  int32 ReceivedChunkCount = 0;
  int32 ReceivedAgentCount = 0;
  double FirstReceiveWorldSeconds = -1.0;
  double LastReceiveWorldSeconds = -1.0;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundCheckpointFrameMetrics
{
  GENERATED_BODY()

  UPROPERTY()
  int32 CorrectionFrameRevision = 0;

  UPROPERTY()
  int32 CorrectionFrameAppliedCount = 0;

  UPROPERTY()
  int32 RoundCheckpointHeaderReceivedCount = 0;

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
struct FCrowdDemoSharedFlowMetrics
{
  GENERATED_BODY()

  UPROPERTY() uint32 AgentStateHash = 0;
  UPROPERTY() uint32 SharedFlowFieldBuildHash = 0;
  UPROPERTY() uint32 NavigationV2Hash = 0;
  UPROPERTY() uint32 TargetFactHash = 0;
  UPROPERTY() int32 SharedFlowConnectivityContractVersion = 0;
  UPROPERTY() int32 SharedFlowValidDirectedEdgeCount = 0;
  UPROPERTY() int32 NavigationDirectedEdgeCount = 0;
  UPROPERTY() int32 NavigationInternalEdgeCount = 0;
  UPROPERTY() int32 NavigationConnectionPointCount = 0;
  UPROPERTY() int32 NavigationCenterAnchorCount = 0;
  UPROPERTY() int32 NavigationSafeIntervalCount = 0;
  UPROPERTY() int32 GoalAttachmentCount = 0;
  UPROPERTY() int32 SourceAttachmentSuccessCount = 0;
  UPROPERTY() int32 CenterInvalidButConnectedCellCount = 0;
  UPROPERTY() int32 NavigationUnreachableSampleCount = 0;
  UPROPERTY() int32 FlowRecoveredFromRasterMismatchCount = 0;
  UPROPERTY() int32 FlowDesiredSegmentHardObstacleViolationCount = 0;
  UPROPERTY() float NavigationHardClearanceCm = 0.0f;
  UPROPERTY() float NavigationDomainReprojectDeltaCmMax = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetMotionRule
{
  GENERATED_BODY()

  UPROPERTY() int32 TargetId = 1;
  UPROPERTY() int32 TargetRevision = 1;
  UPROPERTY() FVector_NetQuantize10 InitialLocation = FVector(0.0f, 2200.0f, 60.0f);
  UPROPERTY() FVector_NetQuantize10 LinearVelocity = FVector::ZeroVector;
  UPROPERTY() uint8 bReflectAtMotionBounds = 0;
  UPROPERTY() FVector_NetQuantize10 MotionBoundsMin = FVector(-100000.0f, -100000.0f, 0.0f);
  UPROPERTY() FVector_NetQuantize10 MotionBoundsMax = FVector(100000.0f, 100000.0f, 0.0f);
  UPROPERTY() float InitialYawDegrees = 0.0f;
  UPROPERTY() float YawRateDegreesPerSecond = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrowdDemoTargetDistanceBandSettings
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
  UPROPERTY() int32 ProjectilePierceCount = 0;
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
  uint8 bEnableObstacle = 0;

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
  ECrowdDemoSoftPressureTestCase SoftPressureTestCase = ECrowdDemoSoftPressureTestCase::CorridorRoute;

  UPROPERTY()
  uint8 bEnableHeterogeneousProfiles = 0;

  UPROPERTY()
  FCrowdDemoTargetMotionRule TargetMotion;

  UPROPERTY()
  FCrowdDemoTargetDistanceBandSettings TargetDistanceBandSettings;

  UPROPERTY()
  FCrowdDemoTargetRegionTransportRuleSettings TargetRegionTransportSettings;

  UPROPERTY()
  FCrowdDemoRangedCombatSettings RangedCombatSettings;

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

  // The nominal capability observation window remains separate from an
  // explicitly predeclared completion grace. DurationSeconds is their sum.
  UPROPERTY()
  float NominalDurationSeconds = 1.5f;

  UPROPERTY()
  float CompletionGraceSeconds = 0.0f;

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
struct FCrowdDemoRoundPerformanceMetrics
{
  GENERATED_BODY()

  UPROPERTY() float FixedStepPipelineMsP50 = -1.0f;
  UPROPERTY() float FixedStepPipelineMsP95 = -1.0f;
  UPROPERTY() float FixedStepPipelineMsMax = -1.0f;
  UPROPERTY() float BusinessPrepareStageMsP95 = -1.0f;
  UPROPERTY() float BusinessPrepareStageMsMax = -1.0f;
  UPROPERTY() float SharedFlowStageMsP95 = -1.0f;
  UPROPERTY() float SharedFlowStageMsMax = -1.0f;
  UPROPERTY() float TargetTopologyStageMsP95 = -1.0f;
  UPROPERTY() float TargetTopologyStageMsMax = -1.0f;
  UPROPERTY() float TargetDemandStageMsP95 = -1.0f;
  UPROPERTY() float TargetDemandStageMsMax = -1.0f;
  UPROPERTY() float TargetPlanStageMsP95 = -1.0f;
  UPROPERTY() float TargetPlanStageMsMax = -1.0f;
  UPROPERTY() float TargetGuidanceStageMsP95 = -1.0f;
  UPROPERTY() float TargetGuidanceStageMsMax = -1.0f;
  UPROPERTY() float GuidanceComposeStageMsP95 = -1.0f;
  UPROPERTY() float GuidanceComposeStageMsMax = -1.0f;
  UPROPERTY() float LocalPredictiveStageMsP95 = -1.0f;
  UPROPERTY() float LocalPredictiveStageMsMax = -1.0f;
  UPROPERTY() float ParticleStageMsP95 = -1.0f;
  UPROPERTY() float ParticleStageMsMax = -1.0f;
  UPROPERTY() float FacingFinalizeStageMsP95 = -1.0f;
  UPROPERTY() float FacingFinalizeStageMsMax = -1.0f;
  UPROPERTY() float CommitStageMsP95 = -1.0f;
  UPROPERTY() float CommitStageMsMax = -1.0f;
  UPROPERTY() int32 TargetTopologyBuildCount = 0;
  UPROPERTY() int32 TargetTopologyCacheHitCount = 0;
  UPROPERTY() int32 TargetDemandFullBuildCount = 0;
  UPROPERTY() int32 TargetDemandPopulationUpdateCount = 0;
  UPROPERTY() float FixedStepsPerGameFrameP50 = -1.0f;
  UPROPERTY() float FixedStepsPerGameFrameP95 = -1.0f;
  UPROPERTY() int32 FixedStepsPerGameFrameMax = 0;
  UPROPERTY() int32 CatchupFrameCount = 0;
  UPROPERTY() int32 CatchupCpuBudgetHitCount = 0;
  UPROPERTY() int32 CatchupCpuBudgetConsecutiveMax = 0;
  UPROPERTY() int32 MaxFixedStepsPerFrameHitCount = 0;
  UPROPERTY() int32 BoundaryPendingFrameCount = 0;
  UPROPERTY() int32 BoundaryStaleResultCount = 0;
  UPROPERTY() int32 OrdinaryBlockWaitCount = 0;
  UPROPERTY() float FixedStepBacklogMsMax = 0.0f;
  UPROPERTY() float FixedStepBacklogMsP95 = 0.0f;
  UPROPERTY() float WorkerQueueMsP95 = -1.0f;
  UPROPERTY() float WorkerRunMsP95 = -1.0f;
  UPROPERTY() float WorkerCriticalPathMsP95 = -1.0f;
  UPROPERTY() float SimulationRealtimeFactor = -1.0f;
  UPROPERTY() float RollbackReplayMsP95 = -1.0f;
  UPROPERTY() float RollbackReplayMsMax = -1.0f;
  UPROPERTY() int32 RollbackReplaySampleCount = 0;
  UPROPERTY() int32 ZeroErrorRollbackReplayCount = 0;
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
  UPROPERTY() FCrowdDemoRoundPerformanceMetrics Performance;

  UPROPERTY() uint8 bLocalPredictiveValid = 0;
  UPROPERTY() uint32 GuidanceCandidateHash = 0;
  UPROPERTY() uint32 GuidanceComposeHash = 0;
  UPROPERTY() int32 GuidanceComposeSampleCount = 0;
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
  UPROPERTY() uint8 bTargetPlanLifecycleDiagnosticValid = 0;
  UPROPERTY() int32 TargetPlanLifecycleSampleBoundaryCount = 0;
  UPROPERTY() uint32 TargetPlanLifecycleHash = 0;
  UPROPERTY() int32 TargetPlanLifecycleInitialInvalidRebuildCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleCostOnlyGraphChangeCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleCellFeasibilityChangeCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleEdgeSetChangeCount = 0;
  UPROPERTY() int32 TargetPlanLifecyclePrematureRebuildCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleActiveClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleGeometryEligibleClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleSupplyEligibleClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleNewPlanEligibleClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleMigratedClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleCompletedAtReplacementClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleDroppedStillFeasibleClaimCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleStateMismatchCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleClaimOffEdgeCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleQuotaExceededCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleSupplyWithoutOutgoingQuotaCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleOtherInvalidCount = 0;
  UPROPERTY() int32 TargetPlanLifecycleAgeP50 = 0;
  UPROPERTY() int32 TargetPlanLifecycleAgeP95 = 0;
  UPROPERTY() int32 TargetPlanLifecycleAgeMax = 0;
  UPROPERTY() uint8 bTargetPlanLifecycleFixtureValid = 0;
  UPROPERTY() int32 TargetPlanLifecycleFixtureStep = INDEX_NONE;
  UPROPERTY() uint32 TargetPlanLifecycleFixtureCohortKey = 0;
  UPROPERTY() int32 TargetPlanLifecycleFixtureReason = 0;
  UPROPERTY() int32 TargetPlanLifecycleFixtureSelectionKind = 0;
  UPROPERTY() int32 TargetPlanLifecycleFixtureRegionKey = INDEX_NONE;
  UPROPERTY() uint32 TargetPlanLifecycleFixtureHash = 0;
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
  UPROPERTY() int32 T4FinalSettledCount = 0;
  UPROPERTY() int32 T4FinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T4UnreachableSampleCount = 0;
  UPROPERTY() int32 T4LastFixedStep = INDEX_NONE;
  UPROPERTY() int32 T4CompletionStepMax = INDEX_NONE;
  UPROPERTY() int32 T4GroupCompletionStep = INDEX_NONE;
  UPROPERTY() int32 T4GroupSettledStep = INDEX_NONE;
  UPROPERTY() uint8 bT6TransitValid = 0;
  UPROPERTY() uint32 T6TransitLayoutHash = 0;
  UPROPERTY() uint32 T6TransitFlowHash = 0;
  UPROPERTY() uint32 T6TransitProgressHash = 0;
  UPROPERTY() int32 T6TransitWallPassedCount = 0;
  UPROPERTY() int32 T6TransitCorridorExitedCount = 0;
  UPROPERTY() int32 T6TransitCompletedCount = 0;
  UPROPERTY() int32 T6TransitFinalSettledCount = 0;
  UPROPERTY() int32 T6TransitFinalDeadlockAgentCount = 0;
  UPROPERTY() int32 T6TransitUnreachableSampleCount = 0;
  UPROPERTY() int32 T6TransitLastFixedStep = INDEX_NONE;
  UPROPERTY() int32 T6TransitCompletionStepMax = INDEX_NONE;
  UPROPERTY() int32 T6TransitGroupCompletionStep = INDEX_NONE;
  UPROPERTY() int32 T6TransitGroupSettledStep = INDEX_NONE;
  UPROPERTY() int32 T6TransitTargetHandoffStep = INDEX_NONE;
  UPROPERTY() int32 T6TransitTargetInsideBandCount = 0;
  UPROPERTY() int32 T6TransitTargetCoverageCount = 0;
  UPROPERTY() int32 T6TransitTargetRequiredCoverageCount = 0;
  UPROPERTY() int32 T6TransitTargetEngagedHoldCount = 0;
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
  int32 ObstaclePenetrationCount = 0;

  UPROPERTY()
  int32 ArrivalCount = 0;

  UPROPERTY()
  FCrowdDemoSharedFlowMetrics SharedFlowMetrics;

  UPROPERTY()
  FCrowdDemoParticleMetrics ParticleMetrics;

  UPROPERTY()
  FCrowdDemoProjectileMetrics ProjectileMetrics;
};

USTRUCT(BlueprintType)
struct FCrowdDemoRoundResultHeader
{
  GENERATED_BODY()

  static constexpr uint8 CurrentContractVersion = 2;
  static constexpr int32 MaximumSerializedBytes = 2048;

  UPROPERTY() uint8 ContractVersion = CurrentContractVersion;
  // 1 = SoftPressure, 2 = SoftPressure + projectile combat.
  UPROPERTY() uint8 PayloadKind = 1;
  UPROPERTY() int32 SerializedByteCount = 0;
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
  UPROPERTY() int32 ObstaclePenetrationCount = 0;
  UPROPERTY() int32 ArrivalCount = 0;
  UPROPERTY() FCrowdDemoSharedFlowMetrics SharedFlowMetrics;
  UPROPERTY() FCrowdDemoParticleMetrics ParticleMetrics;
  UPROPERTY() FCrowdDemoProjectileMetrics ProjectileMetrics;

  bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FCrowdDemoRoundResultHeader>
  : public TStructOpsTypeTraitsBase2<FCrowdDemoRoundResultHeader>
{
  enum { WithNetSerializer = true };
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
  FCrowdDemoSharedFlowMetrics SharedFlowMetrics;

  UPROPERTY()
  int32 CorridorDeadlockAgentCount = 0;
};

struct FCrowdDemoSummaryMetrics
{
  int32 Agents = 0;
  int32 VisibleInstances = 0;
  float ServerFrameMsP95 = -1.0f;
  float CrowdSolverMsP95 = -1.0f;
  float SnapshotBuildMsP95 = -1.0f;
  float ClientFrameMsP95 = -1.0f;
  float ClientFrameMsMax = -1.0f;
  float ClientGameThreadMsP95 = -1.0f;
  float ClientGameThreadMsMax = -1.0f;
  float ClientRenderThreadMsP95 = -1.0f;
  float ClientRenderThreadMsMax = -1.0f;
  float ClientGpuFrameMsP95 = -1.0f;
  float ClientGpuFrameMsMax = -1.0f;
  float ClientGameThreadWaitMsP95 = -1.0f;
  float ClientRhiThreadMsP95 = -1.0f;
  float ClientSwapBufferMsP95 = -1.0f;
  int32 ClientGameBoundHitchCount = 0;
  int32 ClientRenderBoundHitchCount = 0;
  int32 ClientGpuBoundHitchCount = 0;
  int32 ClientUnattributedHitchCount = 0;
  int32 ClientShaderCompilingFrameCount = 0;
  int32 ClientShaderJobsMax = 0;
  int32 ClientAsyncLoadingFrameCount = 0;
  int32 ClientVisualAssetCompilingFrameCount = 0;
  int32 ClientVisualPsoPrecacheFrameCount = 0;
  float ClientWarmupSeconds = 0.0f;
  float ClientWarmupFrameMsP95 = -1.0f;
  float ClientWarmupFrameMsMax = -1.0f;
  int32 ClientWarmupShaderCompilingFrameCount = 0;
  int32 ClientWarmupShaderJobsMax = 0;
  int32 ClientWarmupAsyncLoadingFrameCount = 0;
  int32 ClientWarmupVisualAssetCompilingFrameCount = 0;
  int32 ClientWarmupVisualPsoPrecacheFrameCount = 0;
  float VisualProcessorMsP95 = -1.0f;
  float VisualProcessorMsMax = -1.0f;
  float VisualSubmitIntervalMsP95 = -1.0f;
  float VisualSubmitIntervalMsMax = -1.0f;
  float VisualSimDeltaCmP95 = -1.0f;
  float VisualSimDeltaCmMax = -1.0f;
  float VisualDisplayDeltaCmP95 = -1.0f;
  float VisualDisplayDeltaCmMax = -1.0f;
  float VisualCollapsedSimStepsP95 = -1.0f;
  int32 VisualCollapsedSimStepsMax = 0;
  int32 VisualCatchupSubmitCount = 0;
  int32 VisualCatchupDiscontinuityCount = 0;
  int32 NonCorrectionVisualDiscontinuityCount = 0;
  int32 RoundResetVisualJumpCount = 0;
  int32 TestBoundaryResetVisualJumpCount = 0;
  int32 VisualIsmRebuildCount = 0;
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
  FCrowdDemoSharedFlowMetrics SharedFlowMetrics;
  int32 CorridorDeadlockAgentCount = 0;
};
