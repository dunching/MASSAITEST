#include "MassCrowdWorkerRuntimeV2.h"

namespace CrowdWorkerRuntimeV2Private
{
  constexpr uint64 V2FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 V2FnvPrime64 = 1099511628211ull;

  void V2Fold(uint64& Hash, const uint64 Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(Value); ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= V2FnvPrime64;
    }
  }

  void V2FoldRef(
    uint64& Hash,
    const FCrowdStableEntityRef& Ref)
  {
    V2Fold(Hash, Ref.ProviderId);
    V2Fold(Hash, Ref.StableEntityId);
    V2Fold(Hash, Ref.LifecycleSerial);
  }

  void V2FoldWorkKey(
    uint64& Hash,
    const FCrowdWorkerWorkKey& Key)
  {
    V2Fold(Hash, static_cast<uint8>(Key.Domain));
    V2Fold(Hash, static_cast<uint8>(Key.Kind));
    V2FoldRef(Hash, Key.PrimaryEntity);
    V2FoldRef(Hash, Key.SecondaryEntity);
    V2Fold(Hash, Key.ScopeKey);
  }

  bool V2WorkStableLess(
    const FCrowdWorkerWorkItem& A,
    const FCrowdWorkerWorkItem& B)
  {
    if (A.Priority != B.Priority)
      return A.Priority < B.Priority;
    return A.Key < B.Key;
  }
}

using namespace CrowdWorkerRuntimeV2Private;

bool FCrowdWorkerRuntimeV2Config::IsValid() const
{
  return MaxWorkItems > 0
    && MaxWakeups > 0
    && MaxDependencyEdges > 0
    && MaxDirtyEntities > 0
    && MaxOrderedEvents > 0
    && MaxPropagationRoundsPerEpoch > 0
    && ShardEntityCount > 0
    && Mode <= ECrowdWorkerRuntimeV2Mode::Production;
}

FCrowdWorkerRuntimeV2Config
FCrowdWorkerRuntimeV2Config::MakeProduction10k()
{
  FCrowdWorkerRuntimeV2Config Config;
  Config.Mode = ECrowdWorkerRuntimeV2Mode::Production;
  return Config;
}

bool FCrowdWorkerWorkKey::operator<(
  const FCrowdWorkerWorkKey& Other) const
{
  if (Domain != Other.Domain) return Domain < Other.Domain;
  if (Kind != Other.Kind) return Kind < Other.Kind;
  if (PrimaryEntity != Other.PrimaryEntity)
    return PrimaryEntity < Other.PrimaryEntity;
  if (SecondaryEntity != Other.SecondaryEntity)
    return SecondaryEntity < Other.SecondaryEntity;
  return ScopeKey < Other.ScopeKey;
}

bool FCrowdWorkerWorkItem::IsValid() const
{
  if (Key.Domain >= ECrowdWorkerDomainId::Count
    || Priority >= ECrowdWorkerWorkPriority::Count)
    return false;
  if (Key.Kind == ECrowdWorkerWorkKind::Resource
    || Key.Kind == ECrowdWorkerWorkKind::Cohort)
    return Key.ScopeKey != 0;
  if (!Key.PrimaryEntity.IsValid()) return false;
  return Key.Kind != ECrowdWorkerWorkKind::Pair
    || (Key.SecondaryEntity.IsValid()
      && Key.PrimaryEntity != Key.SecondaryEntity);
}

void FCrowdWorkerWorkItem::NormalizePair()
{
  if (Key.Kind == ECrowdWorkerWorkKind::Pair
    && Key.SecondaryEntity < Key.PrimaryEntity)
    Swap(Key.PrimaryEntity, Key.SecondaryEntity);
}

bool FCrowdWorkerWorkRing::Reset(
  const int32 InMaxWorkItems,
  const uint64 InitialEpoch)
{
  if (InMaxWorkItems <= 0) return false;
  MaxWorkItems = InMaxWorkItems;
  Epoch = InitialEpoch;
  FairDomainCursor = 0;
  HighWatermark = 0;
  DuplicateMergeCount = 0;
  CapacityRejectCount = 0;
  DeferredPropagationCount = 0;
  PopBucketProbeCount = 0;
  ResetQueue(CurrentQueue);
  ResetQueue(NextQueue);
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerWorkRing::EnqueueCurrent(
  FCrowdWorkerWorkItem Item)
{
  Item.EnqueueEpoch = Epoch;
  return Enqueue(MoveTemp(Item), CurrentQueue);
}

ECrowdWorkerQueueResult FCrowdWorkerWorkRing::EnqueueNext(
  FCrowdWorkerWorkItem Item)
{
  Item.EnqueueEpoch = Epoch + 1;
  return Enqueue(MoveTemp(Item), NextQueue);
}

ECrowdWorkerQueueResult FCrowdWorkerWorkRing::Enqueue(
  FCrowdWorkerWorkItem Item,
  FWorkQueue& Queue)
{
  Item.NormalizePair();
  if (!Item.IsValid())
    return ECrowdWorkerQueueResult::RejectedInvalid;
  if (const FBucketLocation* ExistingLocation =
    Queue.Indices.Find(Item.Key))
  {
    FWorkBucket& ExistingBucket =
      Queue.Buckets[ExistingLocation->BucketIndex];
    FCrowdWorkerWorkItem& Existing =
      ExistingBucket.Items[ExistingLocation->ItemIndex];
    Existing.ReasonMask |= Item.ReasonMask;
    if (Item.Priority < Existing.Priority)
    {
      FCrowdWorkerWorkItem Promoted = MoveTemp(Existing);
      Promoted.Priority = Item.Priority;
      const int32 RemovedIndex = ExistingLocation->ItemIndex;
      ExistingBucket.Items.RemoveAtSwap(
        RemovedIndex, 1, EAllowShrinking::No);
      ExistingBucket.bSorted = false;
      if (RemovedIndex < ExistingBucket.Items.Num())
      {
        Queue.Indices.FindChecked(
          ExistingBucket.Items[RemovedIndex].Key).ItemIndex =
            RemovedIndex;
      }
      Queue.Indices.Remove(Promoted.Key);
      --Queue.Count;
      const ECrowdWorkerQueueResult PromoteResult =
        Enqueue(MoveTemp(Promoted), Queue);
      check(PromoteResult == ECrowdWorkerQueueResult::Added);
    }
    ++DuplicateMergeCount;
    return ECrowdWorkerQueueResult::MergedDuplicate;
  }
  if (CurrentQueue.Count + NextQueue.Count >= MaxWorkItems)
  {
    ++CapacityRejectCount;
    return ECrowdWorkerQueueResult::RejectedCapacity;
  }
  const int32 TargetBucketIndex = BucketIndex(
    Item.Priority, Item.Key.Domain);
  FWorkBucket& TargetBucket = Queue.Buckets[TargetBucketIndex];
  const int32 NewIndex = TargetBucket.Items.Add(MoveTemp(Item));
  TargetBucket.bSorted = false;
  Queue.Indices.Add(
    TargetBucket.Items[NewIndex].Key,
    {TargetBucketIndex, NewIndex});
  ++Queue.Count;
  HighWatermark = FMath::Max(
    HighWatermark, CurrentQueue.Count + NextQueue.Count);
  return ECrowdWorkerQueueResult::Added;
}

bool FCrowdWorkerWorkRing::PopCurrent(
  FCrowdWorkerWorkItem& OutItem)
{
  if (CurrentQueue.Count == 0) return false;
  const uint8 PriorityCount =
    static_cast<uint8>(ECrowdWorkerWorkPriority::Count);
  const uint8 DomainCount =
    static_cast<uint8>(ECrowdWorkerDomainId::Count);
  for (uint8 PriorityValue = 0;
    PriorityValue < PriorityCount; ++PriorityValue)
  {
    for (uint8 Offset = 0; Offset < DomainCount; ++Offset)
    {
      ++PopBucketProbeCount;
      const uint8 DomainValue =
        (FairDomainCursor + Offset) % DomainCount;
      const int32 SelectedBucketIndex = BucketIndex(
        static_cast<ECrowdWorkerWorkPriority>(PriorityValue),
        static_cast<ECrowdWorkerDomainId>(DomainValue));
      FWorkBucket& Bucket =
        CurrentQueue.Buckets[SelectedBucketIndex];
      if (Bucket.Cursor >= Bucket.Items.Num())
        continue;
      PrepareBucket(CurrentQueue, SelectedBucketIndex);
      check(Bucket.Cursor < Bucket.Items.Num());
      OutItem = MoveTemp(Bucket.Items[Bucket.Cursor++]);
      CurrentQueue.Indices.Remove(OutItem.Key);
      --CurrentQueue.Count;
      FairDomainCursor = (DomainValue + 1) % DomainCount;
      if (Bucket.Cursor == Bucket.Items.Num())
      {
        Bucket.Items.Reset();
        Bucket.Cursor = 0;
        Bucket.bSorted = true;
      }
      return true;
    }
  }
  checkNoEntry();
  return false;
}

void FCrowdWorkerWorkRing::AdvanceEpoch()
{
  check(CurrentQueue.Count == 0);
  ++Epoch;
  CurrentQueue = MoveTemp(NextQueue);
  ResetQueue(NextQueue);
}

void FCrowdWorkerWorkRing::DeferCurrentToNext()
{
  TArray<FCrowdWorkerWorkItem> Deferred;
  AppendQueueItems(CurrentQueue, Deferred);
  ResetQueue(CurrentQueue);
  for (FCrowdWorkerWorkItem& Item : Deferred)
  {
    ++DeferredPropagationCount;
    EnqueueNext(MoveTemp(Item));
  }
}

int32 FCrowdWorkerWorkRing::InvalidateEntityRevision(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 MinimumCorrectionRevision)
{
  const auto RemoveStale = [&EntityRef, MinimumCorrectionRevision](
    const FCrowdWorkerWorkItem& Item)
  {
    const bool bReferencesEntity =
      Item.Key.PrimaryEntity == EntityRef
      || Item.Key.SecondaryEntity == EntityRef;
    return bReferencesEntity
      && Item.CorrectionRevision < MinimumCorrectionRevision;
  };
  const int32 RemovedCurrent =
    RemoveMatching(CurrentQueue, RemoveStale);
  const int32 RemovedNext =
    RemoveMatching(NextQueue, RemoveStale);
  return RemovedCurrent + RemovedNext;
}

int32 FCrowdWorkerWorkRing::RemoveEntity(
  const FCrowdStableEntityRef& EntityRef)
{
  const auto ReferencesEntity = [&EntityRef](
    const FCrowdWorkerWorkItem& Item)
  {
    return Item.Key.PrimaryEntity == EntityRef
      || Item.Key.SecondaryEntity == EntityRef;
  };
  const int32 RemovedCurrent =
    RemoveMatching(CurrentQueue, ReferencesEntity);
  const int32 RemovedNext =
    RemoveMatching(NextQueue, ReferencesEntity);
  return RemovedCurrent + RemovedNext;
}

int32 FCrowdWorkerWorkRing::BucketIndex(
  const ECrowdWorkerWorkPriority Priority,
  const ECrowdWorkerDomainId Domain)
{
  return static_cast<int32>(Priority)
      * static_cast<int32>(ECrowdWorkerDomainId::Count)
    + static_cast<int32>(Domain);
}

void FCrowdWorkerWorkRing::ResetQueue(FWorkQueue& Queue)
{
  Queue.Buckets.SetNum(
    static_cast<int32>(ECrowdWorkerWorkPriority::Count)
      * static_cast<int32>(ECrowdWorkerDomainId::Count));
  for (FWorkBucket& Bucket : Queue.Buckets)
  {
    Bucket.Items.Reset();
    Bucket.Cursor = 0;
    Bucket.bSorted = true;
  }
  Queue.Indices.Reset();
  Queue.Count = 0;
}

void FCrowdWorkerWorkRing::PrepareBucket(
  FWorkQueue& Queue,
  const int32 BucketIndexValue)
{
  FWorkBucket& Bucket = Queue.Buckets[BucketIndexValue];
  if (Bucket.bSorted) return;
  if (Bucket.Cursor > 0)
  {
    Bucket.Items.RemoveAt(
      0, Bucket.Cursor, EAllowShrinking::No);
    Bucket.Cursor = 0;
  }
  Bucket.Items.Sort([](
    const FCrowdWorkerWorkItem& A,
    const FCrowdWorkerWorkItem& B)
  {
    return A.Key < B.Key;
  });
  for (int32 Index = 0; Index < Bucket.Items.Num(); ++Index)
  {
    FBucketLocation& Location =
      Queue.Indices.FindChecked(Bucket.Items[Index].Key);
    Location.BucketIndex = BucketIndexValue;
    Location.ItemIndex = Index;
  }
  Bucket.bSorted = true;
}

void FCrowdWorkerWorkRing::AppendQueueItems(
  const FWorkQueue& Queue,
  TArray<FCrowdWorkerWorkItem>& OutItems)
{
  OutItems.Reserve(OutItems.Num() + Queue.Count);
  for (const FWorkBucket& Bucket : Queue.Buckets)
  {
    for (int32 Index = Bucket.Cursor;
      Index < Bucket.Items.Num(); ++Index)
      OutItems.Add(Bucket.Items[Index]);
  }
}

int32 FCrowdWorkerWorkRing::RemoveMatching(
  FWorkQueue& Queue,
  const TFunctionRef<bool(const FCrowdWorkerWorkItem&)> Predicate)
{
  TArray<FCrowdWorkerWorkItem> Items;
  AppendQueueItems(Queue, Items);
  const int32 Removed = Items.RemoveAll(Predicate);
  if (Removed == 0) return 0;
  ResetQueue(Queue);
  for (FCrowdWorkerWorkItem& Item : Items)
  {
    const ECrowdWorkerQueueResult Result =
      Enqueue(MoveTemp(Item), Queue);
    check(Result == ECrowdWorkerQueueResult::Added);
  }
  return Removed;
}

FCrowdWorkerWorkRingStats FCrowdWorkerWorkRing::GetStats() const
{
  FCrowdWorkerWorkRingStats Stats;
  Stats.CurrentDepth = CurrentQueue.Count;
  Stats.NextDepth = NextQueue.Count;
  Stats.HighWatermark = HighWatermark;
  Stats.DuplicateMergeCount = DuplicateMergeCount;
  Stats.CapacityRejectCount = CapacityRejectCount;
  Stats.DeferredPropagationCount = DeferredPropagationCount;
  Stats.PopBucketProbeCount = PopBucketProbeCount;
  return Stats;
}

void FCrowdWorkerWorkRing::GetSnapshot(
  FCrowdWorkerWorkRingSnapshot& OutSnapshot) const
{
  OutSnapshot = {};
  OutSnapshot.Epoch = Epoch;
  OutSnapshot.FairDomainCursor = FairDomainCursor;
  AppendQueueItems(CurrentQueue, OutSnapshot.CurrentItems);
  AppendQueueItems(NextQueue, OutSnapshot.NextItems);
  OutSnapshot.CurrentItems.Sort(V2WorkStableLess);
  OutSnapshot.NextItems.Sort(V2WorkStableLess);
}

bool FCrowdWorkerWorkRing::RestoreSnapshot(
  const FCrowdWorkerWorkRingSnapshot& Snapshot)
{
  if (Snapshot.Epoch == 0
    || Snapshot.FairDomainCursor
      >= static_cast<uint8>(ECrowdWorkerDomainId::Count)
    || Snapshot.CurrentItems.Num() + Snapshot.NextItems.Num()
      > MaxWorkItems)
    return false;
  FCrowdWorkerWorkRing Candidate;
  if (!Candidate.Reset(MaxWorkItems, Snapshot.Epoch))
    return false;
  Candidate.FairDomainCursor = Snapshot.FairDomainCursor;
  for (const FCrowdWorkerWorkItem& Item : Snapshot.CurrentItems)
  {
    if (Item.EnqueueEpoch != Snapshot.Epoch
      || Candidate.EnqueueCurrent(Item)
        != ECrowdWorkerQueueResult::Added)
      return false;
  }
  for (const FCrowdWorkerWorkItem& Item : Snapshot.NextItems)
  {
    if (Item.EnqueueEpoch != Snapshot.Epoch + 1
      || Candidate.EnqueueNext(Item)
        != ECrowdWorkerQueueResult::Added)
      return false;
  }
  *this = MoveTemp(Candidate);
  return true;
}

bool FCrowdWorkerWakeupKey::operator<(
  const FCrowdWorkerWakeupKey& Other) const
{
  if (Domain != Other.Domain) return Domain < Other.Domain;
  if (EntityRef != Other.EntityRef)
    return EntityRef < Other.EntityRef;
  return WakeupId < Other.WakeupId;
}

bool FCrowdWorkerWakeup::IsValid() const
{
  return Key.Domain < ECrowdWorkerDomainId::Count
    && Key.EntityRef.IsValid()
    && Key.WakeupId != 0
    && AbsoluteSimulationTick != 0
    && Priority < ECrowdWorkerWorkPriority::Count;
}

bool FCrowdWorkerTimeWheel::Reset(const int32 InMaxWakeups)
{
  if (InMaxWakeups <= 0) return false;
  MaxWakeups = InMaxWakeups;
  HighWatermark = 0;
  ScannedBucketCount = 0;
  Buckets.Reset();
  Scheduled.Reset();
  MinimumTickHeap.Reset();
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerTimeWheel::Schedule(
  FCrowdWorkerWakeup Wakeup)
{
  if (!Wakeup.IsValid())
    return ECrowdWorkerQueueResult::RejectedInvalid;
  bool bReplaced = false;
  if (const FCrowdWorkerWakeup* Existing =
    Scheduled.Find(Wakeup.Key))
  {
    if (Wakeup.Revision < Existing->Revision)
      return ECrowdWorkerQueueResult::RejectedStale;
    if (Wakeup.Revision == Existing->Revision)
    {
      if (Wakeup.AbsoluteSimulationTick
        == Existing->AbsoluteSimulationTick)
        return ECrowdWorkerQueueResult::MergedDuplicate;
      return ECrowdWorkerQueueResult::Conflict;
    }
    RemoveFromBucket(
      Existing->AbsoluteSimulationTick, Wakeup.Key);
    Scheduled.Remove(Wakeup.Key);
    bReplaced = true;
  }
  else if (Scheduled.Num() >= MaxWakeups)
  {
    return ECrowdWorkerQueueResult::RejectedCapacity;
  }
  const bool bNewTick = !Buckets.Contains(
    Wakeup.AbsoluteSimulationTick);
  Buckets.FindOrAdd(Wakeup.AbsoluteSimulationTick).Add(Wakeup);
  if (bNewTick)
    PushTick(Wakeup.AbsoluteSimulationTick);
  Scheduled.Add(Wakeup.Key, MoveTemp(Wakeup));
  HighWatermark = FMath::Max(HighWatermark, Scheduled.Num());
  return bReplaced
    ? ECrowdWorkerQueueResult::Replaced
    : ECrowdWorkerQueueResult::Added;
}

bool FCrowdWorkerTimeWheel::Cancel(
  const FCrowdWorkerWakeupKey& Key)
{
  const FCrowdWorkerWakeup* Existing = Scheduled.Find(Key);
  if (!Existing) return false;
  const uint64 Tick = Existing->AbsoluteSimulationTick;
  RemoveFromBucket(Tick, Key);
  Scheduled.Remove(Key);
  return true;
}

int32 FCrowdWorkerTimeWheel::CancelEntity(
  const FCrowdStableEntityRef& EntityRef)
{
  TArray<FCrowdWorkerWakeupKey> Keys;
  for (const TPair<FCrowdWorkerWakeupKey, FCrowdWorkerWakeup>& Pair :
    Scheduled)
  {
    if (Pair.Key.EntityRef == EntityRef)
      Keys.Add(Pair.Key);
  }
  for (const FCrowdWorkerWakeupKey& Key : Keys)
    Cancel(Key);
  return Keys.Num();
}

int32 FCrowdWorkerTimeWheel::InvalidateEntityRevision(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 MinimumRevision)
{
  TArray<FCrowdWorkerWakeupKey> Keys;
  for (const TPair<FCrowdWorkerWakeupKey, FCrowdWorkerWakeup>& Pair :
    Scheduled)
  {
    if (Pair.Key.EntityRef == EntityRef
      && Pair.Value.Revision < MinimumRevision)
      Keys.Add(Pair.Key);
  }
  for (const FCrowdWorkerWakeupKey& Key : Keys)
    Cancel(Key);
  return Keys.Num();
}

int32 FCrowdWorkerTimeWheel::DrainDue(
  const uint64 InclusiveSimulationTick,
  TArray<FCrowdWorkerWakeup>& OutWakeups)
{
  const int32 StartCount = OutWakeups.Num();
  while (!MinimumTickHeap.IsEmpty()
    && MinimumTickHeap[0] <= InclusiveSimulationTick)
  {
    uint64 Tick = 0;
    check(PopMinimumTick(Tick));
    TArray<FCrowdWorkerWakeup>* Bucket = Buckets.Find(Tick);
    if (!Bucket)
      continue;
    ++ScannedBucketCount;
    Bucket->Sort([](
      const FCrowdWorkerWakeup& A,
      const FCrowdWorkerWakeup& B)
    {
      if (A.Priority != B.Priority)
        return A.Priority < B.Priority;
      return A.Key < B.Key;
    });
    for (FCrowdWorkerWakeup& Wakeup : *Bucket)
    {
      Scheduled.Remove(Wakeup.Key);
      OutWakeups.Add(MoveTemp(Wakeup));
    }
    Buckets.Remove(Tick);
  }
  return OutWakeups.Num() - StartCount;
}

void FCrowdWorkerTimeWheel::PushTick(const uint64 Tick)
{
  int32 Index = MinimumTickHeap.Add(Tick);
  while (Index > 0)
  {
    const int32 Parent = (Index - 1) / 2;
    if (MinimumTickHeap[Parent] <= MinimumTickHeap[Index])
      break;
    Swap(MinimumTickHeap[Parent], MinimumTickHeap[Index]);
    Index = Parent;
  }
}

bool FCrowdWorkerTimeWheel::PopMinimumTick(uint64& OutTick)
{
  if (MinimumTickHeap.IsEmpty()) return false;
  OutTick = MinimumTickHeap[0];
  const uint64 Tail = MinimumTickHeap.Pop(EAllowShrinking::No);
  if (MinimumTickHeap.IsEmpty()) return true;
  MinimumTickHeap[0] = Tail;
  int32 Index = 0;
  for (;;)
  {
    const int32 Left = Index * 2 + 1;
    if (Left >= MinimumTickHeap.Num()) break;
    const int32 Right = Left + 1;
    int32 Smallest = Left;
    if (Right < MinimumTickHeap.Num()
      && MinimumTickHeap[Right] < MinimumTickHeap[Left])
      Smallest = Right;
    if (MinimumTickHeap[Index] <= MinimumTickHeap[Smallest])
      break;
    Swap(MinimumTickHeap[Index], MinimumTickHeap[Smallest]);
    Index = Smallest;
  }
  return true;
}

void FCrowdWorkerTimeWheel::GetScheduled(
  TArray<FCrowdWorkerWakeup>& OutWakeups) const
{
  OutWakeups.Reset();
  OutWakeups.Reserve(Scheduled.Num());
  for (const TPair<FCrowdWorkerWakeupKey, FCrowdWorkerWakeup>& Pair :
    Scheduled)
    OutWakeups.Add(Pair.Value);
  OutWakeups.Sort([](
    const FCrowdWorkerWakeup& A,
    const FCrowdWorkerWakeup& B)
  {
    if (A.AbsoluteSimulationTick != B.AbsoluteSimulationTick)
      return A.AbsoluteSimulationTick < B.AbsoluteSimulationTick;
    return A.Key < B.Key;
  });
}

bool FCrowdWorkerTimeWheel::RestoreScheduled(
  const TConstArrayView<FCrowdWorkerWakeup> Wakeups)
{
  if (Wakeups.Num() > MaxWakeups) return false;
  FCrowdWorkerTimeWheel Candidate;
  if (!Candidate.Reset(MaxWakeups)) return false;
  for (const FCrowdWorkerWakeup& Wakeup : Wakeups)
  {
    if (Candidate.Schedule(Wakeup)
      != ECrowdWorkerQueueResult::Added)
      return false;
  }
  *this = MoveTemp(Candidate);
  return true;
}

bool FCrowdWorkerTimeWheel::RemoveFromBucket(
  const uint64 Tick,
  const FCrowdWorkerWakeupKey& Key)
{
  TArray<FCrowdWorkerWakeup>* Bucket = Buckets.Find(Tick);
  if (!Bucket) return false;
  const int32 Removed = Bucket->RemoveAll(
    [&Key](const FCrowdWorkerWakeup& Wakeup)
    {
      return Wakeup.Key == Key;
    });
  if (Bucket->IsEmpty()) Buckets.Remove(Tick);
  return Removed == 1;
}

bool FCrowdWorkerDependencyKey::IsValid() const
{
  return Kind == ECrowdWorkerDependencyKind::Entity
    ? EntityRef.IsValid()
    : ScopeKey != 0;
}

bool FCrowdWorkerDependencyKey::operator<(
  const FCrowdWorkerDependencyKey& Other) const
{
  if (Kind != Other.Kind) return Kind < Other.Kind;
  if (EntityRef != Other.EntityRef)
    return EntityRef < Other.EntityRef;
  return ScopeKey < Other.ScopeKey;
}

bool FCrowdWorkerDependencyIndex::Reset(const int32 InMaxEdges)
{
  if (InMaxEdges <= 0) return false;
  MaxEdges = InMaxEdges;
  EdgeCount = 0;
  HighWatermark = 0;
  Edges.Reset();
  return true;
}

ECrowdWorkerQueueResult
FCrowdWorkerDependencyIndex::AddDependency(
  const FCrowdWorkerDependencyKey& Source,
  FCrowdWorkerWorkItem Dependent)
{
  Dependent.NormalizePair();
  if (!Source.IsValid() || !Dependent.IsValid())
    return ECrowdWorkerQueueResult::RejectedInvalid;
  TArray<FCrowdWorkerWorkItem>& Dependents =
    Edges.FindOrAdd(Source);
  for (FCrowdWorkerWorkItem& Existing : Dependents)
  {
    if (Existing.Key != Dependent.Key) continue;
    Existing.ReasonMask |= Dependent.ReasonMask;
    Existing.Priority = FMath::Min(
      Existing.Priority, Dependent.Priority);
    Existing.EnqueueEpoch = FMath::Max(
      Existing.EnqueueEpoch, Dependent.EnqueueEpoch);
    Existing.CorrectionRevision = FMath::Max(
      Existing.CorrectionRevision,
      Dependent.CorrectionRevision);
    return ECrowdWorkerQueueResult::MergedDuplicate;
  }
  if (EdgeCount >= MaxEdges)
    return ECrowdWorkerQueueResult::RejectedCapacity;
  Dependents.Add(MoveTemp(Dependent));
  Dependents.Sort(V2WorkStableLess);
  ++EdgeCount;
  HighWatermark = FMath::Max(HighWatermark, EdgeCount);
  return ECrowdWorkerQueueResult::Added;
}

int32 FCrowdWorkerDependencyIndex::CollectDependents(
  const FCrowdWorkerDependencyKey& Source,
  TArray<FCrowdWorkerWorkItem>& OutItems) const
{
  const TArray<FCrowdWorkerWorkItem>* Dependents =
    Edges.Find(Source);
  if (!Dependents) return 0;
  OutItems.Append(*Dependents);
  return Dependents->Num();
}

bool FCrowdWorkerDependencyIndex::ContainsDependency(
  const FCrowdWorkerDependencyKey& Source,
  const FCrowdWorkerWorkKey& Dependent) const
{
  const TArray<FCrowdWorkerWorkItem>* Dependents =
    Edges.Find(Source);
  if (!Dependents) return false;
  return Dependents->ContainsByPredicate([
    &Dependent](const FCrowdWorkerWorkItem& Item)
  {
    return Item.Key == Dependent;
  });
}

int32 FCrowdWorkerDependencyIndex::RemoveEntity(
  const FCrowdStableEntityRef& EntityRef)
{
  int32 Removed = 0;
  TArray<FCrowdWorkerDependencyKey> EmptyKeys;
  for (TPair<
    FCrowdWorkerDependencyKey,
    TArray<FCrowdWorkerWorkItem>>& Pair : Edges)
  {
    if (Pair.Key.Kind == ECrowdWorkerDependencyKind::Entity
      && Pair.Key.EntityRef == EntityRef)
    {
      Removed += Pair.Value.Num();
      EmptyKeys.Add(Pair.Key);
      continue;
    }
    Removed += Pair.Value.RemoveAll(
      [&EntityRef](const FCrowdWorkerWorkItem& Item)
      {
        return Item.Key.PrimaryEntity == EntityRef
          || Item.Key.SecondaryEntity == EntityRef;
      });
    if (Pair.Value.IsEmpty()) EmptyKeys.Add(Pair.Key);
  }
  for (const FCrowdWorkerDependencyKey& Key : EmptyKeys)
    Edges.Remove(Key);
  EdgeCount -= Removed;
  check(EdgeCount >= 0);
  return Removed;
}

void FCrowdWorkerDependencyIndex::GetRecords(
  TArray<FCrowdWorkerDependencyRecord>& OutRecords) const
{
  OutRecords.Reset();
  OutRecords.Reserve(EdgeCount);
  for (const TPair<
    FCrowdWorkerDependencyKey,
    TArray<FCrowdWorkerWorkItem>>& Pair : Edges)
  {
    for (const FCrowdWorkerWorkItem& Dependent : Pair.Value)
      OutRecords.Add({Pair.Key, Dependent});
  }
  OutRecords.Sort([](
    const FCrowdWorkerDependencyRecord& A,
    const FCrowdWorkerDependencyRecord& B)
  {
    if (A.Source != B.Source) return A.Source < B.Source;
    return V2WorkStableLess(A.Dependent, B.Dependent);
  });
}

bool FCrowdWorkerDependencyIndex::RestoreRecords(
  const TConstArrayView<FCrowdWorkerDependencyRecord> Records)
{
  if (Records.Num() > MaxEdges) return false;
  FCrowdWorkerDependencyIndex Candidate;
  if (!Candidate.Reset(MaxEdges)) return false;
  for (const FCrowdWorkerDependencyRecord& Record : Records)
  {
    if (Candidate.AddDependency(Record.Source, Record.Dependent)
      != ECrowdWorkerQueueResult::Added)
      return false;
  }
  *this = MoveTemp(Candidate);
  return true;
}

bool FCrowdWorkerResourceStore::Reset(
  const int32 InMaxPayloadBytes)
{
  if (InMaxPayloadBytes <= 0) return false;
  MaxPayloadBytes = InMaxPayloadBytes;
  Current.Reset();
  Building.Reset();
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerResourceStore::StageBuilding(
  FCrowdWorkerResourceRecord Record)
{
  if (Record.ResourceId == 0 || Record.Revision == 0
    || !Record.Payload.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  uint64 LatestRevision = 0;
  if (const FCrowdWorkerResourceRecord* CurrentRecord =
    Current.Find(Record.ResourceId))
    LatestRevision = CurrentRecord->Revision;
  if (const FCrowdWorkerResourceRecord* BuildingRecord =
    Building.Find(Record.ResourceId))
    LatestRevision = FMath::Max(
      LatestRevision, BuildingRecord->Revision);
  if (Record.Revision < LatestRevision)
    return ECrowdWorkerQueueResult::RejectedStale;
  if (Record.Revision == LatestRevision)
  {
    const FCrowdWorkerResourceRecord* Existing =
      Building.Find(Record.ResourceId);
    if (!Existing) Existing = Current.Find(Record.ResourceId);
    return Existing
      && Existing->Payload == Record.Payload
      ? ECrowdWorkerQueueResult::MergedDuplicate
      : ECrowdWorkerQueueResult::Conflict;
  }
  Building.Add(Record.ResourceId, MoveTemp(Record));
  return ECrowdWorkerQueueResult::Added;
}

bool FCrowdWorkerResourceStore::CommitBuildingAtEpoch(
  const uint64 Epoch,
  TArray<FCrowdWorkerResourceRevisionEvent>& OutEvents)
{
  if (Epoch == 0) return false;
  TArray<uint64> ResourceIds;
  Building.GetKeys(ResourceIds);
  ResourceIds.Sort();
  for (const uint64 ResourceId : ResourceIds)
  {
    FCrowdWorkerResourceRecord* BuildingRecord =
      Building.Find(ResourceId);
    check(BuildingRecord);
    const FCrowdWorkerResourceRecord* CurrentRecord =
      Current.Find(ResourceId);
    FCrowdWorkerResourceRevisionEvent Event;
    Event.ResourceId = ResourceId;
    Event.PreviousRevision =
      CurrentRecord ? CurrentRecord->Revision : 0;
    Event.CurrentRevision = BuildingRecord->Revision;
    Event.AppliedEpoch = Epoch;
    Event.PayloadStableHash =
      BuildingRecord->Payload.StableHash;
    OutEvents.Add(Event);
    Current.Add(ResourceId, MoveTemp(*BuildingRecord));
  }
  Building.Reset();
  return true;
}

const FCrowdWorkerResourceRecord*
FCrowdWorkerResourceStore::FindCurrent(
  const uint64 ResourceId) const
{
  return Current.Find(ResourceId);
}

void FCrowdWorkerResourceStore::GetCurrentRecords(
  TArray<FCrowdWorkerResourceRecord>& OutRecords) const
{
  OutRecords.Reset();
  OutRecords.Reserve(Current.Num());
  for (const TPair<uint64, FCrowdWorkerResourceRecord>& Pair :
    Current)
    OutRecords.Add(Pair.Value);
  OutRecords.Sort([](
    const FCrowdWorkerResourceRecord& A,
    const FCrowdWorkerResourceRecord& B)
  {
    return A.ResourceId < B.ResourceId;
  });
}

bool FCrowdWorkerResourceStore::RestoreCurrentRecords(
  const TConstArrayView<FCrowdWorkerResourceRecord> Records)
{
  TMap<uint64, FCrowdWorkerResourceRecord> Candidate;
  Candidate.Reserve(Records.Num());
  for (const FCrowdWorkerResourceRecord& Record : Records)
  {
    if (Record.ResourceId == 0
      || Record.Revision == 0
      || !Record.Payload.IsValid(MaxPayloadBytes)
      || Candidate.Contains(Record.ResourceId))
      return false;
    Candidate.Add(Record.ResourceId, Record);
  }
  Current = MoveTemp(Candidate);
  Building.Reset();
  return true;
}

bool FCrowdWorkerResourceStore::ApplyAuthoritativeRecords(
  const TConstArrayView<FCrowdWorkerResourceRecord> Records)
{
  FCrowdWorkerResourceStore Candidate = *this;
  TSet<uint64> Seen;
  for (const FCrowdWorkerResourceRecord& Record : Records)
  {
    if (Record.ResourceId == 0 || Record.Revision == 0
      || !Record.Payload.IsValid(MaxPayloadBytes)
      || Seen.Contains(Record.ResourceId))
      return false;
    Seen.Add(Record.ResourceId);
    Candidate.Current.Add(Record.ResourceId, Record);
    Candidate.Building.Remove(Record.ResourceId);
  }
  *this = MoveTemp(Candidate);
  return true;
}

uint64 FCrowdWorkerResourceStore::CalculateCurrentStableHash() const
{
  TArray<uint64> ResourceIds;
  Current.GetKeys(ResourceIds);
  ResourceIds.Sort();
  uint64 Hash = V2FnvOffset64;
  V2Fold(Hash, 1);
  for (const uint64 ResourceId : ResourceIds)
  {
    const FCrowdWorkerResourceRecord& Record =
      Current.FindChecked(ResourceId);
    V2Fold(Hash, Record.ResourceId);
    V2Fold(Hash, Record.Revision);
    V2Fold(Hash, Record.Payload.StableHash);
  }
  return Hash;
}

bool FCrowdWorkerDirtyStateRecord::IsValid(
  const int32 MaxPayloadBytes) const
{
  return EntityRef.IsValid()
    && Field < ECrowdWorkerField::Count
    && Generation != 0
    && WorkerEpoch != 0
    && StateRevision != 0
    && Payload.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerDirtyStateKey::operator<(
  const FCrowdWorkerDirtyStateKey& Other) const
{
  if (EntityRef != Other.EntityRef)
    return EntityRef < Other.EntityRef;
  return Field < Other.Field;
}

bool FCrowdWorkerDirtyStateStore::Reset(
  const int32 InMaxDirtyEntities,
  const int32 InMaxPayloadBytes)
{
  if (InMaxDirtyEntities <= 0 || InMaxPayloadBytes <= 0)
    return false;
  MaxDirtyEntities = InMaxDirtyEntities;
  MaxPayloadBytes = InMaxPayloadBytes;
  HighWatermark = 0;
  DirtyEntities.Reset();
  Records.Reset();
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerDirtyStateStore::MarkDirty(
  FCrowdWorkerDirtyStateRecord Record)
{
  if (!Record.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  const FCrowdWorkerDirtyStateKey Key{
    Record.EntityRef, Record.Field};
  if (const FCrowdWorkerDirtyStateRecord* Existing =
    Records.Find(Key))
  {
    if (Record.Generation < Existing->Generation
      || (Record.Generation == Existing->Generation
        && Record.CorrectionRevision
          < Existing->CorrectionRevision)
      || (Record.Generation == Existing->Generation
        && Record.CorrectionRevision
          == Existing->CorrectionRevision
        && Record.StateRevision < Existing->StateRevision))
      return ECrowdWorkerQueueResult::RejectedStale;
    if (Record.Generation == Existing->Generation
      && Record.CorrectionRevision
        == Existing->CorrectionRevision
      && Record.StateRevision == Existing->StateRevision)
    {
      return Record.Payload == Existing->Payload
        ? ECrowdWorkerQueueResult::MergedDuplicate
        : ECrowdWorkerQueueResult::Conflict;
    }
    Records.Add(Key, MoveTemp(Record));
    return ECrowdWorkerQueueResult::Replaced;
  }
  if (!DirtyEntities.Contains(Record.EntityRef)
    && DirtyEntities.Num() >= MaxDirtyEntities)
    return ECrowdWorkerQueueResult::RejectedCapacity;
  DirtyEntities.Add(Record.EntityRef);
  Records.Add(Key, MoveTemp(Record));
  HighWatermark = FMath::Max(
    HighWatermark, DirtyEntities.Num());
  return ECrowdWorkerQueueResult::Added;
}

int32 FCrowdWorkerDirtyStateStore::Drain(
  TArray<FCrowdWorkerDirtyStateRecord>& OutRecords)
{
  TArray<FCrowdWorkerDirtyStateKey> Keys;
  Records.GetKeys(Keys);
  Keys.Sort();
  const int32 StartCount = OutRecords.Num();
  for (const FCrowdWorkerDirtyStateKey& Key : Keys)
    OutRecords.Add(MoveTemp(Records.FindChecked(Key)));
  Records.Reset();
  DirtyEntities.Reset();
  return OutRecords.Num() - StartCount;
}

int32 FCrowdWorkerDirtyStateStore::InvalidateEntityRevision(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 MinimumCorrectionRevision)
{
  int32 Removed = 0;
  for (auto It = Records.CreateIterator(); It; ++It)
  {
    if (It.Key().EntityRef == EntityRef
      && It.Value().CorrectionRevision
        < MinimumCorrectionRevision)
    {
      It.RemoveCurrent();
      ++Removed;
    }
  }
  if (Removed > 0)
  {
    bool bHasRemaining = false;
    for (const TPair<
      FCrowdWorkerDirtyStateKey,
      FCrowdWorkerDirtyStateRecord>& Pair : Records)
    {
      if (Pair.Key.EntityRef == EntityRef)
      {
        bHasRemaining = true;
        break;
      }
    }
    if (!bHasRemaining) DirtyEntities.Remove(EntityRef);
  }
  return Removed;
}

int32 FCrowdWorkerDirtyStateStore::RemoveEntity(
  const FCrowdStableEntityRef& EntityRef)
{
  int32 Removed = 0;
  for (auto It = Records.CreateIterator(); It; ++It)
  {
    if (It.Key().EntityRef == EntityRef)
    {
      It.RemoveCurrent();
      ++Removed;
    }
  }
  DirtyEntities.Remove(EntityRef);
  return Removed;
}

bool FCrowdWorkerEntityStateStore::Reset(
  const int32 InMaxEntities,
  const int32 InMaxPayloadBytes)
{
  if (InMaxEntities <= 0 || InMaxPayloadBytes <= 0)
    return false;
  MaxEntities = InMaxEntities;
  MaxPayloadBytes = InMaxPayloadBytes;
  Entities.Reset();
  ActiveEntitiesByLogicalKey.Reset();
  LatestLifecycleSerials.Reset();
  Fields.Reset();
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerEntityStateStore::Spawn(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 Generation,
  const uint64 SourceInputSequence,
  FCrowdWorkerPayload InitialState)
{
  if (!EntityRef.IsValid() || Generation == 0
    || SourceInputSequence == 0
    || !InitialState.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  if (Entities.Contains(EntityRef))
  {
    const FCrowdWorkerDirtyStateRecord* Existing =
      Find(EntityRef, ECrowdWorkerField::InputSnapshot);
    return Existing
        && Existing->Generation == Generation
        && Existing->SourceInputSequence == SourceInputSequence
        && Existing->Payload == InitialState
      ? ECrowdWorkerQueueResult::MergedDuplicate
      : ECrowdWorkerQueueResult::Conflict;
  }
  const FLogicalEntityKey LogicalKey =
    MakeLogicalKey(EntityRef);
  if (ActiveEntitiesByLogicalKey.Contains(LogicalKey))
    return ECrowdWorkerQueueResult::Conflict;
  if (const uint32* LatestLifecycle =
    LatestLifecycleSerials.Find(LogicalKey))
  {
    if (*LatestLifecycle == MAX_uint32
      || EntityRef.LifecycleSerial != *LatestLifecycle + 1)
      return EntityRef.LifecycleSerial <= *LatestLifecycle
        ? ECrowdWorkerQueueResult::RejectedStale
        : ECrowdWorkerQueueResult::Conflict;
  }
  if (Entities.Num() >= MaxEntities)
    return ECrowdWorkerQueueResult::RejectedCapacity;
  Entities.Add(EntityRef);
  ActiveEntitiesByLogicalKey.Add(LogicalKey, EntityRef);
  LatestLifecycleSerials.Add(
    LogicalKey, EntityRef.LifecycleSerial);
  FCrowdWorkerDirtyStateRecord Record;
  Record.EntityRef = EntityRef;
  Record.Field = ECrowdWorkerField::InputSnapshot;
  Record.Generation = Generation;
  Record.WorkerEpoch = 1;
  Record.StateRevision = SourceInputSequence;
  Record.SourceInputSequence = SourceInputSequence;
  Record.Payload = MoveTemp(InitialState);
  Fields.Add({EntityRef, ECrowdWorkerField::InputSnapshot},
    MoveTemp(Record));
  return ECrowdWorkerQueueResult::Added;
}

bool FCrowdWorkerEntityStateStore::Despawn(
  const FCrowdStableEntityRef& EntityRef)
{
  if (!Entities.Remove(EntityRef)) return false;
  const FLogicalEntityKey LogicalKey =
    MakeLogicalKey(EntityRef);
  const FCrowdStableEntityRef* Active =
    ActiveEntitiesByLogicalKey.Find(LogicalKey);
  if (!Active || *Active != EntityRef)
    return false;
  ActiveEntitiesByLogicalKey.Remove(LogicalKey);
  for (auto It = Fields.CreateIterator(); It; ++It)
  {
    if (It.Key().EntityRef == EntityRef)
      It.RemoveCurrent();
  }
  return true;
}

ECrowdWorkerQueueResult
FCrowdWorkerEntityStateStore::ApplyInputState(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 Generation,
  const uint64 SourceInputSequence,
  FCrowdWorkerPayload State)
{
  if (!Entities.Contains(EntityRef) || Generation == 0
    || SourceInputSequence == 0
    || !State.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  FCrowdWorkerDirtyStateRecord Record;
  Record.EntityRef = EntityRef;
  Record.Field = ECrowdWorkerField::InputSnapshot;
  Record.Generation = Generation;
  Record.WorkerEpoch = 1;
  Record.StateRevision = SourceInputSequence;
  Record.SourceInputSequence = SourceInputSequence;
  Record.Payload = MoveTemp(State);
  return ApplyDirty(Record);
}

ECrowdWorkerQueueResult FCrowdWorkerEntityStateStore::ApplyDirty(
  const FCrowdWorkerDirtyStateRecord& Record)
{
  if (!Entities.Contains(Record.EntityRef)
    || !Record.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  const FCrowdWorkerDirtyStateKey Key{
    Record.EntityRef, Record.Field};
  if (const FCrowdWorkerDirtyStateRecord* Existing =
    Fields.Find(Key))
  {
    if (Record.Generation < Existing->Generation
      || (Record.Generation == Existing->Generation
        && Record.CorrectionRevision
          < Existing->CorrectionRevision)
      || (Record.Generation == Existing->Generation
        && Record.CorrectionRevision
          == Existing->CorrectionRevision
        && Record.StateRevision < Existing->StateRevision))
      return ECrowdWorkerQueueResult::RejectedStale;
    if (Record.Generation == Existing->Generation
      && Record.CorrectionRevision
        == Existing->CorrectionRevision
      && Record.StateRevision == Existing->StateRevision)
    {
      return Record.Payload == Existing->Payload
        ? ECrowdWorkerQueueResult::MergedDuplicate
        : ECrowdWorkerQueueResult::Conflict;
    }
  }
  Fields.Add(Key, Record);
  return ECrowdWorkerQueueResult::Replaced;
}

bool FCrowdWorkerEntityStateStore::ApplyAuthoritativeDirty(
  const FCrowdWorkerDirtyStateRecord& Record)
{
  if (!Entities.Contains(Record.EntityRef)
    || !Record.IsValid(MaxPayloadBytes))
    return false;
  Fields.Add({Record.EntityRef, Record.Field}, Record);
  return true;
}

bool FCrowdWorkerEntityStateStore::RemoveAuthoritativeField(
  const FCrowdStableEntityRef& EntityRef,
  const ECrowdWorkerField Field)
{
  if (!Entities.Contains(EntityRef)
    || Field == ECrowdWorkerField::InputSnapshot)
    return false;
  return Fields.Remove({EntityRef, Field}) > 0;
}

const FCrowdWorkerDirtyStateRecord*
FCrowdWorkerEntityStateStore::Find(
  const FCrowdStableEntityRef& EntityRef,
  const ECrowdWorkerField Field) const
{
  return Fields.Find({EntityRef, Field});
}

bool FCrowdWorkerEntityStateStore::Contains(
  const FCrowdStableEntityRef& EntityRef) const
{
  return Entities.Contains(EntityRef);
}

void FCrowdWorkerEntityStateStore::GetEntities(
  TArray<FCrowdStableEntityRef>& OutEntities) const
{
  OutEntities.Reset();
  OutEntities.Reserve(Entities.Num());
  for (const FCrowdStableEntityRef& EntityRef : Entities)
    OutEntities.Add(EntityRef);
  OutEntities.Sort();
}

void FCrowdWorkerEntityStateStore::GetStateRecords(
  TArray<FCrowdWorkerDirtyStateRecord>& OutRecords) const
{
  OutRecords.Reset();
  OutRecords.Reserve(Fields.Num());
  for (const TPair<
    FCrowdWorkerDirtyStateKey,
    FCrowdWorkerDirtyStateRecord>& Pair : Fields)
    OutRecords.Add(Pair.Value);
  OutRecords.Sort([](
    const FCrowdWorkerDirtyStateRecord& A,
    const FCrowdWorkerDirtyStateRecord& B)
  {
    const FCrowdWorkerDirtyStateKey AKey{A.EntityRef, A.Field};
    const FCrowdWorkerDirtyStateKey BKey{B.EntityRef, B.Field};
    return AKey < BKey;
  });
}

bool FCrowdWorkerEntityStateStore::RestoreStateRecords(
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> Records)
{
  FCrowdWorkerEntityStateStore Candidate;
  if (!Candidate.Reset(MaxEntities, MaxPayloadBytes))
    return false;
  TArray<FCrowdWorkerDirtyStateRecord> Sorted;
  Sorted.Append(Records);
  Sorted.Sort([](
    const FCrowdWorkerDirtyStateRecord& A,
    const FCrowdWorkerDirtyStateRecord& B)
  {
    const FCrowdWorkerDirtyStateKey AKey{A.EntityRef, A.Field};
    const FCrowdWorkerDirtyStateKey BKey{B.EntityRef, B.Field};
    return AKey < BKey;
  });
  for (const FCrowdWorkerDirtyStateRecord& Record : Sorted)
  {
    if (Record.Field != ECrowdWorkerField::InputSnapshot)
      continue;
    if (Candidate.Spawn(
        Record.EntityRef,
        Record.Generation,
        Record.SourceInputSequence,
        Record.Payload)
      != ECrowdWorkerQueueResult::Added)
      return false;
  }
  for (const FCrowdWorkerDirtyStateRecord& Record : Sorted)
  {
    if (Record.Field == ECrowdWorkerField::InputSnapshot)
    {
      const FCrowdWorkerDirtyStateRecord* Spawned =
        Candidate.Find(
          Record.EntityRef,
          ECrowdWorkerField::InputSnapshot);
      if (!Spawned)
        return false;
      FCrowdWorkerDirtyStateRecord Restored = Record;
      const ECrowdWorkerQueueResult Result =
        Candidate.ApplyDirty(Restored);
      if (Result != ECrowdWorkerQueueResult::Replaced
        && Result != ECrowdWorkerQueueResult::MergedDuplicate)
        return false;
      continue;
    }
    const ECrowdWorkerQueueResult Result =
      Candidate.ApplyDirty(Record);
    if (Result != ECrowdWorkerQueueResult::Replaced
      && Result != ECrowdWorkerQueueResult::Added)
      return false;
  }
  if (Candidate.Fields.Num() != Sorted.Num())
    return false;
  *this = MoveTemp(Candidate);
  return true;
}

void FCrowdWorkerEntityStateStore::GetLifecycleWatermarks(
  TArray<FCrowdWorkerLifecycleWatermark>& OutWatermarks) const
{
  OutWatermarks.Reset();
  OutWatermarks.Reserve(LatestLifecycleSerials.Num());
  for (const TPair<FLogicalEntityKey, uint32>& Pair :
    LatestLifecycleSerials)
  {
    OutWatermarks.Add({
      Pair.Key.ProviderId,
      Pair.Key.StableEntityId,
      Pair.Value});
  }
  OutWatermarks.Sort([](
    const FCrowdWorkerLifecycleWatermark& A,
    const FCrowdWorkerLifecycleWatermark& B)
  {
    if (A.ProviderId != B.ProviderId)
      return A.ProviderId < B.ProviderId;
    return A.StableEntityId < B.StableEntityId;
  });
}

bool FCrowdWorkerEntityStateStore::RestoreLifecycleWatermarks(
  const TConstArrayView<FCrowdWorkerLifecycleWatermark> Watermarks)
{
  TMap<FLogicalEntityKey, uint32> Candidate;
  for (const FCrowdStableEntityRef& EntityRef : Entities)
    Candidate.Add(MakeLogicalKey(EntityRef), EntityRef.LifecycleSerial);
  FLogicalEntityKey Previous;
  bool bHasPrevious = false;
  for (const FCrowdWorkerLifecycleWatermark& Watermark : Watermarks)
  {
    const FLogicalEntityKey Key{
      Watermark.ProviderId,
      Watermark.StableEntityId};
    if (Watermark.ProviderId == 0
      || Watermark.StableEntityId == 0
      || Watermark.LastLifecycleSerial == 0
      || (bHasPrevious
        && !(Previous.ProviderId < Key.ProviderId
          || (Previous.ProviderId == Key.ProviderId
            && Previous.StableEntityId < Key.StableEntityId))))
      return false;
    if (const uint32* ActiveSerial = Candidate.Find(Key))
    {
      if (Watermark.LastLifecycleSerial < *ActiveSerial)
        return false;
    }
    Candidate.Add(Key, Watermark.LastLifecycleSerial);
    Previous = Key;
    bHasPrevious = true;
  }
  LatestLifecycleSerials = MoveTemp(Candidate);
  return true;
}

uint64 FCrowdWorkerEntityStateStore::CalculateStableHash() const
{
  TArray<FCrowdWorkerDirtyStateKey> Keys;
  Fields.GetKeys(Keys);
  Keys.Sort();
  uint64 Hash = V2FnvOffset64;
  V2Fold(Hash, 1);
  for (const FCrowdWorkerDirtyStateKey& Key : Keys)
  {
    const FCrowdWorkerDirtyStateRecord& Record =
      Fields.FindChecked(Key);
    V2FoldRef(Hash, Key.EntityRef);
    V2Fold(Hash, static_cast<uint8>(Key.Field));
    V2Fold(Hash, Record.Generation);
    V2Fold(Hash, Record.WorkerEpoch);
    V2Fold(Hash, Record.StateRevision);
    V2Fold(Hash, Record.CorrectionRevision);
    V2Fold(Hash, Record.SourceInputSequence);
    V2Fold(Hash, Record.Payload.StableHash);
  }
  return Hash;
}

bool FCrowdWorkerCommandRecord::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && EntityRef.IsValid()
    && CommandId != 0
    && FMath::IsFinite(EffectiveSimulationTimeSeconds)
    && EffectiveSimulationTimeSeconds >= 0.0
    && Payload.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerCommandStore::Reset(
  const int32 InMaxCommands,
  const int32 InMaxPayloadBytes)
{
  if (InMaxCommands <= 0 || InMaxPayloadBytes <= 0)
    return false;
  MaxCommands = InMaxCommands;
  MaxPayloadBytes = InMaxPayloadBytes;
  HighWatermark = 0;
  Records.Reset();
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerCommandStore::Enqueue(
  const FCrowdWorkerCommandDelta& Command)
{
  FCrowdWorkerCommandRecord Record;
  Record.InputSequence = Command.InputSequence;
  Record.EntityRef = Command.EntityRef;
  Record.CommandId = Command.CommandId;
  Record.EffectiveSimulationTimeSeconds =
    Command.EffectiveSimulationTimeSeconds;
  Record.Payload = Command.Payload;
  if (!Record.IsValid(MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  if (const FCrowdWorkerCommandRecord* Existing =
    Records.Find(Record.InputSequence))
  {
    return *Existing == Record
      ? ECrowdWorkerQueueResult::MergedDuplicate
      : ECrowdWorkerQueueResult::Conflict;
  }
  if (Records.Num() >= MaxCommands)
    return ECrowdWorkerQueueResult::RejectedCapacity;
  Records.Add(Record.InputSequence, MoveTemp(Record));
  HighWatermark = FMath::Max(HighWatermark, Records.Num());
  return ECrowdWorkerQueueResult::Added;
}

int32 FCrowdWorkerCommandStore::CollectEntity(
  const FCrowdStableEntityRef& EntityRef,
  const double InclusiveSimulationTimeSeconds,
  TArray<FCrowdWorkerCommandRecord>& OutCommands) const
{
  const int32 StartCount = OutCommands.Num();
  for (const TPair<uint64, FCrowdWorkerCommandRecord>& Pair :
    Records)
  {
    if (Pair.Value.EntityRef == EntityRef
      && Pair.Value.EffectiveSimulationTimeSeconds
        <= InclusiveSimulationTimeSeconds)
      OutCommands.Add(Pair.Value);
  }
  OutCommands.Sort([](
    const FCrowdWorkerCommandRecord& A,
    const FCrowdWorkerCommandRecord& B)
  {
    return A.InputSequence < B.InputSequence;
  });
  return OutCommands.Num() - StartCount;
}

bool FCrowdWorkerCommandStore::Acknowledge(
  const FCrowdStableEntityRef& EntityRef,
  const uint64 InputSequence)
{
  const FCrowdWorkerCommandRecord* Existing =
    Records.Find(InputSequence);
  if (!Existing || Existing->EntityRef != EntityRef)
    return false;
  return Records.Remove(InputSequence) == 1;
}

bool FCrowdWorkerCommandStore::Acknowledge(
  const uint64 InputSequence)
{
  return InputSequence != 0
    && Records.Remove(InputSequence) == 1;
}

int32 FCrowdWorkerCommandStore::RemoveEntity(
  const FCrowdStableEntityRef& EntityRef)
{
  int32 Removed = 0;
  for (auto It = Records.CreateIterator(); It; ++It)
  {
    if (It.Value().EntityRef == EntityRef)
    {
      It.RemoveCurrent();
      ++Removed;
    }
  }
  return Removed;
}

void FCrowdWorkerCommandStore::GetRecords(
  TArray<FCrowdWorkerCommandRecord>& OutRecords) const
{
  OutRecords.Reset();
  OutRecords.Reserve(Records.Num());
  for (const TPair<uint64, FCrowdWorkerCommandRecord>& Pair : Records)
    OutRecords.Add(Pair.Value);
  OutRecords.Sort([](
    const FCrowdWorkerCommandRecord& A,
    const FCrowdWorkerCommandRecord& B)
  {
    return A.InputSequence < B.InputSequence;
  });
}

bool FCrowdWorkerCommandStore::RestoreRecords(
  const TConstArrayView<FCrowdWorkerCommandRecord> InRecords)
{
  if (InRecords.Num() > MaxCommands) return false;
  FCrowdWorkerCommandStore Candidate;
  if (!Candidate.Reset(MaxCommands, MaxPayloadBytes))
    return false;
  for (const FCrowdWorkerCommandRecord& Record : InRecords)
  {
    FCrowdWorkerCommandDelta Delta;
    Delta.InputSequence = Record.InputSequence;
    Delta.EntityRef = Record.EntityRef;
    Delta.CommandId = Record.CommandId;
    Delta.EffectiveSimulationTimeSeconds =
      Record.EffectiveSimulationTimeSeconds;
    Delta.Payload = Record.Payload;
    if (Candidate.Enqueue(Delta)
      != ECrowdWorkerQueueResult::Added)
      return false;
  }
  *this = MoveTemp(Candidate);
  return true;
}

uint64 FCrowdWorkerCommandStore::CalculateStableHash() const
{
  TArray<uint64> Sequences;
  Records.GetKeys(Sequences);
  Sequences.Sort();
  uint64 Hash = V2FnvOffset64;
  V2Fold(Hash, 1);
  for (const uint64 Sequence : Sequences)
  {
    const FCrowdWorkerCommandRecord& Record =
      Records.FindChecked(Sequence);
    V2Fold(Hash, Record.InputSequence);
    V2FoldRef(Hash, Record.EntityRef);
    V2Fold(Hash, Record.CommandId);
    V2Fold(Hash, Record.EffectiveSimulationTimeSeconds);
    V2Fold(Hash, Record.Payload.StableHash);
  }
  return Hash;
}

bool FCrowdWorkerOrderedEventStore::Reset(
  const int32 InMaxEvents,
  const int32 InMaxPayloadBytes,
  const uint64 InGeneration,
  const uint64 InNextEventSequence)
{
  if (InMaxEvents <= 0 || InMaxPayloadBytes <= 0
    || InGeneration == 0 || InNextEventSequence == 0)
    return false;
  MaxEvents = InMaxEvents;
  MaxPayloadBytes = InMaxPayloadBytes;
  HighWatermark = 0;
  Generation = InGeneration;
  LastAcceptedEventSequence = InNextEventSequence - 1;
  Events.Reset();
  Events.Reserve(MaxEvents);
  return true;
}

ECrowdWorkerQueueResult FCrowdWorkerOrderedEventStore::Append(
  FCrowdWorkerGameplayEvent Event)
{
  if (!Event.IsValid(Generation, MaxPayloadBytes))
    return ECrowdWorkerQueueResult::RejectedInvalid;
  if (Event.EventSequence <= LastAcceptedEventSequence)
    return ECrowdWorkerQueueResult::RejectedStale;
  if (Event.EventSequence != LastAcceptedEventSequence + 1)
    return ECrowdWorkerQueueResult::Conflict;
  if (Events.Num() >= MaxEvents)
    return ECrowdWorkerQueueResult::RejectedCapacity;
  LastAcceptedEventSequence = Event.EventSequence;
  Events.Add(MoveTemp(Event));
  HighWatermark = FMath::Max(HighWatermark, Events.Num());
  return ECrowdWorkerQueueResult::Added;
}

int32 FCrowdWorkerOrderedEventStore::Drain(
  TArray<FCrowdWorkerGameplayEvent>& OutEvents)
{
  const int32 StartCount = OutEvents.Num();
  OutEvents.Append(MoveTemp(Events));
  Events.Reset();
  return OutEvents.Num() - StartCount;
}

uint64 FCrowdWorkerCheckpoint::CalculateStableHash() const
{
  uint64 Hash = V2FnvOffset64;
  V2Fold(Hash, 1);
  V2Fold(Hash, Generation);
  V2Fold(Hash, WorkerEpoch);
  V2Fold(Hash, AbsoluteSimulationTick);
  V2Fold(Hash, LastAppliedInputSequence);
  V2Fold(Hash, LastOrderedEventSequence);
  V2Fold(Hash, EntityStateHash);
  V2Fold(Hash, ResourceRevisionHash);
  return Hash;
}

void FCrowdWorkerCheckpoint::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerCheckpoint::IsValid() const
{
  return Generation != 0
    && WorkerEpoch != 0
    && AbsoluteSimulationTick != 0
    && EntityStateHash != 0
    && ResourceRevisionHash != 0
    && StableHash == CalculateStableHash();
}

bool FCrowdWorkerDomainShard::IsValid() const
{
  if (Domain >= ECrowdWorkerDomainId::Count
    || WorkItems.IsEmpty())
    return false;
  for (const FCrowdWorkerWorkItem& Work : WorkItems)
  {
    if (!Work.IsValid() || Work.Key.Domain != Domain)
      return false;
  }
  return true;
}

bool FCrowdWorkerDeterministicShardPlanner::Build(
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  const int32 ShardEntityCount,
  TArray<FCrowdWorkerDomainShard>& OutShards)
{
  OutShards.Reset();
  if (ShardEntityCount <= 0) return false;
  TArray<FCrowdWorkerWorkItem> Sorted;
  Sorted.Reserve(WorkItems.Num());
  for (FCrowdWorkerWorkItem Work : WorkItems)
  {
    Work.NormalizePair();
    if (!Work.IsValid()) return false;
    Sorted.Add(MoveTemp(Work));
  }
  Sorted.Sort(V2WorkStableLess);
  TMap<ECrowdWorkerDomainId, uint32> NextOrdinalByDomain;
  for (int32 Offset = 0; Offset < Sorted.Num();)
  {
    const ECrowdWorkerDomainId Domain =
      Sorted[Offset].Key.Domain;
    int32 DomainEnd = Offset + 1;
    while (DomainEnd < Sorted.Num()
      && Sorted[DomainEnd].Key.Domain == Domain)
      ++DomainEnd;
    uint32& NextOrdinal = NextOrdinalByDomain.FindOrAdd(Domain);
    for (int32 ShardStart = Offset;
      ShardStart < DomainEnd;
      ShardStart += ShardEntityCount)
    {
      FCrowdWorkerDomainShard Shard;
      Shard.Domain = Domain;
      Shard.ShardOrdinal = NextOrdinal++;
      const int32 Count = FMath::Min(
        ShardEntityCount, DomainEnd - ShardStart);
      Shard.WorkItems.Append(
        Sorted.GetData() + ShardStart, Count);
      OutShards.Add(MoveTemp(Shard));
    }
    Offset = DomainEnd;
  }
  return true;
}

bool FCrowdWorkerDeterministicShardPlanner::Merge(
  const TConstArrayView<FCrowdWorkerDomainShardResult> Results,
  const int32 MaxDirtyEntities,
  const int32 MaxPayloadBytes,
  const int32 MaxOrderedEvents,
  FCrowdWorkerDomainOutput& OutMerged,
  const uint64 FirstOrderedEventSequence)
{
  OutMerged = {};
  if (MaxDirtyEntities <= 0
    || MaxPayloadBytes <= 0
    || MaxOrderedEvents <= 0
    || FirstOrderedEventSequence == 0)
    return false;

  TArray<int32> Order;
  TSet<uint64> SeenShardKeys;
  for (int32 Index = 0; Index < Results.Num(); ++Index)
  {
    const FCrowdWorkerDomainShardResult& Result = Results[Index];
    if (!Result.bSucceeded
      || Result.Domain >= ECrowdWorkerDomainId::Count)
      return false;
    const uint64 ShardKey =
      (static_cast<uint64>(Result.Domain) << 32)
      | Result.ShardOrdinal;
    if (SeenShardKeys.Contains(ShardKey)) return false;
    SeenShardKeys.Add(ShardKey);
    Order.Add(Index);
  }
  Order.Sort([&Results](const int32 A, const int32 B)
  {
    const FCrowdWorkerDomainShardResult& Left = Results[A];
    const FCrowdWorkerDomainShardResult& Right = Results[B];
    if (Left.Domain != Right.Domain)
      return Left.Domain < Right.Domain;
    return Left.ShardOrdinal < Right.ShardOrdinal;
  });

  FCrowdWorkerDirtyStateStore DirtyStore;
  if (!DirtyStore.Reset(MaxDirtyEntities, MaxPayloadBytes))
    return false;
  for (const int32 Index : Order)
  {
    const FCrowdWorkerDomainOutput& Output =
      Results[Index].Output;
    OutMerged.NextWork.Append(Output.NextWork);
    OutMerged.Wakeups.Append(Output.Wakeups);
    OutMerged.OrderedEvents.Append(Output.OrderedEvents);
    OutMerged.DeclaredDependencies.Append(
      Output.DeclaredDependencies);
    OutMerged.ObservedDependencies.Append(
      Output.ObservedDependencies);
    OutMerged.ConsumedCommandInputSequences.Append(
      Output.ConsumedCommandInputSequences);
    for (const FCrowdWorkerDirtyStateRecord& Dirty :
      Output.DirtyStates)
    {
      const ECrowdWorkerQueueResult DirtyResult =
        DirtyStore.MarkDirty(Dirty);
      if (DirtyResult != ECrowdWorkerQueueResult::Added
        && DirtyResult != ECrowdWorkerQueueResult::Replaced
        && DirtyResult
          != ECrowdWorkerQueueResult::MergedDuplicate)
        return false;
    }
  }
  DirtyStore.Drain(OutMerged.DirtyStates);

  for (FCrowdWorkerWorkItem& Work : OutMerged.NextWork)
  {
    Work.NormalizePair();
    if (!Work.IsValid()) return false;
  }
  OutMerged.NextWork.Sort(V2WorkStableLess);
  for (int32 Index = OutMerged.NextWork.Num() - 1;
    Index > 0;
    --Index)
  {
    FCrowdWorkerWorkItem& Current =
      OutMerged.NextWork[Index];
    FCrowdWorkerWorkItem& Previous =
      OutMerged.NextWork[Index - 1];
    if (Current.Key != Previous.Key) continue;
    Previous.Priority = FMath::Min(
      Previous.Priority, Current.Priority);
    Previous.ReasonMask |= Current.ReasonMask;
    Previous.CorrectionRevision = FMath::Max(
      Previous.CorrectionRevision,
      Current.CorrectionRevision);
    Previous.EnqueueEpoch = FMath::Max(
      Previous.EnqueueEpoch, Current.EnqueueEpoch);
    OutMerged.NextWork.RemoveAt(
      Index, EAllowShrinking::No);
  }

  OutMerged.Wakeups.Sort([](
    const FCrowdWorkerWakeup& A,
    const FCrowdWorkerWakeup& B)
  {
    if (A.AbsoluteSimulationTick
      != B.AbsoluteSimulationTick)
      return A.AbsoluteSimulationTick
        < B.AbsoluteSimulationTick;
    return A.Key < B.Key;
  });
  TMap<FCrowdWorkerWakeupKey, int32> WakeupIndices;
  TArray<FCrowdWorkerWakeup> MergedWakeups;
  for (const FCrowdWorkerWakeup& Wakeup : OutMerged.Wakeups)
  {
    if (!Wakeup.IsValid()) return false;
    if (int32* ExistingIndex = WakeupIndices.Find(Wakeup.Key))
    {
      FCrowdWorkerWakeup& Existing =
        MergedWakeups[*ExistingIndex];
      if (Wakeup.Revision < Existing.Revision) continue;
      if (Wakeup.Revision == Existing.Revision)
      {
        if (Wakeup.AbsoluteSimulationTick
          != Existing.AbsoluteSimulationTick)
          return false;
        Existing.ReasonMask |= Wakeup.ReasonMask;
        Existing.Priority = FMath::Min(
          Existing.Priority, Wakeup.Priority);
        continue;
      }
      Existing = Wakeup;
      continue;
    }
    WakeupIndices.Add(
      Wakeup.Key, MergedWakeups.Add(Wakeup));
  }
  OutMerged.Wakeups = MoveTemp(MergedWakeups);
  OutMerged.Wakeups.Sort([](
    const FCrowdWorkerWakeup& A,
    const FCrowdWorkerWakeup& B)
  {
    if (A.AbsoluteSimulationTick
      != B.AbsoluteSimulationTick)
      return A.AbsoluteSimulationTick
        < B.AbsoluteSimulationTick;
    return A.Key < B.Key;
  });

  if (OutMerged.OrderedEvents.Num() > MaxOrderedEvents)
    return false;
  uint64 NextEventSequence = FirstOrderedEventSequence;
  for (FCrowdWorkerGameplayEvent& Event :
    OutMerged.OrderedEvents)
  {
    // Shards produce stable local event order. Only the Owner merge may
    // assign the global contiguous sequence; shard-local sequences can
    // overlap because every shard reads the same frozen epoch baseline.
    if ((Event.EntityRef.IsValid() == false
        && !Event.EntityRef.IsUnset())
      || Event.Generation == 0
      || Event.WorkerEpoch == 0
      || Event.EventSequence == 0
      || Event.EventId == 0
      || !Event.Payload.IsValid(MaxPayloadBytes)
      || Event.StableHash != Event.CalculateStableHash())
      return false;
    Event.EventSequence = NextEventSequence++;
    if (NextEventSequence == 0) return false;
    Event.RecalculateStableHash();
    if (!Event.IsValid(Event.Generation, MaxPayloadBytes))
      return false;
  }

  OutMerged.DeclaredDependencies.Sort([](
    const FCrowdWorkerDependencyDeclaration& A,
    const FCrowdWorkerDependencyDeclaration& B)
  {
    if (A.Source != B.Source)
      return A.Source < B.Source;
    return A.Dependent.Key < B.Dependent.Key;
  });
  for (const FCrowdWorkerDependencyDeclaration& Declaration :
    OutMerged.DeclaredDependencies)
  {
    if (!Declaration.IsValid()) return false;
  }

  OutMerged.ObservedDependencies.Sort([](
    const FCrowdWorkerDependencyObservation& A,
    const FCrowdWorkerDependencyObservation& B)
  {
    if (A.Source != B.Source)
      return A.Source < B.Source;
    return A.Dependent < B.Dependent;
  });
  for (const FCrowdWorkerDependencyObservation& Observation :
    OutMerged.ObservedDependencies)
  {
    if (!Observation.IsValid()) return false;
  }
  OutMerged.ConsumedCommandInputSequences.Sort();
  for (int32 Index =
      OutMerged.ConsumedCommandInputSequences.Num() - 1;
    Index > 0; --Index)
  {
    if (OutMerged.ConsumedCommandInputSequences[Index] == 0)
      return false;
    if (OutMerged.ConsumedCommandInputSequences[Index]
        == OutMerged.ConsumedCommandInputSequences[Index - 1])
      OutMerged.ConsumedCommandInputSequences.RemoveAt(
        Index, EAllowShrinking::No);
  }
  if (!OutMerged.ConsumedCommandInputSequences.IsEmpty()
    && OutMerged.ConsumedCommandInputSequences[0] == 0)
    return false;
  return true;
}

uint64 FCrowdWorkerDeterministicShardPlanner::CalculateStableHash(
  const FCrowdWorkerDomainOutput& Output)
{
  uint64 Hash = V2FnvOffset64;
  V2Fold(Hash, 1);
  V2Fold(Hash, Output.NextWork.Num());
  for (const FCrowdWorkerWorkItem& Work : Output.NextWork)
  {
    V2FoldWorkKey(Hash, Work.Key);
    V2Fold(Hash, static_cast<uint8>(Work.Priority));
    V2Fold(Hash, Work.EnqueueEpoch);
    V2Fold(Hash, Work.CorrectionRevision);
    V2Fold(Hash, Work.ReasonMask);
  }
  V2Fold(Hash, Output.Wakeups.Num());
  for (const FCrowdWorkerWakeup& Wakeup : Output.Wakeups)
  {
    V2Fold(Hash, static_cast<uint8>(Wakeup.Key.Domain));
    V2FoldRef(Hash, Wakeup.Key.EntityRef);
    V2Fold(Hash, Wakeup.Key.WakeupId);
    V2Fold(Hash, Wakeup.AbsoluteSimulationTick);
    V2Fold(Hash, Wakeup.Revision);
    V2Fold(Hash, Wakeup.ReasonMask);
  }
  V2Fold(Hash, Output.DirtyStates.Num());
  for (const FCrowdWorkerDirtyStateRecord& Dirty :
    Output.DirtyStates)
  {
    V2FoldRef(Hash, Dirty.EntityRef);
    V2Fold(Hash, static_cast<uint8>(Dirty.Field));
    V2Fold(Hash, Dirty.Generation);
    V2Fold(Hash, Dirty.WorkerEpoch);
    V2Fold(Hash, Dirty.StateRevision);
    V2Fold(Hash, Dirty.CorrectionRevision);
    V2Fold(Hash, Dirty.SourceInputSequence);
    V2Fold(Hash, Dirty.Payload.StableHash);
  }
  V2Fold(Hash, Output.OrderedEvents.Num());
  for (const FCrowdWorkerGameplayEvent& Event :
    Output.OrderedEvents)
    V2Fold(Hash, Event.StableHash);
  V2Fold(Hash, Output.DeclaredDependencies.Num());
  for (const FCrowdWorkerDependencyDeclaration& Declaration :
    Output.DeclaredDependencies)
  {
    V2Fold(Hash, static_cast<uint8>(Declaration.Source.Kind));
    V2FoldRef(Hash, Declaration.Source.EntityRef);
    V2Fold(Hash, Declaration.Source.ScopeKey);
    V2FoldWorkKey(Hash, Declaration.Dependent.Key);
  }
  V2Fold(Hash, Output.ObservedDependencies.Num());
  for (const FCrowdWorkerDependencyObservation& Observation :
    Output.ObservedDependencies)
  {
    V2Fold(Hash, static_cast<uint8>(Observation.Source.Kind));
    V2FoldRef(Hash, Observation.Source.EntityRef);
    V2Fold(Hash, Observation.Source.ScopeKey);
    V2FoldWorkKey(Hash, Observation.Dependent);
  }
  V2Fold(
    Hash, Output.ConsumedCommandInputSequences.Num());
  for (const uint64 Sequence :
    Output.ConsumedCommandInputSequences)
    V2Fold(Hash, Sequence);
  return Hash;
}

bool FCrowdWorkerDomainRegistry::Register(
  TUniquePtr<ICrowdWorkerDomainExecutor> Executor)
{
  if (bFrozen || !Executor
    || Executor->GetDomainId() >= ECrowdWorkerDomainId::Count)
    return false;
  for (const TUniquePtr<ICrowdWorkerDomainExecutor>& Existing :
    Executors)
  {
    if (Existing->GetDomainId() == Executor->GetDomainId())
      return false;
  }
  Executors.Add(MoveTemp(Executor));
  return true;
}

bool FCrowdWorkerDomainRegistry::Freeze()
{
  if (bFrozen || Executors.IsEmpty()) return false;
  TSet<ECrowdWorkerDomainId> Registered;
  for (const TUniquePtr<ICrowdWorkerDomainExecutor>& Executor :
    Executors)
    Registered.Add(Executor->GetDomainId());
  for (const TUniquePtr<ICrowdWorkerDomainExecutor>& Executor :
    Executors)
  {
    TArray<ECrowdWorkerDomainId> Dependencies;
    Executor->GetDependencies(Dependencies);
    for (const ECrowdWorkerDomainId Dependency : Dependencies)
    {
      if (!Registered.Contains(Dependency)
        || CrowdWorkerRuntimeV2DomainExecutionRank(Dependency)
          >= CrowdWorkerRuntimeV2DomainExecutionRank(
            Executor->GetDomainId()))
        return false;
    }
  }
  bFrozen = true;
  return true;
}

bool FCrowdWorkerDomainRegistry::HasDomain(
  const ECrowdWorkerDomainId Domain) const
{
  for (const TUniquePtr<ICrowdWorkerDomainExecutor>& Executor :
    Executors)
  {
    if (Executor->GetDomainId() == Domain)
      return true;
  }
  return false;
}

bool FCrowdWorkerDomainRegistry::ExecuteEpoch(
  const FCrowdWorkerDomainContext& Context,
  const TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
  FCrowdWorkerDomainOutput& OutOutput)
{
  if (!bFrozen || Context.Generation == 0
    || Context.WorkerEpoch == 0)
    return false;
  for (uint8 ExecutionRank = 0;
    ExecutionRank
      < static_cast<uint8>(ECrowdWorkerDomainId::Count);
    ++ExecutionRank)
  {
    ICrowdWorkerDomainExecutor* Executor = nullptr;
    for (TUniquePtr<ICrowdWorkerDomainExecutor>& Candidate :
      Executors)
    {
      if (CrowdWorkerRuntimeV2DomainExecutionRank(
          Candidate->GetDomainId()) == ExecutionRank)
      {
        Executor = Candidate.Get();
        break;
      }
    }
    if (!Executor) continue;
    TArray<FCrowdWorkerWorkItem> DomainWork;
    for (const FCrowdWorkerWorkItem& Work : WorkItems)
    {
      if (Work.Key.Domain == Executor->GetDomainId())
        DomainWork.Add(Work);
    }
    DomainWork.Sort(V2WorkStableLess);
    if (!DomainWork.IsEmpty()
      && !Executor->Execute(Context, DomainWork, OutOutput))
      return false;
  }
  return true;
}
