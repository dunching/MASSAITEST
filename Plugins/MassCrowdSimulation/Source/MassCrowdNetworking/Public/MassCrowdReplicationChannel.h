#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"
#include "MassCrowdRelevantSnapshot.h"

enum class ECrowdReliableStateKind : uint8
{
  Spawn = 0,
  Despawn,
  RelevancyExit,
  Membership,
  Behavior,
  Task,
  Inventory,
  Cargo,
  PresentationEvent,
  HostEvent
};

struct FCrowdReplicationEntityLineage
{
  uint32 ProviderId = 0;
  uint64 StableEntityId = 0;

  bool operator==(const FCrowdReplicationEntityLineage& Other) const = default;
  friend uint32 GetTypeHash(const FCrowdReplicationEntityLineage& Value)
  {
    return HashCombine(
      GetTypeHash(Value.ProviderId), GetTypeHash(Value.StableEntityId));
  }
};

struct FCrowdTaskReplicationRecord
{
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef SourceRef;
  FCrowdStableEntityRef SinkRef;
  FCrowdStableEntityRef CarrierRef;
  int32 Quantity = 0;
  uint32 State = 0;
  uint32 Revision = 0;
};

struct FCrowdInventoryReplicationRecord
{
  FCrowdStableEntityRef OwnerRef;
  int32 OnHand = 0;
  int32 ReservedInbound = 0;
  int32 ReservedOutbound = 0;
  int32 InTransit = 0;
  int32 Capacity = 0;
  uint32 Revision = 0;
};

struct FCrowdCargoReplicationRecord
{
  FCrowdStableEntityRef CargoRef;
  FCrowdStableEntityRef CarrierRef;
  FCrowdStableEntityRef SourceRef;
  FCrowdStableEntityRef SinkRef;
  int32 Quantity = 0;
  uint32 State = 0;
  uint32 Revision = 0;
};

struct FCrowdAgentReplicationRecord
{
  FCrowdStableEntityRef EntityRef;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  uint32 MovementProfileKey = 0;
  uint8 Behavior = 0;
  uint32 Revision = 0;
};

struct FCrowdPresentationReplicationRecord
{
  FCrowdStableEntityRef EntityRef;
  FCrowdStableEntityRef CargoRef;
  FVector Location = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  uint32 ProfileKey = 0;
  uint8 VisualState = 0;
  uint32 Revision = 0;
};

struct FCrowdReliableStateRecord
{
  uint64 Sequence = 0;
  ECrowdReliableStateKind Kind = ECrowdReliableStateKind::Spawn;
  FCrowdStableEntityRef EntityRef;
  uint32 Revision = 0;
  TArray<uint8> Payload;
  uint64 StableHash = 0;
};

struct FCrowdReliableStateBatch
{
  uint64 FirstSequence = 0;
  TArray<FCrowdReliableStateRecord> Records;
  uint64 StableHash = 0;
};

struct FCrowdMovementCorrectionRecord
{
  FCrowdStableEntityRef EntityRef;
  uint64 Sequence = 0;
  int64 FixedStepIndex = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  uint64 StableHash = 0;
};

struct FCrowdBaselineBegin
{
  uint32 BaselineRevision = 0;
  uint64 ResumeReliableSequence = 0;
  uint64 StableHash = 0;
};

struct FCrowdBaselineEnd
{
  uint32 BaselineRevision = 0;
  uint64 ResumeReliableSequence = 0;
  uint64 SnapshotHash = 0;
};

enum class ECrowdReplicationApplyFrameKind : uint8
{
  Baseline = 0,
  ReliableState,
  MovementCorrection
};

struct FCrowdReplicationApplyFrame
{
  static constexpr uint16 CurrentVersion = 1;

  uint16 Version = CurrentVersion;
  ECrowdReplicationApplyFrameKind Kind =
    ECrowdReplicationApplyFrameKind::Baseline;
  uint32 BaselineRevision = 0;
  uint64 FirstReliableSequence = 0;
  TArray<FCrowdRelevantSnapshotEntityPayload> BaselineEntities;
  TArray<FCrowdReliableStateRecord> ReliableRecords;
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  uint64 StableHash = 0;
  bool bValid = false;
};

struct FCrowdReplicationChannelLimits
{
  int32 MaxReliableRecordsPerBatch = 256;
  int32 MaxReliablePayloadBytesPerRecord = 4096;
  int32 MaxBufferedReliableRecords = 2048;
  int32 MaxCorrectionRecords = 1024;
  int32 MaxPendingApplyFrames = 64;
  int32 MaxDuplicateHistoryRecords = 4096;
  FCrowdRelevantSnapshotLimits SnapshotLimits;

  bool IsValid() const;
};

enum class ECrowdReplicationAcceptResult : uint8
{
  Accepted = 0,
  Duplicate,
  BaselineComplete,
  IgnoredStaleCorrection,
  ResyncRequired,
  Rejected
};

class MASSCROWDNETWORKING_API FCrowdReplicationTransport
{
public:
  static uint64 CalculateReliableRecordHash(
    const FCrowdReliableStateRecord& Record);
  static uint64 CalculateReliableBatchHash(
    const FCrowdReliableStateBatch& Batch);
  static uint64 CalculateMovementCorrectionHash(
    const FCrowdMovementCorrectionRecord& Record);
  static uint64 CalculateApplyFrameHash(
    const FCrowdReplicationApplyFrame& Frame);
};

class MASSCROWDNETWORKING_API FCrowdReplicationCodec
{
public:
  static bool EncodeAgent(
    const FCrowdAgentReplicationRecord& Record, TArray<uint8>& OutBytes);
  static bool DecodeAgent(
    TConstArrayView<uint8> Bytes, FCrowdAgentReplicationRecord& OutRecord);
  static bool EncodeTask(
    const FCrowdTaskReplicationRecord& Record, TArray<uint8>& OutBytes);
  static bool DecodeTask(
    TConstArrayView<uint8> Bytes, FCrowdTaskReplicationRecord& OutRecord);
  static bool EncodeInventory(
    const FCrowdInventoryReplicationRecord& Record, TArray<uint8>& OutBytes);
  static bool DecodeInventory(
    TConstArrayView<uint8> Bytes,
    FCrowdInventoryReplicationRecord& OutRecord);
  static bool EncodeCargo(
    const FCrowdCargoReplicationRecord& Record, TArray<uint8>& OutBytes);
  static bool DecodeCargo(
    TConstArrayView<uint8> Bytes, FCrowdCargoReplicationRecord& OutRecord);
  static bool EncodePresentation(
    const FCrowdPresentationReplicationRecord& Record,
    TArray<uint8>& OutBytes);
  static bool DecodePresentation(
    TConstArrayView<uint8> Bytes,
    FCrowdPresentationReplicationRecord& OutRecord);
};

class MASSCROWDNETWORKING_API FCrowdReplicationClientState
{
public:
  bool Initialize(const FCrowdReplicationChannelLimits& Limits);

  ECrowdReplicationAcceptResult AcceptBaselineBegin(
    const FCrowdBaselineBegin& Begin,
    double NowSeconds);
  ECrowdReplicationAcceptResult AcceptSnapshotHeader(
    const FCrowdRelevantSnapshotHeader& Header,
    double NowSeconds);
  ECrowdReplicationAcceptResult AcceptSnapshotChunk(
    const FCrowdRelevantSnapshotChunk& Chunk,
    double NowSeconds);
  ECrowdReplicationAcceptResult AcceptBaselineEnd(
    const FCrowdBaselineEnd& End,
    double NowSeconds);

  ECrowdReplicationAcceptResult AcceptReliableBatch(
    const FCrowdReliableStateBatch& Batch);
  ECrowdReplicationAcceptResult AcceptMovementCorrection(
    const FCrowdMovementCorrectionRecord& Correction);
  ECrowdReplicationAcceptResult AcceptMovementCorrections(
    TConstArrayView<FCrowdMovementCorrectionRecord> Corrections);

  bool ConsumeCompletedBaseline(
    TArray<FCrowdRelevantSnapshotEntityPayload>& OutEntities,
    uint32& OutBaselineRevision,
    uint64& OutResumeReliableSequence);
  bool DrainApplyFrames(
    TArray<FCrowdReplicationApplyFrame>& OutFrames);

  bool RequiresResync() const { return bRequiresResync; }
  uint64 GetNextReliableSequence() const { return NextReliableSequence; }
  int32 GetLatestCorrectionCount() const
  {
    return LatestCorrections.Num();
  }

private:
  void RequireResync();

  FCrowdReplicationChannelLimits Limits;
  FCrowdRelevantSnapshotAssembly Assembly;
  FCrowdBaselineBegin PendingBegin;
  FCrowdBaselineEnd PendingEnd;
  TArray<FCrowdRelevantSnapshotEntityPayload> CompletedEntities;
  TArray<FCrowdReliableStateRecord> AppliedReliableRecords;
  TArray<FCrowdReplicationApplyFrame> PendingApplyFrames;
  TMap<uint64, uint64> AppliedReliableHashes;
  TMap<FCrowdStableEntityRef, FCrowdMovementCorrectionRecord>
    LatestCorrections;
  TMap<FCrowdReplicationEntityLineage, uint32> LatestLifecycleByEntity;
  uint64 NextReliableSequence = 1;
  bool bInitialized = false;
  bool bHasBegin = false;
  bool bHasEnd = false;
  bool bBaselineReady = false;
  bool bRequiresResync = false;
};

class MASSCROWDNETWORKING_API FCrowdReplicationServerState
{
public:
  bool Initialize(const FCrowdReplicationChannelLimits& Limits);
  bool BeginBaseline(
    const FCrowdBaselineBegin& Begin,
    const FCrowdRelevantSnapshotHeader& Header);
  bool BufferReliable(const FCrowdReliableStateRecord& Record);
  bool AckBaseline(uint32 BaselineRevision, uint64 ResumeSequence);
  bool ConsumeBufferedReliable(TArray<FCrowdReliableStateRecord>& OutRecords);

  bool RequiresNewBaseline() const { return bRequiresNewBaseline; }
  bool IsAwaitingAck() const { return bAwaitingAck; }

private:
  FCrowdReplicationChannelLimits Limits;
  FCrowdBaselineBegin PendingBegin;
  FCrowdRelevantSnapshotHeader PendingHeader;
  TArray<FCrowdReliableStateRecord> BufferedReliable;
  bool bInitialized = false;
  bool bAwaitingAck = false;
  bool bRequiresNewBaseline = false;
};
