#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

namespace CrowdDemoScenarioRegistry
{
  struct FCrowdDemoRoundTiming
  {
    float NominalDurationSeconds = 0.0f;
    float CompletionGraceSeconds = 0.0f;

    float GetTotalDurationSeconds() const
    {
      return NominalDurationSeconds + CompletionGraceSeconds;
    }
  };

  MASSAICROWDDEMO_API bool TryParse(const FString& Value, ECrowdDemoScenario& OutScenario);
  MASSAICROWDDEMO_API const TCHAR* ToString(ECrowdDemoScenario Scenario);
  MASSAICROWDDEMO_API bool IsValidValue(int32 Value);
  MASSAICROWDDEMO_API FCrowdDemoRoundTiming ResolveRoundTiming(
    ECrowdDemoScenario Scenario,
    ECrowdDemoSoftPressureTestCase TestCase);
}
