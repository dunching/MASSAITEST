#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"
#include "CrowdDemoMassFragments.generated.h"

USTRUCT()
struct FCrowdDemoMassAgentTag : public FMassTag
{
  GENERATED_BODY()
};

USTRUCT()
struct FCrowdDemoMassProjectileTag : public FMassTag
{
  GENERATED_BODY()
};

USTRUCT()
struct FCrowdDemoMassProjectileFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) uint64 ProjectileId = 0;
  UPROPERTY(Transient) int32 SourceAgentId = INDEX_NONE;
  UPROPERTY(Transient) int32 TargetAgentId = INDEX_NONE;
  UPROPERTY(Transient) int32 SpawnFixedStep = INDEX_NONE;
  UPROPERTY(Transient) FVector PreviousPosition = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Position = FVector::ZeroVector;
  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) float RadiusCm = 12.0f;
  UPROPERTY(Transient) bool bActive = false;
  UPROPERTY(Transient) bool bImpacted = false;
  UPROPERTY(Transient) bool bExpired = false;
};

USTRUCT()
struct FCrowdDemoMassIdentityFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 Id = INDEX_NONE;

  UPROPERTY(Transient)
  int32 VisualId = INDEX_NONE;

  UPROPERTY(Transient)
  int32 LifecycleSerial = 1;
};

USTRUCT()
struct FCrowdDemoMassStatsFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  float Health = 100.0f;

  UPROPERTY(Transient)
  float MaxHealth = 100.0f;

  UPROPERTY(Transient)
  ECrowdDemoLifecycleState LifecycleState = ECrowdDemoLifecycleState::Alive;

  UPROPERTY(Transient)
  bool bAlive = true;
};

USTRUCT()
struct FCrowdDemoBusinessStateFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) ECrowdDemoBusinessState State = ECrowdDemoBusinessState::Idle;
  UPROPERTY(Transient) int32 StateRevision = 0;
  UPROPERTY(Transient) int32 StateEnterFixedStep = 0;
  UPROPERTY(Transient) int32 TargetAgentId = INDEX_NONE;
  UPROPERTY(Transient) int32 TargetLifecycleSerial = 0;
  UPROPERTY(Transient) uint64 LastConsumedHitEventId = 0;
};

USTRUCT()
struct FCrowdDemoRangedAttackFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) ECrowdDemoAttackPhase Phase = ECrowdDemoAttackPhase::None;
  UPROPERTY(Transient) int32 PhaseEnterFixedStep = 0;
  UPROPERTY(Transient) int32 CooldownEndFixedStep = 0;
  UPROPERTY(Transient) int32 LockedTargetAgentId = INDEX_NONE;
  UPROPERTY(Transient) int32 LockedTargetLifecycleSerial = 0;
  UPROPERTY(Transient) FVector LockedTargetLocation = FVector::ZeroVector;
  UPROPERTY(Transient) int32 FireSequence = 0;
  UPROPERTY(Transient) bool bFireRequestIssued = false;
};

USTRUCT()
struct FCrowdDemoReactiveMotionFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) ECrowdDemoReactiveMotionMode Mode = ECrowdDemoReactiveMotionMode::None;
  UPROPERTY(Transient) FVector HorizontalVelocity = FVector::ZeroVector;
  UPROPERTY(Transient) float VerticalVelocityCmps = 0.0f;
  UPROPERTY(Transient) int32 StartFixedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 EndFixedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 ReactiveRevision = 0;
  UPROPERTY(Transient) ECrowdDemoBusinessState RestoreBusinessState = ECrowdDemoBusinessState::Idle;
  UPROPERTY(Transient) int32 ApexCount = 0;
  UPROPERTY(Transient) int32 LandingCount = 0;
};

USTRUCT()
struct FCrowdDemoReactiveMotionStepFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) bool bActive = false;
  UPROPERTY(Transient) float ProposedZ = 0.0f;
  UPROPERTY(Transient) float VerticalVelocityCmps = 0.0f;
};

USTRUCT()
struct FCrowdDemoHitFlashFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 FlashRevision = 0;
  UPROPERTY(Transient) float StartServerTimeSeconds = 0.0f;
  UPROPERTY(Transient) float DurationSeconds = 0.0f;
  UPROPERTY(Transient) uint32 ProfileKey = 0;
  UPROPERTY(Transient) float PeakIntensity = 0.0f;
};

USTRUCT()
struct FCrowdDemoMassMovementFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  float ContactRadiusCm = 42.0f;

  UPROPERTY(Transient)
  float MaxSpeedCmPerSecond = 260.0f;

  UPROPERTY(Transient)
  float YawDegrees = 0.0f;

  UPROPERTY(Transient)
  FVector DesiredVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector CurrentVelocity = FVector::ZeroVector;

};

USTRUCT()
struct FCrowdDemoRoundSimStateFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector Location = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector Velocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  float YawDegrees = 0.0f;

  UPROPERTY(Transient)
  float SimulatedServerTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  int32 PlanRevision = 0;

  UPROPERTY(Transient)
  bool bInitialized = false;
};

USTRUCT()
struct FCrowdDemoRoundFormationFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 FormationIndex = INDEX_NONE;

  UPROPERTY(Transient)
  FVector LocalOffset = FVector::ZeroVector;

  UPROPERTY(Transient)
  float RadiusCm = 42.0f;

  UPROPERTY(Transient)
  bool bInitialized = false;
};

USTRUCT()
struct FCrowdDemoRoundMoveIntentFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector PreferredDirection = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector DesiredLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector DesiredVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  float DesiredYawDegrees = 0.0f;

  UPROPERTY(Transient)
  int32 PlanRevision = 0;
};

USTRUCT()
struct FCrowdDemoRoundGuidanceCandidatesFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdDemoGuidanceCandidate SharedFlow;
  FCrowdDemoGuidanceCandidate TargetRegion;
  FCrowdDemoGuidanceCandidate BusinessOverride;
};

USTRUCT()
struct FCrowdDemoRoundComposedGuidanceFragment : public FMassFragment
{
  GENERATED_BODY()

  FCrowdDemoComposedGuidance Value;
};

USTRUCT()
struct FCrowdDemoRoundFacingFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) float ResolvedYawDegrees = 0.0f;
  UPROPERTY(Transient) int32 ConsecutiveFinalSettleSteps = 0;
  UPROPERTY(Transient) bool bFinalPositionSettled = false;
  UPROPERTY(Transient) bool bFacingTarget = false;
};

USTRUCT()
struct FCrowdDemoTargetCapabilityFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) uint32 CapabilityMask = 1u;
  UPROPERTY(Transient) float MinimumFunctionalDistanceCm = 0.0f;
  UPROPERTY(Transient) float MaximumFunctionalDistanceCm = 5000.0f;
  UPROPERTY(Transient) int32 StableBusinessPriority = 0;
};

USTRUCT()
struct FCrowdDemoRoundFlowSampleFragment : public FMassFragment
{
  GENERATED_BODY()

  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  ECrowdDemoFlowLocationStatus Status = ECrowdDemoFlowLocationStatus::OutOfBounds;

  UPROPERTY(Transient)
  FVector FlowDirection = FVector::ZeroVector;

  UPROPERTY(Transient)
  int32 IntegrationCost = MAX_int32;

  UPROPERTY(Transient)
  float GuidanceDistanceCm = 0.0f;

  UPROPERTY(Transient)
  uint64 NavigationNodeKey = 0;

  UPROPERTY(Transient)
  uint64 NextNavigationNodeKey = 0;

  UPROPERTY(Transient)
  bool bBlocked = false;

  UPROPERTY(Transient)
  bool bUnreachable = true;

  bool bRecoveredFromRasterMismatch = false;
  bool bSourceAttached = false;
};

USTRUCT()
struct FCrowdDemoRoundLocalVelocityFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) int32 NeighborCount = 0;
  UPROPERTY(Transient) int32 ConstraintCount = 0;
  UPROPERTY(Transient) int32 BlockedAgeSteps = 0;
  UPROPERTY(Transient) uint32 ComponentKey = 0;
  UPROPERTY(Transient) int32 GrantEpoch = 0;
  UPROPERTY(Transient) int32 PlanRevision = 0;
  UPROPERTY(Transient) bool bAdjusted = false;
  UPROPERTY(Transient) bool bGranted = false;
  UPROPERTY(Transient) bool bYielding = false;
  UPROPERTY(Transient) bool bValid = false;
};

USTRUCT()
struct FCrowdDemoRoundProposedMovementFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector StartLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector ProposedLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector ProposedVelocity = FVector::ZeroVector;
};

USTRUCT()
struct FCrowdDemoParticlePropertiesFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 ProfileId = INDEX_NONE;

  UPROPERTY(Transient)
  uint32 CapabilityProfileKey = 0;

  UPROPERTY(Transient)
  float PhysicalRadiusCm = 42.0f;

  UPROPERTY(Transient)
  float HardSafetyGapCm = 10.0f;

  UPROPERTY(Transient)
  float SoftMarginCm = 17.0f;

  UPROPERTY(Transient)
  float Mobility = 1.0f;
};

// Test-only participation state for T1 OpenSpawnRelaxation. This is not an
// entity lifecycle or gameplay spawn contract: all Mass entities remain alive
// and visible while inactive particles stay in the staging area.
USTRUCT()
struct FCrowdDemoOpenSpawnRelaxationFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 FormationIndex = INDEX_NONE;

  UPROPERTY(Transient)
  bool bParticleActive = false;

  UPROPERTY(Transient)
  bool bPendingBoundaryReset = false;

  UPROPERTY(Transient)
  FVector BoundaryResetLocation = FVector::ZeroVector;
};

USTRUCT()
struct FCrowdDemoRoundParticleConstraintFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector CorrectedLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector CorrectedVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector RealizedCorrection = FVector::ZeroVector;

  UPROPERTY(Transient)
  int32 FirstInfluencedIteration = INDEX_NONE;

  UPROPERTY(Transient)
  bool bValid = false;

};

USTRUCT()
struct FCrowdDemoRoundObstacleConstraintFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector ConstrainedLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector ConstrainedVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  bool bHitObstacle = false;

  UPROPERTY(Transient)
  bool bPenetrating = false;

  bool bHitFlowBounds = false;
  float FlowBoundsReprojectDeltaCm = 0.0f;
};



USTRUCT()
struct FCrowdDemoMassVisualFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  ECrowdDemoAnimState AnimState = ECrowdDemoAnimState::Idle;

  UPROPERTY(Transient)
  uint8 VatClipIndex = 0;

  UPROPERTY(Transient)
  uint8 VatPhaseByte = 0;

  UPROPERTY(Transient)
  uint8 VatPlayRateByte = 128;

  UPROPERTY(Transient)
  ECrowdDemoVisualState VisualState = ECrowdDemoVisualState::Idle;

  UPROPERTY(Transient)
  int32 VisualRevision = 0;

  UPROPERTY(Transient)
  float StateStartServerTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  uint32 PhaseSeed = 0;

};

USTRUCT()
struct FCrowdDemoClientAuthorityFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  int32 VisualId = INDEX_NONE;

  UPROPERTY(Transient)
  int32 LifecycleSerial = 0;

  UPROPERTY(Transient)
  FVector AuthoritativeLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector AuthoritativeVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  float AuthoritativeYawDegrees = 0.0f;

  UPROPERTY(Transient)
  float ServerSampleTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  float LastReceiveWorldTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  ECrowdDemoAnimState AnimState = ECrowdDemoAnimState::Idle;

  UPROPERTY(Transient)
  uint8 VatClipIndex = 0;

  UPROPERTY(Transient)
  uint8 VatPhaseByte = 0;

  UPROPERTY(Transient)
  uint8 VatPlayRateByte = 128;

  UPROPERTY(Transient)
  FCrowdDemoCombatNetState Combat;

  UPROPERTY(Transient)
  bool bInitialized = false;
};

USTRUCT()
struct FCrowdDemoClientVisualOffsetFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector DisplayLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  float DisplayYawDegrees = 0.0f;

  UPROPERTY(Transient)
  int32 InstanceIndex = INDEX_NONE;

  UPROPERTY(Transient)
  int32 LastRoundSimCorrectionRevision = 0;

  UPROPERTY(Transient)
  FVector RoundSimDisplayOffset = FVector::ZeroVector;

  UPROPERTY(Transient)
  float RoundSimYawOffsetDegrees = 0.0f;

  UPROPERTY(Transient)
  FVector LastSubmittedSimLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector LastSubmittedDisplayLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  float LastSubmittedSimServerTimeSeconds = 0.0f;

  UPROPERTY(Transient)
  float LastSubmittedWorldSeconds = 0.0f;

  UPROPERTY(Transient)
  int32 LastSubmittedPlanRevision = 0;

  // T1 keeps every Mass entity alive and moves inactive participants between
  // its staging and active fixture layouts at explicit test boundaries.  This
  // bit lets the client visual path classify that authored reset separately
  // from an ordinary movement discontinuity.
  UPROPERTY(Transient)
  bool bLastSubmittedOpenSpawnParticleActive = false;

  UPROPERTY(Transient)
  FVector InterpolationFromLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector InterpolationToLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  float InterpolationFromYawDegrees = 0.0f;

  UPROPERTY(Transient)
  float InterpolationToYawDegrees = 0.0f;

  UPROPERTY(Transient)
  float InterpolationStartWorldSeconds = 0.0f;

  UPROPERTY(Transient)
  float InterpolationDurationSeconds = 0.0f;

  UPROPERTY(Transient)
  bool bDisplayInitialized = false;

};
