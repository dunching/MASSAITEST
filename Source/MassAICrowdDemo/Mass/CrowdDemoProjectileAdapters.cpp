#include "Mass/CrowdDemoProjectileAdapters.h"
#include "CrowdDemoRangedAttackPlanner.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  void Fold(uint32& Hash, const uint32 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
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

bool FCrowdDemoProjectileAdapters::BuildRangedAttackPlan(
  const int32 RoundId,
  const int32 FixedStepIndex,
  const FCrowdDemoRangedCombatSettings& Settings,
  TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
  TArray<FCrowdProjectileSpawnRequest>& OutSpawnRequests,
  FCrowdDemoProjectileStepSummary& InOutSummary)
{
  OutSpawnRequests.Reset();
  if (!ValidateSettings(Settings))
    return false;
  FCrowdDemoRangedAttackSettings PlannerSettings;
  PlannerSettings.ShooterCount = Settings.ShooterCount;
  PlannerSettings.WindupFixedSteps = Settings.WindupFixedSteps;
  PlannerSettings.RecoveryFixedSteps = Settings.RecoveryFixedSteps;
  PlannerSettings.CooldownFixedSteps = Settings.CooldownFixedSteps;
  PlannerSettings.ProjectileSpeedCmps = Settings.ProjectileSpeedCmps;
  PlannerSettings.MuzzleForwardOffsetCm = Settings.MuzzleForwardOffsetCm;
  PlannerSettings.PositionQuantumCm = Settings.PositionQuantumCm;
  PlannerSettings.VelocityQuantumCmps = Settings.VelocityQuantumCmps;

  TArray<FCrowdDemoRangedAttackAgent> PlannerAgents;
  PlannerAgents.Reserve(InOutAgents.Num());
  for (const FCrowdDemoRangedCombatAgent& Agent : InOutAgents)
  {
    FCrowdDemoRangedAttackAgent& PlannerAgent =
      PlannerAgents.AddDefaulted_GetRef();
    PlannerAgent.EntityRef = Agent.EntityRef;
    PlannerAgent.AgentId = Agent.AgentId;
    PlannerAgent.LifecycleSerial = Agent.LifecycleSerial;
    PlannerAgent.StateLifecycleSerial = Agent.Combat.LifecycleSerial;
    PlannerAgent.FormationIndex = Agent.FormationIndex;
    PlannerAgent.FactionId = Agent.FactionId;
    PlannerAgent.NavLayer = Agent.NavLayer;
    PlannerAgent.Position = Agent.Position;
    PlannerAgent.bAlive = Agent.bAlive;
    PlannerAgent.bStateAlive = Agent.Combat.bAlive;
    PlannerAgent.State.BusinessState =
      static_cast<ECrowdDemoRangedBusinessState>(
        Agent.Combat.BusinessState);
    PlannerAgent.State.BusinessStateRevision =
      Agent.Combat.BusinessStateRevision;
    PlannerAgent.State.BusinessStateEnterFixedStep =
      Agent.Combat.BusinessStateEnterFixedStep;
    PlannerAgent.State.TargetAgentId = Agent.Combat.TargetAgentId;
    PlannerAgent.State.TargetLifecycleSerial =
      Agent.Combat.TargetLifecycleSerial;
    PlannerAgent.State.AttackPhase =
      static_cast<ECrowdDemoRangedAttackPhase>(
        Agent.Combat.AttackPhase);
    PlannerAgent.State.AttackPhaseEnterFixedStep =
      Agent.Combat.AttackPhaseEnterFixedStep;
    PlannerAgent.State.CooldownEndFixedStep =
      Agent.Combat.CooldownEndFixedStep;
    PlannerAgent.State.LockedTargetAgentId =
      Agent.Combat.LockedTargetAgentId;
    PlannerAgent.State.LockedTargetLifecycleSerial =
      Agent.Combat.LockedTargetLifecycleSerial;
    PlannerAgent.State.LockedTargetLocation =
      Agent.Combat.LockedTargetLocation;
    PlannerAgent.State.FireSequence = Agent.Combat.FireSequence;
    PlannerAgent.State.bFireRequestIssued =
      Agent.Combat.bFireRequestIssued;
  }
  TArray<FCrowdDemoFireIntent> FireIntents;
  FCrowdDemoRangedAttackPlanSummary PlannerSummary;
  if (!FCrowdDemoRangedAttackPlanner::Advance(
      RoundId, FixedStepIndex, PlannerSettings,
      PlannerAgents, FireIntents, PlannerSummary))
    return false;
  InOutAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (FCrowdDemoRangedCombatAgent& Agent : InOutAgents)
  {
    const FCrowdDemoRangedAttackAgent* PlannerAgent =
      PlannerAgents.FindByPredicate(
        [&Agent](const auto& Candidate)
        {
          return Candidate.AgentId == Agent.AgentId;
        });
    if (!PlannerAgent) return false;
    Agent.Combat.BusinessState =
      static_cast<ECrowdDemoBusinessState>(
        PlannerAgent->State.BusinessState);
    Agent.Combat.BusinessStateRevision =
      PlannerAgent->State.BusinessStateRevision;
    Agent.Combat.BusinessStateEnterFixedStep =
      PlannerAgent->State.BusinessStateEnterFixedStep;
    Agent.Combat.TargetAgentId = PlannerAgent->State.TargetAgentId;
    Agent.Combat.TargetLifecycleSerial =
      PlannerAgent->State.TargetLifecycleSerial;
    Agent.Combat.AttackPhase =
      static_cast<ECrowdDemoAttackPhase>(
        PlannerAgent->State.AttackPhase);
    Agent.Combat.AttackPhaseEnterFixedStep =
      PlannerAgent->State.AttackPhaseEnterFixedStep;
    Agent.Combat.CooldownEndFixedStep =
      PlannerAgent->State.CooldownEndFixedStep;
    Agent.Combat.LockedTargetAgentId =
      PlannerAgent->State.LockedTargetAgentId;
    Agent.Combat.LockedTargetLifecycleSerial =
      PlannerAgent->State.LockedTargetLifecycleSerial;
    Agent.Combat.LockedTargetLocation =
      PlannerAgent->State.LockedTargetLocation;
    Agent.Combat.FireSequence = PlannerAgent->State.FireSequence;
    Agent.Combat.bFireRequestIssued =
      PlannerAgent->State.bFireRequestIssued;
  }
  for (const FCrowdDemoFireIntent& Intent : FireIntents)
  {
    FCrowdProjectileSpawnRequest& Request =
      OutSpawnRequests.AddDefaulted_GetRef();
    Request.ProjectileId = Intent.ProjectileId;
    Request.FixedStepIndex = Intent.FixedStepIndex;
    Request.Instigator = Intent.Instigator;
    Request.Target = Intent.Target;
    Request.FireSequence = Intent.FireSequence;
    Request.SourceFactionId = Intent.SourceFactionId;
    Request.NavLayer = Intent.NavLayer;
    Request.ProjectileProfileId =
      CrowdDemoProjectileSchemas::ProjectileProfileId;
    Request.CollisionProfileId = 1;
    Request.EffectProfileId = 1;
    Request.Position = Intent.Position;
    Request.Velocity = Intent.Velocity;
    Request.RecalculateStableHash();
    if (!Request.IsValid()) return false;
  }
  InOutSummary.bValid = PlannerSummary.bValid;
  InOutSummary.TargetAcquiredCount +=
    PlannerSummary.TargetAcquiredCount;
  InOutSummary.CompletedWindupCount +=
    PlannerSummary.CompletedWindupCount;
  InOutSummary.InvalidTargetLifecycleCount +=
    PlannerSummary.InvalidTargetLifecycleCount;
  InOutSummary.AttackStateHash = HashAttackStates(InOutAgents);
  return InOutSummary.bValid;
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
    Target.EntityRef = Agent.EntityRef;
    Target.FactionId = Agent.FactionId;
    Target.NavLayer = Agent.NavLayer;
    Target.PreviousPosition =
      Agent.Position - Agent.Velocity * FixedStepSeconds;
    Target.Position = Agent.Position;
    Target.RadiusCm = Agent.RadiusCm;
    Target.bAlive = Agent.bAlive && Agent.Combat.bAlive;
    Target.RecalculateStableHash();
    if (!Target.IsValid())
    {
      UE_LOG(LogTemp, Error,
        TEXT("CrowdDemoProjectileTargetInvalid agent=%d lifecycle=%d radius=%.3f faction=%u layer=%u collision_mask=%u query_mask=%u position=%s previous=%s hash=%llu"),
        Agent.AgentId,
        Agent.LifecycleSerial,
        Agent.RadiusCm,
        Agent.FactionId,
        Agent.NavLayer,
        Target.CollisionMask,
        Target.QueryMask,
        *Target.Position.ToCompactString(),
        *Target.PreviousPosition.ToCompactString(),
        Target.StableHash);
      return false;
    }
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
  if (!BuildRangedAttackPlan(
      RoundId, FixedStepIndex, Settings,
      InOutAgents, Requests, OutSummary))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=attack_plan step=%d agents=%d"),
      FixedStepIndex, InOutAgents.Num());
    return false;
  }

  FCrowdProjectileBoundaryInput Input;
  Input.FixedStepIndex = FixedStepIndex;
  Input.ServerTimeSeconds = ServerTimeSeconds;
  Input.FixedStepSeconds = FixedStepSeconds;
  Input.Profiles.Add(BuildProfile(Settings, 65536));
  Input.SpawnRequests = MoveTemp(Requests);
  Input.CurrentStates.Append(CurrentStates);
  if (!BuildTargetSnapshots(
      FixedStepSeconds, InOutAgents, Input.Targets))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=targets step=%d agents=%d"),
      FixedStepIndex, InOutAgents.Num());
    return false;
  }
  const FCrowdDemoFlowObstacleCollisionSnapshotProvider
    EnvironmentProvider(FlowConfig);
  if (!EnvironmentProvider.Gather(
      FixedStepIndex, Input.EnvironmentBodies))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=environment step=%d"),
      FixedStepIndex);
    return false;
  }
  if (!FCrowdProjectileBoundaryPipeline::Prepare(
      Input, OutPrepared))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=pipeline_prepare step=%d spawns=%d states=%d targets=%d"),
      FixedStepIndex, Input.SpawnRequests.Num(),
      Input.CurrentStates.Num(), Input.Targets.Num());
    return false;
  }
  if (!FCrowdProjectileBoundaryPipeline::ValidatePrepared(
      Input, OutPrepared))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=pipeline_validate step=%d"),
      FixedStepIndex);
    return false;
  }

  FCrowdHitResolveResult ResolveResult;
  const TArray<FCrowdEffectProfile> EffectProfiles = {
    BuildEffectProfile(Settings)};
  if (!FCrowdCombatResolver::Resolve(
      OutPrepared.Impacts, EffectProfiles, ResolveResult)
    || !BuildDemoHitFacts(
      ResolveResult.Hits, InOutAgents, OutHitFacts))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoProjectilePrepareDetail reason=hit_resolve step=%d impacts=%d"),
      FixedStepIndex, OutPrepared.Impacts.Num());
    return false;
  }
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

bool FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
  const TConstArrayView<FCrowdHitFact> Hits,
  const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
  TArray<FCrowdDemoHitFact>& OutFacts)
{
  TArray<FCrowdDemoRangedCombatAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B)
  {
    return A.EntityRef < B.EntityRef;
  });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    const FCrowdDemoRangedCombatAgent& Agent = SortedAgents[Index];
    if (!Agent.EntityRef.IsValid() || Agent.AgentId == INDEX_NONE
      || (Index > 0
        && SortedAgents[Index - 1].EntityRef == Agent.EntityRef))
      return false;
  }

  if (!BuildDemoHitFacts(Hits, OutFacts))
    return false;
  for (int32 Index = 0; Index < Hits.Num(); ++Index)
  {
    const FCrowdHitFact& Hit = Hits[Index];
    const FCrowdDemoRangedCombatAgent* Source =
      SortedAgents.FindByPredicate(
        [&Hit](const FCrowdDemoRangedCombatAgent& Agent)
        {
          return Agent.EntityRef == Hit.Impact.Instigator;
        });
    const FCrowdDemoRangedCombatAgent* Target =
      SortedAgents.FindByPredicate(
        [&Hit](const FCrowdDemoRangedCombatAgent& Agent)
        {
          return Agent.EntityRef == Hit.Impact.Target;
        });
    if (!Source || !Target
      || Source->LifecycleSerial
        != static_cast<int32>(
          Hit.Impact.Instigator.LifecycleSerial)
      || Target->LifecycleSerial
        != static_cast<int32>(
          Hit.Impact.Target.LifecycleSerial))
    {
      OutFacts.Reset();
      return false;
    }
    FCrowdDemoHitFact* const Fact = OutFacts.FindByPredicate(
      [&Hit](const FCrowdDemoHitFact& Candidate)
      {
        return Candidate.HitEventId
          == Hit.Impact.ProjectileId;
      });
    if (!Fact)
    {
      OutFacts.Reset();
      return false;
    }
    Fact->SourceAgentId = Source->AgentId;
    Fact->SourceLifecycleSerial =
      Source->LifecycleSerial;
    Fact->TargetAgentId = Target->AgentId;
    Fact->TargetLifecycleSerial =
      Target->LifecycleSerial;
  }
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
