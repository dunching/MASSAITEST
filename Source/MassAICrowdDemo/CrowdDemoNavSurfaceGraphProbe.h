#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrowdDemoNavSurfaceGraphProbe.generated.h"

UCLASS()
class MASSAICROWDDEMO_API ACrowdDemoNavSurfaceGraphProbe : public AActor
{
  GENERATED_BODY()

public:
  ACrowdDemoNavSurfaceGraphProbe();
  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;

private:
  double FirstAttemptWorldSeconds = 0.0;
  double NextAttemptWorldSeconds = 0.0;
  bool bFinished = false;
  int32 AttemptCount = 0;
  bool bCaptureRequested = false;
  bool bCaptureCompleted = false;
  bool bProductSmall = false;
  double CaptureAtWorldSeconds = 0.0;

  bool TryValidate();
  void PrepareCaptureView();
};
