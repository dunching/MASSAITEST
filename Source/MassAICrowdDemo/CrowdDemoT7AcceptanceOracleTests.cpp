#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoT7AcceptanceOracle.h"

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

#endif
