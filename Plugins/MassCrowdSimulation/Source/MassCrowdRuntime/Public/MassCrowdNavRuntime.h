#pragma once

#include "CoreMinimal.h"
#include "CrowdNavSurfaceGraph.h"

enum class ECrowdNavGraphResourceState : uint8
{
  Empty = 0,
  Ready,
  Failed
};

struct FCrowdNavGraphResource
{
  ECrowdNavGraphResourceState State =
    ECrowdNavGraphResourceState::Empty;
  uint32 TopologyRevision = 0;
  uint64 TopologyHash = 0;
  FString FailureReason;
  TSharedPtr<const FCrowdNavSurfaceGraph, ESPMode::ThreadSafe> Graph;

  bool IsReady() const
  {
    return State == ECrowdNavGraphResourceState::Ready
      && Graph.IsValid() && Graph->IsValid()
      && TopologyHash == Graph->TopologyHash;
  }
};

class MASSCROWDRUNTIME_API ICrowdNavDataProvider
{
public:
  virtual ~ICrowdNavDataProvider() = default;

  virtual uint32 GetTopologyRevision() const = 0;
  virtual void InvalidateTopology() = 0;
  virtual bool BuildGraph(
    const FCrowdNavSurfaceGraphBuildConfig& Config,
    FCrowdNavSurfaceGraph& OutGraph,
    FString& OutFailureReason) = 0;
};

struct FCrowdNavFlowKey
{
  uint32 TopologyRevision = 0;
  uint64 ObjectiveAttachment = 0;
  uint32 MovementProfileKey = 0;
  uint32 NavLayer = 0;

  bool IsValid() const
  {
    return TopologyRevision != 0 && ObjectiveAttachment != 0;
  }

  bool operator==(const FCrowdNavFlowKey& Other) const = default;

  bool operator<(const FCrowdNavFlowKey& Other) const
  {
    if (TopologyRevision != Other.TopologyRevision)
      return TopologyRevision < Other.TopologyRevision;
    if (ObjectiveAttachment != Other.ObjectiveAttachment)
      return ObjectiveAttachment < Other.ObjectiveAttachment;
    if (MovementProfileKey != Other.MovementProfileKey)
      return MovementProfileKey < Other.MovementProfileKey;
    return NavLayer < Other.NavLayer;
  }

  friend uint32 GetTypeHash(const FCrowdNavFlowKey& Key)
  {
    uint32 Hash = HashCombineFast(
      ::GetTypeHash(Key.TopologyRevision),
      ::GetTypeHash(Key.ObjectiveAttachment));
    Hash = HashCombineFast(Hash, ::GetTypeHash(Key.MovementProfileKey));
    return HashCombineFast(Hash, ::GetTypeHash(Key.NavLayer));
  }
};

struct FCrowdNavFlowHandle
{
  uint32 Slot = 0;
  uint32 Generation = 0;

  bool IsValid() const { return Slot != 0 && Generation != 0; }
  bool operator==(const FCrowdNavFlowHandle& Other) const = default;
};

struct FCrowdNavFlowCacheLimits
{
  int32 MaxResources = 64;
  uint64 MaxBytes = 32ull * 1024ull * 1024ull;

  bool IsValid() const
  {
    return MaxResources > 0 && MaxBytes > 0;
  }
};

struct FCrowdNavFlowCacheMetrics
{
  int32 ResourceCount = 0;
  int32 ReferencedResourceCount = 0;
  uint64 Bytes = 0;
  uint64 HitCount = 0;
  uint64 MissCount = 0;
  uint64 EvictionCount = 0;
  uint64 RejectedAcquireCount = 0;
};

class MASSCROWDRUNTIME_API FCrowdNavFlowCache
{
public:
  explicit FCrowdNavFlowCache(
    FCrowdNavFlowCacheLimits Limits = {});

  void Reset();
  void SetLimits(const FCrowdNavFlowCacheLimits& Limits);

  bool Acquire(
    const FCrowdNavFlowKey& Key,
    const FCrowdNavSurfaceGraph& Graph,
    FCrowdNavFlowHandle& OutHandle);

  bool Release(const FCrowdNavFlowHandle& Handle);

  TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Resolve(
    const FCrowdNavFlowHandle& Handle) const;

  FCrowdNavFlowCacheMetrics GetMetrics() const;

private:
  struct FEntry
  {
    FCrowdNavFlowKey Key;
    FCrowdNavFlowHandle Handle;
    TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe> Flow;
    int32 RefCount = 0;
    uint64 EstimatedBytes = 0;
    uint64 LastTouch = 0;
  };

  bool EvictUnreferencedUntilWithin(
    int32 PendingResourceCount, uint64 PendingBytes);
  int32 FindEntry(const FCrowdNavFlowHandle& Handle) const;

  FCrowdNavFlowCacheLimits Limits;
  TArray<FEntry> Entries;
  uint32 NextSlot = 1;
  uint64 TouchSequence = 0;
  uint64 HitCount = 0;
  uint64 MissCount = 0;
  uint64 EvictionCount = 0;
  uint64 RejectedAcquireCount = 0;
};
