#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoScenarioConfigActor.generated.h"

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoScenarioConfigActor : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoScenarioConfigActor();

  UPROPERTY(EditAnywhere, Category = "Crowd Demo")
  ECrowdDemoScenario Scenario = ECrowdDemoScenario::SimRoundObstacle;

  UPROPERTY(EditAnywhere, Category = "Crowd Demo", meta = (ClampMin = "-1", ClampMax = "11"))
  int32 ScenarioOverrideValue = -1;

  UPROPERTY(EditAnywhere, Category = "Crowd Demo")
  int32 EntityCountOverride = -1;

  UPROPERTY(EditAnywhere, Category = "Crowd Demo")
  int32 InitialAliveCountOverride = -1;
};
