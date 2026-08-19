#include "MassCrowdReplicationActor.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerReplicationCodec.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassCrowdReplicationActor)

namespace
{
  TAutoConsoleVariable<int32> CVarCrowdWorkerAuthorityCorrectionEnabled(
    TEXT("crowd.Worker.AuthorityCorrectionEnabled"),
    1,
    TEXT("Enable sparse Worker authority correction requests. Intent, digest, simulation and presentation remain active when disabled."),
    ECVF_Default);

  bool IsWorkerAuthorityCorrectionEnabled()
  {
    return CVarCrowdWorkerAuthorityCorrectionEnabled.GetValueOnGameThread()
        != 0
      && !FParse::Param(
        FCommandLine::Get(),
        TEXT("CrowdWorkerAuthorityCorrectionDisabled"));
  }

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
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = true;
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
  WorkerPacketConfig.MaxChunkBytes =
    FCrowdWorkerPacketTransportConfig::ReliableRpcSafeChunkBytes;
  WorkerPacketConfig.MaxPacketBytes = FMath::Max(
    WorkerNetworkConfig.MaxEncodedCheckpointBytes,
    FMath::Max(
      WorkerNetworkConfig.MaxEncodedCorrectionBytes,
      WorkerNetworkConfig.MaxEncodedIntentBytes));
  WorkerPacketConfig.MaxChunkCount = FMath::DivideAndRoundUp(
    WorkerPacketConfig.MaxPacketBytes,
    WorkerPacketConfig.MaxChunkBytes);
  WorkerPacketConfig.AssemblyTimeoutSeconds = 10.0;
  WorkerPacketAssembler.Initialize(WorkerPacketConfig);
  WorkerClientGate.Initialize(WorkerNetworkConfig);
}

void AMassCrowdReplicationActor::BeginPlay()
{
  Super::BeginPlay();
  UE_LOG(LogTemp, Display,
    TEXT("MassCrowdReplicationChannel role=%s stage=begin owner=%s worker_authority_correction=%d"),
    HasAuthority() ? TEXT("server") : TEXT("client"),
    *GetNameSafe(GetOwner()),
    IsWorkerAuthorityCorrectionEnabled() ? 1 : 0);
}

void AMassCrowdReplicationActor::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() == NM_Standalone) return;
  UpdateWorkerTrafficRates(NowSeconds());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();

  if (HasAuthority())
  {
    PumpOutgoingWorkerPackets();
    if (!OutgoingWorkerPackets.IsEmpty()) return;
    if (!PendingOutgoingAuthorityCorrections.IsEmpty())
    {
      const FCrowdWorkerAuthorityCorrectionBatch& Correction =
        PendingOutgoingAuthorityCorrections[0];
      TArray<uint8> Bytes;
      if (!FCrowdWorkerReplicationCodec::EncodeCorrection(
          Correction, WorkerNetworkConfig, Bytes)
        || !SendWorkerPacket(
          ECrowdWorkerPacketKind::Correction,
          Correction.Generation,
          Correction.CorrectionSequence,
          Correction.StableHash,
          Bytes))
        return;
      WorkerTrafficMetrics.CorrectionBytes += Bytes.Num();
      WorkerTrafficWindowCorrectionBytes += Bytes.Num();
      WorkerTrafficMetrics.LastCorrectionEntityCount =
        Correction.AuthoritativeMembers.Num();
      WorkerTrafficMetrics.LastCorrectionScopeCount =
        Correction.Scopes.Num();
      PendingOutgoingAuthorityCorrections.RemoveAt(
        0, 1, EAllowShrinking::No);
      return;
    }
    const uint64 Generation = Runtime.GetGeneration();
    if (Generation == 0) return;
    if (LastWorkerSentGeneration != Generation)
    {
      FCrowdWorkerNetworkCheckpoint Checkpoint;
      if (FCrowdWorkerReplicationServerAdapter::CaptureCheckpoint(
          Runtime, Generation, Checkpoint)
        != ECrowdWorkerNetworkReadResult::Ready)
        return;
      if (!PublishWorkerCheckpoint(Checkpoint)) return;
      LastWorkerSentGeneration = Generation;
      LastWorkerSentInputSequence =
        Checkpoint.Header.LastAppliedInputSequence;
      LastWorkerSentDigestSequence = 0;
      NextWorkerCorrectionSequence = 1;
      PendingOutgoingAuthorityCorrections.Reset();
      return;
    }

    PumpAuthorityDigest(Runtime, Generation);

    TArray<FCrowdWorkerIntentBatch> Intents;
    const ECrowdWorkerNetworkReadResult ReadResult =
      Runtime.ReadNetworkIntents(
        Generation,
        LastWorkerSentInputSequence,
        Intents);
    if (ReadResult == ECrowdWorkerNetworkReadResult::RequiresCheckpoint)
    {
      LastWorkerSentGeneration = 0;
      return;
    }
    if (ReadResult != ECrowdWorkerNetworkReadResult::Ready
      || Intents.IsEmpty())
      return;
    if (PublishWorkerIntents(MakeArrayView(&Intents[0], 1)))
      LastWorkerSentInputSequence = Intents[0].LastInputSequence;
    return;
  }

  PumpWorkerClientRuntime(Runtime);
}

void AMassCrowdReplicationActor::PumpWorkerClientRuntime(
  FCrowdAsyncSimulationRuntime& Runtime)
{
  if (HasAuthority())
    return;
  if (!bWorkerClientReady
    || Runtime.GetState()
      != ECrowdAsyncSimulationRuntimeState::Running)
    return;
  if (!PendingAuthorityCorrections.IsEmpty())
  {
    for (const FCrowdWorkerAuthorityCorrectionBatch& Correction :
      PendingAuthorityCorrections)
    {
      const ECrowdAsyncSimulationCorrectionResult Result =
        Runtime.SubmitAuthorityCorrection(Correction);
      if (Result != ECrowdAsyncSimulationCorrectionResult::Accepted
        && Result != ECrowdAsyncSimulationCorrectionResult::Duplicate)
      {
        HandleClientFailure(TEXT("worker_authority_correction_submit"));
        return;
      }
    }
    PendingAuthorityCorrections.Reset();
    bWorkerCorrectionPending = false;
  }
  ProcessPendingAuthorityDigest(Runtime);
  if (bWorkerCorrectionPending)
    return;
  const FCrowdWorkerAuthorityDigestBatch* PendingDigest =
    AuthorityDigestInbox.Peek();
  const uint64 MaximumInputSequence = PendingDigest != nullptr
    ? PendingDigest->ThroughInputSequence
    : MAX_uint64;
  constexpr int32 MaxIntentSubmitsPerPump = 4;
  int32 SubmittedIntentCount = 0;
  while (!PendingWorkerIntents.IsEmpty()
    && PendingWorkerIntents[0].LastInputSequence
      <= MaximumInputSequence
    && SubmittedIntentCount < MaxIntentSubmitsPerPump)
  {
    const FCrowdWorkerIntentBatch& Intent =
      PendingWorkerIntents[0];
    const ECrowdAsyncSimulationSubmitResult Result =
      Runtime.SubmitIntentBatch(Intent);
    if (Result == ECrowdAsyncSimulationSubmitResult::RejectedCapacity)
      return;
    if (Result != ECrowdAsyncSimulationSubmitResult::Accepted)
    {
      const FCrowdAsyncSimulationRuntimeMetrics Metrics =
        Runtime.GetMetrics();
      UE_LOG(LogTemp, Error,
        TEXT("MassCrowdWorkerNetwork role=client stage=runtime_intent_rejected result=%u generation=%llu first_sequence=%llu last_sequence=%llu runtime_generation=%llu runtime_state=%u requires_resnapshot=%d"),
        static_cast<uint32>(Result),
        Intent.Generation,
        Intent.FirstInputSequence,
        Intent.LastInputSequence,
        Runtime.GetGeneration(),
        static_cast<uint32>(Runtime.GetState()),
        Metrics.bRequiresResnapshot ? 1 : 0);
      HandleClientFailure(TEXT("worker_runtime_intent_submit"));
      return;
    }
    PendingWorkerIntents.RemoveAt(
      0, 1, EAllowShrinking::No);
    ++SubmittedIntentCount;
  }
  ProcessPendingAuthorityDigest(Runtime);
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

bool AMassCrowdReplicationActor::PublishWorkerCheckpoint(
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
{
  TArray<uint8> Bytes;
  const bool bPublished = HasAuthority()
    && FCrowdWorkerReplicationCodec::EncodeCheckpoint(
      Checkpoint, WorkerNetworkConfig, Bytes)
    && SendWorkerPacket(
      ECrowdWorkerPacketKind::Checkpoint,
      Checkpoint.Header.Generation,
      Checkpoint.Header.WorkerEpoch,
      Checkpoint.StableHash,
      Bytes);
  if (bPublished)
  {
    WorkerTrafficMetrics.CheckpointBytes += Bytes.Num();
    WorkerTrafficMetrics.LastCheckpointBytes = Bytes.Num();
    ++WorkerTrafficMetrics.CheckpointCount;
    UE_LOG(LogTemp, Display,
      TEXT("MassCrowdWorkerNetwork role=server stage=checkpoint_queued generation=%llu sequence=%llu bytes=%d hash=%llu"),
      Checkpoint.Header.Generation,
      Checkpoint.Header.WorkerEpoch,
      Bytes.Num(),
      Checkpoint.StableHash);
  }
  return bPublished;
}

bool AMassCrowdReplicationActor::PublishWorkerIntents(
  const TConstArrayView<FCrowdWorkerIntentBatch> Batches)
{
  if (!HasAuthority() || Batches.Num() != 1) return false;
  const FCrowdWorkerIntentBatch& Batch = Batches[0];
  TArray<uint8> Bytes;
  if (!FCrowdWorkerReplicationCodec::EncodeIntent(
      Batch, WorkerNetworkConfig, Bytes)
    || !SendWorkerPacket(
      ECrowdWorkerPacketKind::Intent,
      Batch.Generation,
      Batch.LastInputSequence,
      Batch.StableHash,
      Bytes))
    return false;
  WorkerTrafficMetrics.IntentBytes += Bytes.Num();
  WorkerTrafficWindowIntentBytes += Bytes.Num();
  UE_LOG(LogTemp, VeryVerbose,
    TEXT("MassCrowdWorkerNetwork role=server stage=intent_queued generation=%llu first_sequence=%llu last_sequence=%llu tick=%llu records=%d bytes=%d hash=%llu"),
    Batch.Generation,
    Batch.FirstInputSequence,
    Batch.LastInputSequence,
    Batch.Clock.SimulationTick,
    Batch.GetRecordCount(),
    Bytes.Num(),
    Batch.StableHash);
  return true;
}

bool AMassCrowdReplicationActor::ConsumeWorkerCheckpoint(
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint)
{
  OutCheckpoint = {};
  if (!bWorkerCheckpointReady) return false;
  OutCheckpoint = MoveTemp(ReadyWorkerCheckpoint);
  bWorkerCheckpointReady = false;
  return true;
}

bool AMassCrowdReplicationActor::DrainWorkerIntents(
  TArray<FCrowdWorkerIntentBatch>& OutBatches)
{
  OutBatches = MoveTemp(PendingWorkerIntents);
  PendingWorkerIntents.Reset();
  return !OutBatches.IsEmpty();
}

void AMassCrowdReplicationActor::ClientBaselineBegin_Implementation(
  const uint32 Revision,
  const uint64 ResumeSequence,
  const uint64 StableHash)
{
  bClientReady = false;
  bWorkerClientReady = false;
  bWorkerCheckpointReady = false;
  LastWorkerReceivedInputSequence = 0;
  PendingWorkerIntents.Reset();
  AuthorityDigestInbox.Reset();
  PendingAuthorityCorrections.Reset();
  bWorkerCorrectionPending = false;
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

void AMassCrowdReplicationActor::PumpAuthorityDigest(
  FCrowdAsyncSimulationRuntime& Runtime,
  const uint64 Generation)
{
  FCrowdWorkerAuthorityDigestBatch Digest;
  if (Runtime.ReadAuthorityDigest(Generation, Digest)
      != ECrowdWorkerNetworkReadResult::Ready
    || Digest.DigestSequence <= LastWorkerSentDigestSequence)
    return;
  TArray<uint8> Fields;
  TArray<uint8> Kinds;
  TArray<int64> ScopeIds;
  TArray<uint32> EntityCounts;
  TArray<uint64> Hashes;
  Fields.Reserve(Digest.Entries.Num());
  Kinds.Reserve(Digest.Entries.Num());
  ScopeIds.Reserve(Digest.Entries.Num());
  EntityCounts.Reserve(Digest.Entries.Num());
  Hashes.Reserve(Digest.Entries.Num());
  for (const FCrowdWorkerAuthorityDigestEntry& Entry : Digest.Entries)
  {
    Fields.Add(static_cast<uint8>(Entry.Scope.Field));
    Kinds.Add(static_cast<uint8>(Entry.Scope.Kind));
    ScopeIds.Add(Entry.Scope.ScopeId);
    EntityCounts.Add(Entry.EntityCount);
    Hashes.Add(Entry.StableHash);
  }
  ClientWorkerDigest(
    Digest.Generation,
    Digest.DigestSequence,
    Digest.SimulationTick,
    Digest.ThroughInputSequence,
    Fields,
    Kinds,
    ScopeIds,
    EntityCounts,
    Hashes,
    Digest.StableHash);
  LastWorkerSentDigestSequence = Digest.DigestSequence;
}

void AMassCrowdReplicationActor::ClientWorkerDigest_Implementation(
  const uint64 Generation,
  const uint64 DigestSequence,
  const uint64 SimulationTick,
  const uint64 ThroughInputSequence,
  const TArray<uint8>& Fields,
  const TArray<uint8>& ScopeKinds,
  const TArray<int64>& ScopeIds,
  const TArray<uint32>& EntityCounts,
  const TArray<uint64>& ScopeStableHashes,
  const uint64 StableHash)
{
  const int32 Count = Fields.Num();
  if (!bWorkerClientReady
    || Generation != WorkerClientGate.GetGeneration()
    || Count > WorkerNetworkConfig.MaxDigestScopes
    || ScopeKinds.Num() != Count
    || ScopeIds.Num() != Count
    || EntityCounts.Num() != Count
    || ScopeStableHashes.Num() != Count)
  {
    HandleClientFailure(TEXT("worker_digest_shape"));
    return;
  }
  FCrowdWorkerAuthorityDigestBatch Digest;
  Digest.Generation = Generation;
  Digest.DigestSequence = DigestSequence;
  Digest.SimulationTick = SimulationTick;
  Digest.ThroughInputSequence = ThroughInputSequence;
  Digest.StableHash = StableHash;
  Digest.Entries.Reserve(Count);
  for (int32 Index = 0; Index < Count; ++Index)
  {
    FCrowdWorkerAuthorityDigestEntry& Entry =
      Digest.Entries.AddDefaulted_GetRef();
    Entry.Scope.Field = static_cast<ECrowdWorkerField>(Fields[Index]);
    Entry.Scope.Kind = static_cast<ECrowdWorkerAuthorityScopeKind>(
      ScopeKinds[Index]);
    Entry.Scope.ScopeId = ScopeIds[Index];
    Entry.SimulationTick = SimulationTick;
    Entry.ThroughInputSequence = ThroughInputSequence;
    Entry.EntityCount = EntityCounts[Index];
    Entry.StableHash = ScopeStableHashes[Index];
  }
  if (!Digest.IsValid(WorkerNetworkConfig))
  {
    HandleClientFailure(TEXT("worker_digest_contract"));
    return;
  }
  AuthorityDigestInbox.Offer(MoveTemp(Digest));
}

void AMassCrowdReplicationActor::ProcessPendingAuthorityDigest(
  FCrowdAsyncSimulationRuntime& Runtime)
{
  const FCrowdWorkerAuthorityDigestBatch* PendingDigest =
    AuthorityDigestInbox.Peek();
  if (PendingDigest == nullptr || bWorkerCorrectionPending)
    return;
  TArray<FCrowdWorkerAuthorityScopeKey> Mismatches;
  uint64 LocalAuthorityHash = 0;
  uint64 RemoteAuthorityHash = 0;
  const ECrowdWorkerNetworkReadResult Result =
    Runtime.CompareAuthorityDigest(
      *PendingDigest, Mismatches,
      &LocalAuthorityHash, &RemoteAuthorityHash);
  if (Result == ECrowdWorkerNetworkReadResult::NoData) return;
  if (Result != ECrowdWorkerNetworkReadResult::Ready)
  {
    HandleClientFailure(TEXT("worker_digest_compare"));
    return;
  }
  const uint64 Generation = PendingDigest->Generation;
  const uint64 DigestSequence = PendingDigest->DigestSequence;
  const uint64 DigestSimulationTick = PendingDigest->SimulationTick;
  const uint64 DigestInputSequence = PendingDigest->ThroughInputSequence;
  const FCrowdAsyncSimulationRuntimeMetrics WorkerMetrics =
    Runtime.GetMetrics();
  UE_LOG(LogTemp, Display,
    TEXT("CrowdWorkerCorrectionCheckpoint role=client generation=%llu digest_sequence=%llu digest_tick=%llu digest_through_input=%llu mismatched_scope_count=%d correction_sequence=0 correction_apply_tick=0 correction_through_input=0 worker_epoch_before=%llu worker_epoch_after=%llu correction_revision_before=%llu correction_revision_after=%llu invalidated_work_count=0 invalidated_wakeup_count=0 invalidated_dirty_count=0 discarded_stale_output_count=%llu result=%s worker_failure=%u local_authority_hash=%llu remote_authority_hash=%llu converged=%d"),
    Generation,
    DigestSequence,
    DigestSimulationTick,
    DigestInputSequence,
    Mismatches.Num(),
    WorkerMetrics.WorkerEpoch,
    WorkerMetrics.WorkerEpoch,
    WorkerMetrics.LastAppliedAuthorityCorrectionSequence,
    WorkerMetrics.LastAppliedAuthorityCorrectionSequence,
    WorkerMetrics.StaleAfterCorrectionDiscardCount,
    Mismatches.IsEmpty() ? TEXT("matched") : TEXT("mismatch"),
    static_cast<uint32>(WorkerMetrics.WorkerV2.LastFailure),
    LocalAuthorityHash,
    RemoteAuthorityHash,
    Mismatches.IsEmpty() ? 1 : 0);
  AuthorityDigestInbox.Consume();
  if (Mismatches.IsEmpty()) return;
  ++WorkerTrafficMetrics.DigestMismatchCount;
  UE_LOG(LogTemp, Display,
    TEXT("MassCrowdWorkerDigestMismatch digest=%llu tick=%llu input=%llu scopes=%d first_field=%u first_kind=%u first_scope=%lld"),
    DigestSequence,
    DigestSimulationTick,
    DigestInputSequence,
    Mismatches.Num(),
    static_cast<uint32>(Mismatches[0].Field),
    static_cast<uint32>(Mismatches[0].Kind),
    Mismatches[0].ScopeId);
  if (!IsWorkerAuthorityCorrectionEnabled())
    return;
  if (!Runtime.BeginAuthorityCorrectionBarrier(
      Generation, DigestSimulationTick, DigestInputSequence))
  {
    HandleClientFailure(TEXT("worker_correction_barrier"));
    return;
  }
  TArray<uint8> Fields;
  TArray<uint8> Kinds;
  TArray<int64> ScopeIds;
  for (const FCrowdWorkerAuthorityScopeKey& Scope : Mismatches)
  {
    Fields.Add(static_cast<uint8>(Scope.Field));
    Kinds.Add(static_cast<uint8>(Scope.Kind));
    ScopeIds.Add(Scope.ScopeId);
  }
  bWorkerCorrectionPending = true;
  ServerRequestWorkerCorrection(
    Generation, DigestSequence, Fields, Kinds, ScopeIds);
}

void AMassCrowdReplicationActor::ServerRequestWorkerCorrection_Implementation(
  const uint64 Generation,
  const uint64 DigestSequence,
  const TArray<uint8>& Fields,
  const TArray<uint8>& ScopeKinds,
  const TArray<int64>& ScopeIds)
{
  if (!HasAuthority() || Fields.IsEmpty()
    || Fields.Num() > WorkerNetworkConfig.MaxCorrectionScopes
    || ScopeKinds.Num() != Fields.Num()
    || ScopeIds.Num() != Fields.Num())
  {
    Destroy();
    return;
  }
  UWorld* World = GetWorld();
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem = World
    ? World->GetSubsystem<UMassCrowdRuntimeSubsystem>()
    : nullptr;
  if (!RuntimeSubsystem) return;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  TArray<FCrowdWorkerAuthorityScopeKey> Scopes;
  Scopes.Reserve(Fields.Num());
  for (int32 Index = 0; Index < Fields.Num(); ++Index)
  {
    FCrowdWorkerAuthorityScopeKey Scope;
    Scope.Field = static_cast<ECrowdWorkerField>(Fields[Index]);
    Scope.Kind = static_cast<ECrowdWorkerAuthorityScopeKind>(
      ScopeKinds[Index]);
    Scope.ScopeId = ScopeIds[Index];
    if (!Scope.IsValid())
    {
      Destroy();
      return;
    }
    Scopes.Add(Scope);
  }
  Scopes.Sort();
  for (int32 Index = 1; Index < Scopes.Num(); ++Index)
  {
    if (!(Scopes[Index - 1] < Scopes[Index]))
    {
      Destroy();
      return;
    }
  }
  FCrowdWorkerAuthorityCorrectionBatch Correction;
  if (Runtime.BuildAuthorityCorrection(
      Generation,
      DigestSequence,
      NextWorkerCorrectionSequence,
      Scopes,
      Correction) != ECrowdWorkerNetworkReadResult::Ready)
  {
    Destroy();
    return;
  }
  ++NextWorkerCorrectionSequence;
  PendingOutgoingAuthorityCorrections.Add(MoveTemp(Correction));
}

void AMassCrowdReplicationActor::ClientWorkerPacketBegin_Implementation(
  const uint8 Kind,
  const uint64 Generation,
  const uint64 Sequence,
  const uint64 ObjectStableHash,
  const int32 TotalBytes,
  const int32 ChunkCount,
  const uint64 HeaderStableHash)
{
  FCrowdWorkerPacketHeader Header;
  Header.Kind = static_cast<ECrowdWorkerPacketKind>(Kind);
  Header.Generation = Generation;
  Header.Sequence = Sequence;
  Header.ObjectStableHash = ObjectStableHash;
  Header.TotalBytes = TotalBytes;
  Header.ChunkCount = ChunkCount;
  Header.StableHash = HeaderStableHash;
  const ECrowdWorkerPacketAcceptResult Result =
    WorkerPacketAssembler.AcceptHeader(Header, NowSeconds());
  if (Header.Kind == ECrowdWorkerPacketKind::Checkpoint
    && Result == ECrowdWorkerPacketAcceptResult::Accepted)
    bWorkerClientReady = false;
  if (Result != ECrowdWorkerPacketAcceptResult::Accepted
    && Result != ECrowdWorkerPacketAcceptResult::Duplicate)
    HandleClientFailure(TEXT("worker_packet_begin"));
}

void AMassCrowdReplicationActor::ClientWorkerPacketChunk_Implementation(
  const uint64 Sequence,
  const int32 ChunkIndex,
  const TArray<uint8>& Bytes,
  const uint64 ChunkStableHash)
{
  FCrowdWorkerPacketChunk Chunk;
  Chunk.Sequence = Sequence;
  Chunk.ChunkIndex = ChunkIndex;
  Chunk.Bytes = Bytes;
  Chunk.StableHash = ChunkStableHash;
  const ECrowdWorkerPacketAcceptResult Result =
    WorkerPacketAssembler.AcceptChunk(Chunk, NowSeconds());
  if (Result != ECrowdWorkerPacketAcceptResult::Accepted
    && Result != ECrowdWorkerPacketAcceptResult::Duplicate)
    HandleClientFailure(TEXT("worker_packet_chunk"));
}

void AMassCrowdReplicationActor::ClientWorkerPacketEnd_Implementation(
  const uint64 Sequence,
  const uint64 ObjectStableHash,
  const uint64 EndStableHash)
{
  FCrowdWorkerPacketEnd End;
  End.Sequence = Sequence;
  End.ObjectStableHash = ObjectStableHash;
  End.StableHash = EndStableHash;
  if (WorkerPacketAssembler.AcceptEnd(End, NowSeconds())
    != ECrowdWorkerPacketAcceptResult::Complete)
  {
    HandleClientFailure(TEXT("worker_packet_end"));
    return;
  }
  HandleCompletedWorkerPacket();
}

double AMassCrowdReplicationActor::NowSeconds() const
{
  const UWorld* World = GetWorld();
  return World ? World->GetTimeSeconds() : 0.0;
}

void AMassCrowdReplicationActor::UpdateWorkerTrafficRates(
  const double Now)
{
  if (WorkerTrafficWindowStartSeconds <= 0.0)
  {
    WorkerTrafficWindowStartSeconds = Now;
    return;
  }
  const double Elapsed = Now - WorkerTrafficWindowStartSeconds;
  if (Elapsed < 1.0) return;
  WorkerTrafficMetrics.IntentBytesPerSecond =
    static_cast<double>(WorkerTrafficWindowIntentBytes) / Elapsed;
  WorkerTrafficMetrics.CorrectionBytesPerSecond =
    static_cast<double>(WorkerTrafficWindowCorrectionBytes) / Elapsed;
  WorkerTrafficWindowIntentBytes = 0;
  WorkerTrafficWindowCorrectionBytes = 0;
  WorkerTrafficWindowStartSeconds = Now;
}

void AMassCrowdReplicationActor::HandleClientFailure(
  const TCHAR* Stage)
{
  ++WorkerTrafficMetrics.ResyncCount;
  bClientReady = false;
  bWorkerClientReady = false;
  bWorkerCheckpointReady = false;
  LastWorkerReceivedInputSequence = 0;
  PendingWorkerIntents.Reset();
  AuthorityDigestInbox.Reset();
  PendingAuthorityCorrections.Reset();
  bWorkerCorrectionPending = false;
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

bool AMassCrowdReplicationActor::SendWorkerPacket(
  const ECrowdWorkerPacketKind Kind,
  const uint64 Generation,
  const uint64 Sequence,
  const uint64 ObjectStableHash,
  const TConstArrayView<uint8> Bytes)
{
  if (!OutgoingWorkerPackets.IsEmpty()
    || OutgoingWorkerPacketBytes != 0
    || Bytes.Num() > WorkerPacketConfig.MaxPacketBytes)
    return false;
  FOutgoingWorkerPacket Packet;
  if (!FCrowdWorkerPacketTransport::Build(
    Kind, Generation, Sequence, ObjectStableHash, Bytes,
    WorkerPacketConfig,
    Packet.Header,
    Packet.Chunks,
    Packet.End))
    return false;
  OutgoingWorkerPacketBytes = Packet.Header.TotalBytes;
  OutgoingWorkerPackets.Add(MoveTemp(Packet));
  return true;
}

void AMassCrowdReplicationActor::PumpOutgoingWorkerPackets()
{
  if (OutgoingWorkerPackets.IsEmpty()) return;
  FOutgoingWorkerPacket& Packet = OutgoingWorkerPackets[0];
  if (!Packet.bBeginSent)
  {
    ClientWorkerPacketBegin(
      static_cast<uint8>(Packet.Header.Kind),
      Packet.Header.Generation,
      Packet.Header.Sequence,
      Packet.Header.ObjectStableHash,
      Packet.Header.TotalBytes,
      Packet.Header.ChunkCount,
      Packet.Header.StableHash);
    Packet.bBeginSent = true;
  }
  constexpr int32 MaxChunksPerTick = 8;
  const int32 EndChunk = FMath::Min(
    Packet.NextChunkIndex + MaxChunksPerTick,
    Packet.Chunks.Num());
  while (Packet.NextChunkIndex < EndChunk)
  {
    const FCrowdWorkerPacketChunk& Chunk =
      Packet.Chunks[Packet.NextChunkIndex++];
    ClientWorkerPacketChunk(
      Chunk.Sequence,
      Chunk.ChunkIndex,
      Chunk.Bytes,
      Chunk.StableHash);
  }
  if (Packet.NextChunkIndex != Packet.Chunks.Num()) return;
  ClientWorkerPacketEnd(
    Packet.End.Sequence,
    Packet.End.ObjectStableHash,
    Packet.End.StableHash);
  OutgoingWorkerPacketBytes -= Packet.Header.TotalBytes;
  OutgoingWorkerPackets.RemoveAt(0, 1, EAllowShrinking::No);
}

void AMassCrowdReplicationActor::HandleCompletedWorkerPacket()
{
  FCrowdWorkerAssembledPacket Packet;
  if (!WorkerPacketAssembler.ConsumeCompleted(Packet))
  {
    HandleClientFailure(TEXT("worker_packet_consume"));
    return;
  }
  if (Packet.Header.Kind == ECrowdWorkerPacketKind::Checkpoint)
  {
    FCrowdWorkerNetworkCheckpoint Checkpoint;
    if (!FCrowdWorkerReplicationCodec::DecodeCheckpoint(
        Packet.Bytes, WorkerNetworkConfig, Checkpoint)
      || Checkpoint.Header.Generation != Packet.Header.Generation
      || Checkpoint.Header.WorkerEpoch != Packet.Header.Sequence
      || Checkpoint.StableHash != Packet.Header.ObjectStableHash
      || WorkerClientGate.AcceptCheckpoint(Checkpoint)
        != ECrowdWorkerReplicationAcceptResult::Accepted
      || WorkerClientGate.AcceptResourceRevisions(
        Checkpoint.StableHash, Checkpoint.ResourceRecords)
        != ECrowdWorkerReplicationAcceptResult::Accepted
      || WorkerClientGate.AcceptEventBaseline(
        Checkpoint.StableHash, Checkpoint.EventBaselineSequence)
        != ECrowdWorkerReplicationAcceptResult::BaselineReady
      || !WorkerClientGate.ConsumeReadyCheckpoint(
        ReadyWorkerCheckpoint))
    {
      HandleClientFailure(TEXT("worker_checkpoint_decode"));
      return;
    }
    bWorkerCheckpointReady = true;
    bWorkerClientReady = true;
    LastWorkerReceivedInputSequence =
      ReadyWorkerCheckpoint.Header.LastAppliedInputSequence;
    PendingWorkerIntents.Reset();
    AuthorityDigestInbox.Reset();
    PendingAuthorityCorrections.Reset();
    bWorkerCorrectionPending = false;
    UE_LOG(LogTemp, Display,
      TEXT("MassCrowdWorkerNetwork role=client stage=checkpoint_ready generation=%llu sequence=%llu states=%d resources=%d work_current=%d work_next=%d wakeups=%d hash=%llu"),
      ReadyWorkerCheckpoint.Header.Generation,
      ReadyWorkerCheckpoint.InputBaselineSequence,
      ReadyWorkerCheckpoint.StateRecords.Num(),
      ReadyWorkerCheckpoint.ResourceRecords.Num(),
      ReadyWorkerCheckpoint.Continuation.WorkRing.CurrentItems.Num(),
      ReadyWorkerCheckpoint.Continuation.WorkRing.NextItems.Num(),
      ReadyWorkerCheckpoint.Continuation.Wakeups.Num(),
      ReadyWorkerCheckpoint.StableHash);
    return;
  }
  if (Packet.Header.Kind == ECrowdWorkerPacketKind::Intent)
  {
    FCrowdWorkerIntentBatch Batch;
    const bool bDecoded = FCrowdWorkerReplicationCodec::DecodeIntent(
      Packet.Bytes, WorkerNetworkConfig, Batch);
    const bool bValid = bWorkerClientReady && bDecoded
      && Batch.Generation == Packet.Header.Generation
      && Batch.LastInputSequence == Packet.Header.Sequence
      && Batch.StableHash == Packet.Header.ObjectStableHash
      && Batch.FirstInputSequence
        == LastWorkerReceivedInputSequence + 1
      && PendingWorkerIntents.Num()
        < WorkerNetworkConfig.MaxRetainedIntentBatches;
    if (!bValid)
    {
      UE_LOG(LogTemp, Error,
        TEXT("MassCrowdWorkerNetwork role=client stage=intent_order_detail ready=%d decoded=%d packet_generation=%llu batch_generation=%llu packet_sequence=%llu batch_first=%llu batch_last=%llu expected_first=%llu pending=%d capacity=%d packet_hash=%llu batch_hash=%llu"),
        bWorkerClientReady ? 1 : 0,
        bDecoded ? 1 : 0,
        Packet.Header.Generation,
        Batch.Generation,
        Packet.Header.Sequence,
        Batch.FirstInputSequence,
        Batch.LastInputSequence,
        LastWorkerReceivedInputSequence + 1,
        PendingWorkerIntents.Num(),
        WorkerNetworkConfig.MaxRetainedIntentBatches,
        Packet.Header.ObjectStableHash,
        Batch.StableHash);
      HandleClientFailure(TEXT("worker_intent_decode_or_order"));
      return;
    }
    LastWorkerReceivedInputSequence = Batch.LastInputSequence;
    PendingWorkerIntents.Add(MoveTemp(Batch));
    return;
  }
  if (Packet.Header.Kind == ECrowdWorkerPacketKind::Correction)
  {
    FCrowdWorkerAuthorityCorrectionBatch Correction;
    if (!bWorkerClientReady
      || !bWorkerCorrectionPending
      || !FCrowdWorkerReplicationCodec::DecodeCorrection(
        Packet.Bytes, WorkerNetworkConfig, Correction)
      || Correction.Generation != Packet.Header.Generation
      || Correction.CorrectionSequence != Packet.Header.Sequence
      || Correction.StableHash != Packet.Header.ObjectStableHash
      || PendingAuthorityCorrections.Num()
        >= WorkerNetworkConfig.MaxCorrectionScopes)
    {
      HandleClientFailure(TEXT("worker_correction_decode"));
      return;
    }
    PendingAuthorityCorrections.Add(MoveTemp(Correction));
    return;
  }
  HandleClientFailure(TEXT("worker_packet_kind"));
}
