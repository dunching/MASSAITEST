#include "MassCrowdProjectileFacts.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  void Fold(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= FnvPrime;
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    Fold(Hash, Ref.ProviderId);
    Fold(Hash, Ref.StableEntityId);
    Fold(Hash, Ref.LifecycleSerial);
  }

  void FoldVector(uint64& Hash, const FVector& Value)
  {
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.X * 100.0)));
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Y * 100.0)));
    Fold(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Z * 100.0)));
  }
}

bool FCrowdProjectileEnvironmentBody::IsValid() const
{
  if (StableSurfaceId == 0 || BoundsMin.ContainsNaN()
    || BoundsMax.ContainsNaN()
    || BoundsMin.X > BoundsMax.X
    || BoundsMin.Y > BoundsMax.Y
    || BoundsMin.Z > BoundsMax.Z
    || CollisionProfileId == 0 || EffectProfileId == 0
    || StableHash == 0)
    return false;
  FCrowdProjectileEnvironmentBody Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdProjectileEnvironmentBody::RecalculateStableHash()
{
  uint64 Hash = FnvOffset;
  Fold(Hash, StableSurfaceId);
  Fold(Hash, NavLayer);
  FoldVector(Hash, BoundsMin);
  FoldVector(Hash, BoundsMax);
  Fold(Hash, CollisionProfileId);
  Fold(Hash, EffectProfileId);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdImpactFact::IsValid() const
{
  if (ProjectileId == 0 || FixedStepIndex < 0
    || !Instigator.IsValid() || CollisionProfileId == 0
    || EffectProfileId == 0 || TimeOfImpactQ > 1000000
    || Position.ContainsNaN() || Normal.ContainsNaN()
    || Normal.IsNearlyZero() || StableHash == 0)
    return false;
  FCrowdImpactFact Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdImpactFact::RecalculateStableHash()
{
  uint64 Hash = FnvOffset;
  Fold(Hash, ProjectileId);
  Fold(Hash, static_cast<uint64>(FixedStepIndex));
  FoldRef(Hash, Instigator);
  FoldRef(Hash, Target);
  FoldVector(Hash, Position);
  FoldVector(Hash, Normal.GetSafeNormal());
  Fold(Hash, CollisionProfileId);
  Fold(Hash, EffectProfileId);
  Fold(Hash, TimeOfImpactQ);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdHitFact::IsValid() const
{
  if (!Impact.IsValid() || PayloadTypeId == 0
    || !Payload.IsValid() || StableHash == 0)
    return false;
  FCrowdHitFact Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdHitFact::RecalculateStableHash()
{
  uint64 Hash = FnvOffset;
  Fold(Hash, Impact.StableHash);
  Fold(Hash, PayloadTypeId);
  Fold(Hash, Payload.CalculateStableHash());
  StableHash = Hash == 0 ? 1 : Hash;
}
