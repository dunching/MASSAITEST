#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Mass/CrowdDemoCombatStateKernel.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdDemoCombatAgentState MakeAgent(const int32 AgentId)
  {
    FCrowdDemoCombatAgentState Agent;
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1;
    Agent.VisualPhaseSeed = static_cast<uint32>(AgentId);
    return Agent;
  }

  FCrowdDemoHitFact MakeHit(
    const uint64 EventId,
    const int32 TargetId,
    const float Damage,
    const float Horizontal,
    const float Vertical)
  {
    FCrowdDemoHitFact Hit;
    Hit.HitEventId = EventId;
    Hit.ApplyFixedStep = 10;
    Hit.SourceAgentId = 99;
    Hit.SourceLifecycleSerial = 1;
    Hit.TargetAgentId = TargetId;
    Hit.TargetLifecycleSerial = 1;
    Hit.HitPosition = FVector(100.0f, 0.0f, 60.0f);
    Hit.HitDirection = FVector::ForwardVector;
    Hit.Damage = Damage;
    Hit.HorizontalImpulseCmps = Horizontal;
    Hit.VerticalImpulseCmps = Vertical;
    Hit.HitFlashProfileKey = 7;
    return Hit;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatHitFactDeterminismTest,
  "CrowdDemo.Combat.T7.HitFactDeterminism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatHitFactDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoCombatAgentState> Agents = {MakeAgent(1), MakeAgent(2)};
  TArray<FCrowdDemoHitFact> Hits = {
    MakeHit(102, 2, 15.0f, 100.0f, 0.0f),
    MakeHit(101, 1, 20.0f, 200.0f, 300.0f)};
  FCrowdDemoHitResponseSettings Settings;
  FCrowdDemoHitResponseSummary Summary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, Summary);
  TestTrue(TEXT("hit facts valid"), Summary.bValid);
  TestEqual(TEXT("applied hits"), Summary.AppliedHitCount, 2);
  TestEqual(TEXT("agent 1 health"), Agents[0].Health, 80.0f);
  TestEqual(TEXT("agent 2 health"), Agents[1].Health, 85.0f);
  TestEqual(TEXT("agent 1 reactive mode"), Agents[0].ReactiveMode,
    ECrowdDemoReactiveMotionMode::KnockUp);
  const uint32 Hash = Summary.StableHash;

  TArray<FCrowdDemoCombatAgentState> ReversedAgents = {MakeAgent(2), MakeAgent(1)};
  Algo::Reverse(Hits);
  FCrowdDemoHitResponseSummary ReversedSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, Hits, Settings, ReversedAgents, ReversedSummary);
  TestTrue(TEXT("reversed valid"), ReversedSummary.bValid);
  TestEqual(TEXT("reversed hash"), ReversedSummary.StableHash, Hash);
  TestEqual(TEXT("reversed agent hash"),
    FCrowdDemoCombatStateKernel::HashAgents(ReversedAgents),
    FCrowdDemoCombatStateKernel::HashAgents(Agents));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatHitDedupLifecycleDeathTest,
  "CrowdDemo.Combat.T7.HitDedupLifecycleDeath",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatHitDedupLifecycleDeathTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHitResponseSettings Settings;
  TArray<FCrowdDemoCombatAgentState> Agents = {MakeAgent(1)};
  TArray<FCrowdDemoHitFact> Hits = {
    MakeHit(100, 1, 25.0f, 100.0f, 0.0f),
    MakeHit(100, 1, 25.0f, 100.0f, 0.0f)};
  FCrowdDemoHitResponseSummary Summary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, Summary);
  TestTrue(TEXT("dedup valid"), Summary.bValid);
  TestEqual(TEXT("duplicate counted"), Summary.DuplicateHitCount, 1);
  TestEqual(TEXT("damage once"), Agents[0].Health, 75.0f);

  FCrowdDemoHitResponseSummary ReplaySummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(10, 1.0f, Hits, Settings, Agents, ReplaySummary);
  TestEqual(TEXT("replay no damage"), Agents[0].Health, 75.0f);
  TestEqual(TEXT("replay applied none"), ReplaySummary.AppliedHitCount, 0);

  FCrowdDemoHitFact Stale = MakeHit(101, 1, 10.0f, 0.0f, 0.0f);
  Stale.TargetLifecycleSerial = 2;
  FCrowdDemoHitResponseSummary StaleSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, MakeArrayView(&Stale, 1), Settings, Agents, StaleSummary);
  TestEqual(TEXT("stale lifecycle"), StaleSummary.StaleLifecycleCount, 1);
  TestEqual(TEXT("stale no damage"), Agents[0].Health, 75.0f);

  FCrowdDemoHitFact Lethal = MakeHit(102, 1, 100.0f, 0.0f, 0.0f);
  FCrowdDemoHitResponseSummary DeathSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    10, 1.0f, MakeArrayView(&Lethal, 1), Settings, Agents, DeathSummary);
  TestEqual(TEXT("death counted"), DeathSummary.DeathCount, 1);
  TestFalse(TEXT("dead not alive"), Agents[0].bAlive);
  TestEqual(TEXT("dead lifecycle"), Agents[0].LifecycleState, ECrowdDemoLifecycleState::Dead);
  TestEqual(TEXT("death visual"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(Agents[0], FVector::ZeroVector),
    ECrowdDemoVisualState::Death);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatBallisticVisualTest,
  "CrowdDemo.Combat.T7.BallisticAndVisual",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatBallisticVisualTest::RunTest(const FString& Parameters)
{
  FCrowdDemoHitResponseSettings Settings;
  FCrowdDemoCombatAgentState Agent = MakeAgent(1);
  Agent.BusinessState = ECrowdDemoBusinessState::Attacking;
  Agent.AttackPhase = ECrowdDemoAttackPhase::Windup;
  Agent.VisualState = ECrowdDemoVisualState::Attack;
  Agent.ReactiveMode = ECrowdDemoReactiveMotionMode::KnockUp;
  Agent.VerticalReactiveVelocityCmps = 500.0f;
  Agent.HorizontalReactiveVelocity = FVector(100.0f, 0.0f, 0.0f);
  Agent.RestoreBusinessState = ECrowdDemoBusinessState::Attacking;
  float Z = Settings.GroundZ;
  int32 ApexEvents = 0;
  int32 LandingEvents = 0;
  for (int32 Step = 10; Step < 200 && Agent.ReactiveMode != ECrowdDemoReactiveMotionMode::LandingRecovery; ++Step)
  {
    const auto Result = FCrowdDemoCombatStateKernel::AdvanceReactiveMotion(Step, Z, Settings, Agent);
    TestTrue(TEXT("ballistic step valid"), Result.bValid);
    Z = Result.NewZ;
    ApexEvents += Result.bReachedApex ? 1 : 0;
    LandingEvents += Result.bLanded ? 1 : 0;
  }
  TestEqual(TEXT("single apex"), ApexEvents, 1);
  TestEqual(TEXT("single landing"), LandingEvents, 1);
  TestEqual(TEXT("stored apex"), Agent.ApexCount, 1);
  TestEqual(TEXT("stored landing"), Agent.LandingCount, 1);
  TestEqual(TEXT("landed on ground"), Z, Settings.GroundZ);

  Agent.HitFlashRevision = 1;
  Agent.HitFlashStartServerTimeSeconds = 1.0f;
  Agent.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
  Agent.BusinessState = ECrowdDemoBusinessState::Attacking;
  Agent.AttackPhase = ECrowdDemoAttackPhase::Windup;
  TestEqual(TEXT("flash does not interrupt attack"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(Agent, FVector::ZeroVector),
    ECrowdDemoVisualState::Attack);
  FCrowdDemoCombatStateKernel::ResolveVisualStateBoundary(
    50, 2.0f, FVector::ZeroVector, Agent);
  TestEqual(TEXT("attack visual stored"), Agent.VisualState, ECrowdDemoVisualState::Attack);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatVatShowcaseMotionTest,
  "CrowdDemo.Combat.T7.ShowcaseMotion",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatVatShowcaseMotionTest::RunTest(const FString& Parameters)
{
  const FVector Anchor(100.0f, -200.0f, 60.0f);
  const auto Idle = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    0, 0, Anchor, Anchor);
  TestTrue(TEXT("idle valid"), Idle.bValid);
  TestFalse(TEXT("idle not moving group"), Idle.bMovingGroup);
  TestTrue(TEXT("idle velocity zero"), Idle.DesiredVelocity.IsNearlyZero());

  const auto MovingStart = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    4, 0, Anchor, Anchor);
  TestTrue(TEXT("moving valid"), MovingStart.bValid);
  TestTrue(TEXT("moving group"), MovingStart.bMovingGroup);
  TestEqual(TEXT("moving positive speed"), MovingStart.DesiredVelocity.X, 60.0);
  TestEqual(TEXT("moving bounded target"), MovingStart.DesiredLocation.X, 112.0);

  const auto MovingReturn = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    7, 6, Anchor + FVector(12.0f, 0.0f, 0.0f), Anchor);
  TestEqual(TEXT("moving return speed"), MovingReturn.DesiredVelocity.X, -60.0);
  TestEqual(TEXT("moving return target"), MovingReturn.DesiredLocation.X, 88.0);
  TestEqual(TEXT("moving deterministic hash"),
    FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
      7, 6, Anchor + FVector(12.0f, 0.0f, 0.0f), Anchor).StableHash,
    MovingReturn.StableHash);

  const auto Attack = FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
    8, 0, Anchor, Anchor);
  TestFalse(TEXT("attack not locomotion group"), Attack.bMovingGroup);
  TestTrue(TEXT("attack base velocity zero"), Attack.DesiredVelocity.IsNearlyZero());

  FCrowdDemoCombatAgentState ExplicitIdle = MakeAgent(20);
  ExplicitIdle.BusinessState = ECrowdDemoBusinessState::Idle;
  TestEqual(TEXT("generic velocity resolver remains velocity driven"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector(60.0f, 0.0f, 0.0f)),
    ECrowdDemoVisualState::Move);
  TestEqual(TEXT("showcase idle ignores particle drift"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector(60.0f, 0.0f, 0.0f), true),
    ECrowdDemoVisualState::Idle);
  ExplicitIdle.BusinessState = ECrowdDemoBusinessState::Moving;
  TestEqual(TEXT("showcase moving survives zero velocity"),
    FCrowdDemoCombatStateKernel::ResolveVisualState(
      ExplicitIdle, FVector::ZeroVector, true),
    ECrowdDemoVisualState::Move);

  FCrowdDemoVatShowcaseMotionSettings InvalidSettings;
  InvalidSettings.HalfCycleFixedSteps = 0;
  TestFalse(TEXT("invalid settings rejected"),
    FCrowdDemoCombatStateKernel::BuildVatShowcaseMotion(
      4, 0, Anchor, Anchor, InvalidSettings).bValid);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoCombatRollbackCompletionGateTest,
  "CrowdDemo.Combat.Rollback.CompletionGate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoCombatRollbackCompletionGateTest::RunTest(
  const FString& Parameters)
{
  auto* Pipeline = NewObject<UCrowdDemoRoundSimPipelineSubsystem>();
  FCrowdDemoRoundPlanPacket Plan;
  Plan.bValid = 1;
  Plan.RoundId = 1;
  Plan.Revision = 1;
  Plan.Rules.Scenario = ECrowdDemoScenario::SimRoundSoftPressure;
  Plan.Rules.FixedStepSeconds = 1.0f / 30.0f;
  Pipeline->ActivatePlan(Plan, 2, false);

  FCrowdMassBoundarySnapshot Boundary;
  Boundary.FixedStepIndex = Pipeline->GetCurrentFixedStepIndex();
  Boundary.PlanRevision = Pipeline->GetCurrentPlanRevision();
  Boundary.bValid = true;
  TArray<FCrowdDemoRoundBoundaryFormationFact> FormationFacts;
  for (const int32 AgentId : {10, 20})
  {
    FCrowdMassBoundaryAgentRecord& Agent = Boundary.Agents.AddDefaulted_GetRef();
    Agent.Identity.AgentId = AgentId;
    Agent.Identity.LifecycleSerial = 1;
    FCrowdDemoRoundBoundaryFormationFact& Formation =
      FormationFacts.AddDefaulted_GetRef();
    Formation.AgentId = AgentId;
  }
  TestTrue(TEXT("boundary snapshot accepted"),
    Pipeline->PublishBoundarySnapshot(MoveTemp(Boundary), MoveTemp(FormationFacts)));

  TArray<FCrowdDemoSoftPressureRollbackAgentState> MovementFacts;
  for (const int32 AgentId : {10, 20})
  {
    FCrowdDemoSoftPressureRollbackAgentState& Agent =
      MovementFacts.AddDefaulted_GetRef();
    Agent.AgentId = AgentId;
    Agent.LifecycleSerial = 1;
  }
  const int32 Step = Pipeline->GetCurrentFixedStepIndex();
  Pipeline->RecordSoftPressureRollbackSnapshot(Step, MoveTemp(MovementFacts));
  const FCrowdDemoSoftPressureRollbackSnapshot* Snapshot =
    Pipeline->FindSoftPressureRollbackSnapshot(Step);
  TestNotNull(TEXT("movement snapshot exists"), Snapshot);
  TestTrue(TEXT("movement facts complete"),
    Snapshot && Snapshot->bMovementFactsComplete);
  TestFalse(TEXT("incomplete snapshot is not replayable"),
    Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(Step));

  TArray<FCrowdDemoPreparedCombatRollbackFact> MissingFacts;
  MissingFacts.AddDefaulted_GetRef().AgentId = 10;
  TestFalse(TEXT("missing combat fact rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, MissingFacts));
  TArray<FCrowdDemoPreparedCombatRollbackFact> DuplicateFacts;
  DuplicateFacts.AddDefaulted_GetRef().AgentId = 10;
  DuplicateFacts.AddDefaulted_GetRef().AgentId = 10;
  TestFalse(TEXT("duplicate combat fact rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, DuplicateFacts));
  TArray<FCrowdDemoPreparedCombatRollbackFact> WrongFacts;
  WrongFacts.AddDefaulted_GetRef().AgentId = 10;
  WrongFacts.AddDefaulted_GetRef().AgentId = 30;
  TestFalse(TEXT("wrong combat AgentId rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, WrongFacts));

  TArray<FCrowdDemoPreparedCombatRollbackFact> CombatFacts;
  FCrowdDemoPreparedCombatRollbackFact& Agent20 = CombatFacts.AddDefaulted_GetRef();
  Agent20.AgentId = 20;
  Agent20.Combat.Health = 80.0f;
  Agent20.Combat.VisualState = ECrowdDemoVisualState::HitReact;
  FCrowdDemoPreparedCombatRollbackFact& Agent10 = CombatFacts.AddDefaulted_GetRef();
  Agent10.AgentId = 10;
  Agent10.Combat.Health = 90.0f;
  Agent10.Combat.VisualState = ECrowdDemoVisualState::Attack;
  TestTrue(TEXT("reverse-order final combat facts accepted"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, CombatFacts));
  TestTrue(TEXT("complete snapshot is replayable"),
    Pipeline->IsSoftPressureRollbackSnapshotReadyForReplay(Step));
  Snapshot = Pipeline->FindSoftPressureRollbackSnapshot(Step);
  TestEqual(TEXT("Agent 10 final visual state stored"),
    Snapshot->Agents[0].Combat.VisualState, ECrowdDemoVisualState::Attack);
  TestEqual(TEXT("Agent 20 final health stored"),
    Snapshot->Agents[1].Combat.Health, 80.0f);
  TestFalse(TEXT("duplicate completion rejected"),
    Pipeline->CompleteSoftPressureRollbackCombatState(Step, CombatFacts));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoPostFinalizeMinimalQueryStructureTest,
  "CrowdDemo.Architecture.PostFinalizeMinimalQuery",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoPostFinalizeMinimalQueryStructureTest::RunTest(
  const FString& Parameters)
{
  FString ProcessorSource;
  const FString ProcessorPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"));
  TestTrue(TEXT("processor source is readable"),
    FFileHelper::LoadFileToString(ProcessorSource, *ProcessorPath));

  const FString ConfigureMarker =
    TEXT("void UCrowdDemoRoundPostFinalizeMetricsProcessor::ConfigureQueries");
  const FString ExecuteMarker =
    TEXT("void UCrowdDemoRoundPostFinalizeMetricsProcessor::Execute");
  const int32 ConfigureStart = ProcessorSource.Find(ConfigureMarker);
  const int32 ExecuteStart = ProcessorSource.Find(ExecuteMarker, ESearchCase::CaseSensitive,
    ESearchDir::FromStart, ConfigureStart + ConfigureMarker.Len());
  TestTrue(TEXT("post-finalize configure block found"),
    ConfigureStart != INDEX_NONE && ExecuteStart > ConfigureStart);
  if (ConfigureStart == INDEX_NONE || ExecuteStart <= ConfigureStart)
  {
    return false;
  }

  const FString ConfigureBlock = ProcessorSource.Mid(
    ConfigureStart, ExecuteStart - ConfigureStart);
  TestTrue(TEXT("post-finalize reads identity"),
    ConfigureBlock.Contains(TEXT("FCrowdDemoMassIdentityFragment")));
  TestTrue(TEXT("post-finalize reads final RoundSim state"),
    ConfigureBlock.Contains(TEXT("FCrowdDemoRoundSimStateFragment")));

  const TCHAR* ForbiddenRequirements[] = {
    TEXT("FCrowdDemoOpenSpawnRelaxationFragment"),
    TEXT("FCrowdDemoMassStatsFragment"),
    TEXT("FCrowdDemoBusinessStateFragment"),
    TEXT("FCrowdDemoRangedAttackFragment"),
    TEXT("FCrowdDemoReactiveMotionFragment"),
    TEXT("FCrowdDemoHitFlashFragment"),
    TEXT("FCrowdDemoMassVisualFragment")
  };
  for (const TCHAR* Forbidden : ForbiddenRequirements)
  {
    TestFalse(FString::Printf(TEXT("post-finalize excludes %s"), Forbidden),
      ConfigureBlock.Contains(Forbidden));
  }

  FString FragmentHeader;
  const FString FragmentPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassFragments.h"));
  TestTrue(TEXT("fragment header is readable"),
    FFileHelper::LoadFileToString(FragmentHeader, *FragmentPath));
  TestFalse(TEXT("OpenSpawn fragment is physically deleted"),
    FragmentHeader.Contains(TEXT("FCrowdDemoOpenSpawnRelaxationFragment")));

  const TCHAR* DeletedCompatibilityFragments[] = {
    TEXT("FCrowdDemoRoundMoveIntentFragment"),
    TEXT("FCrowdDemoRoundGuidanceCandidatesFragment"),
    TEXT("FCrowdDemoRoundComposedGuidanceFragment"),
    TEXT("FCrowdDemoRoundLocalVelocityFragment"),
    TEXT("FCrowdDemoRoundParticleConstraintFragment"),
    TEXT("FCrowdDemoRoundFacingFragment")
  };
  for (const TCHAR* DeletedFragment : DeletedCompatibilityFragments)
  {
    TestFalse(FString::Printf(TEXT("compatibility fragment %s is physically deleted"),
      DeletedFragment), FragmentHeader.Contains(DeletedFragment));
    TestFalse(FString::Printf(TEXT("processor source does not use %s"),
      DeletedFragment), ProcessorSource.Contains(DeletedFragment));
  }

  FString MassSubsystemSource;
  const FString MassSubsystemPath = FPaths::Combine(
    FPaths::ProjectDir(),
    TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassSubsystem.cpp"));
  TestTrue(TEXT("Mass subsystem source is readable"),
    FFileHelper::LoadFileToString(MassSubsystemSource, *MassSubsystemPath));
  for (const TCHAR* DeletedFragment : DeletedCompatibilityFragments)
  {
    TestFalse(FString::Printf(TEXT("Mass template excludes %s"), DeletedFragment),
      MassSubsystemSource.Contains(DeletedFragment));
  }
  return true;
}

#endif
