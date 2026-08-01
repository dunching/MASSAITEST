#include "MassCrowdWorkerCombatState.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
  void SerializePayload(FArchive& Ar, FCrowdWorkerPayload& Payload)
  {
    Ar << Payload.SchemaId;
    Ar << Payload.SchemaVersion;
    Ar << Payload.Bytes;
    Ar << Payload.StableHash;
  }
}

bool FCrowdWorkerCombatState::IsValid() const
{
  return SourceFixedStep >= 0
    && !HorizontalReactiveVelocity.ContainsNaN()
    && FMath::IsFinite(ProposedZ)
    && FMath::IsFinite(VerticalVelocityCmps)
    && (HostState.Bytes.IsEmpty()
      || (HostState.SchemaId != 0
        && HostState.SchemaVersion != 0
        && HostState.StableHash
          == HostState.CalculateStableHash()));
}

bool FCrowdWorkerCombatStateCodec::Encode(
  const FCrowdWorkerCombatState& State,
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  if (!State.IsValid()) return false;
  TArray<uint8> Bytes;
  FMemoryWriter Writer(Bytes, true);
  FCrowdWorkerCombatState Copy = State;
  Writer << Copy.SourceFixedStep;
  uint8 Flags = (Copy.bAlive ? 1 : 0)
    | (Copy.bReactiveActive ? 2 : 0);
  Writer << Flags;
  Writer << Copy.HorizontalReactiveVelocity;
  Writer << Copy.ProposedZ;
  Writer << Copy.VerticalVelocityCmps;
  Writer << Copy.LastConsumedHitEventId;
  SerializePayload(Writer, Copy.HostState);
  if (Writer.IsError() || Bytes.IsEmpty()
    || Bytes.Num() > MaxEncodedBytes)
    return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.Bytes = MoveTemp(Bytes);
  OutPayload.RecalculateStableHash();
  return OutPayload.StableHash != 0;
}

bool FCrowdWorkerCombatStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerCombatState& OutState)
{
  OutState = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.IsEmpty()
    || Payload.Bytes.Num() > MaxEncodedBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  FMemoryReader Reader(Payload.Bytes, true);
  uint8 Flags = 0;
  Reader << OutState.SourceFixedStep;
  Reader << Flags;
  Reader << OutState.HorizontalReactiveVelocity;
  Reader << OutState.ProposedZ;
  Reader << OutState.VerticalVelocityCmps;
  Reader << OutState.LastConsumedHitEventId;
  SerializePayload(Reader, OutState.HostState);
  OutState.bAlive = (Flags & 1) != 0;
  OutState.bReactiveActive = (Flags & 2) != 0;
  return !Reader.IsError() && Reader.AtEnd()
    && (Flags & ~3u) == 0 && OutState.IsValid();
}
