#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAsyncSimulationRuntime.h"
#include "MassCrowdBehaviorSource.h"
#include "MassCrowdRuntimeBridge.h"

struct MASSCROWDRUNTIME_API FCrowdWorkerShadowSyncConfig
{
  FCrowdAsyncSimulationRuntimeConfig RuntimeConfig;
  int32 MaxPendingExpectations = 0;

  bool IsValid() const
  {
    return RuntimeConfig.IsValid() && MaxPendingExpectations > 0;
  }
};

enum class ECrowdWorkerShadowSubmitResult : uint8
{
  Accepted = 0,
  RejectedState,
  RejectedSnapshot,
  RejectedCapacity,
  RequiresResnapshot,
  Violation
};

enum class ECrowdWorkerShadowCompareResult : uint8
{
  NoProgress = 0,
  Working,
  Match,
  Violation
};

struct MASSCROWDRUNTIME_API FCrowdWorkerShadowSyncMetrics
{
  uint64 Generation = 0;
  uint64 SubmittedSnapshotCount = 0;
  uint64 SubmittedInputRecordCount = 0;
  uint64 SubmittedCommandRecordCount = 0;
  uint64 LastSubmittedCommandRecordCount = 0;
  uint64 FullResnapshotCount = 0;
  uint64 ComparedSnapshotCount = 0;
  uint64 SupersededExpectationCount = 0;
  uint64 LastSubmittedInputSequence = 0;
  uint64 LastComparedInputSequence = 0;
  uint64 LastSourceSnapshotHash = 0;
  uint64 LastComparedSourceSnapshotHash = 0;
  uint64 LastExpectedEntitySetHash = 0;
  uint64 LastObservedEntitySetHash = 0;
  uint64 LastExpectedStateHash = 0;
  uint64 LastObservedStateHash = 0;
  int32 PendingExpectationCount = 0;
  bool bViolation = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerBoundaryStateCodec
{
public:
  static constexpr uint32 StateSchemaId = 0x43574253u;
  static constexpr uint16 StateSchemaVersion = 1;
  static constexpr uint32 SnapshotResourceSchemaId = 0x43574252u;
  static constexpr uint16 SnapshotResourceSchemaVersion = 1;
  static constexpr uint64 SnapshotResourceId = 0x435744534E415001ull;
  static constexpr uint32 BehaviorCommandSchemaId = 0x43574243u;
  static constexpr uint16 BehaviorCommandSchemaVersion = 1;
  static constexpr int32 MaxEncodedBehaviorCommandBytes = 160;

  static bool EncodeState(
    const FCrowdMassBoundaryAgentRecord& Record,
    FCrowdWorkerPayload& OutPayload);

  static bool EncodeSnapshotResource(
    const FCrowdMassBoundarySnapshot& Snapshot,
    FCrowdWorkerPayload& OutPayload);

  static bool EncodeBehaviorCommand(
    const FCrowdBehaviorSourceCommand& Command,
    FCrowdWorkerPayload& OutPayload);

  static uint64 CalculateStateHash(
    TConstArrayView<FCrowdStableEntityRef> EntityRefs,
    TConstArrayView<uint64> PayloadHashes);
};

class MASSCROWDRUNTIME_API FCrowdWorkerBoundaryShadowSync
{
public:
  bool Start(
    FCrowdAsyncSimulationRuntime& Runtime,
    const FCrowdWorkerShadowSyncConfig& Config,
    uint64 Generation);

  ECrowdWorkerShadowSubmitResult SubmitSnapshot(
    FCrowdAsyncSimulationRuntime& Runtime,
    const FCrowdMassBoundarySnapshot& Snapshot,
    double TargetSimulationTimeSeconds,
    TConstArrayView<FCrowdBehaviorSourceCommand>
      PendingBehaviorCommands = {});

  ECrowdWorkerShadowCompareResult PollAndCompare(
    FCrowdAsyncSimulationRuntime& Runtime);

  bool ResetQuiescent();
  bool IsStarted() const { return bStarted; }
  bool HasViolation() const { return Metrics.bViolation; }
  uint64 GetGeneration() const { return Generation; }
  const FCrowdWorkerShadowSyncMetrics& GetMetrics() const
  {
    return Metrics;
  }

private:
  struct FEncodedAgent
  {
    FCrowdStableEntityRef EntityRef;
    FCrowdWorkerPayload Payload;
  };

  struct FExpectation
  {
    uint64 LastInputSequence = 0;
    uint64 SourceSnapshotHash = 0;
    uint64 EntitySetHash = 0;
    uint64 StateHash = 0;
    uint64 SnapshotResourcePayloadHash = 0;
    TArray<FCrowdStableEntityRef> EntityRefs;
  };

  bool EncodeSnapshot(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TArray<FEncodedAgent>& OutAgents,
    FCrowdWorkerPayload& OutSnapshotResource) const;
  void LatchViolation();

  FCrowdWorkerShadowSyncConfig Config;
  TArray<FEncodedAgent> PreviousAgents;
  TArray<FExpectation> Expectations;
  FCrowdWorkerShadowSyncMetrics Metrics;
  uint64 Generation = 0;
  uint64 NextInputSequence = 1;
  uint64 SnapshotResourceRevision = 0;
  bool bStarted = false;
  bool bSubmittedResnapshot = false;
};
