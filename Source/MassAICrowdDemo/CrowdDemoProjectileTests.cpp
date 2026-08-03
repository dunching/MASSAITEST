#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoAttackHostAdapter.h"
#include "Mass/CrowdDemoProjectileAdapters.h"
#include "MassCommonFragments.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdProjectileFragments.h"
#include "MassCrowdProjectileKernel.h"
#include "MassCrowdProjectileMassStore.h"
#include "MassCrowdSpatialSafety.h"
#include "MassEntityManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdDemoRangedCombatSettings MakeProjectileSettings()
  {
    FCrowdDemoRangedCombatSettings Settings;
    Settings.bEnabled = 1;
    Settings.ShooterCount = 1;
    Settings.WindupFixedSteps = 2;
    Settings.RecoveryFixedSteps = 1;
    Settings.CooldownFixedSteps = 2;
    Settings.ProjectileSpeedCmps = 6000.0f;
    Settings.ProjectileRadiusCm = 12.0f;
    Settings.ProjectileLifetimeFixedSteps = 3;
    Settings.MuzzleForwardOffsetCm = 0.0f;
    Settings.Damage = 20.0f;
    Settings.PositionQuantumCm = 1.0f;
    Settings.VelocityQuantumCmps = 1.0f;
    return Settings;
  }

  FCrowdDemoRangedCombatAgent MakeRangedAgent(
    const int32 AgentId,
    const int32 FormationIndex,
    const FVector& Position)
  {
    FCrowdDemoRangedCombatAgent Agent;
    Agent.EntityRef = {
      1,
      AgentId == 0
        ? 100000ull
        : static_cast<uint64>(AgentId),
      1};
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1;
    Agent.FormationIndex = FormationIndex;
    Agent.Position = Position;
    Agent.RadiusCm = 42.0f;
    Agent.bAlive = true;
    Agent.Combat.AgentId = AgentId;
    Agent.Combat.LifecycleSerial = 1;
    Agent.Combat.bAlive = true;
    Agent.Combat.LifecycleState = ECrowdDemoLifecycleState::Alive;
    return Agent;
  }

  FCrowdProjectileSpawnRequest MakeSpawnRequest(
    const uint64 ProjectileId,
    const int32 FixedStepIndex,
    const FVector& Position,
    const FVector& Velocity)
  {
    FCrowdProjectileSpawnRequest Request;
    Request.ProjectileId = ProjectileId;
    Request.FixedStepIndex = FixedStepIndex;
    Request.Instigator = {1, 1, 1};
    Request.Target = {1, 2, 1};
    Request.FireSequence = 1;
    Request.ProjectileProfileId =
      CrowdDemoProjectileSchemas::ProjectileProfileId;
    Request.CollisionProfileId = 1;
    Request.EffectProfileId = 1;
    Request.Position = Position;
    Request.Velocity = Velocity;
    Request.RecalculateStableHash();
    return Request;
  }

  struct FCrowdProjectilePublicApiFixture
  {
    static void BuildRangedAttackPlan(
      const int32 RoundId,
      const int32 FixedStepIndex,
      const FCrowdDemoRangedCombatSettings& Settings,
      TArray<FCrowdDemoRangedCombatAgent>& InOutAgents,
      TArray<FCrowdProjectileSpawnRequest>& OutRequests,
      FCrowdDemoProjectileStepSummary& InOutSummary)
    {
      FCrowdDemoProjectileAdapters::BuildRangedAttackPlan(
        RoundId, FixedStepIndex, Settings,
        InOutAgents, OutRequests, InOutSummary);
    }

    static void SpawnProjectiles(
      const int32 FixedStepIndex,
      const float ServerTimeSeconds,
      const FCrowdDemoRangedCombatSettings& Settings,
      const TConstArrayView<FCrowdProjectileSpawnRequest> Requests,
      TArray<FCrowdProjectileState>& InOutProjectiles,
      TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
      FCrowdDemoProjectileStepSummary& InOutSummary)
    {
      FCrowdProjectileProfile Profile =
        FCrowdDemoProjectileAdapters::BuildProfile(Settings, 65536);
      FCrowdProjectileStepSummary Summary;
      TArray<FCrowdProjectileLifecycleEvent> Events;
      if (!FCrowdProjectileKernel::Spawn(
          FixedStepIndex, ServerTimeSeconds,
          MakeArrayView(&Profile, 1), Requests,
          InOutProjectiles, Events, Summary))
        Summary.bValid = false;
      if (!InOutSummary.bValid)
        InOutSummary.bValid = Summary.bValid;
      FCrowdDemoProjectileAdapters::AppendVisualEvents(
        Events, OutEvents);
      FCrowdDemoProjectileAdapters::MergeSummary(
        Summary, InOutSummary);
    }

    static void AdvanceProjectiles(
      const int32 FixedStepIndex,
      const float ServerTimeSeconds,
      const float FixedStepSeconds,
      const FCrowdDemoRangedCombatSettings& Settings,
      const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
      const TConstArrayView<FCrowdSpatialEnvironmentBody>
        EnvironmentBodies,
      TArray<FCrowdProjectileState>& InOutProjectiles,
      TArray<FCrowdImpactFact>& OutImpacts,
      TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
      FCrowdDemoProjectileStepSummary& InOutSummary)
    {
      FCrowdProjectileProfile Profile =
        FCrowdDemoProjectileAdapters::BuildProfile(Settings, 65536);
      TArray<FCrowdProjectileTargetSnapshot> Targets;
      FCrowdProjectileStepSummary Summary;
      TArray<FCrowdProjectileLifecycleEvent> Events;
      if (!FCrowdDemoProjectileAdapters::BuildTargetSnapshots(
          FixedStepSeconds, Agents, Targets)
        || !FCrowdProjectileKernel::Advance(
          FixedStepIndex, ServerTimeSeconds, FixedStepSeconds,
          MakeArrayView(&Profile, 1), Targets, EnvironmentBodies,
          InOutProjectiles, OutImpacts, Events, Summary))
        Summary.bValid = false;
      if (!InOutSummary.bValid)
        InOutSummary.bValid = Summary.bValid;
      FCrowdDemoProjectileAdapters::AppendVisualEvents(
        Events, OutEvents);
      FCrowdDemoProjectileAdapters::MergeSummary(
        Summary, InOutSummary);
    }

    static void AdvanceProjectiles(
      const int32 FixedStepIndex,
      const float ServerTimeSeconds,
      const float FixedStepSeconds,
      const FCrowdDemoRangedCombatSettings& Settings,
      const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
      TArray<FCrowdProjectileState>& InOutProjectiles,
      TArray<FCrowdImpactFact>& OutImpacts,
      TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
      FCrowdDemoProjectileStepSummary& InOutSummary)
    {
      AdvanceProjectiles(
        FixedStepIndex, ServerTimeSeconds, FixedStepSeconds,
        Settings, Agents, {}, InOutProjectiles, OutImpacts,
        OutEvents, InOutSummary);
    }

    static void AdvanceProjectiles(
      const int32 FixedStepIndex,
      const float ServerTimeSeconds,
      const float FixedStepSeconds,
      const FCrowdDemoRangedCombatSettings& Settings,
      const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents,
      TArray<FCrowdProjectileState>& InOutProjectiles,
      TArray<FCrowdDemoHitFact>& OutHitFacts,
      TArray<FCrowdDemoProjectileVisualEvent>& OutEvents,
      FCrowdDemoProjectileStepSummary& InOutSummary)
    {
      TArray<FCrowdImpactFact> Impacts;
      AdvanceProjectiles(
        FixedStepIndex, ServerTimeSeconds, FixedStepSeconds,
        Settings, Agents, InOutProjectiles, Impacts,
        OutEvents, InOutSummary);
      TArray<FCrowdHitFact> Hits;
      FCrowdHitResolveResult ResolveResult;
      const TArray<FCrowdEffectProfile> Profiles = {
        FCrowdDemoProjectileAdapters::BuildEffectProfile(Settings)};
      if (!FCrowdCombatResolver::Resolve(
          Impacts, Profiles, ResolveResult)
        || !FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
          ResolveResult.Hits, OutHitFacts))
        InOutSummary.bValid = false;
    }

    static uint32 HashAttackStates(
      const TConstArrayView<FCrowdDemoRangedCombatAgent> Agents)
    {
      return FCrowdDemoProjectileAdapters::HashAttackStates(Agents);
    }

    static uint32 HashProjectileStates(
      const TConstArrayView<FCrowdProjectileState> Projectiles)
    {
      return FCrowdProjectileKernel::HashStates(Projectiles);
    }
  };

  struct FTenLaneSimulation
  {
    TArray<FCrowdDemoRangedCombatAgent> Agents;
    TArray<FCrowdProjectileState> Projectiles;
    int32 Spawned = 0;
    int32 Impacted = 0;
    int32 Expired = 0;
    int32 DamageApplied = 0;
    int32 DuplicateFire = 0;
    int32 DuplicateHit = 0;
  };

  FCrowdDemoRangedCombatSettings MakeTenLaneSettings()
  {
    FCrowdDemoRangedCombatSettings Settings;
    Settings.bEnabled = 1;
    Settings.ShooterCount = 10;
    Settings.WindupFixedSteps = 15;
    Settings.RecoveryFixedSteps = 12;
    Settings.CooldownFixedSteps = 30;
    Settings.ProjectileSpeedCmps = 1800.0f;
    Settings.ProjectileRadiusCm = 12.0f;
    Settings.ProjectileLifetimeFixedSteps = 60;
    Settings.MuzzleForwardOffsetCm = 70.0f;
    Settings.Damage = 20.0f;
    Settings.PositionQuantumCm = 1.0f;
    Settings.VelocityQuantumCmps = 1.0f;
    return Settings;
  }

  FTenLaneSimulation MakeTenLaneSimulation(const bool bReverseInput)
  {
    FTenLaneSimulation Simulation;
    for (int32 Lane = 0; Lane < 10; ++Lane)
    {
      const float X = (static_cast<float>(Lane) - 4.5f) * 128.0f;
      Simulation.Agents.Add(MakeRangedAgent(100 + Lane, Lane, FVector(X, -450.0f, 60.0f)));
      Simulation.Agents.Add(MakeRangedAgent(200 + Lane, 10 + Lane, FVector(X, 450.0f, 60.0f)));
    }
    if (bReverseInput)
    {
      Algo::Reverse(Simulation.Agents);
    }
    return Simulation;
  }

  void AdvanceTenLaneSimulation(
    FTenLaneSimulation& Simulation,
    const int32 FirstStep,
    const int32 LastStep)
  {
    const FCrowdDemoRangedCombatSettings Settings = MakeTenLaneSettings();
    FCrowdDemoHitResponseSettings HitSettings;
    HitSettings.FixedStepSeconds = 1.0f / 30.0f;
    for (int32 Step = FirstStep; Step <= LastStep; ++Step)
    {
      FCrowdDemoProjectileStepSummary ProjectileSummary;
      TArray<FCrowdProjectileSpawnRequest> Requests;
      TArray<FCrowdDemoProjectileVisualEvent> Events;
      TArray<FCrowdDemoHitFact> Hits;
      FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(
        17, Step, Settings, Simulation.Agents, Requests, ProjectileSummary);
      FCrowdProjectilePublicApiFixture::SpawnProjectiles(
        Step, Step / 30.0f, Settings, Requests,
        Simulation.Projectiles, Events, ProjectileSummary);
      FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
        Step, Step / 30.0f, 1.0f / 30.0f, Settings, Simulation.Agents,
        Simulation.Projectiles, Hits, Events, ProjectileSummary);

      TArray<FCrowdDemoCombatAgentState> CombatAgents;
      for (const FCrowdDemoRangedCombatAgent& Agent : Simulation.Agents)
      {
        CombatAgents.Add(Agent.Combat);
      }
      FCrowdDemoHitResponseSummary HitSummary;
      FCrowdDemoCombatStateKernel::ResolveHitFacts(
        Step, Step / 30.0f, Hits, HitSettings, CombatAgents, HitSummary);
      CombatAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      Simulation.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      for (int32 Index = 0; Index < Simulation.Agents.Num(); ++Index)
      {
        Simulation.Agents[Index].Combat = CombatAgents[Index];
        Simulation.Agents[Index].bAlive = CombatAgents[Index].bAlive;
      }

      Simulation.Spawned += ProjectileSummary.SpawnedCount;
      Simulation.Impacted += ProjectileSummary.ImpactedCount;
      Simulation.Expired += ProjectileSummary.ExpiredCount;
      Simulation.DuplicateFire += ProjectileSummary.DuplicateFireCount;
      Simulation.DuplicateHit += HitSummary.DuplicateHitCount;
      Simulation.DamageApplied += HitSummary.AppliedHitCount;
    }
  }

  namespace R7FixtureContract
  {
    inline constexpr FCrowdCapabilityProfileKey ProfileKey{60001u};
    inline constexpr FCrowdBehaviorSourceTypeId SourceTypeId{60001u};
    inline constexpr FCrowdBehaviorContextTypeId ContextTypeId{60001u};
    inline constexpr uint32 PayloadSchemaId = 60001u;

    struct FPayload
    {
      FVector DesiredVelocity = FVector::ZeroVector;
      FCrowdStableEntityRef TargetRef;
      uint64 CommitId = 0;
    };

    struct FContext
    {
      int32 VelocityScaleQ15 = CrowdBehavior::FullQ15Weight;
    };

    static_assert(std::is_trivially_copyable_v<FPayload>);
    static_assert(std::is_trivially_copyable_v<FContext>);
  }

  UWorld* FindR7MassTestWorld()
  {
    if (!GEngine)
      return nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
      if (Context.World()
        && (Context.WorldType == EWorldType::Editor
          || Context.WorldType == EWorldType::Game
          || Context.WorldType == EWorldType::PIE))
      {
        return Context.World();
      }
    }
    return nullptr;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileWindupSingleFireTest,
  "CrowdDemo.Combat.T8.WindupSingleFire",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileWindupSingleFireTest::RunTest(const FString& Parameters)
{
  const FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector),
    MakeRangedAgent(2, 1, FVector(600.0f, 0.0f, 0.0f))};
  FCrowdDemoProjectileStepSummary Summary;
  TArray<FCrowdProjectileSpawnRequest> Requests;

  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(7, 0, Settings, Agents, Requests, Summary);
  TestTrue(TEXT("settings and acquire valid"), Summary.bValid);
  TestEqual(TEXT("one target acquired"), Summary.TargetAcquiredCount, 1);
  TestEqual(TEXT("acquire does not fire"), Requests.Num(), 0);

  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(7, 1, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("windup not complete at step one"), Requests.Num(), 0);
  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(7, 2, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("windup emits one request"), Requests.Num(), 1);
  TestEqual(TEXT("one completed windup"), Summary.CompletedWindupCount, 1);
  const uint64 ProjectileId = Requests[0].ProjectileId;

  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(7, 2, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("same boundary cannot emit twice"), Requests.Num(), 0);
  TestEqual(TEXT("fire sequence remains one"), Agents[0].Combat.FireSequence, 1);
  TestTrue(TEXT("projectile id is stable and nonzero"), ProjectileId != 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileLifecycleInvalidationTest,
  "CrowdDemo.Combat.T8.LifecycleInvalidation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileLifecycleInvalidationTest::RunTest(const FString& Parameters)
{
  const FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector),
    MakeRangedAgent(2, 1, FVector(600.0f, 0.0f, 0.0f))};
  FCrowdDemoProjectileStepSummary Summary;
  TArray<FCrowdProjectileSpawnRequest> Requests;
  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(8, 0, Settings, Agents, Requests, Summary);

  Agents[1].LifecycleSerial = 2;
  Agents[1].Combat.LifecycleSerial = 2;
  FCrowdProjectilePublicApiFixture::BuildRangedAttackPlan(8, 2, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("stale lock emits no request"), Requests.Num(), 0);
  TestEqual(TEXT("shooter returns to acquire"), Agents[0].Combat.AttackPhase,
    ECrowdDemoAttackPhase::AcquireTarget);
  TestEqual(TEXT("invalid lifecycle counted"), Summary.InvalidTargetLifecycleCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileSweptEarliestHitTest,
  "CrowdDemo.Combat.T8.SweptEarliestHit",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileSweptEarliestHitTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileSpeedCmps = 60000.0f;
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    1001, 10, FVector(-1000.0f, 0.0f, 0.0f), FVector(60000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    10, 1.0f, Settings, MakeArrayView(&Request, 1), Projectiles, Events, Summary);

  TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(8, 2, FVector(300.0f, 0.0f, 0.0f)),
    MakeRangedAgent(5, 3, FVector(300.0f, 0.0f, 0.0f))};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    10, 1.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("high speed swept segment hits"), Hits.Num(), 1);
  TestEqual(TEXT("same hit time resolves lower AgentId"), Hits[0].TargetAgentId, 5);
  TestEqual(TEXT("impact counted"), Summary.ImpactedCount, 1);
  TestFalse(TEXT("impacted projectile inactive"), Projectiles[0].bActive);
  TestTrue(TEXT("impact position is before target center"), Projectiles[0].Position.X < 300.0f);

  const uint32 ProjectileHash = Summary.ProjectileStateHash;
  const uint32 EventHash = Summary.EventHash;
  Algo::Reverse(Agents);
  TArray<FCrowdProjectileState> ReversedProjectiles;
  TArray<FCrowdDemoProjectileVisualEvent> ReversedEvents;
  FCrowdDemoProjectileStepSummary ReversedSummary;
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    10, 1.0f, Settings, MakeArrayView(&Request, 1), ReversedProjectiles, ReversedEvents, ReversedSummary);
  TArray<FCrowdDemoHitFact> ReversedHits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    10, 1.0f, 1.0f / 30.0f, Settings, Agents,
    ReversedProjectiles, ReversedHits, ReversedEvents, ReversedSummary);
  TestEqual(TEXT("reversed input picks same target"), ReversedHits[0].TargetAgentId, 5);
  TestEqual(TEXT("reversed projectile hash"), ReversedSummary.ProjectileStateHash, ProjectileHash);
  TestEqual(TEXT("reversed event hash"), ReversedSummary.EventHash, EventHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileExpiryConservationTest,
  "CrowdDemo.Combat.T8.ExpiryConservation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileExpiryConservationTest::RunTest(const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileLifetimeFixedSteps = 2;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    2001, 20, FVector::ZeroVector, FVector(300.0f, 0.0f, 0.0f));
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    20, 2.0f, Settings, MakeArrayView(&Request, 1), Projectiles, Events, Summary);
  const TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector)};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    20, 2.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    21, 2.0f + 1.0f / 30.0f, 1.0f / 30.0f,
    Settings, Agents, Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("one projectile spawned"), Summary.SpawnedCount, 1);
  TestEqual(TEXT("one projectile expired"), Summary.ExpiredCount, 1);
  TestEqual(TEXT("no active projectile remains"), Summary.ActiveCount, 0);
  TestEqual(TEXT("lifecycle conservation"),
    Summary.SpawnedCount, Summary.ActiveCount + Summary.ImpactedCount + Summary.ExpiredCount);
  TestEqual(TEXT("spawn and expire visual events"), Events.Num(), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileDuplicateRequestTest,
  "CrowdDemo.Combat.T8.DuplicateRequest",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileDuplicateRequestTest::RunTest(const FString& Parameters)
{
  const FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    3001, 30, FVector::ZeroVector, FVector(6000.0f, 0.0f, 0.0f));
  const TArray<FCrowdProjectileSpawnRequest> DuplicateRequests = {Request, Request};
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    30, 3.0f, Settings, DuplicateRequests, Projectiles, Events, Summary);
  TestEqual(TEXT("duplicate request spawns one projectile"), Projectiles.Num(), 1);
  TestEqual(TEXT("duplicate fire counted"), Summary.DuplicateFireCount, 1);
  TestEqual(TEXT("single spawn event"), Events.Num(), 1);

  const TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector),
    MakeRangedAgent(2, 1, FVector(150.0f, 0.0f, 0.0f))};
  TArray<FCrowdDemoHitFact> FirstHits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    30, 3.0f, 1.0f / 30.0f, Settings, Agents,
    Projectiles, FirstHits, Events, Summary);
  TestEqual(TEXT("first advance emits one hit"), FirstHits.Num(), 1);
  TArray<FCrowdDemoHitFact> RepeatedHits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    30, 3.0f, 1.0f / 30.0f, Settings, Agents,
    Projectiles, RepeatedHits, Events, Summary);
  TestEqual(TEXT("inactive projectile cannot hit twice"),
    RepeatedHits.Num(), 0);
  TestEqual(TEXT("spawned equals impacted after hit"), Summary.SpawnedCount, Summary.ImpactedCount);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileImpactIdTickOrderingTest,
  "CrowdDemo.Combat.T8.ImpactIdTickOrdering",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileImpactIdTickOrderingTest::RunTest(
  const FString& Parameters)
{
  const FCrowdDemoRangedCombatSettings Settings =
    MakeProjectileSettings();
  const TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector),
    MakeRangedAgent(2, 1, FVector(150.0f, 0.0f, 0.0f))};
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;

  const FCrowdProjectileSpawnRequest EarlierRequest =
    MakeSpawnRequest(
      MAX_uint64 - 1, 10, FVector::ZeroVector,
      FVector(6000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    10, 1.0f, Settings, MakeArrayView(&EarlierRequest, 1),
    Projectiles, Events, Summary);
  TArray<FCrowdImpactFact> EarlierImpacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    10, 1.0f, 1.0f / 30.0f, Settings, Agents,
    Projectiles, EarlierImpacts, Events, Summary);

  const FCrowdProjectileSpawnRequest LaterRequest =
    MakeSpawnRequest(
      1, 11, FVector::ZeroVector,
      FVector(6000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    11, 1.0f + 1.0f / 30.0f, Settings,
    MakeArrayView(&LaterRequest, 1),
    Projectiles, Events, Summary);
  TArray<FCrowdImpactFact> LaterImpacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    11, 1.0f + 1.0f / 30.0f, 1.0f / 30.0f,
    Settings, Agents, Projectiles, LaterImpacts,
    Events, Summary);

  if (!TestEqual(TEXT("earlier tick emits one impact"),
      EarlierImpacts.Num(), 1)
    || !TestEqual(TEXT("later tick emits one impact"),
      LaterImpacts.Num(), 1))
    return false;
  TestTrue(TEXT("impact ids are nonzero"),
    EarlierImpacts[0].ImpactId != 0
      && LaterImpacts[0].ImpactId != 0);
  TestTrue(TEXT("later tick dominates projectile id ordering"),
    EarlierImpacts[0].ImpactId
      < LaterImpacts[0].ImpactId);
  TestEqual(TEXT("earlier impact carries its tick"),
    EarlierImpacts[0].FixedStepIndex, 10ll);
  TestEqual(TEXT("later impact carries its tick"),
    LaterImpacts[0].FixedStepIndex, 11ll);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileTenLaneRoundTest,
  "CrowdDemo.Combat.T8.TenLaneRound",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileTenLaneRoundTest::RunTest(const FString& Parameters)
{
  FTenLaneSimulation Forward = MakeTenLaneSimulation(false);
  FTenLaneSimulation Reversed = MakeTenLaneSimulation(true);
  AdvanceTenLaneSimulation(Forward, 0, 299);
  AdvanceTenLaneSimulation(Reversed, 0, 299);

  TestEqual(TEXT("fifty projectiles spawned"), Forward.Spawned, 50);
  TestEqual(TEXT("all projectiles impacted"), Forward.Impacted, 50);
  TestEqual(TEXT("no projectiles expired"), Forward.Expired, 0);
  TestEqual(TEXT("fifty hit facts applied"), Forward.DamageApplied, 50);
  TestEqual(TEXT("no duplicate fire"), Forward.DuplicateFire, 0);
  TestEqual(TEXT("no duplicate hit"), Forward.DuplicateHit, 0);
  TestEqual(TEXT("lifecycle conservation"),
    Forward.Spawned, Forward.Impacted + Forward.Expired);
  TestEqual(TEXT("reversed spawn count"), Reversed.Spawned, Forward.Spawned);
  TestEqual(TEXT("reversed impact count"), Reversed.Impacted, Forward.Impacted);
  TestEqual(TEXT("reversed combat hash"),
    FCrowdProjectilePublicApiFixture::HashAttackStates(Reversed.Agents),
    FCrowdProjectilePublicApiFixture::HashAttackStates(Forward.Agents));
  TestEqual(TEXT("reversed projectile hash"),
    FCrowdProjectilePublicApiFixture::HashProjectileStates(Reversed.Projectiles),
    FCrowdProjectilePublicApiFixture::HashProjectileStates(Forward.Projectiles));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileRollbackReplayTest,
  "CrowdDemo.Combat.T8.RollbackReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileRollbackReplayTest::RunTest(const FString& Parameters)
{
  FTenLaneSimulation Snapshot = MakeTenLaneSimulation(false);
  AdvanceTenLaneSimulation(Snapshot, 0, 120);
  FTenLaneSimulation Control = Snapshot;
  FTenLaneSimulation Replay = Snapshot;

  AdvanceTenLaneSimulation(Control, 121, 299);
  AdvanceTenLaneSimulation(Replay, 121, 220);
  Replay = Snapshot;
  AdvanceTenLaneSimulation(Replay, 121, 299);

  TestEqual(TEXT("replay spawn count"), Replay.Spawned, Control.Spawned);
  TestEqual(TEXT("replay impact count"), Replay.Impacted, Control.Impacted);
  TestEqual(TEXT("replay damage count"), Replay.DamageApplied, Control.DamageApplied);
  TestEqual(TEXT("replay has no duplicate fire"), Replay.DuplicateFire, 0);
  TestEqual(TEXT("replay has no duplicate hit"), Replay.DuplicateHit, 0);
  TestEqual(TEXT("replay attack state hash"),
    FCrowdProjectilePublicApiFixture::HashAttackStates(Replay.Agents),
    FCrowdProjectilePublicApiFixture::HashAttackStates(Control.Agents));
  TestEqual(TEXT("replay projectile state hash"),
    FCrowdProjectilePublicApiFixture::HashProjectileStates(Replay.Projectiles),
    FCrowdProjectilePublicApiFixture::HashProjectileStates(Control.Projectiles));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileRelativeSweepTest,
  "CrowdDemo.Combat.T8.RelativeSweepMovingTarget",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileRelativeSweepTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileSpeedCmps = 100.0f;
  Settings.ProjectileLifetimeFixedSteps = 10;
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9001, 1, FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  FCrowdDemoRangedCombatAgent Source =
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f));
  FCrowdDemoRangedCombatAgent Moving =
    MakeRangedAgent(2, 1, FVector(50.0f, 200.0f, 0.0f));
  Moving.Velocity = FVector(0.0f, 400.0f, 0.0f);
  const FCrowdDemoRangedCombatAgent Agents[] = {Source, Moving};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 1.0f, Settings, Agents,
    Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("relative sweep detects crossing moving target"),
    Hits.Num(), 1);
  TestEqual(TEXT("moving target identity remains deterministic"),
    Hits[0].TargetAgentId, 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileSpatialBroadphaseTest,
  "CrowdDemo.Combat.T8.SpatialBroadphase",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileSpatialBroadphaseTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileSpeedCmps = 1000.0f;
  Settings.ProjectileLifetimeFixedSteps = 10;
  TArray<FCrowdDemoRangedCombatAgent> Agents;
  Agents.Add(MakeRangedAgent(
    1, 0, FVector(-1000.0f, 0.0f, 0.0f)));
  Agents.Add(MakeRangedAgent(
    2, 1, FVector(500.0f, 0.0f, 0.0f)));
  for (int32 Index = 0; Index < 998; ++Index)
  {
    Agents.Add(MakeRangedAgent(
      Index + 3, Index + 2,
      FVector(
        10000.0f + Index * 300.0f,
        10000.0f, 3000.0f)));
  }
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9002, 1, FVector::ZeroVector,
    FVector(1000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 1.0f, Settings, Agents,
    Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("near target still hits"), Hits.Num(), 1);
  TestTrue(TEXT("broadphase avoids projectile times agent scan"),
    Summary.SweepTestCount < 50);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileHostResolverTest,
  "CrowdDemo.Combat.T8.GenericImpactHostResolver",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileHostResolverTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9010, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  Request.Instigator = {1, 101, 1};
  Request.Target = {1, 202, 1};
  Request.RecalculateStableHash();
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(2, 1, FVector(500.0f, 0.0f, 0.0f))};
  Agents[0].EntityRef = Request.Instigator;
  Agents[1].EntityRef = Request.Target;
  TArray<FCrowdImpactFact> Impacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 0.1f, Settings, Agents,
    Projectiles, Impacts, Events, Summary);
  TestEqual(TEXT("worker emits one neutral impact"), Impacts.Num(), 1);
  TestTrue(TEXT("neutral impact validates"),
    Impacts.Num() == 1 && Impacts[0].IsValid());

  TArray<FCrowdHitFact> Hits;
  const FCrowdDemoHostHitResolver Resolver(Settings);
  TestTrue(TEXT("host resolver accepts neutral impacts"),
    Resolver.Resolve(Impacts, Hits));
  TestEqual(TEXT("host resolver emits one generic hit"), Hits.Num(), 1);
  TestTrue(TEXT("generic hit validates"),
    Hits.Num() == 1 && Hits[0].IsValid());

  TArray<FCrowdDemoHitFact> DemoFacts;
  TestTrue(TEXT("demo adapter consumes generic hit"),
    FCrowdDemoProjectileAdapters::BuildDemoHitFacts(
      Hits, Agents, DemoFacts));
  TestEqual(TEXT("single demo damage fact"), DemoFacts.Num(), 1);
  TestEqual(TEXT("stable target ref maps to demo agent id"),
    DemoFacts[0].TargetAgentId, 2);
  TestEqual(TEXT("target lifecycle survives both adapters"),
    DemoFacts[0].TargetLifecycleSerial, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileEnvironmentPriorityTest,
  "CrowdDemo.Combat.T8.EnvironmentWallPriority",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileEnvironmentPriorityTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9020, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  const FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(2, 1, FVector(500.0f, 0.0f, 0.0f))};
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  FCrowdDemoSharedFlowObstacleSpec& WallSpec =
    FlowConfig.ObstacleSpecs.AddDefaulted_GetRef();
  WallSpec.ObstacleId = 77;
  WallSpec.Center = FVector(210.0f, 0.0f, 0.0f);
  WallSpec.Extent = FVector(10.0f, 100.0f, 0.0f);
  TArray<FCrowdSpatialEnvironmentBody> EnvironmentBodies;
  const FCrowdDemoFlowObstacleCollisionSnapshotProvider
    EnvironmentProvider(FlowConfig);
  TestTrue(TEXT("demo host gathers flow obstacle snapshot"),
    EnvironmentProvider.Gather(1, EnvironmentBodies));
  TestEqual(TEXT("one stable environment body"),
    EnvironmentBodies.Num(), 1);
  TArray<FCrowdImpactFact> Impacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 0.1f, Settings, Agents,
    EnvironmentBodies, Projectiles,
    Impacts, Events, Summary);
  TestEqual(TEXT("wall produces one earliest impact"),
    Impacts.Num(), 1);
  TestFalse(TEXT("environment impact is not a target hit"),
    Impacts[0].Target.IsValid());
  TestEqual(TEXT("environment profile survives sweep"),
    Impacts[0].CollisionProfileId, 1u);
  TestEqual(TEXT("environment impact counted"),
    Summary.EnvironmentImpactCount, 1);
  TArray<FCrowdHitFact> Hits;
  const FCrowdDemoHostHitResolver Resolver(Settings);
  TestTrue(TEXT("host resolver accepts environment impact"),
    Resolver.Resolve(Impacts, Hits));
  TestEqual(TEXT("wall never becomes gameplay damage"),
    Hits.Num(), 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectilePierceTest,
  "CrowdDemo.Combat.T8.PierceStableMultiImpact",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectilePierceTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectilePierceCount = 1;
  Settings.ProjectileLifetimeFixedSteps = 10;
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9030, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  const FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(2, 1, FVector(200.0f, 0.0f, 0.0f)),
    MakeRangedAgent(3, 2, FVector(500.0f, 0.0f, 0.0f))};
  TArray<FCrowdImpactFact> FirstImpacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 0.1f, Settings, Agents,
    Projectiles, FirstImpacts, Events, Summary);
  TestEqual(TEXT("first target is pierced"), FirstImpacts.Num(), 1);
  TestTrue(TEXT("projectile remains active after one pierce"),
    Projectiles.Num() == 1 && Projectiles[0].bActive);
  TestEqual(TEXT("pierce budget is consumed"),
    Projectiles[0].RemainingPierces, 0);
  TArray<FCrowdImpactFact> SecondImpacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    2, 1.1f, 0.1f, Settings, Agents,
    Projectiles, SecondImpacts, Events, Summary);
  TestEqual(TEXT("second boundary emits the terminal impact"),
    SecondImpacts.Num(), 1);
  TestFalse(TEXT("projectile retires after second target"),
    Projectiles[0].bActive);
  TestTrue(TEXT("impact ordering preserves distinct targets"),
    FirstImpacts[0].Target.StableEntityId == 2
      && SecondImpacts[0].Target.StableEntityId == 3);

  TArray<FCrowdHitFact> FirstHits;
  TArray<FCrowdHitFact> SecondHits;
  const FCrowdDemoHostHitResolver Resolver(Settings);
  TestTrue(TEXT("host resolver accepts first boundary impact"),
    Resolver.Resolve(FirstImpacts, FirstHits));
  TestTrue(TEXT("host resolver accepts second boundary impact"),
    Resolver.Resolve(SecondImpacts, SecondHits));
  TestEqual(TEXT("both pierced targets receive one hit"),
    FirstHits.Num() + SecondHits.Num(), 2);
  TArray<FCrowdImpactFact> DuplicateImpacts = FirstImpacts;
  DuplicateImpacts.Add(FirstImpacts[0]);
  TestFalse(TEXT("exact replayed impact is rejected"),
    Resolver.Resolve(DuplicateImpacts, FirstHits));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoProjectileFactionAndLayerTest,
  "CrowdDemo.Combat.T8.FactionAndNavLayerFiltering",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoProjectileFactionAndLayerTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileLifetimeFixedSteps = 10;
  FCrowdProjectileSpawnRequest Request = MakeSpawnRequest(
    9040, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  Request.SourceFactionId = 7;
  Request.NavLayer = 2;
  Request.RecalculateStableHash();
  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdProjectilePublicApiFixture::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  FCrowdDemoRangedCombatAgent Friendly =
    MakeRangedAgent(2, 1, FVector(150.0f, 0.0f, 0.0f));
  Friendly.FactionId = 7;
  Friendly.NavLayer = 2;
  FCrowdDemoRangedCombatAgent OtherLayer =
    MakeRangedAgent(3, 2, FVector(250.0f, 0.0f, 0.0f));
  OtherLayer.FactionId = 8;
  OtherLayer.NavLayer = 1;
  FCrowdDemoRangedCombatAgent Hostile =
    MakeRangedAgent(4, 3, FVector(500.0f, 0.0f, 0.0f));
  Hostile.FactionId = 8;
  Hostile.NavLayer = 2;
  const FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    Friendly, OtherLayer, Hostile};
  FCrowdSpatialEnvironmentBody OtherLayerWall;
  OtherLayerWall.StableSurfaceId = 99;
  OtherLayerWall.NavLayer = 1;
  OtherLayerWall.BoundsMin = FVector(300.0f, -100.0f, -100.0f);
  OtherLayerWall.BoundsMax = FVector(320.0f, 100.0f, 100.0f);
  OtherLayerWall.CollisionProfileId = 2;
  OtherLayerWall.EffectProfileId = 3;
  OtherLayerWall.RecalculateStableHash();
  TArray<FCrowdImpactFact> Impacts;
  FCrowdProjectilePublicApiFixture::AdvanceProjectiles(
    1, 1.0f, 0.1f, Settings, Agents,
    MakeArrayView(&OtherLayerWall, 1), Projectiles,
    Impacts, Events, Summary);
  TestEqual(TEXT("only same-layer hostile is hit"),
    Impacts.Num(), 1);
  TestEqual(TEXT("friendly and other layer are filtered"),
    Impacts[0].Target.StableEntityId, 4ull);
  TestEqual(TEXT("projectile collision profile is authoritative"),
    Impacts[0].CollisionProfileId, 1u);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoR7ThirdPartySourceMassProjectileGateTest,
  "CrowdDemo.Integration.R7.ThirdPartySourceMassProjectile20",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoR7ThirdPartySourceMassProjectileGateTest::RunTest(
  const FString& Parameters)
{
  constexpr int32 EntityCount = 20;
  constexpr int32 ProjectileCount = 10;
  constexpr int64 FixedStep = 50;

  FCrowdBehaviorSourceRuntime SourceRuntime;
  if (!TestTrue(TEXT("registry includes external providers"),
      SourceRuntime.InitializeFromRegisteredProviders()))
    return false;
  if (!TestNotNull(TEXT("fixture source is unknown to core but registered"),
      SourceRuntime.GetEvaluators().FindSpec(
        R7FixtureContract::SourceTypeId)))
    return false;

  for (int32 Index = 0; Index < EntityCount; ++Index)
  {
    const FCrowdStableEntityRef EntityRef{
      77, static_cast<uint64>(Index + 1), 1};
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey = CrowdDemoBehaviorSchemas::FullProfile;
    Binding.ModifierRevision = 1;
    Binding.ModifierCount = 1;
    Binding.Modifiers[0] = {
      {60001u}, ECrowdCapabilityModifierOperation::Add};
    if (!TestTrue(TEXT("fixture entity registers"),
        SourceRuntime.RegisterEntity(EntityRef, Binding)))
      return false;

    FCrowdBehaviorEntityEvaluationContext Evaluation;
    Evaluation.EntityRef = EntityRef;
    Evaluation.FixedStepIndex = FixedStep;
    Evaluation.Position = FVector(
      (Index % 10) * 150.0, Index < 10 ? -200.0 : 0.0, 60.0);
    Evaluation.Facing = FVector::YAxisVector;
    FCrowdBehaviorContextRecord& Record =
      Evaluation.Records.AddDefaulted_GetRef();
    R7FixtureContract::FContext Extra;
    if (!TestTrue(TEXT("fixture context serializes"),
        Record.Set(R7FixtureContract::ContextTypeId, 1, Extra)))
      return false;
    Evaluation.RecalculateStableHash();
    if (!TestTrue(TEXT("fixture context enters production runtime"),
        SourceRuntime.SetEvaluationContext(Evaluation)))
      return false;

    R7FixtureContract::FPayload Payload;
    Payload.DesiredVelocity = FVector(0.0, 120.0, 0.0);
    Payload.TargetRef = {
      77, static_cast<uint64>((Index + 10) % EntityCount + 1), 1};
    Payload.CommitId = 0x71000000ull + Index;
    FCrowdBehaviorSourceCommand Start;
    Start.EffectiveFixedStep = FixedStep;
    Start.Handle = {EntityRef, {0x7100u}, 1};
    Start.CommandSequence = 1;
    Start.Kind = ECrowdBehaviorSourceCommandKind::Start;
    Start.SourceTypeId = R7FixtureContract::SourceTypeId;
    if (!TestTrue(TEXT("fixture payload serializes"),
        Start.Payload.Set(
          R7FixtureContract::PayloadSchemaId, Payload))
      || !TestTrue(TEXT("fixture start queues"),
        SourceRuntime.QueueCommand(Start)))
      return false;

    const auto QueueSource = [&](
      const FCrowdBehaviorSourceTypeId TypeId,
      const uint32 SourceSequence,
      const uint32 CommandSequence,
      const FCrowdBehaviorSourcePayload& SourcePayload,
      const int32 LifetimeSteps = 0)
    {
      FCrowdBehaviorSourceCommand Command;
      Command.EffectiveFixedStep = FixedStep;
      Command.Handle = {
        EntityRef, {0x7200u}, SourceSequence};
      Command.CommandSequence = CommandSequence;
      Command.Kind = ECrowdBehaviorSourceCommandKind::Start;
      Command.SourceTypeId = TypeId;
      Command.LifetimeSteps = LifetimeSteps;
      Command.Payload = SourcePayload;
      return SourceRuntime.QueueCommand(Command);
    };

    FCrowdMoveToLocationPayload MovementPayload;
    MovementPayload.TargetLocation = FVector3f(
      Evaluation.Position.X, 1000.0f, Evaluation.Position.Z);
    MovementPayload.MaximumSpeedCmps = 120.0f;
    MovementPayload.AcceptanceRadiusCm = 10.0f;
    FCrowdBehaviorSourcePayload MovementSourcePayload;
    if (!TestTrue(TEXT("persistent movement source queues"),
        MovementSourcePayload.Set(
          CrowdStandardSources::PayloadSchema(
            CrowdStandardSources::MoveToLocation),
          MovementPayload)
        && QueueSource(
          CrowdStandardSources::MoveToLocation,
          1, 1, MovementSourcePayload)))
      return false;

    FCrowdDemoBehaviorSourcePayload CargoPayload;
    CargoPayload.PrimaryId = 7201;
    CargoPayload.SecondaryId = 1;
    FCrowdBehaviorSourcePayload CargoSourcePayload;
    if (!TestTrue(TEXT("persistent cargo source queues"),
        CargoSourcePayload.Set(
          CrowdDemoBehaviorSchemas::Standard, CargoPayload)
        && QueueSource(
          CrowdDemoSourceTypeIds::CarryCargo,
          2, 2, CargoSourcePayload)))
      return false;

    FCrowdDemoBehaviorSourcePayload BusinessPayload;
    BusinessPayload.TargetRef = Payload.TargetRef;
    BusinessPayload.CommitId = 0x72000000ull + Index;
    BusinessPayload.PrimaryId =
      CrowdDemoBehaviorAdapterIds::CargoPickup;
    BusinessPayload.SecondaryId = 7202;
    BusinessPayload.Quantity = 1;
    FCrowdBehaviorSourcePayload BusinessSourcePayload;
    if (!TestTrue(TEXT("persistent business source queues"),
        BusinessSourcePayload.Set(
          CrowdDemoBehaviorSchemas::Standard, BusinessPayload)
        && QueueSource(
          CrowdDemoSourceTypeIds::PickupInteraction,
          3, 3, BusinessSourcePayload)))
      return false;

    FCrowdTimedImpulsePayload HitReactionPayload;
    HitReactionPayload.InitialVelocity =
      FVector3f(-300.0f, 0.0f, 0.0f);
    HitReactionPayload.DecayMode =
      ECrowdImpulseDecayMode::Linear;
    FCrowdBehaviorSourcePayload HitReactionSourcePayload;
    if (!TestTrue(TEXT("temporary suppression source queues"),
        HitReactionSourcePayload.Set(
          CrowdStandardSources::PayloadSchema(
            CrowdStandardSources::TimedImpulse),
          HitReactionPayload)
        && QueueSource(
          CrowdStandardSources::TimedImpulse,
          4, 4, HitReactionSourcePayload, 2)))
      return false;
  }

  FCrowdBehaviorPreparedBoundary PreparedSources;
  if (!TestTrue(TEXT("20 fixture sources prepare together"),
      SourceRuntime.PrepareBoundary(
        FixedStep, PreparedSources))
    || !TestTrue(TEXT("fixture boundary validates"),
      SourceRuntime.ValidatePrepared(PreparedSources)))
    return false;
  TestEqual(TEXT("all 20 fixture entities resolve"),
    PreparedSources.Entities.Num(), EntityCount);
  for (const FCrowdBehaviorPreparedEntity& Entity :
    PreparedSources.Entities)
  {
    TestEqual(TEXT("fixture, task, cargo, business and suppression coexist"),
      Entity.StagedSourceSet.Instances.Num(), 5);
    TestTrue(TEXT("temporary source overrides resolved movement"),
      Entity.ResolvedChannels.DesiredVelocity.X < 0.0
        && !Entity.ResolvedChannels.bMovementLocked);
    TestTrue(TEXT("persistent business remains resolved under suppression"),
      Entity.ResolvedChannels.Business.Num() >= 2);
    TestTrue(TEXT("persistent presentation remains resolved under suppression"),
      Entity.ResolvedChannels.Presentation.Num() >= 2);
  }

  UWorld* TestWorld = FindR7MassTestWorld();
  if (!TestNotNull(TEXT("Mass test world is available"), TestWorld))
    return false;
  TSharedRef<FMassEntityManager> EntityManager =
    MakeShared<FMassEntityManager>(TestWorld);
  EntityManager->SetDebugName(
    TEXT("R7ThirdPartySourceMassProjectile20"));
  EntityManager->Initialize();
  EntityManager->PostInitialize();
  FCrowdMassProjectileStore ProjectileStore;
  if (!TestTrue(TEXT("Mass projectile store grows dynamically"),
      ProjectileStore.EnsureCapacity(
        *EntityManager, ProjectileCount, ProjectileCount)))
    return false;

  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileLifetimeFixedSteps = 10;
  const TArray<FCrowdProjectileProfile> Profiles = {
    FCrowdDemoProjectileAdapters::BuildProfile(
      Settings, ProjectileCount)};
  TArray<FCrowdDemoRangedCombatAgent> Agents;
  TArray<FCrowdProjectileSpawnRequest> Requests;
  for (int32 Lane = 0; Lane < ProjectileCount; ++Lane)
  {
    const FVector SourcePosition(Lane * 150.0, -200.0, 60.0);
    const FVector TargetPosition(Lane * 150.0, 0.0, 60.0);
    FCrowdDemoRangedCombatAgent Source =
      MakeRangedAgent(Lane + 1, Lane, SourcePosition);
    Source.FactionId = 1;
    FCrowdDemoRangedCombatAgent Target =
      MakeRangedAgent(
        ProjectileCount + Lane + 1,
        ProjectileCount + Lane,
        TargetPosition);
    Target.FactionId = 2;
    Agents.Add(Source);
    Agents.Add(Target);

    FCrowdProjectileSpawnRequest Request;
    Request.ProjectileId = 0x710000ull + Lane;
    Request.FixedStepIndex = FixedStep;
    Request.Instigator = {
      1, static_cast<uint64>(Source.AgentId),
      static_cast<uint32>(Source.LifecycleSerial)};
    Request.Target = {
      1, static_cast<uint64>(Target.AgentId),
      static_cast<uint32>(Target.LifecycleSerial)};
    Request.FireSequence = Lane + 1;
    Request.SourceFactionId = Source.FactionId;
    Request.ProjectileProfileId =
      CrowdDemoProjectileSchemas::ProjectileProfileId;
    Request.CollisionProfileId = 1;
    Request.EffectProfileId = 1;
    Request.Position = SourcePosition;
    Request.Velocity = FVector(0.0, 6000.0, 0.0);
    Request.RecalculateStableHash();
    Requests.Add(Request);
  }

  TArray<FCrowdProjectileState> Projectiles;
  TArray<FCrowdProjectileLifecycleEvent> Events;
  FCrowdProjectileStepSummary ProjectileSummary;
  TestTrue(TEXT("generic projectile kernel spawns"),
    FCrowdProjectileKernel::Spawn(
      FixedStep, FixedStep / 30.0f, Profiles, Requests,
      Projectiles, Events, ProjectileSummary));
  TestEqual(TEXT("representative concurrent projectiles spawn"),
    Projectiles.Num(), ProjectileCount);

  ProjectileStore.ApplyValidated(*EntityManager, Projectiles);
  TArray<FCrowdProjectileState> MassSnapshot;
  TestTrue(TEXT("Mass fragments gather authoritative projectiles"),
    ProjectileStore.Gather(*EntityManager, MassSnapshot));
  TestEqual(TEXT("all projectiles are represented by Mass fragments"),
    MassSnapshot.Num(), ProjectileCount);
  TestEqual(TEXT("Mass snapshot preserves projectile hash"),
    FCrowdProjectileKernel::HashStates(MassSnapshot),
    FCrowdProjectileKernel::HashStates(Projectiles));

  TArray<FCrowdProjectileTargetSnapshot> Targets;
  TestTrue(TEXT("Demo builds generic target snapshots"),
    FCrowdDemoProjectileAdapters::BuildTargetSnapshots(
      1.0f / 30.0f, Agents, Targets));
  TArray<FCrowdImpactFact> Impacts;
  TestTrue(TEXT("generic projectile kernel advances"),
    FCrowdProjectileKernel::Advance(
      FixedStep, FixedStep / 30.0f, 1.0f / 30.0f,
      Profiles, Targets, {}, MassSnapshot, Impacts,
      Events, ProjectileSummary));
  TestEqual(TEXT("all concurrent Mass projectiles hit once"),
    Impacts.Num(), ProjectileCount);
  TestEqual(TEXT("impact accounting is exact"),
    ProjectileSummary.ImpactedCount, ProjectileCount);
  TestTrue(TEXT("grid broadphase avoids Projectile x Agent scan"),
    ProjectileSummary.SweepTestCount < ProjectileCount * EntityCount);

  ProjectileStore.ApplyValidated(*EntityManager, MassSnapshot);
  TArray<FCrowdProjectileState> RetiredMassSnapshot;
  TestTrue(TEXT("retired Mass projectile state gathers"),
    ProjectileStore.Gather(*EntityManager, RetiredMassSnapshot));
  TestEqual(TEXT("impacted projectiles leave no active authority"),
    RetiredMassSnapshot.Num(), 0);
  ProjectileStore.DestroyAll(*EntityManager);

  if (!TestTrue(TEXT("combined suppressed Source boundary commits"),
      SourceRuntime.CommitPrepared(PreparedSources)))
    return false;

  constexpr int64 RecoveryStep = FixedStep + 3;
  for (int32 Index = 0; Index < EntityCount; ++Index)
  {
    FCrowdBehaviorEntityEvaluationContext Evaluation;
    Evaluation.EntityRef = {
      77, static_cast<uint64>(Index + 1), 1};
    Evaluation.FixedStepIndex = RecoveryStep;
    Evaluation.Position = FVector(
      (Index % 10) * 150.0, Index < 10 ? -200.0 : 0.0, 60.0);
    Evaluation.Facing = FVector::YAxisVector;
    FCrowdBehaviorContextRecord& Record =
      Evaluation.Records.AddDefaulted_GetRef();
    R7FixtureContract::FContext Extra;
    TestTrue(TEXT("recovery fixture context serializes"),
      Record.Set(R7FixtureContract::ContextTypeId, 1, Extra));
    Evaluation.RecalculateStableHash();
    if (!TestTrue(TEXT("recovery context enters runtime"),
        SourceRuntime.SetEvaluationContext(Evaluation)))
      return false;
  }
  FCrowdBehaviorPreparedBoundary RecoveredSources;
  if (!TestTrue(TEXT("expired suppression prepares recovery"),
      SourceRuntime.PrepareBoundary(
        RecoveryStep, RecoveredSources))
    || !TestTrue(TEXT("recovered boundary validates"),
      SourceRuntime.ValidatePrepared(RecoveredSources)))
    return false;
  TestEqual(TEXT("all 20 entities recover"),
    RecoveredSources.Entities.Num(), EntityCount);

  TArray<FCrowdSpatialSafetyAgent> SafetyAgents;
  for (int32 Index = 0; Index < EntityCount; ++Index)
    SafetyAgents.Add({
      {77, static_cast<uint64>(Index + 1), 1},
      FVector(
        (Index % 10) * 150.0,
        Index < 10 ? -200.0 : 0.0,
        60.0),
      35.0f});
  FCrowdSpatialSafetyIndex Safety;
  if (!TestTrue(TEXT("movement safety stage builds for 20 entities"),
      Safety.Build(SafetyAgents, 70.0f, 150.0f)))
    return false;
  int32 SafeMovementCount = 0;
  for (const FCrowdBehaviorPreparedEntity& Entity :
    RecoveredSources.Entities)
  {
    TestEqual(TEXT("only temporary suppression expires"),
      Entity.StagedSourceSet.Instances.Num(), 4);
    TestFalse(TEXT("movement unlocks after suppression"),
      Entity.ResolvedChannels.bMovementLocked);
    TestTrue(TEXT("business survives suppression recovery"),
      Entity.ResolvedChannels.Business.Num() >= 2);
    TestTrue(TEXT("cargo presentation survives suppression recovery"),
      Entity.ResolvedChannels.Presentation.Num() >= 2);
    const FCrowdSpatialSafetyAgent* Agent = SafetyAgents.FindByPredicate(
      [&Entity](const FCrowdSpatialSafetyAgent& Candidate)
      {
        return Candidate.EntityRef == Entity.EntityRef;
      });
    if (!Agent)
      return false;
    const FVector Candidate = Agent->Position
      + Entity.ResolvedChannels.DesiredVelocity
        .GetClampedToMaxSize(500.0f) / 30.0f;
    if (Safety.IsCandidateSafe(
        Entity.EntityRef, Candidate, Agent->RadiusCm))
    {
      TestTrue(TEXT("safe movement updates safety stage"),
        Safety.Update(Entity.EntityRef, Candidate));
      ++SafeMovementCount;
    }
  }
  TestEqual(TEXT("all recovered entities traverse movement safety stage"),
    SafeMovementCount, EntityCount);
  if (!TestTrue(TEXT("recovered Source boundary commits"),
      SourceRuntime.CommitPrepared(RecoveredSources)))
    return false;

  for (int32 Index = 0; Index < EntityCount; ++Index)
  {
    const FCrowdBehaviorSourceSet* SourceSet =
      SourceRuntime.FindSourceSet({
        77, static_cast<uint64>(Index + 1), 1});
    TestTrue(TEXT("projectile impacts and suppression preserve persistent sources"),
      SourceSet && SourceSet->Instances.Num() == 4);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoAttackHostAdapterTest,
  "CrowdDemo.Projectiles.MixedAttackHostAdapter",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoAttackHostAdapterTest::RunTest(
  const FString& Parameters)
{
  TArray<FCrowdDemoAttackTargetSnapshot> Targets;
  const auto AddTarget = [&Targets](
    const uint64 StableId, const uint32 FactionId,
    const FVector& Position)
  {
    FCrowdDemoAttackTargetSnapshot& Target =
      Targets.AddDefaulted_GetRef();
    Target.Body.EntityRef = {1, StableId, 1};
    Target.Body.StartPosition = Position;
    Target.Body.EndPosition = Position;
    Target.Body.RadiusCm = 42.0f;
    Target.Body.RecalculateStableHash();
    Target.FactionId = FactionId;
  };
  AddTarget(1, 1, FVector(0.0, 0.0, 0.0));
  AddTarget(11, 2, FVector(150.0, 0.0, 0.0));
  AddTarget(2, 1, FVector(0.0, 1000.0, 0.0));
  AddTarget(12, 2, FVector(450.0, 1000.0, 0.0));
  AddTarget(3, 1, FVector(0.0, 2000.0, 0.0));
  AddTarget(13, 2, FVector(850.0, 2000.0, 0.0));

  TArray<FCrowdDemoAttackIntent> Intents;
  const auto AddIntent = [&Intents](
    const uint64 ImpactId, const uint64 SourceId,
    const uint64 TargetId, const uint32 ProfileId,
    const uint32 PayloadTypeId,
    const ECrowdDemoAttackArchetype Archetype,
    const FVector& Position, const float Range)
  {
    FCrowdDemoAttackIntent& Intent =
      Intents.AddDefaulted_GetRef();
    Intent.ImpactId = ImpactId;
    Intent.FixedStepIndex = 10;
    Intent.Instigator = {1, SourceId, 1};
    Intent.Target = {1, TargetId, 1};
    Intent.AttackProfileId = ProfileId;
    Intent.PayloadTypeId = PayloadTypeId;
    Intent.EffectProfileId = 1;
    Intent.Archetype = Archetype;
    Intent.FireSequence = 1;
    Intent.SourceFactionId = 1;
    Intent.Position = Position;
    Intent.Direction = FVector::ForwardVector;
    Intent.TargetStartPosition =
      Position + FVector(Range, 0.0, 0.0);
    Intent.TargetEndPosition = Intent.TargetStartPosition;
    Intent.RangeCm = Range;
    Intent.QueryRadiusCm = 20.0f;
    Intent.ProjectileSpeedCmps =
      Archetype == ECrowdDemoAttackArchetype::Ranged
        ? 1000.0f : 0.0f;
    Intent.Damage = 20;
  };
  AddIntent(
    101, 1, 11, CrowdDemoAttackProfileIds::Melee,
    CrowdDemoAttackPayloadTypeIds::Melee,
    ECrowdDemoAttackArchetype::Melee,
    FVector(0.0, 0.0, 0.0), 300.0f);
  AddIntent(
    102, 2, 12, CrowdDemoAttackProfileIds::MidRange,
    CrowdDemoAttackPayloadTypeIds::MidRange,
    ECrowdDemoAttackArchetype::MidRange,
    FVector(0.0, 1000.0, 0.0), 600.0f);
  AddIntent(
    103, 3, 13, CrowdDemoAttackProfileIds::Ranged,
    CrowdDemoAttackPayloadTypeIds::Ranged,
    ECrowdDemoAttackArchetype::Ranged,
    FVector(0.0, 2000.0, 0.0), 1000.0f);

  FCrowdDemoPreparedAttackBoundary Prepared;
  if (!TestTrue(TEXT("mixed attack prepare"),
      FCrowdDemoAttackHostAdapter::Prepare(
        10, Intents, Targets, {}, Prepared)))
    return false;
  TestTrue(TEXT("prepared attack valid"), Prepared.IsValid());
  TestEqual(TEXT("melee intent count"),
    Prepared.MeleeIntentCount, 1);
  TestEqual(TEXT("mid-range intent count"),
    Prepared.MidRangeIntentCount, 1);
  TestEqual(TEXT("ranged intent count"),
    Prepared.RangedIntentCount, 1);
  TestEqual(TEXT("two immediate impacts"),
    Prepared.ImmediateImpacts.Num(), 2);
  TestEqual(TEXT("one projectile request"),
    Prepared.ProjectileRequests.Num(), 1);
  if (Prepared.ImmediateImpacts.IsEmpty())
    return false;
  if (Prepared.ImmediateImpacts.Num() == 2)
  {
    TestEqual(TEXT("melee impact type"),
      Prepared.ImmediateImpacts[0].ImpactTypeId,
      CrowdDemoAttackPayloadTypeIds::Melee);
    TestEqual(TEXT("mid-range impact type"),
      Prepared.ImmediateImpacts[1].ImpactTypeId,
      CrowdDemoAttackPayloadTypeIds::MidRange);
  }

  FCrowdHitFact Hit;
  Hit.Impact = Prepared.ImmediateImpacts[0];
  Hit.PayloadTypeId =
    CrowdDemoProjectileSchemas::HitPayloadTypeId;
  FCrowdDemoProjectileHitPayload Damage;
  Damage.Damage = 100.0f;
  TestTrue(TEXT("damage payload"),
    Hit.Payload.Set(
      CrowdDemoProjectileSchemas::HitPayloadSchemaId,
      Damage));
  Hit.RecalculateStableHash();
  TArray<FCrowdDemoAttackHealthState> HealthStates;
  HealthStates.Add({{1, 1, 1}, 1, 100, true});
  HealthStates.Add({{1, 11, 1}, 2, 100, true});
  FCrowdDemoPreparedAttackHealthPatch HealthPatch;
  TestTrue(TEXT("health patch prepare"),
    FCrowdDemoAttackHostAdapter::PrepareHealthPatch(
      10, MakeArrayView(&Hit, 1),
      HealthStates, HealthPatch));
  TestTrue(TEXT("health patch valid"), HealthPatch.IsValid());
  TestEqual(TEXT("one death"), HealthPatch.DeathCount, 1);
  const FCrowdDemoAttackHealthState* DeadState =
    HealthPatch.States.FindByPredicate(
      [](const FCrowdDemoAttackHealthState& State)
      {
        return State.EntityRef
          == FCrowdStableEntityRef{1, 11, 1};
      });
  TestNotNull(TEXT("dead state exists"), DeadState);
  if (DeadState)
    TestEqual(TEXT("health reaches zero"),
      DeadState->Health, 0);

  TArray<FCrowdHitFact> DuplicateHits = {Hit, Hit};
  TestFalse(TEXT("duplicate hit batch rejects"),
    FCrowdDemoAttackHostAdapter::PrepareHealthPatch(
      10, DuplicateHits, HealthStates, HealthPatch));
  return true;
}

#endif
