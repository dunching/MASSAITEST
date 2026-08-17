#pragma once

#include "CoreMinimal.h"
#include "MassCrowdPresentationSubsystem.h"

class UInstancedStaticMeshComponent;

class FCrowdDemoIsmPresentationSink final
  : public ICrowdPresentationInstanceSink
{
public:
  FCrowdDemoIsmPresentationSink(
    UInstancedStaticMeshComponent& InInstances,
    UInstancedStaticMeshComponent* InCargoInstances = nullptr);

  virtual int32 AddInstance(
    const FCrowdPresentationState& State) override;
  virtual bool UpdateInstance(
    int32 Slot,
    const FCrowdPresentationState& State) override;
  virtual bool RemoveInstanceSwap(
    int32 Slot,
    int32 LastSlot) override;

private:
  TWeakObjectPtr<UInstancedStaticMeshComponent> Instances;
  TWeakObjectPtr<UInstancedStaticMeshComponent> CargoInstances;
};
