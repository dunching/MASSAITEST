#include "MassCrowdProjectileKernel.h"

namespace
{
  constexpr uint32 ProjectileKernelFnvOffset = 2166136261u;
  constexpr uint32 ProjectileKernelFnvPrime = 16777619u;

  void FoldProjectileKernelHash(
    uint32& Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= ProjectileKernelFnvPrime;
  }

  int32 Q(const float Value, const float Quantum)
  {
    return FMath::RoundToInt(
      Value / FMath::Max(Quantum, SMALL_NUMBER));
  }

  FVector QuantizeVector(
    const FVector& Value, const float Quantum)
  {
    return FVector(
      Q(Value.X, Quantum),
      Q(Value.Y, Quantum),
      Q(Value.Z, Quantum)) * Quantum;
  }

  bool StateLess(
    const FCrowdProjectileState& A,
    const FCrowdProjectileState& B)
  {
    return A.ProjectileId < B.ProjectileId;
  }

  bool EventLess(
    const FCrowdProjectileLifecycleEvent& A,
    const FCrowdProjectileLifecycleEvent& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.ProjectileId != B.ProjectileId)
      return A.ProjectileId < B.ProjectileId;
    return static_cast<uint8>(A.Kind)
      < static_cast<uint8>(B.Kind);
  }

  void AddEvent(
    const ECrowdProjectileLifecycleEventKind Kind,
    const FCrowdProjectileState& Projectile,
    const int64 FixedStepIndex,
    const float ServerTimeSeconds,
    TArray<FCrowdProjectileLifecycleEvent>& OutEvents)
  {
    FCrowdProjectileLifecycleEvent& Event =
      OutEvents.AddDefaulted_GetRef();
    Event.Kind = Kind;
    Event.ProjectileId = Projectile.ProjectileId;
    Event.FixedStepIndex = FixedStepIndex;
    Event.ServerTimeSeconds = ServerTimeSeconds;
    Event.Position = Projectile.Position;
    Event.Velocity = Projectile.Velocity;
    Event.RadiusCm = Projectile.RadiusCm;
  }

  bool BuildProfileMap(
    const TConstArrayView<FCrowdProjectileProfile> Profiles,
    TMap<uint32, const FCrowdProjectileProfile*>& OutProfiles)
  {
    OutProfiles.Reset();
    for (const FCrowdProjectileProfile& Profile : Profiles)
    {
      if (!Profile.IsValid()
        || OutProfiles.Contains(Profile.ProfileId))
        return false;
      OutProfiles.Add(Profile.ProfileId, &Profile);
    }
    return !OutProfiles.IsEmpty();
  }
}

bool FCrowdProjectileKernel::Spawn(
  const int64 FixedStepIndex,
  const float ServerTimeSeconds,
  const TConstArrayView<FCrowdProjectileProfile> Profiles,
  const TConstArrayView<FCrowdProjectileSpawnRequest> Requests,
  TArray<FCrowdProjectileState>& InOutProjectiles,
  TArray<FCrowdProjectileLifecycleEvent>& OutEvents,
  FCrowdProjectileStepSummary& InOutSummary)
{
  TMap<uint32, const FCrowdProjectileProfile*> ProfilesById;
  if (FixedStepIndex < 0 || !FMath::IsFinite(ServerTimeSeconds)
    || !BuildProfileMap(Profiles, ProfilesById))
    return false;
  InOutProjectiles.Sort(StateLess);
  for (int32 Index = 0; Index < InOutProjectiles.Num(); ++Index)
  {
    if (!InOutProjectiles[Index].IsValid()
      || (Index > 0
        && InOutProjectiles[Index - 1].ProjectileId
          == InOutProjectiles[Index].ProjectileId))
      return false;
  }
  TArray<FCrowdProjectileSpawnRequest> Sorted(Requests);
  Sorted.Sort([](
    const FCrowdProjectileSpawnRequest& A,
    const FCrowdProjectileSpawnRequest& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    if (A.FireSequence != B.FireSequence)
      return A.FireSequence < B.FireSequence;
    return A.ProjectileId < B.ProjectileId;
  });
  for (const FCrowdProjectileSpawnRequest& Request : Sorted)
  {
    const FCrowdProjectileProfile* const* Profile =
      ProfilesById.Find(Request.ProjectileProfileId);
    if (!Request.IsValid()
      || Request.FixedStepIndex != FixedStepIndex || !Profile)
    {
      ++InOutSummary.InvalidProjectileCount;
      InOutSummary.bValid = false;
      return false;
    }
    if (InOutProjectiles.ContainsByPredicate(
      [&Request](const FCrowdProjectileState& Projectile)
      {
        return Projectile.ProjectileId == Request.ProjectileId;
      }))
    {
      ++InOutSummary.DuplicateFireCount;
      continue;
    }
    int32 ActiveCount = 0;
    for (const FCrowdProjectileState& Projectile : InOutProjectiles)
      ActiveCount += Projectile.bActive ? 1 : 0;
    if (ActiveCount >= (*Profile)->MaxActiveProjectiles)
    {
      ++InOutSummary.InvalidProjectileCount;
      InOutSummary.bValid = false;
      return false;
    }
    FCrowdProjectileState& Projectile =
      InOutProjectiles.AddDefaulted_GetRef();
    Projectile.ProjectileId = Request.ProjectileId;
    Projectile.Instigator = Request.Instigator;
    Projectile.Target = Request.Target;
    Projectile.FireSequence = Request.FireSequence;
    Projectile.SpawnFixedStep = FixedStepIndex;
    Projectile.RemainingPierces = (*Profile)->PierceCount;
    Projectile.SourceFactionId = Request.SourceFactionId;
    Projectile.NavLayer = Request.NavLayer;
    Projectile.ProjectileProfileId = Request.ProjectileProfileId;
    Projectile.CollisionProfileId = Request.CollisionProfileId;
    Projectile.EffectProfileId = Request.EffectProfileId;
    Projectile.PreviousPosition = QuantizeVector(
      Request.Position, (*Profile)->PositionQuantumCm);
    Projectile.Position = Projectile.PreviousPosition;
    Projectile.Velocity = QuantizeVector(
      Request.Velocity, (*Profile)->VelocityQuantumCmps);
    Projectile.RadiusCm = (*Profile)->RadiusCm;
    Projectile.bActive = true;
    AddEvent(
      ECrowdProjectileLifecycleEventKind::Spawn,
      Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
    ++InOutSummary.SpawnedCount;
  }
  InOutProjectiles.Sort(StateLess);
  return true;
}

bool FCrowdProjectileKernel::Advance(
  const int64 FixedStepIndex,
  const float ServerTimeSeconds,
  const float FixedStepSeconds,
  const TConstArrayView<FCrowdProjectileProfile> Profiles,
  const TConstArrayView<FCrowdProjectileTargetSnapshot> Targets,
  const TConstArrayView<FCrowdSpatialEnvironmentBody> EnvironmentBodies,
  TArray<FCrowdProjectileState>& InOutProjectiles,
  TArray<FCrowdImpactFact>& OutImpacts,
  TArray<FCrowdProjectileLifecycleEvent>& OutEvents,
  FCrowdProjectileStepSummary& InOutSummary)
{
  TMap<uint32, const FCrowdProjectileProfile*> ProfilesById;
  if (FixedStepIndex < 0 || !FMath::IsFinite(ServerTimeSeconds)
    || !FMath::IsFinite(FixedStepSeconds)
    || FixedStepSeconds <= 0.0f
    || !BuildProfileMap(Profiles, ProfilesById))
    return false;

  TArray<FCrowdProjectileTargetSnapshot> SortedTargets(Targets);
  SortedTargets.Sort([](
    const FCrowdProjectileTargetSnapshot& A,
    const FCrowdProjectileTargetSnapshot& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  TArray<FCrowdSpatialBodySnapshot> SpatialBodies;
  SpatialBodies.Reserve(SortedTargets.Num());
  for (int32 Index = 0; Index < SortedTargets.Num(); ++Index)
  {
    const FCrowdProjectileTargetSnapshot& Target =
      SortedTargets[Index];
    if (!Target.IsValid()
      || (Index > 0
        && SortedTargets[Index - 1].EntityRef == Target.EntityRef))
      return false;
    FCrowdSpatialBodySnapshot& Body =
      SpatialBodies.AddDefaulted_GetRef();
    Body.EntityRef = Target.EntityRef;
    Body.StartPosition = Target.PreviousPosition;
    Body.EndPosition = Target.Position;
    Body.RadiusCm = Target.RadiusCm;
    Body.NavLayer = Target.NavLayer;
    Body.CollisionMask = Target.CollisionMask;
    Body.QueryMask = Target.QueryMask;
    Body.RecalculateStableHash();
  }

  TArray<FCrowdSpatialEnvironmentBody> SortedEnvironment(
    EnvironmentBodies);
  SortedEnvironment.Sort([](
    const FCrowdSpatialEnvironmentBody& A,
    const FCrowdSpatialEnvironmentBody& B)
  {
    return A.StableSurfaceId < B.StableSurfaceId;
  });
  for (int32 Index = 0; Index < SortedEnvironment.Num(); ++Index)
  {
    if (!SortedEnvironment[Index].IsValid()
      || (Index > 0
        && SortedEnvironment[Index - 1].StableSurfaceId
          == SortedEnvironment[Index].StableSurfaceId))
      return false;
  }

  TMap<uint32, TUniquePtr<FCrowdSpatialQueryIndex>> IndicesByProfile;
  for (const FCrowdProjectileProfile& Profile : Profiles)
  {
    TUniquePtr<FCrowdSpatialQueryIndex> Index =
      MakeUnique<FCrowdSpatialQueryIndex>();
    if (!Index->Build(SpatialBodies, Profile.GridCellSizeCm))
      return false;
    IndicesByProfile.Add(Profile.ProfileId, MoveTemp(Index));
  }

  InOutProjectiles.Sort(StateLess);
  TSet<uint64> SeenProjectileIds;
  for (FCrowdProjectileState& Projectile : InOutProjectiles)
  {
    if (!Projectile.IsValid()
      || SeenProjectileIds.Contains(Projectile.ProjectileId))
      return false;
    SeenProjectileIds.Add(Projectile.ProjectileId);
    if (!Projectile.bActive)
      continue;
    const FCrowdProjectileProfile* const* Profile =
      ProfilesById.Find(Projectile.ProjectileProfileId);
    const TUniquePtr<FCrowdSpatialQueryIndex>* SpatialIndex =
      IndicesByProfile.Find(Projectile.ProjectileProfileId);
    if (!Profile || !SpatialIndex)
      return false;

    Projectile.PreviousPosition = Projectile.Position;
    const FVector Proposed = QuantizeVector(
      Projectile.Position
        + Projectile.Velocity * FixedStepSeconds,
      (*Profile)->PositionQuantumCm);
    bool bHasBestHit = false;
    FCrowdSpatialSweepHit BestHit;
    const FCrowdProjectileTargetSnapshot* BestTarget = nullptr;
    TArray<int32> Candidates;
    if (!(*SpatialIndex)->GatherCandidates(
      Projectile.PreviousPosition, Proposed,
      Projectile.RadiusCm, Projectile.NavLayer,
      (*Profile)->QueryMask, Candidates))
      return false;
    InOutSummary.BroadphaseCandidateCount += Candidates.Num();
    for (const int32 CandidateIndex : Candidates)
    {
      const FCrowdSpatialBodySnapshot& Body =
        (*SpatialIndex)->GetBodyChecked(CandidateIndex);
      const FCrowdProjectileTargetSnapshot& Target =
        SortedTargets[CandidateIndex];
      if (!Target.bAlive
        || Target.EntityRef == Projectile.Instigator
        || Target.EntityRef == Projectile.LastHitTarget
        || Target.NavLayer != Projectile.NavLayer
        || (Projectile.SourceFactionId != 0
          && Target.FactionId == Projectile.SourceFactionId)
        || (Target.QueryMask & (*Profile)->CollisionMask) == 0)
        continue;
      ++InOutSummary.SweepTestCount;
      FCrowdSpatialSweepHit Hit;
      if (!FCrowdSpatialSweep::MovingSphere(
        Projectile.PreviousPosition, Proposed,
        Projectile.RadiusCm, Body, Hit))
        continue;
      if (!bHasBestHit
        || FCrowdSpatialSweep::IsEarlierStableHit(Hit, BestHit))
      {
        bHasBestHit = true;
        BestHit = Hit;
        BestTarget = &Target;
      }
    }
    for (const FCrowdSpatialEnvironmentBody& Environment
      : SortedEnvironment)
    {
      if (Environment.NavLayer != Projectile.NavLayer
        || (Environment.CollisionMask & (*Profile)->QueryMask) == 0
        || (Environment.QueryMask & (*Profile)->CollisionMask) == 0)
        continue;
      ++InOutSummary.SweepTestCount;
      FCrowdSpatialSweepHit Hit;
      if (!FCrowdSpatialSweep::EnvironmentAabb(
        Projectile.PreviousPosition, Proposed,
        Projectile.RadiusCm, Environment, Hit))
        continue;
      if (!bHasBestHit
        || FCrowdSpatialSweep::IsEarlierStableHit(Hit, BestHit))
      {
        bHasBestHit = true;
        BestHit = Hit;
        BestTarget = nullptr;
      }
    }

    ++Projectile.AgeFixedSteps;
    if (bHasBestHit)
    {
      Projectile.Position = QuantizeVector(
        BestHit.Position, (*Profile)->PositionQuantumCm);
      const bool bContinuesAfterImpact =
        BestTarget && Projectile.RemainingPierces > 0;
      Projectile.bActive = bContinuesAfterImpact;
      Projectile.bImpacted = !bContinuesAfterImpact;
      if (bContinuesAfterImpact)
      {
        --Projectile.RemainingPierces;
        Projectile.LastHitTarget = BestTarget->EntityRef;
        Projectile.Position +=
          Projectile.Velocity.GetSafeNormal();
      }
      FCrowdImpactFact& Impact =
        OutImpacts.AddDefaulted_GetRef();
      Impact.ImpactId = Projectile.ProjectileId;
      Impact.ImpactTypeId = CrowdImpactTypeIds::Projectile;
      Impact.FixedStepIndex = FixedStepIndex;
      Impact.Instigator = Projectile.Instigator;
      if (BestTarget)
        Impact.Target = BestTarget->EntityRef;
      Impact.Position = Projectile.Position;
      Impact.Normal = BestHit.Normal;
      Impact.CollisionProfileId = BestTarget
        ? Projectile.CollisionProfileId
        : BestHit.CollisionProfileId;
      Impact.EffectProfileId = BestTarget
        ? Projectile.EffectProfileId
        : BestHit.EffectProfileId;
      Impact.TimeOfImpactQ = BestHit.TimeOfImpactQ;
      Impact.RecalculateStableHash();
      AddEvent(
        ECrowdProjectileLifecycleEventKind::Impact,
        Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
      ++InOutSummary.ImpactedCount;
      if (!BestTarget)
      {
        ++InOutSummary.EnvironmentImpactCount;
      }
    }
    else if (Projectile.AgeFixedSteps
      >= (*Profile)->LifetimeFixedSteps)
    {
      Projectile.Position = Proposed;
      Projectile.bActive = false;
      Projectile.bExpired = true;
      AddEvent(
        ECrowdProjectileLifecycleEventKind::Expire,
        Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
      ++InOutSummary.ExpiredCount;
    }
    else
    {
      Projectile.Position = Proposed;
    }
  }

  OutImpacts.Sort([](
    const FCrowdImpactFact& A,
    const FCrowdImpactFact& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.TimeOfImpactQ != B.TimeOfImpactQ)
      return A.TimeOfImpactQ < B.TimeOfImpactQ;
    if (A.Target != B.Target)
      return A.Target < B.Target;
    return A.ImpactId < B.ImpactId;
  });
  OutEvents.Sort(EventLess);
  InOutSummary.ActiveCount = 0;
  for (const FCrowdProjectileState& Projectile : InOutProjectiles)
    InOutSummary.ActiveCount += Projectile.bActive ? 1 : 0;
  InOutSummary.ProjectileStateHash = HashStates(InOutProjectiles);
  InOutSummary.EventHash = HashEvents(OutEvents);
  return true;
}

uint32 FCrowdProjectileKernel::HashStates(
  const TConstArrayView<FCrowdProjectileState> Projectiles)
{
  TArray<FCrowdProjectileState> Sorted(Projectiles);
  Sorted.Sort(StateLess);
  uint32 Hash = ProjectileKernelFnvOffset;
  for (const FCrowdProjectileState& Projectile : Sorted)
  {
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Projectile.ProjectileId));
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Projectile.ProjectileId >> 32));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(
      Projectile.Instigator.StableEntityId));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(
      Projectile.Target.StableEntityId));
    FoldProjectileKernelHash(Hash, Projectile.FireSequence);
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Projectile.AgeFixedSteps));
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Projectile.RemainingPierces));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(
      Projectile.LastHitTarget.StableEntityId));
    FoldProjectileKernelHash(Hash, Projectile.SourceFactionId);
    FoldProjectileKernelHash(Hash, Projectile.NavLayer);
    FoldProjectileKernelHash(Hash, Projectile.CollisionProfileId);
    FoldProjectileKernelHash(Hash, Projectile.EffectProfileId);
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Position.X), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Position.Y), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Position.Z), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Velocity.X), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Velocity.Y), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Projectile.Velocity.Z), 1.0f)));
    FoldProjectileKernelHash(Hash, Projectile.bActive ? 1u : 0u);
    FoldProjectileKernelHash(Hash, Projectile.bImpacted ? 1u : 0u);
    FoldProjectileKernelHash(Hash, Projectile.bExpired ? 1u : 0u);
  }
  return Hash;
}

uint32 FCrowdProjectileKernel::HashEvents(
  const TConstArrayView<FCrowdProjectileLifecycleEvent> Events)
{
  TArray<FCrowdProjectileLifecycleEvent> Sorted(Events);
  Sorted.Sort(EventLess);
  uint32 Hash = ProjectileKernelFnvOffset;
  for (const FCrowdProjectileLifecycleEvent& Event : Sorted)
  {
    FoldProjectileKernelHash(
      Hash, static_cast<uint8>(Event.Kind));
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Event.ProjectileId));
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Event.ProjectileId >> 32));
    FoldProjectileKernelHash(
      Hash, static_cast<uint32>(Event.FixedStepIndex));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Event.Position.X), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Event.Position.Y), 1.0f)));
    FoldProjectileKernelHash(Hash, static_cast<uint32>(Q(
      static_cast<float>(Event.Position.Z), 1.0f)));
  }
  return Hash;
}
