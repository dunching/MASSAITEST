#include "Mass/CrowdDemoWorkerNetworkBridgeSubsystem.h"

#include "EngineUtils.h"
#include "Mass/CrowdDemoWorkerInputSync.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdRuntimeSubsystem.h"

void UCrowdDemoWorkerNetworkBridgeSubsystem::Tick(
  const float DeltaTime)
{
  (void)DeltaTime;
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() != NM_Client) return;
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();

  AMassCrowdReplicationActor* CheckpointChannel = nullptr;
  FCrowdWorkerNetworkCheckpoint NewestCheckpoint;
  bool bFoundCheckpoint = false;
  for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
  {
    AMassCrowdReplicationActor* Channel = *It;
    FCrowdWorkerNetworkCheckpoint Candidate;
    if (!Channel || !Channel->IsWorkerReady()
      || !Channel->ConsumeWorkerCheckpoint(Candidate))
      continue;
    if (!bFoundCheckpoint
      || Candidate.Header.Generation
        > NewestCheckpoint.Header.Generation
      || (Candidate.Header.Generation
          == NewestCheckpoint.Header.Generation
        && Candidate.Header.WorkerEpoch
          >= NewestCheckpoint.Header.WorkerEpoch))
    {
      NewestCheckpoint = MoveTemp(Candidate);
      CheckpointChannel = Channel;
      bFoundCheckpoint = true;
    }
  }

  if (bFoundCheckpoint)
  {
    const ECrowdAsyncSimulationRuntimeState State = Runtime.GetState();
    const bool bNeedsBootstrap =
      State == ECrowdAsyncSimulationRuntimeState::Stopped
      || Runtime.RequiresResnapshot()
      || Runtime.GetGeneration()
        != NewestCheckpoint.Header.Generation;
    if (bNeedsBootstrap)
    {
      PendingBootstrapCheckpoint = MoveTemp(NewestCheckpoint);
      bHasPendingBootstrapCheckpoint = true;
      if (State != ECrowdAsyncSimulationRuntimeState::Stopped
        && State != ECrowdAsyncSimulationRuntimeState::Draining
        && !Runtime.BeginStop()
        && CheckpointChannel)
      {
        CheckpointChannel->RequestResync();
      }
    }
    else
    {
      UE_LOG(LogTemp, VeryVerbose,
        TEXT("MassCrowdWorkerNetwork role=client stage=checkpoint_ignored_running generation=%llu epoch=%llu input=%llu"),
        NewestCheckpoint.Header.Generation,
        NewestCheckpoint.Header.WorkerEpoch,
        NewestCheckpoint.Header.LastAppliedInputSequence);
    }
  }

  if (Runtime.GetState() == ECrowdAsyncSimulationRuntimeState::Draining)
  {
    const ECrowdAsyncSimulationPollResult StopResult = Runtime.Poll();
    if (StopResult == ECrowdAsyncSimulationPollResult::Failed)
    {
      for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
        if (AMassCrowdReplicationActor* Channel = *It)
          Channel->RequestResync();
    }
    if (Runtime.GetState() != ECrowdAsyncSimulationRuntimeState::Stopped)
      return;
  }

  if (Runtime.GetState() == ECrowdAsyncSimulationRuntimeState::Stopped
    && bHasPendingBootstrapCheckpoint)
  {
    RuntimeSubsystem->GetWorkerShadowSync().ResetQuiescent();
    if (!FCrowdDemoWorkerInputSync::StartClientFromNetworkCheckpoint(
        *World, PendingBootstrapCheckpoint))
    {
      for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
        if (AMassCrowdReplicationActor* Channel = *It)
          Channel->RequestResync();
      return;
    }
    LastObservedInputSequence =
      PendingBootstrapCheckpoint.Header.LastAppliedInputSequence;
    ActiveCheckpointInputBaseline =
      PendingBootstrapCheckpoint.InputBaselineSequence;
    bLoggedFirstIntentApply = false;
    UE_LOG(LogTemp, Display,
      TEXT("MassCrowdWorkerNetwork role=client stage=checkpoint_bootstrap_complete generation=%llu epoch=%llu input=%llu"),
      PendingBootstrapCheckpoint.Header.Generation,
      PendingBootstrapCheckpoint.Header.WorkerEpoch,
      PendingBootstrapCheckpoint.Header.LastAppliedInputSequence);
    PendingBootstrapCheckpoint = {};
    bHasPendingBootstrapCheckpoint = false;
  }

  if (Runtime.GetState() != ECrowdAsyncSimulationRuntimeState::Running)
    return;
  const ECrowdAsyncSimulationPollResult PollResult = Runtime.Poll();
  if (PollResult == ECrowdAsyncSimulationPollResult::Failed)
  {
    for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
      if (AMassCrowdReplicationActor* Channel = *It)
        Channel->RequestResync();
    return;
  }

  const uint64 AppliedInputSequence =
    Runtime.GetMetrics().LastAppliedInputSequence;
  if (AppliedInputSequence <= LastObservedInputSequence) return;
  LastObservedInputSequence = AppliedInputSequence;
  if (!bLoggedFirstIntentApply || AppliedInputSequence % 300 == 0)
  {
    UE_LOG(LogTemp, Display,
      TEXT("MassCrowdWorkerNetwork role=client stage=intent_applied generation=%llu input=%llu"),
      Runtime.GetGeneration(), AppliedInputSequence);
    bLoggedFirstIntentApply = true;
  }

  int32 ForceAfterAppliedIntents = 0;
  if (!bForcedResyncRequested
    && FParse::Value(
      FCommandLine::Get(),
      TEXT("CrowdWorkerForceResyncAfterAppliedIntents="),
      ForceAfterAppliedIntents)
    && ForceAfterAppliedIntents > 0
    && AppliedInputSequence >= ActiveCheckpointInputBaseline
      + static_cast<uint64>(ForceAfterAppliedIntents))
  {
    bForcedResyncRequested = true;
    UE_LOG(LogTemp, Display,
      TEXT("MassCrowdWorkerNetwork role=client stage=forced_resync baseline_input=%llu applied_input=%llu threshold=%d"),
      ActiveCheckpointInputBaseline,
      AppliedInputSequence,
      ForceAfterAppliedIntents);
    for (TActorIterator<AMassCrowdReplicationActor> It(World); It; ++It)
      if (AMassCrowdReplicationActor* Channel = *It)
      {
        Channel->RequestResync();
        break;
      }
  }
}

TStatId UCrowdDemoWorkerNetworkBridgeSubsystem::GetStatId() const
{
  RETURN_QUICK_DECLARE_CYCLE_STAT(
    UCrowdDemoWorkerNetworkBridgeSubsystem,
    STATGROUP_Tickables);
}

bool UCrowdDemoWorkerNetworkBridgeSubsystem::DoesSupportWorldType(
  const EWorldType::Type WorldType) const
{
  return WorldType == EWorldType::Game
    || WorldType == EWorldType::PIE;
}
