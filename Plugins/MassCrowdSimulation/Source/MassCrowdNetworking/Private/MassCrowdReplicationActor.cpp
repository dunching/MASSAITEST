#include "MassCrowdReplicationActor.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdReplicationActor)

namespace
{
  uint64 CalculateBaselineBeginHash(
    const uint32 Revision, const uint64 ResumeSequence)
  {
    constexpr uint64 Offset = 14695981039346656037ull;
    constexpr uint64 Prime = 1099511628211ull;
    uint64 Hash = Offset;
    const uint64 Values[] = {1, Revision, ResumeSequence};
    for (const uint64 Value : Values)
      for (int32 Byte = 0; Byte < 8; ++Byte)
      {
        Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
        Hash *= Prime;
      }
    return Hash;
  }
}

AMassCrowdReplicationActor::AMassCrowdReplicationActor()
{
  bReplicates = true;
  bAlwaysRelevant = false;
  bOnlyRelevantToOwner = true;
  SetReplicateMovement(false);
  SetNetUpdateFrequency(30.0f);
  Limits.MaxReliableRecordsPerBatch = 256;
  Limits.MaxReliablePayloadBytesPerRecord = 4096;
  Limits.MaxBufferedReliableRecords = 2048;
  Limits.MaxCorrectionRecords = 2048;
  Limits.SnapshotLimits.MaxEntityCount = 4096;
  Limits.SnapshotLimits.MaxChunkCount = 4096;
  Limits.SnapshotLimits.MaxEntitiesPerChunk = 128;
  Limits.SnapshotLimits.MaxChunkPayloadBytes = 64 * 1024;
  Limits.SnapshotLimits.MaxTotalPayloadBytes = 16 * 1024 * 1024;
  Limits.SnapshotLimits.AssemblyTimeoutSeconds = 10.0;
  ServerState.Initialize(Limits);
  ClientState.Initialize(Limits);
}

void AMassCrowdReplicationActor::BeginPlay()
{
  Super::BeginPlay();
  UE_LOG(LogTemp, Display,
    TEXT("MassCrowdReplicationChannel role=%s stage=begin owner=%s"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    *GetNameSafe(GetOwner()));
}

AMassCrowdReplicationActor* AMassCrowdReplicationActor::SpawnForController(
  APlayerController& Controller)
{
  UWorld* World = Controller.GetWorld();
  if (!World || !Controller.HasAuthority()) return nullptr;
  FActorSpawnParameters Parameters;
  Parameters.Owner = &Controller;
  Parameters.SpawnCollisionHandlingOverride =
    ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  return World->SpawnActor<AMassCrowdReplicationActor>(
    AMassCrowdReplicationActor::StaticClass(),
    FTransform::Identity, Parameters);
}

bool AMassCrowdReplicationActor::PublishBaseline(
  const FCrowdRelevantSnapshotHeader& Header,
  const TConstArrayView<FCrowdRelevantSnapshotChunk> Chunks,
  const uint64 ResumeReliableSequence)
{
  if (!HasAuthority() || ResumeReliableSequence == 0
    || Chunks.Num() != Header.ChunkCount)
    return false;
  const FCrowdBaselineBegin Begin{
    Header.SnapshotRevision,
    ResumeReliableSequence,
    CalculateBaselineBeginHash(
      Header.SnapshotRevision, ResumeReliableSequence)};
  if (!ServerState.BeginBaseline(Begin, Header)) return false;
  UE_LOG(LogTemp, Display,
    TEXT("MassCrowdReplicationChannel role=server stage=baseline revision=%u resume=%llu entities=%d chunks=%d"),
    Header.SnapshotRevision, ResumeReliableSequence,
    Header.EntityCount, Header.ChunkCount);
  ClientBaselineBegin(
    Begin.BaselineRevision, Begin.ResumeReliableSequence, Begin.StableHash);
  ClientSnapshotHeader(Header);
  for (const FCrowdRelevantSnapshotChunk& Chunk : Chunks)
    ClientSnapshotChunk(Chunk);
  ClientBaselineEnd(
    Header.SnapshotRevision, ResumeReliableSequence, Header.SnapshotHash);
  return true;
}

bool AMassCrowdReplicationActor::PublishReliable(
  const FCrowdReliableStateRecord& Record)
{
  return PublishReliables(MakeArrayView(&Record, 1));
}

bool AMassCrowdReplicationActor::PublishReliables(
  const TConstArrayView<FCrowdReliableStateRecord> Records)
{
  if (!HasAuthority() || Records.IsEmpty()
    || Records.Num() > Limits.MaxBufferedReliableRecords)
    return false;
  uint64 ExpectedSequence = Records[0].Sequence;
  for (const FCrowdReliableStateRecord& Record : Records)
  {
    if (Record.Sequence != ExpectedSequence++
      || Record.Payload.Num() > Limits.MaxReliablePayloadBytesPerRecord
      || Record.StableHash
        != FCrowdReplicationTransport::CalculateReliableRecordHash(Record))
      return false;
  }
  if (ServerState.IsAwaitingAck())
  {
    for (const FCrowdReliableStateRecord& Record : Records)
      if (!ServerState.BufferReliable(Record))
        return false;
    return true;
  }
  if (ServerState.RequiresNewBaseline()) return false;
  for (int32 Begin = 0; Begin < Records.Num();
    Begin += Limits.MaxReliableRecordsPerBatch)
  {
    SendReliableBatch(MakeArrayView(
      Records.GetData() + Begin,
      FMath::Min(
        Limits.MaxReliableRecordsPerBatch,
        Records.Num() - Begin)));
  }
  return true;
}

bool AMassCrowdReplicationActor::PublishMovementCorrection(
  const FCrowdMovementCorrectionRecord& Correction)
{
  if (!HasAuthority()
    || Correction.StableHash
      != FCrowdReplicationTransport::CalculateMovementCorrectionHash(
        Correction))
    return false;
  ClientMovementCorrection(
    Correction.EntityRef.ProviderId,
    Correction.EntityRef.StableEntityId,
    Correction.EntityRef.LifecycleSerial,
    Correction.Sequence,
    Correction.FixedStepIndex,
    Correction.Position,
    Correction.Velocity,
    Correction.YawDegrees,
    Correction.StableHash);
  return true;
}

bool AMassCrowdReplicationActor::PublishMovementCorrections(
  const TConstArrayView<FCrowdMovementCorrectionRecord> Corrections)
{
  if (!HasAuthority() || Corrections.IsEmpty()
    || Corrections.Num() > Limits.MaxCorrectionRecords)
    return false;

  TArray<uint32> ProviderIds;
  TArray<uint64> StableEntityIds;
  TArray<uint32> LifecycleSerials;
  TArray<uint64> Sequences;
  TArray<int64> FixedStepIndices;
  TArray<FVector> Positions;
  TArray<FVector> Velocities;
  TArray<float> YawDegrees;
  TArray<uint64> StableHashes;
  ProviderIds.Reserve(Corrections.Num());
  StableEntityIds.Reserve(Corrections.Num());
  LifecycleSerials.Reserve(Corrections.Num());
  Sequences.Reserve(Corrections.Num());
  FixedStepIndices.Reserve(Corrections.Num());
  Positions.Reserve(Corrections.Num());
  Velocities.Reserve(Corrections.Num());
  YawDegrees.Reserve(Corrections.Num());
  StableHashes.Reserve(Corrections.Num());
  for (const FCrowdMovementCorrectionRecord& Correction : Corrections)
  {
    if (!Correction.EntityRef.IsValid()
      || Correction.StableHash
        != FCrowdReplicationTransport::CalculateMovementCorrectionHash(
          Correction))
      return false;
    ProviderIds.Add(Correction.EntityRef.ProviderId);
    StableEntityIds.Add(Correction.EntityRef.StableEntityId);
    LifecycleSerials.Add(Correction.EntityRef.LifecycleSerial);
    Sequences.Add(Correction.Sequence);
    FixedStepIndices.Add(Correction.FixedStepIndex);
    Positions.Add(Correction.Position);
    Velocities.Add(Correction.Velocity);
    YawDegrees.Add(Correction.YawDegrees);
    StableHashes.Add(Correction.StableHash);
  }
  ClientMovementCorrectionBatch(
    ProviderIds, StableEntityIds, LifecycleSerials, Sequences,
    FixedStepIndices, Positions, Velocities, YawDegrees, StableHashes);
  UE_LOG(LogTemp, Verbose,
    TEXT("MassCrowdReplicationChannel role=server stage=correction_batch records=%d"),
    Corrections.Num());
  return true;
}

void AMassCrowdReplicationActor::ClientBaselineBegin_Implementation(
  const uint32 Revision,
  const uint64 ResumeSequence,
  const uint64 StableHash)
{
  bClientReady = false;
  if (ClientState.AcceptBaselineBegin(
    {Revision, ResumeSequence, StableHash}, NowSeconds())
    == ECrowdReplicationAcceptResult::Rejected)
    HandleClientFailure(TEXT("baseline_begin"));
}

void AMassCrowdReplicationActor::ClientSnapshotHeader_Implementation(
  const FCrowdRelevantSnapshotHeader Header)
{
  const auto Result = ClientState.AcceptSnapshotHeader(Header, NowSeconds());
  if (Result == ECrowdReplicationAcceptResult::Rejected
    || Result == ECrowdReplicationAcceptResult::ResyncRequired)
    HandleClientFailure(TEXT("snapshot_header"));
}

void AMassCrowdReplicationActor::ClientSnapshotChunk_Implementation(
  const FCrowdRelevantSnapshotChunk Chunk)
{
  const auto Result = ClientState.AcceptSnapshotChunk(Chunk, NowSeconds());
  if (Result == ECrowdReplicationAcceptResult::Rejected
    || Result == ECrowdReplicationAcceptResult::ResyncRequired)
    HandleClientFailure(TEXT("snapshot_chunk"));
}

void AMassCrowdReplicationActor::ClientBaselineEnd_Implementation(
  const uint32 Revision,
  const uint64 ResumeSequence,
  const uint64 SnapshotHash)
{
  if (ClientState.AcceptBaselineEnd(
    {Revision, ResumeSequence, SnapshotHash}, NowSeconds())
    != ECrowdReplicationAcceptResult::BaselineComplete
    || !ClientState.ConsumeCompletedBaseline(
      CompletedBaselineEntities,
      CompletedBaselineRevision,
      CompletedResumeSequence))
  {
    HandleClientFailure(TEXT("baseline_end"));
    return;
  }
  bClientReady = true;
  UE_LOG(LogTemp, Display,
    TEXT("MassCrowdReplicationChannel role=client stage=baseline_complete revision=%u resume=%llu entities=%d"),
    CompletedBaselineRevision, CompletedResumeSequence,
    CompletedBaselineEntities.Num());
  ServerAckBaseline(CompletedBaselineRevision, CompletedResumeSequence);
}

void AMassCrowdReplicationActor::ClientReliableState_Implementation(
  const uint64 Sequence,
  const uint8 Kind,
  const uint32 ProviderId,
  const uint64 StableEntityId,
  const uint32 LifecycleSerial,
  const uint32 Revision,
  const TArray<uint8>& Payload,
  const uint64 StableHash)
{
  FCrowdReliableStateRecord Record;
  Record.Sequence = Sequence;
  Record.Kind = static_cast<ECrowdReliableStateKind>(Kind);
  Record.EntityRef = {ProviderId, StableEntityId, LifecycleSerial};
  Record.Revision = Revision;
  Record.Payload = Payload;
  Record.StableHash = StableHash;
  FCrowdReliableStateBatch Batch;
  Batch.FirstSequence = Sequence;
  Batch.Records.Add(Record);
  Batch.StableHash =
    FCrowdReplicationTransport::CalculateReliableBatchHash(Batch);
  const auto Result = ClientState.AcceptReliableBatch(Batch);
  if (Result == ECrowdReplicationAcceptResult::Rejected
    || Result == ECrowdReplicationAcceptResult::ResyncRequired)
    HandleClientFailure(TEXT("reliable_single"));
}

void AMassCrowdReplicationActor::ClientReliableStateBatch_Implementation(
  const TArray<uint64>& Sequences,
  const TArray<uint8>& Kinds,
  const TArray<uint32>& ProviderIds,
  const TArray<uint64>& StableEntityIds,
  const TArray<uint32>& LifecycleSerials,
  const TArray<uint32>& Revisions,
  const TArray<int32>& PayloadOffsets,
  const TArray<uint8>& PayloadBytes,
  const TArray<uint64>& StableHashes)
{
  const int32 Count = Sequences.Num();
  if (Count <= 0 || Count > Limits.MaxReliableRecordsPerBatch
    || Kinds.Num() != Count
    || ProviderIds.Num() != Count
    || StableEntityIds.Num() != Count
    || LifecycleSerials.Num() != Count
    || Revisions.Num() != Count
    || PayloadOffsets.Num() != Count + 1
    || StableHashes.Num() != Count
    || PayloadOffsets[0] != 0
    || PayloadOffsets.Last() != PayloadBytes.Num())
  {
    HandleClientFailure(TEXT("reliable_batch_shape"));
    return;
  }
  FCrowdReliableStateBatch Batch;
  Batch.FirstSequence = Sequences[0];
  Batch.Records.Reserve(Count);
  for (int32 Index = 0; Index < Count; ++Index)
  {
    const int32 Begin = PayloadOffsets[Index];
    const int32 End = PayloadOffsets[Index + 1];
    if (Begin < 0 || End < Begin || End > PayloadBytes.Num()
      || End - Begin > Limits.MaxReliablePayloadBytesPerRecord)
    {
      HandleClientFailure(TEXT("reliable_batch_payload"));
      return;
    }
    FCrowdReliableStateRecord& Record =
      Batch.Records.AddDefaulted_GetRef();
    Record.Sequence = Sequences[Index];
    Record.Kind = static_cast<ECrowdReliableStateKind>(Kinds[Index]);
    Record.EntityRef = {
      ProviderIds[Index],
      StableEntityIds[Index],
      LifecycleSerials[Index]};
    Record.Revision = Revisions[Index];
    Record.Payload.Append(PayloadBytes.GetData() + Begin, End - Begin);
    Record.StableHash = StableHashes[Index];
  }
  Batch.StableHash =
    FCrowdReplicationTransport::CalculateReliableBatchHash(Batch);
  const auto Result = ClientState.AcceptReliableBatch(Batch);
  if (Result == ECrowdReplicationAcceptResult::Rejected
    || Result == ECrowdReplicationAcceptResult::ResyncRequired)
    HandleClientFailure(TEXT("reliable_batch_accept"));
}

void AMassCrowdReplicationActor::ClientMovementCorrection_Implementation(
  const uint32 ProviderId,
  const uint64 StableEntityId,
  const uint32 LifecycleSerial,
  const uint64 Sequence,
  const int64 FixedStepIndex,
  const FVector Position,
  const FVector Velocity,
  const float YawDegrees,
  const uint64 StableHash)
{
  FCrowdMovementCorrectionRecord Correction;
  Correction.EntityRef = {ProviderId, StableEntityId, LifecycleSerial};
  Correction.Sequence = Sequence;
  Correction.FixedStepIndex = FixedStepIndex;
  Correction.Position = Position;
  Correction.Velocity = Velocity;
  Correction.YawDegrees = YawDegrees;
  Correction.StableHash = StableHash;
  const auto Result = ClientState.AcceptMovementCorrection(Correction);
  if (Result == ECrowdReplicationAcceptResult::ResyncRequired)
    HandleClientFailure(TEXT("correction_single"));
}

void AMassCrowdReplicationActor::ClientMovementCorrectionBatch_Implementation(
  const TArray<uint32>& ProviderIds,
  const TArray<uint64>& StableEntityIds,
  const TArray<uint32>& LifecycleSerials,
  const TArray<uint64>& Sequences,
  const TArray<int64>& FixedStepIndices,
  const TArray<FVector>& Positions,
  const TArray<FVector>& Velocities,
  const TArray<float>& YawDegrees,
  const TArray<uint64>& StableHashes)
{
  const int32 Count = ProviderIds.Num();
  if (Count <= 0 || Count > Limits.MaxCorrectionRecords
    || StableEntityIds.Num() != Count
    || LifecycleSerials.Num() != Count
    || Sequences.Num() != Count
    || FixedStepIndices.Num() != Count
    || Positions.Num() != Count
    || Velocities.Num() != Count
    || YawDegrees.Num() != Count
    || StableHashes.Num() != Count)
  {
    HandleClientFailure(TEXT("correction_batch_shape"));
    return;
  }
  TArray<FCrowdMovementCorrectionRecord> Corrections;
  Corrections.Reserve(Count);
  for (int32 Index = 0; Index < Count; ++Index)
  {
    FCrowdMovementCorrectionRecord& Correction =
      Corrections.AddDefaulted_GetRef();
    Correction.EntityRef = {
      ProviderIds[Index],
      StableEntityIds[Index],
      LifecycleSerials[Index]};
    Correction.Sequence = Sequences[Index];
    Correction.FixedStepIndex = FixedStepIndices[Index];
    Correction.Position = Positions[Index];
    Correction.Velocity = Velocities[Index];
    Correction.YawDegrees = YawDegrees[Index];
    Correction.StableHash = StableHashes[Index];
  }
  const auto Result =
    ClientState.AcceptMovementCorrections(Corrections);
  if (Result == ECrowdReplicationAcceptResult::ResyncRequired
    || Result == ECrowdReplicationAcceptResult::Rejected)
  {
    HandleClientFailure(TEXT("correction_batch_accept"));
    return;
  }
  UE_LOG(LogTemp, Verbose,
    TEXT("MassCrowdReplicationChannel role=client stage=correction_batch records=%d latest=%d"),
    Count, ClientState.GetLatestCorrectionCount());
}

void AMassCrowdReplicationActor::ServerAckBaseline_Implementation(
  const uint32 Revision,
  const uint64 ResumeSequence)
{
  if (!ServerState.AckBaseline(Revision, ResumeSequence)) return;
  TArray<FCrowdReliableStateRecord> Buffered;
  if (!ServerState.ConsumeBufferedReliable(Buffered)) return;
  for (int32 Begin = 0; Begin < Buffered.Num();
    Begin += Limits.MaxReliableRecordsPerBatch)
  {
    SendReliableBatch(MakeArrayView(
      Buffered.GetData() + Begin,
      FMath::Min(
        Limits.MaxReliableRecordsPerBatch,
        Buffered.Num() - Begin)));
  }
}

void AMassCrowdReplicationActor::ServerRequestResync_Implementation()
{
  if (!HasAuthority()) return;
  UE_LOG(LogTemp, Warning,
    TEXT("MassCrowdReplicationChannel role=server stage=resync_recreate owner=%s"),
    *GetNameSafe(GetOwner()));
  Destroy();
}

double AMassCrowdReplicationActor::NowSeconds() const
{
  const UWorld* World = GetWorld();
  return World ? World->GetTimeSeconds() : 0.0;
}

void AMassCrowdReplicationActor::HandleClientFailure(
  const TCHAR* Stage)
{
  bClientReady = false;
  UE_LOG(LogTemp, Error,
    TEXT("MassCrowdReplicationChannel role=client stage=resync_required source=%s"),
    Stage);
  ServerRequestResync();
}

void AMassCrowdReplicationActor::SendReliable(
  const FCrowdReliableStateRecord& Record)
{
  ClientReliableState(
    Record.Sequence,
    static_cast<uint8>(Record.Kind),
    Record.EntityRef.ProviderId,
    Record.EntityRef.StableEntityId,
    Record.EntityRef.LifecycleSerial,
    Record.Revision,
    Record.Payload,
    Record.StableHash);
}

void AMassCrowdReplicationActor::SendReliableBatch(
  const TConstArrayView<FCrowdReliableStateRecord> Records)
{
  TArray<uint64> Sequences;
  TArray<uint8> Kinds;
  TArray<uint32> ProviderIds;
  TArray<uint64> StableEntityIds;
  TArray<uint32> LifecycleSerials;
  TArray<uint32> Revisions;
  TArray<int32> PayloadOffsets;
  TArray<uint8> PayloadBytes;
  TArray<uint64> StableHashes;
  Sequences.Reserve(Records.Num());
  Kinds.Reserve(Records.Num());
  ProviderIds.Reserve(Records.Num());
  StableEntityIds.Reserve(Records.Num());
  LifecycleSerials.Reserve(Records.Num());
  Revisions.Reserve(Records.Num());
  PayloadOffsets.Reserve(Records.Num() + 1);
  StableHashes.Reserve(Records.Num());
  PayloadOffsets.Add(0);
  for (const FCrowdReliableStateRecord& Record : Records)
  {
    Sequences.Add(Record.Sequence);
    Kinds.Add(static_cast<uint8>(Record.Kind));
    ProviderIds.Add(Record.EntityRef.ProviderId);
    StableEntityIds.Add(Record.EntityRef.StableEntityId);
    LifecycleSerials.Add(Record.EntityRef.LifecycleSerial);
    Revisions.Add(Record.Revision);
    PayloadBytes.Append(Record.Payload);
    PayloadOffsets.Add(PayloadBytes.Num());
    StableHashes.Add(Record.StableHash);
  }
  ClientReliableStateBatch(
    Sequences, Kinds, ProviderIds, StableEntityIds,
    LifecycleSerials, Revisions, PayloadOffsets,
    PayloadBytes, StableHashes);
}
