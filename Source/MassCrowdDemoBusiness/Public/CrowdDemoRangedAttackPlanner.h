#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdDemoRangedBusinessState : uint8
{
  Idle = 0,
  Moving = 1,
  Attacking = 2,
  HitReact = 3,
  Dead = 4
};

enum class ECrowdDemoRangedAttackPhase : uint8
{
  None = 0,
  AcquireTarget = 1,
  Windup = 2,
  Fire = 3,
  Recovery = 4,
  Cooldown = 5
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoRangedAttackSettings
{
  int32 ShooterCount = 0;
  int32 WindupFixedSteps = 0;
  int32 RecoveryFixedSteps = 0;
  int32 CooldownFixedSteps = 0;
  float ProjectileSpeedCmps = 0.0f;
  float MuzzleForwardOffsetCm = 0.0f;
  float PositionQuantumCm = 0.0f;
  float VelocityQuantumCmps = 0.0f;

  bool IsValid() const;
};

struct FCrowdDemoRangedAttackState
{
  ECrowdDemoRangedBusinessState BusinessState =
    ECrowdDemoRangedBusinessState::Idle;
  int32 BusinessStateRevision = 0;
  int32 BusinessStateEnterFixedStep = 0;
  int32 TargetAgentId = INDEX_NONE;
  int32 TargetLifecycleSerial = 0;
  ECrowdDemoRangedAttackPhase AttackPhase =
    ECrowdDemoRangedAttackPhase::None;
  int32 AttackPhaseEnterFixedStep = 0;
  int32 CooldownEndFixedStep = 0;
  int32 LockedTargetAgentId = INDEX_NONE;
  int32 LockedTargetLifecycleSerial = 0;
  FVector LockedTargetLocation = FVector::ZeroVector;
  int32 FireSequence = 0;
  bool bFireRequestIssued = false;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoRangedAttackAgent
{
  FCrowdStableEntityRef EntityRef;
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 StateLifecycleSerial = 0;
  int32 FormationIndex = INDEX_NONE;
  uint32 FactionId = 0;
  uint32 NavLayer = 0;
  FVector Position = FVector::ZeroVector;
  bool bAlive = true;
  bool bStateAlive = true;
  FCrowdDemoRangedAttackState State;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoFireIntent
{
  uint64 ProjectileId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdStableEntityRef Instigator;
  FCrowdStableEntityRef Target;
  int32 FireSequence = 0;
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;

  bool IsValid() const;
};

struct FCrowdDemoRangedAttackPlanSummary
{
  bool bValid = false;
  int32 TargetAcquiredCount = 0;
  int32 CompletedWindupCount = 0;
  int32 InvalidTargetLifecycleCount = 0;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoRangedAttackPlanner
{
public:
  static bool Advance(
    int32 RoundId,
    int32 FixedStepIndex,
    const FCrowdDemoRangedAttackSettings& Settings,
    TArray<FCrowdDemoRangedAttackAgent>& InOutAgents,
    TArray<FCrowdDemoFireIntent>& OutFireIntents,
    FCrowdDemoRangedAttackPlanSummary& OutSummary);
};
