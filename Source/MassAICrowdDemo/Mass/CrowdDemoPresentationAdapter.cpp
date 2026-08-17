#include "Mass/CrowdDemoPresentationAdapter.h"

#include "Components/InstancedStaticMeshComponent.h"

FCrowdDemoIsmPresentationSink::FCrowdDemoIsmPresentationSink(
  UInstancedStaticMeshComponent& InInstances,
  UInstancedStaticMeshComponent* InCargoInstances)
  : Instances(&InInstances),
    CargoInstances(InCargoInstances)
{
}

int32 FCrowdDemoIsmPresentationSink::AddInstance(
  const FCrowdPresentationState& State)
{
  UInstancedStaticMeshComponent* ResolvedInstances = Instances.Get();
  if (!ResolvedInstances) return INDEX_NONE;
  const int32 Slot = ResolvedInstances->AddInstance(
    State.Transform, true);
  ResolvedInstances->SetCustomDataValue(
    Slot, 0, State.CustomData.X, false);
  ResolvedInstances->SetCustomDataValue(
    Slot, 1, State.CustomData.Y, false);
  ResolvedInstances->SetCustomDataValue(
    Slot, 2, State.CustomData.Z, false);
  if (UInstancedStaticMeshComponent* Cargo = CargoInstances.Get())
  {
    FTransform CargoTransform = State.Transform;
    CargoTransform.AddToTranslation(FVector(0.0, 0.0, 105.0));
    CargoTransform.SetScale3D(State.CargoRef.IsValid()
      ? FVector(0.48, 0.48, 0.48) : FVector::ZeroVector);
    if (Cargo->AddInstance(CargoTransform, true) != Slot)
      return INDEX_NONE;
  }
  return Slot;
}

bool FCrowdDemoIsmPresentationSink::UpdateInstance(
  const int32 Slot,
  const FCrowdPresentationState& State)
{
  UInstancedStaticMeshComponent* ResolvedInstances = Instances.Get();
  if (!ResolvedInstances) return false;
  bool bUpdated = ResolvedInstances->UpdateInstanceTransform(
      Slot, State.Transform, true, false, true);
  bUpdated = bUpdated
    && ResolvedInstances->SetCustomDataValue(
      Slot, 0, State.CustomData.X, false)
    && ResolvedInstances->SetCustomDataValue(
      Slot, 1, State.CustomData.Y, false)
    && ResolvedInstances->SetCustomDataValue(
      Slot, 2, State.CustomData.Z, false);
  if (UInstancedStaticMeshComponent* Cargo = CargoInstances.Get())
  {
    FTransform CargoTransform = State.Transform;
    CargoTransform.AddToTranslation(FVector(0.0, 0.0, 105.0));
    CargoTransform.SetScale3D(State.CargoRef.IsValid()
      ? FVector(0.48, 0.48, 0.48) : FVector::ZeroVector);
    bUpdated = bUpdated && Cargo->UpdateInstanceTransform(
      Slot, CargoTransform, true, false, true);
  }
  return bUpdated;
}

bool FCrowdDemoIsmPresentationSink::RemoveInstanceSwap(
  const int32 Slot,
  const int32 LastSlot)
{
  UInstancedStaticMeshComponent* ResolvedInstances = Instances.Get();
  if (!ResolvedInstances || Slot < 0 || LastSlot < Slot) return false;
  if (Slot != LastSlot)
  {
    FTransform LastTransform;
    if (!ResolvedInstances->GetInstanceTransform(
          LastSlot, LastTransform, true)
      || !ResolvedInstances->UpdateInstanceTransform(
          Slot, LastTransform, true, false, true))
      return false;
    const int32 CustomDataOffset = LastSlot
      * ResolvedInstances->NumCustomDataFloats;
    if (ResolvedInstances->NumCustomDataFloats < 3
      || !ResolvedInstances->PerInstanceSMCustomData.IsValidIndex(
        CustomDataOffset + 2)
      || !ResolvedInstances->SetCustomDataValue(
        Slot, 0,
        ResolvedInstances->PerInstanceSMCustomData[CustomDataOffset],
        false)
      || !ResolvedInstances->SetCustomDataValue(
        Slot, 1,
        ResolvedInstances->PerInstanceSMCustomData[CustomDataOffset + 1],
        false)
      || !ResolvedInstances->SetCustomDataValue(
        Slot, 2,
        ResolvedInstances->PerInstanceSMCustomData[CustomDataOffset + 2],
        false))
      return false;
    if (UInstancedStaticMeshComponent* Cargo = CargoInstances.Get())
    {
      FTransform LastCargo;
      if (!Cargo->GetInstanceTransform(LastSlot, LastCargo, true)
        || !Cargo->UpdateInstanceTransform(
          Slot, LastCargo, true, false, true))
        return false;
    }
  }
  bool bRemoved = ResolvedInstances->RemoveInstance(LastSlot);
  if (UInstancedStaticMeshComponent* Cargo = CargoInstances.Get())
    bRemoved = bRemoved && Cargo->RemoveInstance(LastSlot);
  return bRemoved;
}
