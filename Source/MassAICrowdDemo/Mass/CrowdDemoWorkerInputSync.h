#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdWorkerShadowSync.h"

class UWorld;

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
    bool bAutonomousAfterBootstrap = false);

  static bool Poll(UWorld& World);

  static bool StartClientFromNetworkCheckpoint(
    UWorld& World,
    const FCrowdWorkerNetworkCheckpoint& Checkpoint,
    double FixedSimulationQuantumSeconds = 1.0 / 30.0);

  static bool ConsumePublishedResults(
    UWorld& World,
    uint64 ConsumerFrameSequence);

  static bool SubmitShadowWork(
    UWorld& World,
    EShadowKernel Kernel,
    uint64 WorkSequence,
    uint64 ExpectedStableHash,
    TFunction<uint64()>&& Execute,
    bool bRequireExpectedStableHash = true);
};
