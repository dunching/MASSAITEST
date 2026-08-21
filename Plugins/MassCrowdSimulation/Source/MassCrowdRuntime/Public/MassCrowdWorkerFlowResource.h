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

private:
  bool bStructurallyValidated = false;

  friend class FCrowdWorkerFlowFieldResourceCodec;
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

struct MASSCROWDRUNTIME_API FCrowdWorkerFlowResourceCacheKey
{
  uint64 ResourceId = 0;
  uint64 Revision = 0;

  bool operator==(
    const FCrowdWorkerFlowResourceCacheKey& Other) const = default;

  friend uint32 GetTypeHash(
    const FCrowdWorkerFlowResourceCacheKey& Key)
  {
    return HashCombineFast(
      ::GetTypeHash(Key.ResourceId),
      ::GetTypeHash(Key.Revision));
  }
};

class MASSCROWDRUNTIME_API FCrowdWorkerFlowResourceCache
{
public:
  bool Resolve(
    uint64 ResourceId,
    uint64 Revision,
    const FCrowdWorkerPayload& Payload,
    const FCrowdWorkerFlowFieldResource*& OutResource);

  int32 NumDecodedResources() const { return Entries.Num(); }
  int32 GetDecodeCount() const { return DecodeCount; }
  int32 GetValidationCount() const { return ValidationCount; }

private:
  struct FEntry
  {
    uint64 PayloadStableHash = 0;
    TSharedPtr<FCrowdWorkerFlowFieldResource> Resource;
  };

  TMap<FCrowdWorkerFlowResourceCacheKey, FEntry> Entries;
  int32 DecodeCount = 0;
  int32 ValidationCount = 0;
};
