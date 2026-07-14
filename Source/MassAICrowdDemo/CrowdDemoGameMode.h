#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CrowdDemoGameMode.generated.h"

UCLASS()
class MASSAICROWDDEMO_API AMassAICrowdDemoGameMode : public AGameModeBase
{
  GENERATED_BODY()

public:
  AMassAICrowdDemoGameMode();

  virtual void BeginPlay() override;
};
