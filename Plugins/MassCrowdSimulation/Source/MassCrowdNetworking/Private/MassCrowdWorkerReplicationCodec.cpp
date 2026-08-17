#include "MassCrowdWorkerReplicationCodec.h"

namespace CrowdWorkerReplicationCodecPrivate
{
constexpr uint32 Magic = 0x324e5743u;
constexpr uint16 CodecVersion = 2;
constexpr uint8 CheckpointKind = 1;
constexpr uint8 IntentKind = 2;
constexpr uint8 CorrectionKind = 3;

class FWriter
{
public:
  void U8(const uint8 Value) { Bytes.Add(Value); }
  void U16(const uint16 Value) { Unsigned(Value); }
  void U32(const uint32 Value) { Unsigned(Value); }
  void U64(const uint64 Value) { Unsigned(Value); }
  void I32(const int32 Value) { U32(static_cast<uint32>(Value)); }
  void Double(const double Value)
  {
    uint64 Bits = 0;
    static_assert(sizeof(Bits) == sizeof(Value));
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    U64(Bits);
  }
  void Raw(const TConstArrayView<uint8> Value)
  {
    Bytes.Append(Value.GetData(), Value.Num());
  }

  TArray<uint8> Bytes;

private:
  template <typename T>
  void Unsigned(const T Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }
};

class FReader
{
public:
  explicit FReader(const TConstArrayView<uint8> InBytes)
    : Bytes(InBytes)
  {
  }

  bool U8(uint8& OutValue)
  {
    if (Offset >= Bytes.Num()) return false;
    OutValue = Bytes[Offset++];
    return true;
  }
  bool U16(uint16& OutValue) { return Unsigned(OutValue); }
  bool U32(uint32& OutValue) { return Unsigned(OutValue); }
  bool U64(uint64& OutValue) { return Unsigned(OutValue); }
  bool I32(int32& OutValue)
  {
    uint32 Value = 0;
    if (!U32(Value)) return false;
    OutValue = static_cast<int32>(Value);
    return true;
  }
  bool Double(double& OutValue)
  {
    uint64 Bits = 0;
    if (!U64(Bits)) return false;
    FMemory::Memcpy(&OutValue, &Bits, sizeof(Bits));
    return FMath::IsFinite(OutValue);
  }
  bool Raw(const int32 Count, TArray<uint8>& OutValue)
  {
    if (Count < 0 || Count > Bytes.Num() - Offset) return false;
    OutValue.Reset(Count);
    OutValue.Append(Bytes.GetData() + Offset, Count);
    Offset += Count;
    return true;
  }
  bool IsAtEnd() const { return Offset == Bytes.Num(); }

private:
  template <typename T>
  bool Unsigned(T& OutValue)
  {
    if (static_cast<int64>(Offset) + sizeof(T) > Bytes.Num())
      return false;
    OutValue = 0;
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      OutValue |= static_cast<T>(Bytes[Offset++]) << (Byte * 8);
    return true;
  }

  TConstArrayView<uint8> Bytes;
  int32 Offset = 0;
};

void WriteRef(FWriter& Writer, const FCrowdStableEntityRef& Ref)
{
  Writer.U32(Ref.ProviderId);
  Writer.U64(Ref.StableEntityId);
  Writer.U32(Ref.LifecycleSerial);
}

bool ReadRef(FReader& Reader, FCrowdStableEntityRef& OutRef)
{
  return Reader.U32(OutRef.ProviderId)
    && Reader.U64(OutRef.StableEntityId)
    && Reader.U32(OutRef.LifecycleSerial);
}

void WritePayload(FWriter& Writer, const FCrowdWorkerPayload& Payload)
{
  Writer.U32(Payload.SchemaId);
  Writer.U16(Payload.SchemaVersion);
  Writer.U32(static_cast<uint32>(Payload.Bytes.Num()));
  Writer.Raw(Payload.Bytes);
  Writer.U64(Payload.StableHash);
}

bool ReadPayload(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerPayload& OutPayload)
{
  uint32 Count = 0;
  return Reader.U32(OutPayload.SchemaId)
    && Reader.U16(OutPayload.SchemaVersion)
    && Reader.U32(Count)
    && Count <= static_cast<uint32>(MaxPayloadBytes)
    && Reader.Raw(static_cast<int32>(Count), OutPayload.Bytes)
    && Reader.U64(OutPayload.StableHash)
    && OutPayload.IsValid(MaxPayloadBytes);
}

void WriteState(FWriter& Writer, const FCrowdWorkerDirtyStateRecord& Record)
{
  WriteRef(Writer, Record.EntityRef);
  Writer.U8(static_cast<uint8>(Record.Field));
  Writer.U64(Record.Generation);
  Writer.U64(Record.WorkerEpoch);
  Writer.U64(Record.StateRevision);
  Writer.U64(Record.CorrectionRevision);
  Writer.U64(Record.SourceInputSequence);
  WritePayload(Writer, Record.Payload);
}

bool ReadState(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerDirtyStateRecord& OutRecord)
{
  uint8 Field = 0;
  if (!ReadRef(Reader, OutRecord.EntityRef)
    || !Reader.U8(Field)
    || !Reader.U64(OutRecord.Generation)
    || !Reader.U64(OutRecord.WorkerEpoch)
    || !Reader.U64(OutRecord.StateRevision)
    || !Reader.U64(OutRecord.CorrectionRevision)
    || !Reader.U64(OutRecord.SourceInputSequence)
    || !ReadPayload(Reader, MaxPayloadBytes, OutRecord.Payload))
    return false;
  OutRecord.Field = static_cast<ECrowdWorkerField>(Field);
  return OutRecord.IsValid(MaxPayloadBytes);
}

void WriteResource(FWriter& Writer, const FCrowdWorkerResourceRecord& Record)
{
  Writer.U64(Record.ResourceId);
  Writer.U64(Record.Revision);
  WritePayload(Writer, Record.Payload);
}

bool ReadResource(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerResourceRecord& OutRecord)
{
  return Reader.U64(OutRecord.ResourceId)
    && Reader.U64(OutRecord.Revision)
    && ReadPayload(Reader, MaxPayloadBytes, OutRecord.Payload)
    && OutRecord.ResourceId != 0
    && OutRecord.Revision != 0;
}

void WriteWorkKey(FWriter& Writer, const FCrowdWorkerWorkKey& Key)
{
  Writer.U8(static_cast<uint8>(Key.Domain));
  Writer.U8(static_cast<uint8>(Key.Kind));
  WriteRef(Writer, Key.PrimaryEntity);
  WriteRef(Writer, Key.SecondaryEntity);
  Writer.U64(Key.ScopeKey);
}

bool ReadWorkKey(FReader& Reader, FCrowdWorkerWorkKey& OutKey)
{
  uint8 Domain = 0;
  uint8 Kind = 0;
  if (!Reader.U8(Domain) || !Reader.U8(Kind)
    || !ReadRef(Reader, OutKey.PrimaryEntity)
    || !ReadRef(Reader, OutKey.SecondaryEntity)
    || !Reader.U64(OutKey.ScopeKey))
    return false;
  OutKey.Domain = static_cast<ECrowdWorkerDomainId>(Domain);
  OutKey.Kind = static_cast<ECrowdWorkerWorkKind>(Kind);
  return true;
}

void WriteWork(FWriter& Writer, const FCrowdWorkerWorkItem& Work)
{
  WriteWorkKey(Writer, Work.Key);
  Writer.U8(static_cast<uint8>(Work.Priority));
  Writer.U64(Work.EnqueueEpoch);
  Writer.U64(Work.CorrectionRevision);
  Writer.U64(Work.ReasonMask);
}

bool ReadWork(FReader& Reader, FCrowdWorkerWorkItem& OutWork)
{
  uint8 Priority = 0;
  if (!ReadWorkKey(Reader, OutWork.Key)
    || !Reader.U8(Priority)
    || !Reader.U64(OutWork.EnqueueEpoch)
    || !Reader.U64(OutWork.CorrectionRevision)
    || !Reader.U64(OutWork.ReasonMask))
    return false;
  OutWork.Priority = static_cast<ECrowdWorkerWorkPriority>(Priority);
  return OutWork.IsValid();
}

void WriteWakeup(FWriter& Writer, const FCrowdWorkerWakeup& Wakeup)
{
  Writer.U8(static_cast<uint8>(Wakeup.Key.Domain));
  WriteRef(Writer, Wakeup.Key.EntityRef);
  Writer.U64(Wakeup.Key.WakeupId);
  Writer.U64(Wakeup.AbsoluteSimulationTick);
  Writer.U64(Wakeup.Revision);
  Writer.U8(static_cast<uint8>(Wakeup.Priority));
  Writer.U64(Wakeup.ReasonMask);
}

bool ReadWakeup(FReader& Reader, FCrowdWorkerWakeup& OutWakeup)
{
  uint8 Domain = 0;
  uint8 Priority = 0;
  if (!Reader.U8(Domain)
    || !ReadRef(Reader, OutWakeup.Key.EntityRef)
    || !Reader.U64(OutWakeup.Key.WakeupId)
    || !Reader.U64(OutWakeup.AbsoluteSimulationTick)
    || !Reader.U64(OutWakeup.Revision)
    || !Reader.U8(Priority)
    || !Reader.U64(OutWakeup.ReasonMask))
    return false;
  OutWakeup.Key.Domain = static_cast<ECrowdWorkerDomainId>(Domain);
  OutWakeup.Priority = static_cast<ECrowdWorkerWorkPriority>(Priority);
  return OutWakeup.IsValid();
}

void WriteDependency(
  FWriter& Writer,
  const FCrowdWorkerDependencyRecord& Record)
{
  Writer.U8(static_cast<uint8>(Record.Source.Kind));
  WriteRef(Writer, Record.Source.EntityRef);
  Writer.U64(Record.Source.ScopeKey);
  WriteWork(Writer, Record.Dependent);
}

bool ReadDependency(
  FReader& Reader,
  FCrowdWorkerDependencyRecord& OutRecord)
{
  uint8 Kind = 0;
  if (!Reader.U8(Kind)
    || !ReadRef(Reader, OutRecord.Source.EntityRef)
    || !Reader.U64(OutRecord.Source.ScopeKey)
    || !ReadWork(Reader, OutRecord.Dependent))
    return false;
  OutRecord.Source.Kind = static_cast<ECrowdWorkerDependencyKind>(Kind);
  return OutRecord.Source.IsValid();
}

void WriteCommand(FWriter& Writer, const FCrowdWorkerCommandRecord& Command)
{
  Writer.U64(Command.InputSequence);
  WriteRef(Writer, Command.EntityRef);
  Writer.U32(Command.CommandId);
  Writer.Double(Command.EffectiveSimulationTimeSeconds);
  WritePayload(Writer, Command.Payload);
}

bool ReadCommand(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerCommandRecord& OutCommand)
{
  return Reader.U64(OutCommand.InputSequence)
    && ReadRef(Reader, OutCommand.EntityRef)
    && Reader.U32(OutCommand.CommandId)
    && Reader.Double(OutCommand.EffectiveSimulationTimeSeconds)
    && ReadPayload(Reader, MaxPayloadBytes, OutCommand.Payload)
    && OutCommand.IsValid(MaxPayloadBytes);
}

void WriteContinuation(
  FWriter& Writer,
  const FCrowdWorkerNetworkContinuationState& Continuation)
{
  Writer.U64(Continuation.WorkRing.Epoch);
  Writer.U8(Continuation.WorkRing.FairDomainCursor);
  Writer.U32(static_cast<uint32>(Continuation.WorkRing.CurrentItems.Num()));
  for (const FCrowdWorkerWorkItem& Work :
    Continuation.WorkRing.CurrentItems)
    WriteWork(Writer, Work);
  Writer.U32(static_cast<uint32>(Continuation.WorkRing.NextItems.Num()));
  for (const FCrowdWorkerWorkItem& Work : Continuation.WorkRing.NextItems)
    WriteWork(Writer, Work);
  Writer.U32(static_cast<uint32>(Continuation.Wakeups.Num()));
  for (const FCrowdWorkerWakeup& Wakeup : Continuation.Wakeups)
    WriteWakeup(Writer, Wakeup);
  Writer.U32(static_cast<uint32>(Continuation.Dependencies.Num()));
  for (const FCrowdWorkerDependencyRecord& Dependency :
    Continuation.Dependencies)
    WriteDependency(Writer, Dependency);
  Writer.U32(static_cast<uint32>(Continuation.Commands.Num()));
  for (const FCrowdWorkerCommandRecord& Command : Continuation.Commands)
    WriteCommand(Writer, Command);
  Writer.U32(static_cast<uint32>(
    Continuation.LifecycleWatermarks.Num()));
  for (const FCrowdWorkerLifecycleWatermark& Watermark :
    Continuation.LifecycleWatermarks)
  {
    Writer.U32(Watermark.ProviderId);
    Writer.U64(Watermark.StableEntityId);
    Writer.U32(Watermark.LastLifecycleSerial);
  }
}

template <typename T, typename FRead>
bool ReadArray(
  FReader& Reader,
  const int32 MaxCount,
  TArray<T>& OutValues,
  FRead&& ReadValue)
{
  uint32 Count = 0;
  if (!Reader.U32(Count)
    || Count > static_cast<uint32>(MaxCount))
    return false;
  OutValues.SetNum(static_cast<int32>(Count));
  for (T& Value : OutValues)
    if (!ReadValue(Value)) return false;
  return true;
}

bool ReadContinuation(
  FReader& Reader,
  const FCrowdWorkerNetworkStateConfig& Config,
  FCrowdWorkerNetworkContinuationState& OutContinuation)
{
  if (!Reader.U64(OutContinuation.WorkRing.Epoch)
    || !Reader.U8(OutContinuation.WorkRing.FairDomainCursor)
    || !ReadArray(
      Reader,
      Config.MaxWorkItemsPerCheckpoint,
      OutContinuation.WorkRing.CurrentItems,
      [&Reader](FCrowdWorkerWorkItem& Work)
      {
        return ReadWork(Reader, Work);
      })
    || !ReadArray(
      Reader,
      Config.MaxWorkItemsPerCheckpoint,
      OutContinuation.WorkRing.NextItems,
      [&Reader](FCrowdWorkerWorkItem& Work)
      {
        return ReadWork(Reader, Work);
      })
    || OutContinuation.WorkRing.CurrentItems.Num()
        + OutContinuation.WorkRing.NextItems.Num()
      > Config.MaxWorkItemsPerCheckpoint
    || !ReadArray(
      Reader,
      Config.MaxWakeupsPerCheckpoint,
      OutContinuation.Wakeups,
      [&Reader](FCrowdWorkerWakeup& Wakeup)
      {
        return ReadWakeup(Reader, Wakeup);
      })
    || !ReadArray(
      Reader,
      Config.MaxDependencyEdgesPerCheckpoint,
      OutContinuation.Dependencies,
      [&Reader](FCrowdWorkerDependencyRecord& Dependency)
      {
        return ReadDependency(Reader, Dependency);
      })
    || !ReadArray(
      Reader,
      Config.MaxCommandsPerCheckpoint,
      OutContinuation.Commands,
      [&Reader, &Config](FCrowdWorkerCommandRecord& Command)
      {
        return ReadCommand(Reader, Config.MaxPayloadBytes, Command);
      })
    || !ReadArray(
      Reader,
      Config.MaxLifecycleWatermarksPerCheckpoint,
      OutContinuation.LifecycleWatermarks,
      [&Reader](FCrowdWorkerLifecycleWatermark& Watermark)
      {
        return Reader.U32(Watermark.ProviderId)
          && Reader.U64(Watermark.StableEntityId)
          && Reader.U32(Watermark.LastLifecycleSerial)
          && Watermark.ProviderId != 0
          && Watermark.StableEntityId != 0
          && Watermark.LastLifecycleSerial != 0;
      }))
    return false;
  return true;
}

void WriteHeader(FWriter& Writer, const FCrowdWorkerCheckpoint& Header)
{
  Writer.U64(Header.Generation);
  Writer.U64(Header.WorkerEpoch);
  Writer.U64(Header.AbsoluteSimulationTick);
  Writer.Double(Header.FixedSimulationQuantumSeconds);
  Writer.U64(Header.LastAppliedInputSequence);
  Writer.U64(Header.LastOrderedEventSequence);
  Writer.U64(Header.EntityStateHash);
  Writer.U64(Header.ResourceRevisionHash);
  Writer.U64(Header.StableHash);
}

bool ReadHeader(FReader& Reader, FCrowdWorkerCheckpoint& OutHeader)
{
  return Reader.U64(OutHeader.Generation)
    && Reader.U64(OutHeader.WorkerEpoch)
    && Reader.U64(OutHeader.AbsoluteSimulationTick)
    && Reader.Double(OutHeader.FixedSimulationQuantumSeconds)
    && Reader.U64(OutHeader.LastAppliedInputSequence)
    && Reader.U64(OutHeader.LastOrderedEventSequence)
    && Reader.U64(OutHeader.EntityStateHash)
    && Reader.U64(OutHeader.ResourceRevisionHash)
    && Reader.U64(OutHeader.StableHash)
    && OutHeader.IsValid();
}

void WriteSpawn(FWriter& Writer, const FCrowdWorkerSpawnDelta& Spawn)
{
  Writer.U64(Spawn.InputSequence);
  WriteRef(Writer, Spawn.EntityRef);
  WritePayload(Writer, Spawn.InitialState);
}

bool ReadSpawn(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerSpawnDelta& OutSpawn)
{
  return Reader.U64(OutSpawn.InputSequence)
    && ReadRef(Reader, OutSpawn.EntityRef)
    && ReadPayload(Reader, MaxPayloadBytes, OutSpawn.InitialState)
    && OutSpawn.IsValid(MaxPayloadBytes);
}

void WriteDespawn(FWriter& Writer, const FCrowdWorkerDespawnDelta& Despawn)
{
  Writer.U64(Despawn.InputSequence);
  WriteRef(Writer, Despawn.EntityRef);
  Writer.U32(Despawn.ReasonId);
}

bool ReadDespawn(FReader& Reader, FCrowdWorkerDespawnDelta& OutDespawn)
{
  return Reader.U64(OutDespawn.InputSequence)
    && ReadRef(Reader, OutDespawn.EntityRef)
    && Reader.U32(OutDespawn.ReasonId)
    && OutDespawn.IsValid();
}

void WriteIntentCommand(
  FWriter& Writer,
  const FCrowdWorkerCommandDelta& Command)
{
  Writer.U64(Command.InputSequence);
  WriteRef(Writer, Command.EntityRef);
  Writer.U32(Command.CommandId);
  Writer.Double(Command.EffectiveSimulationTimeSeconds);
  WritePayload(Writer, Command.Payload);
}

bool ReadIntentCommand(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerCommandDelta& OutCommand)
{
  return Reader.U64(OutCommand.InputSequence)
    && ReadRef(Reader, OutCommand.EntityRef)
    && Reader.U32(OutCommand.CommandId)
    && Reader.Double(OutCommand.EffectiveSimulationTimeSeconds)
    && ReadPayload(Reader, MaxPayloadBytes, OutCommand.Payload)
    && OutCommand.IsValid(MaxPayloadBytes);
}

void WriteObjective(
  FWriter& Writer,
  const FCrowdWorkerObjectiveRevisionDelta& Objective)
{
  Writer.U64(Objective.InputSequence);
  Writer.U64(Objective.ObjectiveId);
  Writer.U64(Objective.Revision);
  WritePayload(Writer, Objective.Payload);
}

bool ReadObjective(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerObjectiveRevisionDelta& OutObjective)
{
  return Reader.U64(OutObjective.InputSequence)
    && Reader.U64(OutObjective.ObjectiveId)
    && Reader.U64(OutObjective.Revision)
    && ReadPayload(Reader, MaxPayloadBytes, OutObjective.Payload)
    && OutObjective.IsValid(MaxPayloadBytes);
}

void WriteExternalGameplay(
  FWriter& Writer,
  const FCrowdWorkerExternalGameplayInput& Input)
{
  Writer.U64(Input.InputSequence);
  WriteRef(Writer, Input.EntityRef);
  Writer.U16(Input.InputTypeId);
  Writer.U64(Input.DirtyMask);
  WritePayload(Writer, Input.FullState);
}

bool ReadExternalGameplay(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerExternalGameplayInput& OutInput)
{
  return Reader.U64(OutInput.InputSequence)
    && ReadRef(Reader, OutInput.EntityRef)
    && Reader.U16(OutInput.InputTypeId)
    && Reader.U64(OutInput.DirtyMask)
    && ReadPayload(Reader, MaxPayloadBytes, OutInput.FullState)
    && OutInput.IsValid(MaxPayloadBytes);
}

void WriteResourceDelta(
  FWriter& Writer,
  const FCrowdWorkerResourceDelta& Resource)
{
  Writer.U64(Resource.InputSequence);
  Writer.U64(Resource.ResourceId);
  Writer.U64(Resource.Revision);
  WritePayload(Writer, Resource.Payload);
}

bool ReadResourceDelta(
  FReader& Reader,
  const int32 MaxPayloadBytes,
  FCrowdWorkerResourceDelta& OutResource)
{
  return Reader.U64(OutResource.InputSequence)
    && Reader.U64(OutResource.ResourceId)
    && Reader.U64(OutResource.Revision)
    && ReadPayload(Reader, MaxPayloadBytes, OutResource.Payload)
    && OutResource.IsValid(MaxPayloadBytes);
}

void WriteEvent(FWriter& Writer, const FCrowdWorkerGameplayEvent& Event)
{
  WriteRef(Writer, Event.EntityRef);
  Writer.U64(Event.Generation);
  Writer.U64(Event.WorkerEpoch);
  Writer.U64(Event.SourceInputSequence);
  Writer.U64(Event.EventSequence);
  Writer.U64(Event.EventId);
  WritePayload(Writer, Event.Payload);
  Writer.U64(Event.StableHash);
}

bool ReadEvent(
  FReader& Reader,
  const uint64 Generation,
  const int32 MaxPayloadBytes,
  FCrowdWorkerGameplayEvent& OutEvent)
{
  return ReadRef(Reader, OutEvent.EntityRef)
    && Reader.U64(OutEvent.Generation)
    && Reader.U64(OutEvent.WorkerEpoch)
    && Reader.U64(OutEvent.SourceInputSequence)
    && Reader.U64(OutEvent.EventSequence)
    && Reader.U64(OutEvent.EventId)
    && ReadPayload(Reader, MaxPayloadBytes, OutEvent.Payload)
    && Reader.U64(OutEvent.StableHash)
    && OutEvent.IsValid(Generation, MaxPayloadBytes);
}

void WritePrefix(FWriter& Writer, const uint8 Kind)
{
  Writer.U32(Magic);
  Writer.U16(CodecVersion);
  Writer.U8(Kind);
}

bool ReadPrefix(FReader& Reader, const uint8 ExpectedKind)
{
  uint32 ReadMagic = 0;
  uint16 Version = 0;
  uint8 Kind = 0;
  return Reader.U32(ReadMagic)
    && Reader.U16(Version)
    && Reader.U8(Kind)
    && ReadMagic == Magic
    && Version == CodecVersion
    && Kind == ExpectedKind;
}
}

using namespace CrowdWorkerReplicationCodecPrivate;

bool FCrowdWorkerReplicationCodec::EncodeCheckpoint(
  const FCrowdWorkerNetworkCheckpoint& Checkpoint,
  const FCrowdWorkerNetworkStateConfig& Config,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Checkpoint.IsValid(Config)) return false;
  FWriter Writer;
  WritePrefix(Writer, CheckpointKind);
  Writer.U16(Checkpoint.Version);
  WriteHeader(Writer, Checkpoint.Header);
  Writer.U64(Checkpoint.InputBaselineSequence);
  Writer.U64(Checkpoint.EventBaselineSequence);
  Writer.U64(Checkpoint.MaxCorrectionRevision);
  Writer.U32(static_cast<uint32>(Checkpoint.StateRecords.Num()));
  for (const FCrowdWorkerDirtyStateRecord& Record :
    Checkpoint.StateRecords)
    WriteState(Writer, Record);
  Writer.U32(static_cast<uint32>(Checkpoint.ResourceRecords.Num()));
  for (const FCrowdWorkerResourceRecord& Record :
    Checkpoint.ResourceRecords)
    WriteResource(Writer, Record);
  WriteContinuation(Writer, Checkpoint.Continuation);
  Writer.U64(Checkpoint.StableHash);
  if (Writer.Bytes.Num() > Config.MaxEncodedCheckpointBytes)
    return false;
  OutBytes = MoveTemp(Writer.Bytes);
  return true;
}

bool FCrowdWorkerReplicationCodec::DecodeCheckpoint(
  const TConstArrayView<uint8> Bytes,
  const FCrowdWorkerNetworkStateConfig& Config,
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint)
{
  OutCheckpoint = {};
  if (!Config.IsValid() || Bytes.IsEmpty()
    || Bytes.Num() > Config.MaxEncodedCheckpointBytes)
    return false;
  FReader Reader(Bytes);
  if (!ReadPrefix(Reader, CheckpointKind)
    || !Reader.U16(OutCheckpoint.Version)
    || !ReadHeader(Reader, OutCheckpoint.Header)
    || !Reader.U64(OutCheckpoint.InputBaselineSequence)
    || !Reader.U64(OutCheckpoint.EventBaselineSequence)
    || !Reader.U64(OutCheckpoint.MaxCorrectionRevision)
    || !ReadArray(
      Reader,
      Config.MaxStateRecordsPerCheckpoint,
      OutCheckpoint.StateRecords,
      [&Reader, &Config](FCrowdWorkerDirtyStateRecord& Record)
      {
        return ReadState(Reader, Config.MaxPayloadBytes, Record);
      })
    || !ReadArray(
      Reader,
      Config.MaxResourceRecordsPerCheckpoint,
      OutCheckpoint.ResourceRecords,
      [&Reader, &Config](FCrowdWorkerResourceRecord& Record)
      {
        return ReadResource(Reader, Config.MaxPayloadBytes, Record);
      })
    || !ReadContinuation(Reader, Config, OutCheckpoint.Continuation)
    || !Reader.U64(OutCheckpoint.StableHash)
    || !Reader.IsAtEnd()
    || !OutCheckpoint.IsValid(Config))
  {
    OutCheckpoint = {};
    return false;
  }
  return true;
}

bool FCrowdWorkerReplicationCodec::EncodeIntent(
  const FCrowdWorkerIntentBatch& Batch,
  const FCrowdWorkerNetworkStateConfig& Config,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  const FCrowdWorkerContractLimits Limits{
    Config.MaxPayloadBytes,
    Config.MaxIntentRecordsPerBatch,
    1,
    1};
  // Authority correction is a separate packet contract. It must never be
  // smuggled into the reliable intent stream.
  if (!Config.IsValid() || !Batch.IsValid(Limits))
    return false;

  FWriter Writer;
  WritePrefix(Writer, IntentKind);
  Writer.U32(Batch.Version);
  Writer.U64(Batch.Generation);
  Writer.U64(Batch.FirstInputSequence);
  Writer.U64(Batch.LastInputSequence);
  Writer.Double(Batch.TargetSimulationTimeSeconds);
  Writer.U64(Batch.Clock.InputSequence);
  Writer.U64(Batch.Clock.SimulationTick);
  Writer.U32(static_cast<uint32>(Batch.Spawns.Num()));
  for (const FCrowdWorkerSpawnDelta& Value : Batch.Spawns)
    WriteSpawn(Writer, Value);
  Writer.U32(static_cast<uint32>(Batch.Despawns.Num()));
  for (const FCrowdWorkerDespawnDelta& Value : Batch.Despawns)
    WriteDespawn(Writer, Value);
  Writer.U32(static_cast<uint32>(Batch.Commands.Num()));
  for (const FCrowdWorkerCommandDelta& Value : Batch.Commands)
    WriteIntentCommand(Writer, Value);
  Writer.U32(static_cast<uint32>(Batch.ObjectiveRevisions.Num()));
  for (const FCrowdWorkerObjectiveRevisionDelta& Value :
    Batch.ObjectiveRevisions)
    WriteObjective(Writer, Value);
  Writer.U32(static_cast<uint32>(Batch.ExternalGameplayInputs.Num()));
  for (const FCrowdWorkerExternalGameplayInput& Value :
    Batch.ExternalGameplayInputs)
    WriteExternalGameplay(Writer, Value);
  Writer.U32(static_cast<uint32>(Batch.ResourceDeltas.Num()));
  for (const FCrowdWorkerResourceDelta& Value : Batch.ResourceDeltas)
    WriteResourceDelta(Writer, Value);
  Writer.U64(Batch.StableHash);
  if (Writer.Bytes.Num() > Config.MaxEncodedIntentBytes)
    return false;
  OutBytes = MoveTemp(Writer.Bytes);
  return true;
}

bool FCrowdWorkerReplicationCodec::DecodeIntent(
  const TConstArrayView<uint8> Bytes,
  const FCrowdWorkerNetworkStateConfig& Config,
  FCrowdWorkerIntentBatch& OutBatch)
{
  OutBatch = {};
  if (!Config.IsValid() || Bytes.IsEmpty()
    || Bytes.Num() > Config.MaxEncodedIntentBytes)
    return false;
  FReader Reader(Bytes);
  if (!ReadPrefix(Reader, IntentKind)
    || !Reader.U32(OutBatch.Version)
    || !Reader.U64(OutBatch.Generation)
    || !Reader.U64(OutBatch.FirstInputSequence)
    || !Reader.U64(OutBatch.LastInputSequence)
    || !Reader.Double(OutBatch.TargetSimulationTimeSeconds)
    || !Reader.U64(OutBatch.Clock.InputSequence)
    || !Reader.U64(OutBatch.Clock.SimulationTick)
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.Spawns,
      [&Reader, &Config](FCrowdWorkerSpawnDelta& Value)
      {
        return ReadSpawn(Reader, Config.MaxPayloadBytes, Value);
      })
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.Despawns,
      [&Reader](FCrowdWorkerDespawnDelta& Value)
      {
        return ReadDespawn(Reader, Value);
      })
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.Commands,
      [&Reader, &Config](FCrowdWorkerCommandDelta& Value)
      {
        return ReadIntentCommand(
          Reader, Config.MaxPayloadBytes, Value);
      })
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.ObjectiveRevisions,
      [&Reader, &Config](FCrowdWorkerObjectiveRevisionDelta& Value)
      {
        return ReadObjective(Reader, Config.MaxPayloadBytes, Value);
      })
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.ExternalGameplayInputs,
      [&Reader, &Config](FCrowdWorkerExternalGameplayInput& Value)
      {
        return ReadExternalGameplay(
          Reader, Config.MaxPayloadBytes, Value);
      })
    || !ReadArray(
      Reader,
      Config.MaxIntentRecordsPerBatch,
      OutBatch.ResourceDeltas,
      [&Reader, &Config](FCrowdWorkerResourceDelta& Value)
      {
        return ReadResourceDelta(Reader, Config.MaxPayloadBytes, Value);
      })
    || !Reader.U64(OutBatch.StableHash)
    || !Reader.IsAtEnd())
  {
    OutBatch = {};
    return false;
  }
  const FCrowdWorkerContractLimits Limits{
    Config.MaxPayloadBytes,
    Config.MaxIntentRecordsPerBatch,
    1,
    1};
  if (!OutBatch.IsValid(Limits))
  {
    OutBatch = {};
    return false;
  }
  return true;
}

bool FCrowdWorkerReplicationCodec::EncodeCorrection(
  const FCrowdWorkerAuthorityCorrectionBatch& Correction,
  const FCrowdWorkerNetworkStateConfig& Config,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Correction.IsValid(Config)) return false;
  FWriter Writer;
  WritePrefix(Writer, CorrectionKind);
  Writer.U16(Correction.Version);
  Writer.U64(Correction.Generation);
  Writer.U64(Correction.CorrectionSequence);
  Writer.U64(Correction.ApplySimulationTick);
  Writer.U64(Correction.ThroughInputSequence);
  Writer.U32(static_cast<uint32>(Correction.Scopes.Num()));
  for (const FCrowdWorkerAuthorityScopeKey& Scope : Correction.Scopes)
  {
    Writer.U8(static_cast<uint8>(Scope.Field));
    Writer.U8(static_cast<uint8>(Scope.Kind));
    Writer.U64(static_cast<uint64>(Scope.ScopeId));
  }
  Writer.U32(static_cast<uint32>(
    Correction.AuthoritativeMembers.Num()));
  for (const FCrowdStableEntityRef& Member :
    Correction.AuthoritativeMembers)
    WriteRef(Writer, Member);
  Writer.U32(static_cast<uint32>(Correction.Records.Num()));
  for (const FCrowdWorkerDirtyStateRecord& Record : Correction.Records)
    WriteState(Writer, Record);
  Writer.U32(static_cast<uint32>(Correction.Tombstones.Num()));
  for (const FCrowdWorkerAuthorityTombstone& Tombstone :
    Correction.Tombstones)
  {
    WriteRef(Writer, Tombstone.EntityRef);
    Writer.U8(static_cast<uint8>(Tombstone.Field));
  }
  Writer.U64(Correction.StableHash);
  if (Writer.Bytes.Num() > Config.MaxEncodedCorrectionBytes)
    return false;
  OutBytes = MoveTemp(Writer.Bytes);
  return true;
}

bool FCrowdWorkerReplicationCodec::DecodeCorrection(
  const TConstArrayView<uint8> Bytes,
  const FCrowdWorkerNetworkStateConfig& Config,
  FCrowdWorkerAuthorityCorrectionBatch& OutCorrection)
{
  OutCorrection = {};
  if (!Config.IsValid() || Bytes.IsEmpty()
    || Bytes.Num() > Config.MaxEncodedCorrectionBytes)
    return false;
  FReader Reader(Bytes);
  if (!ReadPrefix(Reader, CorrectionKind)
    || !Reader.U16(OutCorrection.Version)
    || !Reader.U64(OutCorrection.Generation)
    || !Reader.U64(OutCorrection.CorrectionSequence)
    || !Reader.U64(OutCorrection.ApplySimulationTick)
    || !Reader.U64(OutCorrection.ThroughInputSequence)
    || !ReadArray(
      Reader,
      Config.MaxCorrectionScopes,
      OutCorrection.Scopes,
      [&Reader](FCrowdWorkerAuthorityScopeKey& Scope)
      {
        uint8 Field = 0;
        uint8 Kind = 0;
        uint64 ScopeId = 0;
        if (!Reader.U8(Field)
          || !Reader.U8(Kind)
          || !Reader.U64(ScopeId))
          return false;
        Scope.Field = static_cast<ECrowdWorkerField>(Field);
        Scope.Kind = static_cast<ECrowdWorkerAuthorityScopeKind>(Kind);
        Scope.ScopeId = static_cast<int64>(ScopeId);
        return Scope.IsValid();
      })
    || !ReadArray(
      Reader,
      Config.MaxCorrectionEntities,
      OutCorrection.AuthoritativeMembers,
      [&Reader](FCrowdStableEntityRef& Ref)
      {
        return ReadRef(Reader, Ref) && Ref.IsValid();
      })
    || !ReadArray(
      Reader,
      Config.MaxCorrectionEntities,
      OutCorrection.Records,
      [&Reader, &Config](FCrowdWorkerDirtyStateRecord& Record)
      {
        return ReadState(Reader, Config.MaxPayloadBytes, Record);
      })
    || !ReadArray(
      Reader,
      Config.MaxCorrectionEntities,
      OutCorrection.Tombstones,
      [&Reader](FCrowdWorkerAuthorityTombstone& Tombstone)
      {
        uint8 Field = 0;
        if (!ReadRef(Reader, Tombstone.EntityRef)
          || !Reader.U8(Field))
          return false;
        Tombstone.Field = static_cast<ECrowdWorkerField>(Field);
        return Tombstone.EntityRef.IsValid()
          && Tombstone.Field < ECrowdWorkerField::Count;
      })
    || !Reader.U64(OutCorrection.StableHash)
    || !Reader.IsAtEnd()
    || !OutCorrection.IsValid(Config))
  {
    OutCorrection = {};
    return false;
  }
  return true;
}
