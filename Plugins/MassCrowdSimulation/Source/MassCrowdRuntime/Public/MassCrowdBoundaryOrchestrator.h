#pragma once

#include "CoreMinimal.h"
#include "MassCrowdRuntimeBridge.h"
#include "MassEntityHandle.h"
#include "Tasks/Task.h"
#include "Templates/Function.h"

struct FCrowdBoundaryStageId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryStageId&) const = default;
  auto operator<=>(const FCrowdBoundaryStageId&) const = default;
  friend uint32 GetTypeHash(const FCrowdBoundaryStageId Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdBoundaryTaskTypeId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryTaskTypeId&) const = default;
  auto operator<=>(const FCrowdBoundaryTaskTypeId&) const = default;
  friend uint32 GetTypeHash(const FCrowdBoundaryTaskTypeId Id)
  {
    return ::GetTypeHash(Id.Value);
  }
};

struct FCrowdBoundaryResourceId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryResourceId&) const = default;
  auto operator<=>(const FCrowdBoundaryResourceId&) const = default;
};

struct FCrowdBoundaryTaskKey
{
  FCrowdBoundaryStageId StageId;
  FCrowdBoundaryTaskTypeId TaskTypeId;
  uint64 ScopeKey = 0;

  bool operator==(const FCrowdBoundaryTaskKey& Other) const = default;

  bool operator<(const FCrowdBoundaryTaskKey& Other) const
  {
    if (StageId != Other.StageId)
      return StageId < Other.StageId;
    if (TaskTypeId != Other.TaskTypeId)
      return TaskTypeId < Other.TaskTypeId;
    return ScopeKey < Other.ScopeKey;
  }

  bool IsValid() const
  {
    return StageId.IsValid() && TaskTypeId.IsValid();
  }

  friend uint32 GetTypeHash(const FCrowdBoundaryTaskKey& Key)
  {
    return HashCombineFast(
      HashCombineFast(
        ::GetTypeHash(Key.StageId.Value),
        ::GetTypeHash(Key.TaskTypeId.Value)),
      ::GetTypeHash(Key.ScopeKey));
  }
};

struct FCrowdBoundaryInputResource
{
  FCrowdBoundaryResourceId ResourceId;
  uint32 Revision = 0;
  uint32 SchemaId = 0;
  uint64 StableHash = 0;

  bool IsValid() const
  {
    return ResourceId.IsValid() && Revision != 0
      && SchemaId != 0 && StableHash != 0;
  }
};

struct FCrowdBoundaryOutputResource
{
  FCrowdBoundaryResourceId ResourceId;
  uint32 Revision = 0;
  uint32 SchemaId = 0;
  uint32 Capacity = 0;
  uint64 StableHash = 0;
  bool bRequiresCompleteSet = true;

  bool IsValid() const
  {
    return ResourceId.IsValid() && Revision != 0
      && SchemaId != 0 && Capacity != 0 && StableHash != 0;
  }
};

struct MASSCROWDRUNTIME_API FCrowdBoundaryTaskDescriptor
{
  FCrowdBoundaryTaskKey Key;
  TArray<FCrowdBoundaryTaskKey> Prerequisites;
  TArray<FCrowdBoundaryInputResource> Inputs;
  FCrowdBoundaryOutputResource Output;
  uint32 TelemetryId = 0;
  bool bRequireOffGameThread = true;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdBoundaryTaskResult
{
  uint64 StableHash = 14695981039346656037ull;
  bool bSucceeded = false;
  bool bRanOffGameThread = false;

  static FCrowdBoundaryTaskResult Success(uint64 StableHash);
  static FCrowdBoundaryTaskResult Failure();
};

using FCrowdBoundaryTaskBody =
  TUniqueFunction<FCrowdBoundaryTaskResult()>;

enum class ECrowdBoundaryTransactionState : uint8
{
  Idle = 0,
  Gathering,
  Queued,
  Working,
  Merging,
  Validating,
  ReadyToCommit,
  Committed,
  Failed
};

enum class ECrowdBoundaryPollResult : uint8
{
  Pending = 0,
  Ready,
  Failed
};

struct FCrowdBoundaryPhaseTimings
{
  double GatherMilliseconds = 0.0;
  double QueueMilliseconds = 0.0;
  double WorkMilliseconds = 0.0;
  double WaitMilliseconds = 0.0;
  double MergeMilliseconds = 0.0;
  double ValidateMilliseconds = 0.0;
  double CommitMilliseconds = 0.0;
};

struct FCrowdBoundaryTaskTimings
{
  double QueueMilliseconds = 0.0;
  double ExecutionMilliseconds = 0.0;
  double EndToEndMilliseconds = 0.0;
};

struct FCrowdBoundaryCompletedTask
{
  FCrowdBoundaryTaskKey Key;
  uint32 TelemetryId = 0;
  FCrowdBoundaryTaskResult Result;
  FCrowdBoundaryTaskTimings Timings;
};

struct FCrowdBoundaryOrchestratorResult
{
  ECrowdBoundaryTransactionState State =
    ECrowdBoundaryTransactionState::Idle;
  FCrowdBoundaryPhaseTimings Timings;
  TArray<FCrowdBoundaryCompletedTask> Tasks;
  uint64 SnapshotHash = 0;
  uint64 CommitPlanHash = 0;
  bool bSucceeded = false;
};

class ICrowdBoundaryPreparedPatchPayload
{
public:
  virtual ~ICrowdBoundaryPreparedPatchPayload() = default;
};

struct FCrowdBoundaryApplyPhaseId
{
  uint32 Value = 1;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryApplyPhaseId&) const = default;
  auto operator<=>(const FCrowdBoundaryApplyPhaseId&) const = default;
};

struct FCrowdBoundaryAdapterId
{
  uint32 Value = 0;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryAdapterId&) const = default;
  auto operator<=>(const FCrowdBoundaryAdapterId&) const = default;
};

struct FCrowdBoundaryPatchKey
{
  uint64 Value = 1;
  bool IsValid() const { return Value != 0; }
  bool operator==(const FCrowdBoundaryPatchKey&) const = default;
  auto operator<=>(const FCrowdBoundaryPatchKey&) const = default;
};

struct FCrowdBoundaryPreparedPatch
{
  FCrowdBoundaryApplyPhaseId ApplyPhase;
  FCrowdBoundaryAdapterId AdapterId;
  FCrowdBoundaryPatchKey PatchKey;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdStableEntityRef> EntityRefs;
  uint64 StableHash = 0;
  TSharedPtr<const ICrowdBoundaryPreparedPatchPayload, ESPMode::ThreadSafe>
    Payload;
  bool bValid = false;
};

struct MASSCROWDRUNTIME_API FCrowdBoundaryPatchDescriptor
{
  FCrowdBoundaryApplyPhaseId ApplyPhase;
  FCrowdBoundaryAdapterId AdapterId;
  FCrowdBoundaryPatchKey PatchKey;
  uint64 StableHash = 0;

  bool operator==(const FCrowdBoundaryPatchDescriptor& Other) const = default;
};

struct MASSCROWDRUNTIME_API FCrowdBehaviorBoundaryMetadata
{
  uint32 SourceSetRevision = 0;
  uint64 SourceSetHash = 0;
  uint64 CommandBatchHash = 0;
  uint64 ResolvedChannelHash = 0;

  bool IsEmpty() const
  {
    return SourceSetRevision == 0
      && SourceSetHash == 0
      && CommandBatchHash == 0
      && ResolvedChannelHash == 0;
  }

  bool IsValid() const
  {
    return SourceSetRevision != 0
      && SourceSetHash != 0
      && CommandBatchHash != 0
      && ResolvedChannelHash != 0;
  }
};

// Versioned, product-level description of every write that belongs to one
// fixed-step transaction. Demo-owned payloads stay outside Runtime; only their
// stable descriptors participate in the authoritative plan hash.
struct MASSCROWDRUNTIME_API FCrowdBoundaryCommitEnvelope
{
  static constexpr uint32 CurrentVersion = 3;

  uint32 Version = CurrentVersion;
  FCrowdMassCommitPlan MovementPlan;
  FCrowdBehaviorBoundaryMetadata Behavior;
  TArray<FCrowdBoundaryPatchDescriptor> Patches;
  uint64 StableHash = 0;
  bool bValid = false;
};

struct FCrowdBoundaryApplyContext
{
  FMassEntityManager* EntityManager = nullptr;
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TMap<FCrowdStableEntityRef, FMassEntityHandle> ResolvedTargets;
};

class MASSCROWDRUNTIME_API ICrowdBoundaryCommitAdapter
{
public:
  virtual ~ICrowdBoundaryCommitAdapter() = default;

  virtual bool Prepare(
    const FCrowdMassBoundarySnapshot& Snapshot,
    FCrowdBoundaryPreparedPatch& OutPatch) const = 0;

  virtual bool ValidatePrepared(
    const FCrowdBoundaryPreparedPatch& Patch,
    TConstArrayView<FCrowdMassCommitTarget> Targets) const = 0;

  virtual void ApplyPrepared(
    const FCrowdBoundaryPreparedPatch& Patch,
    FCrowdBoundaryApplyContext& Context) const = 0;
};

// Owns the prepare/validate/apply lifecycle for every host adapter in one
// boundary. ApplyAll is deliberately void: after ValidateAll succeeds there is
// no business failure path and no adapter is allowed to publish a partial
// result before the complete patch set has been accepted.
class MASSCROWDRUNTIME_API FCrowdBoundaryPatchTransaction
{
public:
  bool AddAdapter(const ICrowdBoundaryCommitAdapter& Adapter);
  bool PrepareAll(const FCrowdMassBoundarySnapshot& Snapshot);
  bool ValidateAll(TConstArrayView<FCrowdMassCommitTarget> Targets);
  void ApplyAll(FCrowdBoundaryApplyContext& Context);

  TConstArrayView<FCrowdBoundaryPreparedPatch> GetPreparedPatches() const
  {
    return PreparedPatches;
  }

  bool IsPrepared() const { return bPrepared; }
  bool IsValidated() const { return bValidated; }

private:
  struct FEntry
  {
    const ICrowdBoundaryCommitAdapter* Adapter = nullptr;
    FCrowdBoundaryPreparedPatch Patch;
  };

  TArray<FEntry> Entries;
  TArray<FCrowdBoundaryPreparedPatch> PreparedPatches;
  bool bPrepared = false;
  bool bValidated = false;
  bool bApplied = false;
};

class MASSCROWDRUNTIME_API FCrowdMassBoundaryOrchestrator
{
public:
  FCrowdMassBoundaryOrchestrator();
  ~FCrowdMassBoundaryOrchestrator();

  FCrowdMassBoundaryOrchestrator(
    const FCrowdMassBoundaryOrchestrator&) = delete;
  FCrowdMassBoundaryOrchestrator& operator=(
    const FCrowdMassBoundaryOrchestrator&) = delete;

  bool Begin(
    const FCrowdMassBoundarySnapshot& Snapshot,
    double GatherMilliseconds);

  bool AddTask(
    FCrowdBoundaryTaskKey Key,
    TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites,
    FCrowdBoundaryTaskBody&& Body,
    bool bRequireOffGameThread = true);

  bool AddTask(
    FCrowdBoundaryTaskDescriptor Descriptor,
    FCrowdBoundaryTaskBody&& Body);

  bool Dispatch();
  ECrowdBoundaryPollResult PollAndDrain();

  bool SealMergedPlan(
    const FCrowdMassCommitPlan& CommitPlan,
    double MergeMilliseconds);

  static bool BuildCommitEnvelope(
    const FCrowdMassBoundarySnapshot& Snapshot,
    const FCrowdMassCommitPlan& MovementPlan,
    TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
    FCrowdBoundaryCommitEnvelope& OutEnvelope,
    const FCrowdBehaviorBoundaryMetadata* BehaviorMetadata = nullptr);

  bool SealMergedEnvelope(
    const FCrowdBoundaryCommitEnvelope& Envelope,
    double MergeMilliseconds);

  bool MarkValidated(double ValidateMilliseconds);
  bool MarkCommitted(double CommitMilliseconds);
  void Fail();

  ECrowdBoundaryTransactionState GetState() const { return State; }
  const FCrowdMassBoundarySnapshot& GetSnapshot() const { return Snapshot; }
  FCrowdBoundaryOrchestratorResult BuildResult() const;

private:
  struct FTaskNode;

  bool ValidateTaskGraph() const;
  FTaskNode* FindNode(const FCrowdBoundaryTaskKey& Key);
  const FTaskNode* FindNode(const FCrowdBoundaryTaskKey& Key) const;

  FCrowdMassBoundarySnapshot Snapshot;
  FCrowdBoundaryPhaseTimings Timings;
  TArray<TUniquePtr<FTaskNode>> Nodes;
  UE::Tasks::FTask CompletionTask;
  ECrowdBoundaryTransactionState State =
    ECrowdBoundaryTransactionState::Idle;
  uint64 CommitPlanHash = 0;
  double WorkStartSeconds = 0.0;
};
