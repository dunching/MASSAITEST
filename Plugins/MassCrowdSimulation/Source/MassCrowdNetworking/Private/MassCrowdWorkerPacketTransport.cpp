#include "MassCrowdWorkerPacketTransport.h"

namespace CrowdWorkerPacketTransportPrivate
{
constexpr uint64 Offset = 14695981039346656037ull;
constexpr uint64 Prime = 1099511628211ull;

template <typename T>
void Fold(uint64& Hash, const T Value)
{
  static_assert(TIsIntegral<T>::Value || TIsEnum<T>::Value);
  const uint64 Unsigned = static_cast<uint64>(Value);
  for (uint32 Byte = 0; Byte < sizeof(T); ++Byte)
  {
    Hash ^= static_cast<uint8>(Unsigned >> (Byte * 8));
    Hash *= Prime;
  }
}

void FoldBytes(uint64& Hash, const TConstArrayView<uint8> Bytes)
{
  Fold(Hash, static_cast<uint32>(Bytes.Num()));
  for (const uint8 Byte : Bytes)
  {
    Hash ^= Byte;
    Hash *= Prime;
  }
}

bool IsKnownKind(const ECrowdWorkerPacketKind Kind)
{
  return Kind == ECrowdWorkerPacketKind::Checkpoint
    || Kind == ECrowdWorkerPacketKind::Intent
    || Kind == ECrowdWorkerPacketKind::Correction;
}
}

using namespace CrowdWorkerPacketTransportPrivate;

uint64 FCrowdWorkerPacketTransport::CalculateHeaderHash(
  const FCrowdWorkerPacketHeader& Header)
{
  uint64 Hash = Offset;
  Fold(Hash, Header.Version);
  Fold(Hash, Header.Kind);
  Fold(Hash, Header.Generation);
  Fold(Hash, Header.Sequence);
  Fold(Hash, Header.ObjectStableHash);
  Fold(Hash, Header.TotalBytes);
  Fold(Hash, Header.ChunkCount);
  return Hash;
}

uint64 FCrowdWorkerPacketTransport::CalculateChunkHash(
  const FCrowdWorkerPacketChunk& Chunk)
{
  uint64 Hash = Offset;
  Fold(Hash, Chunk.Sequence);
  Fold(Hash, Chunk.ChunkIndex);
  FoldBytes(Hash, Chunk.Bytes);
  return Hash;
}

uint64 FCrowdWorkerPacketTransport::CalculateEndHash(
  const FCrowdWorkerPacketEnd& End)
{
  uint64 Hash = Offset;
  Fold(Hash, End.Sequence);
  Fold(Hash, End.ObjectStableHash);
  return Hash;
}

bool FCrowdWorkerPacketTransport::Build(
  const ECrowdWorkerPacketKind Kind,
  const uint64 Generation,
  const uint64 Sequence,
  const uint64 ObjectStableHash,
  const TConstArrayView<uint8> Bytes,
  const FCrowdWorkerPacketTransportConfig& Config,
  FCrowdWorkerPacketHeader& OutHeader,
  TArray<FCrowdWorkerPacketChunk>& OutChunks,
  FCrowdWorkerPacketEnd& OutEnd)
{
  OutHeader = {};
  OutChunks.Reset();
  OutEnd = {};
  if (!Config.IsValid() || !IsKnownKind(Kind)
    || Generation == 0 || Sequence == 0 || ObjectStableHash == 0
    || Bytes.IsEmpty() || Bytes.Num() > Config.MaxPacketBytes)
    return false;
  const int32 ChunkCount = FMath::DivideAndRoundUp(
    Bytes.Num(), Config.MaxChunkBytes);
  if (ChunkCount <= 0 || ChunkCount > Config.MaxChunkCount)
    return false;

  OutHeader.Kind = Kind;
  OutHeader.Generation = Generation;
  OutHeader.Sequence = Sequence;
  OutHeader.ObjectStableHash = ObjectStableHash;
  OutHeader.TotalBytes = Bytes.Num();
  OutHeader.ChunkCount = ChunkCount;
  OutHeader.StableHash = CalculateHeaderHash(OutHeader);
  OutChunks.Reserve(ChunkCount);
  for (int32 Index = 0; Index < ChunkCount; ++Index)
  {
    const int32 Begin = Index * Config.MaxChunkBytes;
    const int32 Count = FMath::Min(
      Config.MaxChunkBytes, Bytes.Num() - Begin);
    FCrowdWorkerPacketChunk& Chunk = OutChunks.AddDefaulted_GetRef();
    Chunk.Sequence = Sequence;
    Chunk.ChunkIndex = Index;
    Chunk.Bytes.Append(Bytes.GetData() + Begin, Count);
    Chunk.StableHash = CalculateChunkHash(Chunk);
  }
  OutEnd.Sequence = Sequence;
  OutEnd.ObjectStableHash = ObjectStableHash;
  OutEnd.StableHash = CalculateEndHash(OutEnd);
  return true;
}

bool FCrowdWorkerPacketAssembler::Initialize(
  const FCrowdWorkerPacketTransportConfig& InConfig)
{
  Reset();
  if (!InConfig.IsValid()) return false;
  Config = InConfig;
  bInitialized = true;
  return true;
}

void FCrowdWorkerPacketAssembler::Reset()
{
  ActiveHeader = {};
  Chunks.Reset();
  Completed = {};
  BeginSeconds = 0.0;
  ReceivedBytes = 0;
  bInitialized = false;
  bActive = false;
  bCompleted = false;
}

bool FCrowdWorkerPacketAssembler::HasTimedOut(
  const double NowSeconds) const
{
  return bActive
    && (!FMath::IsFinite(NowSeconds)
      || NowSeconds < BeginSeconds
      || NowSeconds - BeginSeconds > Config.AssemblyTimeoutSeconds);
}

bool FCrowdWorkerPacketAssembler::ValidateHeader(
  const FCrowdWorkerPacketHeader& Header) const
{
  if (Header.Version != FCrowdWorkerPacketHeader::CurrentVersion
    || !IsKnownKind(Header.Kind)
    || Header.Generation == 0 || Header.Sequence == 0
    || Header.ObjectStableHash == 0
    || Header.TotalBytes <= 0
    || Header.TotalBytes > Config.MaxPacketBytes
    || Header.ChunkCount <= 0
    || Header.ChunkCount > Config.MaxChunkCount
    || Header.ChunkCount != FMath::DivideAndRoundUp(
      Header.TotalBytes, Config.MaxChunkBytes))
    return false;
  return Header.StableHash
    == FCrowdWorkerPacketTransport::CalculateHeaderHash(Header);
}

ECrowdWorkerPacketAcceptResult FCrowdWorkerPacketAssembler::AcceptHeader(
  const FCrowdWorkerPacketHeader& Header,
  const double NowSeconds)
{
  if (!bInitialized || !FMath::IsFinite(NowSeconds)
    || !ValidateHeader(Header))
    return ECrowdWorkerPacketAcceptResult::Rejected;
  if (HasTimedOut(NowSeconds))
  {
    bActive = false;
    Chunks.Reset();
    return ECrowdWorkerPacketAcceptResult::RequiresResync;
  }
  if (bActive)
    return Header == ActiveHeader
      ? ECrowdWorkerPacketAcceptResult::Duplicate
      : ECrowdWorkerPacketAcceptResult::RequiresResync;
  if (bCompleted)
    return Header == Completed.Header
      ? ECrowdWorkerPacketAcceptResult::Duplicate
      : ECrowdWorkerPacketAcceptResult::RequiresResync;
  ActiveHeader = Header;
  Chunks.Reset(Header.ChunkCount);
  ReceivedBytes = 0;
  BeginSeconds = NowSeconds;
  bActive = true;
  return ECrowdWorkerPacketAcceptResult::Accepted;
}

ECrowdWorkerPacketAcceptResult FCrowdWorkerPacketAssembler::AcceptChunk(
  const FCrowdWorkerPacketChunk& Chunk,
  const double NowSeconds)
{
  if (!bInitialized || !bActive)
    return ECrowdWorkerPacketAcceptResult::Rejected;
  if (HasTimedOut(NowSeconds))
  {
    bActive = false;
    Chunks.Reset();
    return ECrowdWorkerPacketAcceptResult::RequiresResync;
  }
  if (Chunk.Sequence != ActiveHeader.Sequence
    || Chunk.ChunkIndex < 0
    || Chunk.ChunkIndex >= ActiveHeader.ChunkCount
    || Chunk.Bytes.IsEmpty()
    || Chunk.Bytes.Num() > Config.MaxChunkBytes
    || Chunk.StableHash
      != FCrowdWorkerPacketTransport::CalculateChunkHash(Chunk))
    return ECrowdWorkerPacketAcceptResult::Rejected;
  if (Chunk.ChunkIndex != Chunks.Num())
  {
    if (Chunk.ChunkIndex < Chunks.Num()
      && Chunk == Chunks[Chunk.ChunkIndex])
      return ECrowdWorkerPacketAcceptResult::Duplicate;
    return ECrowdWorkerPacketAcceptResult::RequiresResync;
  }
  const int32 ExpectedBytes = Chunk.ChunkIndex
      == ActiveHeader.ChunkCount - 1
    ? ActiveHeader.TotalBytes - ReceivedBytes
    : Config.MaxChunkBytes;
  if (Chunk.Bytes.Num() != ExpectedBytes
    || ReceivedBytes > ActiveHeader.TotalBytes - Chunk.Bytes.Num())
    return ECrowdWorkerPacketAcceptResult::Rejected;
  Chunks.Add(Chunk);
  ReceivedBytes += Chunk.Bytes.Num();
  return ECrowdWorkerPacketAcceptResult::Accepted;
}

ECrowdWorkerPacketAcceptResult FCrowdWorkerPacketAssembler::AcceptEnd(
  const FCrowdWorkerPacketEnd& End,
  const double NowSeconds)
{
  if (!bInitialized || !bActive)
    return ECrowdWorkerPacketAcceptResult::Rejected;
  if (HasTimedOut(NowSeconds))
  {
    bActive = false;
    Chunks.Reset();
    return ECrowdWorkerPacketAcceptResult::RequiresResync;
  }
  if (End.Sequence != ActiveHeader.Sequence
    || End.ObjectStableHash != ActiveHeader.ObjectStableHash
    || End.StableHash
      != FCrowdWorkerPacketTransport::CalculateEndHash(End)
    || Chunks.Num() != ActiveHeader.ChunkCount
    || ReceivedBytes != ActiveHeader.TotalBytes)
    return ECrowdWorkerPacketAcceptResult::Rejected;

  Completed = {};
  Completed.Header = ActiveHeader;
  Completed.Bytes.Reserve(ReceivedBytes);
  for (const FCrowdWorkerPacketChunk& Chunk : Chunks)
    Completed.Bytes.Append(Chunk.Bytes);
  Chunks.Reset();
  bActive = false;
  bCompleted = true;
  return ECrowdWorkerPacketAcceptResult::Complete;
}

bool FCrowdWorkerPacketAssembler::ConsumeCompleted(
  FCrowdWorkerAssembledPacket& OutPacket)
{
  OutPacket = {};
  if (!bCompleted) return false;
  OutPacket = MoveTemp(Completed);
  bCompleted = false;
  return true;
}
