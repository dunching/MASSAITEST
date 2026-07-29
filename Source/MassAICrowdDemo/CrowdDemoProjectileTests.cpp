#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "CrowdDemoBehaviorSourceProvider.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/CrowdDemoMassFragments.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Mass/CrowdDemoProjectileKernel.h"
#include "MassCommonFragments.h"
#include "MassCrowdBehaviorSourceRuntime.h"
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

  FCrowdDemoProjectileSpawnRequest MakeSpawnRequest(
    const uint64 ProjectileId,
    const int32 FixedStepIndex,
    const FVector& Position,
    const FVector& Velocity)
  {
    FCrowdDemoProjectileSpawnRequest Request;
    Request.ProjectileId = ProjectileId;
    Request.FixedStepIndex = FixedStepIndex;
    Request.SourceAgentId = 1;
    Request.SourceLifecycleSerial = 1;
    Request.TargetAgentId = 2;
    Request.TargetLifecycleSerial = 1;
    Request.FireSequence = 1;
    Request.Position = Position;
    Request.Velocity = Velocity;
    return Request;
  }

  struct FTenLaneSimulation
  {
    TArray<FCrowdDemoRangedCombatAgent> Agents;
    TArray<FCrowdDemoProjectileState> Projectiles;
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
      TArray<FCrowdDemoProjectileSpawnRequest> Requests;
      TArray<FCrowdDemoProjectileVisualEvent> Events;
      TArray<FCrowdDemoHitFact> Hits;
      FCrowdDemoProjectileKernel::AdvanceAttackPhases(
        17, Step, Settings, Simulation.Agents, Requests, ProjectileSummary);
      FCrowdDemoProjectileKernel::SpawnProjectiles(
        Step, Step / 30.0f, Settings, Requests,
        Simulation.Projectiles, Events, ProjectileSummary);
      FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  TArray<FCrowdDemoProjectileSpawnRequest> Requests;

  FCrowdDemoProjectileKernel::AdvanceAttackPhases(7, 0, Settings, Agents, Requests, Summary);
  TestTrue(TEXT("settings and acquire valid"), Summary.bValid);
  TestEqual(TEXT("one target acquired"), Summary.TargetAcquiredCount, 1);
  TestEqual(TEXT("acquire does not fire"), Requests.Num(), 0);

  FCrowdDemoProjectileKernel::AdvanceAttackPhases(7, 1, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("windup not complete at step one"), Requests.Num(), 0);
  FCrowdDemoProjectileKernel::AdvanceAttackPhases(7, 2, Settings, Agents, Requests, Summary);
  TestEqual(TEXT("windup emits one request"), Requests.Num(), 1);
  TestEqual(TEXT("one completed windup"), Summary.CompletedWindupCount, 1);
  const uint64 ProjectileId = Requests[0].ProjectileId;

  FCrowdDemoProjectileKernel::AdvanceAttackPhases(7, 2, Settings, Agents, Requests, Summary);
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
  TArray<FCrowdDemoProjectileSpawnRequest> Requests;
  FCrowdDemoProjectileKernel::AdvanceAttackPhases(8, 0, Settings, Agents, Requests, Summary);

  Agents[1].LifecycleSerial = 2;
  Agents[1].Combat.LifecycleSerial = 2;
  FCrowdDemoProjectileKernel::AdvanceAttackPhases(8, 2, Settings, Agents, Requests, Summary);
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    1001, 10, FVector(-1000.0f, 0.0f, 0.0f), FVector(60000.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    10, 1.0f, Settings, MakeArrayView(&Request, 1), Projectiles, Events, Summary);

  TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(8, 2, FVector(300.0f, 0.0f, 0.0f)),
    MakeRangedAgent(5, 3, FVector(300.0f, 0.0f, 0.0f))};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    10, 1.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("high speed swept segment hits"), Hits.Num(), 1);
  TestEqual(TEXT("same hit time resolves lower AgentId"), Hits[0].TargetAgentId, 5);
  TestEqual(TEXT("impact counted"), Summary.ImpactedCount, 1);
  TestFalse(TEXT("impacted projectile inactive"), Projectiles[0].bActive);
  TestTrue(TEXT("impact position is before target center"), Projectiles[0].Position.X < 300.0f);

  const uint32 ProjectileHash = Summary.ProjectileStateHash;
  const uint32 EventHash = Summary.EventHash;
  Algo::Reverse(Agents);
  TArray<FCrowdDemoProjectileState> ReversedProjectiles;
  TArray<FCrowdDemoProjectileVisualEvent> ReversedEvents;
  FCrowdDemoProjectileStepSummary ReversedSummary;
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    10, 1.0f, Settings, MakeArrayView(&Request, 1), ReversedProjectiles, ReversedEvents, ReversedSummary);
  TArray<FCrowdDemoHitFact> ReversedHits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    2001, 20, FVector::ZeroVector, FVector(300.0f, 0.0f, 0.0f));
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    20, 2.0f, Settings, MakeArrayView(&Request, 1), Projectiles, Events, Summary);
  const TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector)};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    20, 2.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    3001, 30, FVector::ZeroVector, FVector(6000.0f, 0.0f, 0.0f));
  const TArray<FCrowdDemoProjectileSpawnRequest> DuplicateRequests = {Request, Request};
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    30, 3.0f, Settings, DuplicateRequests, Projectiles, Events, Summary);
  TestEqual(TEXT("duplicate request spawns one projectile"), Projectiles.Num(), 1);
  TestEqual(TEXT("duplicate fire counted"), Summary.DuplicateFireCount, 1);
  TestEqual(TEXT("single spawn event"), Events.Num(), 1);

  const TArray<FCrowdDemoRangedCombatAgent> Agents = {
    MakeRangedAgent(1, 0, FVector::ZeroVector),
    MakeRangedAgent(2, 1, FVector(150.0f, 0.0f, 0.0f))};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    30, 3.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    30, 3.0f, 1.0f / 30.0f, Settings, Agents, Projectiles, Hits, Events, Summary);
  TestEqual(TEXT("inactive projectile cannot hit twice"), Hits.Num(), 1);
  TestEqual(TEXT("spawned equals impacted after hit"), Summary.SpawnedCount, Summary.ImpactedCount);
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
    FCrowdDemoProjectileKernel::HashAttackStates(Reversed.Agents),
    FCrowdDemoProjectileKernel::HashAttackStates(Forward.Agents));
  TestEqual(TEXT("reversed projectile hash"),
    FCrowdDemoProjectileKernel::HashProjectileStates(Reversed.Projectiles),
    FCrowdDemoProjectileKernel::HashProjectileStates(Forward.Projectiles));
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
    FCrowdDemoProjectileKernel::HashAttackStates(Replay.Agents),
    FCrowdDemoProjectileKernel::HashAttackStates(Control.Agents));
  TestEqual(TEXT("replay projectile state hash"),
    FCrowdDemoProjectileKernel::HashProjectileStates(Replay.Projectiles),
    FCrowdDemoProjectileKernel::HashProjectileStates(Control.Projectiles));
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9001, 1, FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  FCrowdDemoRangedCombatAgent Source =
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f));
  FCrowdDemoRangedCombatAgent Moving =
    MakeRangedAgent(2, 1, FVector(50.0f, 200.0f, 0.0f));
  Moving.Velocity = FVector(0.0f, 400.0f, 0.0f);
  const FCrowdDemoRangedCombatAgent Agents[] = {Source, Moving};
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9002, 1, FVector::ZeroVector,
    FVector(1000.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  TArray<FCrowdDemoHitFact> Hits;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9010, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  const FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(2, 1, FVector(500.0f, 0.0f, 0.0f))};
  TArray<FCrowdImpactFact> Impacts;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
    FCrowdDemoHostHitResolver::BuildDemoHitFacts(Hits, DemoFacts));
  TestEqual(TEXT("single demo damage fact"), DemoFacts.Num(), 1);
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9020, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
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
  TArray<FCrowdProjectileEnvironmentBody> EnvironmentBodies;
  const FCrowdDemoFlowObstacleCollisionSnapshotProvider
    EnvironmentProvider(FlowConfig);
  TestTrue(TEXT("demo host gathers flow obstacle snapshot"),
    EnvironmentProvider.Gather(1, EnvironmentBodies));
  TestEqual(TEXT("one stable environment body"),
    EnvironmentBodies.Num(), 1);
  TArray<FCrowdImpactFact> Impacts;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  const FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9030, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    1, 0.0f, Settings, MakeArrayView(&Request, 1),
    Projectiles, Events, Summary);
  const FCrowdDemoRangedCombatAgent Agents[] = {
    MakeRangedAgent(1, 0, FVector(-1000.0f, 0.0f, 0.0f)),
    MakeRangedAgent(2, 1, FVector(200.0f, 0.0f, 0.0f)),
    MakeRangedAgent(3, 2, FVector(500.0f, 0.0f, 0.0f))};
  TArray<FCrowdImpactFact> Impacts;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    1, 1.0f, 0.1f, Settings, Agents,
    Projectiles, Impacts, Events, Summary);
  TestEqual(TEXT("first target is pierced"), Impacts.Num(), 1);
  TestTrue(TEXT("projectile remains active after one pierce"),
    Projectiles.Num() == 1 && Projectiles[0].bActive);
  TestEqual(TEXT("pierce budget is consumed"),
    Projectiles[0].RemainingPierces, 0);
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    2, 1.1f, 0.1f, Settings, Agents,
    Projectiles, Impacts, Events, Summary);
  TestEqual(TEXT("one projectile emits two stable impacts"),
    Impacts.Num(), 2);
  TestFalse(TEXT("projectile retires after second target"),
    Projectiles[0].bActive);
  TestTrue(TEXT("impact ordering preserves distinct targets"),
    Impacts[0].Target.StableEntityId == 2
      && Impacts[1].Target.StableEntityId == 3);

  TArray<FCrowdHitFact> Hits;
  const FCrowdDemoHostHitResolver Resolver(Settings);
  TestTrue(TEXT("host resolver accepts multiple impacts per projectile"),
    Resolver.Resolve(Impacts, Hits));
  TestEqual(TEXT("both pierced targets receive one hit"), Hits.Num(), 2);
  TArray<FCrowdImpactFact> DuplicateImpacts = Impacts;
  DuplicateImpacts.Add(Impacts[0]);
  TestFalse(TEXT("exact replayed impact is rejected"),
    Resolver.Resolve(DuplicateImpacts, Hits));
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
  FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
    9040, 1, FVector::ZeroVector,
    FVector(6000.0f, 0.0f, 0.0f));
  Request.SourceFactionId = 7;
  Request.NavLayer = 2;
  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary Summary;
  FCrowdDemoProjectileKernel::SpawnProjectiles(
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
  FCrowdProjectileEnvironmentBody OtherLayerWall;
  OtherLayerWall.StableSurfaceId = 99;
  OtherLayerWall.NavLayer = 1;
  OtherLayerWall.BoundsMin = FVector(300.0f, -100.0f, -100.0f);
  OtherLayerWall.BoundsMax = FVector(320.0f, 100.0f, 100.0f);
  OtherLayerWall.CollisionProfileId = 2;
  OtherLayerWall.EffectProfileId = 3;
  OtherLayerWall.RecalculateStableHash();
  TArray<FCrowdImpactFact> Impacts;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
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
  const TArray<const UScriptStruct*> ProjectileTypes = {
    FCrowdDemoMassProjectileTag::StaticStruct(),
    FCrowdDemoMassProjectileFragment::StaticStruct(),
    FTransformFragment::StaticStruct()};
  const FMassArchetypeHandle ProjectileArchetype =
    EntityManager->CreateArchetype(ProjectileTypes);
  if (!TestTrue(TEXT("Mass projectile archetype is valid"),
      ProjectileArchetype.IsValid()))
    return false;
  TArray<FMassEntityHandle> ProjectileEntities;
  for (int32 Index = 0; Index < ProjectileCount; ++Index)
    ProjectileEntities.Add(
      EntityManager->CreateEntity(ProjectileArchetype));

  FCrowdDemoRangedCombatSettings Settings = MakeProjectileSettings();
  Settings.ProjectileLifetimeFixedSteps = 10;
  TArray<FCrowdDemoRangedCombatAgent> Agents;
  TArray<FCrowdDemoProjectileSpawnRequest> Requests;
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

    FCrowdDemoProjectileSpawnRequest Request = MakeSpawnRequest(
      0x710000ull + Lane,
      static_cast<int32>(FixedStep),
      SourcePosition,
      FVector(0.0, 6000.0, 0.0));
    Request.SourceAgentId = Source.AgentId;
    Request.TargetAgentId = Target.AgentId;
    Request.SourceFactionId = Source.FactionId;
    Requests.Add(Request);
  }

  TArray<FCrowdDemoProjectileState> Projectiles;
  TArray<FCrowdDemoProjectileVisualEvent> Events;
  FCrowdDemoProjectileStepSummary ProjectileSummary;
  FCrowdDemoProjectileKernel::SpawnProjectiles(
    static_cast<int32>(FixedStep),
    FixedStep / 30.0f,
    Settings,
    Requests,
    Projectiles,
    Events,
    ProjectileSummary);
  TestEqual(TEXT("representative concurrent projectiles spawn"),
    Projectiles.Num(), ProjectileCount);

  FCrowdDemoMassProjectileStoreAdapter::ApplyValidated(
    *EntityManager,
    ProjectileEntities,
    Projectiles);
  TArray<FCrowdDemoProjectileState> MassSnapshot;
  TestTrue(TEXT("Mass fragments gather authoritative projectiles"),
    FCrowdDemoMassProjectileStoreAdapter::Gather(
      *EntityManager,
      ProjectileEntities,
      MassSnapshot));
  TestEqual(TEXT("all projectiles are represented by Mass fragments"),
    MassSnapshot.Num(), ProjectileCount);
  TestEqual(TEXT("Mass snapshot preserves projectile hash"),
    FCrowdDemoProjectileKernel::HashProjectileStates(MassSnapshot),
    FCrowdDemoProjectileKernel::HashProjectileStates(Projectiles));

  TArray<FCrowdImpactFact> Impacts;
  FCrowdDemoProjectileKernel::AdvanceProjectiles(
    static_cast<int32>(FixedStep),
    FixedStep / 30.0f,
    1.0f / 30.0f,
    Settings,
    Agents,
    MassSnapshot,
    Impacts,
    Events,
    ProjectileSummary);
  TestEqual(TEXT("all concurrent Mass projectiles hit once"),
    Impacts.Num(), ProjectileCount);
  TestEqual(TEXT("impact accounting is exact"),
    ProjectileSummary.ImpactedCount, ProjectileCount);
  TestTrue(TEXT("grid broadphase avoids Projectile x Agent scan"),
    ProjectileSummary.SweepTestCount < ProjectileCount * EntityCount);

  FCrowdDemoMassProjectileStoreAdapter::ApplyValidated(
    *EntityManager,
    ProjectileEntities,
    MassSnapshot);
  TArray<FCrowdDemoProjectileState> RetiredMassSnapshot;
  TestTrue(TEXT("retired Mass projectile state gathers"),
    FCrowdDemoMassProjectileStoreAdapter::Gather(
      *EntityManager,
      ProjectileEntities,
      RetiredMassSnapshot));
  TestEqual(TEXT("impacted projectiles leave no active authority"),
    RetiredMassSnapshot.Num(), 0);

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

#endif
