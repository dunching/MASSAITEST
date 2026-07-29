#include "Mass/CrowdDemoProjectileAdapters.h"

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

  uint64 MakeProjectileId(
    const int32 RoundId,
    const int32 SourceAgentId,
    const int32 FireSequence)
  {
    return
      (static_cast<uint64>(
        static_cast<uint32>(RoundId) & 0xffffu) << 48)
      | (static_cast<uint64>(
        static_cast<uint32>(SourceAgentId) & 0xffffffu) << 24)
      | static_cast<uint64>(
        static_cast<uint32>(FireSequence) & 0xffffffu);
  }

  const FCrowdDemoRangedCombatAgent* FindTargetByFormation(
    const TArray<FCrowdDemoRangedCombatAgent>& Agents,
    const int32 FormationIndex)
  {
    return Agents.FindByPredicate(
      [FormationIndex](const FCrowdDemoRangedCombatAgent& Agent)
      {
        return Agent.FormationIndex == FormationIndex;
      });
  }
}

bool FCrowdDemoHostHitResolver::Resolve(
  const TConstArrayView<FCrowdImpactFact> Impacts,
  TArray<FCrowdHitFact>& OutHits) const
{
  FCrowdHitResolveResult Result;
  const TArray<FCrowdEffectProfile> Profiles = {
    FCrowdDemoProjectileAdapters::BuildEffectProfile(Settings)};
  if (!FCrowdCombatResolver::Resolve(Impacts, Profiles, Result))
    return false;
  OutHits = MoveTemp(Result.Hits);
  return true;
}

bool FCrowdDemoHostHitResolver::BuildDemoHitFacts(
  const TConstArrayView<FCrowdHitFact> Hits,
  TArray<FCrowdDemoHitFact>& OutFacts)
{
  return FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
    Hits, OutFacts);
}

bool FCrowdDemoFlowObstacleCollisionSnapshotProvider::Gather(
  const int64 FixedStepIndex,
  TArray<FCrowdSpatialEnvironmentBody>& OutBodies) const
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
    if (Spec.ObstacleId <= 0 || Spec.ObstacleId == PreviousId
      || Center.ContainsNaN() || Extent.ContainsNaN()
      || Extent.X <= 0.0 || Extent.Y <= 0.0
      || Extent.Z < 0.0)
    {
      OutBodies.Reset();
      return false;
    }
    FCrowdSpatialEnvironmentBody& Body =
      OutBodies.AddDefaulted_GetRef();
    Body.StableSurfaceId = static_cast<uint64>(Spec.ObstacleId);
    Body.BoundsMin = Center - Extent;
    Body.BoundsMax = Center + Extent;
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

bool FCrowdDemoProjectileAdapters::ValidateSettings(
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

FCrowdProjectileProfile FCrowdDemoProjectileAdapters::BuildProfile(
  const FCrowdDemoRangedCombatSettings& Settings,
  const int32 MaximumCapacity)
{
  FCrowdProjectileProfile Profile;
  Profile.ProfileId = CrowdDemoProjectileSchemas::ProjectileProfileId;
  Profile.RadiusCm = Settings.ProjectileRadiusCm;
  Profile.LifetimeFixedSteps =
    Settings.ProjectileLifetimeFixedSteps;
  Profile.PierceCount = Settings.ProjectilePierceCount;
  Profile.MaxActiveProjectiles = MaximumCapacity;
  Profile.PositionQuantumCm = Settings.PositionQuantumCm;
  Profile.VelocityQuantumCmps = Settings.VelocityQuantumCmps;
  Profile.RecalculateStableHash();
  return Profile;
}

FCrowdEffectProfile FCrowdDemoProjectileAdapters::BuildEffectProfile(
  const FCrowdDemoRangedCombatSettings& Settings)
{
  FCrowdDemoProjectileHitPayload Payload;
  Payload.Damage = Settings.Damage;
  Payload.HorizontalImpulseCmps = Settings.HorizontalImpulseCmps;
  Payload.VerticalImpulseCmps = Settings.VerticalImpulseCmps;
  Payload.HitFlashProfileKey = 1;
  FCrowdEffectProfile Profile;
  Profile.EffectProfileId = 1;
  Profile.PayloadTypeId =
    CrowdDemoProjectileSchemas::HitPayloadTypeId;
  Profile.Payload.Set(
    CrowdDemoProjectileSchemas::HitPayloadSchemaId, Payload);
  Profile.RecalculateStableHash();
  return Profile;
}

void FCrowdDemoProjectileAdapters::AdvanceAttackPhases(
  const int32 RoundId,
  const int32 FixedStepIndex,
  const FCrowdDemoRangedCombatSettings& Settings,
  TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
  TArray<FCrowdProjectileSpawnRequest>& OutSpawnRequests,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  OutSpawnRequests.Reset();
  InOutAgents.Sort([](
    const FCrowdDemoRangedCombatAgent& A,
    const FCrowdDemoRangedCombatAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  InOutSummary.bValid = ValidateSettings(Settings);
  if (!InOutSummary.bValid)
    return;

  for (FCrowdDemoRangedCombatAgent& Shooter : InOutAgents)
  {
    if (Shooter.FormationIndex < 0
      || Shooter.FormationIndex >= Settings.ShooterCount
      || !Shooter.bAlive || !Shooter.Combat.bAlive)
      continue;
    const FCrowdDemoRangedCombatAgent* Target =
      FindTargetByFormation(
        InOutAgents,
        Settings.ShooterCount + Shooter.FormationIndex);
    const bool bTargetValid =
      Target && Target->bAlive && Target->Combat.bAlive
      && Target->LifecycleSerial == Target->Combat.LifecycleSerial;
    if (Shooter.Combat.AttackPhase == ECrowdDemoAttackPhase::None
      || Shooter.Combat.AttackPhase
        == ECrowdDemoAttackPhase::AcquireTarget)
    {
      if (!bTargetValid)
      {
        const bool bHadTargetIdentity =
          Shooter.Combat.LockedTargetAgentId != INDEX_NONE
          || Shooter.Combat.TargetAgentId != INDEX_NONE;
        Shooter.Combat.AttackPhase =
          ECrowdDemoAttackPhase::AcquireTarget;
        Shooter.Combat.LockedTargetAgentId = INDEX_NONE;
        Shooter.Combat.LockedTargetLifecycleSerial = 0;
        Shooter.Combat.TargetAgentId = INDEX_NONE;
        Shooter.Combat.TargetLifecycleSerial = 0;
        InOutSummary.InvalidTargetLifecycleCount +=
          bHadTargetIdentity ? 1 : 0;
        continue;
      }
      Shooter.Combat.BusinessState =
        ECrowdDemoBusinessState::Attacking;
      Shooter.Combat.TargetAgentId = Target->AgentId;
      Shooter.Combat.TargetLifecycleSerial = Target->LifecycleSerial;
      Shooter.Combat.LockedTargetAgentId = Target->AgentId;
      Shooter.Combat.LockedTargetLifecycleSerial =
        Target->LifecycleSerial;
      Shooter.Combat.LockedTargetLocation = Target->Position;
      Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Windup;
      Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
      Shooter.Combat.bFireRequestIssued = false;
      ++Shooter.Combat.BusinessStateRevision;
      Shooter.Combat.BusinessStateEnterFixedStep = FixedStepIndex;
      ++InOutSummary.TargetAcquiredCount;
      continue;
    }

    const bool bLockedTargetValid =
      bTargetValid
      && Target->AgentId == Shooter.Combat.LockedTargetAgentId
      && Target->LifecycleSerial
        == Shooter.Combat.LockedTargetLifecycleSerial;
    if (!bLockedTargetValid)
    {
      Shooter.Combat.AttackPhase =
        ECrowdDemoAttackPhase::AcquireTarget;
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
          && FixedStepIndex
            - Shooter.Combat.AttackPhaseEnterFixedStep
            >= Settings.WindupFixedSteps)
        {
          const FVector Direction =
            (Shooter.Combat.LockedTargetLocation
              - Shooter.Position).GetSafeNormal();
          if (Direction.IsNearlyZero())
          {
            InOutSummary.bValid = false;
            break;
          }
          ++Shooter.Combat.FireSequence;
          Shooter.Combat.bFireRequestIssued = true;
          Shooter.Combat.AttackPhase = ECrowdDemoAttackPhase::Fire;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          FCrowdProjectileSpawnRequest& Request =
            OutSpawnRequests.AddDefaulted_GetRef();
          Request.ProjectileId = MakeProjectileId(
            RoundId, Shooter.AgentId, Shooter.Combat.FireSequence);
          Request.FixedStepIndex = FixedStepIndex;
          Request.Instigator = {
            1, static_cast<uint64>(Shooter.AgentId),
            static_cast<uint32>(Shooter.LifecycleSerial)};
          Request.Target = {
            1, static_cast<uint64>(Target->AgentId),
            static_cast<uint32>(Target->LifecycleSerial)};
          Request.FireSequence = Shooter.Combat.FireSequence;
          Request.SourceFactionId = Shooter.FactionId;
          Request.NavLayer = Shooter.NavLayer;
          Request.ProjectileProfileId =
            CrowdDemoProjectileSchemas::ProjectileProfileId;
          Request.CollisionProfileId = 1;
          Request.EffectProfileId = 1;
          Request.Position = QuantizeVector(
            Shooter.Position
              + Direction * Settings.MuzzleForwardOffsetCm,
            Settings.PositionQuantumCm);
          Request.Velocity = QuantizeVector(
            Direction * Settings.ProjectileSpeedCmps,
            Settings.VelocityQuantumCmps);
          Request.RecalculateStableHash();
          ++InOutSummary.CompletedWindupCount;
        }
        break;
      case ECrowdDemoAttackPhase::Fire:
        Shooter.Combat.AttackPhase =
          ECrowdDemoAttackPhase::Recovery;
        Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
        break;
      case ECrowdDemoAttackPhase::Recovery:
        if (FixedStepIndex
          - Shooter.Combat.AttackPhaseEnterFixedStep
          >= Settings.RecoveryFixedSteps)
        {
          Shooter.Combat.AttackPhase =
            ECrowdDemoAttackPhase::Cooldown;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          Shooter.Combat.CooldownEndFixedStep =
            FixedStepIndex + Settings.CooldownFixedSteps;
        }
        break;
      case ECrowdDemoAttackPhase::Cooldown:
        if (FixedStepIndex >= Shooter.Combat.CooldownEndFixedStep)
        {
          Shooter.Combat.AttackPhase =
            ECrowdDemoAttackPhase::AcquireTarget;
          Shooter.Combat.AttackPhaseEnterFixedStep = FixedStepIndex;
          Shooter.Combat.bFireRequestIssued = false;
        }
        break;
      default:
        break;
    }
  }
  OutSpawnRequests.Sort([](
    const FCrowdProjectileSpawnRequest& A,
    const FCrowdProjectileSpawnRequest& B)
  {
    if (A.FixedStepIndex != B.FixedStepIndex)
      return A.FixedStepIndex < B.FixedStepIndex;
    if (A.Instigator != B.Instigator)
      return A.Instigator < B.Instigator;
    return A.FireSequence < B.FireSequence;
  });
  InOutSummary.AttackStateHash =
    HashAttackStates(InOutAgents);
}

bool FCrowdDemoProjectileAdapters::BuildTargetSnapshots(
  const float FixedStepSeconds,
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
  TArray<FCrowdProjectileTargetSnapshot>& OutTargets)
{
  OutTargets.Reset();
  if (!FMath::IsFinite(FixedStepSeconds)
    || FixedStepSeconds <= 0.0f)
    return false;
  for (const FCrowdDemoRangedCombatAgent& Agent : Agents)
  {
    FCrowdProjectileTargetSnapshot& Target =
      OutTargets.AddDefaulted_GetRef();
    Target.EntityRef = {
      1, static_cast<uint64>(Agent.AgentId),
      static_cast<uint32>(Agent.LifecycleSerial)};
    Target.FactionId = Agent.FactionId;
    Target.NavLayer = Agent.NavLayer;
    Target.PreviousPosition =
      Agent.Position - Agent.Velocity * FixedStepSeconds;
    Target.Position = Agent.Position;
    Target.RadiusCm = Agent.RadiusCm;
    Target.bAlive = Agent.bAlive && Agent.Combat.bAlive;
    Target.RecalculateStableHash();
    if (!Target.IsValid())
      return false;
  }
  OutTargets.Sort([](
    const FCrowdProjectileTargetSnapshot& A,
    const FCrowdProjectileTargetSnapshot& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  return true;
}

void FCrowdDemoProjectileAdapters::AppendVisualEvents(
  const TConstArrayView<FCrowdProjectileLifecycleEvent> Events,
  TArray<FCrowdDemoProjectileVisualEvent>& OutEvents)
{
  for (const FCrowdProjectileLifecycleEvent& Source : Events)
  {
    FCrowdDemoProjectileVisualEvent& Event =
      OutEvents.AddDefaulted_GetRef();
    Event.Kind = static_cast<ECrowdDemoProjectileVisualEventKind>(
      static_cast<uint8>(Source.Kind));
    Event.ProjectileId = Source.ProjectileId;
    Event.FixedStepIndex = static_cast<int32>(Source.FixedStepIndex);
    Event.ServerTimeSeconds = Source.ServerTimeSeconds;
    Event.Position = Source.Position;
    Event.Velocity = Source.Velocity;
    Event.RadiusCm = Source.RadiusCm;
  }
}

void FCrowdDemoProjectileAdapters::MergeSummary(
  const FCrowdProjectileStepSummary& Source,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  InOutSummary.bValid = InOutSummary.bValid && Source.bValid;
  InOutSummary.SpawnedCount += Source.SpawnedCount;
  InOutSummary.ActiveCount = Source.ActiveCount;
  InOutSummary.ImpactedCount += Source.ImpactedCount;
  InOutSummary.ExpiredCount += Source.ExpiredCount;
  InOutSummary.DuplicateFireCount += Source.DuplicateFireCount;
  InOutSummary.InvalidProjectileCount +=
    Source.InvalidProjectileCount;
  InOutSummary.EnvironmentImpactCount +=
    Source.EnvironmentImpactCount;
  InOutSummary.BroadphaseCandidateCount +=
    Source.BroadphaseCandidateCount;
  InOutSummary.SweepTestCount += Source.SweepTestCount;
  InOutSummary.ProjectileStateHash = Source.ProjectileStateHash;
  InOutSummary.EventHash = Source.EventHash;
}

bool FCrowdDemoProjectileAdapters::PrepareProjectileBoundary(
  const int32 RoundId,
  const int32 FixedStepIndex,
  const float ServerTimeSeconds,
  const float FixedStepSeconds,
  const FCrowdDemoRangedCombatSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
  TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
  const TConstArrayView<FCrowdProjectileState> CurrentStates,
  FCrowdPreparedProjectileBoundary& OutPrepared,
  TArray<FCrowdDemoHitFact>& OutHitFacts,
  TArray<FCrowdDemoProjectileVisualEvent>& OutVisualEvents,
  FCrowdDemoProjectileStepSummary& OutSummary)
{
  OutPrepared = {};
  OutHitFacts.Reset();
  OutVisualEvents.Reset();
  OutSummary = {};
  TArray<FCrowdProjectileSpawnRequest> Requests;
  AdvanceAttackPhases(
    RoundId, FixedStepIndex, Settings,
    InOutAgents, Requests, OutSummary);
  if (!OutSummary.bValid)
    return false;

  FCrowdProjectileBoundaryInput Input;
  Input.FixedStepIndex = FixedStepIndex;
  Input.ServerTimeSeconds = ServerTimeSeconds;
  Input.FixedStepSeconds = FixedStepSeconds;
  Input.Profiles.Add(BuildProfile(Settings, 65536));
  Input.SpawnRequests = MoveTemp(Requests);
  Input.CurrentStates.Append(CurrentStates);
  if (!BuildTargetSnapshots(
      FixedStepSeconds, InOutAgents, Input.Targets))
    return false;
  const FCrowdDemoFlowObstacleCollisionSnapshotProvider
    EnvironmentProvider(FlowConfig);
  if (!EnvironmentProvider.Gather(
      FixedStepIndex, Input.EnvironmentBodies)
    || !FCrowdProjectileBoundaryPipeline::Prepare(
      Input, OutPrepared)
    || !FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, OutPrepared))
    return false;

  FCrowdHitResolveResult ResolveResult;
  const TArray<FCrowdEffectProfile> EffectProfiles = {
    BuildEffectProfile(Settings)};
  if (!FCrowdCombatResolver::Resolve(
      OutPrepared.Impacts, EffectProfiles, ResolveResult)
    || !BuildDemoHitFacts(ResolveResult.Hits, OutHitFacts))
    return false;
  AppendVisualEvents(OutPrepared.Events, OutVisualEvents);
  MergeSummary(OutPrepared.Summary, OutSummary);
  return OutSummary.bValid;
}

bool FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
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
    Fact.SourceAgentId = static_cast<int32>(
      Hit.Impact.Instigator.StableEntityId);
    Fact.SourceLifecycleSerial = static_cast<int32>(
      Hit.Impact.Instigator.LifecycleSerial);
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
  OutFacts.Sort([](
    const FCrowdDemoHitFact& A,
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

uint32 FCrowdDemoProjectileAdapters::HashAttackStates(
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents)
{
  TArray<FCrowdDemoRangedCombatAgent> Sorted(Agents);
  Sorted.Sort([](
    const FCrowdDemoRangedCombatAgent& A,
    const FCrowdDemoRangedCombatAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  uint32 Hash = FnvOffset;
  for (const FCrowdDemoRangedCombatAgent& Agent : Sorted)
  {
    Fold(Hash, static_cast<uint32>(Agent.AgentId));
    Fold(Hash, static_cast<uint32>(Agent.LifecycleSerial));
    Fold(Hash, static_cast<uint32>(Agent.FormationIndex));
    Fold(Hash, Agent.FactionId);
    Fold(Hash, Agent.NavLayer);
    Fold(Hash, static_cast<uint8>(Agent.Combat.BusinessState));
    Fold(Hash, static_cast<uint8>(Agent.Combat.AttackPhase));
    Fold(Hash, static_cast<uint32>(
      Agent.Combat.AttackPhaseEnterFixedStep));
    Fold(Hash, static_cast<uint32>(
      Agent.Combat.LockedTargetAgentId));
    Fold(Hash, static_cast<uint32>(
      Agent.Combat.LockedTargetLifecycleSerial));
    Fold(Hash, static_cast<uint32>(Agent.Combat.FireSequence));
    Fold(Hash, Agent.Combat.bFireRequestIssued ? 1u : 0u);
  }
  return Hash;
}
