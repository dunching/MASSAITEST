#include "MassCrowdBoundaryRunner.h"

bool FCrowdMassBoundaryRunner::Begin(
  const FCrowdMassBoundarySnapshot& InSnapshot,
  const double GatherMilliseconds)
{
  if (bDispatched || bWaited
    || Orchestrator.GetSnapshot().bValid)
    return false;
  return Orchestrator.Begin(InSnapshot, GatherMilliseconds);
}

bool FCrowdMassBoundaryRunner::AddTask(
  const FCrowdBoundaryTaskKey Key,
  const TConstArrayView<FCrowdBoundaryTaskKey> Prerequisites,
  FCrowdBoundaryTaskBody&& Body,
  const bool bRequireOffGameThread)
{
  if (bDispatched || bWaited)
    return false;
  return Orchestrator.AddTask(
    Key, Prerequisites, MoveTemp(Body), bRequireOffGameThread);
}

bool FCrowdMassBoundaryRunner::AddTask(
  FCrowdBoundaryTaskDescriptor Descriptor,
  FCrowdBoundaryTaskBody&& Body)
{
  if (bDispatched || bWaited)
    return false;
  return Orchestrator.AddTask(MoveTemp(Descriptor), MoveTemp(Body));
}

bool FCrowdMassBoundaryRunner::Dispatch()
{
  if (bDispatched || bWaited)
    return false;
  bDispatched = Orchestrator.Dispatch();
  return bDispatched;
}

bool FCrowdMassBoundaryRunner::WaitAndDrain()
{
  if (!bDispatched || bWaited)
    return false;
  bWaited = true;
  return Orchestrator.WaitAndDrain();
}

bool FCrowdMassBoundaryRunner::ValidatePreparedPatches(
  const FCrowdMassBoundarySnapshot& Snapshot,
  const TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
  const TConstArrayView<FCrowdMassCommitTarget> Targets)
{
  if (!Snapshot.bValid || Targets.Num() != Snapshot.Agents.Num())
    return false;

  TArray<FCrowdStableEntityRef> Expected;
  Expected.Reserve(Snapshot.Agents.Num());
  for (const FCrowdMassBoundaryAgentRecord& Agent : Snapshot.Agents)
    Expected.Add(Agent.AgentFacts.StableEntityRef);
  Expected.Sort();
  for (int32 Index = 0; Index < Expected.Num(); ++Index)
    if (!Expected[Index].IsValid()
      || (Index > 0
        && !(Expected[Index - 1] < Expected[Index])))
      return false;

  TArray<FCrowdStableEntityRef> TargetRefs;
  TargetRefs.Reserve(Targets.Num());
  for (const FCrowdMassCommitTarget& Target : Targets)
  {
    if (!Target.EntityRef.IsValid()
      || Target.EntityRef.LifecycleSerial != Target.LifecycleSerial)
      return false;
    TargetRefs.Add(Target.EntityRef);
  }
  TargetRefs.Sort();
  if (TargetRefs != Expected)
    return false;

  for (int32 PatchIndex = 0;
    PatchIndex < PreparedPatches.Num(); ++PatchIndex)
  {
    const FCrowdBoundaryPreparedPatch& Patch =
      PreparedPatches[PatchIndex];
    bool bDuplicateKey = false;
    for (int32 PreviousIndex = 0;
      PreviousIndex < PatchIndex; ++PreviousIndex)
    {
      const FCrowdBoundaryPreparedPatch& Previous =
        PreparedPatches[PreviousIndex];
      bDuplicateKey |= Previous.ApplyPhase == Patch.ApplyPhase
        && Previous.AdapterId == Patch.AdapterId
        && Previous.PatchKey == Patch.PatchKey;
    }
    if (!Patch.bValid || !Patch.ApplyPhase.IsValid()
      || !Patch.AdapterId.IsValid() || !Patch.PatchKey.IsValid()
      || Patch.StableHash == 0
      || Patch.FixedStepIndex != Snapshot.FixedStepIndex
      || Patch.PlanRevision != Snapshot.PlanRevision
      || bDuplicateKey)
      return false;
    TArray<FCrowdStableEntityRef> PatchRefs = Patch.EntityRefs;
    PatchRefs.Sort();
    if (PatchRefs != Expected)
      return false;
  }
  return true;
}

bool FCrowdMassBoundaryRunner::BuildAndSealCommit(
  const FCrowdMassCommitPlan& MovementPlan,
  const TConstArrayView<FCrowdBoundaryPreparedPatch> PreparedPatches,
  const TConstArrayView<FCrowdMassCommitTarget> Targets,
  const double MergeMilliseconds,
  const FCrowdBehaviorBoundaryMetadata* BehaviorMetadata)
{
  const FCrowdMassBoundarySnapshot& Snapshot =
    Orchestrator.GetSnapshot();
  if (!bWaited
    || !FCrowdMassRuntimeBridge::ValidateCommitTargets(
      MovementPlan, Targets)
    || !ValidatePreparedPatches(Snapshot, PreparedPatches, Targets)
    || !FCrowdMassBoundaryOrchestrator::BuildCommitEnvelope(
      Snapshot, MovementPlan, PreparedPatches, CommitEnvelope,
      BehaviorMetadata))
    return false;
  return Orchestrator.SealMergedEnvelope(
    CommitEnvelope, MergeMilliseconds);
}

bool FCrowdMassBoundaryRunner::MarkValidated(
  const double ValidateMilliseconds)
{
  return Orchestrator.MarkValidated(ValidateMilliseconds);
}

bool FCrowdMassBoundaryRunner::MarkCommitted(
  const double CommitMilliseconds)
{
  return Orchestrator.MarkCommitted(CommitMilliseconds);
}

void FCrowdMassBoundaryRunner::Fail()
{
  Orchestrator.Fail();
}

ECrowdBoundaryTransactionState
FCrowdMassBoundaryRunner::GetState() const
{
  return Orchestrator.GetState();
}

FCrowdBoundaryOrchestratorResult
FCrowdMassBoundaryRunner::BuildResult() const
{
  return Orchestrator.BuildResult();
}
