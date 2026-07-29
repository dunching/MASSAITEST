#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdProjectileLifecycleEventKind : uint8
{
  Spawn = 0,
  Impact,
  Expire
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileProfile
{
  uint32 ProfileId = 0;
  float RadiusCm = 12.0f;
  int32 LifetimeFixedSteps = 1;
  int32 PierceCount = 0;
  int32 MaxActiveProjectiles = 4096;
  float PositionQuantumCm = 0.01f;
  float VelocityQuantumCmps = 0.01f;
  float GridCellSizeCm = 256.0f;
  uint32 CollisionMask = MAX_uint32;
  uint32 QueryMask = MAX_uint32;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileSpawnRequest
{
  uint64 ProjectileId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdStableEntityRef Instigator;
  FCrowdStableEntityRef Target;
  uint32 FireSequence = 0;
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  uint32 ProjectileProfileId = 0;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileTargetSnapshot
{
  FCrowdStableEntityRef EntityRef;
  uint32 FactionId = 0;
  uint32 NavLayer = 0;
  FVector PreviousPosition = FVector::ZeroVector;
  FVector Position = FVector::ZeroVector;
  float RadiusCm = 0.0f;
  uint32 CollisionMask = MAX_uint32;
  uint32 QueryMask = MAX_uint32;
  bool bAlive = false;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileState
{
  uint64 ProjectileId = 0;
  FCrowdStableEntityRef Instigator;
  FCrowdStableEntityRef Target;
  uint32 FireSequence = 0;
  int64 SpawnFixedStep = INDEX_NONE;
  int32 AgeFixedSteps = 0;
  int32 RemainingPierces = 0;
  FCrowdStableEntityRef LastHitTarget;
  uint32 SourceFactionId = 0;
  uint32 NavLayer = 0;
  uint32 ProjectileProfileId = 0;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  FVector PreviousPosition = FVector::ZeroVector;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float RadiusCm = 0.0f;
  bool bActive = false;
  bool bImpacted = false;
  bool bExpired = false;

  bool IsValid() const;
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileLifecycleEvent
{
  ECrowdProjectileLifecycleEventKind Kind =
    ECrowdProjectileLifecycleEventKind::Spawn;
  uint64 ProjectileId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  float ServerTimeSeconds = 0.0f;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float RadiusCm = 0.0f;

  bool IsValid() const;
};

struct MASSCROWDPROJECTILES_API FCrowdProjectileStepSummary
{
  bool bValid = true;
  int32 SpawnedCount = 0;
  int32 ActiveCount = 0;
  int32 ImpactedCount = 0;
  int32 ExpiredCount = 0;
  int32 DuplicateFireCount = 0;
  int32 InvalidProjectileCount = 0;
  int32 EnvironmentImpactCount = 0;
  int32 BroadphaseCandidateCount = 0;
  int32 SweepTestCount = 0;
  uint32 ProjectileStateHash = 2166136261u;
  uint32 EventHash = 2166136261u;
};
