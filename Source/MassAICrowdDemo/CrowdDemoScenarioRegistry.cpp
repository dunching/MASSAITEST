#include "CrowdDemoScenarioRegistry.h"

namespace CrowdDemoScenarioRegistry
{
  bool TryParse(const FString& Value, ECrowdDemoScenario& OutScenario)
  {
    if (Value.Equals(TEXT("0")) || Value.Equals(TEXT("SimRoundObstacle"), ESearchCase::IgnoreCase))
    {
      OutScenario = ECrowdDemoScenario::SimRoundObstacle;
      return true;
    }
    if (Value.Equals(TEXT("1")) || Value.Equals(TEXT("SimRoundSoftPressure"), ESearchCase::IgnoreCase))
    {
      OutScenario = ECrowdDemoScenario::SimRoundSoftPressure;
      return true;
    }
    return false;
  }

  const TCHAR* ToString(const ECrowdDemoScenario Scenario)
  {
    switch (Scenario)
    {
    case ECrowdDemoScenario::SimRoundSoftPressure:
      return TEXT("SimRoundSoftPressure");
    case ECrowdDemoScenario::SimRoundObstacle:
    default:
      return TEXT("SimRoundObstacle");
    }
  }

  bool IsValidValue(const int32 Value)
  {
    return Value == static_cast<int32>(ECrowdDemoScenario::SimRoundObstacle)
      || Value == static_cast<int32>(ECrowdDemoScenario::SimRoundSoftPressure);
  }

  FCrowdDemoRoundTiming ResolveRoundTiming(
    const ECrowdDemoScenario Scenario,
    const ECrowdDemoSoftPressureTestCase TestCase)
  {
    FCrowdDemoRoundTiming Timing;
    Timing.NominalDurationSeconds = Scenario == ECrowdDemoScenario::SimRoundObstacle
      ? 20.0f
      : 30.0f;
    Timing.CompletionGraceSeconds =
      Scenario == ECrowdDemoScenario::SimRoundSoftPressure
      && (TestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTargetMoving
        || TestCase == ECrowdDemoSoftPressureTestCase::HeterogeneousTransit)
      ? 15.0f
      : 0.0f;
#if WITH_DEV_AUTOMATION_TESTS
    float AutomationDurationSeconds = 0.0f;
    if (FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdDemoAutomationRoundDurationSeconds="),
        AutomationDurationSeconds)
      && FMath::IsFinite(AutomationDurationSeconds)
      && AutomationDurationSeconds > 0.0f)
    {
      Timing.NominalDurationSeconds = AutomationDurationSeconds;
      Timing.CompletionGraceSeconds = 0.0f;
    }
#endif
    return Timing;
  }
}
