#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoPursuitPositioningKernel.h"
#include "Mass/CrowdDemoTargetApproachKernel.h"
#include "CrowdDemoMassFragments.generated.h"

USTRUCT()
struct FCrowdDemoMassAgentTag : public FMassTag
{
  GENERATED_BODY()
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
struct FCrowdDemoMassMovementFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  float ContactRadiusCm = 42.0f;

  UPROPERTY(Transient)
  float SeparationRadiusCm = 78.0f;

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
struct FCrowdDemoTargetApproachFragment : public FMassFragment
{
  GENERATED_BODY()

  ECrowdDemoTargetApproachState State = ECrowdDemoTargetApproachState::Approach;

  UPROPERTY(Transient) int32 TargetId = INDEX_NONE;
  UPROPERTY(Transient) int32 TargetRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 SlotLayoutRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 AssignedSlotId = INDEX_NONE;
  UPROPERTY(Transient) int32 RingEnterFixedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 StateEnterFixedStep = 0;
  UPROPERTY(Transient) int32 StableSettleSteps = 0;
  UPROPERTY(Transient) int32 StateRevision = 0;
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
  float PhysicalRadiusCm = 42.0f;

  UPROPERTY(Transient)
  float HardSafetyGapCm = 10.0f;

  UPROPERTY(Transient)
  float SoftMarginCm = 17.0f;

  UPROPERTY(Transient)
  float Mobility = 1.0f;
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
struct FCrowdDemoRoundPbdCorrectionFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector PrePbdLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector CorrectedLocation = FVector::ZeroVector;

  UPROPERTY(Transient)
  FVector Correction = FVector::ZeroVector;

  UPROPERTY(Transient)
  int32 CorrectedPairCount = 0;

  UPROPERTY(Transient)
  bool bCorrected = false;
};

USTRUCT()
struct FCrowdDemoRoundSeparationFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient)
  FVector PushVelocity = FVector::ZeroVector;

  UPROPERTY(Transient)
  int32 NeighborCount = 0;

  UPROPERTY(Transient)
  int32 OverlapCount = 0;

  UPROPERTY(Transient)
  int32 SevereOverlapCount = 0;

  UPROPERTY(Transient)
  bool bHardSeparation = false;
};

UENUM()
enum class ECrowdDemoPortalAdmissionState : uint8
{
  None,
  Approach,
  Waiting,
  Reserved,
  Inside,
  Exited
};

USTRUCT()
struct FCrowdDemoPortalAdmissionFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 CohortId = 0;
  UPROPERTY(Transient) int32 PortalId = INDEX_NONE;
  UPROPERTY(Transient) int32 DirectionKey = 0;
  UPROPERTY(Transient) int32 WaitSteps = 0;
  UPROPERTY(Transient) int32 WaitEpoch = 0;
  UPROPERTY(Transient) int32 TokenGrantedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 EnteredPortalStep = INDEX_NONE;
  UPROPERTY(Transient) int32 LastTransitionStep = INDEX_NONE;
  UPROPERTY(Transient) int32 DirectionEpoch = 0;
  UPROPERTY(Transient) ECrowdDemoPortalAdmissionState State = ECrowdDemoPortalAdmissionState::None;
};

USTRUCT()
struct FCrowdDemoPassingBandFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 PortalId = INDEX_NONE;
  UPROPERTY(Transient) int32 DirectionEpoch = INDEX_NONE;
  UPROPERTY(Transient) int16 BandId = INDEX_NONE;
  UPROPERTY(Transient) bool bValid = false;
};

USTRUCT()
struct FCrowdDemoPositionAssignmentFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) int32 TargetId = INDEX_NONE;
  UPROPERTY(Transient) int32 PositionId = INDEX_NONE;
  UPROPERTY(Transient) int32 AssignmentRevision = 0;
  ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
  ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
  UPROPERTY(Transient) FVector LocalOffset = FVector::ZeroVector;
  UPROPERTY(Transient) FVector DesiredLocation = FVector::ZeroVector;
  UPROPERTY(Transient) int32 StableArrivalStepCount = 0;
  UPROPERTY(Transient) int32 ExitGraceStepCount = 0;
  UPROPERTY(Transient) int32 LastReassignmentStep = INDEX_NONE;
  UPROPERTY(Transient) int32 FrontCommitGrantedStep = INDEX_NONE;
  ECrowdDemoFrontApproachPhase FrontApproachPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontApproachPhase RequestedApproachPhase = ECrowdDemoFrontApproachPhase::None;
  ECrowdDemoFrontPhaseReservationDecision PhaseReservationDecision =
    ECrowdDemoFrontPhaseReservationDecision::None;
  UPROPERTY(Transient) int32 PhaseReservationRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 PhaseReservationHeldSteps = 0;
  ECrowdDemoFrontPhaseReservationReason PhaseReservationInvalidReason =
    ECrowdDemoFrontPhaseReservationReason::None;
  UPROPERTY(Transient) int32 FrontApproachRouteRevision = 0;
  UPROPERTY(Transient) int32 FrontApproachBestErrorBucket = MAX_int32;
  UPROPERTY(Transient) int32 FrontApproachLastProgressStep = INDEX_NONE;
  UPROPERTY(Transient) int32 FrontApproachNoProgressSteps = 0;
  UPROPERTY(Transient) float FrontApproachPreviousRadialErrorCm = -1.0f;
  UPROPERTY(Transient) int32 FrontApproachPreviousErrorBucket = MAX_int32;
  UPROPERTY(Transient) int32 FrontApproachComposeBoundarySwitchCount = 0;
  UPROPERTY(Transient) bool bFrontApproachComposeStateInitialized = false;
  UPROPERTY(Transient) bool bFrontApproachWasWithinComposeRange = false;
  UPROPERTY(Transient) bool bFrontApproachRadialErrorImproved = false;
  UPROPERTY(Transient) bool bFrontApproachQuantizedProgressStall = false;
};

UENUM()
enum class ECrowdDemoPursuitSteeringInvalidReason : uint8
{
  None,
  TargetRevision,
  PositionInvalid,
  HoldingInvalid,
  CompatibilityInvalid,
  OwnerMissing,
  NoProgress
};

// Only cross-fixed-step steering ownership lives here. Candidate graphs and
// decision arrays remain prepared SoA in the pipeline subsystem.
USTRUCT()
struct FCrowdDemoPursuitSteeringStateFragment : public FMassFragment
{
  GENERATED_BODY()

  ECrowdDemoPursuitSteeringState SteeringState = ECrowdDemoPursuitSteeringState::Pursuit;
  UPROPERTY(Transient) int32 HoldingId = INDEX_NONE;
  UPROPERTY(Transient) int32 AssignedPositionId = INDEX_NONE;
  UPROPERTY(Transient) int32 TargetRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 StateRevision = 0;
  UPROPERTY(Transient) int32 StateEnterFixedStep = INDEX_NONE;
  UPROPERTY(Transient) int32 WaitEpoch = 0;
  UPROPERTY(Transient) int32 CommitDecisionRevision = INDEX_NONE;
  UPROPERTY(Transient) int32 StableArrivalStepCount = 0;
  UPROPERTY(Transient) int32 LastProgressBucket = MAX_int32;
  UPROPERTY(Transient) int32 LastProgressFixedStep = INDEX_NONE;
  ECrowdDemoPursuitSteeringInvalidReason InvalidReason =
    ECrowdDemoPursuitSteeringInvalidReason::None;
};

USTRUCT()
struct FCrowdDemoPursuitGuidanceFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) FVector DesiredVelocity = FVector::ZeroVector;
  UPROPERTY(Transient) bool bPositioningActive = false;
};

USTRUCT()
struct FCrowdDemoOrcaVelocityFragment : public FMassFragment
{
  GENERATED_BODY()

  UPROPERTY(Transient) FVector Velocity = FVector::ZeroVector;
  UPROPERTY(Transient) int32 NeighborCount = 0;
  UPROPERTY(Transient) int32 ConstraintCount = 0;
  UPROPERTY(Transient) uint8 FallbackStage = 0;
  UPROPERTY(Transient) bool bAdjusted = false;
  UPROPERTY(Transient) bool bInfeasible = false;
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
  bool bDisplayInitialized = false;

};
