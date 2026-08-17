#include "MassCrowdProjectileTypes.h"

namespace
{
  constexpr uint64 ProjectileTypesFnvOffset =
    14695981039346656037ull;
  constexpr uint64 ProjectileTypesFnvPrime = 1099511628211ull;

  bool IsFiniteProjectileVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  void FoldProjectileTypeHash(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= ProjectileTypesFnvPrime;
    }
  }

  void FoldProjectileTypeRef(
    uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldProjectileTypeHash(Hash, Ref.ProviderId);
    FoldProjectileTypeHash(Hash, Ref.StableEntityId);
    FoldProjectileTypeHash(Hash, Ref.LifecycleSerial);
  }

  void FoldProjectileTypeVector(
    uint64& Hash, const FVector& Value)
  {
    FoldProjectileTypeHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.X * 100.0)));
    FoldProjectileTypeHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Y * 100.0)));
    FoldProjectileTypeHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Z * 100.0)));
  }
}

bool FCrowdProjectileProfile::IsValid() const
{
  if (ProfileId == 0 || !FMath::IsFinite(RadiusCm)
    || RadiusCm <= 0.0f || LifetimeFixedSteps <= 0
    || PierceCount < 0 || MaxActiveProjectiles <= 0
    || !FMath::IsFinite(PositionQuantumCm)
    || PositionQuantumCm <= 0.0f
    || !FMath::IsFinite(VelocityQuantumCmps)
    || VelocityQuantumCmps <= 0.0f
    || !FMath::IsFinite(GridCellSizeCm)
    || GridCellSizeCm <= 0.0f
    || CollisionMask == 0 || QueryMask == 0
    || StableHash == 0)
    return false;
  FCrowdProjectileProfile Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdProjectileProfile::RecalculateStableHash()
{
  uint64 Hash = ProjectileTypesFnvOffset;
  FoldProjectileTypeHash(Hash, ProfileId);
  FoldProjectileTypeHash(Hash, static_cast<uint64>(
    FMath::RoundToInt64(RadiusCm * 100.0)));
  FoldProjectileTypeHash(
    Hash, static_cast<uint64>(LifetimeFixedSteps));
  FoldProjectileTypeHash(Hash, static_cast<uint64>(PierceCount));
  FoldProjectileTypeHash(
    Hash, static_cast<uint64>(MaxActiveProjectiles));
  FoldProjectileTypeHash(Hash, static_cast<uint64>(
    FMath::RoundToInt64(PositionQuantumCm * 1000000.0)));
  FoldProjectileTypeHash(Hash, static_cast<uint64>(
    FMath::RoundToInt64(VelocityQuantumCmps * 1000000.0)));
  FoldProjectileTypeHash(Hash, static_cast<uint64>(
    FMath::RoundToInt64(GridCellSizeCm * 100.0)));
  FoldProjectileTypeHash(Hash, CollisionMask);
  FoldProjectileTypeHash(Hash, QueryMask);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdProjectileSpawnRequest::IsValid() const
{
  if (ProjectileId == 0 || FixedStepIndex < 0
    || !Instigator.IsValid()
    || (!Target.IsUnset() && !Target.IsValid())
    || FireSequence == 0 || ProjectileProfileId == 0
    || CollisionProfileId == 0 || EffectProfileId == 0
    || !IsFiniteProjectileVector(Position)
    || !IsFiniteProjectileVector(Velocity)
    || Velocity.IsNearlyZero() || StableHash == 0)
    return false;
  FCrowdProjectileSpawnRequest Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdProjectileSpawnRequest::RecalculateStableHash()
{
  uint64 Hash = ProjectileTypesFnvOffset;
  FoldProjectileTypeHash(Hash, ProjectileId);
  FoldProjectileTypeHash(Hash, static_cast<uint64>(FixedStepIndex));
  FoldProjectileTypeRef(Hash, Instigator);
  FoldProjectileTypeRef(Hash, Target);
  FoldProjectileTypeHash(Hash, FireSequence);
  FoldProjectileTypeHash(Hash, SourceFactionId);
  FoldProjectileTypeHash(Hash, NavLayer);
  FoldProjectileTypeHash(Hash, ProjectileProfileId);
  FoldProjectileTypeHash(Hash, CollisionProfileId);
  FoldProjectileTypeHash(Hash, EffectProfileId);
  FoldProjectileTypeVector(Hash, Position);
  FoldProjectileTypeVector(Hash, Velocity);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdProjectileTargetSnapshot::IsValid() const
{
  if (!EntityRef.IsValid()
    || !IsFiniteProjectileVector(PreviousPosition)
    || !IsFiniteProjectileVector(Position)
    || !FMath::IsFinite(RadiusCm) || RadiusCm < 0.0f
    || CollisionMask == 0 || QueryMask == 0 || StableHash == 0)
    return false;
  FCrowdProjectileTargetSnapshot Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdProjectileTargetSnapshot::RecalculateStableHash()
{
  uint64 Hash = ProjectileTypesFnvOffset;
  FoldProjectileTypeRef(Hash, EntityRef);
  FoldProjectileTypeHash(Hash, FactionId);
  FoldProjectileTypeHash(Hash, NavLayer);
  FoldProjectileTypeVector(Hash, PreviousPosition);
  FoldProjectileTypeVector(Hash, Position);
  FoldProjectileTypeHash(Hash, static_cast<uint64>(
    FMath::RoundToInt64(RadiusCm * 100.0)));
  FoldProjectileTypeHash(Hash, CollisionMask);
  FoldProjectileTypeHash(Hash, QueryMask);
  FoldProjectileTypeHash(Hash, bAlive ? 1 : 0);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdProjectileState::IsValid() const
{
  return ProjectileId != 0 && Instigator.IsValid()
    && (Target.IsUnset() || Target.IsValid())
    && FireSequence != 0 && SpawnFixedStep >= 0
    && AgeFixedSteps >= 0 && RemainingPierces >= 0
    && ProjectileProfileId != 0 && CollisionProfileId != 0
    && EffectProfileId != 0
    && IsFiniteProjectileVector(PreviousPosition)
    && IsFiniteProjectileVector(Position)
    && IsFiniteProjectileVector(Velocity)
    && !Velocity.IsNearlyZero()
    && FMath::IsFinite(RadiusCm) && RadiusCm > 0.0f;
}

bool FCrowdProjectileLifecycleEvent::IsValid() const
{
  return Kind <= ECrowdProjectileLifecycleEventKind::Expire
    && ProjectileId != 0 && FixedStepIndex >= 0
    && FMath::IsFinite(ServerTimeSeconds)
    && IsFiniteProjectileVector(Position)
    && IsFiniteProjectileVector(Velocity)
    && FMath::IsFinite(RadiusCm) && RadiusCm > 0.0f;
}
