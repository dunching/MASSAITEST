#include "CrowdDemoScenarioRegistry.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Mass/CrowdDemoMassSubsystem.h"

namespace
{
void SetScenario(const TArray<FString>& Args, UWorld* World)
{
  if (!World || Args.Num() != 1)
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoCommand: usage=CrowdDemo.SetScenario <0|1|SimRoundObstacle|SimRoundSoftPressure>"));
    return;
  }

  ECrowdDemoScenario Scenario;
  if (!CrowdDemoScenarioRegistry::TryParse(Args[0], Scenario))
  {
    UE_LOG(LogTemp, Warning, TEXT("CrowdDemoCommand: unknown_scenario=%s fallback=SimRoundObstacle"), *Args[0]);
    Scenario = ECrowdDemoScenario::SimRoundObstacle;
  }

  if (UCrowdDemoMassSubsystem* Mass = World->GetSubsystem<UCrowdDemoMassSubsystem>())
  {
    Mass->SetScenario(Scenario);
  }
}

FAutoConsoleCommandWithWorldAndArgs GSetScenario(
  TEXT("CrowdDemo.SetScenario"),
  TEXT("Set deterministic RoundSim scenario: 0 Shared Flow, 1 SoftPressure."),
  FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetScenario));
}
