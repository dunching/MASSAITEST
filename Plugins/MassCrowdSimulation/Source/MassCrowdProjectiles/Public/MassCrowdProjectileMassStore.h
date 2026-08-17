#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "MassCrowdProjectileTypes.h"

struct FMassEntityManager;

class MASSCROWDPROJECTILES_API FCrowdMassProjectileStore
{
public:
  bool EnsureCapacity(
    FMassEntityManager& EntityManager,
    int32 RequiredCount,
    int32 MaximumCapacity);
  void DestroyAll(FMassEntityManager& EntityManager);
  void ResetTracking() { Entities.Reset(); }

  bool Gather(
    const FMassEntityManager& EntityManager,
    TArray<FCrowdProjectileState>& OutStates) const;
  bool ValidatePreparedStates(
    TConstArrayView<FCrowdProjectileState> States) const;
  void ApplyValidated(
    FMassEntityManager& EntityManager,
    TConstArrayView<FCrowdProjectileState> States);

  int32 GetCapacity() const { return Entities.Num(); }
  TConstArrayView<FMassEntityHandle> GetEntities() const
  {
    return Entities;
  }

private:
  bool Grow(FMassEntityManager& EntityManager, int32 AddCount);

  TArray<FMassEntityHandle> Entities;
};
