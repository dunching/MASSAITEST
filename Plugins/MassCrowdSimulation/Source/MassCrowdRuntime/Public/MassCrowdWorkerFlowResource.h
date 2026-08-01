#pragma once

#include "CoreMinimal.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "MassCrowdWorkerContracts.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerFlowFieldResource
{
  FCrowdSharedFlowField Field;
  uint64 Revision = 0;
  uint32 BuildHash = 0;
  FVector BoundsMin = FVector::ZeroVector;
  FVector BoundsMax = FVector::ZeroVector;
  FVector GoalLocation = FVector::ZeroVector;
  float CellSizeCm = 0.0f;
  int32 Width = 0;
  int32 Height = 0;
  TArray<FVector> FlowDirections;
  TBitArray<> Blocked;
  TBitArray<> Unreachable;

  bool IsValid() const;
  bool Sample(
    const FVector& Position,
    FVector& OutDirection,
    bool& bOutReachable) const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerFlowFieldResourceCodec
{
public:
  static constexpr uint32 SchemaId = 0x43574646u;
  static constexpr uint16 SchemaVersion = 2;
  static constexpr int32 MaxEncodedBytes = 32 * 1024 * 1024;

  static bool Encode(
    const FCrowdSharedFlowField& Field,
    FCrowdWorkerPayload& OutPayload);

  static bool Decode(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerFlowFieldResource& OutResource);
};
