#include "MassCrowdWorkerResultApply.h"

bool FCrowdWorkerResultApplyProxy::ResetQuiescent(
  const uint64 Generation,
  const FCrowdWorkerContractLimits& InLimits)
{
  if (Generation == 0 || !InLimits.IsValid())
    return false;
  Limits = InLimits;
  CurrentEntities.Reset();
  ProxyStates.Reset();
  DomainStates.Reset();
  Metrics = {};
  Metrics.Generation = Generation;
  bInitialized = true;
  return true;
}

bool FCrowdWorkerResultApplyProxy::ResetFromCheckpoint(
  const uint64 Generation,
  const FCrowdWorkerContractLimits& InLimits,
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs,
  const uint64 LastAppliedInputSequence,
  const uint64 LastAppliedEventSequence)
{
  if (!ResetQuiescent(Generation, InLimits)
    || !UpdateCurrentEntities(Generation, EntityRefs))
    return false;
  Metrics.LastAppliedInputSequence = LastAppliedInputSequence;
  Metrics.LastAppliedEventSequence = LastAppliedEventSequence;
  return true;
}

bool FCrowdWorkerResultApplyProxy::UpdateCurrentEntities(
  const uint64 Generation,
  const TConstArrayView<FCrowdStableEntityRef> EntityRefs)
{
  if (!bInitialized || Metrics.bViolation
    || Generation != Metrics.Generation)
    return false;
  TSet<FCrowdStableEntityRef> Candidate;
  Candidate.Reserve(EntityRefs.Num());
  for (const FCrowdStableEntityRef& Ref : EntityRefs)
  {
    if (!Ref.IsValid() || Candidate.Contains(Ref))
    {
      LatchViolation();
      return false;
    }
    Candidate.Add(Ref);
  }
  CurrentEntities = MoveTemp(Candidate);
  for (auto It = ProxyStates.CreateIterator(); It; ++It)
    if (!CurrentEntities.Contains(It.Key()))
      It.RemoveCurrent();
  for (auto It = DomainStates.CreateIterator(); It; ++It)
    if (!CurrentEntities.Contains(It.Key().EntityRef))
      It.RemoveCurrent();
  Metrics.CurrentEntityCount = CurrentEntities.Num();
  Metrics.ProxyStateCount = ProxyStates.Num();
  Metrics.DomainStateCount = DomainStates.Num();
  return true;
}

void FCrowdWorkerResultApplyProxy::LatchViolation()
{
  Metrics.bViolation = true;
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::Apply(
  const FCrowdWorkerPublishedBatch& Batch)
{
  if (!bInitialized)
    return ECrowdWorkerResultApplyResult::RejectedNotInitialized;
  if (Metrics.bViolation)
    return ECrowdWorkerResultApplyResult::Violation;
  if (FCrowdWorkerPublishedBatchValidator::Validate(
      Batch, Limits, Metrics.Generation,
      Metrics.LastConsumedPublishSequence)
    != ECrowdWorkerPublishedValidationResult::Valid)
  {
    LatchViolation();
    return ECrowdWorkerResultApplyResult::RejectedBatch;
  }
  for (const FCrowdWorkerStatePatch& Patch : Batch.StatePatches)
  {
    if (Patch.StateFieldId == 0)
    {
      if (Patch.DirtyMask == 0
        || (Patch.DirtyMask
          & ~CrowdWorkerResultFields::AllowedPw5Mask) != 0)
      {
        LatchViolation();
        return ECrowdWorkerResultApplyResult::RejectedOwnerMask;
      }
      continue;
    }
    const uint16 FieldValue = Patch.StateFieldId - 1;
    if (FieldValue
        >= static_cast<uint16>(ECrowdWorkerField::Count)
      || Patch.DirtyMask
        != CrowdWorkerRuntimeV2FieldMask(
          static_cast<ECrowdWorkerField>(FieldValue)))
    {
      LatchViolation();
      return ECrowdWorkerResultApplyResult::RejectedOwnerMask;
    }
  }
  uint64 ExpectedEventSequence =
    Metrics.LastAppliedEventSequence + 1;
  for (const FCrowdWorkerGameplayEvent& Event : Batch.OrderedEvents)
  {
    if (Event.EventSequence != ExpectedEventSequence++)
    {
      LatchViolation();
      return ECrowdWorkerResultApplyResult::RejectedEventSequence;
    }
  }

  for (const FCrowdWorkerStatePatch& Patch : Batch.StatePatches)
  {
    if (!CurrentEntities.Contains(Patch.EntityRef))
    {
      ++Metrics.StaleLifecyclePatchCount;
      continue;
    }
    if (Patch.StateFieldId != 0)
    {
      const ECrowdWorkerField Field =
        static_cast<ECrowdWorkerField>(
          Patch.StateFieldId - 1);
      FCrowdWorkerDomainProxyState& State =
        DomainStates.FindOrAdd({Patch.EntityRef, Field});
      State.EntityRef = Patch.EntityRef;
      State.Field = Field;
      State.State = Patch.State;
      State.WorkerEpoch = Patch.WorkerEpoch;
      State.SourceInputSequence = Patch.SourceInputSequence;
      State.PublishSequence = Batch.PublishSequence;
      ++Metrics.AppliedPatchCount;
      ++Metrics.AppliedDomainPatchCount;
      continue;
    }
    FCrowdWorkerPresentationDiagnosticProxyState& State =
      ProxyStates.FindOrAdd(Patch.EntityRef);
    State.EntityRef = Patch.EntityRef;
    State.State = Patch.State;
    State.DirtyMask |= Patch.DirtyMask;
    State.WorkerEpoch = Patch.WorkerEpoch;
    State.SourceInputSequence = Patch.SourceInputSequence;
    State.PublishSequence = Batch.PublishSequence;
    ++Metrics.AppliedPatchCount;
  }
  Metrics.LastConsumedPublishSequence = Batch.PublishSequence;
  Metrics.LastAppliedInputSequence =
    Batch.LastAppliedInputSequence;
  ++Metrics.AppliedBatchCount;
  Metrics.AppliedEventCount += Batch.OrderedEvents.Num();
  if (!Batch.OrderedEvents.IsEmpty())
    Metrics.LastAppliedEventSequence =
      Batch.OrderedEvents.Last().EventSequence;
  Metrics.ProxyStateCount = ProxyStates.Num();
  Metrics.DomainStateCount = DomainStates.Num();
  if (Batch.StatePatches.IsEmpty()
    && Batch.OrderedEvents.IsEmpty())
  {
    ++Metrics.AppliedEmptyBatchCount;
    return ECrowdWorkerResultApplyResult::AppliedEmpty;
  }
  return ECrowdWorkerResultApplyResult::Applied;
}

const FCrowdWorkerPresentationDiagnosticProxyState*
FCrowdWorkerResultApplyProxy::Find(
  const FCrowdStableEntityRef& EntityRef) const
{
  return ProxyStates.Find(EntityRef);
}

const FCrowdWorkerDomainProxyState*
FCrowdWorkerResultApplyProxy::FindDomain(
  const FCrowdStableEntityRef& EntityRef,
  const ECrowdWorkerField Field) const
{
  return DomainStates.Find({EntityRef, Field});
}
