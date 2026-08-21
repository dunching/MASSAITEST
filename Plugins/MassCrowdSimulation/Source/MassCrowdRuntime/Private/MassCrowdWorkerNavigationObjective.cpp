#include "MassCrowdWorkerNavigationObjective.h"

namespace CrowdWorkerNavigationObjectivePrivate
{
  void AppendDouble(TArray<uint8>& Bytes, const double Value)
  {
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bytes.Add(static_cast<uint8>(Bits >> (Byte * 8)));
  }

  bool ReadDouble(
    const TConstArrayView<uint8> Bytes,
    int32& Offset,
    double& OutValue)
  {
    if (Offset < 0 || Offset + sizeof(uint64) > Bytes.Num())
      return false;
    uint64 Bits = 0;
    for (uint32 Byte = 0; Byte < sizeof(Bits); ++Byte)
      Bits |= static_cast<uint64>(Bytes[Offset + Byte]) << (Byte * 8);
    Offset += sizeof(Bits);
    FMemory::Memcpy(&OutValue, &Bits, sizeof(OutValue));
    return FMath::IsFinite(OutValue);
  }
}

bool FCrowdWorkerNavigationObjectiveResourceCodec::Encode(
  const FCrowdWorkerNavigationObjectiveResource& Objective,
  FCrowdWorkerPayload& OutPayload)
{
  using namespace CrowdWorkerNavigationObjectivePrivate;
  OutPayload = {};
  if (!Objective.IsValid()) return false;
  OutPayload.SchemaId = SchemaId;
  OutPayload.SchemaVersion = SchemaVersion;
  AppendDouble(OutPayload.Bytes, Objective.GoalLocation.X);
  AppendDouble(OutPayload.Bytes, Objective.GoalLocation.Y);
  AppendDouble(OutPayload.Bytes, Objective.GoalLocation.Z);
  OutPayload.RecalculateStableHash();
  return true;
}

bool FCrowdWorkerNavigationObjectiveResourceCodec::Decode(
  const FCrowdWorkerPayload& Payload,
  FCrowdWorkerNavigationObjectiveResource& OutObjective)
{
  using namespace CrowdWorkerNavigationObjectivePrivate;
  OutObjective = {};
  if (Payload.SchemaId != SchemaId
    || Payload.SchemaVersion != SchemaVersion
    || Payload.Bytes.Num() != 3 * sizeof(uint64)
    || Payload.StableHash != Payload.CalculateStableHash())
    return false;
  int32 Offset = 0;
  if (!ReadDouble(Payload.Bytes, Offset, OutObjective.GoalLocation.X)
    || !ReadDouble(Payload.Bytes, Offset, OutObjective.GoalLocation.Y)
    || !ReadDouble(Payload.Bytes, Offset, OutObjective.GoalLocation.Z)
    || Offset != Payload.Bytes.Num()
    || !OutObjective.IsValid())
  {
    OutObjective = {};
    return false;
  }
  return true;
}
