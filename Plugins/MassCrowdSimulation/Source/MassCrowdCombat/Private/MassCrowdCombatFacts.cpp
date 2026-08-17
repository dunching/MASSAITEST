#include "MassCrowdCombatFacts.h"

namespace
{
  constexpr uint64 CombatFactsFnvOffset = 14695981039346656037ull;
  constexpr uint64 CombatFactsFnvPrime = 1099511628211ull;

  void FoldCombatFactHash(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= CombatFactsFnvPrime;
    }
  }

  void FoldCombatFactRef(
    uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldCombatFactHash(Hash, Ref.ProviderId);
    FoldCombatFactHash(Hash, Ref.StableEntityId);
    FoldCombatFactHash(Hash, Ref.LifecycleSerial);
  }

  void FoldCombatFactVector(uint64& Hash, const FVector& Value)
  {
    FoldCombatFactHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.X * 100.0)));
    FoldCombatFactHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Y * 100.0)));
    FoldCombatFactHash(Hash, static_cast<uint64>(
      FMath::RoundToInt64(Value.Z * 100.0)));
  }
}

bool FCrowdImpactFact::IsValid() const
{
  if (ImpactId == 0 || ImpactTypeId == 0 || FixedStepIndex < 0
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
  uint64 Hash = CombatFactsFnvOffset;
  FoldCombatFactHash(Hash, ImpactId);
  FoldCombatFactHash(Hash, ImpactTypeId);
  FoldCombatFactHash(Hash, static_cast<uint64>(FixedStepIndex));
  FoldCombatFactRef(Hash, Instigator);
  FoldCombatFactRef(Hash, Target);
  FoldCombatFactVector(Hash, Position);
  FoldCombatFactVector(Hash, Normal.GetSafeNormal());
  FoldCombatFactHash(Hash, CollisionProfileId);
  FoldCombatFactHash(Hash, EffectProfileId);
  FoldCombatFactHash(Hash, TimeOfImpactQ);
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
  uint64 Hash = CombatFactsFnvOffset;
  FoldCombatFactHash(Hash, Impact.StableHash);
  FoldCombatFactHash(Hash, PayloadTypeId);
  FoldCombatFactHash(Hash, Payload.CalculateStableHash());
  StableHash = Hash == 0 ? 1 : Hash;
}
