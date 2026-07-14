#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CrowdDemoPlayerController.generated.h"

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoPlayerController : public APlayerController
{
  GENERATED_BODY()

public:
  ACrowdDemoPlayerController();
  virtual void Tick(float DeltaSeconds) override;

private:
  UFUNCTION(Server, Reliable)
  void ServerReportCrowdDemoReady(int32 AgentCount, int32 VisibleInstances);

  float ReadyStableSeconds = 0.0f;
  bool bReadyReported = false;
};
