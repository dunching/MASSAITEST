#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdDemoAttackArchetype : uint8
{
  Melee = 1,
  MidRange = 2,
  Ranged = 3
};

enum class ECrowdDemoAttackPlannerPhase : uint8
{
  None = 0,
  AcquireTarget = 1,
  Windup = 2,
  Commit = 3,
  Recovery = 4,
  Cooldown = 5
};

namespace CrowdDemoAttackPayloadTypeIds
{
  inline constexpr uint32 Melee = 24001;
  inline constexpr uint32 MidRange = 24002;
  inline constexpr uint32 Ranged = 24003;
}

namespace CrowdDemoAttackProfileIds
{
  inline constexpr uint32 Melee = 25001;
  inline constexpr uint32 MidRange = 25002;
  inline constexpr uint32 Ranged = 25003;
}

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoAttackProfileV1
{
  uint32 ProfileId = 0;
  uint32 PayloadTypeId = 0;
  uint32 EffectProfileId = 0;
  ECrowdDemoAttackArchetype Archetype =
    ECrowdDemoAttackArchetype::Melee;
  int32 WindupFixedSteps = 0;
  int32 RecoveryFixedSteps = 0;
  int32 CooldownFixedSteps = 0;
  float MinimumDistanceCm = 0.0f;
  float MaximumDistanceCm = 0.0f;
  float QueryRadiusCm = 0.0f;
  float MuzzleForwardOffsetCm = 0.0f;
  float ProjectileSpeedCmps = 0.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  int32 Damage = 0;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoAttackState
{
  ECrowdDemoAttackPlannerPhase Phase =
    ECrowdDemoAttackPlannerPhase::None;
  int64 PhaseEnterFixedStep = 0;
  int64 CooldownEndFixedStep = 0;
  FCrowdStableEntityRef TargetRef;
  FCrowdStableEntityRef LockedTargetRef;
  FVector LockedTargetLocation = FVector::ZeroVector;
  uint32 Revision = 0;
  uint32 FireSequence = 0;
  bool bCommitIssued = false;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoAttackAgent
{
  FCrowdStableEntityRef EntityRef;
  FCrowdStableEntityRef PreferredTargetRef;
  uint32 FactionId = 0;
  uint32 NavLayer = 0;
  uint32 AttackProfileId = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  FVector Facing = FVector::ForwardVector;
  int32 Health = 100;
  bool bAlive = true;
  bool bCanAttack = true;
  bool bRequirePreferredTarget = false;
  FCrowdDemoAttackState State;

  bool IsValid() const;
};

struct MASSCROWDDEMOBUSINESS_API FCrowdDemoAttackIntent
{
  uint64 ImpactId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdStableEntityRef Instigator;
  FCrowdStableEntityRef Target;
  uint32 AttackProfileId = 0;
  uint32 PayloadTypeId = 0;
  uint32 EffectProfileId = 0;
  ECrowdDemoAttackArchetype Archetype =
    ECrowdDemoAttackArchetype::Melee;
  uint32 FireSequence = 0;
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  FVector Position = FVector::ZeroVector;
  FVector Direction = FVector::ForwardVector;
  FVector TargetStartPosition = FVector::ZeroVector;
  FVector TargetEndPosition = FVector::ZeroVector;
  float RangeCm = 0.0f;
  float QueryRadiusCm = 0.0f;
  float ProjectileSpeedCmps = 0.0f;
  int32 Damage = 0;

  bool IsValid() const;
};

struct FCrowdDemoAttackPlanSummary
{
  bool bValid = false;
  int32 TargetAcquiredCount = 0;
  int32 CompletedWindupCount = 0;
  int32 InvalidTargetLifecycleCount = 0;
  int32 OutOfRangeCount = 0;
};

class MASSCROWDDEMOBUSINESS_API FCrowdDemoAttackPlanner
{
public:
  static bool Advance(
    int32 RoundId,
    int64 FixedStepIndex,
    TConstArrayView<FCrowdDemoAttackProfileV1> Profiles,
    TArray<FCrowdDemoAttackAgent>& InOutAgents,
    TArray<FCrowdDemoAttackIntent>& OutIntents,
    FCrowdDemoAttackPlanSummary& OutSummary);
};
