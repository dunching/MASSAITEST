#include "MassCrowdNavRuntime.h"

namespace
{
  uint64 EstimateFlowBytes(const FCrowdNavSurfaceFlow& Flow)
  {
    return sizeof(FCrowdNavSurfaceFlow)
      + static_cast<uint64>(Flow.Nodes.GetAllocatedSize());
  }
}

FCrowdNavFlowCache::FCrowdNavFlowCache(
  const FCrowdNavFlowCacheLimits InLimits)
  : Limits(InLimits.IsValid() ? InLimits : FCrowdNavFlowCacheLimits{})
{
}

void FCrowdNavFlowCache::Reset()
{
  Entries.Reset();
  NextSlot = 1;
  TouchSequence = 0;
  HitCount = 0;
  MissCount = 0;
  EvictionCount = 0;
  RejectedAcquireCount = 0;
}

void FCrowdNavFlowCache::SetLimits(const FCrowdNavFlowCacheLimits& InLimits)
{
  if (!InLimits.IsValid()) return;
  Limits = InLimits;
  EvictUnreferencedUntilWithin(0, 0);
}

bool FCrowdNavFlowCache::Acquire(
  const FCrowdNavFlowKey& Key,
  const FCrowdNavSurfaceGraph& Graph,
  FCrowdNavFlowHandle& OutHandle)
{
  OutHandle = {};
  if (!Key.IsValid() || !Graph.IsValid()) return false;
  for (FEntry& Entry : Entries)
  {
    if (Entry.Key == Key)
    {
      if (!Entry.Flow.IsValid()
        || Entry.Flow->TopologyHash != Graph.TopologyHash
        || Entry.Flow->GoalStableNodeId != Key.ObjectiveAttachment)
      {
        ++RejectedAcquireCount;
        return false;
      }
      ++Entry.RefCount;
      Entry.LastTouch = ++TouchSequence;
      ++HitCount;
      OutHandle = Entry.Handle;
      return true;
    }
  }

  ++MissCount;
  FCrowdNavSurfaceFlow Built;
  if (!FCrowdNavSurfaceGraphKernel::BuildFlow(
      Graph, Key.ObjectiveAttachment, Key.TopologyRevision, Built))
  {
    ++RejectedAcquireCount;
    return false;
  }
  const uint64 Bytes = EstimateFlowBytes(Built);
  if (Bytes > Limits.MaxBytes
    || !EvictUnreferencedUntilWithin(1, Bytes))
  {
    ++RejectedAcquireCount;
    return false;
  }
  FEntry& Entry = Entries.AddDefaulted_GetRef();
  Entry.Key = Key;
  Entry.Handle.Slot = NextSlot++;
  if (NextSlot == 0) NextSlot = 1;
  Entry.Handle.Generation = 1;
  Entry.Flow =
    MakeShared<FCrowdNavSurfaceFlow, ESPMode::ThreadSafe>(MoveTemp(Built));
  Entry.RefCount = 1;
  Entry.EstimatedBytes = Bytes;
  Entry.LastTouch = ++TouchSequence;
  OutHandle = Entry.Handle;
  return true;
}

bool FCrowdNavFlowCache::Release(const FCrowdNavFlowHandle& Handle)
{
  const int32 Index = FindEntry(Handle);
  if (Index == INDEX_NONE || Entries[Index].RefCount <= 0) return false;
  --Entries[Index].RefCount;
  Entries[Index].LastTouch = ++TouchSequence;
  return true;
}

TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe>
FCrowdNavFlowCache::Resolve(const FCrowdNavFlowHandle& Handle) const
{
  const int32 Index = FindEntry(Handle);
  return Index != INDEX_NONE
    ? Entries[Index].Flow
    : TSharedPtr<const FCrowdNavSurfaceFlow, ESPMode::ThreadSafe>();
}

FCrowdNavFlowCacheMetrics FCrowdNavFlowCache::GetMetrics() const
{
  FCrowdNavFlowCacheMetrics Metrics;
  Metrics.ResourceCount = Entries.Num();
  Metrics.HitCount = HitCount;
  Metrics.MissCount = MissCount;
  Metrics.EvictionCount = EvictionCount;
  Metrics.RejectedAcquireCount = RejectedAcquireCount;
  for (const FEntry& Entry : Entries)
  {
    Metrics.Bytes += Entry.EstimatedBytes;
    if (Entry.RefCount > 0) ++Metrics.ReferencedResourceCount;
  }
  return Metrics;
}

bool FCrowdNavFlowCache::EvictUnreferencedUntilWithin(
  const int32 PendingResourceCount,
  const uint64 PendingBytes)
{
  const auto IsWithin = [&]()
  {
    const FCrowdNavFlowCacheMetrics Metrics = GetMetrics();
    return Metrics.ResourceCount + PendingResourceCount
        <= Limits.MaxResources
      && Metrics.Bytes + PendingBytes <= Limits.MaxBytes;
  };
  while (!IsWithin())
  {
    int32 EvictionIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
      if (Entries[Index].RefCount != 0) continue;
      if (EvictionIndex == INDEX_NONE
        || Entries[Index].LastTouch < Entries[EvictionIndex].LastTouch
        || (Entries[Index].LastTouch == Entries[EvictionIndex].LastTouch
          && Entries[Index].Key < Entries[EvictionIndex].Key))
        EvictionIndex = Index;
    }
    if (EvictionIndex == INDEX_NONE) return false;
    Entries.RemoveAt(EvictionIndex);
    ++EvictionCount;
  }
  return true;
}

int32 FCrowdNavFlowCache::FindEntry(
  const FCrowdNavFlowHandle& Handle) const
{
  if (!Handle.IsValid()) return INDEX_NONE;
  return Entries.IndexOfByPredicate([&Handle](const FEntry& Entry)
  {
    return Entry.Handle == Handle;
  });
}
