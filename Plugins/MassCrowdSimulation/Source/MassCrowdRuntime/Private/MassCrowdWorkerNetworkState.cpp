#include "MassCrowdWorkerNetworkState.h"

namespace CrowdWorkerNetworkStatePrivate
{
struct FNetworkLogicalEntityKey
{
  uint32 ProviderId = 0;
  uint64 StableEntityId = 0;
  bool operator==(const FNetworkLogicalEntityKey& Other) const = default;
  friend uint32 GetTypeHash(const FNetworkLogicalEntityKey& Key)
  {
    return HashCombineFast(
      ::GetTypeHash(Key.ProviderId),
      ::GetTypeHash(Key.StableEntityId));
  }
};

constexpr uint64 FnvOffset = 14695981039346656037ull;
constexpr uint64 FnvPrime = 1099511628211ull;

template <typename T>
void Fold(uint64& Hash, const T Value)
{
  using TUnsigned = std::make_unsigned_t<T>;
  TUnsigned Unsigned = static_cast<TUnsigned>(Value);
  for (uint32 ByteIndex = 0; ByteIndex < sizeof(TUnsigned); ++ByteIndex)
  {
    Hash ^= static_cast<uint8>(Unsigned >> (ByteIndex * 8));
    Hash *= FnvPrime;
  }
}

void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
{
  Fold(Hash, Ref.ProviderId);
  Fold(Hash, Ref.StableEntityId);
  Fold(Hash, Ref.LifecycleSerial);
}

void FoldV2(uint64& Hash, const uint64 Value)
{
  for (uint32 ByteIndex = 0; ByteIndex < sizeof(Value); ++ByteIndex)
  {
    Hash ^= static_cast<uint8>(Value >> (ByteIndex * 8));
    Hash *= FnvPrime;
  }
}

void FoldV2Ref(uint64& Hash, const FCrowdStableEntityRef& Ref)
{
  FoldV2(Hash, Ref.ProviderId);
  FoldV2(Hash, Ref.StableEntityId);
  FoldV2(Hash, Ref.LifecycleSerial);
}

void FoldState(uint64& Hash, const FCrowdWorkerDirtyStateRecord& State)
{
  FoldRef(Hash, State.EntityRef);
  Fold(Hash, static_cast<uint8>(State.Field));
  Fold(Hash, State.Generation);
  Fold(Hash, State.WorkerEpoch);
  Fold(Hash, State.StateRevision);
  Fold(Hash, State.CorrectionRevision);
  Fold(Hash, State.SourceInputSequence);
  Fold(Hash, State.Payload.SchemaId);
  Fold(Hash, State.Payload.SchemaVersion);
  Fold(Hash, State.Payload.StableHash);
}

void FoldResource(uint64& Hash, const FCrowdWorkerResourceRecord& Resource)
{
  Fold(Hash, Resource.ResourceId);
  Fold(Hash, Resource.Revision);
  Fold(Hash, Resource.Payload.SchemaId);
  Fold(Hash, Resource.Payload.SchemaVersion);
  Fold(Hash, Resource.Payload.StableHash);
}

void FoldEvent(uint64& Hash, const FCrowdWorkerGameplayEvent& Event)
{
  FoldRef(Hash, Event.EntityRef);
  Fold(Hash, Event.Generation);
  Fold(Hash, Event.WorkerEpoch);
  Fold(Hash, Event.SourceInputSequence);
  Fold(Hash, Event.EventSequence);
  Fold(Hash, Event.EventId);
  Fold(Hash, Event.Payload.SchemaId);
  Fold(Hash, Event.Payload.SchemaVersion);
  Fold(Hash, Event.Payload.StableHash);
  Fold(Hash, Event.StableHash);
}

void FoldWorkKey(uint64& Hash, const FCrowdWorkerWorkKey& Key)
{
  Fold(Hash, static_cast<uint8>(Key.Domain));
  Fold(Hash, static_cast<uint8>(Key.Kind));
  FoldRef(Hash, Key.PrimaryEntity);
  FoldRef(Hash, Key.SecondaryEntity);
  Fold(Hash, Key.ScopeKey);
}

void FoldWork(uint64& Hash, const FCrowdWorkerWorkItem& Work)
{
  FoldWorkKey(Hash, Work.Key);
  Fold(Hash, static_cast<uint8>(Work.Priority));
  Fold(Hash, Work.EnqueueEpoch);
  Fold(Hash, Work.CorrectionRevision);
  Fold(Hash, Work.ReasonMask);
}

void FoldWakeup(uint64& Hash, const FCrowdWorkerWakeup& Wakeup)
{
  Fold(Hash, static_cast<uint8>(Wakeup.Key.Domain));
  FoldRef(Hash, Wakeup.Key.EntityRef);
  Fold(Hash, Wakeup.Key.WakeupId);
  Fold(Hash, Wakeup.AbsoluteSimulationTick);
  Fold(Hash, Wakeup.Revision);
  Fold(Hash, static_cast<uint8>(Wakeup.Priority));
  Fold(Hash, Wakeup.ReasonMask);
}

void FoldDependency(
  uint64& Hash,
  const FCrowdWorkerDependencyRecord& Dependency)
{
  Fold(Hash, static_cast<uint8>(Dependency.Source.Kind));
  FoldRef(Hash, Dependency.Source.EntityRef);
  Fold(Hash, Dependency.Source.ScopeKey);
  FoldWork(Hash, Dependency.Dependent);
}

void FoldCommand(uint64& Hash, const FCrowdWorkerCommandRecord& Command)
{
  Fold(Hash, Command.InputSequence);
  FoldRef(Hash, Command.EntityRef);
  Fold(Hash, Command.CommandId);
  uint64 TimeBits = 0;
  static_assert(sizeof(TimeBits)
    == sizeof(Command.EffectiveSimulationTimeSeconds));
  FMemory::Memcpy(
    &TimeBits,
    &Command.EffectiveSimulationTimeSeconds,
    sizeof(TimeBits));
  Fold(Hash, TimeBits);
  Fold(Hash, Command.Payload.SchemaId);
  Fold(Hash, Command.Payload.SchemaVersion);
  Fold(Hash, Command.Payload.StableHash);
}

void FoldSpawn(uint64& Hash, const FCrowdWorkerSpawnDelta& Spawn)
{
  Fold(Hash, Spawn.InputSequence);
  FoldRef(Hash, Spawn.EntityRef);
  Fold(Hash, Spawn.InitialState.SchemaId);
  Fold(Hash, Spawn.InitialState.SchemaVersion);
  Fold(Hash, Spawn.InitialState.StableHash);
}

void FoldDespawn(uint64& Hash, const FCrowdWorkerDespawnDelta& Despawn)
{
  Fold(Hash, Despawn.InputSequence);
  FoldRef(Hash, Despawn.EntityRef);
  Fold(Hash, Despawn.ReasonId);
}

void FoldContinuation(
  uint64& Hash,
  const FCrowdWorkerNetworkContinuationState& Continuation)
{
  Fold(Hash, Continuation.WorkRing.Epoch);
  Fold(Hash, Continuation.WorkRing.FairDomainCursor);
  Fold(Hash, static_cast<uint32>(
    Continuation.WorkRing.CurrentItems.Num()));
  for (const FCrowdWorkerWorkItem& Work :
    Continuation.WorkRing.CurrentItems)
    FoldWork(Hash, Work);
  Fold(Hash, static_cast<uint32>(
    Continuation.WorkRing.NextItems.Num()));
  for (const FCrowdWorkerWorkItem& Work :
    Continuation.WorkRing.NextItems)
    FoldWork(Hash, Work);
  Fold(Hash, static_cast<uint32>(Continuation.Wakeups.Num()));
  for (const FCrowdWorkerWakeup& Wakeup : Continuation.Wakeups)
    FoldWakeup(Hash, Wakeup);
  Fold(Hash, static_cast<uint32>(Continuation.Dependencies.Num()));
  for (const FCrowdWorkerDependencyRecord& Dependency :
    Continuation.Dependencies)
    FoldDependency(Hash, Dependency);
  Fold(Hash, static_cast<uint32>(Continuation.Commands.Num()));
  for (const FCrowdWorkerCommandRecord& Command :
    Continuation.Commands)
    FoldCommand(Hash, Command);
  Fold(Hash, static_cast<uint32>(
    Continuation.LifecycleWatermarks.Num()));
  for (const FCrowdWorkerLifecycleWatermark& Watermark :
    Continuation.LifecycleWatermarks)
  {
    Fold(Hash, Watermark.ProviderId);
    Fold(Hash, Watermark.StableEntityId);
    Fold(Hash, Watermark.LastLifecycleSerial);
  }
}

bool StateLess(
  const FCrowdWorkerDirtyStateRecord& A,
  const FCrowdWorkerDirtyStateRecord& B)
{
  const FCrowdWorkerDirtyStateKey AKey{A.EntityRef, A.Field};
  const FCrowdWorkerDirtyStateKey BKey{B.EntityRef, B.Field};
  return AKey < BKey;
}

bool IsSortedUniqueStates(
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> States,
  const int32 MaxPayloadBytes)
{
  for (int32 Index = 0; Index < States.Num(); ++Index)
  {
    if (!States[Index].IsValid(MaxPayloadBytes))
      return false;
    if (Index > 0 && !StateLess(States[Index - 1], States[Index]))
      return false;
  }
  return true;
}

bool IsSortedUniqueResources(
  const TConstArrayView<FCrowdWorkerResourceRecord> Resources,
  const int32 MaxPayloadBytes)
{
  for (int32 Index = 0; Index < Resources.Num(); ++Index)
  {
    if (Resources[Index].ResourceId == 0
      || Resources[Index].Revision == 0
      || !Resources[Index].Payload.IsValid(MaxPayloadBytes))
      return false;
    if (Index > 0
      && Resources[Index - 1].ResourceId >= Resources[Index].ResourceId)
      return false;
  }
  return true;
}

bool WorkLess(
  const FCrowdWorkerWorkItem& A,
  const FCrowdWorkerWorkItem& B)
{
  if (A.Priority != B.Priority) return A.Priority < B.Priority;
  return A.Key < B.Key;
}

bool IsSortedUniqueWork(
  const TConstArrayView<FCrowdWorkerWorkItem> Work,
  const uint64 ExpectedEpoch)
{
  TSet<FCrowdWorkerWorkKey> Keys;
  for (int32 Index = 0; Index < Work.Num(); ++Index)
  {
    if (!Work[Index].IsValid()
      || Work[Index].EnqueueEpoch != ExpectedEpoch
      || Keys.Contains(Work[Index].Key))
      return false;
    if (Index > 0
      && WorkLess(Work[Index], Work[Index - 1]))
      return false;
    Keys.Add(Work[Index].Key);
  }
  return true;
}

bool IsSortedUniqueWakeups(
  const TConstArrayView<FCrowdWorkerWakeup> Wakeups,
  const uint64 CompletedTick)
{
  TSet<FCrowdWorkerWakeupKey> Keys;
  for (int32 Index = 0; Index < Wakeups.Num(); ++Index)
  {
    const FCrowdWorkerWakeup& Wakeup = Wakeups[Index];
    if (!Wakeup.IsValid()
      || Wakeup.AbsoluteSimulationTick <= CompletedTick
      || Keys.Contains(Wakeup.Key))
      return false;
    if (Index > 0)
    {
      const FCrowdWorkerWakeup& Previous = Wakeups[Index - 1];
      if (Wakeup.AbsoluteSimulationTick
          < Previous.AbsoluteSimulationTick
        || (Wakeup.AbsoluteSimulationTick
            == Previous.AbsoluteSimulationTick
          && Wakeup.Key < Previous.Key))
        return false;
    }
    Keys.Add(Wakeup.Key);
  }
  return true;
}

bool IsSortedUniqueDependencies(
  const TConstArrayView<FCrowdWorkerDependencyRecord> Dependencies)
{
  for (int32 Index = 0; Index < Dependencies.Num(); ++Index)
  {
    const FCrowdWorkerDependencyRecord& Record = Dependencies[Index];
    if (!Record.Source.IsValid() || !Record.Dependent.IsValid())
      return false;
    if (Index == 0) continue;
    const FCrowdWorkerDependencyRecord& Previous =
      Dependencies[Index - 1];
    if (Record.Source < Previous.Source
      || (Record.Source == Previous.Source
        && (!WorkLess(Previous.Dependent, Record.Dependent)
          || Record.Dependent.Key == Previous.Dependent.Key)))
      return false;
  }
  return true;
}

bool IsSortedUniqueCommands(
  const TConstArrayView<FCrowdWorkerCommandRecord> Commands,
  const int32 MaxPayloadBytes)
{
  for (int32 Index = 0; Index < Commands.Num(); ++Index)
  {
    if (!Commands[Index].IsValid(MaxPayloadBytes)
      || (Index > 0
        && Commands[Index - 1].InputSequence
          >= Commands[Index].InputSequence))
      return false;
  }
  return true;
}

bool IsSortedUniqueLifecycle(
  const TConstArrayView<FCrowdWorkerSpawnDelta> Spawns,
  const TConstArrayView<FCrowdWorkerDespawnDelta> Despawns,
  const uint64 LastAppliedInputSequence,
  const int32 MaxPayloadBytes)
{
  TSet<uint64> InputSequences;
  uint64 PreviousSequence = 0;
  for (const FCrowdWorkerSpawnDelta& Spawn : Spawns)
  {
    if (!Spawn.IsValid(MaxPayloadBytes)
      || Spawn.InputSequence > LastAppliedInputSequence
      || Spawn.InputSequence <= PreviousSequence
      || InputSequences.Contains(Spawn.InputSequence))
      return false;
    PreviousSequence = Spawn.InputSequence;
    InputSequences.Add(Spawn.InputSequence);
  }
  PreviousSequence = 0;
  for (const FCrowdWorkerDespawnDelta& Despawn : Despawns)
  {
    if (!Despawn.IsValid()
      || Despawn.InputSequence > LastAppliedInputSequence
      || Despawn.InputSequence <= PreviousSequence
      || InputSequences.Contains(Despawn.InputSequence))
      return false;
    PreviousSequence = Despawn.InputSequence;
    InputSequences.Add(Despawn.InputSequence);
  }
  return true;
}

bool IsValidContinuation(
  const FCrowdWorkerNetworkContinuationState& Continuation,
  const uint64 CompletedEpoch,
  const uint64 CompletedTick,
  const FCrowdWorkerNetworkStateConfig& Config)
{
  bool bWatermarksValid =
    Continuation.LifecycleWatermarks.Num()
      <= Config.MaxLifecycleWatermarksPerCheckpoint;
  for (int32 Index = 0;
    bWatermarksValid && Index < Continuation.LifecycleWatermarks.Num();
    ++Index)
  {
    const FCrowdWorkerLifecycleWatermark& Watermark =
      Continuation.LifecycleWatermarks[Index];
    bWatermarksValid = Watermark.ProviderId != 0
      && Watermark.StableEntityId != 0
      && Watermark.LastLifecycleSerial != 0;
    if (bWatermarksValid && Index > 0)
    {
      const FCrowdWorkerLifecycleWatermark& Previous =
        Continuation.LifecycleWatermarks[Index - 1];
      bWatermarksValid = Previous.ProviderId < Watermark.ProviderId
        || (Previous.ProviderId == Watermark.ProviderId
          && Previous.StableEntityId < Watermark.StableEntityId);
    }
  }
  return bWatermarksValid
    && CompletedEpoch != MAX_uint64
    && Continuation.WorkRing.Epoch == CompletedEpoch + 1
    && Continuation.WorkRing.FairDomainCursor
      < static_cast<uint8>(ECrowdWorkerDomainId::Count)
    && Continuation.WorkRing.CurrentItems.Num()
        + Continuation.WorkRing.NextItems.Num()
      <= Config.MaxWorkItemsPerCheckpoint
    && Continuation.Wakeups.Num() <= Config.MaxWakeupsPerCheckpoint
    && Continuation.Dependencies.Num()
      <= Config.MaxDependencyEdgesPerCheckpoint
    && Continuation.Commands.Num() <= Config.MaxCommandsPerCheckpoint
    && IsSortedUniqueWork(
      Continuation.WorkRing.CurrentItems,
      Continuation.WorkRing.Epoch)
    && IsSortedUniqueWork(
      Continuation.WorkRing.NextItems,
      Continuation.WorkRing.Epoch + 1)
    && IsSortedUniqueWakeups(Continuation.Wakeups, CompletedTick)
    && IsSortedUniqueDependencies(Continuation.Dependencies)
    && IsSortedUniqueCommands(
      Continuation.Commands, Config.MaxPayloadBytes);
}

bool WatermarksCoverStates(
  const TConstArrayView<FCrowdWorkerLifecycleWatermark> Watermarks,
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> States)
{
  TMap<FNetworkLogicalEntityKey, uint32> LatestByLogicalEntity;
  for (const FCrowdWorkerLifecycleWatermark& Watermark : Watermarks)
    LatestByLogicalEntity.Add(
      {Watermark.ProviderId, Watermark.StableEntityId},
      Watermark.LastLifecycleSerial);
  TSet<FNetworkLogicalEntityKey> ActiveEntities;
  TSet<FNetworkLogicalEntityKey> InputSnapshots;
  for (const FCrowdWorkerDirtyStateRecord& State : States)
  {
    const FNetworkLogicalEntityKey Key{
      State.EntityRef.ProviderId, State.EntityRef.StableEntityId};
    const uint32* Latest = LatestByLogicalEntity.Find(
      Key);
    if (!Latest || *Latest != State.EntityRef.LifecycleSerial)
      return false;
    ActiveEntities.Add(Key);
    if (State.Field == ECrowdWorkerField::InputSnapshot)
      InputSnapshots.Add(Key);
  }
  return ActiveEntities.Num() == InputSnapshots.Num();
}

uint64 CalculateEntityStateHash(
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> States)
{
  uint64 Hash = FnvOffset;
  FoldV2(Hash, 1);
  for (const FCrowdWorkerDirtyStateRecord& State : States)
  {
    FoldV2Ref(Hash, State.EntityRef);
    FoldV2(Hash, static_cast<uint8>(State.Field));
    FoldV2(Hash, State.Generation);
    FoldV2(Hash, State.WorkerEpoch);
    FoldV2(Hash, State.StateRevision);
    FoldV2(Hash, State.CorrectionRevision);
    FoldV2(Hash, State.SourceInputSequence);
    FoldV2(Hash, State.Payload.StableHash);
  }
  return Hash;
}

uint64 CalculateResourceRevisionHash(
  const TConstArrayView<FCrowdWorkerResourceRecord> Resources)
{
  uint64 Hash = FnvOffset;
  FoldV2(Hash, 1);
  for (const FCrowdWorkerResourceRecord& Resource : Resources)
  {
    FoldV2(Hash, Resource.ResourceId);
    FoldV2(Hash, Resource.Revision);
    FoldV2(Hash, Resource.Payload.StableHash);
  }
  return Hash;
}
}

using namespace CrowdWorkerNetworkStatePrivate;

uint64 FCrowdWorkerAuthorityDigestBatch::CalculateStableHash() const
{
  uint64 Hash = FnvOffset;
  Fold(Hash, Version);
  Fold(Hash, Generation);
  Fold(Hash, DigestSequence);
  Fold(Hash, SimulationTick);
  Fold(Hash, ThroughInputSequence);
  Fold(Hash, static_cast<uint32>(Entries.Num()));
  for (const FCrowdWorkerAuthorityDigestEntry& Entry : Entries)
  {
    Fold(Hash, static_cast<uint8>(Entry.Scope.Field));
    Fold(Hash, static_cast<uint8>(Entry.Scope.Kind));
    Fold(Hash, Entry.Scope.ScopeId);
    Fold(Hash, Entry.SimulationTick);
    Fold(Hash, Entry.ThroughInputSequence);
    Fold(Hash, Entry.EntityCount);
    Fold(Hash, Entry.StableHash);
  }
  return Hash;
}

void FCrowdWorkerAuthorityDigestBatch::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerAuthorityDigestBatch::IsValid(
  const FCrowdWorkerNetworkStateConfig& Config) const
{
  if (!Config.IsValid()
    || Version != CurrentVersion
    || Generation == 0
    || DigestSequence == 0
    || SimulationTick == 0
    || Entries.Num() > Config.MaxDigestScopes
    || StableHash == 0
    || StableHash != CalculateStableHash())
    return false;
  for (int32 Index = 0; Index < Entries.Num(); ++Index)
  {
    const FCrowdWorkerAuthorityDigestEntry& Entry = Entries[Index];
    if (!Entry.Scope.IsValid()
      || Entry.SimulationTick != SimulationTick
      || Entry.ThroughInputSequence != ThroughInputSequence
      || Entry.StableHash == 0
      || (Index > 0
        && !(Entries[Index - 1].Scope < Entry.Scope)))
      return false;
  }
  return true;
}

uint64 FCrowdWorkerAuthorityCorrectionBatch::CalculateStableHash() const
{
  uint64 Hash = FnvOffset;
  Fold(Hash, Version);
  Fold(Hash, Generation);
  Fold(Hash, CorrectionSequence);
  Fold(Hash, ApplySimulationTick);
  Fold(Hash, ThroughInputSequence);
  Fold(Hash, static_cast<uint32>(Scopes.Num()));
  for (const FCrowdWorkerAuthorityScopeKey& Scope : Scopes)
  {
    Fold(Hash, static_cast<uint8>(Scope.Field));
    Fold(Hash, static_cast<uint8>(Scope.Kind));
    Fold(Hash, Scope.ScopeId);
  }
  Fold(Hash, static_cast<uint32>(AuthoritativeMembers.Num()));
  for (const FCrowdStableEntityRef& Ref : AuthoritativeMembers)
    FoldRef(Hash, Ref);
  Fold(Hash, static_cast<uint32>(Records.Num()));
  for (const FCrowdWorkerDirtyStateRecord& Record : Records)
    FoldState(Hash, Record);
  Fold(Hash, static_cast<uint32>(Tombstones.Num()));
  for (const FCrowdWorkerAuthorityTombstone& Tombstone : Tombstones)
  {
    FoldRef(Hash, Tombstone.EntityRef);
    Fold(Hash, static_cast<uint8>(Tombstone.Field));
  }
  return Hash;
}

void FCrowdWorkerAuthorityCorrectionBatch::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerAuthorityCorrectionBatch::IsValid(
  const FCrowdWorkerNetworkStateConfig& Config) const
{
  if (!Config.IsValid()
    || Version != CurrentVersion
    || Generation == 0
    || CorrectionSequence == 0
    || ApplySimulationTick == 0
    || Scopes.IsEmpty()
    || Scopes.Num() > Config.MaxCorrectionScopes
    || AuthoritativeMembers.Num() > Config.MaxCorrectionEntities
    || Records.Num() > Config.MaxCorrectionEntities
    || Tombstones.Num() > Config.MaxCorrectionEntities
    || StableHash == 0
    || StableHash != CalculateStableHash())
    return false;
  for (int32 Index = 0; Index < Scopes.Num(); ++Index)
    if (!Scopes[Index].IsValid()
      || (Index > 0 && !(Scopes[Index - 1] < Scopes[Index])))
      return false;
  for (int32 Index = 0; Index < AuthoritativeMembers.Num(); ++Index)
    if (!AuthoritativeMembers[Index].IsValid()
      || (Index > 0
        && !(AuthoritativeMembers[Index - 1]
          < AuthoritativeMembers[Index])))
      return false;
  if (!IsSortedUniqueStates(Records, Config.MaxPayloadBytes))
    return false;
  for (const FCrowdWorkerDirtyStateRecord& Record : Records)
    if (Record.Generation != Generation) return false;
  for (int32 Index = 0; Index < Tombstones.Num(); ++Index)
  {
    const FCrowdWorkerAuthorityTombstone& Value = Tombstones[Index];
    if (!Value.EntityRef.IsValid()
      || Value.Field >= ECrowdWorkerField::Count)
      return false;
    if (Index > 0)
    {
      const FCrowdWorkerAuthorityTombstone& Previous =
        Tombstones[Index - 1];
      if (Value.EntityRef < Previous.EntityRef
        || (Value.EntityRef == Previous.EntityRef
          && static_cast<uint8>(Value.Field)
            <= static_cast<uint8>(Previous.Field)))
        return false;
    }
  }
  return true;
}

uint64 FCrowdWorkerNetworkCheckpoint::CalculateStableHash() const
{
  uint64 Hash = FnvOffset;
  Fold(Hash, Version);
  Fold(Hash, Header.StableHash);
  Fold(Hash, InputBaselineSequence);
  Fold(Hash, EventBaselineSequence);
  Fold(Hash, MaxCorrectionRevision);
  Fold(Hash, static_cast<uint32>(StateRecords.Num()));
  for (const FCrowdWorkerDirtyStateRecord& State : StateRecords)
    FoldState(Hash, State);
  Fold(Hash, static_cast<uint32>(ResourceRecords.Num()));
  for (const FCrowdWorkerResourceRecord& Resource : ResourceRecords)
    FoldResource(Hash, Resource);
  FoldContinuation(Hash, Continuation);
  return Hash;
}

void FCrowdWorkerNetworkCheckpoint::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerNetworkCheckpoint::IsValid(
  const FCrowdWorkerNetworkStateConfig& Config) const
{
  return Config.IsValid()
    && Version == CurrentVersion
    && Header.IsValid()
    && EventBaselineSequence == Header.LastOrderedEventSequence
    && StateRecords.Num() <= Config.MaxStateRecordsPerCheckpoint
    && ResourceRecords.Num() <= Config.MaxResourceRecordsPerCheckpoint
    && IsValidContinuation(
      Continuation,
      Header.WorkerEpoch,
      Header.AbsoluteSimulationTick,
      Config)
    && WatermarksCoverStates(
      Continuation.LifecycleWatermarks, StateRecords)
    && StateRecords.ContainsByPredicate(
      [this](const FCrowdWorkerDirtyStateRecord& State)
      {
        return State.Generation != Header.Generation;
      }) == false
    && IsSortedUniqueStates(StateRecords, Config.MaxPayloadBytes)
    && IsSortedUniqueResources(
      ResourceRecords, Config.MaxPayloadBytes)
    && StableHash == CalculateStableHash();
}

bool FCrowdWorkerNetworkStatePublisher::Reset(
  const FCrowdWorkerNetworkStateConfig& InConfig,
  const uint64 InGeneration)
{
  if (!InConfig.IsValid() || InGeneration == 0)
    return false;
  Config = InConfig;
  LatestCheckpoint = {};
  Metrics = {};
  Metrics.Generation = InGeneration;
  Generation = InGeneration;
  bInitialized = true;
  return true;
}

void FCrowdWorkerNetworkStatePublisher::LatchViolation()
{
  Metrics.bViolation = true;
}

bool FCrowdWorkerNetworkStatePublisher::CommitEpoch(
  FCrowdWorkerCheckpoint Header,
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> CompleteStates,
  const TConstArrayView<FCrowdWorkerResourceRecord> CompleteResources,
  const FCrowdWorkerNetworkContinuationState& Continuation)
{
  if (!bInitialized || Metrics.bViolation
    || Header.Generation != Generation
    || Header.WorkerEpoch == 0
    || Header.AbsoluteSimulationTick == 0
    || CompleteStates.Num() > Config.MaxStateRecordsPerCheckpoint
    || CompleteResources.Num() > Config.MaxResourceRecordsPerCheckpoint)
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerNetworkCommitRejected stage=precondition initialized=%d prior_violation=%d generation=%llu expected_generation=%llu epoch=%llu tick=%llu complete_states=%d complete_resources=%d"),
      bInitialized ? 1 : 0,
      Metrics.bViolation ? 1 : 0,
      Header.Generation,
      Generation,
      Header.WorkerEpoch,
      Header.AbsoluteSimulationTick,
      CompleteStates.Num(),
      CompleteResources.Num());
    LatchViolation();
    return false;
  }

  FCrowdWorkerNetworkCheckpoint Checkpoint;
  Checkpoint.Header = Header;
  Checkpoint.InputBaselineSequence = Header.LastAppliedInputSequence;
  Checkpoint.EventBaselineSequence = Header.LastOrderedEventSequence;
  Checkpoint.StateRecords.Append(CompleteStates);
  Checkpoint.ResourceRecords.Append(CompleteResources);
  Checkpoint.Continuation = Continuation;
  Checkpoint.StateRecords.Sort(StateLess);
  Checkpoint.ResourceRecords.Sort([](
    const FCrowdWorkerResourceRecord& A,
    const FCrowdWorkerResourceRecord& B)
  {
    return A.ResourceId < B.ResourceId;
  });
  for (const FCrowdWorkerDirtyStateRecord& State :
    Checkpoint.StateRecords)
    Checkpoint.MaxCorrectionRevision = FMath::Max(
      Checkpoint.MaxCorrectionRevision, State.CorrectionRevision);
  if (Header.EntityStateHash
      != CalculateEntityStateHash(Checkpoint.StateRecords)
    || Header.ResourceRevisionHash
      != CalculateResourceRevisionHash(Checkpoint.ResourceRecords))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerNetworkCommitRejected stage=checkpoint_hash header_entity=%llu calculated_entity=%llu header_resource=%llu calculated_resource=%llu states=%d resources=%d"),
      Header.EntityStateHash,
      CalculateEntityStateHash(Checkpoint.StateRecords),
      Header.ResourceRevisionHash,
      CalculateResourceRevisionHash(Checkpoint.ResourceRecords),
      Checkpoint.StateRecords.Num(),
      Checkpoint.ResourceRecords.Num());
    LatchViolation();
    return false;
  }
  Checkpoint.RecalculateStableHash();

  if (!Checkpoint.IsValid(Config))
  {
    UE_LOG(LogTemp, Error,
      TEXT("CrowdWorkerNetworkCommitRejected stage=final_validation checkpoint_valid=%d states=%d resources=%d header_last_event=%llu"),
      Checkpoint.IsValid(Config) ? 1 : 0,
      Checkpoint.StateRecords.Num(),
      Checkpoint.ResourceRecords.Num(),
      Header.LastOrderedEventSequence);
    LatchViolation();
    return false;
  }

  LatestCheckpoint = MoveTemp(Checkpoint);
  ++Metrics.CheckpointCount;
  Metrics.LatestCheckpointEpoch = Header.WorkerEpoch;
  Metrics.LatestEventSequence = Header.LastOrderedEventSequence;
  return true;
}

bool FCrowdWorkerNetworkStatePublisher::RestoreCheckpoint(
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
{
  if (!bInitialized || Metrics.bViolation
    || Checkpoint.Header.Generation != Generation
    || !Checkpoint.IsValid(Config)
    || Checkpoint.InputBaselineSequence == MAX_uint64)
  {
    LatchViolation();
    return false;
  }
  LatestCheckpoint = Checkpoint;
  Metrics.CheckpointCount = 1;
  Metrics.LatestCheckpointEpoch = Checkpoint.Header.WorkerEpoch;
  Metrics.LatestEventSequence = Checkpoint.EventBaselineSequence;
  return true;
}

ECrowdWorkerNetworkReadResult
FCrowdWorkerNetworkStatePublisher::ReadCheckpoint(
  const uint64 ExpectedGeneration,
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint) const
{
  OutCheckpoint = {};
  if (!bInitialized)
    return ECrowdWorkerNetworkReadResult::NotInitialized;
  if (Metrics.bViolation)
    return ECrowdWorkerNetworkReadResult::Violation;
  if (ExpectedGeneration != Generation)
    return ECrowdWorkerNetworkReadResult::RejectedGeneration;
  if (!LatestCheckpoint.IsValid(Config))
    return ECrowdWorkerNetworkReadResult::NoData;
  OutCheckpoint = LatestCheckpoint;
  return ECrowdWorkerNetworkReadResult::Ready;
}
