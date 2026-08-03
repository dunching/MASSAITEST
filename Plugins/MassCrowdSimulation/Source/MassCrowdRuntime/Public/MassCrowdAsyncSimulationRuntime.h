#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "MassCrowdWorkerContracts.h"
#include "MassCrowdWorkerExchange.h"
#include "MassCrowdWorkerNetworkState.h"
#include "MassCrowdWorkerRuntimeV2.h"

enum class ECrowdAsyncSimulationRuntimeState : uint8
{
  Stopped = 0,
  Starting,
  Running,
  Invalidating,
  Draining,
  Failed
};

enum class ECrowdAsyncSimulationSubmitResult : uint8
{
  Accepted = 0,
  RejectedState,
  RejectedGeneration,
  RejectedInvalidBatch,
  RejectedCapacity,
  RequiresResnapshot
};

enum class ECrowdAsyncSimulationInputFailure : uint8
{
  None = 0,
  State,
  Generation,
  InvalidPayload,
  Capacity,
  StaleSequence,
  SequenceGap,
  ConflictingDuplicate,
  TimeRegression,
  ResnapshotRequired,
  ApplyFailure
};

enum class ECrowdAsyncSimulationPollResult : uint8
{
  Idle = 0,
  Working,
  StateChanged,
  Stopped,
  Failed
};

enum class ECrowdAsyncSimulationRestoreResult : uint8
{
  Restored = 0,
  RejectedState,
  RejectedGeneration,
  RejectedCheckpoint,
  RejectedBusy,
  RestoreFailure
};

enum class ECrowdAsyncSimulationCorrectionResult : uint8
{
  Accepted = 0,
  Duplicate,
  RejectedState,
  RejectedGeneration,
  RejectedContract,
  RejectedSequence,
  RejectedCapacity,
  RequiresResnapshot
};

enum class ECrowdAsyncShadowWorkSubmitResult : uint8
{
  Accepted = 0,
  RejectedState,
  RejectedGeneration,
  RejectedInvalid,
  RejectedSequence,
  RejectedCapacity
};

struct MASSCROWDRUNTIME_API FCrowdAsyncShadowWorkSubmission
{
  uint64 Generation = 0;
  uint64 WorkSequence = 0;
  uint32 KernelId = 0;
  uint64 ExpectedStableHash = 0;
  bool bRequireExpectedStableHash = true;
  TFunction<uint64()> Execute;

  bool IsValid() const
  {
    return Generation > 0
      && WorkSequence > 0
      && KernelId > 0
      && (!bRequireExpectedStableHash
        || ExpectedStableHash > 0)
      && static_cast<bool>(Execute);
  }
};

struct MASSCROWDRUNTIME_API FCrowdAsyncShadowWorkResult
{
  uint64 Generation = 0;
  uint64 WorkSequence = 0;
  uint64 SubmissionOrdinal = 0;
  uint32 KernelId = 0;
  uint64 ExpectedStableHash = 0;
  uint64 ActualStableHash = 0;
  bool bRequiredExpectedStableHash = true;
  bool bSucceeded = false;
  bool bHashMatch = false;
};

struct MASSCROWDRUNTIME_API FCrowdAsyncSimulationRuntimeConfig
{
  FCrowdWorkerContractLimits ContractLimits;
  double FixedSimulationQuantumSeconds = 0.0;
  int32 MaxQueuedInputBatches = 0;
  int32 MaxInputBatchesPerPump = 0;
  int32 MaxSimulationStepsPerPump = 0;
  int32 MaxPendingCommands = 0;
  int32 MaxInFlightShadowWorks = 0;
  int32 MaxRetainedIntentBatches = 512;
  int32 NetworkPublishIntervalEpochs = 300;
  FCrowdWorkerRuntimeV2Config WorkerV2;
  FCrowdWorkerNetworkStateConfig NetworkState;

  bool IsValid() const
  {
    return ContractLimits.IsValid()
      && FMath::IsFinite(FixedSimulationQuantumSeconds)
      && FixedSimulationQuantumSeconds > 0.0
      && MaxQueuedInputBatches > 0
      && MaxInputBatchesPerPump > 0
      && MaxSimulationStepsPerPump > 0
      && MaxPendingCommands > 0
      && MaxInFlightShadowWorks > 0
      && MaxRetainedIntentBatches > 0
      && NetworkPublishIntervalEpochs > 0
      && WorkerV2.IsValid()
      && NetworkState.IsValid();
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerMirrorSnapshot
{
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 LastAppliedInputSequence = 0;
  double SimulationTimeSeconds = 0.0;
  double TargetSimulationTimeSeconds = 0.0;
  TArray<FCrowdStableEntityRef> EntityRefs;
  TArray<FCrowdWorkerPublishedState> States;
  TArray<uint64> LastStateInputSequences;
  TArray<uint64> CorrectionRevisions;
  TArray<uint64> ResourceIds;
  TArray<uint64> ResourceRevisions;
  TArray<uint64> ResourcePayloadHashes;
  uint64 EntitySetHash = 0;
  uint64 ResourceHash = 0;
  uint64 StableHash = 0;
  bool bValid = false;
};

struct MASSCROWDRUNTIME_API FCrowdAsyncSimulationRuntimeMetrics
{
  uint64 Generation = 0;
  uint64 WorkerEpoch = 0;
  uint64 LastAcceptedInputSequence = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 QueuedInputSequenceWatermark = 0;
  uint64 OwnerPumpCount = 0;
  uint64 ResnapshotCount = 0;
  uint64 RejectedInputCount = 0;
  uint64 SubmittedShadowWorkCount = 0;
  uint64 CompletedShadowWorkCount = 0;
  uint64 SubmittedProductionWorkCount = 0;
  uint64 CompletedProductionWorkCount = 0;
  uint64 ShadowHashMismatchCount = 0;
  uint64 FullMirrorSerializationCount = 0;
  uint64 AuthorityDigestCount = 0;
  uint64 AuthorityCorrectionCount = 0;
  uint64 AuthorityCorrectionEntityCount = 0;
  uint64 AuthorityCorrectionScopeCount = 0;
  uint64 ConsecutivePredictionEpochsWithoutCorrection = 0;
  uint64 MaxPredictionEpochsWithoutCorrection = 0;
  int32 InputQueueDepth = 0;
  int32 InFlightShadowWorkCount = 0;
  int32 MirrorEntityCount = 0;
  double SimulationTimeSeconds = 0.0;
  double TargetSimulationTimeSeconds = 0.0;
  double OldestInputAgeMs = 0.0;
  double SimulationLagMs = 0.0;
  double LastOwnerPumpMs = 0.0;
  double MaxOwnerPumpMs = 0.0;
  double LastScanCoverageMs = 0.0;
  double MaxScanCoverageMs = 0.0;
  double LastTaskQueueMs = 0.0;
  double LastTaskRunMs = 0.0;
  double MaxTaskCriticalMs = 0.0;
  double LastPublishToConsumeMs = 0.0;
  double MaxPublishToConsumeMs = 0.0;
  double LastCorrectionBeforePositionErrorCm = 0.0;
  double LastCorrectionAfterPositionErrorCm = 0.0;
  double LastCorrectionBeforeVelocityErrorCmps = 0.0;
  double LastCorrectionAfterVelocityErrorCmps = 0.0;
  double LastCorrectionBeforeYawErrorDegrees = 0.0;
  double LastCorrectionAfterYawErrorDegrees = 0.0;
  int32 LastCorrectionBeforeCombatMismatchCount = 0;
  int32 LastCorrectionAfterCombatMismatchCount = 0;
  int32 LastCorrectionEntityCount = 0;
  int32 LastCorrectionScopeCount = 0;
  int32 LastPublishedPatchCount = 0;
  int32 LastPublishedEventCount = 0;
  ECrowdAsyncSimulationInputFailure LastInputFailure =
    ECrowdAsyncSimulationInputFailure::None;
  bool bRequiresResnapshot = false;
  FCrowdWorkerRuntimeV2Metrics WorkerV2;
  FCrowdWorkerNetworkStateMetrics NetworkState;
};

class MASSCROWDRUNTIME_API FCrowdAsyncSimulationRuntime
{
public:
  FCrowdAsyncSimulationRuntime();
  ~FCrowdAsyncSimulationRuntime();

  FCrowdAsyncSimulationRuntime(
    const FCrowdAsyncSimulationRuntime&) = delete;
  FCrowdAsyncSimulationRuntime& operator=(
    const FCrowdAsyncSimulationRuntime&) = delete;

  bool Start(
    const FCrowdAsyncSimulationRuntimeConfig& Config,
    uint64 InitialGeneration);
  bool RegisterDomainExecutor(
    TUniquePtr<ICrowdWorkerDomainExecutor> Executor);

  ECrowdAsyncSimulationSubmitResult SubmitResnapshot(
    const FCrowdWorkerIntentBatch& Batch);
  ECrowdAsyncSimulationSubmitResult SubmitIntentBatch(
    const FCrowdWorkerIntentBatch& Batch);
  ECrowdAsyncSimulationRestoreResult RestoreNetworkCheckpoint(
    const FCrowdWorkerNetworkCheckpoint& Checkpoint);
  ECrowdAsyncShadowWorkSubmitResult SubmitShadowWork(
    FCrowdAsyncShadowWorkSubmission&& Submission);
  int32 CollectCompletedShadowWork(
    TArray<FCrowdAsyncShadowWorkResult>& OutResults);

  ECrowdAsyncSimulationPollResult Poll();
  bool Invalidate(uint64 NewGeneration);
  bool BeginStop();
  bool StopAndDrain(double TimeoutSeconds);

  ECrowdAsyncSimulationRuntimeState GetState() const;
  uint64 GetGeneration() const;
  bool RequiresResnapshot() const;
  bool ReadMirrorSnapshot(FCrowdWorkerMirrorSnapshot& OutSnapshot) const;
  ECrowdWorkerNetworkReadResult ReadNetworkCheckpoint(
    uint64 ExpectedGeneration,
    FCrowdWorkerNetworkCheckpoint& OutCheckpoint) const;
  ECrowdWorkerNetworkReadResult ReadNetworkIntents(
    uint64 ExpectedGeneration,
    uint64 AfterInputSequence,
    TArray<FCrowdWorkerIntentBatch>& OutBatches) const;
  ECrowdWorkerNetworkReadResult ReadAuthorityDigest(
    uint64 ExpectedGeneration,
    FCrowdWorkerAuthorityDigestBatch& OutDigest) const;
  ECrowdWorkerNetworkReadResult CompareAuthorityDigest(
    const FCrowdWorkerAuthorityDigestBatch& AuthorityDigest,
    TArray<FCrowdWorkerAuthorityScopeKey>& OutMismatchedScopes) const;
  ECrowdWorkerNetworkReadResult BuildAuthorityCorrection(
    uint64 ExpectedGeneration,
    uint64 AuthorityDigestSequence,
    uint64 CorrectionSequence,
    TConstArrayView<FCrowdWorkerAuthorityScopeKey> Scopes,
    FCrowdWorkerAuthorityCorrectionBatch& OutCorrection) const;
  bool BeginAuthorityCorrectionBarrier(
    uint64 ExpectedGeneration,
    uint64 ApplySimulationTick,
    uint64 ThroughInputSequence);
  ECrowdAsyncSimulationCorrectionResult SubmitAuthorityCorrection(
    const FCrowdWorkerAuthorityCorrectionBatch& Correction);
#if WITH_DEV_AUTOMATION_TESTS
  // Automation-only fault injection. The mutation is applied at the next
  // authority-digest barrier so the normal digest/correction path observes it.
  bool QueueDiagnosticMovementCorruption(
    uint64 ExpectedGeneration,
    const FCrowdStableEntityRef& EntityRef,
    const FVector& PositionOffset,
    const FVector& VelocityOffset,
    float YawOffsetDegrees);
#endif
  FCrowdAsyncSimulationRuntimeMetrics GetMetrics() const;

  ECrowdWorkerExchangeResult TryExchangePublishedBatch(
    uint64 ExpectedGeneration,
    uint64 ConsumerFrameSequence,
    const FCrowdWorkerPublishedBatch*& OutBatch);

private:
  struct FSharedState;

  bool QueueInput(
    const FCrowdWorkerIntentBatch& Batch,
    bool bResnapshot,
    ECrowdAsyncSimulationSubmitResult& OutResult);
  void LaunchOwnerPump();
  void CompleteInvalidation();
  void CompleteStop();

  TSharedPtr<FSharedState, ESPMode::ThreadSafe> SharedState;
  TUniquePtr<FCrowdWorkerDomainRegistry> PendingDomainRegistry;
  UE::Tasks::FTask OwnerTask;
  bool bOwnerTaskActive = false;
};
