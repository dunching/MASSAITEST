#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

struct FCrowdSpatialSafetyAgent
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  float RadiusCm = 0.0f;
  uint32 NavLayer = 0;
};

class MASSCROWDRUNTIME_API FCrowdSpatialSafetyIndex
{
public:
  bool Build(
    TConstArrayView<FCrowdSpatialSafetyAgent> Agents,
    float InCellSizeCm,
    float InLayerToleranceCm);
  bool IsCandidateSafe(
    const FCrowdStableEntityRef& MovingRef,
    const FVector& Candidate,
    float RadiusCm) const;
  bool Update(
    const FCrowdStableEntityRef& EntityRef,
    const FVector& NewPosition);
  bool Update(
    const FCrowdStableEntityRef& EntityRef,
    const FVector& NewPosition,
    uint32 NewNavLayer);
  float CalculateMinimumSeparationCm() const;
  int32 Num() const { return AgentsByRef.Num(); }

private:
  FIntVector CellFor(const FVector& Position) const;

  TMap<FCrowdStableEntityRef, FCrowdSpatialSafetyAgent> AgentsByRef;
  TMap<FIntVector, TArray<FCrowdStableEntityRef>> RefsByCell;
  float CellSizeCm = 0.0f;
  float LayerToleranceCm = 0.0f;
};
