#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerShadowSync.h"

class UWorld;

struct FCrowdDemoPreparedWorkerResultSideEffects
{
  FCrowdWorkerBehaviorAuthority BehaviorAuthority;
  uint64 PublishSequence = 0;
  bool bValid = false;
};

class FCrowdDemoWorkerInputSync
{
public:
  enum class EShadowKernel : uint32
  {
    SharedFlow = 1,
    Facing = 2,
    Business = 3,
    Movement = 4
  };

  static bool SubmitBoundarySnapshot(
    UWorld& World,
    const FCrowdMassBoundarySnapshot& Snapshot,
    double FixedSimulationQuantumSeconds,
    double TargetSimulationTimeSeconds,
    TConstArrayView<FCrowdWorkerVersionedResourceInput>
      AdditionalResources = {},
    const FCrowdBehaviorPreparedBoundary* StagedBehavior = nullptr,
    TConstArrayView<FCrowdWorkerExternalGameplayInput>
      ExternalGameplayInputs = {},
    TConstArrayView<FCrowdBehaviorSourceCommand>
      WorkerBehaviorCommands = {});

  // Ordinary frames submit only ordered intent and resource revisions. The
  // complete Mass snapshot is legal exclusively on the bootstrap path above.
  static bool SubmitIntentBatch(
    UWorld& World,
    int32 SimulationTick,
    int32 PlanRevision,
    double TargetSimulationTimeSeconds,
    TConstArrayView<FCrowdWorkerVersionedResourceInput>
      ResourceRevisions = {},
    TConstArrayView<FCrowdWorkerSpawnDelta> Spawns = {},
    TConstArrayView<FCrowdWorkerDespawnDelta> Despawns = {},
    TConstArrayView<FCrowdWorkerExternalGameplayInput>
      ExternalGameplayInputs = {},
    const FCrowdBehaviorPreparedBoundary* StagedBehavior = nullptr,
    TConstArrayView<FCrowdWorkerObjectiveRevisionDelta>
      ObjectiveRevisions = {},
    TConstArrayView<FCrowdBehaviorSourceCommand>
      WorkerBehaviorCommands = {});

  static bool Poll(UWorld& World);

  static bool StartClientFromNetworkCheckpoint(
    UWorld& World,
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);

  static bool ConsumePublishedResults(
    UWorld& World,
    uint64 ConsumerFrameSequence);

  static bool PreparePublishedResults(
    UWorld& World,
    uint64 ConsumerFrameSequence,
    FCrowdWorkerPreparedResultApply& OutPrepared,
    bool& bOutHasBatch);

  static bool CommitPreparedResults(
    UWorld& World,
    const FCrowdWorkerPreparedResultApply& Prepared);

  static bool PrepareCommittedResultSideEffects(
    UWorld& World,
    const FCrowdWorkerPreparedResultApply& Prepared,
    FCrowdDemoPreparedWorkerResultSideEffects& OutPrepared);

  static void CommitPreparedResultSideEffectsNoFail(
    UWorld& World,
    const FCrowdWorkerPreparedResultApply& Prepared,
    FCrowdDemoPreparedWorkerResultSideEffects& PreparedSideEffects,
    double ApplyMilliseconds);

  static bool FinalizeCommittedResults(
    UWorld& World,
    const FCrowdWorkerPreparedResultApply& Prepared,
    double ApplyMilliseconds,
    bool bAcknowledgeDirtyBatch);

  static bool SubmitShadowWork(
    UWorld& World,
    EShadowKernel Kernel,
    uint64 WorkSequence,
    uint64 ExpectedStableHash,
    TFunction<uint64()>&& Execute,
    bool bRequireExpectedStableHash = true);
};
