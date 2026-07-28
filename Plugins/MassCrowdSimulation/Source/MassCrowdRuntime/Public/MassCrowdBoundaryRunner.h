#pragma once

#include "CoreMinimal.h"
#include "MassCrowdBoundaryOrchestrator.h"

// Product-level façade for one fixed-step boundary. It deliberately exposes
// exactly one dispatch and one wait. Host code may register Demo-owned work
// and patches, but task bodies still only receive immutable POD/read-only
// resources through their captures.
class MASSCROWDRUNTIME_API FCrowdMassBoundaryRunner
{
public:
  bool Begin(
    const FCrowdMassBoundarySnapshot& Snapshot,
    double GatherMilliseconds);

  bool AddTask(
    FCrowdBoundaryTaskKey Key,
    TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites,
    FCrowdBoundaryTaskBody&& Body,
    bool bRequireOffGameThread = true);

  bool Dispatch();
  bool WaitAndDrain();

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
  FCrowdMassBoundarySnapshot Snapshot;
  FCrowdBoundaryCommitEnvelope CommitEnvelope;
  bool bDispatched = false;
  bool bWaited = false;
};
