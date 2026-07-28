#include "Mass/CrowdDemoPresentationAdapter.h"

#include "Components/InstancedStaticMeshComponent.h"

FCrowdDemoIsmPresentationSink::FCrowdDemoIsmPresentationSink(
  UInstancedStaticMeshComponent& InInstances,
  UInstancedStaticMeshComponent& InHitFlashInstances,
  UInstancedStaticMeshComponent* InCargoInstances)
  : Instances(&InInstances),
    HitFlashInstances(&InHitFlashInstances),
    CargoInstances(InCargoInstances)
{
}

int32 FCrowdDemoIsmPresentationSink::AddInstance(
  const FCrowdPresentationState& State)
{
  UInstancedStaticMeshComponent* ResolvedInstances = Instances.Get();
  UInstancedStaticMeshComponent* ResolvedHitFlash =
    HitFlashInstances.Get();
  if (!ResolvedInstances || !ResolvedHitFlash) return INDEX_NONE;
  const int32 Slot = ResolvedInstances->AddInstance(
    State.Transform, true);
  ResolvedInstances->SetCustomDataValue(
    Slot, 0, State.CustomData.X, false);
  ResolvedInstances->SetCustomDataValue(
    Slot, 1, State.CustomData.Y, false);
  ResolvedInstances->SetCustomDataValue(
    Slot, 2, State.CustomData.Z, false);
  FTransform Flash = State.Transform;
  if (State.CustomData.Z <= KINDA_SMALL_NUMBER)
    Flash.SetScale3D(FVector::ZeroVector);
  const int32 FlashSlot = ResolvedHitFlash->AddInstance(Flash, true);
  if (Slot != FlashSlot) return INDEX_NONE;
  ResolvedHitFlash->SetCustomDataValue(
    Slot, 0, State.CustomData.X, false);
  ResolvedHitFlash->SetCustomDataValue(
    Slot, 1, State.CustomData.Y, false);
  ResolvedHitFlash->SetCustomDataValue(
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
  UInstancedStaticMeshComponent* ResolvedHitFlash =
    HitFlashInstances.Get();
  if (!ResolvedInstances || !ResolvedHitFlash) return false;
  FTransform Flash = State.Transform;
  if (State.CustomData.Z <= KINDA_SMALL_NUMBER)
    Flash.SetScale3D(FVector::ZeroVector);
  bool bUpdated = ResolvedInstances->UpdateInstanceTransform(
      Slot, State.Transform, true, false, true)
    && ResolvedHitFlash->UpdateInstanceTransform(
      Slot, Flash, true, false, true);
  bUpdated = bUpdated
    && ResolvedInstances->SetCustomDataValue(
      Slot, 0, State.CustomData.X, false)
    && ResolvedInstances->SetCustomDataValue(
      Slot, 1, State.CustomData.Y, false)
    && ResolvedInstances->SetCustomDataValue(
      Slot, 2, State.CustomData.Z, false)
    && ResolvedHitFlash->SetCustomDataValue(
      Slot, 0, State.CustomData.X, false)
    && ResolvedHitFlash->SetCustomDataValue(
      Slot, 1, State.CustomData.Y, false)
    && ResolvedHitFlash->SetCustomDataValue(
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
  UInstancedStaticMeshComponent* ResolvedHitFlash =
    HitFlashInstances.Get();
  if (!ResolvedInstances || !ResolvedHitFlash
    || Slot < 0 || LastSlot < Slot) return false;
  if (Slot != LastSlot)
  {
    FTransform LastTransform;
    FTransform LastFlash;
    if (!ResolvedInstances->GetInstanceTransform(
          LastSlot, LastTransform, true)
      || !ResolvedHitFlash->GetInstanceTransform(
          LastSlot, LastFlash, true)
      || !ResolvedInstances->UpdateInstanceTransform(
          Slot, LastTransform, true, false, true)
      || !ResolvedHitFlash->UpdateInstanceTransform(
          Slot, LastFlash, true, false, true))
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
  bool bRemoved = ResolvedInstances->RemoveInstance(LastSlot)
    && ResolvedHitFlash->RemoveInstance(LastSlot);
  if (UInstancedStaticMeshComponent* Cargo = CargoInstances.Get())
    bRemoved = bRemoved && Cargo->RemoveInstance(LastSlot);
  return bRemoved;
}
