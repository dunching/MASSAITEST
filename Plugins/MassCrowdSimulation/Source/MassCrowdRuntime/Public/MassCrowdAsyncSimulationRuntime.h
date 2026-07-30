#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "MassCrowdWorkerContracts.h"
#include "MassCrowdWorkerExchange.h"

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

enum class ECrowdAsyncSimulationPollResult : uint8
{
  Idle = 0,
  Working,
  StateChanged,
  Stopped,
  Failed
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

  bool IsValid() const
  {
    return ContractLimits.IsValid()
      && FMath::IsFinite(FixedSimulationQuantumSeconds)
      && FixedSimulationQuantumSeconds > 0.0
      && MaxQueuedInputBatches > 0
      && MaxInputBatchesPerPump > 0
      && MaxSimulationStepsPerPump > 0
      && MaxPendingCommands > 0
      && MaxInFlightShadowWorks > 0;
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
  uint64 LastAppliedInputSequence = 0;
  uint64 OwnerPumpCount = 0;
  uint64 ResnapshotCount = 0;
  uint64 RejectedInputCount = 0;
  uint64 SubmittedShadowWorkCount = 0;
  uint64 CompletedShadowWorkCount = 0;
  uint64 SubmittedProductionWorkCount = 0;
  uint64 CompletedProductionWorkCount = 0;
  uint64 ShadowHashMismatchCount = 0;
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
  int32 LastPublishedPatchCount = 0;
  int32 LastPublishedEventCount = 0;
  bool bRequiresResnapshot = false;
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

  ECrowdAsyncSimulationSubmitResult SubmitResnapshot(
    const FCrowdWorkerInputBatch& Batch);
  ECrowdAsyncSimulationSubmitResult SubmitInput(
    const FCrowdWorkerInputBatch& Batch);
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
  FCrowdAsyncSimulationRuntimeMetrics GetMetrics() const;

  ECrowdWorkerExchangeResult TryExchangePublishedBatch(
    uint64 ExpectedGeneration,
    uint64 ConsumerFrameSequence,
    const FCrowdWorkerPublishedBatch*& OutBatch);

private:
  struct FSharedState;

  bool QueueInput(
    const FCrowdWorkerInputBatch& Batch,
    bool bResnapshot,
    ECrowdAsyncSimulationSubmitResult& OutResult);
  void LaunchOwnerPump();
  void CompleteInvalidation();
  void CompleteStop();

  TSharedPtr<FSharedState, ESPMode::ThreadSafe> SharedState;
  UE::Tasks::FTask OwnerTask;
  bool bOwnerTaskActive = false;
};
