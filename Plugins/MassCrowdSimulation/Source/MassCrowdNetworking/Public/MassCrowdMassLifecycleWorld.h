#pragma once

#include "MassCrowdLifecycleBatches.h"
#include "MassCrowdRuntimeLifecycle.h"

class MASSCROWDNETWORKING_API FCrowdMassLifecycleWorld
{
public:
  bool InitializeFromSnapshot(
    FMassEntityManager& InEntityManager,
    const FMassArchetypeHandle& InArchetype,
    uint32 SnapshotRevision,
    int64 FixedStepIndex,
    uint32 RelevantSetRevision,
    TConstArrayView<FCrowdLifecycleSnapshotEntity> Entities,
    const FCrowdLifecycleBatchLimits& Limits,
    uint64 ResumeSequence = 1);

  void Reset();

  ECrowdLifecycleBatchAcceptResult ApplyAtBoundary(const FCrowdSpawnBatch& Batch);
  ECrowdLifecycleBatchAcceptResult ApplyAtBoundary(const FCrowdDespawnBatch& Batch);
  ECrowdLifecycleBatchAcceptResult ApplyAtBoundary(const FCrowdMembershipBatch& Batch);

  bool ApplyAgentFactsCorrectionAtBoundary(
    int64 FixedStepIndex,
    const FCrowdAgentFacts& CorrectedFacts);
  bool ApplyAgentFactsCorrectionsAtBoundary(
    int64 FixedStepIndex,
    TConstArrayView<FCrowdAgentFacts> CorrectedFacts);
  bool ValidateAgentFactsCorrectionsAtBoundary(
    int64 FixedStepIndex,
    TConstArrayView<FCrowdAgentFacts> CorrectedFacts) const;
  void ApplyValidatedAgentFactsCorrectionsAtBoundary(
    int64 FixedStepIndex,
    TConstArrayView<FCrowdAgentFacts> CorrectedFacts);

  int32 GetActiveEntityCount() const { return RuntimeStore.GetActiveEntityCount(); }
  int64 GetLastAppliedFixedStep() const { return LastAppliedFixedStep; }
  uint64 CalculateMembershipHash() const { return DeltaState.CalculateMembershipHash(); }
  uint64 CalculateEntitySetHash() const;
  bool TryGetEntityHandle(
    const FCrowdStableEntityRef& EntityRef,
    FMassEntityHandle& OutEntity) const;

private:
  FCrowdMassRuntimeLifecycleStore RuntimeStore;
  FCrowdLifecycleDeltaState DeltaState;
  int64 LastAppliedFixedStep = INDEX_NONE;
  bool bInitialized = false;
};
