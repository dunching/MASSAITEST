#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSource.h"

struct MASSCROWDRUNTIME_API FCrowdProjectileEnvironmentBody
{
  uint64 StableSurfaceId = 0;
  uint32 NavLayer = 0;
  FVector BoundsMin = FVector::ZeroVector;
  FVector BoundsMax = FVector::ZeroVector;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDRUNTIME_API FCrowdImpactFact
{
  uint64 ProjectileId = 0;
  int64 FixedStepIndex = INDEX_NONE;
  FCrowdStableEntityRef Instigator;
  FCrowdStableEntityRef Target;
  FVector Position = FVector::ZeroVector;
  FVector Normal = FVector::UpVector;
  uint32 CollisionProfileId = 0;
  uint32 EffectProfileId = 0;
  uint32 TimeOfImpactQ = 0;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

struct MASSCROWDRUNTIME_API FCrowdHitFact
{
  FCrowdImpactFact Impact;
  uint32 PayloadTypeId = 0;
  FCrowdBehaviorSourcePayload Payload;
  uint64 StableHash = 0;

  bool IsValid() const;
  void RecalculateStableHash();
};

// Host-owned implementation translates neutral impact facts into validated
// hit facts. WORK never calls host gameplay or presentation objects directly.
class MASSCROWDRUNTIME_API ICrowdHostHitResolver
{
public:
  virtual ~ICrowdHostHitResolver() = default;

  virtual bool Resolve(
    TConstArrayView<FCrowdImpactFact> Impacts,
    TArray<FCrowdHitFact>& OutHits) const = 0;
};

class MASSCROWDRUNTIME_API ICrowdEnvironmentCollisionSnapshotProvider
{
public:
  virtual ~ICrowdEnvironmentCollisionSnapshotProvider() = default;

  virtual bool Gather(
    int64 FixedStepIndex,
    TArray<FCrowdProjectileEnvironmentBody>& OutBodies) const = 0;
};
