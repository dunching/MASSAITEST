#include "MassCrowdWorkerReplicationAdapter.h"

ECrowdWorkerNetworkReadResult
FCrowdWorkerReplicationServerAdapter::CaptureCheckpoint(
  const FCrowdAsyncSimulationRuntime& Runtime,
  const uint64 ExpectedGeneration,
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint)
{
  return Runtime.ReadNetworkCheckpoint(
    ExpectedGeneration, OutCheckpoint);
}

bool FCrowdWorkerReplicationClientGate::Initialize(
  const FCrowdWorkerNetworkStateConfig& InConfig)
{
  if (!InConfig.IsValid())
    return false;
  Config = InConfig;
  PendingCheckpoint = {};
  LatestLifecycleByEntity.Reset();
  ActiveEntities.Reset();
  Phase = ECrowdWorkerReplicationPhase::AwaitingCheckpoint;
  Generation = 0;
  CurrentCheckpointStableHash = 0;
  LastEventSequence = 0;
  bCheckpointConsumed = false;
  bInitialized = true;
  return true;
}

void FCrowdWorkerReplicationClientGate::RequireResync()
{
  Phase = ECrowdWorkerReplicationPhase::RequiresResync;
}

bool FCrowdWorkerReplicationClientGate::ValidateCheckpointLifecycle(
  const TConstArrayView<FCrowdWorkerDirtyStateRecord> States,
  const TConstArrayView<FCrowdWorkerLifecycleWatermark> Watermarks)
{
  TMap<FLogicalEntityKey, uint32> Candidate;
  TSet<FLogicalEntityKey> CandidateActive;
  for (const FCrowdWorkerDirtyStateRecord& State : States)
  {
    const FLogicalEntityKey Key{
      State.EntityRef.ProviderId,
      State.EntityRef.StableEntityId};
    if (const uint32* Existing = Candidate.Find(Key))
    {
      if (State.EntityRef.LifecycleSerial != *Existing)
        return false;
    }
    else
    {
      Candidate.Add(Key, State.EntityRef.LifecycleSerial);
      CandidateActive.Add(Key);
    }
  }
  for (const FCrowdWorkerLifecycleWatermark& Watermark : Watermarks)
  {
    const FLogicalEntityKey Key{
      Watermark.ProviderId, Watermark.StableEntityId};
    if (const uint32* ActiveSerial = Candidate.Find(Key))
    {
      if (Watermark.LastLifecycleSerial < *ActiveSerial)
        return false;
    }
    Candidate.Add(Key, Watermark.LastLifecycleSerial);
  }
  LatestLifecycleByEntity = MoveTemp(Candidate);
  ActiveEntities = MoveTemp(CandidateActive);
  return true;
}

ECrowdWorkerReplicationAcceptResult
FCrowdWorkerReplicationClientGate::AcceptCheckpoint(
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
{
  if (!bInitialized
    || Phase == ECrowdWorkerReplicationPhase::RequiresResync)
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  if (Phase != ECrowdWorkerReplicationPhase::AwaitingCheckpoint)
    return ECrowdWorkerReplicationAcceptResult::RejectedOrder;
  if (!Checkpoint.IsValid(Config))
    return ECrowdWorkerReplicationAcceptResult::RejectedContract;

  PendingCheckpoint = Checkpoint;
  LatestLifecycleByEntity.Reset();
  ActiveEntities.Reset();
  if (!ValidateCheckpointLifecycle(
      Checkpoint.StateRecords,
      Checkpoint.Continuation.LifecycleWatermarks))
  {
    RequireResync();
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  }
  Generation = Checkpoint.Header.Generation;
  CurrentCheckpointStableHash = Checkpoint.StableHash;
  if (Checkpoint.InputBaselineSequence
      != Checkpoint.Header.LastAppliedInputSequence)
  {
    RequireResync();
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  }
  LastEventSequence = Checkpoint.EventBaselineSequence;
  bCheckpointConsumed = false;
  Phase =
    ECrowdWorkerReplicationPhase::AwaitingResourceRevisions;
  return ECrowdWorkerReplicationAcceptResult::Accepted;
}

bool FCrowdWorkerReplicationClientGate::MatchesPendingResources(
  const TConstArrayView<FCrowdWorkerResourceRecord> Resources) const
{
  if (Resources.Num() != PendingCheckpoint.ResourceRecords.Num())
    return false;
  for (int32 Index = 0; Index < Resources.Num(); ++Index)
  {
    const FCrowdWorkerResourceRecord& A = Resources[Index];
    const FCrowdWorkerResourceRecord& B =
      PendingCheckpoint.ResourceRecords[Index];
    if (A.ResourceId != B.ResourceId
      || A.Revision != B.Revision
      || A.Payload != B.Payload)
      return false;
  }
  return true;
}

ECrowdWorkerReplicationAcceptResult
FCrowdWorkerReplicationClientGate::AcceptResourceRevisions(
  const uint64 CheckpointStableHash,
  const TConstArrayView<FCrowdWorkerResourceRecord> Resources)
{
  if (!bInitialized
    || Phase == ECrowdWorkerReplicationPhase::RequiresResync)
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  if (Phase !=
      ECrowdWorkerReplicationPhase::AwaitingResourceRevisions)
    return ECrowdWorkerReplicationAcceptResult::RejectedOrder;
  if (CheckpointStableHash != CurrentCheckpointStableHash
    || !MatchesPendingResources(Resources))
  {
    RequireResync();
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  }
  Phase = ECrowdWorkerReplicationPhase::AwaitingEventBaseline;
  return ECrowdWorkerReplicationAcceptResult::Accepted;
}

ECrowdWorkerReplicationAcceptResult
FCrowdWorkerReplicationClientGate::AcceptEventBaseline(
  const uint64 CheckpointStableHash,
  const uint64 EventBaselineSequence)
{
  if (!bInitialized
    || Phase == ECrowdWorkerReplicationPhase::RequiresResync)
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  if (Phase != ECrowdWorkerReplicationPhase::AwaitingEventBaseline)
    return ECrowdWorkerReplicationAcceptResult::RejectedOrder;
  if (CheckpointStableHash != CurrentCheckpointStableHash
    || EventBaselineSequence != LastEventSequence)
  {
    RequireResync();
    return ECrowdWorkerReplicationAcceptResult::RequiresResync;
  }
  Phase = ECrowdWorkerReplicationPhase::Live;
  return ECrowdWorkerReplicationAcceptResult::BaselineReady;
}

bool FCrowdWorkerReplicationClientGate::ConsumeReadyCheckpoint(
  FCrowdWorkerNetworkCheckpoint& OutCheckpoint)
{
  OutCheckpoint = {};
  if (!bInitialized
    || Phase != ECrowdWorkerReplicationPhase::Live
    || bCheckpointConsumed)
    return false;
  OutCheckpoint = PendingCheckpoint;
  bCheckpointConsumed = true;
  return true;
}
