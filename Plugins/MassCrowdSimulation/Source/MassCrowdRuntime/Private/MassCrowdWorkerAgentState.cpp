#include "MassCrowdWorkerAgentState.h"

namespace CrowdWorkerAgentStatePrivate
{
  template<typename T>
  void AppendUnsigned(TArray<uint8>& Bytes, const T Value)
  {
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      Bytes.Add(static_cast<uint8>(Value >> (Byte * 8)));
  }

  template<typename T>
  bool ReadUnsigned(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    T& OutValue)
  {
    if (Offset < 0 || Offset + sizeof(T) > Bytes.Num())
      return false;
    OutValue = 0;
    for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
      OutValue |= static_cast<T>(Bytes[Offset + Byte]) << (Byte * 8);
    Offset += sizeof(T);
    return true;
  }

  void AppendRef(
    TArray<uint8>& Bytes,
    const FCrowdStableEntityRef& EntityRef)
  {
    AppendUnsigned(Bytes, EntityRef.ProviderId);
    AppendUnsigned(Bytes, EntityRef.StableEntityId);
    AppendUnsigned(Bytes, EntityRef.LifecycleSerial);
  }

  bool ReadRef(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    FCrowdStableEntityRef& OutEntityRef)
  {
    return ReadUnsigned(Bytes, Offset, OutEntityRef.ProviderId)
      && ReadUnsigned(Bytes, Offset, OutEntityRef.StableEntityId)
      && ReadUnsigned(Bytes, Offset, OutEntityRef.LifecycleSerial);
  }
}

bool FCrowdWorkerLifecycleStateCodec::Encode(
  const FCrowdWorkerLifecycleState& State,
  FCrowdWorkerPayload& OutPayload)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendRef(OutPayload.Bytes, State.EntityRef);
  AppendUnsigned(OutPayload.Bytes, State.Revision);
  AppendUnsigned(OutPayload.Bytes, State.SourceInputSequence);
  AppendUnsigned(OutPayload.Bytes, State.InitialStateHash);
  AppendUnsigned(
    OutPayload.Bytes, static_cast<uint8>(State.Phase));
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerLifecycleStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerLifecycleState& OutState)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutState = {};
  constexpr int32 PayloadBytes =
    sizeof(uint32) + sizeof(uint64) + sizeof(uint32)
    + sizeof(uint64) * 3 + sizeof(uint8);
  uint8 Phase = 0;
  int32 Offset = 0;
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != PayloadBytes
    || Payload.StableHash != Payload.CalculateStableHash()
    || !ReadRef(Payload.Bytes, Offset, OutState.EntityRef)
    || !ReadUnsigned(Payload.Bytes, Offset, OutState.Revision)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutState.SourceInputSequence)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutState.InitialStateHash)
    || !ReadUnsigned(Payload.Bytes, Offset, Phase)
    || Offset != Payload.Bytes.Num())
    return false;
  OutState.Phase = static_cast<ECrowdWorkerLifecyclePhase>(Phase);
  if (!OutState.IsValid())
  {
    OutState = {};
    return false;
  }
  return true;
}

bool FCrowdWorkerLifecycleTransitionCodec::Encode(
  const FCrowdWorkerLifecycleTransition& Transition,
  FCrowdWorkerPayload& OutPayload)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutPayload = {};
  if (!Transition.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendRef(OutPayload.Bytes, Transition.EntityRef);
  AppendUnsigned(OutPayload.Bytes, Transition.ExpectedRevision);
  AppendUnsigned(OutPayload.Bytes, Transition.Revision);
  AppendUnsigned(
    OutPayload.Bytes, static_cast<uint8>(Transition.TargetPhase));
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerLifecycleTransitionCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerLifecycleTransition& OutTransition)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutTransition = {};
  constexpr int32 PayloadBytes =
    sizeof(uint32) + sizeof(uint64) + sizeof(uint32)
    + sizeof(uint64) * 2 + sizeof(uint8);
  uint8 TargetPhase = 0;
  int32 Offset = 0;
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != PayloadBytes
    || Payload.StableHash != Payload.CalculateStableHash()
    || !ReadRef(Payload.Bytes, Offset, OutTransition.EntityRef)
    || !ReadUnsigned(
      Payload.Bytes, Offset, OutTransition.ExpectedRevision)
    || !ReadUnsigned(Payload.Bytes, Offset, OutTransition.Revision)
    || !ReadUnsigned(Payload.Bytes, Offset, TargetPhase)
    || Offset != Payload.Bytes.Num())
    return false;
  OutTransition.TargetPhase =
    static_cast<ECrowdWorkerLifecyclePhase>(TargetPhase);
  if (!OutTransition.IsValid())
  {
    OutTransition = {};
    return false;
  }
  return true;
}

bool FCrowdWorkerLifecycleStateMachine::CanTransition(
  const ECrowdWorkerLifecyclePhase From,
  const ECrowdWorkerLifecyclePhase To)
{
  if (From >= ECrowdWorkerLifecyclePhase::Count
    || To >= ECrowdWorkerLifecyclePhase::Count
    || From == To)
    return false;
  switch (From)
  {
    case ECrowdWorkerLifecyclePhase::SpawnPending:
      return To == ECrowdWorkerLifecyclePhase::Active
        || To == ECrowdWorkerLifecyclePhase::Removed;
    case ECrowdWorkerLifecyclePhase::Active:
      return To == ECrowdWorkerLifecyclePhase::Suspended
        || To == ECrowdWorkerLifecyclePhase::Removed;
    case ECrowdWorkerLifecyclePhase::Suspended:
      return To == ECrowdWorkerLifecyclePhase::Active
        || To == ECrowdWorkerLifecyclePhase::Removed;
    default:
      return false;
  }
}

bool FCrowdWorkerLifecycleStateMachine::Apply(
  const FCrowdWorkerLifecycleState* Current,
  const FCrowdWorkerLifecycleTransition& Transition,
  const uint64 SourceInputSequence,
  const uint64 InitialStateHash,
  FCrowdWorkerLifecycleState& OutState)
{
  OutState = {};
  if (!Transition.IsValid()
    || SourceInputSequence == 0
    || InitialStateHash == 0)
    return false;
  if (!Current)
  {
    if (Transition.ExpectedRevision != 0
      || Transition.TargetPhase
        != ECrowdWorkerLifecyclePhase::SpawnPending)
      return false;
  }
  else if (!Current->IsValid()
    || Current->EntityRef != Transition.EntityRef
    || Current->Revision != Transition.ExpectedRevision
    || Current->InitialStateHash != InitialStateHash
    || !CanTransition(Current->Phase, Transition.TargetPhase))
    return false;

  OutState.EntityRef = Transition.EntityRef;
  OutState.Revision = Transition.Revision;
  OutState.SourceInputSequence = SourceInputSequence;
  OutState.InitialStateHash = InitialStateHash;
  OutState.Phase = Transition.TargetPhase;
  return OutState.IsValid();
}

bool FCrowdWorkerParticipationStateCodec::Encode(
  const FCrowdWorkerParticipationState& State,
  FCrowdWorkerPayload& OutPayload)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutPayload = {};
  if (!State.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendRef(OutPayload.Bytes, State.EntityRef);
  AppendUnsigned(OutPayload.Bytes, State.Revision);
  AppendUnsigned(OutPayload.Bytes, State.EnabledMask);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerParticipationStateCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerParticipationState& OutState)
{
  using namespace CrowdWorkerAgentStatePrivate;
  OutState = {};
  constexpr int32 PayloadBytes =
    sizeof(uint32) + sizeof(uint64) + sizeof(uint32)
    + sizeof(uint64) + sizeof(uint8);
  int32 Offset = 0;
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != PayloadBytes
    || Payload.StableHash != Payload.CalculateStableHash()
    || !ReadRef(Payload.Bytes, Offset, OutState.EntityRef)
    || !ReadUnsigned(Payload.Bytes, Offset, OutState.Revision)
    || !ReadUnsigned(Payload.Bytes, Offset, OutState.EnabledMask)
    || Offset != Payload.Bytes.Num()
    || !OutState.IsValid())
  {
    OutState = {};
    return false;
  }
  return true;
}
