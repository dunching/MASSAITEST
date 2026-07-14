#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

namespace CrowdDemoScenarioRegistry
{
  MASSAICROWDDEMO_API bool TryParse(const FString& Value, ECrowdDemoScenario& OutScenario);
  MASSAICROWDDEMO_API const TCHAR* ToString(ECrowdDemoScenario Scenario);
  MASSAICROWDDEMO_API bool IsValidValue(int32 Value);
}
