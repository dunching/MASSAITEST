#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassCrowdReplicationChannel.h"
#include "MassCrowdReplicationActor.generated.h"

class APlayerController;

UCLASS(NotPlaceable)
class MASSCROWDNETWORKING_API AMassCrowdReplicationActor final
  : public AActor
{
  GENERATED_BODY()

public:
  AMassCrowdReplicationActor();
  virtual void BeginPlay() override;

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

private:
  FCrowdReplicationChannelLimits Limits;
  FCrowdReplicationServerState ServerState;
  FCrowdReplicationClientState ClientState;
  TArray<FCrowdRelevantSnapshotEntityPayload> CompletedBaselineEntities;
  uint32 CompletedBaselineRevision = 0;
  uint64 CompletedResumeSequence = 0;
  bool bClientReady = false;

  double NowSeconds() const;
  void HandleClientFailure();
  void SendReliable(const FCrowdReliableStateRecord& Record);
  void SendReliableBatch(
    TConstArrayView<FCrowdReliableStateRecord> Records);
};
