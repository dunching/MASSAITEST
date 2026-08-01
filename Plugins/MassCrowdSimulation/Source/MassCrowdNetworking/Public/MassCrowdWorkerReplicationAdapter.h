#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAsyncSimulationRuntime.h"

enum class ECrowdWorkerReplicationPhase : uint8
{
  AwaitingCheckpoint = 0,
  AwaitingResourceRevisions,
  AwaitingEventBaseline,
  Live,
  RequiresResync
};

enum class ECrowdWorkerReplicationAcceptResult : uint8
{
  Accepted = 0,
  BaselineReady,
  Duplicate,
  RejectedOrder,
  RejectedContract,
  RequiresResync
};

class MASSCROWDNETWORKING_API FCrowdWorkerReplicationServerAdapter
{
public:
  static ECrowdWorkerNetworkReadResult CaptureCheckpoint(
    const FCrowdAsyncSimulationRuntime& Runtime,
    uint64 ExpectedGeneration,
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint);

};

class MASSCROWDNETWORKING_API FCrowdWorkerReplicationClientGate
{
public:
  bool Initialize(
    const FCrowdWorkerNetworkStateConfig& InConfig);

  ECrowdWorkerReplicationAcceptResult AcceptCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);
  ECrowdWorkerReplicationAcceptResult AcceptResourceRevisions(
    uint64 CheckpointStableHash,
    TConstArrayView<FCrowdWorkerResourceRecord> Resources);
  ECrowdWorkerReplicationAcceptResult AcceptEventBaseline(
    uint64 CheckpointStableHash,
    uint64 EventBaselineSequence);
  bool ConsumeReadyCheckpoint(
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint);

  ECrowdWorkerReplicationPhase GetPhase() const { return Phase; }
  uint64 GetGeneration() const { return Generation; }
  uint64 GetLastEventSequence() const { return LastEventSequence; }

private:
  void RequireResync();
  bool MatchesPendingResources(
    TConstArrayView<FCrowdWorkerResourceRecord> Resources) const;
  bool ValidateCheckpointLifecycle(
    TConstArrayView<FCrowdWorkerDirtyStateRecord> States,
    TConstArrayView<FCrowdWorkerLifecycleWatermark> Watermarks);

  struct FLogicalEntityKey
  {
    uint32 ProviderId = 0;
    uint64 StableEntityId = 0;

    bool operator==(const FLogicalEntityKey& Other) const = default;
    friend uint32 GetTypeHash(const FLogicalEntityKey& Key)
    {
      return HashCombineFast(
        ::GetTypeHash(Key.ProviderId),
        ::GetTypeHash(Key.StableEntityId));
    }
  };

  FCrowdWorkerNetworkStateConfig Config;
  FCrowdWorkerNetworkCheckpoint PendingCheckpoint;
  TMap<FLogicalEntityKey, uint32> LatestLifecycleByEntity;
  TSet<FLogicalEntityKey> ActiveEntities;
  ECrowdWorkerReplicationPhase Phase =
    ECrowdWorkerReplicationPhase::AwaitingCheckpoint;
  uint64 Generation = 0;
  uint64 CurrentCheckpointStableHash = 0;
  uint64 LastEventSequence = 0;
  bool bInitialized = false;
  bool bCheckpointConsumed = false;
};
