#include "MassCrowdReplicationChannel.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  uint64 Fold(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint64 FoldRef(uint64 Hash, const FCrowdStableEntityRef& Ref)
  {
    Hash = Fold(Hash, Ref.ProviderId);
    Hash = Fold(Hash, Ref.StableEntityId);
    return Fold(Hash, Ref.LifecycleSerial);
  }

  uint64 FoldBytes(uint64 Hash, const TConstArrayView<uint8> Bytes)
  {
    Hash = Fold(Hash, Bytes.Num());
    for (const uint8 Byte : Bytes)
    {
      Hash ^= Byte;
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint64 FoldFloat(uint64 Hash, const float Value)
  {
    return Fold(Hash, static_cast<uint32>(
      FMath::RoundToInt(Value * 100.0f)));
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  void WriteU16(TArray<uint8>& Out, const uint16 Value)
  {
    Out.Add(static_cast<uint8>(Value & 0xffu));
    Out.Add(static_cast<uint8>((Value >> 8) & 0xffu));
  }

  void WriteU32(TArray<uint8>& Out, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
      Out.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffu));
  }

  void WriteI16(TArray<uint8>& Out, const int16 Value)
  {
    WriteU16(Out, static_cast<uint16>(Value));
  }

  void WriteU64(TArray<uint8>& Out, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
      Out.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xffull));
  }

  void WriteI64(TArray<uint8>& Out, const int64 Value)
  {
    WriteU64(Out, static_cast<uint64>(Value));
  }

  void WriteI32(TArray<uint8>& Out, const int32 Value)
  {
    WriteU32(Out, static_cast<uint32>(Value));
  }

  void WriteFloat(TArray<uint8>& Out, const float Value)
  {
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    WriteU32(Out, Bits);
  }

  void WriteDouble(TArray<uint8>& Out, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    WriteU64(Out, Bits);
  }

  void WriteRef(
    TArray<uint8>& Out, const FCrowdStableEntityRef& Ref)
  {
    WriteU32(Out, Ref.ProviderId);
    WriteU64(Out, Ref.StableEntityId);
    WriteU32(Out, Ref.LifecycleSerial);
  }

  void WriteVector(TArray<uint8>& Out, const FVector& Value)
  {
    WriteDouble(Out, Value.X);
    WriteDouble(Out, Value.Y);
    WriteDouble(Out, Value.Z);
  }

  struct FByteReader
  {
    explicit FByteReader(const TConstArrayView<uint8> InBytes)
      : Bytes(InBytes)
    {
    }

    bool ReadU8(uint8& Out)
    {
      if (Offset >= Bytes.Num()) return false;
      Out = Bytes[Offset++];
      return true;
    }

    bool ReadU16(uint16& Out)
    {
      if (Offset + 2 > Bytes.Num()) return false;
      Out = static_cast<uint16>(Bytes[Offset])
        | static_cast<uint16>(Bytes[Offset + 1]) << 8;
      Offset += 2;
      return true;
    }

    bool ReadU32(uint32& Out)
    {
      if (Offset + 4 > Bytes.Num()) return false;
      Out = 0;
      for (int32 Byte = 0; Byte < 4; ++Byte)
        Out |= static_cast<uint32>(Bytes[Offset + Byte]) << (Byte * 8);
      Offset += 4;
      return true;
    }

    bool ReadI16(int16& Out)
    {
      uint16 Value = 0;
      if (!ReadU16(Value)) return false;
      Out = static_cast<int16>(Value);
      return true;
    }

    bool ReadI64(int64& Out)
    {
      uint64 Value = 0;
      if (!ReadU64(Value)) return false;
      Out = static_cast<int64>(Value);
      return true;
    }

    bool ReadU64(uint64& Out)
    {
      if (Offset + 8 > Bytes.Num()) return false;
      Out = 0;
      for (int32 Byte = 0; Byte < 8; ++Byte)
        Out |= static_cast<uint64>(Bytes[Offset + Byte]) << (Byte * 8);
      Offset += 8;
      return true;
    }

    bool ReadI32(int32& Out)
    {
      uint32 Value = 0;
      if (!ReadU32(Value)) return false;
      Out = static_cast<int32>(Value);
      return true;
    }

    bool ReadFloat(float& Out)
    {
      uint32 Bits = 0;
      if (!ReadU32(Bits)) return false;
      FMemory::Memcpy(&Out, &Bits, sizeof(Out));
      return FMath::IsFinite(Out);
    }

    bool ReadDouble(double& Out)
    {
      uint64 Bits = 0;
      if (!ReadU64(Bits)) return false;
      FMemory::Memcpy(&Out, &Bits, sizeof(Out));
      return FMath::IsFinite(Out);
    }

    bool ReadRef(FCrowdStableEntityRef& Out)
    {
      return ReadU32(Out.ProviderId)
        && ReadU64(Out.StableEntityId)
        && ReadU32(Out.LifecycleSerial);
    }

    bool ReadVector(FVector& Out)
    {
      return ReadDouble(Out.X)
        && ReadDouble(Out.Y)
        && ReadDouble(Out.Z);
    }

    bool ReadPayload(FCrowdBehaviorSourcePayload& Out)
    {
      Out = {};
      if (!ReadU32(Out.SchemaId)
        || !ReadU16(Out.Size)
        || Out.Size > CrowdBehavior::MaxPayloadBytes
        || Offset + Out.Size > Bytes.Num())
        return false;
      if (Out.Size > 0)
      {
        FMemory::Memcpy(Out.Bytes, Bytes.GetData() + Offset, Out.Size);
        Offset += Out.Size;
      }
      return Out.IsValid();
    }

    bool AtEnd() const { return Offset == Bytes.Num(); }

    TConstArrayView<uint8> Bytes;
    int32 Offset = 0;
  };

  void WritePayload(
    TArray<uint8>& Out, const FCrowdBehaviorSourcePayload& Payload)
  {
    WriteU32(Out, Payload.SchemaId);
    WriteU16(Out, Payload.Size);
    Out.Append(Payload.Bytes, Payload.Size);
  }

  constexpr uint16 CodecVersion = 2;
}

bool FCrowdReplicationChannelLimits::IsValid() const
{
  return MaxReliableRecordsPerBatch > 0
    && MaxReliablePayloadBytesPerRecord > 0
    && MaxBufferedReliableRecords > 0
    && MaxCorrectionRecords > 0
    && MaxPendingApplyFrames > 0
    && MaxDuplicateHistoryRecords > 0
    && SnapshotLimits.IsValid();
}

uint64 FCrowdReplicationTransport::CalculateReliableRecordHash(
  const FCrowdReliableStateRecord& Record)
{
  uint64 Hash = Fold(FnvOffset, CodecVersion);
  Hash = Fold(Hash, Record.Sequence);
  Hash = Fold(Hash, static_cast<uint8>(Record.Kind));
  Hash = FoldRef(Hash, Record.EntityRef);
  Hash = Fold(Hash, Record.Revision);
  return FoldBytes(Hash, Record.Payload);
}

uint64 FCrowdReplicationTransport::CalculateReliableBatchHash(
  const FCrowdReliableStateBatch& Batch)
{
  uint64 Hash = Fold(FnvOffset, CodecVersion);
  Hash = Fold(Hash, Batch.FirstSequence);
  Hash = Fold(Hash, Batch.Records.Num());
  for (const FCrowdReliableStateRecord& Record : Batch.Records)
    Hash = Fold(Hash, Record.StableHash);
  return Hash;
}

uint64 FCrowdReplicationTransport::CalculateMovementCorrectionHash(
  const FCrowdMovementCorrectionRecord& Record)
{
  uint64 Hash = Fold(FnvOffset, CodecVersion);
  Hash = FoldRef(Hash, Record.EntityRef);
  Hash = Fold(Hash, Record.Sequence);
  Hash = Fold(Hash, Record.FixedStepIndex);
  Hash = FoldFloat(Hash, static_cast<float>(Record.Position.X));
  Hash = FoldFloat(Hash, static_cast<float>(Record.Position.Y));
  Hash = FoldFloat(Hash, static_cast<float>(Record.Position.Z));
  Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.X));
  Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.Y));
  Hash = FoldFloat(Hash, static_cast<float>(Record.Velocity.Z));
  return FoldFloat(Hash, Record.YawDegrees);
}

uint64 FCrowdReplicationTransport::CalculateApplyFrameHash(
  const FCrowdReplicationApplyFrame& Frame)
{
  uint64 Hash = Fold(FnvOffset, Frame.Version);
  Hash = Fold(Hash, static_cast<uint8>(Frame.Kind));
  Hash = Fold(Hash, Frame.BaselineRevision);
  Hash = Fold(Hash, Frame.FirstReliableSequence);
  Hash = Fold(Hash, Frame.BaselineEntities.Num());
  for (const FCrowdRelevantSnapshotEntityPayload& Entity
    : Frame.BaselineEntities)
    Hash = FoldBytes(Hash, Entity.Bytes);
  Hash = Fold(Hash, Frame.ReliableRecords.Num());
  for (const FCrowdReliableStateRecord& Record : Frame.ReliableRecords)
    Hash = Fold(Hash, Record.StableHash);
  Hash = Fold(Hash, Frame.Corrections.Num());
  for (const FCrowdMovementCorrectionRecord& Correction
    : Frame.Corrections)
    Hash = Fold(Hash, Correction.StableHash);
  return Hash;
}

bool FCrowdReplicationCodec::EncodeAgent(
  const FCrowdAgentReplicationRecord& Record, TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Record.EntityRef.IsValid()
    || !IsFiniteVector(Record.Position)
    || !IsFiniteVector(Record.Velocity)
    || !FMath::IsFinite(Record.YawDegrees))
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Record.EntityRef);
  WriteVector(OutBytes, Record.Position);
  WriteVector(OutBytes, Record.Velocity);
  WriteFloat(OutBytes, Record.YawDegrees);
  WriteU32(OutBytes, Record.MovementProfileKey);
  WriteU32(OutBytes, Record.CapabilityProfileKey.Value);
  WriteU32(OutBytes, Record.CapabilityModifierRevision);
  WriteU32(OutBytes, Record.SourceSetRevision);
  WriteU64(OutBytes, Record.SourceSetHash);
  WriteU64(OutBytes, Record.ResolvedBehaviorHash);
  WriteU32(OutBytes, Record.DerivedDiagnosticLabel);
  WriteU32(OutBytes, Record.Revision);
  return true;
}

bool FCrowdReplicationCodec::DecodeAgent(
  const TConstArrayView<uint8> Bytes,
  FCrowdAgentReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(OutRecord.EntityRef)
    || !Reader.ReadVector(OutRecord.Position)
    || !Reader.ReadVector(OutRecord.Velocity)
    || !Reader.ReadFloat(OutRecord.YawDegrees)
    || !Reader.ReadU32(OutRecord.MovementProfileKey)
    || !Reader.ReadU32(OutRecord.CapabilityProfileKey.Value)
    || !Reader.ReadU32(OutRecord.CapabilityModifierRevision)
    || !Reader.ReadU32(OutRecord.SourceSetRevision)
    || !Reader.ReadU64(OutRecord.SourceSetHash)
    || !Reader.ReadU64(OutRecord.ResolvedBehaviorHash)
    || !Reader.ReadU32(OutRecord.DerivedDiagnosticLabel)
    || !Reader.ReadU32(OutRecord.Revision)
    || !Reader.AtEnd() || !OutRecord.EntityRef.IsValid()
    || !OutRecord.CapabilityProfileKey.IsValid()
    || OutRecord.SourceSetRevision == 0
    || OutRecord.SourceSetHash == 0
    || OutRecord.ResolvedBehaviorHash == 0)
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodeBehaviorSourceCommand(
  const FCrowdBehaviorSourceCommand& Command,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Command.IsValid()) return false;
  WriteU16(OutBytes, CodecVersion);
  WriteI64(OutBytes, Command.EffectiveFixedStep);
  WriteRef(OutBytes, Command.Handle.EntityRef);
  WriteU32(OutBytes, Command.Handle.ControllerId.Value);
  WriteU32(OutBytes, Command.Handle.SourceSequence);
  WriteU32(OutBytes, Command.CommandSequence);
  OutBytes.Add(static_cast<uint8>(Command.Kind));
  WriteU32(OutBytes, Command.SourceTypeId.Value);
  WriteI16(OutBytes, Command.Priority);
  WriteI32(OutBytes, Command.LifetimeSteps);
  WritePayload(OutBytes, Command.Payload);
  return true;
}

bool FCrowdReplicationCodec::DecodeBehaviorSourceCommand(
  const TConstArrayView<uint8> Bytes,
  FCrowdBehaviorSourceCommand& OutCommand)
{
  OutCommand = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  uint8 Kind = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadI64(OutCommand.EffectiveFixedStep)
    || !Reader.ReadRef(OutCommand.Handle.EntityRef)
    || !Reader.ReadU32(OutCommand.Handle.ControllerId.Value)
    || !Reader.ReadU32(OutCommand.Handle.SourceSequence)
    || !Reader.ReadU32(OutCommand.CommandSequence)
    || !Reader.ReadU8(Kind)
    || !Reader.ReadU32(OutCommand.SourceTypeId.Value)
    || !Reader.ReadI16(OutCommand.Priority)
    || !Reader.ReadI32(OutCommand.LifetimeSteps)
    || !Reader.ReadPayload(OutCommand.Payload)
    || !Reader.AtEnd())
  {
    OutCommand = {};
    return false;
  }
  OutCommand.Kind =
    static_cast<ECrowdBehaviorSourceCommandKind>(Kind);
  if (!OutCommand.IsValid())
  {
    OutCommand = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodeBehaviorSourceSet(
  const FCrowdBehaviorSourceSetReplicationRecord& Record,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  const FCrowdBehaviorSourceSet& Set = Record.SourceSet;
  if (!Set.IsValid() || Record.ResolvedBehaviorHash == 0)
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Set.EntityRef);
  WriteU32(OutBytes, Set.CapabilityBinding.ProfileKey.Value);
  WriteU32(OutBytes, Set.CapabilityBinding.ModifierRevision);
  OutBytes.Add(Set.CapabilityBinding.ModifierCount);
  for (uint8 Index = 0;
    Index < Set.CapabilityBinding.ModifierCount; ++Index)
  {
    WriteU32(OutBytes,
      Set.CapabilityBinding.Modifiers[Index].CapabilityId.Value);
    OutBytes.Add(static_cast<uint8>(
      Set.CapabilityBinding.Modifiers[Index].Operation));
  }
  WriteU32(OutBytes, Set.Revision);
  WriteU64(OutBytes, Set.StableHash);
  OutBytes.Add(static_cast<uint8>(Set.Instances.Num()));
  for (const FCrowdBehaviorSourceInstance& Instance : Set.Instances)
  {
    WriteU32(OutBytes, Instance.Handle.ControllerId.Value);
    WriteU32(OutBytes, Instance.Handle.SourceSequence);
    WriteU32(OutBytes, Instance.SourceTypeId.Value);
    WriteU16(OutBytes, Instance.SourceVersion);
    WriteI16(OutBytes, Instance.Priority);
    WriteU16(OutBytes, Instance.ExclusiveGroup);
    WriteI64(OutBytes, Instance.StartFixedStep);
    WriteI64(OutBytes, Instance.LastUpdateFixedStep);
    WriteI64(OutBytes, Instance.ExpireFixedStep);
    OutBytes.Add(static_cast<uint8>(Instance.ReplicationPolicy));
    WritePayload(OutBytes, Instance.Payload);
  }
  OutBytes.Add(static_cast<uint8>(Set.ControllerCursors.Num()));
  for (const FCrowdBehaviorControllerCursor& Cursor
    : Set.ControllerCursors)
  {
    WriteU32(OutBytes, Cursor.ControllerId.Value);
    WriteU32(OutBytes, Cursor.LastCommandSequence);
    WriteU64(OutBytes, Cursor.LastCommandHash);
  }
  WriteU64(OutBytes, Record.ResolvedBehaviorHash);
  WriteU32(OutBytes, Record.DerivedDiagnosticLabel);
  return true;
}

bool FCrowdReplicationCodec::DecodeBehaviorSourceSet(
  const TConstArrayView<uint8> Bytes,
  FCrowdBehaviorSourceSetReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  FCrowdBehaviorSourceSet& Set = OutRecord.SourceSet;
  uint16 Version = 0;
  uint8 ModifierCount = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(Set.EntityRef)
    || !Reader.ReadU32(Set.CapabilityBinding.ProfileKey.Value)
    || !Reader.ReadU32(Set.CapabilityBinding.ModifierRevision)
    || !Reader.ReadU8(ModifierCount)
    || ModifierCount > CrowdBehavior::MaxCapabilityModifiers)
    return false;
  Set.CapabilityBinding.ModifierCount = ModifierCount;
  for (uint8 Index = 0; Index < ModifierCount; ++Index)
  {
    uint8 Operation = 0;
    if (!Reader.ReadU32(
        Set.CapabilityBinding.Modifiers[Index].CapabilityId.Value)
      || !Reader.ReadU8(Operation))
    {
      OutRecord = {};
      return false;
    }
    Set.CapabilityBinding.Modifiers[Index].Operation =
      static_cast<ECrowdCapabilityModifierOperation>(Operation);
  }
  uint64 EncodedSourceSetHash = 0;
  uint8 InstanceCount = 0;
  if (!Reader.ReadU32(Set.Revision)
    || !Reader.ReadU64(EncodedSourceSetHash)
    || !Reader.ReadU8(InstanceCount)
    || InstanceCount > CrowdBehavior::MaxSourcesPerEntity)
  {
    OutRecord = {};
    return false;
  }
  Set.Instances.Reserve(InstanceCount);
  for (uint8 Index = 0; Index < InstanceCount; ++Index)
  {
    FCrowdBehaviorSourceInstance& Instance =
      Set.Instances.AddDefaulted_GetRef();
    Instance.Handle.EntityRef = Set.EntityRef;
    uint8 ReplicationPolicy = 0;
    if (!Reader.ReadU32(Instance.Handle.ControllerId.Value)
      || !Reader.ReadU32(Instance.Handle.SourceSequence)
      || !Reader.ReadU32(Instance.SourceTypeId.Value)
      || !Reader.ReadU16(Instance.SourceVersion)
      || !Reader.ReadI16(Instance.Priority)
      || !Reader.ReadU16(Instance.ExclusiveGroup)
      || !Reader.ReadI64(Instance.StartFixedStep)
      || !Reader.ReadI64(Instance.LastUpdateFixedStep)
      || !Reader.ReadI64(Instance.ExpireFixedStep)
      || !Reader.ReadU8(ReplicationPolicy)
      || !Reader.ReadPayload(Instance.Payload))
    {
      OutRecord = {};
      return false;
    }
    Instance.ReplicationPolicy =
      static_cast<ECrowdBehaviorSourceReplicationPolicy>(
        ReplicationPolicy);
  }
  uint8 CursorCount = 0;
  if (!Reader.ReadU8(CursorCount)
    || CursorCount > CrowdBehavior::MaxControllersPerEntity)
  {
    OutRecord = {};
    return false;
  }
  Set.ControllerCursors.Reserve(CursorCount);
  for (uint8 Index = 0; Index < CursorCount; ++Index)
  {
    FCrowdBehaviorControllerCursor& Cursor =
      Set.ControllerCursors.AddDefaulted_GetRef();
    if (!Reader.ReadU32(Cursor.ControllerId.Value)
      || !Reader.ReadU32(Cursor.LastCommandSequence)
      || !Reader.ReadU64(Cursor.LastCommandHash))
    {
      OutRecord = {};
      return false;
    }
  }
  if (!Reader.ReadU64(OutRecord.ResolvedBehaviorHash)
    || !Reader.ReadU32(OutRecord.DerivedDiagnosticLabel)
    || !Reader.AtEnd())
  {
    OutRecord = {};
    return false;
  }
  Set.RecalculateStableHash();
  if (!Set.IsValid()
    || Set.StableHash != EncodedSourceSetHash
    || OutRecord.ResolvedBehaviorHash == 0)
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodeTask(
  const FCrowdTaskReplicationRecord& Record, TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Record.TaskRef.IsValid() || !Record.SourceRef.IsValid()
    || !Record.SinkRef.IsValid() || Record.Quantity <= 0)
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Record.TaskRef);
  WriteRef(OutBytes, Record.SourceRef);
  WriteRef(OutBytes, Record.SinkRef);
  WriteRef(OutBytes, Record.CarrierRef);
  WriteI32(OutBytes, Record.Quantity);
  WriteU32(OutBytes, Record.State);
  WriteU32(OutBytes, Record.Revision);
  return true;
}

bool FCrowdReplicationCodec::DecodeTask(
  const TConstArrayView<uint8> Bytes,
  FCrowdTaskReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(OutRecord.TaskRef)
    || !Reader.ReadRef(OutRecord.SourceRef)
    || !Reader.ReadRef(OutRecord.SinkRef)
    || !Reader.ReadRef(OutRecord.CarrierRef)
    || !Reader.ReadI32(OutRecord.Quantity)
    || !Reader.ReadU32(OutRecord.State)
    || !Reader.ReadU32(OutRecord.Revision)
    || !Reader.AtEnd() || !OutRecord.TaskRef.IsValid()
    || !OutRecord.SourceRef.IsValid() || !OutRecord.SinkRef.IsValid()
    || OutRecord.Quantity <= 0)
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodeInventory(
  const FCrowdInventoryReplicationRecord& Record,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Record.OwnerRef.IsValid() || Record.OnHand < 0
    || Record.ReservedInbound < 0 || Record.ReservedOutbound < 0
    || Record.InTransit < 0 || Record.Capacity < 0
    || Record.OnHand > Record.Capacity)
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Record.OwnerRef);
  WriteI32(OutBytes, Record.OnHand);
  WriteI32(OutBytes, Record.ReservedInbound);
  WriteI32(OutBytes, Record.ReservedOutbound);
  WriteI32(OutBytes, Record.InTransit);
  WriteI32(OutBytes, Record.Capacity);
  WriteU32(OutBytes, Record.Revision);
  return true;
}

bool FCrowdReplicationCodec::DecodeInventory(
  const TConstArrayView<uint8> Bytes,
  FCrowdInventoryReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(OutRecord.OwnerRef)
    || !Reader.ReadI32(OutRecord.OnHand)
    || !Reader.ReadI32(OutRecord.ReservedInbound)
    || !Reader.ReadI32(OutRecord.ReservedOutbound)
    || !Reader.ReadI32(OutRecord.InTransit)
    || !Reader.ReadI32(OutRecord.Capacity)
    || !Reader.ReadU32(OutRecord.Revision)
    || !Reader.AtEnd() || !OutRecord.OwnerRef.IsValid()
    || OutRecord.OnHand < 0 || OutRecord.ReservedInbound < 0
    || OutRecord.ReservedOutbound < 0 || OutRecord.InTransit < 0
    || OutRecord.Capacity < 0 || OutRecord.OnHand > OutRecord.Capacity)
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodeCargo(
  const FCrowdCargoReplicationRecord& Record, TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Record.CargoRef.IsValid() || !Record.SourceRef.IsValid()
    || !Record.SinkRef.IsValid() || Record.Quantity <= 0)
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Record.CargoRef);
  WriteRef(OutBytes, Record.CarrierRef);
  WriteRef(OutBytes, Record.SourceRef);
  WriteRef(OutBytes, Record.SinkRef);
  WriteI32(OutBytes, Record.Quantity);
  WriteU32(OutBytes, Record.State);
  WriteU32(OutBytes, Record.Revision);
  return true;
}

bool FCrowdReplicationCodec::DecodeCargo(
  const TConstArrayView<uint8> Bytes,
  FCrowdCargoReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(OutRecord.CargoRef)
    || !Reader.ReadRef(OutRecord.CarrierRef)
    || !Reader.ReadRef(OutRecord.SourceRef)
    || !Reader.ReadRef(OutRecord.SinkRef)
    || !Reader.ReadI32(OutRecord.Quantity)
    || !Reader.ReadU32(OutRecord.State)
    || !Reader.ReadU32(OutRecord.Revision)
    || !Reader.AtEnd() || !OutRecord.CargoRef.IsValid()
    || !OutRecord.SourceRef.IsValid() || !OutRecord.SinkRef.IsValid()
    || OutRecord.Quantity <= 0)
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationCodec::EncodePresentation(
  const FCrowdPresentationReplicationRecord& Record,
  TArray<uint8>& OutBytes)
{
  OutBytes.Reset();
  if (!Record.EntityRef.IsValid()
    || !IsFiniteVector(Record.Location)
    || !FMath::IsFinite(Record.YawDegrees))
    return false;
  WriteU16(OutBytes, CodecVersion);
  WriteRef(OutBytes, Record.EntityRef);
  WriteRef(OutBytes, Record.CargoRef);
  WriteVector(OutBytes, Record.Location);
  WriteFloat(OutBytes, Record.YawDegrees);
  WriteU32(OutBytes, Record.ProfileKey);
  OutBytes.Add(Record.VisualState);
  WriteU32(OutBytes, Record.Revision);
  return true;
}

bool FCrowdReplicationCodec::DecodePresentation(
  const TConstArrayView<uint8> Bytes,
  FCrowdPresentationReplicationRecord& OutRecord)
{
  OutRecord = {};
  FByteReader Reader(Bytes);
  uint16 Version = 0;
  if (!Reader.ReadU16(Version) || Version != CodecVersion
    || !Reader.ReadRef(OutRecord.EntityRef)
    || !Reader.ReadRef(OutRecord.CargoRef)
    || !Reader.ReadVector(OutRecord.Location)
    || !Reader.ReadFloat(OutRecord.YawDegrees)
    || !Reader.ReadU32(OutRecord.ProfileKey)
    || !Reader.ReadU8(OutRecord.VisualState)
    || !Reader.ReadU32(OutRecord.Revision)
    || !Reader.AtEnd() || !OutRecord.EntityRef.IsValid())
  {
    OutRecord = {};
    return false;
  }
  return true;
}

bool FCrowdReplicationClientState::Initialize(
  const FCrowdReplicationChannelLimits& InLimits)
{
  *this = {};
  if (!InLimits.IsValid()) return false;
  Limits = InLimits;
  bInitialized = true;
  return true;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptBaselineBegin(
  const FCrowdBaselineBegin& Begin,
  const double NowSeconds)
{
  if (!bInitialized || bRequiresResync
    || Begin.BaselineRevision == 0
    || Begin.ResumeReliableSequence == 0
    || Begin.StableHash == 0)
    return ECrowdReplicationAcceptResult::Rejected;
  if (bHasBegin)
  {
    if (PendingBegin.BaselineRevision == Begin.BaselineRevision
      && PendingBegin.ResumeReliableSequence == Begin.ResumeReliableSequence
      && PendingBegin.StableHash == Begin.StableHash)
      return ECrowdReplicationAcceptResult::Duplicate;
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  if (!Assembly.Begin(Begin.BaselineRevision, Limits.SnapshotLimits))
    return ECrowdReplicationAcceptResult::Rejected;
  PendingBegin = Begin;
  bHasBegin = true;
  return ECrowdReplicationAcceptResult::Accepted;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptSnapshotHeader(
  const FCrowdRelevantSnapshotHeader& Header,
  const double NowSeconds)
{
  if (!bHasBegin || bRequiresResync)
    return ECrowdReplicationAcceptResult::Rejected;
  const ECrowdRelevantSnapshotAcceptResult Result =
    Assembly.AcceptHeader(Header, NowSeconds);
  if (Result == ECrowdRelevantSnapshotAcceptResult::Rejected
    || Result == ECrowdRelevantSnapshotAcceptResult::TimedOut)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  return Result == ECrowdRelevantSnapshotAcceptResult::Duplicate
    ? ECrowdReplicationAcceptResult::Duplicate
    : ECrowdReplicationAcceptResult::Accepted;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptSnapshotChunk(
  const FCrowdRelevantSnapshotChunk& Chunk,
  const double NowSeconds)
{
  if (!bHasBegin || bRequiresResync)
    return ECrowdReplicationAcceptResult::Rejected;
  const ECrowdRelevantSnapshotAcceptResult Result =
    Assembly.AcceptChunk(Chunk, NowSeconds);
  if (Result == ECrowdRelevantSnapshotAcceptResult::Rejected
    || Result == ECrowdRelevantSnapshotAcceptResult::TimedOut)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  return Result == ECrowdRelevantSnapshotAcceptResult::Duplicate
    ? ECrowdReplicationAcceptResult::Duplicate
    : ECrowdReplicationAcceptResult::Accepted;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptBaselineEnd(
  const FCrowdBaselineEnd& End,
  const double NowSeconds)
{
  if (!bHasBegin || bRequiresResync
    || End.BaselineRevision != PendingBegin.BaselineRevision
    || End.ResumeReliableSequence != PendingBegin.ResumeReliableSequence)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  if (bHasEnd)
  {
    if (PendingEnd.BaselineRevision == End.BaselineRevision
      && PendingEnd.ResumeReliableSequence == End.ResumeReliableSequence
      && PendingEnd.SnapshotHash == End.SnapshotHash)
      return ECrowdReplicationAcceptResult::Duplicate;
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  const FCrowdRelevantSnapshotHeader* Header = Assembly.GetHeader();
  if (!Header || Header->SnapshotHash != End.SnapshotHash
    || !Assembly.TryFinalize(NowSeconds, CompletedEntities))
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  PendingEnd = End;
  bHasEnd = true;
  bBaselineReady = true;
  NextReliableSequence = End.ResumeReliableSequence;
  if (PendingApplyFrames.Num() >= Limits.MaxPendingApplyFrames)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }
  FCrowdReplicationApplyFrame& Frame =
    PendingApplyFrames.AddDefaulted_GetRef();
  Frame.Kind = ECrowdReplicationApplyFrameKind::Baseline;
  Frame.BaselineRevision = End.BaselineRevision;
  Frame.FirstReliableSequence = End.ResumeReliableSequence;
  Frame.BaselineEntities = CompletedEntities;
  Frame.StableHash =
    FCrowdReplicationTransport::CalculateApplyFrameHash(Frame);
  Frame.bValid = Frame.StableHash != 0;
  return ECrowdReplicationAcceptResult::BaselineComplete;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptReliableBatch(
  const FCrowdReliableStateBatch& Batch)
{
  if (!bInitialized || bRequiresResync || !bBaselineReady)
    return ECrowdReplicationAcceptResult::Rejected;
  if (Batch.Records.IsEmpty()
    || Batch.Records.Num() > Limits.MaxReliableRecordsPerBatch
    || Batch.FirstSequence != Batch.Records[0].Sequence
    || Batch.StableHash
      != FCrowdReplicationTransport::CalculateReliableBatchHash(Batch))
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }

  bool bAllDuplicate = true;
  uint64 CandidateNextSequence = NextReliableSequence;
  TMap<FCrowdReplicationEntityLineage, uint32>
    CandidateLifecycleByEntity = LatestLifecycleByEntity;
  for (int32 Index = 0; Index < Batch.Records.Num(); ++Index)
  {
    const FCrowdReliableStateRecord& Record = Batch.Records[Index];
    if (!Record.EntityRef.IsValid()
      || Record.Payload.Num() > Limits.MaxReliablePayloadBytesPerRecord
      || Record.Sequence != Batch.FirstSequence + Index
      || Record.StableHash
        != FCrowdReplicationTransport::CalculateReliableRecordHash(Record))
    {
      RequireResync();
      return ECrowdReplicationAcceptResult::ResyncRequired;
    }
    if (Record.Sequence < CandidateNextSequence)
    {
      const uint64* AppliedHash = AppliedReliableHashes.Find(Record.Sequence);
      if (!AppliedHash || *AppliedHash != Record.StableHash)
      {
        RequireResync();
        return ECrowdReplicationAcceptResult::ResyncRequired;
      }
      continue;
    }
    bAllDuplicate = false;
    if (Record.Sequence != CandidateNextSequence)
    {
      RequireResync();
      return ECrowdReplicationAcceptResult::ResyncRequired;
    }
    const FCrowdReplicationEntityLineage Lineage{
      Record.EntityRef.ProviderId, Record.EntityRef.StableEntityId};
    if (const uint32* LatestLifecycle =
      CandidateLifecycleByEntity.Find(Lineage))
    {
      if (Record.EntityRef.LifecycleSerial < *LatestLifecycle
        || (Record.EntityRef.LifecycleSerial > *LatestLifecycle
          && Record.Kind != ECrowdReliableStateKind::Spawn))
      {
        RequireResync();
        return ECrowdReplicationAcceptResult::ResyncRequired;
      }
    }
    CandidateLifecycleByEntity.Add(
      Lineage, Record.EntityRef.LifecycleSerial);
    ++CandidateNextSequence;
  }
  if (bAllDuplicate)
    return ECrowdReplicationAcceptResult::Duplicate;
  if (PendingApplyFrames.Num() >= Limits.MaxPendingApplyFrames)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }

  LatestLifecycleByEntity = MoveTemp(CandidateLifecycleByEntity);
  FCrowdReplicationApplyFrame Frame;
  Frame.Kind = ECrowdReplicationApplyFrameKind::ReliableState;
  Frame.FirstReliableSequence = NextReliableSequence;
  for (const FCrowdReliableStateRecord& Record : Batch.Records)
  {
    if (Record.Sequence < NextReliableSequence)
      continue;
    AppliedReliableRecords.Add(Record);
    AppliedReliableHashes.Add(Record.Sequence, Record.StableHash);
    Frame.ReliableRecords.Add(Record);
  }
  NextReliableSequence = CandidateNextSequence;
  Frame.StableHash =
    FCrowdReplicationTransport::CalculateApplyFrameHash(Frame);
  Frame.bValid = Frame.StableHash != 0;
  PendingApplyFrames.Add(MoveTemp(Frame));
  return ECrowdReplicationAcceptResult::Accepted;
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptMovementCorrection(
  const FCrowdMovementCorrectionRecord& Correction)
{
  return AcceptMovementCorrections(
    MakeArrayView(&Correction, 1));
}

ECrowdReplicationAcceptResult
FCrowdReplicationClientState::AcceptMovementCorrections(
  const TConstArrayView<FCrowdMovementCorrectionRecord> Corrections)
{
  if (!bInitialized || bRequiresResync)
    return ECrowdReplicationAcceptResult::Rejected;
  if (Corrections.IsEmpty()
    || Corrections.Num() > Limits.MaxCorrectionRecords)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }

  TMap<FCrowdStableEntityRef, FCrowdMovementCorrectionRecord>
    CandidateCorrections = LatestCorrections;
  TMap<FCrowdReplicationEntityLineage, uint32>
    CandidateLifecycle = LatestLifecycleByEntity;
  TArray<FCrowdMovementCorrectionRecord> Accepted;
  Accepted.Reserve(Corrections.Num());
  for (const FCrowdMovementCorrectionRecord& Correction : Corrections)
  {
    if (!Correction.EntityRef.IsValid()
      || Correction.Sequence == 0
      || !IsFiniteVector(Correction.Position)
      || !IsFiniteVector(Correction.Velocity)
      || !FMath::IsFinite(Correction.YawDegrees)
      || Correction.StableHash
        != FCrowdReplicationTransport::CalculateMovementCorrectionHash(
          Correction))
    {
      RequireResync();
      return ECrowdReplicationAcceptResult::ResyncRequired;
    }
    const FCrowdReplicationEntityLineage Lineage{
      Correction.EntityRef.ProviderId,
      Correction.EntityRef.StableEntityId};
    const uint32* LatestLifecycle = CandidateLifecycle.Find(Lineage);
    if (LatestLifecycle
      && Correction.EntityRef.LifecycleSerial < *LatestLifecycle)
      continue;
    if (const FCrowdMovementCorrectionRecord* Existing =
      CandidateCorrections.Find(Correction.EntityRef))
    {
      if (Correction.Sequence <= Existing->Sequence)
        continue;
    }
    if (!LatestLifecycle
      || Correction.EntityRef.LifecycleSerial > *LatestLifecycle)
    {
      TArray<FCrowdStableEntityRef> RetiredRefs;
      for (const auto& Pair : CandidateCorrections)
        if (Pair.Key.ProviderId == Correction.EntityRef.ProviderId
          && Pair.Key.StableEntityId
            == Correction.EntityRef.StableEntityId)
          RetiredRefs.Add(Pair.Key);
      for (const FCrowdStableEntityRef& Retired : RetiredRefs)
        CandidateCorrections.Remove(Retired);
      CandidateLifecycle.Add(
        Lineage, Correction.EntityRef.LifecycleSerial);
    }
    CandidateCorrections.Add(Correction.EntityRef, Correction);
    Accepted.Add(Correction);
  }
  if (Accepted.IsEmpty())
    return ECrowdReplicationAcceptResult::IgnoredStaleCorrection;
  if (CandidateCorrections.Num() > Limits.MaxCorrectionRecords
    || PendingApplyFrames.Num() >= Limits.MaxPendingApplyFrames)
  {
    RequireResync();
    return ECrowdReplicationAcceptResult::ResyncRequired;
  }

  LatestCorrections = MoveTemp(CandidateCorrections);
  LatestLifecycleByEntity = MoveTemp(CandidateLifecycle);
  FCrowdReplicationApplyFrame& Frame =
    PendingApplyFrames.AddDefaulted_GetRef();
  Frame.Kind = ECrowdReplicationApplyFrameKind::MovementCorrection;
  Frame.Corrections = MoveTemp(Accepted);
  Frame.StableHash =
    FCrowdReplicationTransport::CalculateApplyFrameHash(Frame);
  Frame.bValid = Frame.StableHash != 0;
  return ECrowdReplicationAcceptResult::Accepted;
}

bool FCrowdReplicationClientState::ConsumeCompletedBaseline(
  TArray<FCrowdRelevantSnapshotEntityPayload>& OutEntities,
  uint32& OutBaselineRevision,
  uint64& OutResumeReliableSequence)
{
  if (!bBaselineReady || bRequiresResync) return false;
  OutEntities = MoveTemp(CompletedEntities);
  OutBaselineRevision = PendingBegin.BaselineRevision;
  OutResumeReliableSequence = PendingBegin.ResumeReliableSequence;
  bHasBegin = false;
  bHasEnd = false;
  return true;
}

bool FCrowdReplicationClientState::DrainApplyFrames(
  TArray<FCrowdReplicationApplyFrame>& OutFrames)
{
  OutFrames.Reset();
  if (!bInitialized || bRequiresResync || PendingApplyFrames.IsEmpty())
    return false;
  for (const FCrowdReplicationApplyFrame& Frame : PendingApplyFrames)
  {
    if (!Frame.bValid
      || Frame.Version != FCrowdReplicationApplyFrame::CurrentVersion
      || Frame.StableHash
        != FCrowdReplicationTransport::CalculateApplyFrameHash(Frame))
    {
      RequireResync();
      return false;
    }
  }
  OutFrames = MoveTemp(PendingApplyFrames);
  AppliedReliableRecords.Reset();
  const uint64 FirstRetainedSequence =
    NextReliableSequence > static_cast<uint64>(
      Limits.MaxDuplicateHistoryRecords)
      ? NextReliableSequence
        - static_cast<uint64>(Limits.MaxDuplicateHistoryRecords)
      : 0;
  TArray<uint64> ExpiredSequences;
  for (const auto& Pair : AppliedReliableHashes)
    if (Pair.Key < FirstRetainedSequence)
      ExpiredSequences.Add(Pair.Key);
  for (const uint64 Sequence : ExpiredSequences)
    AppliedReliableHashes.Remove(Sequence);
  return true;
}

void FCrowdReplicationClientState::RequireResync()
{
  bRequiresResync = true;
  bBaselineReady = false;
}

bool FCrowdReplicationServerState::Initialize(
  const FCrowdReplicationChannelLimits& InLimits)
{
  *this = {};
  if (!InLimits.IsValid()) return false;
  Limits = InLimits;
  bInitialized = true;
  return true;
}

bool FCrowdReplicationServerState::BeginBaseline(
  const FCrowdBaselineBegin& Begin,
  const FCrowdRelevantSnapshotHeader& Header)
{
  if (!bInitialized || bAwaitingAck
    || Begin.BaselineRevision == 0
    || Begin.BaselineRevision != Header.SnapshotRevision
    || Begin.ResumeReliableSequence == 0
    || Begin.StableHash == 0
    || !Header.IsWellFormed(Limits.SnapshotLimits))
    return false;
  PendingBegin = Begin;
  PendingHeader = Header;
  BufferedReliable.Reset();
  bAwaitingAck = true;
  bRequiresNewBaseline = false;
  return true;
}

bool FCrowdReplicationServerState::BufferReliable(
  const FCrowdReliableStateRecord& Record)
{
  if (!bAwaitingAck || bRequiresNewBaseline
    || Record.Sequence < PendingBegin.ResumeReliableSequence
    || !Record.EntityRef.IsValid()
    || Record.Payload.Num() > Limits.MaxReliablePayloadBytesPerRecord
    || Record.StableHash
      != FCrowdReplicationTransport::CalculateReliableRecordHash(Record))
    return false;
  if (BufferedReliable.Num() >= Limits.MaxBufferedReliableRecords)
  {
    BufferedReliable.Reset();
    bRequiresNewBaseline = true;
    return false;
  }
  const uint64 ExpectedSequence = BufferedReliable.IsEmpty()
    ? PendingBegin.ResumeReliableSequence
    : BufferedReliable.Last().Sequence + 1;
  if (Record.Sequence != ExpectedSequence)
  {
    BufferedReliable.Reset();
    bRequiresNewBaseline = true;
    return false;
  }
  BufferedReliable.Add(Record);
  return true;
}

bool FCrowdReplicationServerState::AckBaseline(
  const uint32 BaselineRevision,
  const uint64 ResumeSequence)
{
  if (!bAwaitingAck || bRequiresNewBaseline
    || BaselineRevision != PendingBegin.BaselineRevision
    || ResumeSequence != PendingBegin.ResumeReliableSequence)
    return false;
  bAwaitingAck = false;
  return true;
}

bool FCrowdReplicationServerState::ConsumeBufferedReliable(
  TArray<FCrowdReliableStateRecord>& OutRecords)
{
  if (bAwaitingAck || bRequiresNewBaseline) return false;
  OutRecords = MoveTemp(BufferedReliable);
  return true;
}
