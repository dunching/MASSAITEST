#include "MassCrowdCombatResolver.h"

namespace
{
  constexpr uint64 CombatResolverFnvOffset =
    14695981039346656037ull;
  constexpr uint64 CombatResolverFnvPrime = 1099511628211ull;

  void FoldCombatResolverHash(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= CombatResolverFnvPrime;
    }
  }

  bool HitLess(const FCrowdHitFact& A, const FCrowdHitFact& B)
  {
    if (A.Impact.FixedStepIndex != B.Impact.FixedStepIndex)
      return A.Impact.FixedStepIndex < B.Impact.FixedStepIndex;
    if (A.Impact.TimeOfImpactQ != B.Impact.TimeOfImpactQ)
      return A.Impact.TimeOfImpactQ < B.Impact.TimeOfImpactQ;
    if (A.Impact.Target != B.Impact.Target)
      return A.Impact.Target < B.Impact.Target;
    if (A.Impact.ImpactTypeId != B.Impact.ImpactTypeId)
      return A.Impact.ImpactTypeId < B.Impact.ImpactTypeId;
    return A.Impact.ImpactId < B.Impact.ImpactId;
  }
}

bool FCrowdEffectProfile::IsValid() const
{
  if (EffectProfileId == 0 || PayloadTypeId == 0
    || !Payload.IsValid() || StableHash == 0)
    return false;
  FCrowdEffectProfile Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdEffectProfile::RecalculateStableHash()
{
  uint64 Hash = CombatResolverFnvOffset;
  FoldCombatResolverHash(Hash, EffectProfileId);
  FoldCombatResolverHash(Hash, PayloadTypeId);
  FoldCombatResolverHash(Hash, Payload.CalculateStableHash());
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdHitResolveResult::IsValid() const
{
  if (FixedStepIndex < 0 || StableHash == 0)
    return false;
  for (const FCrowdHitFact& Hit : Hits)
  {
    if (!Hit.IsValid()
      || Hit.Impact.FixedStepIndex != FixedStepIndex)
      return false;
  }
  FCrowdHitResolveResult Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdHitResolveResult::RecalculateStableHash()
{
  Hits.Sort(HitLess);
  uint64 Hash = CombatResolverFnvOffset;
  FoldCombatResolverHash(Hash, static_cast<uint64>(FixedStepIndex));
  FoldCombatResolverHash(
    Hash, static_cast<uint64>(EnvironmentImpactCount));
  FoldCombatResolverHash(Hash, static_cast<uint64>(Hits.Num()));
  for (const FCrowdHitFact& Hit : Hits)
    FoldCombatResolverHash(Hash, Hit.StableHash);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdCombatResolver::Resolve(
  const TConstArrayView<FCrowdImpactFact> Impacts,
  const TConstArrayView<FCrowdEffectProfile> Profiles,
  FCrowdHitResolveResult& OutResult)
{
  OutResult = {};
  TArray<FCrowdEffectProfile> SortedProfiles(Profiles);
  SortedProfiles.Sort([](
    const FCrowdEffectProfile& A,
    const FCrowdEffectProfile& B)
  {
    return A.EffectProfileId < B.EffectProfileId;
  });
  TMap<uint32, const FCrowdEffectProfile*> ProfilesById;
  for (const FCrowdEffectProfile& Profile : SortedProfiles)
  {
    if (!Profile.IsValid()
      || ProfilesById.Contains(Profile.EffectProfileId))
      return false;
    ProfilesById.Add(Profile.EffectProfileId, &Profile);
  }

  TArray<FCrowdImpactFact> SortedImpacts(Impacts);
  SortedImpacts.Sort([](
    const FCrowdImpactFact& A,
    const FCrowdImpactFact& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.TimeOfImpactQ != B.TimeOfImpactQ)
      return A.TimeOfImpactQ < B.TimeOfImpactQ;
    if (A.Target != B.Target)
      return A.Target < B.Target;
    if (A.ImpactTypeId != B.ImpactTypeId)
      return A.ImpactTypeId < B.ImpactTypeId;
    return A.ImpactId < B.ImpactId;
  });
  TSet<uint64> SeenImpactHashes;
  for (const FCrowdImpactFact& Impact : SortedImpacts)
  {
    if (!Impact.IsValid()
      || SeenImpactHashes.Contains(Impact.StableHash))
      return false;
    SeenImpactHashes.Add(Impact.StableHash);
    if (OutResult.FixedStepIndex == INDEX_NONE)
      OutResult.FixedStepIndex = Impact.FixedStepIndex;
    if (Impact.FixedStepIndex != OutResult.FixedStepIndex)
      return false;
    if (!Impact.Target.IsValid())
    {
      ++OutResult.EnvironmentImpactCount;
      continue;
    }
    const FCrowdEffectProfile* const* Profile =
      ProfilesById.Find(Impact.EffectProfileId);
    if (!Profile)
      return false;
    FCrowdHitFact& Hit = OutResult.Hits.AddDefaulted_GetRef();
    Hit.Impact = Impact;
    Hit.PayloadTypeId = (*Profile)->PayloadTypeId;
    Hit.Payload = (*Profile)->Payload;
    Hit.RecalculateStableHash();
  }
  if (OutResult.FixedStepIndex == INDEX_NONE)
    OutResult.FixedStepIndex = 0;
  OutResult.RecalculateStableHash();
  return true;
}

bool FCrowdPreparedHostHitCommit::IsValid() const
{
  if (FixedStepIndex < 0 || SourceResolveHash == 0
    || StableHash == 0)
    return false;
  for (const FCrowdHitFact& Hit : Hits)
  {
    if (!Hit.IsValid()
      || Hit.Impact.FixedStepIndex != FixedStepIndex)
      return false;
  }
  FCrowdPreparedHostHitCommit Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdPreparedHostHitCommit::RecalculateStableHash()
{
  Hits.Sort(HitLess);
  uint64 Hash = CombatResolverFnvOffset;
  FoldCombatResolverHash(Hash, static_cast<uint64>(FixedStepIndex));
  FoldCombatResolverHash(Hash, SourceResolveHash);
  FoldCombatResolverHash(Hash, static_cast<uint64>(Hits.Num()));
  for (const FCrowdHitFact& Hit : Hits)
    FoldCombatResolverHash(Hash, Hit.StableHash);
  StableHash = Hash == 0 ? 1 : Hash;
}
