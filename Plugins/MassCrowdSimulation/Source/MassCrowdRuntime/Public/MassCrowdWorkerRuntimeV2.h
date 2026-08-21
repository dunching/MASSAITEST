#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerContracts.h"

inline uint32 CrowdWorkerRuntimeV2HashEntityRef(
  const FCrowdStableEntityRef& Ref)
{
  uint32 Hash = HashCombineFast(
    ::GetTypeHash(Ref.ProviderId),
    ::GetTypeHash(Ref.StableEntityId));
  return HashCombineFast(
    Hash, ::GetTypeHash(Ref.LifecycleSerial));
}

namespace CrowdWorkerResourceIds
{
  // Declared in Runtime so resource routing never introduces a reverse
  // dependency from MassCrowdRuntime to MassCrowdProjectiles.
  constexpr uint64 ProjectileControl = 0x435750524f4a4354ull;
  constexpr uint64 ObjectiveRevisionNamespace = 0x8000000000000000ull;

  inline uint64 ObjectiveRevision(const uint64 ObjectiveId)
  {
    return ObjectiveRevisionNamespace
      | (ObjectiveId & ~ObjectiveRevisionNamespace);
  }

  inline uint64 ExternalGameplayInput(const uint16 InputTypeId)
  {
    return 0x4000000000000000ull | InputTypeId;
  }
}

namespace CrowdWorkerReasonMasks
{
  // A Clock intent advances host Combat even when no projectile is active and
  // no ProjectileControl configuration revision was published this tick.
  constexpr uint64 CombatClock = 1ull << 19;
}

enum class ECrowdWorkerDomainId : uint8
{
  LifecycleInput = 0,
  Behavior = 1,
  FlowResource = 2,
  Target = 3,
  CombatReactive = 4,
  Movement = 5,
  ParticleInteraction = 6,
  FacingFinalize = 7,
  Publish = 8,
  // Public contract: append new domains. MovementPlanning emits Movement work
  // into the next propagation round, so it does not rely on numeric adjacency.
  MovementPlanning = 9,
  Count = 10
};

enum class ECrowdWorkerField : uint8
{
  Lifecycle = 0,
  Behavior = 1,
  Resource = 2,
  Target = 3,
  Combat = 4,
  Projectile = 5,
  Movement = 6,
  Particle = 7,
  Facing = 8,
  Presentation = 9,
  InputSnapshot = 10,
  // Public contract: append new fields so persisted field masks remain stable.
  MovementPlan = 11,
  TargetCohort = 12,
  MovementProfile = 13,
  FlowBinding = 14,
  Participation = 15,
  Count = 16
};

// Stable public domain IDs are append-only and therefore cannot also encode
// the evolving DAG order. All execution schedulers must use this rank.
inline uint8 CrowdWorkerRuntimeV2DomainExecutionRank(
  const ECrowdWorkerDomainId Domain)
{
  switch (Domain)
  {
  case ECrowdWorkerDomainId::LifecycleInput: return 0;
  case ECrowdWorkerDomainId::Behavior: return 1;
  case ECrowdWorkerDomainId::FlowResource: return 2;
  case ECrowdWorkerDomainId::Target: return 3;
  case ECrowdWorkerDomainId::CombatReactive: return 4;
  case ECrowdWorkerDomainId::MovementPlanning: return 5;
  case ECrowdWorkerDomainId::Movement: return 6;
  case ECrowdWorkerDomainId::ParticleInteraction: return 7;
  case ECrowdWorkerDomainId::FacingFinalize: return 8;
  case ECrowdWorkerDomainId::Publish: return 9;
  default: return MAX_uint8;
  }
}

inline uint64 CrowdWorkerRuntimeV2FieldMask(
  const ECrowdWorkerField Field)
{
  return Field < ECrowdWorkerField::Count
    ? 1ull << (16 + static_cast<uint8>(Field))
    : 0;
}

inline uint64 CrowdWorkerRuntimeV2DependencyScopeForField(
  const ECrowdWorkerField Field)
{
  return Field < ECrowdWorkerField::Count
    ? 1ull + static_cast<uint8>(Field)
    : 0;
}

enum class ECrowdWorkerWorkKind : uint8
{
  Entity = 0,
  Pair,
  Resource,
  Cohort,
  Timer
};

enum class ECrowdWorkerWorkPriority : uint8
{
  Critical = 0,
  High,
  Normal,
  Low,
  Count
};

enum class ECrowdWorkerQueueResult : uint8
{
  Added = 0,
  MergedDuplicate,
  Replaced,
  RejectedInvalid,
  RejectedCapacity,
  RejectedStale,
  Conflict
};

enum class ECrowdWorkerRuntimeV2Failure : uint8
{
  None = 0,
  InvalidConfiguration,
  WorkQueue,
  WakeupQueue,
  DependencyIndex,
  DirtyState,
  OrderedEventCapacity,
  OrderedEventSequence,
  ResourceValidation,
  DomainRegistration,
  DomainExecution,
  SpatialIndex,
  Publication,
  CoverageAudit
};

enum class ECrowdWorkerRuntimeV2Mode : uint8
{
  Disabled = 0,
  Shadow,
  Production
};

struct MASSCROWDRUNTIME_API FCrowdWorkerRuntimeV2Config
{
  int32 MaxWorkItems = 80000;
  int32 MaxWakeups = 40000;
  int32 MaxDependencyEdges = 320000;
  int32 MaxDirtyEntities = 16000;
  int32 MaxOrderedEvents = 64000;
  int32 MaxPropagationRoundsPerEpoch = 8;
  int32 ShardEntityCount = 64;
  ECrowdWorkerRuntimeV2Mode Mode =
    ECrowdWorkerRuntimeV2Mode::Disabled;
  // Deprecated compatibility switch. When true and Mode is Disabled,
  // the runtime behaves as Shadow.
  bool bEnableSyntheticShadow = false;

  bool IsValid() const;
  ECrowdWorkerRuntimeV2Mode GetEffectiveMode() const
  {
    return Mode == ECrowdWorkerRuntimeV2Mode::Disabled
        && bEnableSyntheticShadow
      ? ECrowdWorkerRuntimeV2Mode::Shadow
      : Mode;
  }
  static FCrowdWorkerRuntimeV2Config MakeProduction10k();
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWorkKey
{
  ECrowdWorkerDomainId Domain =
    ECrowdWorkerDomainId::LifecycleInput;
  ECrowdWorkerWorkKind Kind = ECrowdWorkerWorkKind::Entity;
  FCrowdStableEntityRef PrimaryEntity;
  FCrowdStableEntityRef SecondaryEntity;
  uint64 ScopeKey = 0;

  bool operator==(const FCrowdWorkerWorkKey& Other) const = default;
  bool operator<(const FCrowdWorkerWorkKey& Other) const;

  friend uint32 GetTypeHash(const FCrowdWorkerWorkKey& Key)
  {
    uint32 Hash = HashCombineFast(
      ::GetTypeHash(static_cast<uint8>(Key.Domain)),
      ::GetTypeHash(static_cast<uint8>(Key.Kind)));
    Hash = HashCombineFast(
      Hash, CrowdWorkerRuntimeV2HashEntityRef(Key.PrimaryEntity));
    Hash = HashCombineFast(
      Hash, CrowdWorkerRuntimeV2HashEntityRef(Key.SecondaryEntity));
    return HashCombineFast(Hash, ::GetTypeHash(Key.ScopeKey));
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWorkItem
{
  FCrowdWorkerWorkKey Key;
  ECrowdWorkerWorkPriority Priority =
    ECrowdWorkerWorkPriority::Normal;
  uint64 EnqueueEpoch = 0;
  uint64 CorrectionRevision = 0;
  uint64 ReasonMask = 0;

  bool IsValid() const;
  void NormalizePair();
  bool operator==(const FCrowdWorkerWorkItem& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWorkRingStats
{
  int32 CurrentDepth = 0;
  int32 NextDepth = 0;
  int32 HighWatermark = 0;
  uint64 DuplicateMergeCount = 0;
  uint64 CapacityRejectCount = 0;
  uint64 DeferredPropagationCount = 0;
  uint64 PopBucketProbeCount = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWorkRingSnapshot
{
  uint64 Epoch = 0;
  uint8 FairDomainCursor = 0;
  TArray<FCrowdWorkerWorkItem> CurrentItems;
  TArray<FCrowdWorkerWorkItem> NextItems;
};

class MASSCROWDRUNTIME_API FCrowdWorkerWorkRing
{
public:
  bool Reset(int32 InMaxWorkItems, uint64 InitialEpoch = 0);
  ECrowdWorkerQueueResult EnqueueCurrent(
    FCrowdWorkerWorkItem Item);
  ECrowdWorkerQueueResult EnqueueNext(
    FCrowdWorkerWorkItem Item);
  bool PopCurrent(FCrowdWorkerWorkItem& OutItem);
  void AdvanceEpoch();
  void DeferCurrentToNext();
  int32 InvalidateEntityRevision(
    const FCrowdStableEntityRef& EntityRef,
    uint64 MinimumCorrectionRevision);
  int32 RemoveEntity(const FCrowdStableEntityRef& EntityRef);
  void GetSnapshot(FCrowdWorkerWorkRingSnapshot& OutSnapshot) const;
  bool RestoreSnapshot(const FCrowdWorkerWorkRingSnapshot& Snapshot);
  bool IsCurrentEmpty() const { return CurrentQueue.Count == 0; }
  uint64 GetEpoch() const { return Epoch; }
  FCrowdWorkerWorkRingStats GetStats() const;

private:
  struct FBucketLocation
  {
    int32 BucketIndex = INDEX_NONE;
    int32 ItemIndex = INDEX_NONE;
  };

  struct FWorkBucket
  {
    TArray<FCrowdWorkerWorkItem> Items;
    int32 Cursor = 0;
    bool bSorted = true;
  };

  struct FWorkQueue
  {
    TArray<FWorkBucket> Buckets;
    TMap<FCrowdWorkerWorkKey, FBucketLocation> Indices;
    int32 Count = 0;
  };

  ECrowdWorkerQueueResult Enqueue(
    FCrowdWorkerWorkItem Item,
    FWorkQueue& Queue);
  static int32 BucketIndex(
    ECrowdWorkerWorkPriority Priority,
    ECrowdWorkerDomainId Domain);
  static void ResetQueue(FWorkQueue& Queue);
  static void PrepareBucket(
    FWorkQueue& Queue,
    int32 BucketIndexValue);
  static void AppendQueueItems(
    const FWorkQueue& Queue,
    TArray<FCrowdWorkerWorkItem>& OutItems);
  int32 RemoveMatching(
    FWorkQueue& Queue,
    TFunctionRef<bool(const FCrowdWorkerWorkItem&)> Predicate);

  int32 MaxWorkItems = 0;
  uint64 Epoch = 0;
  uint8 FairDomainCursor = 0;
  int32 HighWatermark = 0;
  uint64 DuplicateMergeCount = 0;
  uint64 CapacityRejectCount = 0;
  uint64 DeferredPropagationCount = 0;
  uint64 PopBucketProbeCount = 0;
  FWorkQueue CurrentQueue;
  FWorkQueue NextQueue;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWakeupKey
{
  ECrowdWorkerDomainId Domain =
    ECrowdWorkerDomainId::LifecycleInput;
  FCrowdStableEntityRef EntityRef;
  uint64 WakeupId = 0;

  bool operator==(const FCrowdWorkerWakeupKey& Other) const = default;
  bool operator<(const FCrowdWorkerWakeupKey& Other) const;

  friend uint32 GetTypeHash(const FCrowdWorkerWakeupKey& Key)
  {
    return HashCombineFast(
      HashCombineFast(
        ::GetTypeHash(static_cast<uint8>(Key.Domain)),
        CrowdWorkerRuntimeV2HashEntityRef(Key.EntityRef)),
      ::GetTypeHash(Key.WakeupId));
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerWakeup
{
  FCrowdWorkerWakeupKey Key;
  uint64 AbsoluteSimulationTick = 0;
  uint64 Revision = 0;
  ECrowdWorkerWorkPriority Priority =
    ECrowdWorkerWorkPriority::Normal;
  uint64 ReasonMask = 0;

  bool IsValid() const;
};

class MASSCROWDRUNTIME_API FCrowdWorkerTimeWheel
{
public:
  bool Reset(int32 InMaxWakeups);
  ECrowdWorkerQueueResult Schedule(FCrowdWorkerWakeup Wakeup);
  bool Cancel(const FCrowdWorkerWakeupKey& Key);
  int32 CancelEntity(const FCrowdStableEntityRef& EntityRef);
  int32 InvalidateEntityRevision(
    const FCrowdStableEntityRef& EntityRef,
    uint64 MinimumRevision);
  int32 DrainDue(
    uint64 InclusiveSimulationTick,
    TArray<FCrowdWorkerWakeup>& OutWakeups);
  void GetScheduled(TArray<FCrowdWorkerWakeup>& OutWakeups) const;
  bool RestoreScheduled(TConstArrayView<FCrowdWorkerWakeup> Wakeups);
  int32 Num() const { return Scheduled.Num(); }
  int32 GetHighWatermark() const { return HighWatermark; }
  uint64 GetScannedBucketCount() const { return ScannedBucketCount; }

private:
  bool RemoveFromBucket(
    uint64 Tick,
    const FCrowdWorkerWakeupKey& Key);
  void PushTick(uint64 Tick);
  bool PopMinimumTick(uint64& OutTick);

  int32 MaxWakeups = 0;
  int32 HighWatermark = 0;
  uint64 ScannedBucketCount = 0;
  TMap<uint64, TArray<FCrowdWorkerWakeup>> Buckets;
  TMap<FCrowdWorkerWakeupKey, FCrowdWorkerWakeup> Scheduled;
  TArray<uint64> MinimumTickHeap;
};

enum class ECrowdWorkerDependencyKind : uint8
{
  Entity = 0,
  Resource,
  Cohort
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDependencyKey
{
  ECrowdWorkerDependencyKind Kind =
    ECrowdWorkerDependencyKind::Entity;
  FCrowdStableEntityRef EntityRef;
  uint64 ScopeKey = 0;

  bool IsValid() const;
  bool operator==(const FCrowdWorkerDependencyKey& Other) const = default;
  bool operator<(const FCrowdWorkerDependencyKey& Other) const;

  friend uint32 GetTypeHash(const FCrowdWorkerDependencyKey& Key)
  {
    return HashCombineFast(
      HashCombineFast(
        ::GetTypeHash(static_cast<uint8>(Key.Kind)),
        CrowdWorkerRuntimeV2HashEntityRef(Key.EntityRef)),
      ::GetTypeHash(Key.ScopeKey));
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDependencyRecord
{
  FCrowdWorkerDependencyKey Source;
  FCrowdWorkerWorkItem Dependent;

  bool operator==(
    const FCrowdWorkerDependencyRecord& Other) const = default;
};

struct FCrowdWorkerDependencyDeclaration;

class MASSCROWDRUNTIME_API FCrowdWorkerDependencyIndex
{
public:
  bool Reset(int32 InMaxEdges);
  ECrowdWorkerQueueResult AddDependency(
    const FCrowdWorkerDependencyKey& Source,
    FCrowdWorkerWorkItem Dependent);
  ECrowdWorkerQueueResult ReplaceDependenciesForDependents(
    TConstArrayView<FCrowdWorkerDependencyDeclaration>
      Declarations);
  int32 CollectDependents(
    const FCrowdWorkerDependencyKey& Source,
    TArray<FCrowdWorkerWorkItem>& OutItems) const;
  bool ContainsDependency(
    const FCrowdWorkerDependencyKey& Source,
    const FCrowdWorkerWorkKey& Dependent) const;
  int32 RemoveDependent(const FCrowdWorkerWorkKey& Dependent);
  int32 RemoveEntity(const FCrowdStableEntityRef& EntityRef);
  void GetRecords(
    TArray<FCrowdWorkerDependencyRecord>& OutRecords) const;
  bool RestoreRecords(
    TConstArrayView<FCrowdWorkerDependencyRecord> Records);
  bool ValidateInvariants() const;
  int32 NumEdges() const { return EdgeCount; }
  int32 GetHighWatermark() const { return HighWatermark; }

private:
  int32 MaxEdges = 0;
  int32 EdgeCount = 0;
  int32 HighWatermark = 0;
  TMap<
    FCrowdWorkerDependencyKey,
    TMap<FCrowdWorkerWorkKey, FCrowdWorkerWorkItem>> ForwardEdges;
  TMap<
    FCrowdWorkerWorkKey,
    TSet<FCrowdWorkerDependencyKey>> ReverseEdges;
};

MASSCROWDRUNTIME_API ECrowdWorkerQueueResult
CrowdWorkerRuntimeV2EnqueueResourceDependents(
  uint64 ResourceId,
  uint64 WorkerEpoch,
  const FCrowdWorkerDependencyIndex& DependencyIndex,
  FCrowdWorkerWorkRing& WorkRing,
  int32& OutDependentCount);

struct MASSCROWDRUNTIME_API FCrowdWorkerResourceRevisionEvent
{
  uint64 ResourceId = 0;
  uint64 PreviousRevision = 0;
  uint64 CurrentRevision = 0;
  uint64 AppliedEpoch = 0;
  uint64 PayloadStableHash = 0;

  bool operator==(const FCrowdWorkerResourceRevisionEvent& Other)
    const = default;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerResourceRecord
{
  uint64 ResourceId = 0;
  uint64 Revision = 0;
  FCrowdWorkerPayload Payload;
};

class MASSCROWDRUNTIME_API FCrowdWorkerResourceStore
{
public:
  bool Reset(int32 InMaxPayloadBytes);
  ECrowdWorkerQueueResult StageBuilding(
    FCrowdWorkerResourceRecord Record);
  bool CommitBuildingAtEpoch(
    uint64 Epoch,
    TArray<FCrowdWorkerResourceRevisionEvent>& OutEvents);
  const FCrowdWorkerResourceRecord* FindCurrent(
    uint64 ResourceId) const;
  void GetCurrentRecords(
    TArray<FCrowdWorkerResourceRecord>& OutRecords) const;
  bool RestoreCurrentRecords(
    TConstArrayView<FCrowdWorkerResourceRecord> Records);
  bool ApplyAuthoritativeRecords(
    TConstArrayView<FCrowdWorkerResourceRecord> Records);
  bool HasBuildingRevision() const { return !Building.IsEmpty(); }
  uint64 CalculateCurrentStableHash() const;

private:
  int32 MaxPayloadBytes = 0;
  TMap<uint64, FCrowdWorkerResourceRecord> Current;
  TMap<uint64, FCrowdWorkerResourceRecord> Building;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDirtyStateRecord
{
  FCrowdStableEntityRef EntityRef;
  ECrowdWorkerField Field = ECrowdWorkerField::Presentation;
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 StateRevision = 0;
  uint64 CorrectionRevision = 0;
  uint64 SourceInputSequence = 0;
  FCrowdWorkerPayload Payload;

  bool IsValid(int32 MaxPayloadBytes) const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDirtyStateKey
{
  FCrowdStableEntityRef EntityRef;
  ECrowdWorkerField Field = ECrowdWorkerField::Presentation;

  bool operator==(const FCrowdWorkerDirtyStateKey& Other) const = default;
  bool operator<(const FCrowdWorkerDirtyStateKey& Other) const;

  friend uint32 GetTypeHash(const FCrowdWorkerDirtyStateKey& Key)
  {
    return HashCombineFast(
      CrowdWorkerRuntimeV2HashEntityRef(Key.EntityRef),
      ::GetTypeHash(static_cast<uint8>(Key.Field)));
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerLifecycleWatermark
{
  uint32 ProviderId = 0;
  uint64 StableEntityId = 0;
  uint32 LastLifecycleSerial = 0;
};

class MASSCROWDRUNTIME_API FCrowdWorkerDirtyStateStore
{
public:
  bool Reset(int32 InMaxDirtyEntities, int32 InMaxPayloadBytes);
  ECrowdWorkerQueueResult MarkDirty(
    FCrowdWorkerDirtyStateRecord Record);
  int32 Drain(TArray<FCrowdWorkerDirtyStateRecord>& OutRecords);
  int32 InvalidateEntityRevision(
    const FCrowdStableEntityRef& EntityRef,
    uint64 MinimumCorrectionRevision);
  int32 RemoveEntity(const FCrowdStableEntityRef& EntityRef);
  const FCrowdWorkerDirtyStateRecord* Find(
    const FCrowdStableEntityRef& EntityRef,
    ECrowdWorkerField Field) const;
  int32 NumEntities() const { return DirtyEntities.Num(); }
  int32 NumFields() const { return Records.Num(); }
  int32 GetHighWatermark() const { return HighWatermark; }

private:
  int32 MaxDirtyEntities = 0;
  int32 MaxPayloadBytes = 0;
  int32 HighWatermark = 0;
  TSet<FCrowdStableEntityRef> DirtyEntities;
  TMap<FCrowdWorkerDirtyStateKey, FCrowdWorkerDirtyStateRecord>
    Records;
};

class MASSCROWDRUNTIME_API FCrowdWorkerEntityStateStore
{
public:
  bool Reset(int32 InMaxEntities, int32 InMaxPayloadBytes);
  ECrowdWorkerQueueResult Spawn(
    const FCrowdStableEntityRef& EntityRef,
    uint64 Generation,
    uint64 SourceInputSequence,
    FCrowdWorkerPayload InitialState);
  bool Despawn(const FCrowdStableEntityRef& EntityRef);
  ECrowdWorkerQueueResult ApplyInputState(
    const FCrowdStableEntityRef& EntityRef,
    uint64 Generation,
    uint64 SourceInputSequence,
    FCrowdWorkerPayload State);
  ECrowdWorkerQueueResult ApplyDirty(
    const FCrowdWorkerDirtyStateRecord& Record);
  bool ApplyAuthoritativeDirty(
    const FCrowdWorkerDirtyStateRecord& Record);
  bool RemoveAuthoritativeField(
    const FCrowdStableEntityRef& EntityRef,
    ECrowdWorkerField Field);
  const FCrowdWorkerDirtyStateRecord* Find(
    const FCrowdStableEntityRef& EntityRef,
    ECrowdWorkerField Field) const;
  bool Contains(const FCrowdStableEntityRef& EntityRef) const;
  void GetEntities(
    TArray<FCrowdStableEntityRef>& OutEntities) const;
  void GetStateRecords(
    TArray<FCrowdWorkerDirtyStateRecord>& OutRecords) const;
  bool RestoreStateRecords(
    TConstArrayView<FCrowdWorkerDirtyStateRecord> Records);
  void GetLifecycleWatermarks(
    TArray<FCrowdWorkerLifecycleWatermark>& OutWatermarks) const;
  bool RestoreLifecycleWatermarks(
    TConstArrayView<FCrowdWorkerLifecycleWatermark> Watermarks);
  int32 NumEntities() const { return Entities.Num(); }
  uint64 CalculateStableHash() const;

private:
  struct FLogicalEntityKey
  {
    uint32 ProviderId = 0;
    uint64 StableEntityId = 0;

    bool operator==(const FLogicalEntityKey& Other) const = default;

    friend uint32 GetTypeHash(const FLogicalEntityKey& Key)
    {
      return HashCombineFast(
        ::GetTypeHash(Key.ProviderId),
        ::GetTypeHash(Key.StableEntityId));
    }
  };

  static FLogicalEntityKey MakeLogicalKey(
    const FCrowdStableEntityRef& EntityRef)
  {
    return {
      EntityRef.ProviderId,
      EntityRef.StableEntityId};
  }

  int32 MaxEntities = 0;
  int32 MaxPayloadBytes = 0;
  TSet<FCrowdStableEntityRef> Entities;
  TMap<FLogicalEntityKey, FCrowdStableEntityRef>
    ActiveEntitiesByLogicalKey;
  TMap<FLogicalEntityKey, uint32> LatestLifecycleSerials;
  TMap<FCrowdWorkerDirtyStateKey, FCrowdWorkerDirtyStateRecord>
    Fields;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerCommandRecord
{
  uint64 InputSequence = 0;
  FCrowdStableEntityRef EntityRef;
  uint32 CommandId = 0;
  double EffectiveSimulationTimeSeconds = 0.0;
  FCrowdWorkerPayload Payload;

  bool IsValid(int32 MaxPayloadBytes) const;
  bool operator==(const FCrowdWorkerCommandRecord& Other) const = default;
};

class MASSCROWDRUNTIME_API FCrowdWorkerCommandStore
{
public:
  bool Reset(int32 InMaxCommands, int32 InMaxPayloadBytes);
  ECrowdWorkerQueueResult Enqueue(
    const FCrowdWorkerCommandDelta& Command);
  int32 CollectEntity(
    const FCrowdStableEntityRef& EntityRef,
    double InclusiveSimulationTimeSeconds,
    TArray<FCrowdWorkerCommandRecord>& OutCommands) const;
  bool Acknowledge(
    const FCrowdStableEntityRef& EntityRef,
    uint64 InputSequence);
  bool Acknowledge(uint64 InputSequence);
  int32 RemoveEntity(const FCrowdStableEntityRef& EntityRef);
  void GetRecords(TArray<FCrowdWorkerCommandRecord>& OutRecords) const;
  bool RestoreRecords(
    TConstArrayView<FCrowdWorkerCommandRecord> InRecords);
  int32 Num() const { return Records.Num(); }
  int32 GetHighWatermark() const { return HighWatermark; }
  uint64 CalculateStableHash() const;

private:
  int32 MaxCommands = 0;
  int32 MaxPayloadBytes = 0;
  int32 HighWatermark = 0;
  TMap<uint64, FCrowdWorkerCommandRecord> Records;
};

class MASSCROWDRUNTIME_API FCrowdWorkerOrderedEventStore
{
public:
  bool Reset(
    int32 InMaxEvents,
    int32 InMaxPayloadBytes,
    uint64 InGeneration,
    uint64 InNextEventSequence = 1);
  ECrowdWorkerQueueResult Append(
    FCrowdWorkerGameplayEvent Event);
  int32 Drain(TArray<FCrowdWorkerGameplayEvent>& OutEvents);
  int32 Num() const { return Events.Num(); }
  int32 GetHighWatermark() const { return HighWatermark; }
  uint64 GetLastAcceptedEventSequence() const
  {
    return LastAcceptedEventSequence;
  }

private:
  int32 MaxEvents = 0;
  int32 MaxPayloadBytes = 0;
  int32 HighWatermark = 0;
  uint64 Generation = 0;
  uint64 LastAcceptedEventSequence = 0;
  TArray<FCrowdWorkerGameplayEvent> Events;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerCheckpoint
{
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 AbsoluteSimulationTick = 0;
  double FixedSimulationQuantumSeconds = 0.0;
  uint64 LastAppliedInputSequence = 0;
  uint64 LastOrderedEventSequence = 0;
  uint64 EntityStateHash = 0;
  uint64 ResourceRevisionHash = 0;
  uint64 StableHash = 0;

  uint64 CalculateStableHash() const;
  void RecalculateStableHash();
  bool IsValid() const;
};

class FCrowdWorkerSpatialIndex;

struct MASSCROWDRUNTIME_API FCrowdWorkerDomainContext
{
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 AbsoluteSimulationTick = 0;
  uint64 CorrectionRevision = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 NextOrderedEventSequence = 1;
  uint64 ResourceRevisionHash = 0;
  int32 PropagationRound = 0;
  double FixedDeltaSeconds = 0.0;
  double SimulationTimeSeconds = 0.0;
  ECrowdWorkerRuntimeV2Mode RuntimeMode =
    ECrowdWorkerRuntimeV2Mode::Disabled;
  const FCrowdWorkerEntityStateStore* EntityStates = nullptr;
  const FCrowdWorkerCommandStore* Commands = nullptr;
  const FCrowdWorkerResourceStore* Resources = nullptr;
  const FCrowdWorkerSpatialIndex* SpatialIndex = nullptr;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDependencyObservation
{
  FCrowdWorkerDependencyKey Source;
  FCrowdWorkerWorkKey Dependent;

  bool IsValid() const
  {
    return Source.IsValid()
      && Dependent.Domain < ECrowdWorkerDomainId::Count;
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDependencyDeclaration
{
  FCrowdWorkerDependencyKey Source;
  FCrowdWorkerWorkItem Dependent;

  bool IsValid() const
  {
    return Source.IsValid() && Dependent.IsValid();
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDomainOutput
{
  TArray<FCrowdWorkerWorkItem> NextWork;
  TArray<FCrowdWorkerWakeup> Wakeups;
  TArray<FCrowdWorkerDirtyStateRecord> DirtyStates;
  TArray<FCrowdWorkerGameplayEvent> OrderedEvents;
  TArray<FCrowdWorkerDependencyDeclaration>
    DeclaredDependencies;
  TArray<FCrowdWorkerDependencyObservation> ObservedDependencies;
  TArray<uint64> ConsumedCommandInputSequences;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDomainShard
{
  ECrowdWorkerDomainId Domain =
    ECrowdWorkerDomainId::LifecycleInput;
  uint32 ShardOrdinal = 0;
  TArray<FCrowdWorkerWorkItem> WorkItems;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDomainShardResult
{
  ECrowdWorkerDomainId Domain =
    ECrowdWorkerDomainId::LifecycleInput;
  uint32 ShardOrdinal = 0;
  FCrowdWorkerDomainOutput Output;
  bool bSucceeded = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerDeterministicShardPlanner
{
public:
  static bool Build(
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    int32 ShardEntityCount,
    TArray<FCrowdWorkerDomainShard>& OutShards);
  static bool Merge(
    TConstArrayView<FCrowdWorkerDomainShardResult> Results,
    int32 MaxDirtyEntities,
    int32 MaxPayloadBytes,
    int32 MaxOrderedEvents,
    FCrowdWorkerDomainOutput& OutMerged,
    uint64 FirstOrderedEventSequence = 1);
  static uint64 CalculateStableHash(
    const FCrowdWorkerDomainOutput& Output);
};

class MASSCROWDRUNTIME_API ICrowdWorkerDomainExecutor
{
public:
  virtual ~ICrowdWorkerDomainExecutor() = default;
  virtual ECrowdWorkerDomainId GetDomainId() const = 0;
  virtual void GetDependencies(
    TArray<ECrowdWorkerDomainId>& OutDependencies) const = 0;
  // Execute may run concurrently for multiple immutable shards of the
  // same domain. Implementations must keep mutable epoch state in
  // shard-local output slots and merge it only at the Owner barrier.
  virtual bool Execute(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput) = 0;
  // Sparse correction replaces the authoritative entity-state records at a
  // clean Owner barrier. Stateful domains must rebase any private retained
  // continuation state from those records before prediction resumes.
  virtual bool ApplyAuthorityCorrection(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerDirtyStateRecord> Records)
  {
    return true;
  }
};

class MASSCROWDRUNTIME_API FCrowdWorkerDomainRegistry
{
public:
  FCrowdWorkerDomainRegistry() = default;
  FCrowdWorkerDomainRegistry(
    const FCrowdWorkerDomainRegistry&) = delete;
  FCrowdWorkerDomainRegistry& operator=(
    const FCrowdWorkerDomainRegistry&) = delete;

  bool Register(TUniquePtr<ICrowdWorkerDomainExecutor> Executor);
  bool Freeze();
  bool IsFrozen() const { return bFrozen; }
  int32 Num() const { return Executors.Num(); }
  bool HasDomain(ECrowdWorkerDomainId Domain) const;
  bool ExecuteEpoch(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerWorkItem> WorkItems,
    FCrowdWorkerDomainOutput& OutOutput);
  bool ApplyAuthorityCorrection(
    const FCrowdWorkerDomainContext& Context,
    TConstArrayView<FCrowdWorkerDirtyStateRecord> Records);

private:
  bool bFrozen = false;
  TArray<TUniquePtr<ICrowdWorkerDomainExecutor>> Executors;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerRuntimeV2Metrics
{
  int32 WorkCurrentDepth = 0;
  int32 WorkNextDepth = 0;
  int32 WorkHighWatermark = 0;
  int32 WakeupDepth = 0;
  int32 WakeupHighWatermark = 0;
  uint64 TimeWheelScannedBucketCount = 0;
  uint64 WorkPopBucketProbeCount = 0;
  int32 DependencyEdgeCount = 0;
  int32 DependencyHighWatermark = 0;
  int32 DirtyEntityCount = 0;
  int32 DirtyHighWatermark = 0;
  int32 EntityStateCount = 0;
  int32 PendingCommandCount = 0;
  int32 CommandHighWatermark = 0;
  int32 SleepingEntityCount = 0;
  int32 SpatialEntityCount = 0;
  uint64 SpatialFullRebuildCount = 0;
  uint64 SpatialIncrementalUpdateCount = 0;
  uint64 SpatialCellMigrationCount = 0;
  uint64 PropagationRoundCount = 0;
  uint64 PropagationLimitHitCount = 0;
  uint64 ShardDispatchCount = 0;
  uint64 ShardCompletionCount = 0;
  uint64 ShardMergeCount = 0;
  int32 ShardInFlightCount = 0;
  int32 ShardInFlightHighWatermark = 0;
  uint64 WorkCapacityRejectCount = 0;
  uint64 OrderedEventLossCount = 0;
  int32 OrderedEventDepth = 0;
  int32 OrderedEventHighWatermark = 0;
  uint64 LastOrderedEventSequence = 0;
  uint64 ResourceRevisionHash = 0;
  uint64 EntityStateHash = 0;
  uint64 PublishedDirtyStateCount = 0;
  uint64 PublishedOrderedEventCount = 0;
  uint64 CoverageAuditFailureCount = 0;
  uint64 ShadowBaselineRebaseCount = 0;
  ECrowdWorkerRuntimeV2Failure LastFailure =
    ECrowdWorkerRuntimeV2Failure::None;
};
