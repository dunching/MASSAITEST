#include "Mass/CrowdDemoAttackHostAdapter.h"

#include "Mass/CrowdDemoProjectileAdapters.h"

namespace CrowdDemoAttackHostAdapterPrivate
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;
  constexpr float GridCellSizeCm = 256.0f;

  void Fold(uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= FnvPrime;
    }
  }

  bool ImpactLess(
    const FCrowdImpactFact& A,
    const FCrowdImpactFact& B)
  {
    if (A.TimeOfImpactQ != B.TimeOfImpactQ)
      return A.TimeOfImpactQ < B.TimeOfImpactQ;
    if (A.ImpactTypeId != B.ImpactTypeId)
      return A.ImpactTypeId < B.ImpactTypeId;
    return A.ImpactId < B.ImpactId;
  }

  bool RequestLess(
    const FCrowdProjectileSpawnRequest& A,
    const FCrowdProjectileSpawnRequest& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    return A.FireSequence < B.FireSequence;
  }
}

using namespace CrowdDemoAttackHostAdapterPrivate;

bool FCrowdDemoPreparedAttackBoundary::IsValid() const
{
  if (!bValid || FixedStepIndex < 0 || StableHash == 0
    || MeleeIntentCount < 0 || MidRangeIntentCount < 0
    || RangedIntentCount < 0 || MissCount < 0
    || EnvironmentImpactCount < 0)
    return false;
  for (const FCrowdImpactFact& Impact : ImmediateImpacts)
  {
    if (!Impact.IsValid()
      || Impact.FixedStepIndex != FixedStepIndex)
      return false;
  }
  for (const FCrowdProjectileSpawnRequest& Request
    : ProjectileRequests)
  {
    if (!Request.IsValid()
      || Request.FixedStepIndex != FixedStepIndex)
      return false;
  }
  FCrowdDemoPreparedAttackBoundary Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

bool FCrowdDemoPreparedAttackHealthPatch::IsValid() const
{
  if (!bValid || FixedStepIndex < 0 || StableHash == 0
    || AppliedDamageCount < 0 || DuplicateHitCount < 0
    || FriendlyFireCount < 0 || DeathCount < 0)
    return false;
  for (int32 Index = 0; Index < States.Num(); ++Index)
  {
    if (!States[Index].EntityRef.IsValid()
      || States[Index].Health < 0
      || States[Index].bAlive != (States[Index].Health > 0)
      || (Index > 0
        && !(States[Index - 1].EntityRef
          < States[Index].EntityRef)))
      return false;
  }
  FCrowdDemoPreparedAttackHealthPatch Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdDemoPreparedAttackHealthPatch::RecalculateStableHash()
{
  States.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  uint64 Hash = FnvOffset;
  Fold(Hash, static_cast<uint64>(FixedStepIndex));
  Fold(Hash, static_cast<uint64>(AppliedDamageCount));
  Fold(Hash, static_cast<uint64>(DuplicateHitCount));
  Fold(Hash, static_cast<uint64>(FriendlyFireCount));
  Fold(Hash, static_cast<uint64>(DeathCount));
  for (const FCrowdDemoAttackHealthState& State : States)
  {
    Fold(Hash, State.EntityRef.ProviderId);
    Fold(Hash, State.EntityRef.StableEntityId);
    Fold(Hash, State.EntityRef.LifecycleSerial);
    Fold(Hash, State.FactionId);
    Fold(Hash, static_cast<uint64>(State.Health));
    Fold(Hash, State.bAlive ? 1 : 0);
  }
  StableHash = Hash == 0 ? 1 : Hash;
}

void FCrowdDemoPreparedAttackBoundary::RecalculateStableHash()
{
  ImmediateImpacts.Sort(ImpactLess);
  ProjectileRequests.Sort(RequestLess);
  uint64 Hash = FnvOffset;
  Fold(Hash, static_cast<uint64>(FixedStepIndex));
  Fold(Hash, static_cast<uint64>(MeleeIntentCount));
  Fold(Hash, static_cast<uint64>(MidRangeIntentCount));
  Fold(Hash, static_cast<uint64>(RangedIntentCount));
  Fold(Hash, static_cast<uint64>(MissCount));
  Fold(Hash, static_cast<uint64>(EnvironmentImpactCount));
  Fold(Hash, static_cast<uint64>(ImmediateImpacts.Num()));
  for (const FCrowdImpactFact& Impact : ImmediateImpacts)
    Fold(Hash, Impact.StableHash);
  Fold(Hash, static_cast<uint64>(ProjectileRequests.Num()));
  for (const FCrowdProjectileSpawnRequest& Request
    : ProjectileRequests)
    Fold(Hash, Request.StableHash);
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdDemoAttackHostAdapter::Prepare(
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdDemoAttackIntent> Intents,
  const TConstArrayView<FCrowdDemoAttackTargetSnapshot> Targets,
  const TConstArrayView<FCrowdSpatialEnvironmentBody> Environment,
  FCrowdDemoPreparedAttackBoundary& OutPrepared)
{
  OutPrepared = {};
  OutPrepared.FixedStepIndex = FixedStepIndex;
  if (FixedStepIndex < 0)
    return false;

  TArray<FCrowdDemoAttackTargetSnapshot> SortedTargets(Targets);
  SortedTargets.Sort([](const auto& A, const auto& B)
  {
    return A.Body.EntityRef < B.Body.EntityRef;
  });
  TArray<FCrowdSpatialBodySnapshot> Bodies;
  Bodies.Reserve(SortedTargets.Num());
  TMap<FCrowdStableEntityRef, uint32> FactionByRef;
  for (int32 Index = 0; Index < SortedTargets.Num(); ++Index)
  {
    const FCrowdDemoAttackTargetSnapshot& Target =
      SortedTargets[Index];
    if (!Target.IsValid()
      || (Index > 0
        && SortedTargets[Index - 1].Body.EntityRef
          == Target.Body.EntityRef))
      return false;
    Bodies.Add(Target.Body);
    FactionByRef.Add(Target.Body.EntityRef, Target.FactionId);
  }
  FCrowdSpatialQueryIndex Index;
  if (!Index.Build(Bodies, GridCellSizeCm))
    return false;

  TArray<FCrowdSpatialEnvironmentBody> SortedEnvironment(Environment);
  SortedEnvironment.Sort([](const auto& A, const auto& B)
  {
    return A.StableSurfaceId < B.StableSurfaceId;
  });
  for (int32 EnvironmentIndex = 0;
    EnvironmentIndex < SortedEnvironment.Num();
    ++EnvironmentIndex)
  {
    if (!SortedEnvironment[EnvironmentIndex].IsValid()
      || (EnvironmentIndex > 0
        && SortedEnvironment[EnvironmentIndex - 1].StableSurfaceId
          == SortedEnvironment[EnvironmentIndex].StableSurfaceId))
      return false;
  }

  TArray<FCrowdDemoAttackIntent> SortedIntents(Intents);
  SortedIntents.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    return A.FireSequence < B.FireSequence;
  });
  TSet<uint64> SeenImpactIds;
  for (const FCrowdDemoAttackIntent& Intent : SortedIntents)
  {
    if (!Intent.IsValid()
      || Intent.FixedStepIndex != FixedStepIndex
      || SeenImpactIds.Contains(Intent.ImpactId))
      return false;
    SeenImpactIds.Add(Intent.ImpactId);
    if (Intent.Archetype == ECrowdDemoAttackArchetype::Ranged)
    {
      ++OutPrepared.RangedIntentCount;
      FCrowdProjectileSpawnRequest& Request =
        OutPrepared.ProjectileRequests.AddDefaulted_GetRef();
      Request.ProjectileId = Intent.ImpactId;
      Request.FixedStepIndex = Intent.FixedStepIndex;
      Request.Instigator = Intent.Instigator;
      Request.Target = Intent.Target;
      Request.FireSequence = Intent.FireSequence;
      Request.SourceFactionId = Intent.SourceFactionId;
      Request.NavLayer = Intent.NavLayer;
      Request.ProjectileProfileId =
        CrowdDemoProjectileSchemas::ProjectileProfileId;
      Request.CollisionProfileId = Intent.AttackProfileId;
      Request.EffectProfileId = Intent.EffectProfileId;
      Request.Position = Intent.Position;
      Request.Velocity =
        Intent.Direction * Intent.ProjectileSpeedCmps;
      Request.RecalculateStableHash();
      if (!Request.IsValid())
        return false;
      continue;
    }

    if (Intent.Archetype == ECrowdDemoAttackArchetype::Melee)
      ++OutPrepared.MeleeIntentCount;
    else if (Intent.Archetype
      == ECrowdDemoAttackArchetype::MidRange)
      ++OutPrepared.MidRangeIntentCount;
    else
      return false;

    const FVector QueryStart = Intent.Position;
    const FVector QueryEnd =
      QueryStart + Intent.Direction * Intent.RangeCm;
    TArray<int32> Candidates;
    if (!Index.GatherCandidates(
        QueryStart, QueryEnd, Intent.QueryRadiusCm,
        Intent.NavLayer, MAX_uint32, Candidates))
      return false;

    FCrowdSpatialSweepHit BestHit;
    bool bHasBestHit = false;
    for (const int32 CandidateIndex : Candidates)
    {
      const FCrowdSpatialBodySnapshot& Candidate =
        Index.GetBodyChecked(CandidateIndex);
      const uint32* CandidateFaction =
        FactionByRef.Find(Candidate.EntityRef);
      if (Candidate.EntityRef == Intent.Instigator
        || (Intent.SourceFactionId != 0
          && CandidateFaction && *CandidateFaction != 0
          && *CandidateFaction == Intent.SourceFactionId))
        continue;
      FCrowdSpatialSweepHit Hit;
      if (!FCrowdSpatialSweep::MovingSphere(
          QueryStart, QueryEnd, Intent.QueryRadiusCm,
          Candidate, Hit))
        continue;
      if (!bHasBestHit
        || FCrowdSpatialSweep::IsEarlierStableHit(
          Hit, BestHit))
      {
        BestHit = Hit;
        bHasBestHit = true;
      }
    }
    for (const FCrowdSpatialEnvironmentBody& Surface
      : SortedEnvironment)
    {
      if (Surface.NavLayer != Intent.NavLayer)
        continue;
      FCrowdSpatialSweepHit Hit;
      if (!FCrowdSpatialSweep::EnvironmentAabb(
          QueryStart, QueryEnd, Intent.QueryRadiusCm,
          Surface, Hit))
        continue;
      if (!bHasBestHit
        || FCrowdSpatialSweep::IsEarlierStableHit(
          Hit, BestHit))
      {
        BestHit = Hit;
        bHasBestHit = true;
      }
    }
    if (!bHasBestHit)
    {
      ++OutPrepared.MissCount;
      continue;
    }

    FCrowdImpactFact& Impact =
      OutPrepared.ImmediateImpacts.AddDefaulted_GetRef();
    Impact.ImpactId = Intent.ImpactId;
    Impact.ImpactTypeId = Intent.PayloadTypeId;
    Impact.FixedStepIndex = Intent.FixedStepIndex;
    Impact.Instigator = Intent.Instigator;
    Impact.Target = BestHit.Target;
    Impact.Position = BestHit.Position;
    Impact.Normal = BestHit.Normal;
    Impact.CollisionProfileId = BestHit.CollisionProfileId != 0
      ? BestHit.CollisionProfileId
      : Intent.AttackProfileId;
    Impact.EffectProfileId = BestHit.EffectProfileId != 0
      ? BestHit.EffectProfileId
      : Intent.EffectProfileId;
    Impact.TimeOfImpactQ = BestHit.TimeOfImpactQ;
    Impact.RecalculateStableHash();
    if (!Impact.IsValid())
      return false;
    if (!Impact.Target.IsValid())
      ++OutPrepared.EnvironmentImpactCount;
  }

  OutPrepared.bValid = true;
  OutPrepared.RecalculateStableHash();
  return OutPrepared.IsValid();
}

bool FCrowdDemoAttackHostAdapter::PrepareHealthPatch(
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdHitFact> Hits,
  const TConstArrayView<FCrowdDemoAttackHealthState>
    CurrentStates,
  FCrowdDemoPreparedAttackHealthPatch& OutPatch)
{
  OutPatch = {};
  OutPatch.FixedStepIndex = FixedStepIndex;
  if (FixedStepIndex < 0)
    return false;
  OutPatch.States = CurrentStates;
  OutPatch.States.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  TMap<FCrowdStableEntityRef, int32> StateIndexByRef;
  for (int32 Index = 0; Index < OutPatch.States.Num(); ++Index)
  {
    FCrowdDemoAttackHealthState& State = OutPatch.States[Index];
    if (!State.EntityRef.IsValid()
      || State.Health < 0
      || StateIndexByRef.Contains(State.EntityRef))
      return false;
    State.bAlive = State.Health > 0;
    StateIndexByRef.Add(State.EntityRef, Index);
  }

  TArray<FCrowdHitFact> SortedHits(Hits);
  SortedHits.Sort([](const auto& A, const auto& B)
  {
    if (A.Impact.TimeOfImpactQ != B.Impact.TimeOfImpactQ)
      return A.Impact.TimeOfImpactQ < B.Impact.TimeOfImpactQ;
    if (A.Impact.ImpactTypeId != B.Impact.ImpactTypeId)
      return A.Impact.ImpactTypeId < B.Impact.ImpactTypeId;
    return A.Impact.ImpactId < B.Impact.ImpactId;
  });
  TSet<uint64> SeenImpactIds;
  for (const FCrowdHitFact& Hit : SortedHits)
  {
    if (!Hit.IsValid()
      || Hit.Impact.FixedStepIndex != FixedStepIndex)
      return false;
    if (SeenImpactIds.Contains(Hit.Impact.ImpactId))
    {
      ++OutPatch.DuplicateHitCount;
      continue;
    }
    SeenImpactIds.Add(Hit.Impact.ImpactId);
    const int32* SourceIndex =
      StateIndexByRef.Find(Hit.Impact.Instigator);
    const int32* TargetIndex =
      StateIndexByRef.Find(Hit.Impact.Target);
    if (!SourceIndex || !TargetIndex)
      return false;
    const FCrowdDemoAttackHealthState& Source =
      OutPatch.States[*SourceIndex];
    FCrowdDemoAttackHealthState& Target =
      OutPatch.States[*TargetIndex];
    if (!Source.bAlive || !Target.bAlive)
      continue;
    if (Source.FactionId != 0
      && Source.FactionId == Target.FactionId)
    {
      ++OutPatch.FriendlyFireCount;
      continue;
    }
    FCrowdDemoProjectileHitPayload Payload;
    if (Hit.PayloadTypeId
        != CrowdDemoProjectileSchemas::HitPayloadTypeId
      || !Hit.Payload.Get(
        CrowdDemoProjectileSchemas::HitPayloadSchemaId,
        Payload)
      || !FMath::IsFinite(Payload.Damage)
      || Payload.Damage <= 0.0f)
      return false;
    const int32 Damage = FMath::Max(
      1, FMath::RoundToInt(Payload.Damage));
    const bool bWasAlive = Target.bAlive;
    Target.Health = FMath::Max(0, Target.Health - Damage);
    Target.bAlive = Target.Health > 0;
    ++OutPatch.AppliedDamageCount;
    if (bWasAlive && !Target.bAlive)
      ++OutPatch.DeathCount;
  }
  if (OutPatch.DuplicateHitCount != 0
    || OutPatch.FriendlyFireCount != 0)
    return false;
  OutPatch.bValid = true;
  OutPatch.RecalculateStableHash();
  return OutPatch.IsValid();
}
