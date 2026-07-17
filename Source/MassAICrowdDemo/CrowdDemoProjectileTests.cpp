#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "Mass/CrowdDemoProjectileKernel.h"

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

#endif
