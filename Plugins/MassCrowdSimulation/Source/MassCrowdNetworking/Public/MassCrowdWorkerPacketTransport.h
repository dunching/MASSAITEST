#pragma once

#include "CoreMinimal.h"

enum class ECrowdWorkerPacketKind : uint8
{
  Checkpoint = 1,
  Intent = 2,
  Correction = 3
};

enum class ECrowdWorkerPacketAcceptResult : uint8
{
  Accepted = 0,
  Duplicate,
  Complete,
  Rejected,
  RequiresResync
};

struct MASSCROWDNETWORKING_API FCrowdWorkerPacketTransportConfig
{
  static constexpr int32 ReliableRpcSafeChunkBytes = 4 * 1024;

  int32 MaxChunkBytes = ReliableRpcSafeChunkBytes;
  int32 MaxPacketBytes = 64 * 1024 * 1024;
  int32 MaxChunkCount = 2048;
  double AssemblyTimeoutSeconds = 10.0;

  bool IsValid() const
  {
    return MaxChunkBytes > 0
      && MaxPacketBytes > 0
      && MaxChunkCount > 0
      && AssemblyTimeoutSeconds > 0.0;
  }
};

struct MASSCROWDNETWORKING_API FCrowdWorkerPacketHeader
{
  static constexpr uint16 CurrentVersion = 1;

  uint16 Version = CurrentVersion;
  ECrowdWorkerPacketKind Kind = ECrowdWorkerPacketKind::Checkpoint;
  uint64 Generation = 0;
  uint64 Sequence = 0;
  uint64 ObjectStableHash = 0;
  int32 TotalBytes = 0;
  int32 ChunkCount = 0;
  uint64 StableHash = 0;

  bool operator==(const FCrowdWorkerPacketHeader&) const = default;
};

struct MASSCROWDNETWORKING_API FCrowdWorkerPacketChunk
{
  uint64 Sequence = 0;
  int32 ChunkIndex = INDEX_NONE;
  TArray<uint8> Bytes;
  uint64 StableHash = 0;

  bool operator==(const FCrowdWorkerPacketChunk&) const = default;
};

struct MASSCROWDNETWORKING_API FCrowdWorkerPacketEnd
{
  uint64 Sequence = 0;
  uint64 ObjectStableHash = 0;
  uint64 StableHash = 0;

  bool operator==(const FCrowdWorkerPacketEnd&) const = default;
};

struct MASSCROWDNETWORKING_API FCrowdWorkerAssembledPacket
{
  FCrowdWorkerPacketHeader Header;
  TArray<uint8> Bytes;
};

class MASSCROWDNETWORKING_API FCrowdWorkerPacketTransport
{
public:
  static uint64 CalculateHeaderHash(
    const FCrowdWorkerPacketHeader& Header);
  static uint64 CalculateChunkHash(
    const FCrowdWorkerPacketChunk& Chunk);
  static uint64 CalculateEndHash(const FCrowdWorkerPacketEnd& End);

  static bool Build(
    ECrowdWorkerPacketKind Kind,
    uint64 Generation,
    uint64 Sequence,
    uint64 ObjectStableHash,
    TConstArrayView<uint8> Bytes,
    const FCrowdWorkerPacketTransportConfig& Config,
    FCrowdWorkerPacketHeader& OutHeader,
    TArray<FCrowdWorkerPacketChunk>& OutChunks,
    FCrowdWorkerPacketEnd& OutEnd);
};

class MASSCROWDNETWORKING_API FCrowdWorkerPacketAssembler
{
public:
  bool Initialize(const FCrowdWorkerPacketTransportConfig& InConfig);
  void Reset();

  ECrowdWorkerPacketAcceptResult AcceptHeader(
    const FCrowdWorkerPacketHeader& Header,
    double NowSeconds);
  ECrowdWorkerPacketAcceptResult AcceptChunk(
    const FCrowdWorkerPacketChunk& Chunk,
    double NowSeconds);
  ECrowdWorkerPacketAcceptResult AcceptEnd(
    const FCrowdWorkerPacketEnd& End,
    double NowSeconds);
  bool ConsumeCompleted(FCrowdWorkerAssembledPacket& OutPacket);

private:
  bool HasTimedOut(double NowSeconds) const;
  bool ValidateHeader(const FCrowdWorkerPacketHeader& Header) const;

  FCrowdWorkerPacketTransportConfig Config;
  FCrowdWorkerPacketHeader ActiveHeader;
  TArray<FCrowdWorkerPacketChunk> Chunks;
  FCrowdWorkerAssembledPacket Completed;
  double BeginSeconds = 0.0;
  int32 ReceivedBytes = 0;
  bool bInitialized = false;
  bool bActive = false;
  bool bCompleted = false;
};
