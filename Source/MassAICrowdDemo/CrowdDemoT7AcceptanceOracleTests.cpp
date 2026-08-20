#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoT7AcceptanceOracle.h"
#include "Mass/CrowdDemoT7PresentationEventStream.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoT7AcceptanceOracleTest,
  "CrowdDemo.Acceptance.T7.StateOracle",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

namespace
{
  FCrowdDemoCombatNetState MakeIdle()
  {
    FCrowdDemoCombatNetState State;
    State.BusinessState = ECrowdDemoBusinessState::Idle;
    State.ReactiveMode = ECrowdDemoReactiveMotionMode::None;
    State.VisualState = ECrowdDemoVisualState::Idle;
    return State;
  }
}

bool FCrowdDemoT7AcceptanceOracleTest::RunTest(
  const FString& Parameters)
{
  const FCrowdDemoCombatNetState Idle = MakeIdle();
  TestTrue(TEXT("idle cohort"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(0, 30, Idle).bMatches);
  TestTrue(TEXT("knockback pending"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(12, 29, Idle).bMatches);

  FCrowdDemoCombatNetState Knockback = Idle;
  Knockback.BusinessState = ECrowdDemoBusinessState::HitReact;
  Knockback.ReactiveMode = ECrowdDemoReactiveMotionMode::Knockback;
  Knockback.VisualState = ECrowdDemoVisualState::HitReact;
  TestTrue(TEXT("knockback begins"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(12, 30, Knockback).bMatches);
  TestTrue(TEXT("knockback remains"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(13, 44, Knockback).bMatches);
  TestTrue(TEXT("knockback recovers"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(12, 45, Idle).bMatches);

  FCrowdDemoCombatNetState KnockUp = Idle;
  KnockUp.BusinessState = ECrowdDemoBusinessState::HitReact;
  KnockUp.ReactiveMode = ECrowdDemoReactiveMotionMode::KnockUp;
  KnockUp.VisualState = ECrowdDemoVisualState::HitReact;
  TestTrue(TEXT("knock-up begins"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(14, 60, KnockUp).bMatches);
  KnockUp.ApexCount = 1;
  TestTrue(TEXT("knock-up apex"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(15, 79, KnockUp).bMatches);

  FCrowdDemoCombatNetState Landing = KnockUp;
  Landing.ReactiveMode =
    ECrowdDemoReactiveMotionMode::LandingRecovery;
  Landing.LandingCount = 1;
  TestTrue(TEXT("landing recovery"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(14, 98, Landing).bMatches);

  FCrowdDemoCombatNetState Recovered = Idle;
  Recovered.ApexCount = 1;
  Recovered.LandingCount = 1;
  TestTrue(TEXT("knock-up recovers"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(14, 104, Recovered).bMatches);

  FCrowdDemoCombatNetState Dead = Idle;
  Dead.bAlive = 0;
  Dead.Health = 0.0f;
  Dead.LifecycleState = ECrowdDemoLifecycleState::Dead;
  Dead.BusinessState = ECrowdDemoBusinessState::Dead;
  Dead.VisualState = ECrowdDemoVisualState::Death;
  TestTrue(TEXT("death"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(16, 90, Dead).bMatches);
  TestFalse(TEXT("death mismatch is visible"),
    FCrowdDemoT7AcceptanceOracle::Evaluate(16, 90, Idle).bMatches);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoT7PresentationEventStreamTest,
  "CrowdDemo.Acceptance.T7.CommittedPresentationEventStream",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoT7PresentationEventStreamTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoT7PresentationEventStream Stream;
  FCrowdDemoT7PresentationEvent Idle;
  Idle.RoundId = 4;
  Idle.AgentId = 12;
  Idle.LifecycleSerial = 3;
  Idle.FixedStepIndex = 0;
  Idle.ServerTimeSeconds = 10.0f;
  Idle.Combat.VisualState = ECrowdDemoVisualState::Idle;
  FCrowdDemoT7PresentationEvent Knockback = Idle;
  Knockback.FixedStepIndex = 30;
  Knockback.ServerTimeSeconds = 11.0f;
  Knockback.Combat.BusinessState = ECrowdDemoBusinessState::HitReact;
  Knockback.Combat.ReactiveMode =
    ECrowdDemoReactiveMotionMode::Knockback;
  Knockback.Combat.VisualState = ECrowdDemoVisualState::HitReact;

  TestTrue(TEXT("accept committed idle event"), Stream.Enqueue(Idle));
  TestTrue(TEXT("accept committed knockback event"),
    Stream.Enqueue(Knockback));
  FCrowdDemoT7PresentationEvent Resolved;
  TestTrue(TEXT("resolve first committed event"),
    Stream.Resolve(4, 12, 3, 20.0, Resolved));
  TestEqual(TEXT("first event is not coalesced away"),
    Resolved.FixedStepIndex, 0);
  TestTrue(TEXT("resolve next committed event"),
    Stream.Resolve(4, 12, 3, 20.11, Resolved));
  TestEqual(TEXT("second event retains Worker tick"),
    Resolved.FixedStepIndex, 30);
  TestEqual(TEXT("second event retains Worker state"),
    Resolved.Combat.ReactiveMode,
    ECrowdDemoReactiveMotionMode::Knockback);
  TestFalse(TEXT("stale lifecycle cannot consume presentation"),
    Stream.Resolve(4, 12, 2, 20.22, Resolved));

  FCrowdDemoT7PresentationEvent NextRound = Idle;
  NextRound.RoundId = 5;
  NextRound.FixedStepIndex = 0;
  TestTrue(TEXT("next round resets presentation stream"),
    Stream.Enqueue(NextRound));
  TestFalse(TEXT("old round cannot consume next-round state"),
    Stream.Resolve(4, 12, 3, 21.0, Resolved));
  TestTrue(TEXT("resolve next-round event"),
    Stream.Resolve(5, 12, 3, 21.0, Resolved));
  TestEqual(TEXT("old-round queued event was discarded"),
    Resolved.RoundId, 5);
  return true;
}

#endif
