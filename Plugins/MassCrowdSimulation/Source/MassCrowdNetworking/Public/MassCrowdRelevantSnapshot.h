#pragma once

#include "CoreMinimal.h"

#include "MassCrowdRelevantSnapshot.generated.h"

struct FCrowdRelevantSnapshotLimits
{
  int32 MaxEntityCount = 0;
  int32 MaxChunkCount = 0;
  int32 MaxEntitiesPerChunk = 0;
  int32 MaxChunkPayloadBytes = 0;
  int32 MaxTotalPayloadBytes = 0;
  double AssemblyTimeoutSeconds = 0.0;

  bool IsValid() const;
};

struct FCrowdRelevantSnapshotEntityPayload
{
  TArray<uint8> Bytes;

  bool operator==(const FCrowdRelevantSnapshotEntityPayload& Other) const = default;
};

USTRUCT()
struct MASSCROWDNETWORKING_API FCrowdRelevantSnapshotHeader
{
  GENERATED_BODY()

  static constexpr uint16 CurrentProtocolVersion = 2;

  UPROPERTY() uint16 ProtocolVersion = CurrentProtocolVersion;
  UPROPERTY() uint32 SnapshotRevision = 0;
  UPROPERTY() int64 FixedStepIndex = 0;
  UPROPERTY() uint32 RelevantSetRevision = 0;
  UPROPERTY() int32 EntityCount = 0;
  UPROPERTY() int32 ChunkCount = 0;
  UPROPERTY() int32 PayloadByteCount = 0;
  UPROPERTY() uint64 SnapshotHash = 0;

  bool IsWellFormed(const FCrowdRelevantSnapshotLimits& Limits) const;
};

USTRUCT()
struct MASSCROWDNETWORKING_API FCrowdRelevantSnapshotChunk
{
  GENERATED_BODY()

  UPROPERTY() uint32 SnapshotRevision = 0;
  UPROPERTY() int32 ChunkIndex = INDEX_NONE;
  UPROPERTY() int32 StartEntityIndex = 0;
  UPROPERTY() int32 EntityCount = 0;
  UPROPERTY() int32 PayloadOffsetBytes = 0;
  UPROPERTY() uint64 ChunkHash = 0;
  UPROPERTY() TArray<uint8> Payload;

  bool IsLocallyWellFormed(const FCrowdRelevantSnapshotLimits& Limits) const;
  bool IsWellFormed(
    const FCrowdRelevantSnapshotHeader& Header,
    const FCrowdRelevantSnapshotLimits& Limits) const;
};

enum class ECrowdRelevantSnapshotAcceptResult : uint8
{
  Accepted,
  Duplicate,
  Complete,
  Rejected,
  TimedOut
};

class MASSCROWDNETWORKING_API FCrowdRelevantSnapshotTransport
{
public:
  static bool Build(
    uint32 SnapshotRevision,
    int64 FixedStepIndex,
    uint32 RelevantSetRevision,
    TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Entities,
    const FCrowdRelevantSnapshotLimits& Limits,
    FCrowdRelevantSnapshotHeader& OutHeader,
    TArray<FCrowdRelevantSnapshotChunk>& OutChunks);

  static uint64 CalculateSnapshotHash(
    const FCrowdRelevantSnapshotHeader& Header,
    TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Entities);

  static uint64 CalculateChunkHash(const FCrowdRelevantSnapshotChunk& Chunk);
};

class MASSCROWDNETWORKING_API FCrowdRelevantSnapshotAssembly
{
public:
  bool Begin(uint32 ExpectedSnapshotRevision, const FCrowdRelevantSnapshotLimits& InLimits);

  ECrowdRelevantSnapshotAcceptResult AcceptHeader(
    const FCrowdRelevantSnapshotHeader& InHeader,
    double NowSeconds);

  ECrowdRelevantSnapshotAcceptResult AcceptChunk(
    const FCrowdRelevantSnapshotChunk& Chunk,
    double NowSeconds);

  bool TryFinalize(
    double NowSeconds,
    TArray<FCrowdRelevantSnapshotEntityPayload>& OutEntities);

  bool IsComplete() const;
  bool IsTimedOut(double NowSeconds) const;
  bool HasFailed() const { return bFailed; }
  void Reset();

  const FCrowdRelevantSnapshotHeader* GetHeader() const
  {
    return bHasHeader ? &Header : nullptr;
  }
  int32 GetReceivedChunkCount() const { return ChunksByIndex.Num(); }
  int32 GetReceivedEntityCount() const { return ReceivedEntityCount; }

private:
  bool Touch(double NowSeconds);
  void Fail();

  FCrowdRelevantSnapshotHeader Header;
  FCrowdRelevantSnapshotLimits Limits;
  TMap<int32, FCrowdRelevantSnapshotChunk> ChunksByIndex;
  uint32 ExpectedRevision = 0;
  int32 ReceivedEntityCount = 0;
  double FirstReceiveSeconds = -1.0;
  double LastReceiveSeconds = -1.0;
  bool bBegun = false;
  bool bHasHeader = false;
  bool bFailed = false;
};
