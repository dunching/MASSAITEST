#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassCrowdReplicationChannel.h"
#include "MassCrowdWorkerPacketTransport.h"
#include "MassCrowdWorkerReplicationAdapter.h"
#include "MassCrowdReplicationActor.generated.h"

class APlayerController;
class FCrowdAsyncSimulationRuntime;

struct MASSCROWDNETWORKING_API FCrowdWorkerNetworkTrafficMetrics
{
  uint64 IntentBytes = 0;
  uint64 CorrectionBytes = 0;
  uint64 CheckpointBytes = 0;
  uint64 DigestMismatchCount = 0;
  uint64 CheckpointCount = 0;
  uint64 ResyncCount = 0;
  double IntentBytesPerSecond = 0.0;
  double CorrectionBytesPerSecond = 0.0;
  int32 LastCheckpointBytes = 0;
  int32 LastCorrectionEntityCount = 0;
  int32 LastCorrectionScopeCount = 0;
};

UCLASS(NotPlaceable)
class MASSCROWDNETWORKING_API AMassCrowdReplicationActor final
  : public AActor
{
  GENERATED_BODY()

public:
  AMassCrowdReplicationActor();
  virtual void BeginPlay() override;
  virtual void Tick(float DeltaSeconds) override;

  static AMassCrowdReplicationActor* SpawnForController(
    APlayerController& Controller);

  bool PublishBaseline(
    const FCrowdRelevantSnapshotHeader& Header,
    TConstArrayView<FCrowdRelevantSnapshotChunk> Chunks,
    uint64 ResumeReliableSequence);
  bool PublishReliable(const FCrowdReliableStateRecord& Record);
  bool PublishReliables(
    TConstArrayView<FCrowdReliableStateRecord> Records);
  bool PublishMovementCorrection(
    const FCrowdMovementCorrectionRecord& Correction);
  bool PublishMovementCorrections(
    TConstArrayView<FCrowdMovementCorrectionRecord> Corrections);
  bool PublishWorkerCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);
  bool PublishWorkerIntents(
    TConstArrayView<FCrowdWorkerIntentBatch> Batches);
  bool ConsumeWorkerCheckpoint(
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint);
  bool DrainWorkerIntents(
    TArray<FCrowdWorkerIntentBatch>& OutBatches);
  // Called after the client Runtime polls so a newly-produced local digest is
  // compared in the same frame as its network counterpart.
  void PumpWorkerClientRuntime(FCrowdAsyncSimulationRuntime& Runtime);
  bool IsWorkerReady() const { return bWorkerClientReady; }
  const FCrowdWorkerNetworkTrafficMetrics&
    GetWorkerTrafficMetrics() const { return WorkerTrafficMetrics; }

  const FCrowdReplicationClientState& GetClientState() const
  {
    return ClientState;
  }
  bool DrainClientApplyFrames(
    TArray<FCrowdReplicationApplyFrame>& OutFrames)
  {
    return ClientState.DrainApplyFrames(OutFrames);
  }
  bool IsReady() const { return bClientReady; }
  bool IsServerAwaitingBaselineAck() const
  {
    return ServerState.IsAwaitingAck();
  }
  bool RequiresNewBaseline() const
  {
    return ServerState.RequiresNewBaseline();
  }
  void RequestResync() { ServerRequestResync(); }
  uint32 GetCompletedBaselineRevision() const
  {
    return CompletedBaselineRevision;
  }
  const TArray<FCrowdRelevantSnapshotEntityPayload>&
    GetCompletedBaselineEntities() const
  {
    return CompletedBaselineEntities;
  }

protected:
  UFUNCTION(Client, Reliable)
  void ClientBaselineBegin(
    uint32 Revision, uint64 ResumeSequence, uint64 StableHash);
  UFUNCTION(Client, Reliable)
  void ClientSnapshotHeader(FCrowdRelevantSnapshotHeader Header);
  UFUNCTION(Client, Reliable)
  void ClientSnapshotChunk(FCrowdRelevantSnapshotChunk Chunk);
  UFUNCTION(Client, Reliable)
  void ClientBaselineEnd(
    uint32 Revision, uint64 ResumeSequence, uint64 SnapshotHash);
  UFUNCTION(Client, Reliable)
  void ClientReliableState(
    uint64 Sequence, uint8 Kind, uint32 ProviderId,
    uint64 StableEntityId, uint32 LifecycleSerial,
    uint32 Revision, const TArray<uint8>& Payload, uint64 StableHash);
  UFUNCTION(Client, Reliable)
  void ClientReliableStateBatch(
    const TArray<uint64>& Sequences,
    const TArray<uint8>& Kinds,
    const TArray<uint32>& ProviderIds,
    const TArray<uint64>& StableEntityIds,
    const TArray<uint32>& LifecycleSerials,
    const TArray<uint32>& Revisions,
    const TArray<int32>& PayloadOffsets,
    const TArray<uint8>& PayloadBytes,
    const TArray<uint64>& StableHashes);
  UFUNCTION(Client, Unreliable)
  void ClientMovementCorrection(
    uint32 ProviderId, uint64 StableEntityId, uint32 LifecycleSerial,
    uint64 Sequence, int64 FixedStepIndex, FVector Position,
    FVector Velocity, float YawDegrees, uint64 StableHash);
  UFUNCTION(Client, Unreliable)
  void ClientMovementCorrectionBatch(
    const TArray<uint32>& ProviderIds,
    const TArray<uint64>& StableEntityIds,
    const TArray<uint32>& LifecycleSerials,
    const TArray<uint64>& Sequences,
    const TArray<int64>& FixedStepIndices,
    const TArray<FVector>& Positions,
    const TArray<FVector>& Velocities,
    const TArray<float>& YawDegrees,
    const TArray<uint64>& StableHashes);
  UFUNCTION(Server, Reliable)
  void ServerAckBaseline(uint32 Revision, uint64 ResumeSequence);
  UFUNCTION(Server, Reliable)
  void ServerRequestResync();
  // Digest is low frequency and self-replacing. Loss is tolerated because the
  // next cadence covers the same authority scopes; ordering is enforced by
  // DigestSequence without coupling digest delivery to reliable intent traffic.
  UFUNCTION(Client, Unreliable)
  void ClientWorkerDigest(
    uint64 Generation,
    uint64 DigestSequence,
    uint64 SimulationTick,
    uint64 ThroughInputSequence,
    const TArray<uint8>& Fields,
    const TArray<uint8>& ScopeKinds,
    const TArray<int64>& ScopeIds,
    const TArray<uint32>& EntityCounts,
    const TArray<uint64>& ScopeStableHashes,
    uint64 StableHash);
  UFUNCTION(Server, Reliable)
  void ServerRequestWorkerCorrection(
    uint64 Generation,
    uint64 DigestSequence,
    const TArray<uint8>& Fields,
    const TArray<uint8>& ScopeKinds,
    const TArray<int64>& ScopeIds);
  UFUNCTION(Client, Reliable)
  void ClientWorkerPacketBegin(
    uint8 Kind,
    uint64 Generation,
    uint64 Sequence,
    uint64 ObjectStableHash,
    int32 TotalBytes,
    int32 ChunkCount,
    uint64 HeaderStableHash);
  UFUNCTION(Client, Reliable)
  void ClientWorkerPacketChunk(
    uint64 Sequence,
    int32 ChunkIndex,
    const TArray<uint8>& Bytes,
    uint64 ChunkStableHash);
  UFUNCTION(Client, Reliable)
  void ClientWorkerPacketEnd(
    uint64 Sequence,
    uint64 ObjectStableHash,
    uint64 EndStableHash);

private:
  struct FOutgoingWorkerPacket
  {
    FCrowdWorkerPacketHeader Header;
    TArray<FCrowdWorkerPacketChunk> Chunks;
    FCrowdWorkerPacketEnd End;
    int32 NextChunkIndex = 0;
    bool bBeginSent = false;
  };

  FCrowdReplicationChannelLimits Limits;
  FCrowdReplicationServerState ServerState;
  FCrowdReplicationClientState ClientState;
  FCrowdWorkerNetworkStateConfig WorkerNetworkConfig;
  FCrowdWorkerPacketTransportConfig WorkerPacketConfig;
  FCrowdWorkerPacketAssembler WorkerPacketAssembler;
  FCrowdWorkerReplicationClientGate WorkerClientGate;
  FCrowdWorkerNetworkCheckpoint ReadyWorkerCheckpoint;
  TArray<FCrowdRelevantSnapshotEntityPayload> CompletedBaselineEntities;
  uint32 CompletedBaselineRevision = 0;
  uint64 CompletedResumeSequence = 0;
  bool bClientReady = false;
  bool bWorkerCheckpointReady = false;
  bool bWorkerClientReady = false;
  uint64 LastWorkerSentGeneration = 0;
  uint64 LastWorkerSentInputSequence = 0;
  uint64 LastWorkerReceivedInputSequence = 0;
  uint64 LastWorkerSentDigestSequence = 0;
  uint64 NextWorkerCorrectionSequence = 1;
  TArray<FOutgoingWorkerPacket> OutgoingWorkerPackets;
  TArray<FCrowdWorkerIntentBatch> PendingWorkerIntents;
  FCrowdWorkerAuthorityDigestInbox AuthorityDigestInbox;
  TArray<FCrowdWorkerAuthorityCorrectionBatch>
    PendingAuthorityCorrections;
  TArray<FCrowdWorkerAuthorityCorrectionBatch>
    PendingOutgoingAuthorityCorrections;
  bool bWorkerCorrectionPending = false;
  int64 OutgoingWorkerPacketBytes = 0;
  FCrowdWorkerNetworkTrafficMetrics WorkerTrafficMetrics;
  double WorkerTrafficWindowStartSeconds = 0.0;
  uint64 WorkerTrafficWindowIntentBytes = 0;
  uint64 WorkerTrafficWindowCorrectionBytes = 0;

  double NowSeconds() const;
  void HandleClientFailure(const TCHAR* Stage);
  void SendReliable(const FCrowdReliableStateRecord& Record);
  void SendReliableBatch(
    TConstArrayView<FCrowdReliableStateRecord> Records);
  bool SendWorkerPacket(
    ECrowdWorkerPacketKind Kind,
    uint64 Generation,
    uint64 Sequence,
    uint64 ObjectStableHash,
    TConstArrayView<uint8> Bytes);
  void PumpOutgoingWorkerPackets();
  void HandleCompletedWorkerPacket();
  void PumpAuthorityDigest(
    FCrowdAsyncSimulationRuntime& Runtime,
    uint64 Generation);
  void ProcessPendingAuthorityDigest(
    FCrowdAsyncSimulationRuntime& Runtime);
  void UpdateWorkerTrafficRates(double NowSeconds);
};
