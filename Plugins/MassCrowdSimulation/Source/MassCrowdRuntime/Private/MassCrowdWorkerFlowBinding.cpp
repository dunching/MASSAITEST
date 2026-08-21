#include "MassCrowdWorkerFlowBinding.h"

namespace CrowdWorkerFlowBindingPrivate
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
}

bool FCrowdWorkerFlowBindingCodec::Encode(
  const FCrowdWorkerFlowBinding& Binding,
  FCrowdWorkerPayload& OutPayload)
{
  using namespace CrowdWorkerFlowBindingPrivate;
  OutPayload = {};
  if (!Binding.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendUnsigned(OutPayload.Bytes, Binding.EntityRef.ProviderId);
  AppendUnsigned(OutPayload.Bytes, Binding.EntityRef.StableEntityId);
  AppendUnsigned(OutPayload.Bytes, Binding.EntityRef.LifecycleSerial);
  AppendUnsigned(OutPayload.Bytes, Binding.ObjectiveRef.ObjectiveId);
  AppendUnsigned(OutPayload.Bytes, Binding.CohortKey);
  AppendUnsigned(OutPayload.Bytes, Binding.FlowResourceId);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerFlowBindingCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerFlowBinding& OutBinding)
{
  using namespace CrowdWorkerFlowBindingPrivate;
  OutBinding = {};
  constexpr int32 PayloadBytes =
    sizeof(uint32) + sizeof(uint64) + sizeof(uint32)
    + sizeof(uint64) + sizeof(uint32) + sizeof(uint64);
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != PayloadBytes
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  if (!ReadUnsigned(
        Payload.Bytes, Offset, OutBinding.EntityRef.ProviderId)
    || !ReadUnsigned(
        Payload.Bytes, Offset, OutBinding.EntityRef.StableEntityId)
    || !ReadUnsigned(
        Payload.Bytes, Offset, OutBinding.EntityRef.LifecycleSerial)
    || !ReadUnsigned(
        Payload.Bytes, Offset, OutBinding.ObjectiveRef.ObjectiveId)
    || !ReadUnsigned(Payload.Bytes, Offset, OutBinding.CohortKey)
    || !ReadUnsigned(Payload.Bytes, Offset, OutBinding.FlowResourceId)
    || Offset != Payload.Bytes.Num()
    || !OutBinding.IsValid())
  {
    OutBinding = {};
    return false;
  }
  return true;
}

bool FCrowdWorkerFlowBindingCodec::EncodeClear(
  FCrowdWorkerPayload& OutPayload)
{
  OutPayload = {};
  OutPayload.SchemaId = ClearSchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  OutPayload.RecalculateStableHash();
  return OutPayload.IsValid(1);
}

bool FCrowdWorkerFlowBindingCodec::IsClearPayload(
  const FCrowdWorkerPayload& Payload)
{
  return Payload.SchemaId == ClearSchemaId
    && Payload.SchemaVersion == SchemaVersion
    && Payload.Bytes.IsEmpty()
    && Payload.StableHash == Payload.CalculateStableHash();
}
