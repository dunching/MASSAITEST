#include "Mass/CrowdDemoProjectileKernel.h"

#include "Algo/Sort.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;
  constexpr double ProjectileGridCellSizeCm = 256.0;

  struct FProjectileGridCell
  {
    int32 X = 0;
    int32 Y = 0;
    int32 Layer = 0;

    bool operator==(const FProjectileGridCell&) const = default;
    friend uint32 GetTypeHash(const FProjectileGridCell& Cell)
    {
      return HashCombineFast(
        HashCombineFast(::GetTypeHash(Cell.X), ::GetTypeHash(Cell.Y)),
        ::GetTypeHash(Cell.Layer));
    }
  };

  int32 GridCoordinate(const double Value)
  {
    return FMath::FloorToInt(Value / ProjectileGridCellSizeCm);
  }

  void AddSweptBoundsToGrid(
    const int32 AgentIndex,
    const FVector& Start,
    const FVector& End,
    const float Radius,
    TMap<FProjectileGridCell, TArray<int32>>& Grid)
  {
    const FVector Minimum = Start.ComponentMin(End)
      - FVector(Radius);
    const FVector Maximum = Start.ComponentMax(End)
      + FVector(Radius);
    for (int32 Layer = GridCoordinate(Minimum.Z);
      Layer <= GridCoordinate(Maximum.Z); ++Layer)
    {
      for (int32 Y = GridCoordinate(Minimum.Y);
        Y <= GridCoordinate(Maximum.Y); ++Y)
      {
        for (int32 X = GridCoordinate(Minimum.X);
          X <= GridCoordinate(Maximum.X); ++X)
          Grid.FindOrAdd({X, Y, Layer}).Add(AgentIndex);
      }
    }
  }

  void GatherGridCandidates(
    const FVector& Start,
    const FVector& End,
    const float Radius,
    const TMap<FProjectileGridCell, TArray<int32>>& Grid,
    TArray<int32>& OutCandidates)
  {
    TSet<int32> Unique;
    const FVector Minimum = Start.ComponentMin(End)
      - FVector(Radius);
    const FVector Maximum = Start.ComponentMax(End)
      + FVector(Radius);
    for (int32 Layer = GridCoordinate(Minimum.Z);
      Layer <= GridCoordinate(Maximum.Z); ++Layer)
    {
      for (int32 Y = GridCoordinate(Minimum.Y);
        Y <= GridCoordinate(Maximum.Y); ++Y)
      {
        for (int32 X = GridCoordinate(Minimum.X);
          X <= GridCoordinate(Maximum.X); ++X)
        {
          if (const TArray<int32>* Values =
            Grid.Find({X, Y, Layer}))
            for (const int32 Value : *Values)
              Unique.Add(Value);
        }
      }
    }
    OutCandidates = Unique.Array();
  }

  void Fold(uint32& Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
  }

  int32 Q(const float Value, const float Quantum)
  {
    return FMath::RoundToInt(Value / FMath::Max(Quantum, SMALL_NUMBER));
  }

  FVector QuantizeVector(const FVector& Value, const float Quantum)
  {
    return FVector(Q(Value.X, Quantum), Q(Value.Y, Quantum), Q(Value.Z, Quantum)) * Quantum;
  }

  uint64 MakeProjectileId(
    const int32 RoundId,
    const int32 SourceAgentId,
    const int32 FireSequence)
  {
    return (static_cast<uint64>(static_cast<uint32>(RoundId) & 0xffffu) << 48)
      | (static_cast<uint64>(static_cast<uint32>(SourceAgentId) & 0xffffffu) << 24)
      | static_cast<uint64>(static_cast<uint32>(FireSequence) & 0xffffffu);
  }

  const FCrowdDemoRangedCombatAgent* FindTargetByFormation(
    const TArray<FCrowdDemoRangedCombatAgent>& Agents,
    const int32 FormationIndex)
  {
    return Agents.FindByPredicate([&](const auto& Agent)
    {
      return Agent.FormationIndex == FormationIndex;
    });
  }

  bool SegmentSphereHitTime(
    const FVector& Start,
    const FVector& End,
    const FVector& Center,
    const float Radius,
    double& OutTime)
  {
    const FVector Segment = End - Start;
    const FVector Offset = Start - Center;
    const double RadiusSquared = static_cast<double>(Radius) * Radius;
    const double C = FVector::DotProduct(Offset, Offset) - RadiusSquared;
    if (C <= 0.0)
    {
      OutTime = 0.0;
      return true;
    }
    const double A = FVector::DotProduct(Segment, Segment);
    if (A <= UE_DOUBLE_SMALL_NUMBER)
      return false;
    const double B = 2.0 * FVector::DotProduct(Offset, Segment);
    const double Discriminant = B * B - 4.0 * A * C;
    if (Discriminant < 0.0)
      return false;
    const double Root = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
    if (Root < 0.0 || Root > 1.0)
      return false;
    OutTime = Root;
    return true;
  }

  bool SegmentExpandedBoxHitTime(
    const FVector& Start,
    const FVector& End,
    const FVector& BoundsMin,
    const FVector& BoundsMax,
    const float Radius,
    double& OutTime,
    FVector& OutNormal)
  {
    const FVector Minimum = BoundsMin - FVector(Radius);
    const FVector Maximum = BoundsMax + FVector(Radius);
    const FVector Delta = End - Start;
    double EnterTime = 0.0;
    double ExitTime = 1.0;
    FVector EnterNormal = FVector::ZeroVector;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
      const double Origin = Start[Axis];
      const double Direction = Delta[Axis];
      if (FMath::Abs(Direction) <= UE_DOUBLE_SMALL_NUMBER)
      {
        if (Origin < Minimum[Axis] || Origin > Maximum[Axis])
          return false;
        continue;
      }
      double NearTime = (Minimum[Axis] - Origin) / Direction;
      double FarTime = (Maximum[Axis] - Origin) / Direction;
      FVector NearNormal = FVector::ZeroVector;
      NearNormal[Axis] = Direction > 0.0 ? -1.0 : 1.0;
      if (NearTime > FarTime)
        Swap(NearTime, FarTime);
      if (NearTime > EnterTime)
      {
        EnterTime = NearTime;
        EnterNormal = NearNormal;
      }
      ExitTime = FMath::Min(ExitTime, FarTime);
      if (EnterTime > ExitTime) return false;
    }
    if (ExitTime < 0.0 || EnterTime > 1.0) return false;
    OutTime = FMath::Clamp(EnterTime, 0.0, 1.0);
    OutNormal = EnterNormal.IsNearlyZero()
      ? -Delta.GetSafeNormal() : EnterNormal;
    return !OutNormal.IsNearlyZero();
  }

  void AddVisualEvent(
    const ECrowdDemoProjectileVisualEventKind Kind,
    const FCrowdDemoProjectileState& Projectile,
    const int32 FixedStepIndex,
    const float ServerTimeSeconds,
    TArray<FCrowdDemoProjectileVisualEvent>& OutEvents)
  {
    auto& Event = OutEvents.AddDefaulted_GetRef();
    Event.Kind = Kind;
    Event.ProjectileId = Projectile.ProjectileId;
    Event.FixedStepIndex = FixedStepIndex;
    Event.ServerTimeSeconds = ServerTimeSeconds;
    Event.Position = Projectile.Position;
    Event.Velocity = Projectile.Velocity;
    Event.RadiusCm = Projectile.RadiusCm;
  }
}

bool FCrowdDemoHostHitResolver::Resolve(
  const TConstArrayView<FCrowdImpactFact> Impacts,
  TArray<FCrowdHitFact>& OutHits) const
{
  OutHits.Reset();
  if (!FCrowdDemoProjectileKernel::ValidateSettings(Settings))
    return false;
  TArray<FCrowdImpactFact> Sorted(Impacts);
  Sorted.Sort([](const FCrowdImpactFact& A, const FCrowdImpactFact& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.TimeOfImpactQ != B.TimeOfImpactQ)
      return A.TimeOfImpactQ < B.TimeOfImpactQ;
    if (A.Target != B.Target)
      return A.Target < B.Target;
    return A.ProjectileId < B.ProjectileId;
  });
  TSet<uint64> SeenImpactHashes;
  for (const FCrowdImpactFact& Impact : Sorted)
  {
    if (!Impact.IsValid()
      || SeenImpactHashes.Contains(Impact.StableHash))
      return false;
    SeenImpactHashes.Add(Impact.StableHash);
    if (!Impact.Target.IsValid())
      continue;
    FCrowdDemoProjectileHitPayload Payload;
    Payload.Damage = Settings.Damage;
    Payload.HorizontalImpulseCmps = Settings.HorizontalImpulseCmps;
    Payload.VerticalImpulseCmps = Settings.VerticalImpulseCmps;
    Payload.HitFlashProfileKey = 1;
    FCrowdHitFact& Hit = OutHits.AddDefaulted_GetRef();
    Hit.Impact = Impact;
    Hit.PayloadTypeId = CrowdDemoProjectileSchemas::HitPayloadTypeId;
    if (!Hit.Payload.Set(
        CrowdDemoProjectileSchemas::HitPayloadSchemaId, Payload))
      return false;
    Hit.RecalculateStableHash();
  }
  return true;
}

bool FCrowdDemoHostHitResolver::BuildDemoHitFacts(
  const TConstArrayView<FCrowdHitFact> Hits,
  TArray<FCrowdDemoHitFact>& OutFacts)
{
  OutFacts.Reset();
  for (const FCrowdHitFact& Hit : Hits)
  {
    FCrowdDemoProjectileHitPayload Payload;
    if (!Hit.IsValid()
      || Hit.PayloadTypeId
        != CrowdDemoProjectileSchemas::HitPayloadTypeId
      || !Hit.Payload.Get(
        CrowdDemoProjectileSchemas::HitPayloadSchemaId, Payload)
      || !Hit.Impact.Target.IsValid())
      return false;
    FCrowdDemoHitFact& Fact = OutFacts.AddDefaulted_GetRef();
    Fact.HitEventId = Hit.Impact.ProjectileId;
    Fact.ApplyFixedStep =
      static_cast<int32>(Hit.Impact.FixedStepIndex);
    Fact.SourceAgentId =
      static_cast<int32>(Hit.Impact.Instigator.StableEntityId);
    Fact.SourceLifecycleSerial =
      static_cast<int32>(Hit.Impact.Instigator.LifecycleSerial);
    Fact.TargetAgentId =
      static_cast<int32>(Hit.Impact.Target.StableEntityId);
    Fact.TargetLifecycleSerial =
      static_cast<int32>(Hit.Impact.Target.LifecycleSerial);
    Fact.HitPosition = Hit.Impact.Position;
    Fact.HitDirection = -Hit.Impact.Normal.GetSafeNormal();
    Fact.Damage = Payload.Damage;
    Fact.HorizontalImpulseCmps = Payload.HorizontalImpulseCmps;
    Fact.VerticalImpulseCmps = Payload.VerticalImpulseCmps;
    Fact.HitFlashProfileKey =
      static_cast<int32>(Payload.HitFlashProfileKey);
  }
  OutFacts.Sort([](const FCrowdDemoHitFact& A,
    const FCrowdDemoHitFact& B)
  {
    if (A.ApplyFixedStep != B.ApplyFixedStep)
      return A.ApplyFixedStep < B.ApplyFixedStep;
    if (A.TargetAgentId != B.TargetAgentId)
      return A.TargetAgentId < B.TargetAgentId;
    return A.HitEventId < B.HitEventId;
  });
  return true;
}

bool FCrowdDemoFlowObstacleCollisionSnapshotProvider::Gather(
  const int64 FixedStepIndex,
  TArray<FCrowdProjectileEnvironmentBody>& OutBodies) const
{
  OutBodies.Reset();
  if (FixedStepIndex < 0)
    return false;
  TArray<FCrowdDemoSharedFlowObstacleSpec> Sorted =
    Config.ObstacleSpecs;
  Sorted.Sort([](
    const FCrowdDemoSharedFlowObstacleSpec& A,
    const FCrowdDemoSharedFlowObstacleSpec& B)
  {
    return A.ObstacleId < B.ObstacleId;
  });
  int32 PreviousId = INDEX_NONE;
  for (const FCrowdDemoSharedFlowObstacleSpec& Spec : Sorted)
  {
    const FVector Center = Spec.Center;
    const FVector Extent = Spec.Extent;
    if (Spec.ObstacleId <= 0
      || Spec.ObstacleId == PreviousId
      || Center.ContainsNaN() || Extent.ContainsNaN()
      || Extent.X <= 0.0 || Extent.Y <= 0.0
      || Extent.Z < 0.0)
    {
      OutBodies.Reset();
      return false;
    }
    FCrowdProjectileEnvironmentBody& Body =
      OutBodies.AddDefaulted_GetRef();
    Body.StableSurfaceId =
      static_cast<uint64>(Spec.ObstacleId);
    Body.NavLayer = 0;
    Body.BoundsMin = Center - Extent;
    Body.BoundsMax = Center + Extent;
    // Flow obstacles are 2D navigation columns. A zero Z extent
    // therefore means an unbounded vertical collision column.
    if (Extent.Z <= UE_KINDA_SMALL_NUMBER)
    {
      Body.BoundsMin.Z = -100000.0;
      Body.BoundsMax.Z = 100000.0;
    }
    Body.CollisionProfileId = 1;
    Body.EffectProfileId = 1;
    Body.RecalculateStableHash();
    PreviousId = Spec.ObstacleId;
  }
  return true;
}

bool FCrowdDemoProjectileKernel::ValidateSettings(
  const FCrowdDemoRangedCombatSettings& Settings)
{
  return Settings.ShooterCount > 0
    && Settings.WindupFixedSteps > 0
    && Settings.RecoveryFixedSteps >= 0
    && Settings.CooldownFixedSteps >= 0
    && Settings.ProjectileSpeedCmps > 0.0f
    && Settings.ProjectileRadiusCm > 0.0f
    && Settings.ProjectileLifetimeFixedSteps > 0
    && Settings.ProjectilePierceCount >= 0
    && Settings.MuzzleForwardOffsetCm >= 0.0f
    && Settings.Damage >= 0.0f
    && Settings.PositionQuantumCm > 0.0f
    && Settings.VelocityQuantumCmps > 0.0f;
}

void FCrowdDemoProjectileKernel::AdvanceAttackPhases(
  const int32 RoundId,
  const int32 FixedStepIndex,
  const FCrowdDemoRangedCombatSettings& Settings,
  TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
  TArray<FCrowdDemoProjectileSpawnRequest>& OutSpawnRequests,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  OutSpawnRequests.Reset();
  InOutAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  InOutSummary.bValid = ValidateSettings(Settings);
  if (!InOutSummary.bValid)
    return;

  for (FCrowdDemoRangedCombatAgent& Shooter : InOutAgents)
  {
    if (Shooter.FormationIndex < 0 || Shooter.FormationIndex >= Settings.ShooterCount
      || !Shooter.bAlive || !Shooter.Combat.bAlive)
      continue;

    const FCrowdDemoRangedCombatAgent* Target = FindTargetByFormation(
      InOutAgents, Settings.ShooterCount + Shooter.FormationIndex);
    const bool bTargetValid = Target && Target->bAlive && Target->Combat.bAlive
      && Target->LifecycleSerial == Target->Combat.LifecycleSerial;

    if (Shooter.Combat.AttackPhase == ECrowdDemoAttackPhase::None
      || Shooter.Combat.AttackPhase == ECrowdDemoAttackPhase::AcquireTarget)
    {
      if (!bTargetValid)
      {
        const bool bHadTargetIdentity =
          Shooter.Combat.LockedTargetAgentId != INDEX_NONE
          || Shooter.Combat.TargetAgentId != INDEX_NONE;
        Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::AcquireTarget;
        Shooter.Combat.LockedTargetAgentId = INDEX_NONE;
        Shooter.Combat.LockedTargetLifecycleSerial = 0;
        Shooter.Combat.TargetAgentId = INDEX_NONE;
        Shooter.Combat.TargetLifecycleSerial = 0;
        InOutSummary.InvalidTargetLifecycleCount += bHadTargetIdentity ? 1 : 0;
        continue;
      }
      Shooter.Combat.BusinessState = ECrowdDemoBusinessState::Attacking;
      Shooter.Combat.TargetAgentId = Target->AgentId;
      Shooter.Combat.TargetLifecycleSerial = Target->LifecycleSerial;
      Shooter.Combat.LockedTargetAgentId = Target->AgentId;
      Shooter.Combat.LockedTargetLifecycleSerial = Target->LifecycleSerial;
      Shooter.Combat.LockedTargetLocation = Target->Position;
      Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Windup;
      Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
      Shooter.Combat.bFireRequestIssued = false;
      ++Shooter.Combat.BusinessStateRevision;
      Shooter.Combat.BusinessStateEnterFixedStep = FixedStepIndex;
      ++InOutSummary.TargetAcquiredCount;
      continue;
    }

    const bool bLockedTargetValid = bTargetValid
      && Target->AgentId == Shooter.Combat.LockedTargetAgentId
      && Target->LifecycleSerial == Shooter.Combat.LockedTargetLifecycleSerial;
    if (!bLockedTargetValid)
    {
      Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::AcquireTarget;
      Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
      Shooter.Combat.LockedTargetAgentId = INDEX_NONE;
      Shooter.Combat.LockedTargetLifecycleSerial = 0;
      Shooter.Combat.TargetAgentId = INDEX_NONE;
      Shooter.Combat.TargetLifecycleSerial = 0;
      Shooter.Combat.bFireRequestIssued = false;
      ++InOutSummary.InvalidTargetLifecycleCount;
      continue;
    }

    switch (Shooter.Combat.AttackPhase)
    {
      case ECrowdDemoAttackPhase::Windup:
        if (!Shooter.Combat.bFireRequestIssued
          && FixedStepIndex - Shooter.Combat.AttackPhaseEnterFixedStep >= Settings.WindupFixedSteps)
        {
          const FVector Direction = (Shooter.Combat.LockedTargetLocation - Shooter.Position).GetSafeNormal();
          if (Direction.IsNearlyZero())
          {
            InOutSummary.bValid = false;
            break;
          }
          ++Shooter.Combat.FireSequence;
          Shooter.Combat.bFireRequestIssued = true;
          Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Fire;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          auto& Request = OutSpawnRequests.AddDefaulted_GetRef();
          Request.ProjectileId = MakeProjectileId(
            RoundId, Shooter.AgentId, Shooter.Combat.FireSequence);
          Request.FixedStepIndex = FixedStepIndex;
          Request.SourceAgentId = Shooter.AgentId;
          Request.SourceLifecycleSerial = Shooter.LifecycleSerial;
          Request.SourceFactionId = Shooter.FactionId;
          Request.NavLayer = Shooter.NavLayer;
          Request.TargetAgentId = Target->AgentId;
          Request.TargetLifecycleSerial = Target->LifecycleSerial;
          Request.FireSequence = Shooter.Combat.FireSequence;
          Request.Position = QuantizeVector(
            Shooter.Position + Direction * Settings.MuzzleForwardOffsetCm,
            Settings.PositionQuantumCm);
          Request.Velocity = QuantizeVector(
            Direction * Settings.ProjectileSpeedCmps,
            Settings.VelocityQuantumCmps);
          ++InOutSummary.CompletedWindupCount;
        }
        break;
      case ECrowdDemoAttackPhase::Fire:
        Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Recovery;
        Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
        break;
      case ECrowdDemoAttackPhase::Recovery:
        if (FixedStepIndex - Shooter.Combat.AttackPhaseEnterFixedStep >= Settings.RecoveryFixedSteps)
        {
          Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Cooldown;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          Shooter.Combat.CooldownEndFixedStep = FixedStepIndex + Settings.CooldownFixedSteps;
        }
        break;
      case ECrowdDemoAttackPhase::Cooldown:
        if (FixedStepIndex >= Shooter.Combat.CooldownEndFixedStep)
        {
          Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::AcquireTarget;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          Shooter.Combat.bFireRequestIssued = false;
        }
        break;
      default:
        break;
    }
  }

  OutSpawnRequests.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex) return A.FixedStepIndex < B.FixedStepIndex;
    if (A.SourceAgentId != B.SourceAgentId) return A.SourceAgentId < B.SourceAgentId;
    return A.FireSequence < B.FireSequence;
  });
  InOutSummary.AttackStateHash = HashAttackStates(InOutAgents);
}

void FCrowdDemoProjectileKernel::SpawnProjectiles(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const FCrowdDemoRangedCombatSettings& Settings,
  const TConstArrayView<FCrowdDemoProjectileSpawnRequest> Requests,
  TArray<FCrowdDemoProjectileState>& InOutProjectiles,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  TArray<FCrowdDemoProjectileSpawnRequest> Sorted(Requests);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex) return A.FixedStepIndex < B.FixedStepIndex;
    if (A.SourceAgentId != B.SourceAgentId) return A.SourceAgentId < B.SourceAgentId;
    return A.FireSequence < B.FireSequence;
  });
  for (const auto& Request : Sorted)
  {
    if (Request.ProjectileId == 0 || Request.FixedStepIndex != FixedStepIndex
      || Request.Velocity.IsNearlyZero()
      || Request.CollisionProfileId == 0
      || Request.EffectProfileId == 0)
    {
      ++InOutSummary.InvalidProjectileCount;
      InOutSummary.bValid = false;
      continue;
    }
    if (InOutProjectiles.ContainsByPredicate(
      [&](const auto& Projectile) { return Projectile.ProjectileId == Request.ProjectileId; }))
    {
      ++InOutSummary.DuplicateFireCount;
      continue;
    }
    auto& Projectile = InOutProjectiles.AddDefaulted_GetRef();
    Projectile.ProjectileId = Request.ProjectileId;
    Projectile.SourceAgentId = Request.SourceAgentId;
    Projectile.SourceLifecycleSerial = Request.SourceLifecycleSerial;
    Projectile.TargetAgentId = Request.TargetAgentId;
    Projectile.TargetLifecycleSerial = Request.TargetLifecycleSerial;
    Projectile.FireSequence = Request.FireSequence;
    Projectile.SpawnFixedStep = FixedStepIndex;
    Projectile.RemainingPierces =
      Settings.ProjectilePierceCount;
    Projectile.LastHitTargetAgentId = INDEX_NONE;
    Projectile.SourceFactionId = Request.SourceFactionId;
    Projectile.NavLayer = Request.NavLayer;
    Projectile.CollisionProfileId = Request.CollisionProfileId;
    Projectile.EffectProfileId = Request.EffectProfileId;
    Projectile.PreviousPosition = Request.Position;
    Projectile.Position = Request.Position;
    Projectile.Velocity = Request.Velocity;
    Projectile.RadiusCm = Settings.ProjectileRadiusCm;
    Projectile.bActive = true;
    AddVisualEvent(ECrowdDemoProjectileVisualEventKind::Spawn,
      Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
    ++InOutSummary.SpawnedCount;
  }
  InOutProjectiles.Sort([](const auto& A, const auto& B)
  {
    return A.ProjectileId < B.ProjectileId;
  });
}

void FCrowdDemoProjectileKernel::AdvanceProjectiles(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const float FixedStepSeconds,
  const FCrowdDemoRangedCombatSettings& Settings,
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
  TArray<FCrowdDemoProjectileState>& InOutProjectiles,
  TArray<FCrowdImpactFact>& OutImpacts,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  AdvanceProjectiles(
    FixedStepIndex, ServerTimeSeconds, FixedStepSeconds,
    Settings, Agents,
    TConstArrayView<FCrowdProjectileEnvironmentBody>(),
    InOutProjectiles, OutImpacts, OutEvents, InOutSummary);
}

void FCrowdDemoProjectileKernel::AdvanceProjectiles(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const float FixedStepSeconds,
  const FCrowdDemoRangedCombatSettings& Settings,
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
  const TConstArrayView<FCrowdProjectileEnvironmentBody> EnvironmentBodies,
  TArray<FCrowdDemoProjectileState>& InOutProjectiles,
  TArray<FCrowdImpactFact>& OutImpacts,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  TArray<FCrowdDemoRangedCombatAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  float MaximumAgentRadius = 0.0f;
  TMap<FProjectileGridCell, TArray<int32>> AgentGrid;
  for (int32 AgentIndex = 0;
    AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    const FCrowdDemoRangedCombatAgent& Agent =
      SortedAgents[AgentIndex];
    MaximumAgentRadius =
      FMath::Max(MaximumAgentRadius, Agent.RadiusCm);
    const FVector Previous =
      Agent.Position - Agent.Velocity * FixedStepSeconds;
    AddSweptBoundsToGrid(
      AgentIndex, Previous, Agent.Position,
      Agent.RadiusCm, AgentGrid);
  }
  TArray<FCrowdProjectileEnvironmentBody> SortedEnvironment(
    EnvironmentBodies);
  SortedEnvironment.Sort([](
    const FCrowdProjectileEnvironmentBody& A,
    const FCrowdProjectileEnvironmentBody& B)
  {
    return A.StableSurfaceId < B.StableSurfaceId;
  });
  for (const FCrowdProjectileEnvironmentBody& Body
    : SortedEnvironment)
  {
    if (!Body.IsValid())
    {
      InOutSummary.bValid = false;
      ++InOutSummary.InvalidProjectileCount;
      return;
    }
  }
  InOutProjectiles.Sort([](const auto& A, const auto& B)
  {
    return A.ProjectileId < B.ProjectileId;
  });
  for (FCrowdDemoProjectileState& Projectile : InOutProjectiles)
  {
    if (!Projectile.bActive)
      continue;
    Projectile.PreviousPosition = Projectile.Position;
    const FVector Proposed = QuantizeVector(
      Projectile.Position + Projectile.Velocity * FixedStepSeconds,
      Settings.PositionQuantumCm);
    const FCrowdDemoRangedCombatAgent* BestTarget = nullptr;
    int64 BestTimeQ = MAX_int64;
    double BestTime = 0.0;
    const FCrowdProjectileEnvironmentBody* BestEnvironment = nullptr;
    FVector BestEnvironmentNormal = FVector::ZeroVector;
    TArray<int32> CandidateIndices;
    GatherGridCandidates(
      Projectile.PreviousPosition, Proposed,
      Projectile.RadiusCm + MaximumAgentRadius,
      AgentGrid, CandidateIndices);
    CandidateIndices.Sort([&](const int32 A, const int32 B)
    {
      return SortedAgents[A].AgentId < SortedAgents[B].AgentId;
    });
    InOutSummary.BroadphaseCandidateCount += CandidateIndices.Num();
    for (const int32 CandidateIndex : CandidateIndices)
    {
      const FCrowdDemoRangedCombatAgent& Target =
        SortedAgents[CandidateIndex];
      if (!Target.bAlive || !Target.Combat.bAlive
        || Target.AgentId == Projectile.SourceAgentId
        || Target.AgentId == Projectile.LastHitTargetAgentId
        || Target.NavLayer != Projectile.NavLayer
        || (Projectile.SourceFactionId != 0
          && Target.FactionId == Projectile.SourceFactionId))
        continue;
      double HitTime = 0.0;
      const FVector TargetPrevious =
        Target.Position - Target.Velocity * FixedStepSeconds;
      const FVector RelativeStart =
        Projectile.PreviousPosition - TargetPrevious;
      const FVector RelativeEnd = Proposed - Target.Position;
      ++InOutSummary.SweepTestCount;
      if (!SegmentSphereHitTime(
        RelativeStart, RelativeEnd, FVector::ZeroVector,
        Projectile.RadiusCm + Target.RadiusCm, HitTime))
        continue;
      const int64 HitTimeQ = FMath::RoundToInt64(HitTime * 1000000.0);
      if (!BestTarget || HitTimeQ < BestTimeQ
        || (HitTimeQ == BestTimeQ && Target.AgentId < BestTarget->AgentId))
      {
        BestTarget = &Target;
        BestTimeQ = HitTimeQ;
        BestTime = HitTime;
      }
    }
    for (const FCrowdProjectileEnvironmentBody& Body
      : SortedEnvironment)
    {
      if (Body.NavLayer != Projectile.NavLayer)
        continue;
      double HitTime = 0.0;
      FVector HitNormal = FVector::ZeroVector;
      if (!SegmentExpandedBoxHitTime(
          Projectile.PreviousPosition, Proposed,
          Body.BoundsMin, Body.BoundsMax,
          Projectile.RadiusCm, HitTime, HitNormal))
        continue;
      const int64 HitTimeQ =
        FMath::RoundToInt64(HitTime * 1000000.0);
      if (HitTimeQ < BestTimeQ
        || (HitTimeQ == BestTimeQ
          && (!BestEnvironment
            || Body.StableSurfaceId
              < BestEnvironment->StableSurfaceId)))
      {
        BestTarget = nullptr;
        BestEnvironment = &Body;
        BestEnvironmentNormal = HitNormal;
        BestTimeQ = HitTimeQ;
        BestTime = HitTime;
      }
    }

    ++Projectile.AgeFixedSteps;
    if (BestTarget || BestEnvironment)
    {
      Projectile.Position = QuantizeVector(
        FMath::Lerp(Projectile.PreviousPosition, Proposed, BestTime),
        Settings.PositionQuantumCm);
      const FVector ImpactPosition = Projectile.Position;
      const bool bContinuesAfterImpact =
        BestTarget && Projectile.RemainingPierces > 0;
      Projectile.bActive = bContinuesAfterImpact;
      Projectile.bImpacted = !bContinuesAfterImpact;
      if (bContinuesAfterImpact)
      {
        --Projectile.RemainingPierces;
        Projectile.LastHitTargetAgentId = BestTarget->AgentId;
        Projectile.Position +=
          Projectile.Velocity.GetSafeNormal();
      }
      FCrowdImpactFact& Impact = OutImpacts.AddDefaulted_GetRef();
      Impact.ProjectileId = Projectile.ProjectileId;
      Impact.FixedStepIndex = FixedStepIndex;
      Impact.Instigator = {
        1, static_cast<uint64>(Projectile.SourceAgentId),
        static_cast<uint32>(Projectile.SourceLifecycleSerial)};
      if (BestTarget)
      {
        Impact.Target = {
          1, static_cast<uint64>(BestTarget->AgentId),
          static_cast<uint32>(BestTarget->LifecycleSerial)};
      }
      Impact.Position = ImpactPosition;
      if (BestTarget)
      {
        const FVector TargetAtImpact = FMath::Lerp(
          BestTarget->Position
            - BestTarget->Velocity * FixedStepSeconds,
          BestTarget->Position, BestTime);
        Impact.Normal =
          (ImpactPosition - TargetAtImpact).GetSafeNormal();
        if (Impact.Normal.IsNearlyZero())
          Impact.Normal = -Projectile.Velocity.GetSafeNormal();
        Impact.CollisionProfileId =
          Projectile.CollisionProfileId;
        Impact.EffectProfileId = Projectile.EffectProfileId;
      }
      else
      {
        Impact.Normal = BestEnvironmentNormal;
        Impact.CollisionProfileId =
          BestEnvironment->CollisionProfileId;
        Impact.EffectProfileId =
          BestEnvironment->EffectProfileId;
        ++InOutSummary.EnvironmentImpactCount;
      }
      Impact.TimeOfImpactQ =
        static_cast<uint32>(FMath::Clamp<int64>(
          BestTimeQ, 0, 1000000));
      Impact.RecalculateStableHash();
      AddVisualEvent(ECrowdDemoProjectileVisualEventKind::Impact,
        Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
      ++InOutSummary.ImpactedCount;
    }
    else if (Projectile.AgeFixedSteps >= Settings.ProjectileLifetimeFixedSteps)
    {
      Projectile.Position = Proposed;
      Projectile.bActive = false;
      Projectile.bExpired = true;
      AddVisualEvent(ECrowdDemoProjectileVisualEventKind::Expire,
        Projectile, FixedStepIndex, ServerTimeSeconds, OutEvents);
      ++InOutSummary.ExpiredCount;
    }
    else
    {
      Projectile.Position = Proposed;
    }
  }
  OutImpacts.Sort([](const FCrowdImpactFact& A,
    const FCrowdImpactFact& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.TimeOfImpactQ != B.TimeOfImpactQ)
      return A.TimeOfImpactQ < B.TimeOfImpactQ;
    if (A.Target != B.Target) return A.Target < B.Target;
    return A.ProjectileId < B.ProjectileId;
  });
  OutEvents.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex) return A.FixedStepIndex < B.FixedStepIndex;
    if (A.ProjectileId != B.ProjectileId) return A.ProjectileId < B.ProjectileId;
    return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
  });
  InOutSummary.ActiveCount = 0;
  for (const FCrowdDemoProjectileState& Projectile : InOutProjectiles)
  {
    InOutSummary.ActiveCount += Projectile.bActive ? 1 : 0;
  }
  InOutSummary.ProjectileStateHash = HashProjectileStates(InOutProjectiles);
  InOutSummary.EventHash = HashEvents(OutEvents);
}

void FCrowdDemoProjectileKernel::AdvanceProjectiles(
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const float FixedStepSeconds,
  const FCrowdDemoRangedCombatSettings& Settings,
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
  TArray<FCrowdDemoProjectileState>& InOutProjectiles,
  TArray<FCrowdDemoHitFact>& OutHitFacts,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  TArray<FCrowdImpactFact> Impacts;
  AdvanceProjectiles(
    FixedStepIndex, ServerTimeSeconds, FixedStepSeconds,
    Settings, Agents, InOutProjectiles, Impacts, OutEvents,
    InOutSummary);
  TArray<FCrowdHitFact> Hits;
  TArray<FCrowdDemoHitFact> NewHitFacts;
  const FCrowdDemoHostHitResolver Resolver(Settings);
  if (!Resolver.Resolve(Impacts, Hits)
    || !FCrowdDemoHostHitResolver::BuildDemoHitFacts(
      Hits, NewHitFacts))
  {
    InOutSummary.bValid = false;
  }
  else
  {
    OutHitFacts.Append(MoveTemp(NewHitFacts));
    OutHitFacts.Sort([](const FCrowdDemoHitFact& A,
      const FCrowdDemoHitFact& B)
    {
      if (A.ApplyFixedStep != B.ApplyFixedStep)
        return A.ApplyFixedStep < B.ApplyFixedStep;
      if (A.TargetAgentId != B.TargetAgentId)
        return A.TargetAgentId < B.TargetAgentId;
      return A.HitEventId < B.HitEventId;
    });
  }
}

uint32 FCrowdDemoProjectileKernel::HashAttackStates(
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents)
{
  TArray<FCrowdDemoRangedCombatAgent> Sorted(Agents);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = FnvOffset;
  for (const auto& Agent : Sorted)
  {
    Fold(Hash, static_cast<uint32>(Agent.AgentId));
    Fold(Hash, static_cast<uint32>(Agent.LifecycleSerial));
    Fold(Hash, static_cast<uint32>(Agent.FormationIndex));
    Fold(Hash, Agent.FactionId);
    Fold(Hash, Agent.NavLayer);
    Fold(Hash, static_cast<uint8>(Agent.Combat.BusinessState));
    Fold(Hash, static_cast<uint8>(Agent.Combat.AttackPhase));
    Fold(Hash, static_cast<uint32>(Agent.Combat.AttackPhaseEnterFixedStep));
    Fold(Hash, static_cast<uint32>(Agent.Combat.LockedTargetAgentId));
    Fold(Hash, static_cast<uint32>(Agent.Combat.LockedTargetLifecycleSerial));
    Fold(Hash, static_cast<uint32>(Agent.Combat.FireSequence));
    Fold(Hash, Agent.Combat.bFireRequestIssued ? 1u : 0u);
  }
  return Hash;
}

uint32 FCrowdDemoProjectileKernel::HashProjectileStates(
  const TConstArrayView<FCrowdDemoProjectileState> Projectiles)
{
  TArray<FCrowdDemoProjectileState> Sorted(Projectiles);
  Sorted.Sort([](const auto& A, const auto& B) { return A.ProjectileId < B.ProjectileId; });
  uint32 Hash = FnvOffset;
  for (const auto& Projectile : Sorted)
  {
    Fold(Hash, static_cast<uint32>(Projectile.ProjectileId));
    Fold(Hash, static_cast<uint32>(Projectile.ProjectileId >> 32));
    Fold(Hash, static_cast<uint32>(Projectile.SourceAgentId));
    Fold(Hash, static_cast<uint32>(Projectile.TargetAgentId));
    Fold(Hash, static_cast<uint32>(Projectile.FireSequence));
    Fold(Hash, static_cast<uint32>(Projectile.AgeFixedSteps));
    Fold(Hash, static_cast<uint32>(Projectile.RemainingPierces));
    Fold(Hash, static_cast<uint32>(
      Projectile.LastHitTargetAgentId));
    Fold(Hash, Projectile.SourceFactionId);
    Fold(Hash, Projectile.NavLayer);
    Fold(Hash, Projectile.CollisionProfileId);
    Fold(Hash, Projectile.EffectProfileId);
    Fold(Hash, static_cast<uint32>(Q(Projectile.Position.X, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Projectile.Position.Y, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Projectile.Position.Z, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Projectile.Velocity.X, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Projectile.Velocity.Y, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Projectile.Velocity.Z, 1.0f)));
    Fold(Hash, Projectile.bActive ? 1u : 0u);
    Fold(Hash, Projectile.bImpacted ? 1u : 0u);
    Fold(Hash, Projectile.bExpired ? 1u : 0u);
  }
  return Hash;
}

uint32 FCrowdDemoProjectileKernel::HashEvents(
  const TConstArrayView<FCrowdDemoProjectileVisualEvent> Events)
{
  TArray<FCrowdDemoProjectileVisualEvent> Sorted(Events);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex) return A.FixedStepIndex < B.FixedStepIndex;
    if (A.ProjectileId != B.ProjectileId) return A.ProjectileId < B.ProjectileId;
    return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
  });
  uint32 Hash = FnvOffset;
  for (const auto& Event : Sorted)
  {
    Fold(Hash, static_cast<uint8>(Event.Kind));
    Fold(Hash, static_cast<uint32>(Event.ProjectileId));
    Fold(Hash, static_cast<uint32>(Event.ProjectileId >> 32));
    Fold(Hash, static_cast<uint32>(Event.FixedStepIndex));
    Fold(Hash, static_cast<uint32>(Q(Event.Position.X, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Event.Position.Y, 1.0f)));
    Fold(Hash, static_cast<uint32>(Q(Event.Position.Z, 1.0f)));
  }
  return Hash;
}
