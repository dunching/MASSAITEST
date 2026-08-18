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
#include "Mass/CrowdDemoMassSubsystem.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

  bool BuildVersionedResources(
    UMassCrowdRuntimeSubsystem& RuntimeSubsystem,
    const TConstArrayView<FCrowdWorkerVersionedResourceInput>
      AdditionalResources,
    TArray<FCrowdWorkerVersionedResourceInput>& OutResources)
  {
    OutResources.Reset();
    const FCrowdNavGraphResource& NavResource =
      RuntimeSubsystem.GetNavGraphResource();
    if (NavResource.IsReady())
    {
      FCrowdWorkerPayload ContentIdentity;
      if (!FCrowdWorkerNavTopologyCodec::Encode(
          NavResource, ContentIdentity))
        return false;
      uint64 Revision = 0;
      bool bNeedsPublication = false;
      if (!RuntimeSubsystem.ResolveWorkerResourceRevision(
          CrowdWorkerResourceIds::NavTopology,
          NavResource.TopologyRevision,
          ContentIdentity.StableHash, Revision,
          bNeedsPublication)
        || Revision > static_cast<uint64>(MAX_uint32))
        return false;
      if (bNeedsPublication)
      {
        FCrowdNavGraphResource PublishedNavResource = NavResource;
        PublishedNavResource.TopologyRevision =
          static_cast<uint32>(Revision);
        FCrowdWorkerVersionedResourceInput& Input =
          OutResources.AddDefaulted_GetRef();
        Input.ResourceId = CrowdWorkerResourceIds::NavTopology;
        Input.Revision = Revision;
        if (!FCrowdWorkerNavTopologyCodec::Encode(
            PublishedNavResource, Input.Payload))
          return false;
      }
    }
    const FCrowdSharedFlowField& FlowField =
      RuntimeSubsystem.GetSharedFlowResource().Field;
    if (FlowField.IsValid() && FlowField.Config.Revision > 0)
    {
      FCrowdWorkerPayload ContentIdentity;
      if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
          FlowField, ContentIdentity))
        return false;
      uint64 Revision = 0;
      bool bNeedsPublication = false;
      if (!RuntimeSubsystem.ResolveWorkerResourceRevision(
          CrowdWorkerResourceIds::Environment,
          static_cast<uint64>(FlowField.Config.Revision),
          ContentIdentity.StableHash,
          Revision, bNeedsPublication)
        || Revision > static_cast<uint64>(MAX_int32))
        return false;
      if (bNeedsPublication)
      {
        FCrowdWorkerVersionedResourceInput& Input =
          OutResources.AddDefaulted_GetRef();
        Input.ResourceId = CrowdWorkerResourceIds::Environment;
        Input.Revision = Revision;
        FCrowdSharedFlowField PublishedFlowField = FlowField;
        PublishedFlowField.Config.Revision =
          static_cast<int32>(Input.Revision);
        if (!FCrowdWorkerFlowFieldResourceCodec::Encode(
            PublishedFlowField, Input.Payload))
          return false;
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoWorkerEnvironmentCheckpoint revision=%llu field_revision=%d build_hash=%u integration_hash=%u source=RuntimeSharedFlowOwner"),
          Input.Revision, FlowField.Config.Revision,
          FlowField.BuildHash, FlowField.IntegrationHash);
      }
    }
    for (const FCrowdWorkerVersionedResourceInput& Additional :
      AdditionalResources)
    {
      if (Additional.ResourceId == 0 || Additional.Revision == 0
        || Additional.Payload.SchemaId == 0
        || Additional.Payload.SchemaVersion == 0
        || Additional.Payload.Bytes.IsEmpty()
        || Additional.Payload.StableHash
          != Additional.Payload.CalculateStableHash()
        || OutResources.ContainsByPredicate(
          [&Additional](
            const FCrowdWorkerVersionedResourceInput& Existing)
          {
            return Existing.ResourceId == Additional.ResourceId;
          }))
        return false;
      OutResources.Add(Additional);
    }
    OutResources.Sort([](
      const FCrowdWorkerVersionedResourceInput& A,
      const FCrowdWorkerVersionedResourceInput& B)
    {
      return A.ResourceId < B.ResourceId;
    });
    return true;
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
    FCrowdWorkerShadowSyncConfig& OutConfig)
  {
    OutConfig = {};
    if (!FMath::IsFinite(
        Checkpoint.Header.FixedSimulationQuantumSeconds)
      || Checkpoint.Header.FixedSimulationQuantumSeconds <= 0.0)
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
      Checkpoint.Header.FixedSimulationQuantumSeconds;
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
  const TConstArrayView<FCrowdWorkerExternalGameplayInput>
    ExternalGameplayInputs)
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
  TArray<FCrowdWorkerVersionedResourceInput>
    VersionedResources;
  if (!BuildVersionedResources(
      *RuntimeSubsystem, AdditionalResources,
      VersionedResources))
    return false;
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
    Shadow.SubmitSnapshot(
      Runtime, WorkerInputSnapshot,
      TargetSimulationTimeSeconds,
      BehaviorCommands,
      BehaviorContexts,
      VersionedResources,
      BehaviorBindingUpdates,
      ExternalGameplayInputs);
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
  for (const FCrowdWorkerVersionedResourceInput& Resource :
    VersionedResources)
  {
    if (!RuntimeSubsystem->AcknowledgeWorkerResourceRevision(
        Resource.ResourceId, Resource.Revision))
      return false;
  }
  const int32 SubmittedCommandCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedCommandRecordCount);
  const int32 SubmittedBindingCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedBindingRecordCount);
  if (bBehaviorProduction)
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
        EntityRefs,
        bHasStagedBehavior))
      return false;
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

bool FCrowdDemoWorkerInputSync::SubmitIntentBatch(
  UWorld& World,
  const int32 SimulationTick,
  const int32 PlanRevision,
  const double TargetSimulationTimeSeconds,
  const TConstArrayView<FCrowdWorkerVersionedResourceInput>
    ResourceRevisions,
  const TConstArrayView<FCrowdWorkerSpawnDelta> Spawns,
  const TConstArrayView<FCrowdWorkerDespawnDelta> Despawns,
  const TConstArrayView<FCrowdWorkerExternalGameplayInput>
    ExternalGameplayInputs,
  const FCrowdBehaviorPreparedBoundary* StagedBehavior,
  const TConstArrayView<FCrowdWorkerObjectiveRevisionDelta>
    ObjectiveRevisions)
{
  check(IsInGameThread());
  if (SimulationTick < 0 || PlanRevision < 0
    || !FMath::IsFinite(TargetSimulationTimeSeconds)
    || TargetSimulationTimeSeconds < 0.0)
    return false;
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem) return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  FCrowdWorkerBoundaryShadowSync& Shadow =
    RuntimeSubsystem->GetWorkerShadowSync();
  FCrowdBehaviorSourceRuntime& BehaviorRuntime =
    RuntimeSubsystem->GetBehaviorSourceRuntime();
  if (!Shadow.IsStarted()
    || Shadow.GetMetrics().FullResnapshotCount == 0
    || BehaviorRuntime.HasWorkerInputCommandJournalOverflowed())
    return false;

  TArray<FCrowdWorkerSpawnDelta> CombinedSpawns;
  TArray<FCrowdWorkerDespawnDelta> CombinedDespawns;
  TArray<FCrowdWorkerExternalGameplayInput> CombinedExternalInputs;
  CombinedSpawns.Append(Spawns.GetData(), Spawns.Num());
  CombinedDespawns.Append(Despawns.GetData(), Despawns.Num());
  CombinedExternalInputs.Append(
    ExternalGameplayInputs.GetData(), ExternalGameplayInputs.Num());
  UCrowdDemoMassSubsystem* MassSubsystem =
    World.GetSubsystem<UCrowdDemoMassSubsystem>();
  TArray<FCrowdWorkerSpawnDelta> JournalSpawns;
  TArray<FCrowdWorkerDespawnDelta> JournalDespawns;
  TArray<FCrowdWorkerExternalGameplayInput> JournalProfileRevisions;
  if (MassSubsystem
    && !MassSubsystem->CopyPendingWorkerLifecycleProfileJournal(
      JournalSpawns, JournalDespawns, JournalProfileRevisions))
    return false;
  CombinedSpawns.Append(JournalSpawns);
  CombinedDespawns.Append(JournalDespawns);
  CombinedExternalInputs.Append(JournalProfileRevisions);

  FCrowdWorkerResultApplyProxy& ResultApplyProxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const TConstArrayView<FCrowdStableEntityRef> CurrentEntityRefs =
    ResultApplyProxy.GetStableEntityView();
  const bool bLifecycleChanged = !CombinedSpawns.IsEmpty()
    || !CombinedDespawns.IsEmpty();
  TArray<FCrowdStableEntityRef> CandidateEntityRefs;
  if (bLifecycleChanged)
    CandidateEntityRefs.Append(
      CurrentEntityRefs.GetData(), CurrentEntityRefs.Num());
  for (const FCrowdWorkerDespawnDelta& Despawn : CombinedDespawns)
  {
    if (Despawn.InputSequence != 0
      || CandidateEntityRefs.RemoveSingle(Despawn.EntityRef) != 1)
      return false;
  }
  for (const FCrowdWorkerSpawnDelta& Spawn : CombinedSpawns)
  {
    if (Spawn.InputSequence != 0
      || Spawn.EntityRef.IsUnset()
      || CandidateEntityRefs.Contains(Spawn.EntityRef))
      return false;
    CandidateEntityRefs.Add(Spawn.EntityRef);
  }
  CandidateEntityRefs.Sort();
  for (const FCrowdWorkerExternalGameplayInput& Input :
    CombinedExternalInputs)
  {
    if (Input.InputSequence != 0
      || (!Input.EntityRef.IsUnset()
        && (bLifecycleChanged
          ? !CandidateEntityRefs.Contains(Input.EntityRef)
          : ResultApplyProxy.FindStableEntitySlot(Input.EntityRef)
            == INDEX_NONE)))
      return false;
  }
  if (CurrentEntityRefs.IsEmpty()
    || (bLifecycleChanged && CandidateEntityRefs.IsEmpty())
    || (bLifecycleChanged
      && (!RuntimeSubsystem->GetWorkerMovementAuthority().
          UpdateCurrentEntities(
            Shadow.GetGeneration(), CurrentEntityRefs)
        || !RuntimeSubsystem->GetWorkerBehaviorAuthority().
          UpdateCurrentEntities(
            Shadow.GetGeneration(), CurrentEntityRefs))))
    return false;

  const ECrowdWorkerShadowCompareResult BeforeSubmit =
    Shadow.PollAndCompare(Runtime);
  LogMatch(Shadow, BeforeSubmit);
  if (BeforeSubmit == ECrowdWorkerShadowCompareResult::Violation)
  {
    LogSubmitFailure(TEXT("intent_before_submit"), Shadow, Runtime);
    return false;
  }

  TArray<FCrowdWorkerVersionedResourceInput> VersionedResources;
  if (!BuildVersionedResources(
      *RuntimeSubsystem, ResourceRevisions,
      VersionedResources))
    return false;

  FCrowdWorkerBehaviorAuthority& BehaviorAuthority =
    RuntimeSubsystem->GetWorkerBehaviorAuthority();
  const bool bBehaviorProduction =
    BehaviorAuthority.GetMode()
      == ECrowdWorkerBehaviorAuthorityMode::Production;
  TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
    BehaviorContexts;
  TConstArrayView<FCrowdBehaviorSourceCommand> BehaviorCommands;
  TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
    BehaviorBindingUpdates;
  TArray<FCrowdBehaviorEntityEvaluationContext>
    StagedBehaviorContexts;
  TArray<FCrowdBehaviorCapabilityBindingUpdate>
    InitialBindingUpdates;
  const bool bHasStagedBehavior = StagedBehavior
    && StagedBehavior->bValid
    && StagedBehavior->FixedStepIndex == SimulationTick;
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
        // Production Behavior owns the entity's Position/Velocity/Facing
        // from the Worker state store. Ordinary intent frames only need to
        // transport typed external context records; an empty record set is
        // advanced locally from the ordered Clock intent.
        if (!Context->Records.IsEmpty())
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
    TArray<FCrowdStableEntityRef> EntitiesRequiringBinding;
    if (!BehaviorAuthority.GetEntitiesRequiringInitialBinding(
        Shadow.GetGeneration(), EntitiesRequiringBinding))
      return false;
    InitialBindingUpdates.Append(BehaviorBindingUpdates.GetData(),
      BehaviorBindingUpdates.Num());
    InitialBindingUpdates.Reserve(
      InitialBindingUpdates.Num()
        + EntitiesRequiringBinding.Num() + CombinedSpawns.Num());
    for (const FCrowdStableEntityRef& EntityRef :
      EntitiesRequiringBinding)
    {
      if (InitialBindingUpdates.ContainsByPredicate(
          [&EntityRef](
            const FCrowdBehaviorCapabilityBindingUpdate& Update)
          {
            return Update.EntityRef == EntityRef;
          }))
        continue;
      FCrowdBehaviorCapabilityBindingUpdate& Update =
        InitialBindingUpdates.AddDefaulted_GetRef();
      Update.EffectiveFixedStep = SimulationTick;
      Update.EntityRef = EntityRef;
      Update.Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      Update.RecalculateStableHash();
      if (!Update.IsValid()) return false;
    }
    for (const FCrowdWorkerSpawnDelta& Spawn : CombinedSpawns)
    {
      if (InitialBindingUpdates.ContainsByPredicate(
          [&Spawn](
            const FCrowdBehaviorCapabilityBindingUpdate& Update)
          {
            return Update.EntityRef == Spawn.EntityRef;
          }))
        continue;
      FCrowdBehaviorCapabilityBindingUpdate& Update =
        InitialBindingUpdates.AddDefaulted_GetRef();
      Update.EffectiveFixedStep = SimulationTick;
      Update.EntityRef = Spawn.EntityRef;
      Update.Binding.ProfileKey =
        CrowdDemoBehaviorSchemas::FullProfile;
      Update.RecalculateStableHash();
      if (!Update.IsValid()) return false;
    }
    InitialBindingUpdates.Sort([](
      const FCrowdBehaviorCapabilityBindingUpdate& A,
      const FCrowdBehaviorCapabilityBindingUpdate& B)
    {
      return A.EntityRef < B.EntityRef;
    });
    BehaviorBindingUpdates = InitialBindingUpdates;
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
    Shadow.SubmitAutonomousFrame(
      Runtime, SimulationTick, PlanRevision,
      TargetSimulationTimeSeconds, BehaviorCommands,
      BehaviorContexts, VersionedResources,
      BehaviorBindingUpdates, CombinedSpawns, CombinedDespawns,
      CombinedExternalInputs, ObjectiveRevisions);
  if (SubmitResult != ECrowdWorkerShadowSubmitResult::Accepted)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoWorkerIntentSubmitRejected result=%u reason=%u tick=%d target_time=%.6f"),
      static_cast<uint32>(SubmitResult),
      static_cast<uint32>(Shadow.GetMetrics().LastSubmitFailure),
      SimulationTick, TargetSimulationTimeSeconds);
    LogSubmitFailure(TEXT("intent_submit_rejected"), Shadow, Runtime);
    return false;
  }
  for (const FCrowdWorkerVersionedResourceInput& Resource :
    VersionedResources)
  {
    if (!RuntimeSubsystem->AcknowledgeWorkerResourceRevision(
        Resource.ResourceId, Resource.Revision))
      return false;
  }
  if (MassSubsystem
    && !MassSubsystem->AcknowledgeWorkerLifecycleProfileJournal(
      JournalSpawns.Num(), JournalDespawns.Num(),
      JournalProfileRevisions.Num()))
    return false;
  if (bLifecycleChanged
    && (!ResultApplyProxy.UpdateCurrentEntities(
          Shadow.GetGeneration(), CandidateEntityRefs)
      || !RuntimeSubsystem->GetWorkerMovementAuthority().
        UpdateCurrentEntities(
          Shadow.GetGeneration(), CandidateEntityRefs)
      || !RuntimeSubsystem->GetWorkerBehaviorAuthority().
        UpdateCurrentEntities(
          Shadow.GetGeneration(), CandidateEntityRefs)))
    return false;
  const FCrowdWorkerShadowSyncMetrics& IntentMetrics =
    Shadow.GetMetrics();
  int32 SubmittedProfileRevisionCount = 0;
  for (const FCrowdWorkerExternalGameplayInput& Input :
    CombinedExternalInputs)
  {
    if (Input.InputTypeId == static_cast<uint16>(
        ECrowdWorkerExternalGameplayInputType::MovementProfileRevision))
      ++SubmittedProfileRevisionCount;
  }
  if (IntentMetrics.SubmittedSnapshotCount <= 2
    || IntentMetrics.SubmittedSnapshotCount % 300 == 0
    || !CombinedSpawns.IsEmpty() || !CombinedDespawns.IsEmpty()
    || !CombinedExternalInputs.IsEmpty())
  {
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerIntentCheckpoint submitted=%llu input_sequence=%llu simulation_tick=%d resources=%d spawns=%d despawns=%d journal_profiles=%d commands=%llu contexts=%llu bindings=%llu source=WorkerInputSync"),
      IntentMetrics.SubmittedSnapshotCount,
      IntentMetrics.LastSubmittedInputSequence,
      SimulationTick, VersionedResources.Num(),
      CombinedSpawns.Num(), CombinedDespawns.Num(),
      SubmittedProfileRevisionCount,
      IntentMetrics.LastSubmittedCommandRecordCount,
      static_cast<uint64>(BehaviorContexts.Num()),
      IntentMetrics.LastSubmittedBindingRecordCount);
  }

  const int32 SubmittedCommandCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedCommandRecordCount);
  const int32 SubmittedBindingCount = static_cast<int32>(
    Shadow.GetMetrics().LastSubmittedBindingRecordCount);
  if (bBehaviorProduction)
  {
    const bool bExpectationQueued =
      BehaviorAuthority.QueueAutonomousExpectation(
        Shadow.GetGeneration(),
        Shadow.GetMetrics().LastSubmittedInputSequence,
        CandidateEntityRefs,
        bHasStagedBehavior);
    if (!bExpectationQueued
      || !BehaviorAuthority.MarkSubmittedBindings(
        Shadow.GetGeneration(), BehaviorBindingUpdates))
      return false;
  }
  else
  {
    if (!BehaviorAuthority.QueueCommittedExpectation(
        Shadow.GetGeneration(),
        Shadow.GetMetrics().LastSubmittedInputSequence,
        BehaviorRuntime, BehaviorContexts,
        false, false)
      || !BehaviorRuntime.AcknowledgeWorkerInputCommands(
        SubmittedCommandCount)
      || !BehaviorRuntime.AcknowledgeWorkerInputContexts(
        BehaviorContexts.Num())
      || !BehaviorRuntime.AcknowledgeWorkerInputBindings(
        SubmittedBindingCount)
      || !BehaviorAuthority.MarkSubmittedBindings(
        Shadow.GetGeneration(), BehaviorBindingUpdates))
      return false;
  }

  const ECrowdWorkerShadowCompareResult AfterSubmit =
    Shadow.PollAndCompare(Runtime);
  LogMatch(Shadow, AfterSubmit);
  if (AfterSubmit == ECrowdWorkerShadowCompareResult::Violation)
    LogSubmitFailure(TEXT("intent_after_submit"), Shadow, Runtime);
  return AfterSubmit != ECrowdWorkerShadowCompareResult::Violation;
}

bool FCrowdDemoWorkerInputSync::StartClientFromNetworkCheckpoint(
  UWorld& World,
  const FCrowdWorkerNetworkCheckpoint& Checkpoint)
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
  if (!BuildConfigFromCheckpoint(Checkpoint, Config)
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
  const uint64 ApplyStartCycles = FPlatformTime::Cycles64();
  FCrowdWorkerPreparedResultApply Prepared;
  bool bHasBatch = false;
  if (!PreparePublishedResults(
      World, ConsumerFrameSequence, Prepared, bHasBatch))
    return false;
  if (!bHasBatch)
    return true;
  if (!CommitPreparedResults(World, Prepared))
    return false;
  const double ApplyMs = FPlatformTime::ToMilliseconds64(
    FPlatformTime::Cycles64() - ApplyStartCycles);
  return FinalizeCommittedResults(
    World, Prepared, ApplyMs, false);
}

bool FCrowdDemoWorkerInputSync::PreparePublishedResults(
  UWorld& World,
  const uint64 ConsumerFrameSequence,
  FCrowdWorkerPreparedResultApply& OutPrepared,
  bool& bOutHasBatch)
{
  check(IsInGameThread());
  OutPrepared.Reset();
  bOutHasBatch = false;
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
        TEXT("CrowdDemoWorkerResultApplyNoBatch consumer_frame=%llu exchange_result=%u runtime_state=%u runtime_v2_failure=%u epoch=%llu accepted_input=%llu applied_input=%llu published_dirty=%llu last_published_patches=%d work=%d/%d inflight=%d source=WorkerResultApply"),
        ConsumerFrameSequence,
        static_cast<uint32>(ExchangeResult),
        static_cast<uint32>(Runtime.GetState()),
        static_cast<uint32>(Metrics.WorkerV2.LastFailure),
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
  const ECrowdWorkerResultApplyResult ApplyResult =
    Proxy.Prepare(*Batch, OutPrepared);
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
  bOutHasBatch = true;
  return true;
}

bool FCrowdDemoWorkerInputSync::CommitPreparedResults(
  UWorld& World,
  const FCrowdWorkerPreparedResultApply& Prepared)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem || !Prepared.IsValid())
    return false;
  FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const ECrowdWorkerResultApplyResult ApplyResult =
    Proxy.CommitPrepared(Prepared);
  if (ApplyResult == ECrowdWorkerResultApplyResult::Applied
    || ApplyResult == ECrowdWorkerResultApplyResult::AppliedEmpty)
    return true;
  const FCrowdWorkerResultApplyMetrics& FailureMetrics =
    Proxy.GetMetrics();
  UE_LOG(LogTemp, Error,
    TEXT("CrowdDemoWorkerResultCommitReject result=%u generation=%llu publish=%llu previous_publish=%llu violation=%d"),
    static_cast<uint32>(ApplyResult), Prepared.Batch.Generation,
    Prepared.Batch.PublishSequence,
    FailureMetrics.LastConsumedPublishSequence,
    FailureMetrics.bViolation ? 1 : 0);
  return false;
}

bool FCrowdDemoWorkerInputSync::PrepareCommittedResultSideEffects(
  UWorld& World,
  const FCrowdWorkerPreparedResultApply& Prepared,
  FCrowdDemoPreparedWorkerResultSideEffects& OutPrepared)
{
  check(IsInGameThread());
  OutPrepared = {};
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem || !Prepared.IsValid()
    || World.GetNetMode() == NM_Client)
    return false;

  FCrowdWorkerResultApplyProxy ProxyPreview =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  if (ProxyPreview.ValidatePreparedState(Prepared)
      != ECrowdWorkerResultApplyResult::Applied)
    return false;
  ProxyPreview.CommitPreparedValidated(Prepared);

  FCrowdWorkerBehaviorAuthority BehaviorPreview =
    RuntimeSubsystem->GetWorkerBehaviorAuthority();
  if (!BehaviorPreview.IngestOrderedEvents(
      Prepared.Batch.OrderedEvents))
    return false;
  for (int32 Index = 0; Index < MaxQueuedShadowBatches; ++Index)
  {
    const ECrowdWorkerBehaviorValidationResult Validation =
      BehaviorPreview.ValidateAvailable(ProxyPreview);
    if (Validation
        == ECrowdWorkerBehaviorValidationResult::Violation)
      return false;
    if (Validation
        != ECrowdWorkerBehaviorValidationResult::Matched)
      break;
  }

  OutPrepared.BehaviorAuthority = MoveTemp(BehaviorPreview);
  OutPrepared.PublishSequence = Prepared.Batch.PublishSequence;
  OutPrepared.bValid = true;
  return true;
}

void FCrowdDemoWorkerInputSync::
CommitPreparedResultSideEffectsNoFail(
  UWorld& World,
  const FCrowdWorkerPreparedResultApply& Prepared,
  FCrowdDemoPreparedWorkerResultSideEffects& PreparedSideEffects,
  const double ApplyMilliseconds)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  checkf(RuntimeSubsystem && Prepared.IsValid()
      && PreparedSideEffects.bValid
      && PreparedSideEffects.PublishSequence
        == Prepared.Batch.PublishSequence,
    TEXT("Worker result side effects escaped prepared owner barrier"));

  RuntimeSubsystem->GetWorkerBehaviorAuthority() =
    MoveTemp(PreparedSideEffects.BehaviorAuthority);
  PreparedSideEffects.bValid = false;

  const FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const FCrowdWorkerResultApplyMetrics& Metrics = Proxy.GetMetrics();
  if (Metrics.AppliedBatchCount <= 2
    || Metrics.AppliedBatchCount % 300 == 0)
  {
    const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
      RuntimeSubsystem->GetAsyncSimulationRuntime().GetMetrics();
    int32 BehaviorPatchCount = 0;
    for (const FCrowdWorkerStatePatch& Patch :
      Prepared.Batch.StatePatches)
    {
      BehaviorPatchCount += Patch.StateFieldId
          == static_cast<uint16>(ECrowdWorkerField::Behavior)
        ? 1 : 0;
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerResultApplyCheckpoint batches=%llu empty=%llu patches=%llu behavior_patches=%d stale_lifecycle=%llu events=%llu publish_sequence=%llu input_sequence=%llu proxies=%d input_queue_depth=%d oldest_input_age_ms=%.3f simulation_lag_ms=%.3f mirror_entities=%d scan_coverage_ms=%.3f owner_pump_ms=%.3f task_queue_ms=%.3f task_run_ms=%.3f task_critical_ms=%.3f publish_to_consume_ms=%.3f gt_apply_ms=%.3f published_patch_count=%d ordered_event_depth=%d resnapshots=%llu"),
      Metrics.AppliedBatchCount,
      Metrics.AppliedEmptyBatchCount,
      Metrics.AppliedPatchCount,
      BehaviorPatchCount,
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
      ApplyMilliseconds,
      RuntimeMetrics.LastPublishedPatchCount,
      RuntimeMetrics.LastPublishedEventCount,
      RuntimeMetrics.ResnapshotCount);
  }
}

bool FCrowdDemoWorkerInputSync::FinalizeCommittedResults(
  UWorld& World,
  const FCrowdWorkerPreparedResultApply& Prepared,
  const double ApplyMilliseconds,
  const bool bAcknowledgeDirtyBatch)
{
  check(IsInGameThread());
  UMassCrowdRuntimeSubsystem* RuntimeSubsystem =
    World.GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!RuntimeSubsystem || !Prepared.IsValid())
    return false;
  FCrowdAsyncSimulationRuntime& Runtime =
    RuntimeSubsystem->GetAsyncSimulationRuntime();
  FCrowdWorkerResultApplyProxy& Proxy =
    RuntimeSubsystem->GetWorkerResultApplyProxy();
  const FCrowdWorkerPublishedBatch& Batch = Prepared.Batch;
  if (World.GetNetMode() == NM_Client)
  {
    if (!bAcknowledgeDirtyBatch)
      return true;
    const FCrowdWorkerResultApplyDirtyBatch* DirtyBatch =
      Proxy.PeekDirtyBatch();
    return (DirtyBatch
        && DirtyBatch->PublishSequence == Batch.PublishSequence
        && Proxy.AcknowledgeDirtyBatch(Batch.PublishSequence))
      || Proxy.GetMetrics().LastConsumedDirtyPublishSequence
        >= Batch.PublishSequence;
  }
  FCrowdWorkerBehaviorAuthority& BehaviorAuthority =
    RuntimeSubsystem->GetWorkerBehaviorAuthority();
  if (!BehaviorAuthority.IngestOrderedEvents(Batch.OrderedEvents))
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
  if (Metrics.AppliedBatchCount <= 2
    || Metrics.AppliedBatchCount % 300 == 0)
  {
    const FCrowdAsyncSimulationRuntimeMetrics RuntimeMetrics =
      Runtime.GetMetrics();
    int32 BehaviorPatchCount = 0;
    for (const FCrowdWorkerStatePatch& Patch : Batch.StatePatches)
      BehaviorPatchCount += Patch.StateFieldId
          == static_cast<uint16>(ECrowdWorkerField::Behavior)
        ? 1 : 0;
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoWorkerResultApplyCheckpoint batches=%llu empty=%llu patches=%llu behavior_patches=%d stale_lifecycle=%llu events=%llu publish_sequence=%llu input_sequence=%llu proxies=%d input_queue_depth=%d oldest_input_age_ms=%.3f simulation_lag_ms=%.3f mirror_entities=%d scan_coverage_ms=%.3f owner_pump_ms=%.3f task_queue_ms=%.3f task_run_ms=%.3f task_critical_ms=%.3f publish_to_consume_ms=%.3f gt_apply_ms=%.3f published_patch_count=%d ordered_event_depth=%d resnapshots=%llu"),
      Metrics.AppliedBatchCount,
      Metrics.AppliedEmptyBatchCount,
      Metrics.AppliedPatchCount,
      BehaviorPatchCount,
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
      ApplyMilliseconds,
      RuntimeMetrics.LastPublishedPatchCount,
      RuntimeMetrics.LastPublishedEventCount,
      RuntimeMetrics.ResnapshotCount);
  }
  if (bAcknowledgeDirtyBatch)
  {
    const FCrowdWorkerResultApplyDirtyBatch* DirtyBatch =
      Proxy.PeekDirtyBatch();
    if (!((DirtyBatch
          && DirtyBatch->PublishSequence == Batch.PublishSequence
          && Proxy.AcknowledgeDirtyBatch(Batch.PublishSequence))
        || Proxy.GetMetrics().LastConsumedDirtyPublishSequence
          >= Batch.PublishSequence))
      return false;
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
