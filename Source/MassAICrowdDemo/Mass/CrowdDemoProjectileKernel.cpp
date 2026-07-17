#include "Mass/CrowdDemoProjectileKernel.h"

#include "Algo/Sort.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

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
      || Request.Velocity.IsNearlyZero())
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
  TArray<FCrowdDemoHitFact>& OutHitFacts,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  TArray<FCrowdDemoRangedCombatAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
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
    for (const auto& Target : SortedAgents)
    {
      if (!Target.bAlive || !Target.Combat.bAlive || Target.AgentId == Projectile.SourceAgentId)
        continue;
      double HitTime = 0.0;
      if (!SegmentSphereHitTime(
        Projectile.PreviousPosition, Proposed, Target.Position,
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

    ++Projectile.AgeFixedSteps;
    if (BestTarget)
    {
      Projectile.Position = QuantizeVector(
        FMath::Lerp(Projectile.PreviousPosition, Proposed, BestTime),
        Settings.PositionQuantumCm);
      Projectile.bActive = false;
      Projectile.bImpacted = true;
      FCrowdDemoHitFact& Hit = OutHitFacts.AddDefaulted_GetRef();
      Hit.HitEventId = Projectile.ProjectileId;
      Hit.ApplyFixedStep = FixedStepIndex;
      Hit.SourceAgentId = Projectile.SourceAgentId;
      Hit.SourceLifecycleSerial = Projectile.SourceLifecycleSerial;
      Hit.TargetAgentId = BestTarget->AgentId;
      Hit.TargetLifecycleSerial = BestTarget->LifecycleSerial;
      Hit.HitPosition = Projectile.Position;
      Hit.HitDirection = Projectile.Velocity.GetSafeNormal();
      Hit.Damage = Settings.Damage;
      Hit.HorizontalImpulseCmps = Settings.HorizontalImpulseCmps;
      Hit.VerticalImpulseCmps = Settings.VerticalImpulseCmps;
      Hit.HitFlashProfileKey = 1;
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
  OutHitFacts.Sort([](const auto& A, const auto& B)
  {
    if (A.ApplyFixedStep != B.ApplyFixedStep) return A.ApplyFixedStep < B.ApplyFixedStep;
    if (A.TargetAgentId != B.TargetAgentId) return A.TargetAgentId < B.TargetAgentId;
    return A.HitEventId < B.HitEventId;
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
