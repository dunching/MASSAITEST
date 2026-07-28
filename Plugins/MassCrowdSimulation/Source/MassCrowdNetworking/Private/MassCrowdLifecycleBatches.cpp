#include "MassCrowdLifecycleBatches.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  void FoldByte(uint64& Hash, const uint8 Value)
  {
    Hash ^= Value;
    Hash *= FnvPrime;
  }

  template<typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      FoldByte(Hash, static_cast<uint8>(Value >> (ByteIndex * 8)));
    }
  }

  void FoldRef(uint64& Hash, const FCrowdStableEntityRef& Ref)
  {
    FoldUnsigned(Hash, Ref.ProviderId);
    FoldUnsigned(Hash, Ref.StableEntityId);
    FoldUnsigned(Hash, Ref.LifecycleSerial);
  }

  void FoldAgentFacts(uint64& Hash, const FCrowdAgentFacts& Facts)
  {
    FoldRef(Hash, Facts.StableEntityRef);
    FoldUnsigned(Hash, Facts.FactionKey);
    FoldUnsigned(Hash, Facts.CapabilitySet.Bits);
    FoldRef(Hash, Facts.BusinessTaskRef);
    FoldRef(Hash, Facts.TargetRef);
    FoldUnsigned(Hash, Facts.MovementProfileKey);
    FoldUnsigned(Hash, Facts.PresentationProfileKey);
    FoldUnsigned(Hash, Facts.RuntimeState);
  }

  void FoldBehaviorState(
    uint64& Hash, const FCrowdLifecycleBehaviorState& State)
  {
    FoldUnsigned(Hash, State.CapabilityBinding.ProfileKey.Value);
    FoldUnsigned(Hash, State.CapabilityBinding.ModifierRevision);
    FoldByte(Hash, State.CapabilityBinding.ModifierCount);
    for (uint8 Index = 0;
      Index < State.CapabilityBinding.ModifierCount; ++Index)
    {
      FoldUnsigned(Hash,
        State.CapabilityBinding.Modifiers[Index].CapabilityId.Value);
      FoldByte(Hash, static_cast<uint8>(
        State.CapabilityBinding.Modifiers[Index].Operation));
    }
    FoldUnsigned(Hash, State.SourceSetRevision);
    FoldUnsigned(Hash, State.SourceSetHash);
    FoldUnsigned(Hash, State.ResolvedBehaviorHash);
    FoldUnsigned(Hash, State.DerivedDiagnosticLabel);
  }

  uint64 BeginBatchHash(
    const FCrowdLifecycleBatchHeader& Header,
    const ECrowdLifecycleBatchKind Kind)
  {
    uint64 Hash = FnvOffset;
    FoldUnsigned(Hash, Header.ProtocolVersion);
    FoldByte(Hash, static_cast<uint8>(Kind));
    FoldUnsigned(Hash, Header.BaseSnapshotRevision);
    FoldUnsigned(Hash, static_cast<uint64>(Header.FixedStepIndex));
    FoldUnsigned(Hash, Header.RelevantSetRevision);
    FoldUnsigned(Hash, Header.Sequence);
    FoldUnsigned(Hash, static_cast<uint32>(Header.EntryCount));
    return Hash;
  }

  bool IsStrictlySorted(
    const FCrowdStableEntityRef& Previous,
    const FCrowdStableEntityRef& Current)
  {
    return Previous < Current && !Previous.IsSameEntitySlot(Current);
  }

  bool IsHeaderShapeValid(
    const FCrowdLifecycleBatchHeader& Header,
    const int32 ActualEntryCount,
    const FCrowdLifecycleBatchLimits& Limits)
  {
    return Header.ProtocolVersion == FCrowdLifecycleBatchHeader::CurrentProtocolVersion
      && Header.BaseSnapshotRevision != 0
      && Header.FixedStepIndex >= 0
      && Header.RelevantSetRevision != 0
      && Header.Sequence != 0
      && Header.EntryCount == ActualEntryCount
      && ActualEntryCount > 0
      && ActualEntryCount <= Limits.MaxEntriesPerBatch;
  }
}

bool FCrowdLifecycleBatchLimits::IsValid() const
{
  return MaxSnapshotEntities > 0
    && MaxEntriesPerBatch > 0
    && MaxTrackedSlots >= MaxSnapshotEntities
    && MaxSequenceHistory > 0;
}

uint64 FCrowdLifecycleBatchTransport::CalculateHash(const FCrowdSpawnBatch& Batch)
{
  uint64 Hash = BeginBatchHash(Batch.Header, ECrowdLifecycleBatchKind::Spawn);
  for (const FCrowdSpawnEntry& Entry : Batch.Entries)
  {
    FoldAgentFacts(Hash, Entry.AgentFacts);
    FoldUnsigned(Hash, Entry.MembershipKey);
    FoldBehaviorState(Hash, Entry.BehaviorState);
  }
  return Hash;
}

uint64 FCrowdLifecycleBatchTransport::CalculateHash(const FCrowdDespawnBatch& Batch)
{
  uint64 Hash = BeginBatchHash(Batch.Header, ECrowdLifecycleBatchKind::Despawn);
  for (const FCrowdDespawnEntry& Entry : Batch.Entries)
  {
    FoldRef(Hash, Entry.EntityRef);
    FoldByte(Hash, static_cast<uint8>(Entry.Reason));
  }
  return Hash;
}

uint64 FCrowdLifecycleBatchTransport::CalculateHash(const FCrowdMembershipBatch& Batch)
{
  uint64 Hash = BeginBatchHash(Batch.Header, ECrowdLifecycleBatchKind::Membership);
  for (const FCrowdMembershipEntry& Entry : Batch.Entries)
  {
    FoldRef(Hash, Entry.EntityRef);
    FoldUnsigned(Hash, Entry.PreviousMembershipKey);
    FoldUnsigned(Hash, Entry.NewMembershipKey);
  }
  return Hash;
}

bool FCrowdLifecycleBatchTransport::Finalize(FCrowdSpawnBatch& Batch)
{
  if (Batch.Entries.IsEmpty()) return false;
  Batch.Header.EntryCount = Batch.Entries.Num();
  Batch.Header.BatchHash = CalculateHash(Batch);
  return true;
}

bool FCrowdLifecycleBatchTransport::Finalize(FCrowdDespawnBatch& Batch)
{
  if (Batch.Entries.IsEmpty()) return false;
  Batch.Header.EntryCount = Batch.Entries.Num();
  Batch.Header.BatchHash = CalculateHash(Batch);
  return true;
}

bool FCrowdLifecycleBatchTransport::Finalize(FCrowdMembershipBatch& Batch)
{
  if (Batch.Entries.IsEmpty()) return false;
  Batch.Header.EntryCount = Batch.Entries.Num();
  Batch.Header.BatchHash = CalculateHash(Batch);
  return true;
}

bool FCrowdLifecycleDeltaState::BeginFromSnapshot(
  const uint32 SnapshotRevision,
  const int64 FixedStepIndex,
  const uint32 RelevantSetRevision,
  const TConstArrayView<FCrowdLifecycleSnapshotEntity> Entities,
  const FCrowdLifecycleBatchLimits& InLimits,
  const uint64 ResumeSequence)
{
  TrackedEntities.Reset();
  AppliedBatches.Reset();
  Limits = {};
  BaseSnapshotRevision = 0;
  CurrentRelevantSetRevision = 0;
  CurrentFixedStepIndex = 0;
  LastSequence = 0;
  bBegun = false;
  if (!InLimits.IsValid()
    || SnapshotRevision == 0
    || FixedStepIndex < 0
    || RelevantSetRevision == 0
    || ResumeSequence == 0
    || Entities.Num() > InLimits.MaxSnapshotEntities
    || Entities.Num() > InLimits.MaxTrackedSlots)
  {
    return false;
  }

  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  TrackedEntities.Reserve(Entities.Num());
  for (const FCrowdLifecycleSnapshotEntity& Entity : Entities)
  {
    const FCrowdStableEntityRef& Ref = Entity.AgentFacts.StableEntityRef;
    if (!Entity.AgentFacts.IsWellFormed()
      || (!Entity.BehaviorState.IsEmpty()
        && !Entity.BehaviorState.IsValid())
      || (bHasPrevious && !IsStrictlySorted(Previous, Ref)))
    {
      TrackedEntities.Reset();
      return false;
    }
    FTrackedEntity& Tracked = TrackedEntities.AddDefaulted_GetRef();
    Tracked.AgentFacts = Entity.AgentFacts;
    Tracked.MembershipKey = Entity.MembershipKey;
    Tracked.bActive = true;
    Previous = Ref;
    bHasPrevious = true;
  }

  Limits = InLimits;
  BaseSnapshotRevision = SnapshotRevision;
  CurrentRelevantSetRevision = RelevantSetRevision;
  CurrentFixedStepIndex = FixedStepIndex;
  LastSequence = ResumeSequence - 1;
  bBegun = true;
  return true;
}

ECrowdLifecycleBatchAcceptResult FCrowdLifecycleDeltaState::AcceptSpawnBatch(
  const FCrowdSpawnBatch& Batch)
{
  const uint64 CalculatedHash = FCrowdLifecycleBatchTransport::CalculateHash(Batch);
  if (!bBegun) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (const FAppliedBatch* Existing = AppliedBatches.Find(Batch.Header.Sequence))
  {
    return Existing->Kind == ECrowdLifecycleBatchKind::Spawn
        && Existing->Hash == Batch.Header.BatchHash
        && CalculatedHash == Batch.Header.BatchHash
      ? ECrowdLifecycleBatchAcceptResult::Duplicate
      : ECrowdLifecycleBatchAcceptResult::RejectedConflict;
  }
  if (Batch.Header.Sequence <= LastSequence)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  if (Batch.Header.Sequence != LastSequence + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedMissingSequence;
  if (Batch.Entries.Num() > Limits.MaxEntriesPerBatch)
    return ECrowdLifecycleBatchAcceptResult::RejectedBounds;
  if (!IsHeaderShapeValid(Batch.Header, Batch.Entries.Num(), Limits)
    || Batch.Header.BatchHash != CalculatedHash)
    return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.BaseSnapshotRevision != BaseSnapshotRevision
    || Batch.Header.FixedStepIndex < CurrentFixedStepIndex
    || CurrentRelevantSetRevision == MAX_uint32
    || Batch.Header.RelevantSetRevision != CurrentRelevantSetRevision + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;

  TArray<FTrackedEntity> Working = TrackedEntities;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdSpawnEntry& Entry : Batch.Entries)
  {
    const FCrowdStableEntityRef& Ref = Entry.AgentFacts.StableEntityRef;
    if (!Entry.AgentFacts.IsWellFormed()
      || (!Entry.BehaviorState.IsEmpty()
        && !Entry.BehaviorState.IsValid())
      || (bHasPrevious && !IsStrictlySorted(Previous, Ref)))
      return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;

    int32 SlotIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Working.Num(); ++Index)
    {
      if (Working[Index].AgentFacts.StableEntityRef.IsSameEntitySlot(Ref))
      {
        SlotIndex = Index;
        break;
      }
    }
    if (SlotIndex != INDEX_NONE)
    {
      FTrackedEntity& Existing = Working[SlotIndex];
      if (Ref.LifecycleSerial <= Existing.AgentFacts.StableEntityRef.LifecycleSerial)
        return ECrowdLifecycleBatchAcceptResult::RejectedStale;
      if (Existing.bActive)
        return ECrowdLifecycleBatchAcceptResult::RejectedConflict;
      Existing.AgentFacts = Entry.AgentFacts;
      Existing.MembershipKey = Entry.MembershipKey;
      Existing.bActive = true;
    }
    else
    {
      if (Working.Num() >= Limits.MaxTrackedSlots)
        return ECrowdLifecycleBatchAcceptResult::RejectedBounds;
      FTrackedEntity& Added = Working.AddDefaulted_GetRef();
      Added.AgentFacts = Entry.AgentFacts;
      Added.MembershipKey = Entry.MembershipKey;
      Added.bActive = true;
    }
    Previous = Ref;
    bHasPrevious = true;
  }
  Working.Sort([](const FTrackedEntity& A, const FTrackedEntity& B)
  {
    return A.AgentFacts.StableEntityRef < B.AgentFacts.StableEntityRef;
  });
  int32 ActiveCount = 0;
  for (const FTrackedEntity& Entity : Working) ActiveCount += Entity.bActive ? 1 : 0;
  if (ActiveCount > Limits.MaxSnapshotEntities)
    return ECrowdLifecycleBatchAcceptResult::RejectedBounds;

  TrackedEntities = MoveTemp(Working);
  CurrentFixedStepIndex = Batch.Header.FixedStepIndex;
  CurrentRelevantSetRevision = Batch.Header.RelevantSetRevision;
  LastSequence = Batch.Header.Sequence;
  RecordAppliedBatch(LastSequence, ECrowdLifecycleBatchKind::Spawn, Batch.Header.BatchHash);
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

ECrowdLifecycleBatchAcceptResult FCrowdLifecycleDeltaState::AcceptDespawnBatch(
  const FCrowdDespawnBatch& Batch)
{
  const uint64 CalculatedHash = FCrowdLifecycleBatchTransport::CalculateHash(Batch);
  if (!bBegun) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (const FAppliedBatch* Existing = AppliedBatches.Find(Batch.Header.Sequence))
  {
    return Existing->Kind == ECrowdLifecycleBatchKind::Despawn
        && Existing->Hash == Batch.Header.BatchHash
        && CalculatedHash == Batch.Header.BatchHash
      ? ECrowdLifecycleBatchAcceptResult::Duplicate
      : ECrowdLifecycleBatchAcceptResult::RejectedConflict;
  }
  if (Batch.Header.Sequence <= LastSequence)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  if (Batch.Header.Sequence != LastSequence + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedMissingSequence;
  if (Batch.Entries.Num() > Limits.MaxEntriesPerBatch)
    return ECrowdLifecycleBatchAcceptResult::RejectedBounds;
  if (!IsHeaderShapeValid(Batch.Header, Batch.Entries.Num(), Limits)
    || Batch.Header.BatchHash != CalculatedHash)
    return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.BaseSnapshotRevision != BaseSnapshotRevision
    || Batch.Header.FixedStepIndex < CurrentFixedStepIndex
    || CurrentRelevantSetRevision == MAX_uint32
    || Batch.Header.RelevantSetRevision != CurrentRelevantSetRevision + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;

  TArray<FTrackedEntity> Working = TrackedEntities;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdDespawnEntry& Entry : Batch.Entries)
  {
    if (!Entry.EntityRef.IsValid()
      || Entry.Reason >= ECrowdDespawnReason::Count
      || (bHasPrevious && !IsStrictlySorted(Previous, Entry.EntityRef)))
      return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
    int32 SlotIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Working.Num(); ++Index)
    {
      if (Working[Index].AgentFacts.StableEntityRef.IsSameEntitySlot(Entry.EntityRef))
      {
        SlotIndex = Index;
        break;
      }
    }
    if (SlotIndex == INDEX_NONE
      || !Working[SlotIndex].bActive
      || !(Working[SlotIndex].AgentFacts.StableEntityRef == Entry.EntityRef))
      return ECrowdLifecycleBatchAcceptResult::RejectedStale;
    Working[SlotIndex].bActive = false;
    Previous = Entry.EntityRef;
    bHasPrevious = true;
  }

  TrackedEntities = MoveTemp(Working);
  CurrentFixedStepIndex = Batch.Header.FixedStepIndex;
  CurrentRelevantSetRevision = Batch.Header.RelevantSetRevision;
  LastSequence = Batch.Header.Sequence;
  RecordAppliedBatch(LastSequence, ECrowdLifecycleBatchKind::Despawn, Batch.Header.BatchHash);
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

ECrowdLifecycleBatchAcceptResult FCrowdLifecycleDeltaState::AcceptMembershipBatch(
  const FCrowdMembershipBatch& Batch)
{
  const uint64 CalculatedHash = FCrowdLifecycleBatchTransport::CalculateHash(Batch);
  if (!bBegun) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (const FAppliedBatch* Existing = AppliedBatches.Find(Batch.Header.Sequence))
  {
    return Existing->Kind == ECrowdLifecycleBatchKind::Membership
        && Existing->Hash == Batch.Header.BatchHash
        && CalculatedHash == Batch.Header.BatchHash
      ? ECrowdLifecycleBatchAcceptResult::Duplicate
      : ECrowdLifecycleBatchAcceptResult::RejectedConflict;
  }
  if (Batch.Header.Sequence <= LastSequence)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  if (Batch.Header.Sequence != LastSequence + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedMissingSequence;
  if (Batch.Entries.Num() > Limits.MaxEntriesPerBatch)
    return ECrowdLifecycleBatchAcceptResult::RejectedBounds;
  if (!IsHeaderShapeValid(Batch.Header, Batch.Entries.Num(), Limits)
    || Batch.Header.BatchHash != CalculatedHash)
    return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.BaseSnapshotRevision != BaseSnapshotRevision
    || Batch.Header.FixedStepIndex < CurrentFixedStepIndex
    || CurrentRelevantSetRevision == MAX_uint32
    || Batch.Header.RelevantSetRevision != CurrentRelevantSetRevision + 1)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;

  TArray<FTrackedEntity> Working = TrackedEntities;
  FCrowdStableEntityRef Previous;
  bool bHasPrevious = false;
  for (const FCrowdMembershipEntry& Entry : Batch.Entries)
  {
    if (!Entry.EntityRef.IsValid()
      || Entry.PreviousMembershipKey == Entry.NewMembershipKey
      || (bHasPrevious && !IsStrictlySorted(Previous, Entry.EntityRef)))
      return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
    int32 SlotIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Working.Num(); ++Index)
    {
      if (Working[Index].AgentFacts.StableEntityRef.IsSameEntitySlot(Entry.EntityRef))
      {
        SlotIndex = Index;
        break;
      }
    }
    if (SlotIndex == INDEX_NONE
      || !Working[SlotIndex].bActive
      || !(Working[SlotIndex].AgentFacts.StableEntityRef == Entry.EntityRef))
      return ECrowdLifecycleBatchAcceptResult::RejectedStale;
    if (Working[SlotIndex].MembershipKey != Entry.PreviousMembershipKey)
      return ECrowdLifecycleBatchAcceptResult::RejectedConflict;
    Working[SlotIndex].MembershipKey = Entry.NewMembershipKey;
    Previous = Entry.EntityRef;
    bHasPrevious = true;
  }

  TrackedEntities = MoveTemp(Working);
  CurrentFixedStepIndex = Batch.Header.FixedStepIndex;
  CurrentRelevantSetRevision = Batch.Header.RelevantSetRevision;
  LastSequence = Batch.Header.Sequence;
  RecordAppliedBatch(LastSequence, ECrowdLifecycleBatchKind::Membership, Batch.Header.BatchHash);
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

int32 FCrowdLifecycleDeltaState::FindSlot(const FCrowdStableEntityRef& EntityRef) const
{
  for (int32 Index = 0; Index < TrackedEntities.Num(); ++Index)
  {
    if (TrackedEntities[Index].AgentFacts.StableEntityRef.IsSameEntitySlot(EntityRef))
      return Index;
  }
  return INDEX_NONE;
}

void FCrowdLifecycleDeltaState::RecordAppliedBatch(
  const uint64 Sequence,
  const ECrowdLifecycleBatchKind Kind,
  const uint64 Hash)
{
  AppliedBatches.Add(Sequence, FAppliedBatch{Kind, Hash});
  while (AppliedBatches.Num() > Limits.MaxSequenceHistory)
  {
    uint64 OldestSequence = MAX_uint64;
    for (const TPair<uint64, FAppliedBatch>& Pair : AppliedBatches)
      OldestSequence = FMath::Min(OldestSequence, Pair.Key);
    AppliedBatches.Remove(OldestSequence);
  }
}

int32 FCrowdLifecycleDeltaState::GetActiveEntityCount() const
{
  int32 Count = 0;
  for (const FTrackedEntity& Entity : TrackedEntities) Count += Entity.bActive ? 1 : 0;
  return Count;
}

uint64 FCrowdLifecycleDeltaState::CalculateMembershipHash() const
{
  uint64 Hash = FnvOffset;
  FoldUnsigned(Hash, FCrowdLifecycleBatchHeader::CurrentProtocolVersion);
  for (const FTrackedEntity& Entity : TrackedEntities)
  {
    if (!Entity.bActive) continue;
    FoldRef(Hash, Entity.AgentFacts.StableEntityRef);
    FoldUnsigned(Hash, Entity.MembershipKey);
  }
  return Hash;
}

bool FCrowdLifecycleDeltaState::Contains(const FCrowdStableEntityRef& EntityRef) const
{
  const int32 Index = FindSlot(EntityRef);
  return Index != INDEX_NONE
    && TrackedEntities[Index].bActive
    && TrackedEntities[Index].AgentFacts.StableEntityRef == EntityRef;
}

bool FCrowdLifecycleDeltaState::TryGetMembership(
  const FCrowdStableEntityRef& EntityRef,
  uint32& OutMembershipKey) const
{
  const int32 Index = FindSlot(EntityRef);
  if (Index == INDEX_NONE
    || !TrackedEntities[Index].bActive
    || !(TrackedEntities[Index].AgentFacts.StableEntityRef == EntityRef))
  {
    return false;
  }
  OutMembershipKey = TrackedEntities[Index].MembershipKey;
  return true;
}
