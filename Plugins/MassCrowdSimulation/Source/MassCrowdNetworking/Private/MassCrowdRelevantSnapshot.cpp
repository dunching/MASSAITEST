#include "MassCrowdRelevantSnapshot.h"

#include <type_traits>

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  uint64 FoldByte(uint64 Hash, const uint8 Value)
  {
    return (Hash ^ Value) * FnvPrime64;
  }

  template<typename T>
  uint64 FoldUnsigned(uint64 Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      Hash = FoldByte(Hash, static_cast<uint8>(Value >> (ByteIndex * 8)));
    }
    return Hash;
  }

  uint64 FoldBytes(uint64 Hash, const TConstArrayView<uint8> Bytes)
  {
    for (const uint8 Byte : Bytes)
    {
      Hash = FoldByte(Hash, Byte);
    }
    return Hash;
  }

  void AppendUint32(TArray<uint8>& Bytes, const uint32 Value)
  {
    for (uint32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
    {
      Bytes.Add(static_cast<uint8>(Value >> (ByteIndex * 8)));
    }
  }

  bool ReadUint32(
    const TConstArrayView<uint8> Bytes,
    int32& InOutOffset,
    uint32& OutValue)
  {
    if (InOutOffset < 0 || Bytes.Num() - InOutOffset < 4)
    {
      return false;
    }
    OutValue = 0;
    for (uint32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
    {
      OutValue |= static_cast<uint32>(Bytes[InOutOffset++]) << (ByteIndex * 8);
    }
    return true;
  }

  bool AreSameHeader(
    const FCrowdRelevantSnapshotHeader& A,
    const FCrowdRelevantSnapshotHeader& B)
  {
    return A.ProtocolVersion == B.ProtocolVersion
      && A.SnapshotRevision == B.SnapshotRevision
      && A.FixedStepIndex == B.FixedStepIndex
      && A.RelevantSetRevision == B.RelevantSetRevision
      && A.EntityCount == B.EntityCount
      && A.ChunkCount == B.ChunkCount
      && A.PayloadByteCount == B.PayloadByteCount
      && A.SnapshotHash == B.SnapshotHash;
  }

  bool AreSameChunk(
    const FCrowdRelevantSnapshotChunk& A,
    const FCrowdRelevantSnapshotChunk& B)
  {
    return A.SnapshotRevision == B.SnapshotRevision
      && A.ChunkIndex == B.ChunkIndex
      && A.StartEntityIndex == B.StartEntityIndex
      && A.EntityCount == B.EntityCount
      && A.PayloadOffsetBytes == B.PayloadOffsetBytes
      && A.ChunkHash == B.ChunkHash
      && A.Payload == B.Payload;
  }
}

bool FCrowdRelevantSnapshotLimits::IsValid() const
{
  return MaxEntityCount > 0
    && MaxChunkCount > 0
    && MaxEntitiesPerChunk > 0
    && MaxEntitiesPerChunk <= MaxEntityCount
    && MaxChunkPayloadBytes > 4
    && MaxTotalPayloadBytes >= MaxChunkPayloadBytes
    && FMath::IsFinite(AssemblyTimeoutSeconds)
    && AssemblyTimeoutSeconds > 0.0;
}

bool FCrowdRelevantSnapshotHeader::IsWellFormed(
  const FCrowdRelevantSnapshotLimits& Limits) const
{
  if (!Limits.IsValid()
    || ProtocolVersion != CurrentProtocolVersion
    || SnapshotRevision == 0
    || FixedStepIndex < 0
    || RelevantSetRevision == 0
    || EntityCount < 0
    || EntityCount > Limits.MaxEntityCount
    || ChunkCount < 0
    || ChunkCount > Limits.MaxChunkCount
    || PayloadByteCount < 0
    || PayloadByteCount > Limits.MaxTotalPayloadBytes)
  {
    return false;
  }
  if (EntityCount == 0)
  {
    return ChunkCount == 0 && PayloadByteCount == 0;
  }
  return ChunkCount > 0
    && ChunkCount <= EntityCount
    && static_cast<int64>(PayloadByteCount) >= static_cast<int64>(EntityCount) * 4;
}

bool FCrowdRelevantSnapshotChunk::IsLocallyWellFormed(
  const FCrowdRelevantSnapshotLimits& Limits) const
{
  return Limits.IsValid()
    && SnapshotRevision != 0
    && ChunkIndex >= 0
    && ChunkIndex < Limits.MaxChunkCount
    && StartEntityIndex >= 0
    && EntityCount > 0
    && EntityCount <= Limits.MaxEntitiesPerChunk
    && StartEntityIndex <= Limits.MaxEntityCount - EntityCount
    && PayloadOffsetBytes >= 0
    && Payload.Num() >= EntityCount * 4
    && Payload.Num() <= Limits.MaxChunkPayloadBytes
    && PayloadOffsetBytes <= Limits.MaxTotalPayloadBytes - Payload.Num();
}

bool FCrowdRelevantSnapshotChunk::IsWellFormed(
  const FCrowdRelevantSnapshotHeader& Header,
  const FCrowdRelevantSnapshotLimits& Limits) const
{
  return IsLocallyWellFormed(Limits)
    && Header.IsWellFormed(Limits)
    && SnapshotRevision == Header.SnapshotRevision
    && ChunkIndex < Header.ChunkCount
    && StartEntityIndex <= Header.EntityCount - EntityCount
    && PayloadOffsetBytes <= Header.PayloadByteCount - Payload.Num();
}

bool FCrowdRelevantSnapshotTransport::Build(
  const uint32 SnapshotRevision,
  const int64 FixedStepIndex,
  const uint32 RelevantSetRevision,
  const TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Entities,
  const FCrowdRelevantSnapshotLimits& Limits,
  FCrowdRelevantSnapshotHeader& OutHeader,
  TArray<FCrowdRelevantSnapshotChunk>& OutChunks)
{
  OutHeader = {};
  OutChunks.Reset();
  if (!Limits.IsValid()
    || SnapshotRevision == 0
    || FixedStepIndex < 0
    || RelevantSetRevision == 0
    || Entities.Num() > Limits.MaxEntityCount)
  {
    return false;
  }

  OutHeader.SnapshotRevision = SnapshotRevision;
  OutHeader.FixedStepIndex = FixedStepIndex;
  OutHeader.RelevantSetRevision = RelevantSetRevision;
  OutHeader.EntityCount = Entities.Num();

  int64 TotalPayloadBytes = 0;
  int32 EntityIndex = 0;
  while (EntityIndex < Entities.Num())
  {
    FCrowdRelevantSnapshotChunk Chunk;
    Chunk.SnapshotRevision = SnapshotRevision;
    Chunk.ChunkIndex = OutChunks.Num();
    Chunk.StartEntityIndex = EntityIndex;
    Chunk.PayloadOffsetBytes = static_cast<int32>(TotalPayloadBytes);

    while (EntityIndex < Entities.Num()
      && Chunk.EntityCount < Limits.MaxEntitiesPerChunk)
    {
      const TArray<uint8>& EntityBytes = Entities[EntityIndex].Bytes;
      if (EntityBytes.IsEmpty()
        || EntityBytes.Num() > Limits.MaxChunkPayloadBytes - 4)
      {
        OutHeader = {};
        OutChunks.Reset();
        return false;
      }
      const int32 FramedBytes = 4 + EntityBytes.Num();
      if (Chunk.EntityCount > 0
        && Chunk.Payload.Num() > Limits.MaxChunkPayloadBytes - FramedBytes)
      {
        break;
      }
      AppendUint32(Chunk.Payload, static_cast<uint32>(EntityBytes.Num()));
      Chunk.Payload.Append(EntityBytes);
      ++Chunk.EntityCount;
      ++EntityIndex;
    }

    TotalPayloadBytes += Chunk.Payload.Num();
    if (OutChunks.Num() >= Limits.MaxChunkCount
      || TotalPayloadBytes > Limits.MaxTotalPayloadBytes)
    {
      OutHeader = {};
      OutChunks.Reset();
      return false;
    }
    Chunk.ChunkHash = CalculateChunkHash(Chunk);
    OutChunks.Add(MoveTemp(Chunk));
  }

  OutHeader.ChunkCount = OutChunks.Num();
  OutHeader.PayloadByteCount = static_cast<int32>(TotalPayloadBytes);
  OutHeader.SnapshotHash = CalculateSnapshotHash(OutHeader, Entities);
  if (!OutHeader.IsWellFormed(Limits))
  {
    OutHeader = {};
    OutChunks.Reset();
    return false;
  }
  return true;
}

uint64 FCrowdRelevantSnapshotTransport::CalculateSnapshotHash(
  const FCrowdRelevantSnapshotHeader& Header,
  const TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Entities)
{
  uint64 Hash = FnvOffset64;
  Hash = FoldUnsigned(Hash, Header.ProtocolVersion);
  Hash = FoldUnsigned(Hash, Header.SnapshotRevision);
  Hash = FoldUnsigned(Hash, static_cast<uint64>(Header.FixedStepIndex));
  Hash = FoldUnsigned(Hash, Header.RelevantSetRevision);
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Entities.Num()));
  for (const FCrowdRelevantSnapshotEntityPayload& Entity : Entities)
  {
    Hash = FoldUnsigned(Hash, static_cast<uint32>(Entity.Bytes.Num()));
    Hash = FoldBytes(Hash, Entity.Bytes);
  }
  return Hash;
}

uint64 FCrowdRelevantSnapshotTransport::CalculateChunkHash(
  const FCrowdRelevantSnapshotChunk& Chunk)
{
  uint64 Hash = FnvOffset64;
  Hash = FoldUnsigned(Hash, Chunk.SnapshotRevision);
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Chunk.ChunkIndex));
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Chunk.StartEntityIndex));
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Chunk.EntityCount));
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Chunk.PayloadOffsetBytes));
  Hash = FoldUnsigned(Hash, static_cast<uint32>(Chunk.Payload.Num()));
  return FoldBytes(Hash, Chunk.Payload);
}

bool FCrowdRelevantSnapshotAssembly::Begin(
  const uint32 ExpectedSnapshotRevision,
  const FCrowdRelevantSnapshotLimits& InLimits)
{
  Reset();
  if (ExpectedSnapshotRevision == 0 || !InLimits.IsValid())
  {
    return false;
  }
  ExpectedRevision = ExpectedSnapshotRevision;
  Limits = InLimits;
  bBegun = true;
  return true;
}

ECrowdRelevantSnapshotAcceptResult FCrowdRelevantSnapshotAssembly::AcceptHeader(
  const FCrowdRelevantSnapshotHeader& InHeader,
  const double NowSeconds)
{
  if (!bBegun || bFailed || !FMath::IsFinite(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (InHeader.SnapshotRevision != ExpectedRevision)
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (IsTimedOut(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::TimedOut;
  }
  if (!InHeader.IsWellFormed(Limits))
  {
    Fail();
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (bHasHeader)
  {
    if (!AreSameHeader(Header, InHeader))
    {
      Fail();
      return ECrowdRelevantSnapshotAcceptResult::Rejected;
    }
    return ECrowdRelevantSnapshotAcceptResult::Duplicate;
  }
  if (!Touch(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }

  Header = InHeader;
  bHasHeader = true;
  for (const TPair<int32, FCrowdRelevantSnapshotChunk>& Pair : ChunksByIndex)
  {
    if (!Pair.Value.IsWellFormed(Header, Limits))
    {
      Fail();
      return ECrowdRelevantSnapshotAcceptResult::Rejected;
    }
  }
  return IsComplete()
    ? ECrowdRelevantSnapshotAcceptResult::Complete
    : ECrowdRelevantSnapshotAcceptResult::Accepted;
}

ECrowdRelevantSnapshotAcceptResult FCrowdRelevantSnapshotAssembly::AcceptChunk(
  const FCrowdRelevantSnapshotChunk& Chunk,
  const double NowSeconds)
{
  if (!bBegun || bFailed || !FMath::IsFinite(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (Chunk.SnapshotRevision != ExpectedRevision)
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (IsTimedOut(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::TimedOut;
  }
  if (!Chunk.IsLocallyWellFormed(Limits)
    || Chunk.ChunkHash != FCrowdRelevantSnapshotTransport::CalculateChunkHash(Chunk)
    || (bHasHeader && !Chunk.IsWellFormed(Header, Limits)))
  {
    Fail();
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  if (const FCrowdRelevantSnapshotChunk* Existing =
    ChunksByIndex.Find(Chunk.ChunkIndex))
  {
    if (!AreSameChunk(*Existing, Chunk))
    {
      Fail();
      return ECrowdRelevantSnapshotAcceptResult::Rejected;
    }
    return ECrowdRelevantSnapshotAcceptResult::Duplicate;
  }
  if (!Touch(NowSeconds))
  {
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }

  ChunksByIndex.Add(Chunk.ChunkIndex, Chunk);
  ReceivedEntityCount += Chunk.EntityCount;
  if (ReceivedEntityCount > Limits.MaxEntityCount
    || (bHasHeader && ReceivedEntityCount > Header.EntityCount))
  {
    Fail();
    return ECrowdRelevantSnapshotAcceptResult::Rejected;
  }
  return IsComplete()
    ? ECrowdRelevantSnapshotAcceptResult::Complete
    : ECrowdRelevantSnapshotAcceptResult::Accepted;
}

bool FCrowdRelevantSnapshotAssembly::TryFinalize(
  const double NowSeconds,
  TArray<FCrowdRelevantSnapshotEntityPayload>& OutEntities)
{
  OutEntities.Reset();
  if (!bBegun || bFailed || IsTimedOut(NowSeconds) || !IsComplete())
  {
    return false;
  }
  if (Header.EntityCount == 0)
  {
    if (FCrowdRelevantSnapshotTransport::CalculateSnapshotHash(
      Header, OutEntities) == Header.SnapshotHash)
    {
      return true;
    }
    Fail();
    return false;
  }

  OutEntities.Reserve(Header.EntityCount);
  int32 ExpectedEntityIndex = 0;
  int32 ExpectedPayloadOffset = 0;
  for (int32 ChunkIndex = 0; ChunkIndex < Header.ChunkCount; ++ChunkIndex)
  {
    const FCrowdRelevantSnapshotChunk* Chunk = ChunksByIndex.Find(ChunkIndex);
    if (!Chunk
      || Chunk->StartEntityIndex != ExpectedEntityIndex
      || Chunk->PayloadOffsetBytes != ExpectedPayloadOffset)
    {
      OutEntities.Reset();
      Fail();
      return false;
    }

    int32 PayloadOffset = 0;
    for (int32 LocalEntityIndex = 0;
      LocalEntityIndex < Chunk->EntityCount;
      ++LocalEntityIndex)
    {
      uint32 EntityPayloadBytes = 0;
      if (!ReadUint32(Chunk->Payload, PayloadOffset, EntityPayloadBytes)
        || EntityPayloadBytes == 0
        || EntityPayloadBytes
          > static_cast<uint32>(Chunk->Payload.Num() - PayloadOffset))
      {
        OutEntities.Reset();
        Fail();
        return false;
      }
      FCrowdRelevantSnapshotEntityPayload& Entity =
        OutEntities.AddDefaulted_GetRef();
      Entity.Bytes.Append(
        Chunk->Payload.GetData() + PayloadOffset,
        static_cast<int32>(EntityPayloadBytes));
      PayloadOffset += static_cast<int32>(EntityPayloadBytes);
    }
    if (PayloadOffset != Chunk->Payload.Num())
    {
      OutEntities.Reset();
      Fail();
      return false;
    }
    ExpectedEntityIndex += Chunk->EntityCount;
    ExpectedPayloadOffset += Chunk->Payload.Num();
  }

  if (ExpectedEntityIndex != Header.EntityCount
    || ExpectedPayloadOffset != Header.PayloadByteCount
    || OutEntities.Num() != Header.EntityCount
    || FCrowdRelevantSnapshotTransport::CalculateSnapshotHash(
      Header, OutEntities) != Header.SnapshotHash)
  {
    OutEntities.Reset();
    Fail();
    return false;
  }
  return true;
}

bool FCrowdRelevantSnapshotAssembly::IsComplete() const
{
  return bBegun
    && !bFailed
    && bHasHeader
    && ChunksByIndex.Num() == Header.ChunkCount
    && ReceivedEntityCount == Header.EntityCount;
}

bool FCrowdRelevantSnapshotAssembly::IsTimedOut(const double NowSeconds) const
{
  return bBegun
    && !bFailed
    && !IsComplete()
    && FirstReceiveSeconds >= 0.0
    && FMath::IsFinite(NowSeconds)
    && NowSeconds - FirstReceiveSeconds > Limits.AssemblyTimeoutSeconds;
}

void FCrowdRelevantSnapshotAssembly::Reset()
{
  Header = {};
  Limits = {};
  ChunksByIndex.Reset();
  ExpectedRevision = 0;
  ReceivedEntityCount = 0;
  FirstReceiveSeconds = -1.0;
  LastReceiveSeconds = -1.0;
  bBegun = false;
  bHasHeader = false;
  bFailed = false;
}

bool FCrowdRelevantSnapshotAssembly::Touch(const double NowSeconds)
{
  if (!FMath::IsFinite(NowSeconds))
  {
    return false;
  }
  if (FirstReceiveSeconds < 0.0)
  {
    FirstReceiveSeconds = NowSeconds;
  }
  LastReceiveSeconds = NowSeconds;
  return true;
}

void FCrowdRelevantSnapshotAssembly::Fail()
{
  bFailed = true;
}
