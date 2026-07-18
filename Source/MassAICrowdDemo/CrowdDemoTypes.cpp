#include "CrowdDemoTypes.h"

#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
constexpr uint8 SoftPressurePayload = 1;
constexpr uint8 CombatPayload = 2;

bool SerializePayloadToRaw(
  FCrowdDemoRoundResultHeader& Header, TArray<uint8>& OutRaw)
{
  FMemoryWriter Writer(OutRaw, true);
  FCrowdDemoSharedFlowMetrics::StaticStruct()->SerializeBin(
    Writer, &Header.SharedFlowMetrics);
  FCrowdDemoParticleMetrics::StaticStruct()->SerializeBin(
    Writer, &Header.ParticleMetrics);
  if (Header.PayloadKind == CombatPayload)
  {
    FCrowdDemoProjectileMetrics::StaticStruct()->SerializeBin(
      Writer, &Header.ProjectileMetrics);
  }
  Writer.Close();
  return !Writer.IsError();
}

bool DeserializePayloadFromRaw(
  FCrowdDemoRoundResultHeader& Header, const TArray<uint8>& Raw)
{
  FMemoryReader Reader(Raw, true);
  FCrowdDemoSharedFlowMetrics::StaticStruct()->SerializeBin(
    Reader, &Header.SharedFlowMetrics);
  FCrowdDemoParticleMetrics::StaticStruct()->SerializeBin(
    Reader, &Header.ParticleMetrics);
  if (Header.PayloadKind == CombatPayload)
  {
    FCrowdDemoProjectileMetrics::StaticStruct()->SerializeBin(
      Reader, &Header.ProjectileMetrics);
  }
  else
  {
    Header.ProjectileMetrics = {};
  }
  Reader.Close();
  return !Reader.IsError();
}

bool CompressPayload(const TArray<uint8>& Raw, TArray<uint8>& OutCompressed)
{
  if (Raw.IsEmpty()) return false;
  int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
  OutCompressed.SetNumUninitialized(CompressedSize);
  if (!FCompression::CompressMemory(
    NAME_Zlib, OutCompressed.GetData(), CompressedSize, Raw.GetData(), Raw.Num(),
    COMPRESS_BiasMemory))
  {
    OutCompressed.Reset();
    return false;
  }
  OutCompressed.SetNum(CompressedSize, EAllowShrinking::Yes);
  return true;
}

bool DecompressPayload(
  const TArray<uint8>& Compressed, const int32 RawSize, TArray<uint8>& OutRaw)
{
  if (Compressed.IsEmpty() || RawSize <= 0) return false;
  OutRaw.SetNumUninitialized(RawSize);
  if (!FCompression::UncompressMemory(
    NAME_Zlib, OutRaw.GetData(), RawSize, Compressed.GetData(), Compressed.Num()))
  {
    OutRaw.Reset();
    return false;
  }
  return true;
}
}

bool FCrowdDemoRoundResultHeader::NetSerialize(
  FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
  (void)Map;
  const int64 StartOffset = Ar.Tell();
  if (Ar.IsSaving()) ContractVersion = CurrentContractVersion;
  Ar << ContractVersion;
  Ar << PayloadKind;
  Ar << bValid;
  Ar << RoundId;
  Ar << Revision;
  Ar << CheckpointRevision;
  Ar << StateFrameRevision;
  Ar << EndServerTimeSeconds;
  Ar << AgentCount;
  Ar << OverlapPairCount;
  Ar << InitialOverlapPairCount;
  Ar << SevereOverlapPairCount;
  Ar << InitialSevereOverlapPairCount;
  Ar << ObstaclePenetrationCount;
  Ar << ArrivalCount;

  if (Ar.IsLoading()
    && (ContractVersion != CurrentContractVersion
      || (PayloadKind != SoftPressurePayload && PayloadKind != CombatPayload)))
  {
    bOutSuccess = false;
    return true;
  }

  TArray<uint8> Compressed;
  int32 RawSize = 0;
  if (Ar.IsSaving())
  {
    TArray<uint8> Raw;
    if (!SerializePayloadToRaw(*this, Raw) || !CompressPayload(Raw, Compressed))
    {
      bOutSuccess = false;
      return true;
    }
    RawSize = Raw.Num();
  }
  Ar << RawSize;
  Ar << Compressed;

  if (Ar.IsLoading())
  {
    TArray<uint8> Raw;
    if (!DecompressPayload(Compressed, RawSize, Raw)
      || !DeserializePayloadFromRaw(*this, Raw))
    {
      bOutSuccess = false;
      return true;
    }
  }

  const int64 EndOffset = Ar.Tell();
  SerializedByteCount = StartOffset >= 0 && EndOffset >= StartOffset
    ? static_cast<int32>(EndOffset - StartOffset)
    : Compressed.Num() + 64;
  bOutSuccess = !Ar.IsError() && SerializedByteCount <= MaximumSerializedBytes;
  return true;
}
