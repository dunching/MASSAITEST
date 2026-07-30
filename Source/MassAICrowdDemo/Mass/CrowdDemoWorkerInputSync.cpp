#include "Mass/CrowdDemoWorkerInputSync.h"

#include "Engine/World.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace CrowdDemoWorkerInputSyncPrivate
{
  constexpr int32 MaxQueuedShadowBatches = 8;
  constexpr int32 MaxShadowBatchesPerPump = 8;
  constexpr int32 MaxShadowStepsPerPump = 32;

  bool ResolveMovementAuthorityMode(
    const TConstArrayView<FCrowdStableEntityRef> CurrentEntityRefs,
    ECrowdWorkerMovementAuthorityMode& OutMode,
    TArray<FCrowdStableEntityRef>& OutCanaries)
  {
    OutMode = ECrowdWorkerMovementAuthorityMode::Shadow;
    OutCanaries.Reset();
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerMovementMode="), Value))
      return true;
    if (Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdWorkerMovementAuthorityMode::Production;
      return true;
    }
    if (!Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
      return false;
    int32 CanaryCount = 0;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerMovementCanaryCount="), CanaryCount)
      || CanaryCount <= 0
      || CanaryCount >= CurrentEntityRefs.Num())
      return false;
    OutMode = ECrowdWorkerMovementAuthorityMode::Canary;
    TArray<FCrowdStableEntityRef> Sorted;
    Sorted.Append(
      CurrentEntityRefs.GetData(), CurrentEntityRefs.Num());
    Sorted.Sort();
    OutCanaries.Append(Sorted.GetData(), CanaryCount);
    return true;
  }

  bool DrainShadowWork(FCrowdAsyncSimulationRuntime& Runtime)
  {
    TArray<FCrowdAsyncShadowWorkResult> Results;
    Runtime.CollectCompletedShadowWork(Results);
    for (const FCrowdAsyncShadowWorkResult& Result : Results)
    {
      if (!Result.bHashMatch)
      {
        UE_LOG(LogTemp, Error,
          TEXT("VIOLATION CrowdDemoKernelShadowMismatch generation=%llu kernel=%u work_sequence=%llu expected_hash=%llu actual_hash=%llu succeeded=%d"),
          Result.Generation,
          Result.KernelId,
          Result.WorkSequence,
          Result.ExpectedStableHash,
          Result.ActualStableHash,
          Result.bSucceeded ? 1 : 0);
        return false;
      }
    }
    const FCrowdAsyncSimulationRuntimeMetrics Metrics =
      Runtime.GetMetrics();
    const bool bCompletedShadowResult =
      Results.ContainsByPredicate([](
        const FCrowdAsyncShadowWorkResult& Result)
      {
        return Result.bRequiredExpectedStableHash;
      });
    if (bCompletedShadowResult
      && (Metrics.CompletedShadowWorkCount == 1
        || Metrics.CompletedShadowWorkCount % 900 == 0))
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoKernelShadowCheckpoint submitted=%llu completed=%llu in_flight=%d mismatches=%llu"),
        Metrics.SubmittedShadowWorkCount,
        Metrics.CompletedShadowWorkCount,
        Metrics.InFlightShadowWorkCount,
        Metrics.ShadowHashMismatchCount);
    }
    return true;
  }

  void LogMatch(
    const FCrowdWorkerBoundaryShadowSync& Shadow,
    const ECrowdWorkerShadowCompareResult Result)
  {
    if (Result != ECrowdWorkerShadowCompareResult::Match)
      return;
    const FCrowdWorkerShadowSyncMetrics& Metrics =
      Shadow.GetMetrics();
    if (Metrics.ComparedSnapshotCount != 1
      && Metrics.ComparedSnapshotCount % 300 != 0)
      return;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerShadowCheckpoint compared=%llu submitted=%llu commands=%llu pending=%d input_sequence=%llu source_hash=%llu entity_hash=%llu state_hash=%llu superseded=%llu"),
      Metrics.ComparedSnapshotCount,
      Metrics.SubmittedSnapshotCount,
      Metrics.SubmittedCommandRecordCount,
      Metrics.PendingExpectationCount,
      Metrics.LastComparedInputSequence,
      Metrics.LastComparedSourceSnapshotHash,
      Metrics.LastObservedEntitySetHash,
      Metrics.LastObservedStateHash,
      Metrics.SupersededExpectationCount);
  }

  void LogSubmitFailure(
    const TCHAR* Stage,
    const FCrowdWorkerBoundaryShadowSync& Shadow,
    const FCrowdAsyncSimulationRuntime& Runtime)
  {
    const FCrowdWorkerShadowSyncMetrics& ShadowMetrics =
      Shadow.GetMetrics();
    const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
      Runtime.GetMetrics();
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerInputSyncFailure stage=%s generation=%llu submitted=%llu compared=%llu pending=%d last_submitted_input=%llu last_compared_input=%llu expected_entity_hash=%llu observed_entity_hash=%llu expected_state_hash=%llu observed_state_hash=%llu shadow_violation=%d runtime_input=%llu runtime_queue=%d runtime_sim_time=%.6f runtime_target_time=%.6f"),
      Stage,
      ShadowMetrics.Generation,
      ShadowMetrics.SubmittedSnapshotCount,
      ShadowMetrics.ComparedSnapshotCount,
      ShadowMetrics.PendingExpectationCount,
      ShadowMetrics.LastSubmittedInputSequence,
      ShadowMetrics.LastComparedInputSequence,
      ShadowMetrics.LastExpectedEntitySetHash,
      ShadowMetrics.LastObservedEntitySetHash,
      ShadowMetrics.LastExpectedStateHash,
      ShadowMetrics.LastObservedStateHash,
      ShadowMetrics.bViolation ? 1 : 0,
      RuntimeMetrics.LastAppliedInputSequence,
      RuntimeMetrics.InputQueueDepth,
      RuntimeMetrics.SimulationTimeSeconds,
      RuntimeMetrics.TargetSimulationTimeSeconds);
  }

  bool BuildConfig(
    const FCrowdMassBoundarySnapshot& Snapshot,
    const double FixedSimulationQuantumSeconds,
    FCrowdWorkerShadowSyncConfig& OutConfig)
  {
    OutConfig = {};
    if (!Snapshot.bValid || Snapshot.Agents.IsEmpty()
      || !FMath::IsFinite(FixedSimulationQuantumSeconds)
      || FixedSimulationQuantumSeconds <= 0.0)
      return false;
    FCrowdWorkerPayload StatePayload;
    FCrowdWorkerPayload ResourcePayload;
    if (!FCrowdWorkerBoundaryStateCodec::EncodeState(
        Snapshot.Agents[0], StatePayload)
      || !FCrowdWorkerBoundaryStateCodec::EncodeSnapshotResource(
        Snapshot, ResourcePayload))
      return false;

    const int32 EntityCapacity = Snapshot.Agents.Num();
    OutConfig.RuntimeConfig.ContractLimits.MaxPayloadBytes =
      FMath::Max3(
        StatePayload.Bytes.Num(),
        ResourcePayload.Bytes.Num(),
        FCrowdWorkerBoundaryStateCodec::
          MaxEncodedBehaviorCommandBytes);
    OutConfig.RuntimeConfig.ContractLimits.MaxInputRecordsPerBatch =
      EntityCapacity * 2 + 1 + 4096;
    OutConfig.RuntimeConfig.ContractLimits.MaxStatePatchesPerSlot =
      EntityCapacity;
    OutConfig.RuntimeConfig.ContractLimits.MaxPendingOrderedEvents = 1;
    OutConfig.RuntimeConfig.FixedSimulationQuantumSeconds =
      FixedSimulationQuantumSeconds;
    OutConfig.RuntimeConfig.MaxQueuedInputBatches =
      MaxQueuedShadowBatches;
    OutConfig.RuntimeConfig.MaxInputBatchesPerPump =
      MaxShadowBatchesPerPump;
    OutConfig.RuntimeConfig.MaxSimulationStepsPerPump =
      MaxShadowStepsPerPump;
    OutConfig.RuntimeConfig.MaxPendingCommands = 4096;
    OutConfig.RuntimeConfig.MaxInFlightShadowWorks =
      MaxQueuedShadowBatches * 3;
    OutConfig.MaxPendingExpectations = MaxQueuedShadowBatches;
    return OutConfig.IsValid();
  }
}

using namespace CrowdDemoWorkerInputSyncPrivate;

bool FCrowdDemoWorkerInputSync::SubmitBoundarySnapshot(
  UWorld& World,
  const FCrowdMassBoundarySnapshot& Snapshot,
  const double FixedSimulationQuantumSeconds,
  const double TargetSimulationTimeSeconds)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  FCrowdWorkerBoundaryShadowSync& Shadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  FCrowdBehaviorSourceRuntime& BehaviorRuntime =
    RuntimeSubsystem->GetBehaviorSourceRuntime();
  if (BehaviorRuntime.HasWorkerInputCommandJournalOverflowed())
    return false;
  TArray<FCrowdStableEntityRef> CurrentEntityRefs;
  CurrentEntityRefs.Reserve(Snapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent :
    Snapshot.Agents)
    CurrentEntityRefs.Add(Agent.AgentFacts.StableEntityRef);
  if (!Shadow.IsStarted())
  {
    FCrowdWorkerShadowSyncConfig Config;
    if (!BuildConfig(
        Snapshot, FixedSimulationQuantumSeconds, Config)
      || !Shadow.Start(Runtime, Config, 1))
      return false;
    if (!RuntimeSubsystem->GetWorkerResultApplyProxy().
      ResetQuiescent(1, Config.RuntimeConfig.ContractLimits))
      return false;
    ECrowdWorkerMovementAuthorityMode MovementMode;
    TArray<FCrowdStableEntityRef> CanaryEntities;
    if (!ResolveMovementAuthorityMode(
        CurrentEntityRefs, MovementMode, CanaryEntities)
      || !RuntimeSubsystem->GetWorkerMovementAuthority().
        ResetQuiescent(1, MovementMode, CanaryEntities))
      return false;
  }
  if (!RuntimeSubsystem->GetWorkerResultApplyProxy().
    UpdateCurrentEntities(
      Shadow.GetGeneration(), CurrentEntityRefs))
    return false;
  if (!RuntimeSubsystem->GetWorkerMovementAuthority().
    UpdateCurrentEntities(
      Shadow.GetGeneration(), CurrentEntityRefs))
    return false;
  const ECrowdWorkerShadowCompareResult BeforeSubmit =
    Shadow.PollAndCompare(Runtime);
  LogMatch(Shadow, BeforeSubmit);
  if (BeforeSubmit == ECrowdWorkerShadowCompareResult::Violation)
  {
    LogSubmitFailure(TEXT("before_submit"), Shadow, Runtime);
    return false;
  }
  FCrowdMassBoundarySnapshot WorkerInputSnapshot = Snapshot;
  FCrowdWorkerMovementAuthority& MovementAuthority =
    RuntimeSubsystem->GetWorkerMovementAuthority();
  if (MovementAuthority.GetMode()
      != ECrowdWorkerMovementAuthorityMode::Shadow)
  {
    TArray<FCrowdMassBoundaryAgentRecord> SanitizedRecords =
      Snapshot.Agents;
    for (FCrowdMassBoundaryAgentRecord& Agent : SanitizedRecords)
    {
      if (!MovementAuthority.IsWorkerOwner(
          Agent.AgentFacts.StableEntityRef))
        continue;
      if (MovementAuthority.ValidateNormalInput(
          Agent.AgentFacts.StableEntityRef,
          CrowdWorkerMovementFields::Movement))
        return false;
      FCrowdWorkerMovementSample Sample;
      if (!MovementAuthority.Sample(
          Agent.AgentFacts.StableEntityRef,
          FMath::Max(
            0.0,
            TargetSimulationTimeSeconds
              - FixedSimulationQuantumSeconds),
          Sample))
        continue;
      Agent.State.Position = Sample.Position;
      Agent.State.Velocity = Sample.Velocity;
      Agent.State.YawDegrees = Sample.YawDegrees;
    }
    FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
      Snapshot.FixedStepIndex, Snapshot.PlanRevision,
      SanitizedRecords, WorkerInputSnapshot);
    if (!WorkerInputSnapshot.bValid)
      return false;
  }
  const ECrowdWorkerShadowSubmitResult SubmitResult =
    Shadow.SubmitSnapshot(
      Runtime, WorkerInputSnapshot,
      TargetSimulationTimeSeconds,
      BehaviorRuntime.GetWorkerInputCommandJournal());
  if (SubmitResult != ECrowdWorkerShadowSubmitResult::Accepted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerInputSyncSubmitRejected result=%u fixed_step=%d target_time=%.6f"),
      static_cast<uint32>(SubmitResult),
      Snapshot.FixedStepIndex,
      TargetSimulationTimeSeconds);
    LogSubmitFailure(TEXT("submit_rejected"), Shadow, Runtime);
    return false;
  }
  const int32 SubmittedCommandCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedCommandRecordCount);
  if (!BehaviorRuntime.AcknowledgeWorkerInputCommands(
      SubmittedCommandCount))
    return false;
  const ECrowdWorkerShadowCompareResult AfterSubmit =
    Shadow.PollAndCompare(Runtime);
  LogMatch(Shadow, AfterSubmit);
  if (AfterSubmit == ECrowdWorkerShadowCompareResult::Violation)
    LogSubmitFailure(TEXT("after_submit"), Shadow, Runtime);
  return AfterSubmit != ECrowdWorkerShadowCompareResult::Violation;
}

bool FCrowdDemoWorkerInputSync::Poll(UWorld& World)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem)
    return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  if (!DrainShadowWork(Runtime))
    return false;
  FCrowdWorkerBoundaryShadowSync& Shadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  return !Shadow.IsStarted()
    || Shadow.PollAndCompare(
      Runtime)
      != ECrowdWorkerShadowCompareResult::Violation;
}

bool FCrowdDemoWorkerInputSync::ConsumePublishedResults(
  UWorld& World,
  const uint64 ConsumerFrameSequence)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  if (Runtime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Stopped
    || Runtime.GetGeneration() == 0)
    return true;
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  const ECrowdWorkerExchangeResult ExchangeResult =
    Runtime.TryExchangePublishedBatch(
      Runtime.GetGeneration(), ConsumerFrameSequence, Batch);
  if (ExchangeResult
      == ECrowdWorkerExchangeResult::NoPublishedBatch
    || ExchangeResult
      == ECrowdWorkerExchangeResult::RejectedNotInitialized)
    return true;
  if (ExchangeResult != ECrowdWorkerExchangeResult::Exchanged
    || !Batch)
    return false;
  FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const uint64 ApplyStartCycles = FPlatformTime::Cycles64();
  const ECrowdWorkerResultApplyResult ApplyResult =
    Proxy.Apply(*Batch);
  const double ApplyMs = FPlatformTime::ToMilliseconds64(
    FPlatformTime::Cycles64() - ApplyStartCycles);
  if (ApplyResult != ECrowdWorkerResultApplyResult::Applied
    && ApplyResult
      != ECrowdWorkerResultApplyResult::AppliedEmpty)
    return false;
  const FCrowdWorkerResultApplyMetrics& Metrics =
    Proxy.GetMetrics();
  if (Metrics.AppliedBatchCount == 1
    || Metrics.AppliedBatchCount % 300 == 0)
  {
    const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
      Runtime.GetMetrics();
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerResultApplyCheckpoint batches=%llu empty=%llu patches=%llu stale_lifecycle=%llu events=%llu publish_sequence=%llu input_sequence=%llu proxies=%d input_queue_depth=%d oldest_input_age_ms=%.3f simulation_lag_ms=%.3f mirror_entities=%d scan_coverage_ms=%.3f owner_pump_ms=%.3f task_queue_ms=%.3f task_run_ms=%.3f task_critical_ms=%.3f publish_to_consume_ms=%.3f gt_apply_ms=%.3f published_patch_count=%d ordered_event_depth=%d resnapshots=%llu"),
      Metrics.AppliedBatchCount,
      Metrics.AppliedEmptyBatchCount,
      Metrics.AppliedPatchCount,
      Metrics.StaleLifecyclePatchCount,
      Metrics.AppliedEventCount,
      Metrics.LastConsumedPublishSequence,
      Metrics.LastAppliedInputSequence,
      Metrics.ProxyStateCount,
      RuntimeMetrics.InputQueueDepth,
      RuntimeMetrics.OldestInputAgeMs,
      RuntimeMetrics.SimulationLagMs,
      RuntimeMetrics.MirrorEntityCount,
      RuntimeMetrics.LastScanCoverageMs,
      RuntimeMetrics.LastOwnerPumpMs,
      RuntimeMetrics.LastTaskQueueMs,
      RuntimeMetrics.LastTaskRunMs,
      RuntimeMetrics.MaxTaskCriticalMs,
      RuntimeMetrics.LastPublishToConsumeMs,
      ApplyMs,
      RuntimeMetrics.LastPublishedPatchCount,
      RuntimeMetrics.LastPublishedEventCount,
      RuntimeMetrics.ResnapshotCount);
  }
  return true;
}

bool FCrowdDemoWorkerInputSync::SubmitShadowWork(
  UWorld& World,
  const EShadowKernel Kernel,
  const uint64 WorkSequence,
  const uint64 ExpectedStableHash,
  TFunction<uint64()>&& Execute,
  const bool bRequireExpectedStableHash)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  if (!DrainShadowWork(Runtime))
    return false;
  if (Runtime.GetState()
    == ECrowdAsyncSimulationRuntimeState::Starting)
    Runtime.Poll();
  if (Runtime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Starting
    || Runtime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Stopped)
    return true;
  FCrowdAsyncShadowWorkSubmission Submission;
  Submission.Generation = Runtime.GetGeneration();
  Submission.WorkSequence = WorkSequence;
  Submission.KernelId = static_cast<uint32>(Kernel);
  Submission.ExpectedStableHash = ExpectedStableHash;
  Submission.bRequireExpectedStableHash =
    bRequireExpectedStableHash;
  Submission.Execute = MoveTemp(Execute);
  return Runtime.SubmitShadowWork(MoveTemp(Submission))
    == ECrowdAsyncShadowWorkSubmitResult::Accepted;
}
