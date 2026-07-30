#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBoundaryOrchestrator.h"

struct MASSCROWDRUNTIME_API FCrowdBoundaryTransactionId
{
  uint64 Generation = 0;
  uint64 PlanRevision = 0;
  int64 FixedStepIndex = INDEX_NONE;
  uint64 SnapshotHash = 0;

  bool IsValid() const
  {
    return Generation != 0 && FixedStepIndex >= 0
      && SnapshotHash != 0;
  }

  bool operator==(const FCrowdBoundaryTransactionId&) const = default;

  static FCrowdBoundaryTransactionId FromSnapshot(
    const FCrowdMassBoundarySnapshot& Snapshot,
    uint64 Generation);
};

// Product-level façade for one fixed-step boundary. It deliberately exposes
// exactly one dispatch and a non-blocking poll/drain transition. Host code may
// register Demo-owned work and patches, but task bodies still only receive
// immutable POD/read-only resources through their captures.
class MASSCROWDRUNTIME_API FCrowdMassBoundaryRunner
{
public:
  bool Begin(
    const FCrowdMassBoundarySnapshot& Snapshot,
    double GatherMilliseconds);
  bool Begin(
    const FCrowdMassBoundarySnapshot& Snapshot,
    double GatherMilliseconds,
    const FCrowdBoundaryTransactionId& TransactionId);

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

  bool BuildAndSealCommit(
    const FCrowdMassCommitPlan& MovementPlan,
    TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
    TConstArrayView<FCrowdMassCommitTarget> Targets,
    double MergeMilliseconds,
    const FCrowdBehaviorBoundaryMetadata* BehaviorMetadata = nullptr);

  bool MarkValidated(double ValidateMilliseconds);
  bool MarkCommitted(double CommitMilliseconds);
  void Fail();

  ECrowdBoundaryTransactionState GetState() const;
  const FCrowdBoundaryTransactionId& GetTransactionId() const
  {
    return TransactionId;
  }
  bool MatchesTransaction(
    const FCrowdBoundaryTransactionId& Expected) const
  {
    return TransactionId == Expected;
  }
  const FCrowdBoundaryCommitEnvelope& GetCommitEnvelope() const
  {
    return CommitEnvelope;
  }
  FCrowdBoundaryOrchestratorResult BuildResult() const;

  static bool ValidatePreparedPatches(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
    TConstArrayView<FCrowdMassCommitTarget> Targets);

private:
  FCrowdMassBoundaryOrchestrator Orchestrator;
  FCrowdBoundaryCommitEnvelope CommitEnvelope;
  FCrowdBoundaryTransactionId TransactionId;
  bool bDispatched = false;
  bool bDrained = false;
};
