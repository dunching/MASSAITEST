#include "Mass/CrowdDemoWorkerInputSync.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassCrowdReplicationActor.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerFlowResource.h"
#include "MassCrowdWorkerInteractionDomain.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerMovementDomain.h"
#include "MassCrowdWorkerNavigationResource.h"
#include "MassCrowdWorkerTargetDomain.h"
#include "MassCrowdWorkerProjectileDomain.h"
#include "Mass/CrowdDemoWorkerCombatExtension.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Mass/CrowdDemoRoundSimPipelineSubsystem.h"

namespace CrowdDemoWorkerInputSyncPrivate
{
  constexpr int32 MaxQueuedShadowBatches = 8;
  constexpr int32 MaxShadowBatchesPerPump = 8;
  constexpr int32 MaxShadowStepsPerPump = 32;

  bool RegisterDomainExecutors(
    FCrowdAsyncSimulationRuntime& Runtime,
    FCrowdBehaviorSourceRuntime& BehaviorRuntime)
  {
    return Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerLifecycleDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerBehaviorDomainExecutor>(
          BehaviorRuntime.GetCapabilityProfiles(),
          BehaviorRuntime.GetEvaluators()))
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerFlowResourceDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerTargetDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerProjectileDomainExecutor>(
          MakeCrowdDemoWorkerCombatExtension()))
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerMovementPlanningDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerMovementDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerParticleInteractionDomainExecutor>())
      && Runtime.RegisterDomainExecutor(
        MakeUnique<FCrowdWorkerFacingFinalizeDomainExecutor>());
  }

  bool ConsumeWorkerLateJoinCheckpoint(
    UWorld& World,
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint)
  {
    OutCheckpoint = {};
    for (TActorIterator<AMassCrowdReplicationActor> It(&World); It; ++It)
    {
      AMassCrowdReplicationActor* Channel = *It;
      if (Channel && Channel->IsWorkerReady()
        && Channel->ConsumeWorkerCheckpoint(OutCheckpoint))
        return true;
    }
    return false;
  }

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

  bool ResolveBehaviorAuthorityMode(
    const TConstArrayView<FCrowdStableEntityRef> CurrentEntityRefs,
    ECrowdWorkerBehaviorAuthorityMode& OutMode,
    TArray<FCrowdStableEntityRef>& OutCanaries)
  {
    OutMode = ECrowdWorkerBehaviorAuthorityMode::Shadow;
    OutCanaries.Reset();
    FString Value;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerBehaviorMode="), Value))
      return true;
    if (Value.Equals(TEXT("Shadow"), ESearchCase::IgnoreCase))
      return true;
    if (Value.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
    {
      OutMode = ECrowdWorkerBehaviorAuthorityMode::Production;
      return true;
    }
    if (!Value.Equals(TEXT("Canary"), ESearchCase::IgnoreCase))
      return false;
    int32 CanaryCount = 0;
    if (!FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerBehaviorCanaryCount="), CanaryCount)
      || CanaryCount <= 0
      || CanaryCount >= CurrentEntityRefs.Num())
      return false;
    OutMode = ECrowdWorkerBehaviorAuthorityMode::Canary;
    TArray<FCrowdStableEntityRef> Sorted(CurrentEntityRefs);
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
      TEXT("VIOLATION CrowdDemoWorkerInputSyncFailure stage=%s generation=%llu submitted=%llu compared=%llu pending=%d last_submitted_input=%llu last_compared_input=%llu expected_entity_hash=%llu observed_entity_hash=%llu expected_state_hash=%llu observed_state_hash=%llu shadow_violation=%d runtime_accepted_input=%llu runtime_applied_input=%llu runtime_queued_watermark=%llu runtime_queue=%d runtime_input_failure=%u runtime_v2_failure=%u runtime_v2_work=%d/%d runtime_v2_inflight=%d runtime_sim_time=%.6f runtime_target_time=%.6f"),
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
      RuntimeMetrics.LastAcceptedInputSequence,
      RuntimeMetrics.LastAppliedInputSequence,
      RuntimeMetrics.QueuedInputSequenceWatermark,
      RuntimeMetrics.InputQueueDepth,
      static_cast<uint32>(RuntimeMetrics.LastInputFailure),
      static_cast<uint32>(
        RuntimeMetrics.WorkerV2.LastFailure),
      RuntimeMetrics.WorkerV2.WorkCurrentDepth,
      RuntimeMetrics.WorkerV2.WorkNextDepth,
      RuntimeMetrics.WorkerV2.ShardInFlightCount,
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
        FMath::Max(
          FCrowdWorkerNavTopologyCodec::MaxEncodedBytes,
          FCrowdWorkerProjectileControlResourceCodec::
            MaxEncodedBytes),
        FMath::Max3(
          StatePayload.Bytes.Num(),
          ResourcePayload.Bytes.Num(),
          FCrowdWorkerBoundaryStateCodec::
            MaxEncodedBehaviorCommandBytes),
        FMath::Max3(
          FCrowdWorkerBehaviorInputCodec::MaxEncodedBytes,
          FCrowdWorkerBehaviorBindingInputCodec::MaxEncodedBytes,
          FCrowdWorkerBehaviorStateCodec::MaxEncodedBytes));
    OutConfig.RuntimeConfig.NetworkState.MaxPayloadBytes =
      OutConfig.RuntimeConfig.ContractLimits.MaxPayloadBytes;
    OutConfig.RuntimeConfig.ContractLimits.MaxInputRecordsPerBatch =
      EntityCapacity * 2 + 16 + 4096;
    OutConfig.RuntimeConfig.ContractLimits.MaxStatePatchesPerSlot =
      EntityCapacity
        * static_cast<int32>(ECrowdWorkerField::Count);
    OutConfig.RuntimeConfig.ContractLimits.MaxPendingOrderedEvents =
      64000;
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
    OutConfig.RuntimeConfig.NetworkPublishIntervalEpochs = 300;
    OutConfig.RuntimeConfig.WorkerV2 =
      FCrowdWorkerRuntimeV2Config::MakeProduction10k();
    FString MovementModeValue;
    const bool bProductionMovement =
      FParse::Value(
        FCommandLine::Get(),
        TEXT("CrowdWorkerMovementMode="),
        MovementModeValue)
      && MovementModeValue.Equals(
        TEXT("Production"), ESearchCase::IgnoreCase);
    OutConfig.RuntimeConfig.WorkerV2.Mode =
      bProductionMovement
        ? ECrowdWorkerRuntimeV2Mode::Production
        : ECrowdWorkerRuntimeV2Mode::Shadow;
    OutConfig.MaxPendingExpectations = MaxQueuedShadowBatches;
    return OutConfig.IsValid();
  }

  bool BuildConfigFromCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint,
    const double FixedSimulationQuantumSeconds,
    FCrowdWorkerShadowSyncConfig& OutConfig)
  {
    OutConfig = {};
    if (!FMath::IsFinite(FixedSimulationQuantumSeconds)
      || FixedSimulationQuantumSeconds <= 0.0)
      return false;
    int32 EntityCapacity = 0;
    int32 MaxObservedPayloadBytes = 0;
    for (const FCrowdWorkerDirtyStateRecord& State :
      Checkpoint.StateRecords)
    {
      if (State.Field == ECrowdWorkerField::InputSnapshot)
        ++EntityCapacity;
      MaxObservedPayloadBytes = FMath::Max(
        MaxObservedPayloadBytes, State.Payload.Bytes.Num());
    }
    for (const FCrowdWorkerResourceRecord& Resource :
      Checkpoint.ResourceRecords)
      MaxObservedPayloadBytes = FMath::Max(
        MaxObservedPayloadBytes, Resource.Payload.Bytes.Num());
    for (const FCrowdWorkerCommandRecord& Command :
      Checkpoint.Continuation.Commands)
      MaxObservedPayloadBytes = FMath::Max(
        MaxObservedPayloadBytes, Command.Payload.Bytes.Num());
    if (EntityCapacity <= 0) return false;

    OutConfig.RuntimeConfig.ContractLimits.MaxPayloadBytes =
      FMath::Max3(
        FMath::Max(
          FCrowdWorkerNavTopologyCodec::MaxEncodedBytes,
          FCrowdWorkerProjectileControlResourceCodec::MaxEncodedBytes),
        FMath::Max3(
          MaxObservedPayloadBytes,
          FCrowdWorkerBoundaryStateCodec::EncodedStateByteCount,
          FCrowdWorkerBoundaryStateCodec::MaxEncodedBehaviorCommandBytes),
        FMath::Max3(
          FCrowdWorkerBehaviorInputCodec::MaxEncodedBytes,
          FCrowdWorkerBehaviorBindingInputCodec::MaxEncodedBytes,
          FCrowdWorkerBehaviorStateCodec::MaxEncodedBytes));
    OutConfig.RuntimeConfig.NetworkState.MaxPayloadBytes =
      OutConfig.RuntimeConfig.ContractLimits.MaxPayloadBytes;
    OutConfig.RuntimeConfig.ContractLimits.MaxInputRecordsPerBatch =
      EntityCapacity * 2 + 16 + 4096;
    OutConfig.RuntimeConfig.ContractLimits.MaxStatePatchesPerSlot =
      EntityCapacity
        * static_cast<int32>(ECrowdWorkerField::Count);
    OutConfig.RuntimeConfig.ContractLimits.MaxPendingOrderedEvents = 64000;
    OutConfig.RuntimeConfig.FixedSimulationQuantumSeconds =
      FixedSimulationQuantumSeconds;
    OutConfig.RuntimeConfig.MaxQueuedInputBatches = MaxQueuedShadowBatches;
    OutConfig.RuntimeConfig.MaxInputBatchesPerPump = MaxShadowBatchesPerPump;
    OutConfig.RuntimeConfig.MaxSimulationStepsPerPump = MaxShadowStepsPerPump;
    OutConfig.RuntimeConfig.MaxPendingCommands = 4096;
    OutConfig.RuntimeConfig.MaxInFlightShadowWorks =
      MaxQueuedShadowBatches * 3;
    OutConfig.RuntimeConfig.NetworkPublishIntervalEpochs = 300;
    OutConfig.RuntimeConfig.WorkerV2 =
      FCrowdWorkerRuntimeV2Config::MakeProduction10k();
    OutConfig.MaxPendingExpectations = MaxQueuedShadowBatches;
    return OutConfig.IsValid()
      && Checkpoint.IsValid(OutConfig.RuntimeConfig.NetworkState);
  }
}

using namespace CrowdDemoWorkerInputSyncPrivate;

bool FCrowdDemoWorkerInputSync::SubmitBoundarySnapshot(
  UWorld& World,
  const FCrowdMassBoundarySnapshot& Snapshot,
  const double FixedSimulationQuantumSeconds,
  const double TargetSimulationTimeSeconds,
  const TConstArrayView<FCrowdWorkerVersionedResourceInput>
    AdditionalResources,
  const FCrowdBehaviorPreparedBoundary* StagedBehavior,
  const bool bAutonomousAfterBootstrap)
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
    FCrowdWorkerNetworkCheckpoint LateJoinCheckpoint;
    const bool bNetworkClient = World.GetNetMode() == NM_Client;
    if (bNetworkClient
      && !ConsumeWorkerLateJoinCheckpoint(
        World, LateJoinCheckpoint))
      return true;
    if (!BuildConfig(
        Snapshot, FixedSimulationQuantumSeconds, Config)
      || !RegisterDomainExecutors(Runtime, BehaviorRuntime))
      return false;
    if (bNetworkClient
      ? !Shadow.StartFromNetworkCheckpoint(
        Runtime, Config, LateJoinCheckpoint)
      : !Shadow.Start(Runtime, Config, 1))
      return false;
    if (!RuntimeSubsystem->GetWorkerResultApplyProxy().
      ResetQuiescent(
        Shadow.GetGeneration(),
        Config.RuntimeConfig.ContractLimits))
      return false;
    ECrowdWorkerMovementAuthorityMode MovementMode;
    TArray<FCrowdStableEntityRef> CanaryEntities;
    if (!ResolveMovementAuthorityMode(
        CurrentEntityRefs, MovementMode, CanaryEntities)
      || !RuntimeSubsystem->GetWorkerMovementAuthority().
        ResetQuiescent(
          Shadow.GetGeneration(),
          MovementMode,
          CanaryEntities))
      return false;
    ECrowdWorkerBehaviorAuthorityMode BehaviorMode;
    TArray<FCrowdStableEntityRef> BehaviorCanaries;
    if (!ResolveBehaviorAuthorityMode(
        CurrentEntityRefs, BehaviorMode, BehaviorCanaries)
      || !RuntimeSubsystem->GetWorkerBehaviorAuthority().
        ResetQuiescent(
          Shadow.GetGeneration(),
          BehaviorMode, BehaviorCanaries,
          MaxQueuedShadowBatches))
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
  if (!RuntimeSubsystem->GetWorkerBehaviorAuthority().
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
  const bool bSubmitAutonomous = bAutonomousAfterBootstrap
    && Shadow.GetMetrics().FullResnapshotCount > 0;
  FCrowdMassBoundarySnapshot WorkerInputSnapshot = Snapshot;
  FCrowdWorkerMovementAuthority& MovementAuthority =
    RuntimeSubsystem->GetWorkerMovementAuthority();
  if (!bSubmitAutonomous
    && MovementAuthority.GetMode()
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
  TArray<FCrowdWorkerVersionedResourceInput>
    VersionedResources;
  const FCrowdNavGraphResource& NavResource =
    RuntimeSubsystem->GetNavGraphResource();
  if (NavResource.IsReady())
  {
    FCrowdWorkerVersionedResourceInput& Input =
      VersionedResources.AddDefaulted_GetRef();
    Input.ResourceId = CrowdWorkerResourceIds::NavTopology;
    Input.Revision = NavResource.TopologyRevision;
    if (!FCrowdWorkerNavTopologyCodec::Encode(
        NavResource, Input.Payload))
      return false;
  }
  if (const UCrowdDemoRoundSimPipelineSubsystem* Pipeline =
    World.GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>())
  {
    const FCrowdSharedFlowField& FlowField =
      Pipeline->GetRuntimeSharedFlowField();
    if (FlowField.IsValid() && FlowField.Config.Revision > 0)
    {
      FCrowdWorkerVersionedResourceInput& Input =
        VersionedResources.AddDefaulted_GetRef();
      Input.ResourceId = CrowdWorkerResourceIds::Environment;
      FCrowdWorkerPayload ContentIdentity;
      if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
          FlowField, ContentIdentity))
        return false;
      if (!RuntimeSubsystem->ResolveWorkerResourceRevision(
          Input.ResourceId,
          static_cast<uint64>(FlowField.Config.Revision),
          ContentIdentity.StableHash,
          Input.Revision)
        || Input.Revision > static_cast<uint64>(MAX_int32))
        return false;
      FCrowdSharedFlowField PublishedFlowField = FlowField;
      PublishedFlowField.Config.Revision =
        static_cast<int32>(Input.Revision);
      if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
          PublishedFlowField, Input.Payload))
        return false;
    }
  }
  for (const FCrowdWorkerVersionedResourceInput& Additional :
    AdditionalResources)
  {
    if (Additional.ResourceId == 0
      || Additional.Revision == 0
      || Additional.Payload.SchemaId == 0
      || Additional.Payload.SchemaVersion == 0
      || Additional.Payload.Bytes.IsEmpty()
      || Additional.Payload.StableHash
        != Additional.Payload.CalculateStableHash()
      || VersionedResources.ContainsByPredicate(
        [&Additional](
          const FCrowdWorkerVersionedResourceInput& Existing)
        {
          return Existing.ResourceId == Additional.ResourceId;
        }))
      return false;
    VersionedResources.Add(Additional);
  }
  VersionedResources.Sort(
    [](const FCrowdWorkerVersionedResourceInput& A,
      const FCrowdWorkerVersionedResourceInput& B)
    {
      return A.ResourceId < B.ResourceId;
    });
  FCrowdWorkerBehaviorAuthority& BehaviorAuthority =
    RuntimeSubsystem->GetWorkerBehaviorAuthority();
  const bool bBehaviorProduction =
    BehaviorAuthority.GetMode()
      == ECrowdWorkerBehaviorAuthorityMode::Production;
  TArray<FCrowdBehaviorEntityEvaluationContext>
    StagedBehaviorContexts;
  TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
    BehaviorContexts;
  TConstArrayView<FCrowdBehaviorSourceCommand> BehaviorCommands;
  TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
    BehaviorBindingUpdates;
  TArray<FCrowdBehaviorCapabilityBindingUpdate>
    SubmittedBehaviorBindingUpdates;
  const bool bHasStagedBehavior = StagedBehavior
    && StagedBehavior->bValid
    && StagedBehavior->FixedStepIndex == Snapshot.FixedStepIndex;
  if (bBehaviorProduction)
  {
    if (StagedBehavior && !bHasStagedBehavior)
      return false;
    if (bHasStagedBehavior)
    {
      StagedBehaviorContexts.Reserve(StagedBehavior->Entities.Num());
      for (const FCrowdBehaviorPreparedEntity& Entity :
        StagedBehavior->Entities)
      {
        const FCrowdBehaviorEntityEvaluationContext* Context =
          BehaviorRuntime.FindEvaluationContext(Entity.EntityRef);
        if (!Context
          || Context->FixedStepIndex != StagedBehavior->FixedStepIndex
          || Context->StableHash != Entity.EvaluationContextHash)
          return false;
        StagedBehaviorContexts.Add(*Context);
      }
      BehaviorContexts = StagedBehaviorContexts;
      BehaviorCommands = BehaviorRuntime.GetPendingCommands();
      BehaviorBindingUpdates =
        BehaviorRuntime.GetPendingBindingUpdates();
    }
    else if (!BehaviorRuntime.GetPendingCommands().IsEmpty()
      || !BehaviorRuntime.GetPendingBindingUpdates().IsEmpty())
      return false;

    SubmittedBehaviorBindingUpdates = BehaviorBindingUpdates;
    TArray<FCrowdStableEntityRef> EntitiesRequiringBinding;
    if (!BehaviorAuthority.GetEntitiesRequiringInitialBinding(
        Shadow.GetGeneration(), EntitiesRequiringBinding))
      return false;
    for (const FCrowdStableEntityRef& EntityRef :
      EntitiesRequiringBinding)
    {
      if (SubmittedBehaviorBindingUpdates.ContainsByPredicate(
          [&EntityRef](
            const FCrowdBehaviorCapabilityBindingUpdate& Update)
          {
            return Update.EntityRef == EntityRef;
          }))
        continue;
      FCrowdBehaviorCapabilityBindingUpdate& Update =
        SubmittedBehaviorBindingUpdates.AddDefaulted_GetRef();
      Update.EffectiveFixedStep = Snapshot.FixedStepIndex;
      Update.EntityRef = EntityRef;
      Update.Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      Update.RecalculateStableHash();
      if (!Update.IsValid()) return false;
    }
    SubmittedBehaviorBindingUpdates.Sort([](
      const FCrowdBehaviorCapabilityBindingUpdate& A,
      const FCrowdBehaviorCapabilityBindingUpdate& B)
    {
      if (A.EffectiveFixedStep != B.EffectiveFixedStep)
        return A.EffectiveFixedStep < B.EffectiveFixedStep;
      return A.EntityRef < B.EntityRef;
    });
    BehaviorBindingUpdates = SubmittedBehaviorBindingUpdates;
  }
  else
  {
    BehaviorContexts =
      BehaviorRuntime.GetWorkerInputContextJournal();
    BehaviorCommands =
      BehaviorRuntime.GetWorkerInputCommandJournal();
    BehaviorBindingUpdates =
      BehaviorRuntime.GetWorkerInputBindingJournal();
  }
  const ECrowdWorkerShadowSubmitResult SubmitResult =
    bSubmitAutonomous
      ? Shadow.SubmitAutonomousFrame(
          Runtime, Snapshot.FixedStepIndex, Snapshot.PlanRevision,
          TargetSimulationTimeSeconds, BehaviorCommands,
          BehaviorContexts, VersionedResources,
          BehaviorBindingUpdates)
      : Shadow.SubmitSnapshot(
          Runtime, WorkerInputSnapshot,
          TargetSimulationTimeSeconds,
          BehaviorCommands,
          BehaviorContexts,
          VersionedResources,
          BehaviorBindingUpdates);
  if (SubmitResult != ECrowdWorkerShadowSubmitResult::Accepted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerInputSyncSubmitRejected result=%u reason=%u fixed_step=%d target_time=%.6f resource_id=%llu previous_revision=%llu submitted_revision=%llu previous_hash=%llu submitted_hash=%llu"),
      static_cast<uint32>(SubmitResult),
      static_cast<uint32>(Shadow.GetMetrics().LastSubmitFailure),
      Snapshot.FixedStepIndex,
      TargetSimulationTimeSeconds,
      Shadow.GetMetrics().LastRejectedResourceId,
      Shadow.GetMetrics().LastRejectedPreviousRevision,
      Shadow.GetMetrics().LastRejectedSubmittedRevision,
      Shadow.GetMetrics().LastRejectedPreviousPayloadHash,
      Shadow.GetMetrics().LastRejectedSubmittedPayloadHash);
    LogSubmitFailure(TEXT("submit_rejected"), Shadow, Runtime);
    return false;
  }
  const int32 SubmittedCommandCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedCommandRecordCount);
  const int32 SubmittedBindingCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedBindingRecordCount);
  if (bBehaviorProduction)
  {
    if (bHasStagedBehavior)
    {
      if (!BehaviorAuthority.QueuePreparedExpectation(
          Shadow.GetGeneration(),
          Shadow.GetMetrics().LastSubmittedInputSequence,
          *StagedBehavior, BehaviorContexts))
        return false;
    }
    else
    {
      TArray<FCrowdStableEntityRef> EntityRefs;
      EntityRefs.Reserve(Snapshot.Agents.Num());
      for (const FCrowdMassBoundaryAgentRecord& Agent
        : Snapshot.Agents)
        EntityRefs.Add(Agent.AgentFacts.StableEntityRef);
      EntityRefs.Sort();
      if (!BehaviorAuthority.QueueAutonomousExpectation(
          Shadow.GetGeneration(),
          Shadow.GetMetrics().LastSubmittedInputSequence,
          EntityRefs))
        return false;
    }
    if (!BehaviorAuthority.MarkSubmittedBindings(
        Shadow.GetGeneration(), BehaviorBindingUpdates))
      return false;
  }
  else
  {
    if (!BehaviorAuthority.QueueCommittedExpectation(
        Shadow.GetGeneration(),
        Shadow.GetMetrics().LastSubmittedInputSequence,
        BehaviorRuntime, BehaviorContexts))
      return false;
    if (!BehaviorRuntime.AcknowledgeWorkerInputCommands(
        SubmittedCommandCount))
      return false;
    if (!BehaviorRuntime.AcknowledgeWorkerInputContexts(
        BehaviorContexts.Num()))
      return false;
    if (!BehaviorRuntime.AcknowledgeWorkerInputBindings(
        SubmittedBindingCount))
      return false;
    if (!BehaviorAuthority.MarkSubmittedBindings(
        Shadow.GetGeneration(), BehaviorBindingUpdates))
      return false;
  }
  const ECrowdWorkerShadowCompareResult AfterSubmit =
    Shadow.PollAndCompare(Runtime);
  LogMatch(Shadow, AfterSubmit);
  if (AfterSubmit == ECrowdWorkerShadowCompareResult::Violation)
    LogSubmitFailure(TEXT("after_submit"), Shadow, Runtime);
  return AfterSubmit != ECrowdWorkerShadowCompareResult::Violation;
}

bool FCrowdDemoWorkerInputSync::StartClientFromNetworkCheckpoint(
  UWorld& World,
  const FCrowdWorkerNetworkCheckpoint& Checkpoint,
  const double FixedSimulationQuantumSeconds)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  FCrowdWorkerBoundaryShadowSync& Shadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  if (Shadow.IsStarted())
    return Shadow.GetGeneration() == Checkpoint.Header.Generation;
  FCrowdWorkerShadowSyncConfig Config;
  FCrowdBehaviorSourceRuntime& BehaviorRuntime =
    RuntimeSubsystem->GetBehaviorSourceRuntime();
  if (!BuildConfigFromCheckpoint(
      Checkpoint, FixedSimulationQuantumSeconds, Config)
    || !RegisterDomainExecutors(Runtime, BehaviorRuntime)
    || !Shadow.StartFromNetworkCheckpoint(
      Runtime, Config, Checkpoint))
    return false;
  TArray<FCrowdStableEntityRef> CurrentEntityRefs;
  CurrentEntityRefs.Reserve(
    Checkpoint.Continuation.LifecycleWatermarks.Num());
  for (const FCrowdWorkerLifecycleWatermark& Watermark :
    Checkpoint.Continuation.LifecycleWatermarks)
  {
    CurrentEntityRefs.Add({
      Watermark.ProviderId,
      Watermark.StableEntityId,
      Watermark.LastLifecycleSerial});
  }
  if (!RuntimeSubsystem->GetWorkerResultApplyProxy().
      ResetFromCheckpoint(
        Shadow.GetGeneration(),
        Config.RuntimeConfig.ContractLimits,
        CurrentEntityRefs,
        Checkpoint.Header.LastAppliedInputSequence,
        Checkpoint.EventBaselineSequence))
    return false;
  UE_LOG(LogTemp, Display,
    TEXT("CrowdDemoWorkerNetworkBridge role=client stage=runtime_started generation=%llu sequence=%llu entities=%d"),
    Checkpoint.Header.Generation,
    Checkpoint.Header.LastAppliedInputSequence,
    Checkpoint.Continuation.LifecycleWatermarks.Num());
  return true;
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
  {
    if (ConsumerFrameSequence == 1
      || ConsumerFrameSequence % 300 == 0)
    {
      const FCrowdAsyncSimulationRuntimeMetrics Metrics =
        Runtime.GetMetrics();
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoWorkerResultApplyNoBatch consumer_frame=%llu exchange_result=%u runtime_state=%u epoch=%llu accepted_input=%llu applied_input=%llu published_dirty=%llu last_published_patches=%d work=%d/%d inflight=%d source=WorkerResultApply"),
        ConsumerFrameSequence,
        static_cast<uint32>(ExchangeResult),
        static_cast<uint32>(Runtime.GetState()),
        Metrics.WorkerEpoch,
        Metrics.LastAcceptedInputSequence,
        Metrics.LastAppliedInputSequence,
        Metrics.WorkerV2.PublishedDirtyStateCount,
        Metrics.LastPublishedPatchCount,
        Metrics.WorkerV2.WorkCurrentDepth,
        Metrics.WorkerV2.WorkNextDepth,
        Metrics.WorkerV2.ShardInFlightCount);
    }
    return true;
  }
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
  {
    const FCrowdWorkerResultApplyMetrics& FailureMetrics =
      Proxy.GetMetrics();
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoWorkerResultApplyReject result=%u generation=%llu publish=%llu previous_publish=%llu events=%d previous_event=%llu violation=%d"),
      static_cast<uint32>(ApplyResult), Batch->Generation,
      Batch->PublishSequence,
      FailureMetrics.LastConsumedPublishSequence,
      Batch->OrderedEvents.Num(),
      FailureMetrics.LastAppliedEventSequence,
      FailureMetrics.bViolation ? 1 : 0);
    return false;
  }
  if (World.GetNetMode() == NM_Client)
    return true;
  FCrowdWorkerBehaviorAuthority& BehaviorAuthority =
    RuntimeSubsystem->GetWorkerBehaviorAuthority();
  if (!BehaviorAuthority.IngestOrderedEvents(Batch->OrderedEvents))
    return false;
  for (int32 Index = 0; Index < MaxQueuedShadowBatches; ++Index)
  {
    const ECrowdWorkerBehaviorValidationResult Validation =
      BehaviorAuthority.ValidateAvailable(Proxy);
    if (Validation
        == ECrowdWorkerBehaviorValidationResult::Violation)
    {
      const FCrowdWorkerBehaviorAuthorityMetrics& BehaviorMetrics =
        BehaviorAuthority.GetMetrics();
      UE_LOG(LogTemp, Error,
        TEXT("VIOLATION CrowdDemoWorkerBehaviorParity matched=%llu pending=%d last_input=%llu mismatches=%llu"),
        BehaviorMetrics.MatchedExpectationCount,
        BehaviorMetrics.PendingExpectationCount,
        BehaviorMetrics.LastMatchedInputSequence,
        BehaviorMetrics.MismatchCount);
      return false;
    }
    if (Validation
        != ECrowdWorkerBehaviorValidationResult::Matched)
      break;
    const FCrowdWorkerBehaviorAuthorityMetrics& BehaviorMetrics =
      BehaviorAuthority.GetMetrics();
    if (BehaviorMetrics.MatchedExpectationCount == 1
      || BehaviorMetrics.MatchedExpectationCount % 300 == 0)
    {
      UE_LOG(LogTemp, Display,
        TEXT("CrowdDemoWorkerBehaviorCheckpoint matched=%llu pending=%d input_sequence=%llu mode=%u canaries=%d"),
        BehaviorMetrics.MatchedExpectationCount,
        BehaviorMetrics.PendingExpectationCount,
        BehaviorMetrics.LastMatchedInputSequence,
        static_cast<uint32>(BehaviorAuthority.GetMode()),
        BehaviorMetrics.CanaryEntityCount);
    }
  }
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
  const ECrowdAsyncShadowWorkSubmitResult SubmitResult =
    Runtime.SubmitShadowWork(MoveTemp(Submission));
  if (SubmitResult
      != ECrowdAsyncShadowWorkSubmitResult::Accepted)
  {
    const FCrowdAsyncSimulationRuntimeMetrics Metrics =
      Runtime.GetMetrics();
    UE_LOG(LogTemp, Error,
      TEXT("CrowdDemoKernelShadowRuntimeRejected result=%u state=%u kernel=%u work_sequence=%llu v2_failure=%u v2_work=%d/%d v2_inflight=%d shadow_inflight=%d"),
      static_cast<uint32>(SubmitResult),
      static_cast<uint32>(Runtime.GetState()),
      static_cast<uint32>(Kernel),
      WorkSequence,
      static_cast<uint32>(Metrics.WorkerV2.LastFailure),
      Metrics.WorkerV2.WorkCurrentDepth,
      Metrics.WorkerV2.WorkNextDepth,
      Metrics.WorkerV2.ShardInFlightCount,
      Metrics.InFlightShadowWorkCount);
  }
  return SubmitResult
    == ECrowdAsyncShadowWorkSubmitResult::Accepted;
}
