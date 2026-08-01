#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerNetworkState.h"
#include "Subsystems/WorldSubsystem.h"
#include "CrowdDemoWorkerNetworkBridgeSubsystem.generated.h"

UCLASS()
class MASSAICROWDDEMO_API UCrowdDemoWorkerNetworkBridgeSubsystem final
  : public UTickableWorldSubsystem
{
  GENERATED_BODY()

public:
  virtual void Tick(float DeltaTime) override;
  virtual TStatId GetStatId() const override;
  virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
  FCrowdWorkerNetworkCheckpoint PendingBootstrapCheckpoint;
  uint64 ActiveCheckpointInputBaseline = 0;
  uint64 LastObservedInputSequence = 0;
  bool bHasPendingBootstrapCheckpoint = false;
  bool bLoggedFirstIntentApply = false;
  bool bForcedResyncRequested = false;
};
