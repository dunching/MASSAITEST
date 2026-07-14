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
    if (Value.Equals(TEXT("2")) || Value.Equals(TEXT("SimRoundCrowdTraffic"), ESearchCase::IgnoreCase))
    {
      OutScenario = ECrowdDemoScenario::SimRoundCrowdTraffic;
      return true;
    }
    if (Value.Equals(TEXT("3")) || Value.Equals(TEXT("SimRoundPursuitPositioning"), ESearchCase::IgnoreCase))
    {
      OutScenario = ECrowdDemoScenario::SimRoundPursuitPositioning;
      return true;
    }
    return false;
  }

  const TCHAR* ToString(const ECrowdDemoScenario Scenario)
  {
    switch (Scenario)
    {
    case ECrowdDemoScenario::SimRoundPursuitPositioning:
      return TEXT("SimRoundPursuitPositioning");
    case ECrowdDemoScenario::SimRoundCrowdTraffic:
      return TEXT("SimRoundCrowdTraffic");
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
      || Value == static_cast<int32>(ECrowdDemoScenario::SimRoundSoftPressure)
      || Value == static_cast<int32>(ECrowdDemoScenario::SimRoundCrowdTraffic)
      || Value == static_cast<int32>(ECrowdDemoScenario::SimRoundPursuitPositioning);
  }
}
