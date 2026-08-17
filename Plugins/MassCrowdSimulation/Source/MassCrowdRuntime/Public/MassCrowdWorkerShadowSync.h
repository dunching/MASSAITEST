#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAsyncSimulationRuntime.h"
#include "MassCrowdBehaviorSource.h"
#include "MassCrowdBehaviorSourceRuntime.h"
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

struct MASSCROWDRUNTIME_API FCrowdWorkerVersionedResourceInput
{
  uint64 ResourceId = 0;
  uint64 Revision = 0;
  FCrowdWorkerPayload Payload;

  bool IsValid(int32 MaxPayloadBytes) const
  {
    return ResourceId != 0 && Revision != 0
      && Payload.IsValid(MaxPayloadBytes);
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

enum class ECrowdWorkerShadowSubmitFailure : uint8
{
  None = 0,
  InvalidState,
  InvalidTime,
  SnapshotEncoding,
  BindingEncoding,
  CommandEncoding,
  InvalidContext,
  ContextEncoding,
  InvalidVersionedResource,
  DuplicateVersionedResource,
  ResourceRevisionRegression,
  ConflictingResourceRevision,
  InvalidBatch,
  ExpectationCapacity,
  RuntimeCapacity,
  RuntimeRequiresResnapshot,
  RuntimeViolation
};

enum class ECrowdWorkerShadowCompareResult : uint8
{
  NoProgress = 0,
  Working,
  Match,
  Violation
};

struct MASSCROWDRUNTIME_API FCrowdWorkerBoundaryKinematicState
{
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
  int32 PlanRevision = INDEX_NONE;
  float PhysicalRadiusCm = 0.0f;
  float HardSafetyGapCm = 0.0f;
  float SoftMarginCm = 0.0f;
  float Mobility = 0.0f;
  float MaximumSpeedCmps = 0.0f;
  uint32 CapabilityProfileKey = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerShadowSyncMetrics
{
  uint64 Generation = 0;
  uint64 SubmittedSnapshotCount = 0;
  uint64 SubmittedInputRecordCount = 0;
  uint64 SubmittedCommandRecordCount = 0;
  uint64 LastSubmittedCommandRecordCount = 0;
  uint64 LastSubmittedBindingRecordCount = 0;
  uint64 FullResnapshotCount = 0;
  uint64 FullStateSerializationCount = 0;
  uint64 SnapshotResourceSerializationCount = 0;
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
  ECrowdWorkerShadowSubmitFailure LastSubmitFailure =
    ECrowdWorkerShadowSubmitFailure::None;
  uint64 LastRejectedResourceId = 0;
  uint64 LastRejectedPreviousRevision = 0;
  uint64 LastRejectedSubmittedRevision = 0;
  uint64 LastRejectedPreviousPayloadHash = 0;
  uint64 LastRejectedSubmittedPayloadHash = 0;
  bool bViolation = false;
};

class MASSCROWDRUNTIME_API FCrowdWorkerBoundaryStateCodec
{
public:
  static constexpr uint32 StateSchemaId = 0x43574253u;
  static constexpr uint16 StateSchemaVersion = 1;
  static constexpr int32 EncodedStateByteCount = 136;
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
  static bool DecodeSnapshotResource(
    const FCrowdWorkerPayload& Payload,
    int32& OutFixedStepIndex,
    int32& OutPlanRevision);

  static bool EncodeBehaviorCommand(
    const FCrowdBehaviorSourceCommand& Command,
    FCrowdWorkerPayload& OutPayload);
  static bool DecodeBehaviorCommand(
    const FCrowdWorkerPayload& Payload,
    FCrowdBehaviorSourceCommand& OutCommand);

  static bool DecodeKinematicState(
    const FCrowdWorkerPayload& Payload,
    FCrowdWorkerBoundaryKinematicState& OutState);

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
  bool StartFromNetworkCheckpoint(
    FCrowdAsyncSimulationRuntime& Runtime,
    const FCrowdWorkerShadowSyncConfig& Config,
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);

  ECrowdWorkerShadowSubmitResult SubmitSnapshot(
    FCrowdAsyncSimulationRuntime& Runtime,
    const FCrowdMassBoundarySnapshot& Snapshot,
    double TargetSimulationTimeSeconds,
    TConstArrayView<FCrowdBehaviorSourceCommand>
      PendingBehaviorCommands = {},
    TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
      BehaviorContexts = {},
    TConstArrayView<FCrowdWorkerVersionedResourceInput>
      VersionedResources = {},
    TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
      PendingBehaviorBindingUpdates = {},
    TConstArrayView<FCrowdWorkerExternalGameplayInput>
      ExternalGameplayInputs = {});

  // Advances an already bootstrapped Worker without re-reading or echoing
  // entity simulation state. The cached bootstrap facts remain immutable;
  // only external commands, resources and the simulation target advance.
  ECrowdWorkerShadowSubmitResult SubmitAutonomousFrame(
    FCrowdAsyncSimulationRuntime& Runtime,
    int32 FixedStepIndex,
    int32 PlanRevision,
    double TargetSimulationTimeSeconds,
    TConstArrayView<FCrowdBehaviorSourceCommand>
      PendingBehaviorCommands = {},
    TConstArrayView<FCrowdBehaviorEntityEvaluationContext>
      BehaviorContexts = {},
    TConstArrayView<FCrowdWorkerVersionedResourceInput>
      VersionedResources = {},
    TConstArrayView<FCrowdBehaviorCapabilityBindingUpdate>
      PendingBehaviorBindingUpdates = {},
    TConstArrayView<FCrowdWorkerSpawnDelta> Spawns = {},
    TConstArrayView<FCrowdWorkerDespawnDelta> Despawns = {},
    TConstArrayView<FCrowdWorkerExternalGameplayInput>
      ExternalGameplayInputs = {},
    TConstArrayView<FCrowdWorkerObjectiveRevisionDelta>
      ObjectiveRevisions = {});

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
  uint64 ResolveInputSequenceForSnapshotHash(
    uint64 SourceSnapshotHash) const;

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
  FCrowdMassBoundarySnapshot PreviousSourceSnapshot;
  TMap<uint64, FCrowdWorkerResourceRecord>
    PreviousVersionedResources;
  TArray<FExpectation> Expectations;
  FCrowdWorkerShadowSyncMetrics Metrics;
  uint64 Generation = 0;
  uint64 NextInputSequence = 1;
  uint64 SnapshotResourceRevision = 0;
  uint64 LastSnapshotResourcePayloadHash = 0;
  bool bStarted = false;
  bool bSubmittedResnapshot = false;
};
