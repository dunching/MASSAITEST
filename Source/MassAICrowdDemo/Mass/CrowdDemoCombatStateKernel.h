#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoHitFact
{
  uint64 HitEventId = 0;
  int32 ApplyFixedStep = INDEX_NONE;
  int32 SourceAgentId = INDEX_NONE;
  int32 SourceLifecycleSerial = 0;
  int32 TargetAgentId = INDEX_NONE;
  int32 TargetLifecycleSerial = 0;
  FVector HitPosition = FVector::ZeroVector;
  FVector HitDirection = FVector::ForwardVector;
  float Damage = 0.0f;
  float HorizontalImpulseCmps = 0.0f;
  float VerticalImpulseCmps = 0.0f;
  uint32 HitFlashProfileKey = 0;
};

struct FCrowdDemoCombatAgentState
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  float Health = 100.0f;
  float MaxHealth = 100.0f;
  ECrowdDemoLifecycleState LifecycleState = ECrowdDemoLifecycleState::Alive;
  bool bAlive = true;

  ECrowdDemoBusinessState BusinessState = ECrowdDemoBusinessState::Idle;
  int32 BusinessStateRevision = 0;
  int32 BusinessStateEnterFixedStep = 0;
  int32 TargetAgentId = INDEX_NONE;
  int32 TargetLifecycleSerial = 0;

  ECrowdDemoAttackPhase AttackPhase = ECrowdDemoAttackPhase::None;
  int32 AttackPhaseEnterFixedStep = 0;
  int32 CooldownEndFixedStep = 0;
  int32 LockedTargetAgentId = INDEX_NONE;
  int32 LockedTargetLifecycleSerial = 0;
  FVector LockedTargetLocation = FVector::ZeroVector;
  int32 FireSequence = 0;
  bool bFireRequestIssued = false;

  ECrowdDemoReactiveMotionMode ReactiveMode = ECrowdDemoReactiveMotionMode::None;
  FVector HorizontalReactiveVelocity = FVector::ZeroVector;
  float VerticalReactiveVelocityCmps = 0.0f;
  int32 ReactiveStartFixedStep = INDEX_NONE;
  int32 ReactiveEndFixedStep = INDEX_NONE;
  int32 ReactiveRevision = 0;
  ECrowdDemoBusinessState RestoreBusinessState = ECrowdDemoBusinessState::Idle;
  int32 ApexCount = 0;
  int32 LandingCount = 0;

  int32 HitFlashRevision = 0;
  float HitFlashStartServerTimeSeconds = 0.0f;
  float HitFlashDurationSeconds = 0.0f;
  uint32 HitFlashProfileKey = 0;
  float HitFlashPeakIntensity = 0.0f;

  uint64 LastConsumedHitEventId = 0;

  ECrowdDemoVisualState VisualState = ECrowdDemoVisualState::Idle;
  int32 VisualRevision = 0;
  float VisualStateStartServerTimeSeconds = 0.0f;
  uint32 VisualPhaseSeed = 0;
};

struct FCrowdDemoHitResponseSettings
{
  float MaximumHorizontalImpulseCmps = 1200.0f;
  float MaximumVerticalImpulseCmps = 1600.0f;
  int32 ReactiveDurationFixedSteps = 15;
  int32 LandingRecoveryFixedSteps = 6;
  float HitFlashDurationSeconds = 0.15f;
  float HitFlashPeakIntensity = 1.0f;
  float GravityCmps2 = -980.0f;
  float GroundZ = 60.0f;
  float FixedStepSeconds = 1.0f / 30.0f;
};

struct FCrowdDemoHitResponseSummary
{
  bool bValid = false;
  int32 InputHitCount = 0;
  int32 AppliedHitCount = 0;
  int32 DuplicateHitCount = 0;
  int32 StaleLifecycleCount = 0;
  int32 MissingTargetCount = 0;
  int32 AlreadyDeadCount = 0;
  int32 DamageAppliedAgentCount = 0;
  int32 ReactiveAgentCount = 0;
  int32 DeathCount = 0;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoReactiveMotionStepResult
{
  bool bValid = false;
  FVector HorizontalVelocity = FVector::ZeroVector;
  float NewZ = 0.0f;
  float NewVerticalVelocityCmps = 0.0f;
  bool bReachedApex = false;
  bool bLanded = false;
  bool bRecovered = false;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoVatShowcaseMotionSettings
{
  int32 FirstMovingFormationIndex = 4;
  int32 MovingAgentCount = 4;
  int32 HalfCycleFixedSteps = 6;
  float MoveSpeedCmps = 60.0f;
  float MaximumAnchorOffsetCm = 12.0f;
};

struct FCrowdDemoVatShowcaseMotionResult
{
  bool bValid = false;
  bool bMovingGroup = false;
  FVector DesiredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  uint32 StableHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoCombatStateKernel
{
public:
  static void ResolveHitFacts(
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    TConstArrayView<FCrowdDemoHitFact> HitFacts,
    const FCrowdDemoHitResponseSettings& Settings,
    TArray<FCrowdDemoCombatAgentState>& InOutAgents,
    FCrowdDemoHitResponseSummary& OutSummary);

  static FCrowdDemoReactiveMotionStepResult AdvanceReactiveMotion(
    int32 FixedStepIndex,
    float CurrentZ,
    const FCrowdDemoHitResponseSettings& Settings,
    FCrowdDemoCombatAgentState& InOutAgent);

  static FCrowdDemoVatShowcaseMotionResult BuildVatShowcaseMotion(
    int32 FormationIndex,
    int32 FixedStepIndex,
    const FVector& CurrentLocation,
    const FVector& AnchorLocation,
    const FCrowdDemoVatShowcaseMotionSettings& Settings = {});

  static ECrowdDemoVisualState ResolveVisualState(
    const FCrowdDemoCombatAgentState& Agent,
    const FVector& Velocity,
    bool bUseExplicitBusinessLocomotionState = false);

  static void ResolveVisualStateBoundary(
    int32 FixedStepIndex,
    float ServerTimeSeconds,
    const FVector& Velocity,
    FCrowdDemoCombatAgentState& InOutAgent,
    bool bUseExplicitBusinessLocomotionState = false);

  static uint32 HashAgents(TConstArrayView<FCrowdDemoCombatAgentState> Agents);
};
