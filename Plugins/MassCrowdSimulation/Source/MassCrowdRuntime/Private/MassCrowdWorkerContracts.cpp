#include "MassCrowdWorkerContracts.h"

#include <type_traits>

namespace CrowdWorkerContractsPrivate
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  enum class EInputRecordKind : uint8
  {
    Spawn = 1,
    Despawn,
    Command,
    State,
    Resource,
    Correction
  };

  uint64 FoldBytes(
    uint64 Hash,
    const uint8* Bytes,
    const int32 ByteCount)
  {
    for (int32 Index = 0; Index < ByteCount; ++Index)
    {
      Hash ^= Bytes[Index];
      Hash *= FnvPrime64;
    }
    return Hash;
  }

  template<typename T>
  uint64 FoldPod(uint64 Hash, const T& Value)
  {
    static_assert(std::is_integral_v<T>);
    using TUnsigned = std::make_unsigned_t<T>;
    const TUnsigned Bits = static_cast<TUnsigned>(Value);
    for (int32 Byte = 0;
      Byte < static_cast<int32>(sizeof(T)); ++Byte)
    {
      Hash ^= static_cast<uint8>(
        (Bits >> (Byte * 8)) & TUnsigned{0xff});
      Hash *= FnvPrime64;
    }
    return Hash;
  }

  uint64 FoldDouble(uint64 Hash, const double Value)
  {
    uint64 Bits = 0;
    static_assert(sizeof(Bits) == sizeof(Value));
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    return FoldPod(Hash, Bits);
  }

  uint64 FoldRef(uint64 Hash, const FCrowdStableEntityRef& Ref)
  {
    Hash = FoldPod(Hash, Ref.ProviderId);
    Hash = FoldPod(Hash, Ref.StableEntityId);
    return FoldPod(Hash, Ref.LifecycleSerial);
  }

  uint64 FoldPayload(
    uint64 Hash,
    const FCrowdWorkerPayload& Payload)
  {
    Hash = FoldPod(Hash, Payload.SchemaId);
    Hash = FoldPod(Hash, Payload.SchemaVersion);
    Hash = FoldPod(Hash, Payload.Bytes.Num());
    return FoldBytes(Hash, Payload.Bytes.GetData(), Payload.Bytes.Num());
  }

  bool IsFiniteNonNegative(const double Value)
  {
    return FMath::IsFinite(Value) && Value >= 0.0;
  }

  struct FInputRecordRef
  {
    uint64 Sequence = 0;
    EInputRecordKind Kind = EInputRecordKind::Spawn;
    int32 Index = INDEX_NONE;
  };

  bool InputRecordLess(
    const FInputRecordRef& A,
    const FInputRecordRef& B)
  {
    if (A.Sequence != B.Sequence) return A.Sequence < B.Sequence;
    if (A.Kind != B.Kind)
      return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
    return A.Index < B.Index;
  }

  void GatherInputRecords(
    const FCrowdWorkerInputBatch& Batch,
    TArray<FInputRecordRef>& OutRecords)
  {
    OutRecords.Reset();
    OutRecords.Reserve(Batch.GetRecordCount());
    for (int32 Index = 0; Index < Batch.Spawns.Num(); ++Index)
      OutRecords.Add({
        Batch.Spawns[Index].InputSequence,
        EInputRecordKind::Spawn,
        Index});
    for (int32 Index = 0; Index < Batch.Despawns.Num(); ++Index)
      OutRecords.Add({
        Batch.Despawns[Index].InputSequence,
        EInputRecordKind::Despawn,
        Index});
    for (int32 Index = 0; Index < Batch.Commands.Num(); ++Index)
      OutRecords.Add({
        Batch.Commands[Index].InputSequence,
        EInputRecordKind::Command,
        Index});
    for (int32 Index = 0; Index < Batch.StateDeltas.Num(); ++Index)
      OutRecords.Add({
        Batch.StateDeltas[Index].InputSequence,
        EInputRecordKind::State,
        Index});
    for (int32 Index = 0; Index < Batch.ResourceDeltas.Num(); ++Index)
      OutRecords.Add({
        Batch.ResourceDeltas[Index].InputSequence,
        EInputRecordKind::Resource,
        Index});
    for (int32 Index = 0; Index < Batch.Corrections.Num(); ++Index)
      OutRecords.Add({
        Batch.Corrections[Index].InputSequence,
        EInputRecordKind::Correction,
        Index});
    OutRecords.Sort(InputRecordLess);
  }

  uint64 FoldInputRecord(
    uint64 Hash,
    const FCrowdWorkerInputBatch& Batch,
    const FInputRecordRef& Record)
  {
    Hash = FoldPod(Hash, static_cast<uint8>(Record.Kind));
    Hash = FoldPod(Hash, Record.Sequence);
    switch (Record.Kind)
    {
      case EInputRecordKind::Spawn:
      {
        const FCrowdWorkerSpawnDelta& Delta =
          Batch.Spawns[Record.Index];
        Hash = FoldRef(Hash, Delta.EntityRef);
        return FoldPod(Hash, Delta.InitialState.StableHash);
      }
      case EInputRecordKind::Despawn:
      {
        const FCrowdWorkerDespawnDelta& Delta =
          Batch.Despawns[Record.Index];
        Hash = FoldRef(Hash, Delta.EntityRef);
        return FoldPod(Hash, Delta.ReasonId);
      }
      case EInputRecordKind::Command:
      {
        const FCrowdWorkerCommandDelta& Delta =
          Batch.Commands[Record.Index];
        Hash = FoldRef(Hash, Delta.EntityRef);
        Hash = FoldPod(Hash, Delta.CommandId);
        Hash = FoldDouble(Hash, Delta.EffectiveSimulationTimeSeconds);
        return FoldPod(Hash, Delta.Payload.StableHash);
      }
      case EInputRecordKind::State:
      {
        const FCrowdWorkerStateDelta& Delta =
          Batch.StateDeltas[Record.Index];
        Hash = FoldRef(Hash, Delta.EntityRef);
        Hash = FoldPod(Hash, Delta.DirtyMask);
        return FoldPod(Hash, Delta.FullState.StableHash);
      }
      case EInputRecordKind::Resource:
      {
        const FCrowdWorkerResourceDelta& Delta =
          Batch.ResourceDeltas[Record.Index];
        Hash = FoldPod(Hash, Delta.ResourceId);
        Hash = FoldPod(Hash, Delta.Revision);
        return FoldPod(Hash, Delta.Payload.StableHash);
      }
      case EInputRecordKind::Correction:
      {
        const FCrowdWorkerCorrectionDelta& Delta =
          Batch.Corrections[Record.Index];
        Hash = FoldRef(Hash, Delta.EntityRef);
        Hash = FoldPod(Hash, Delta.CorrectionRevision);
        Hash = FoldPod(Hash, Delta.DirtyMask);
        return FoldPod(Hash, Delta.FullState.StableHash);
      }
    }
    return Hash;
  }
}

using namespace CrowdWorkerContractsPrivate;

uint64 FCrowdWorkerPayload::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, uint32{1});
  return FoldPayload(Hash, *this);
}

void FCrowdWorkerPayload::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerPayload::IsValid(const int32 MaxPayloadBytes) const
{
  return SchemaId != 0
    && SchemaVersion != 0
    && MaxPayloadBytes > 0
    && Bytes.Num() <= MaxPayloadBytes
    && StableHash != 0
    && StableHash == CalculateStableHash();
}

bool FCrowdWorkerSpawnDelta::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && EntityRef.IsValid()
    && InitialState.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerDespawnDelta::IsValid() const
{
  return InputSequence != 0
    && EntityRef.IsValid()
    && ReasonId != 0;
}

bool FCrowdWorkerCommandDelta::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && (EntityRef.IsUnset() || EntityRef.IsValid())
    && CommandId != 0
    && IsFiniteNonNegative(EffectiveSimulationTimeSeconds)
    && Payload.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerStateDelta::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && EntityRef.IsValid()
    && DirtyMask != 0
    && FullState.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerResourceDelta::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && ResourceId != 0
    && Revision != 0
    && Payload.IsValid(MaxPayloadBytes);
}

bool FCrowdWorkerCorrectionDelta::IsValid(
  const int32 MaxPayloadBytes) const
{
  return InputSequence != 0
    && EntityRef.IsValid()
    && CorrectionRevision != 0
    && DirtyMask != 0
    && FullState.IsValid(MaxPayloadBytes);
}

int32 FCrowdWorkerInputBatch::GetRecordCount() const
{
  return Spawns.Num()
    + Despawns.Num()
    + Commands.Num()
    + StateDeltas.Num()
    + ResourceDeltas.Num()
    + Corrections.Num();
}

uint64 FCrowdWorkerInputBatch::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, Version);
  Hash = FoldPod(Hash, Generation);
  Hash = FoldPod(Hash, FirstInputSequence);
  Hash = FoldPod(Hash, LastInputSequence);
  Hash = FoldDouble(Hash, TargetSimulationTimeSeconds);
  Hash = FoldPod(Hash, GetRecordCount());
  TArray<FInputRecordRef> Records;
  GatherInputRecords(*this, Records);
  for (const FInputRecordRef& Record : Records)
    Hash = FoldInputRecord(Hash, *this, Record);
  return Hash;
}

void FCrowdWorkerInputBatch::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerInputBatch::IsValid(
  const FCrowdWorkerContractLimits& Limits) const
{
  if (!Limits.IsValid()
    || Version != CurrentVersion
    || Generation == 0
    || !IsFiniteNonNegative(TargetSimulationTimeSeconds))
    return false;

  const int32 RecordCount = GetRecordCount();
  if (RecordCount > Limits.MaxInputRecordsPerBatch)
    return false;
  if (RecordCount == 0)
  {
    return FirstInputSequence == 0
      && LastInputSequence == 0
      && StableHash != 0
      && StableHash == CalculateStableHash();
  }
  if (FirstInputSequence == 0
    || LastInputSequence < FirstInputSequence
    || static_cast<uint64>(RecordCount)
      != LastInputSequence - FirstInputSequence + 1)
    return false;

  for (const FCrowdWorkerSpawnDelta& Delta : Spawns)
    if (!Delta.IsValid(Limits.MaxPayloadBytes)) return false;
  for (const FCrowdWorkerDespawnDelta& Delta : Despawns)
    if (!Delta.IsValid()) return false;
  for (const FCrowdWorkerCommandDelta& Delta : Commands)
    if (!Delta.IsValid(Limits.MaxPayloadBytes)) return false;
  for (const FCrowdWorkerStateDelta& Delta : StateDeltas)
    if (!Delta.IsValid(Limits.MaxPayloadBytes)) return false;
  for (const FCrowdWorkerResourceDelta& Delta : ResourceDeltas)
    if (!Delta.IsValid(Limits.MaxPayloadBytes)) return false;
  for (const FCrowdWorkerCorrectionDelta& Delta : Corrections)
    if (!Delta.IsValid(Limits.MaxPayloadBytes)) return false;

  TArray<FInputRecordRef> Records;
  GatherInputRecords(*this, Records);
  for (int32 Index = 0; Index < Records.Num(); ++Index)
    if (Records[Index].Sequence != FirstInputSequence + Index)
      return false;
  return StableHash != 0 && StableHash == CalculateStableHash();
}

bool FCrowdWorkerInputSequenceGate::ResetForResnapshot(
  const uint64 InGeneration,
  const uint64 FirstExpectedInputSequence)
{
  if (InGeneration == 0 || FirstExpectedInputSequence == 0)
    return false;
  Generation = InGeneration;
  NextExpectedSequence = FirstExpectedInputSequence;
  LastAcceptedFirstSequence = 0;
  LastAcceptedLastSequence = 0;
  LastAcceptedHash = 0;
  LastTargetSimulationTimeSeconds = 0.0;
  bHasAcceptedBatch = false;
  bRequiresResnapshot = false;
  return true;
}

ECrowdWorkerInputAcceptResult FCrowdWorkerInputSequenceGate::Accept(
  const FCrowdWorkerInputBatch& Batch,
  const FCrowdWorkerContractLimits& Limits)
{
  if (Batch.Generation != Generation)
    return ECrowdWorkerInputAcceptResult::RejectedGeneration;
  if (bRequiresResnapshot)
    return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
  if (!Batch.IsValid(Limits))
  {
    bRequiresResnapshot = true;
    return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
  }
  if (Batch.GetRecordCount() == 0)
  {
    if (bHasAcceptedBatch
      && Batch.TargetSimulationTimeSeconds
        < LastTargetSimulationTimeSeconds)
    {
      bRequiresResnapshot = true;
      return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
    }
    LastTargetSimulationTimeSeconds =
      Batch.TargetSimulationTimeSeconds;
    bHasAcceptedBatch = true;
    return ECrowdWorkerInputAcceptResult::AcceptedEmpty;
  }
  if (Batch.FirstInputSequence == NextExpectedSequence)
  {
    if (bHasAcceptedBatch
      && Batch.TargetSimulationTimeSeconds
        < LastTargetSimulationTimeSeconds)
    {
      bRequiresResnapshot = true;
      return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
    }
    NextExpectedSequence = Batch.LastInputSequence + 1;
    LastAcceptedFirstSequence = Batch.FirstInputSequence;
    LastAcceptedLastSequence = Batch.LastInputSequence;
    LastAcceptedHash = Batch.StableHash;
    LastTargetSimulationTimeSeconds =
      Batch.TargetSimulationTimeSeconds;
    bHasAcceptedBatch = true;
    return ECrowdWorkerInputAcceptResult::Accepted;
  }
  if (Batch.FirstInputSequence == LastAcceptedFirstSequence
    && Batch.LastInputSequence == LastAcceptedLastSequence)
  {
    if (Batch.StableHash == LastAcceptedHash)
      return ECrowdWorkerInputAcceptResult::AcceptedDuplicate;
    bRequiresResnapshot = true;
    return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
  }
  if (Batch.LastInputSequence < LastAcceptedFirstSequence)
    return ECrowdWorkerInputAcceptResult::RejectedStale;

  bRequiresResnapshot = true;
  return ECrowdWorkerInputAcceptResult::RequiresResnapshot;
}

uint64 FCrowdWorkerPublishedState::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, uint32{1});
  Hash = FoldPod(Hash, StateRevision);
  return FoldPod(Hash, Payload.StableHash);
}

bool FCrowdWorkerPublishedState::IsValid(
  const int32 MaxPayloadBytes) const
{
  return StateRevision != 0
    && Payload.IsValid(MaxPayloadBytes);
}

uint64 FCrowdWorkerStatePatch::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, uint32{1});
  Hash = FoldRef(Hash, EntityRef);
  Hash = FoldPod(Hash, Generation);
  Hash = FoldPod(Hash, WorkerEpoch);
  Hash = FoldPod(Hash, SourceInputSequence);
  Hash = FoldPod(Hash, DirtyMask);
  return FoldPod(Hash, State.CalculateStableHash());
}

void FCrowdWorkerStatePatch::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerStatePatch::IsValid(
  const uint64 ExpectedGeneration,
  const int32 MaxPayloadBytes) const
{
  return EntityRef.IsValid()
    && Generation == ExpectedGeneration
    && WorkerEpoch != 0
    && DirtyMask != 0
    && State.IsValid(MaxPayloadBytes)
    && StableHash != 0
    && StableHash == CalculateStableHash();
}

uint64 FCrowdWorkerGameplayEvent::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, uint32{1});
  Hash = FoldRef(Hash, EntityRef);
  Hash = FoldPod(Hash, Generation);
  Hash = FoldPod(Hash, WorkerEpoch);
  Hash = FoldPod(Hash, SourceInputSequence);
  Hash = FoldPod(Hash, EventSequence);
  Hash = FoldPod(Hash, EventId);
  return FoldPod(Hash, Payload.StableHash);
}

void FCrowdWorkerGameplayEvent::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

bool FCrowdWorkerGameplayEvent::IsValid(
  const uint64 ExpectedGeneration,
  const int32 MaxPayloadBytes) const
{
  return (EntityRef.IsUnset() || EntityRef.IsValid())
    && Generation == ExpectedGeneration
    && WorkerEpoch != 0
    && EventSequence != 0
    && EventId != 0
    && Payload.IsValid(MaxPayloadBytes)
    && StableHash != 0
    && StableHash == CalculateStableHash();
}

uint64 FCrowdWorkerPublishedBatch::CalculateStableHash() const
{
  uint64 Hash = FoldPod(FnvOffset64, Version);
  Hash = FoldPod(Hash, Generation);
  Hash = FoldPod(Hash, PublishSequence);
  Hash = FoldPod(Hash, MinWorkerEpoch);
  Hash = FoldPod(Hash, MaxWorkerEpoch);
  Hash = FoldPod(Hash, LastAppliedInputSequence);
  Hash = FoldDouble(Hash, PublishedSimulationTimeSeconds);
  Hash = FoldPod(Hash, StatePatches.Num());
  for (const FCrowdWorkerStatePatch& Patch : StatePatches)
    Hash = FoldPod(Hash, Patch.StableHash);
  Hash = FoldPod(Hash, OrderedEvents.Num());
  for (const FCrowdWorkerGameplayEvent& Event : OrderedEvents)
    Hash = FoldPod(Hash, Event.StableHash);
  return Hash;
}

void FCrowdWorkerPublishedBatch::RecalculateStableHash()
{
  StableHash = CalculateStableHash();
}

ECrowdWorkerPublishedValidationResult
FCrowdWorkerPublishedBatchValidator::Validate(
  const FCrowdWorkerPublishedBatch& Batch,
  const FCrowdWorkerContractLimits& Limits,
  const uint64 ExpectedGeneration,
  const uint64 LastConsumedPublishSequence)
{
  if (!Limits.IsValid()
    || Batch.Version != FCrowdWorkerPublishedBatch::CurrentVersion
    || Batch.PublishSequence == 0
    || Batch.MinWorkerEpoch == 0
    || Batch.MaxWorkerEpoch < Batch.MinWorkerEpoch
    || !IsFiniteNonNegative(Batch.PublishedSimulationTimeSeconds)
    || Batch.StatePatches.Num() > Limits.MaxStatePatchesPerSlot
    || Batch.OrderedEvents.Num() > Limits.MaxPendingOrderedEvents)
    return ECrowdWorkerPublishedValidationResult::RejectedStructure;
  if (Batch.Generation != ExpectedGeneration)
    return ECrowdWorkerPublishedValidationResult::RejectedGeneration;
  if (Batch.PublishSequence <= LastConsumedPublishSequence)
    return ECrowdWorkerPublishedValidationResult::RejectedPublishSequence;

  FCrowdStableEntityRef PreviousRef;
  for (const FCrowdWorkerStatePatch& Patch : Batch.StatePatches)
  {
    if (!Patch.IsValid(
        ExpectedGeneration, Limits.MaxPayloadBytes)
      || Patch.WorkerEpoch < Batch.MinWorkerEpoch
      || Patch.WorkerEpoch > Batch.MaxWorkerEpoch
      || Patch.SourceInputSequence > Batch.LastAppliedInputSequence
      || (!PreviousRef.IsUnset()
        && !(PreviousRef < Patch.EntityRef)))
      return ECrowdWorkerPublishedValidationResult::RejectedStructure;
    PreviousRef = Patch.EntityRef;
  }

  uint64 PreviousEventSequence = 0;
  for (const FCrowdWorkerGameplayEvent& Event : Batch.OrderedEvents)
  {
    if (!Event.IsValid(
        ExpectedGeneration, Limits.MaxPayloadBytes)
      || Event.WorkerEpoch < Batch.MinWorkerEpoch
      || Event.WorkerEpoch > Batch.MaxWorkerEpoch
      || Event.SourceInputSequence > Batch.LastAppliedInputSequence
      || (PreviousEventSequence != 0
        && Event.EventSequence != PreviousEventSequence + 1))
      return ECrowdWorkerPublishedValidationResult::RejectedStructure;
    PreviousEventSequence = Event.EventSequence;
  }

  if (Batch.StableHash == 0
    || Batch.StableHash != Batch.CalculateStableHash())
    return ECrowdWorkerPublishedValidationResult::RejectedHash;
  return ECrowdWorkerPublishedValidationResult::Valid;
}
