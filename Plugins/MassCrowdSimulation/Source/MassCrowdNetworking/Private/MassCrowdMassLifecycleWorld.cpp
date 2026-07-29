#include "MassCrowdMassLifecycleWorld.h"

bool FCrowdMassLifecycleWorld::InitializeFromSnapshot(
  FMassEntityManager& InEntityManager,
  const FMassArchetypeHandle& InArchetype,
  const uint32 SnapshotRevision,
  const int64 FixedStepIndex,
  const uint32 RelevantSetRevision,
  const TConstArrayView<FCrowdLifecycleSnapshotEntity> Entities,
  const FCrowdLifecycleBatchLimits& Limits,
  const uint64 ResumeSequence)
{
  if (bInitialized) return false;
  FCrowdLifecycleDeltaState CandidateState;
  if (!CandidateState.BeginFromSnapshot(
    SnapshotRevision,
    FixedStepIndex,
    RelevantSetRevision,
    Entities,
    Limits,
    ResumeSequence)) return false;

  TArray<FCrowdMassRuntimeLifecycleEntity> RuntimeEntities;
  RuntimeEntities.Reserve(Entities.Num());
  for (const FCrowdLifecycleSnapshotEntity& Entity : Entities)
  {
    RuntimeEntities.Add(FCrowdMassRuntimeLifecycleEntity{
      Entity.AgentFacts, Entity.MembershipKey});
  }
  if (!RuntimeStore.Initialize(InEntityManager, InArchetype, RuntimeEntities)) return false;
  DeltaState = MoveTemp(CandidateState);
  LastAppliedFixedStep = FixedStepIndex;
  bInitialized = true;
  return true;
}

void FCrowdMassLifecycleWorld::Reset()
{
  RuntimeStore.Reset();
  DeltaState = {};
  LastAppliedFixedStep = INDEX_NONE;
  bInitialized = false;
}

ECrowdLifecycleBatchAcceptResult FCrowdMassLifecycleWorld::ApplyAtBoundary(
  const FCrowdSpawnBatch& Batch)
{
  if (!bInitialized) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.FixedStepIndex < LastAppliedFixedStep)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  FCrowdLifecycleDeltaState CandidateState = DeltaState;
  const ECrowdLifecycleBatchAcceptResult Result = CandidateState.AcceptSpawnBatch(Batch);
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return Result;
  TArray<FCrowdMassRuntimeLifecycleEntity> RuntimeEntities;
  for (const FCrowdSpawnEntry& Entry : Batch.Entries)
    RuntimeEntities.Add(FCrowdMassRuntimeLifecycleEntity{Entry.AgentFacts, Entry.MembershipKey});
  if (!RuntimeStore.Spawn(RuntimeEntities))
    return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  DeltaState = MoveTemp(CandidateState);
  LastAppliedFixedStep = Batch.Header.FixedStepIndex;
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

ECrowdLifecycleBatchAcceptResult FCrowdMassLifecycleWorld::ApplyAtBoundary(
  const FCrowdDespawnBatch& Batch)
{
  if (!bInitialized) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.FixedStepIndex < LastAppliedFixedStep)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  FCrowdLifecycleDeltaState CandidateState = DeltaState;
  const ECrowdLifecycleBatchAcceptResult Result = CandidateState.AcceptDespawnBatch(Batch);
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return Result;
  TArray<FCrowdStableEntityRef> EntityRefs;
  for (const FCrowdDespawnEntry& Entry : Batch.Entries) EntityRefs.Add(Entry.EntityRef);
  if (!RuntimeStore.Despawn(EntityRefs))
    return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  DeltaState = MoveTemp(CandidateState);
  LastAppliedFixedStep = Batch.Header.FixedStepIndex;
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

ECrowdLifecycleBatchAcceptResult FCrowdMassLifecycleWorld::ApplyAtBoundary(
  const FCrowdMembershipBatch& Batch)
{
  if (!bInitialized) return ECrowdLifecycleBatchAcceptResult::RejectedInvalid;
  if (Batch.Header.FixedStepIndex < LastAppliedFixedStep)
    return ECrowdLifecycleBatchAcceptResult::RejectedStale;
  FCrowdLifecycleDeltaState CandidateState = DeltaState;
  const ECrowdLifecycleBatchAcceptResult Result = CandidateState.AcceptMembershipBatch(Batch);
  if (Result != ECrowdLifecycleBatchAcceptResult::Accepted) return Result;
  TArray<FCrowdMassRuntimeMembershipChange> Changes;
  for (const FCrowdMembershipEntry& Entry : Batch.Entries)
  {
    Changes.Add(FCrowdMassRuntimeMembershipChange{
      Entry.EntityRef, Entry.PreviousMembershipKey, Entry.NewMembershipKey});
  }
  if (!RuntimeStore.UpdateMembership(Changes))
    return ECrowdLifecycleBatchAcceptResult::RejectedConflict;
  DeltaState = MoveTemp(CandidateState);
  LastAppliedFixedStep = Batch.Header.FixedStepIndex;
  return ECrowdLifecycleBatchAcceptResult::Accepted;
}

bool FCrowdMassLifecycleWorld::ApplyAgentFactsCorrectionAtBoundary(
  const int64 FixedStepIndex,
  const FCrowdAgentFacts& CorrectedFacts)
{
  return ApplyAgentFactsCorrectionsAtBoundary(
    FixedStepIndex, MakeArrayView(&CorrectedFacts, 1));
}

bool FCrowdMassLifecycleWorld::ApplyAgentFactsCorrectionsAtBoundary(
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdAgentFacts> CorrectedFacts)
{
  if (!ValidateAgentFactsCorrectionsAtBoundary(
      FixedStepIndex, CorrectedFacts))
    return false;
  ApplyValidatedAgentFactsCorrectionsAtBoundary(
    FixedStepIndex, CorrectedFacts);
  return true;
}

bool FCrowdMassLifecycleWorld::ValidateAgentFactsCorrectionsAtBoundary(
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdAgentFacts> CorrectedFacts) const
{
  if (!bInitialized || CorrectedFacts.IsEmpty()
    || FixedStepIndex < LastAppliedFixedStep)
    return false;
  for (const FCrowdAgentFacts& Facts : CorrectedFacts)
    if (!DeltaState.Contains(Facts.StableEntityRef))
      return false;
  return RuntimeStore.ValidateAgentFactsCorrections(CorrectedFacts);
}

void FCrowdMassLifecycleWorld::
ApplyValidatedAgentFactsCorrectionsAtBoundary(
  const int64 FixedStepIndex,
  const TConstArrayView<FCrowdAgentFacts> CorrectedFacts)
{
  check(ValidateAgentFactsCorrectionsAtBoundary(
    FixedStepIndex, CorrectedFacts));
  RuntimeStore.ApplyValidatedAgentFactsCorrections(
    CorrectedFacts);
  LastAppliedFixedStep = FixedStepIndex;
}

uint64 FCrowdMassLifecycleWorld::CalculateEntitySetHash() const
{
  return RuntimeStore.CalculateEntitySetHash();
}

bool FCrowdMassLifecycleWorld::TryGetEntityHandle(
  const FCrowdStableEntityRef& EntityRef,
  FMassEntityHandle& OutEntity) const
{
  return DeltaState.Contains(EntityRef)
    && RuntimeStore.TryGetEntityHandle(EntityRef, OutEntity);
}
