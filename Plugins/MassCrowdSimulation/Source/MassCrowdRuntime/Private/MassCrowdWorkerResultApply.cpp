#include "MassCrowdWorkerResultApply.h"

FCrowdWorkerResultCommitToken
FCrowdWorkerResultCommitToken::FromPrepared(
  const FCrowdWorkerPreparedResultApply& Prepared)
{
  FCrowdWorkerResultCommitToken Token;
  Token.Generation = Prepared.Batch.Generation;
  Token.PublishSequence = Prepared.Batch.PublishSequence;
  Token.LastAppliedInputSequence =
    Prepared.Batch.LastAppliedInputSequence;
  Token.BaseConsumedPublishSequence =
    Prepared.BaseConsumedPublishSequence;
  Token.BaseAppliedEventSequence =
    Prepared.BaseAppliedEventSequence;
  Token.BaseStableEntityViewRevision =
    Prepared.BaseStableEntityViewRevision;
  return Token;
}

bool FCrowdWorkerResultCommitToken::Matches(
  const FCrowdWorkerPreparedResultApply& Prepared) const
{
  return IsValid() && Prepared.IsValid()
    && Generation == Prepared.Batch.Generation
    && PublishSequence == Prepared.Batch.PublishSequence
    && LastAppliedInputSequence
      == Prepared.Batch.LastAppliedInputSequence
    && BaseConsumedPublishSequence
      == Prepared.BaseConsumedPublishSequence
    && BaseAppliedEventSequence
      == Prepared.BaseAppliedEventSequence
    && BaseStableEntityViewRevision
      == Prepared.BaseStableEntityViewRevision;
}

bool FCrowdWorkerResultCommitToken::IsValid() const
{
  return Generation != 0 && PublishSequence != 0;
}

ECrowdWorkerResultOwnerCommitResult
FCrowdWorkerResultOwnerCommitBarrier::Commit(
  FCrowdWorkerResultApplyProxy& Proxy,
  const FCrowdWorkerPreparedResultApply& Prepared,
  const FCrowdWorkerResultCommitToken& CommitToken,
  TFunctionRef<bool()> HostFinalValidate,
  TFunctionRef<void()> HostApplyNoFail,
  TFunctionRef<void()> HostCommitSideEffectsNoFail)
{
  if (!CommitToken.Matches(Prepared))
    return ECrowdWorkerResultOwnerCommitResult::RejectedCandidate;
  const ECrowdWorkerResultApplyResult ProxyValidation =
    Proxy.ValidatePreparedState(Prepared);
  if (ProxyValidation != ECrowdWorkerResultApplyResult::Applied
    && ProxyValidation != ECrowdWorkerResultApplyResult::AppliedEmpty)
    return ECrowdWorkerResultOwnerCommitResult::RejectedProxyState;
  if (!HostFinalValidate())
    return ECrowdWorkerResultOwnerCommitResult::RejectedHostState;

  HostApplyNoFail();
  Proxy.CommitPreparedValidated(Prepared);
  HostCommitSideEffectsNoFail();
  return ECrowdWorkerResultOwnerCommitResult::Committed;
}

bool FCrowdWorkerResultApplyProxy::ResetQuiescent(
  const uint64 Generation,
  const FCrowdWorkerContractLimits& InLimits)
{
  if (Generation == 0 || !InLimits.IsValid())
    return false;
  Limits = InLimits;
  StableEntities.Reset();
  StableEntitySlots.Reset();
  ProxyStates.Reset();
  DomainStates.Reset();
  PendingDirtyStates.Reset();
  PendingDirtyBatch.Reset();
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
  TArray<FCrowdStableEntityRef> Candidate;
  Candidate.Reserve(EntityRefs.Num());
  for (const FCrowdStableEntityRef& Ref : EntityRefs)
  {
    if (!Ref.IsValid())
    {
      LatchViolation();
      return false;
    }
    Candidate.Add(Ref);
  }
  Candidate.Sort();
  for (int32 Index = 1; Index < Candidate.Num(); ++Index)
  {
    if (Candidate[Index - 1] == Candidate[Index])
    {
      LatchViolation();
      return false;
    }
  }
  if (Candidate == StableEntities) return true;
  StableEntities = MoveTemp(Candidate);
  StableEntitySlots.Reset();
  StableEntitySlots.Reserve(StableEntities.Num());
  for (int32 Index = 0; Index < StableEntities.Num(); ++Index)
    StableEntitySlots.Add(StableEntities[Index], Index);
  for (auto It = ProxyStates.CreateIterator(); It; ++It)
    if (!StableEntitySlots.Contains(It.Key()))
      It.RemoveCurrent();
  for (auto It = DomainStates.CreateIterator(); It; ++It)
    if (!StableEntitySlots.Contains(It.Key().EntityRef))
      It.RemoveCurrent();
  for (auto It = PendingDirtyStates.CreateIterator(); It; ++It)
  {
    const int32* Slot = StableEntitySlots.Find(It.Key().EntityRef);
    if (!Slot)
      It.RemoveCurrent();
    else
      It.Value().StableSlot = *Slot;
  }
  RebuildPendingDirtyBatch();
  Metrics.CurrentEntityCount = StableEntities.Num();
  Metrics.ProxyStateCount = ProxyStates.Num();
  Metrics.DomainStateCount = DomainStates.Num();
  ++Metrics.StableEntityViewRevision;
  return true;
}

void FCrowdWorkerResultApplyProxy::LatchViolation()
{
  Metrics.bViolation = true;
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::Prepare(
  const FCrowdWorkerPublishedBatch& Batch,
  FCrowdWorkerPreparedResultApply& OutPrepared)
{
  OutPrepared.Reset();
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

  OutPrepared.Batch = Batch;
  OutPrepared.StatePatchStableSlots.Reserve(
    Batch.StatePatches.Num());
  for (const FCrowdWorkerStatePatch& Patch : Batch.StatePatches)
  {
    const int32* StableSlot = StableEntitySlots.Find(Patch.EntityRef);
    OutPrepared.StatePatchStableSlots.Add(
      StableSlot ? *StableSlot : INDEX_NONE);
  }
  OutPrepared.BaseConsumedPublishSequence =
    Metrics.LastConsumedPublishSequence;
  OutPrepared.BaseAppliedEventSequence =
    Metrics.LastAppliedEventSequence;
  OutPrepared.BaseStableEntityViewRevision =
    Metrics.StableEntityViewRevision;
  OutPrepared.bPrepared = true;
  return Batch.StatePatches.IsEmpty()
      && Batch.OrderedEvents.IsEmpty()
    ? ECrowdWorkerResultApplyResult::AppliedEmpty
    : ECrowdWorkerResultApplyResult::Applied;
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::ValidatePreparedState(
  const FCrowdWorkerPreparedResultApply& Prepared) const
{
  if (!bInitialized)
    return ECrowdWorkerResultApplyResult::RejectedNotInitialized;
  if (Metrics.bViolation)
    return ECrowdWorkerResultApplyResult::Violation;
  if (!Prepared.IsValid()
    || Prepared.Batch.Generation != Metrics.Generation
    || Prepared.BaseConsumedPublishSequence
      != Metrics.LastConsumedPublishSequence
    || Prepared.BaseAppliedEventSequence
      != Metrics.LastAppliedEventSequence
    || Prepared.BaseStableEntityViewRevision
      != Metrics.StableEntityViewRevision)
    return ECrowdWorkerResultApplyResult::RejectedPreparedState;
  return Prepared.Batch.StatePatches.IsEmpty()
      && Prepared.Batch.OrderedEvents.IsEmpty()
    ? ECrowdWorkerResultApplyResult::AppliedEmpty
    : ECrowdWorkerResultApplyResult::Applied;
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::CommitPrepared(
  const FCrowdWorkerPreparedResultApply& Prepared)
{
  const ECrowdWorkerResultApplyResult Validation =
    ValidatePreparedState(Prepared);
  if (Validation != ECrowdWorkerResultApplyResult::Applied
    && Validation != ECrowdWorkerResultApplyResult::AppliedEmpty)
  {
    if (Validation == ECrowdWorkerResultApplyResult::RejectedPreparedState)
      LatchViolation();
    return Validation;
  }

  return ApplyPreparedNoFail(Prepared);
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::ApplyPreparedNoFail(
  const FCrowdWorkerPreparedResultApply& Prepared)
{
  checkf(Prepared.IsValid(),
    TEXT("Invalid prepared Result Apply entered no-fail commit"));

  const FCrowdWorkerPublishedBatch& Batch = Prepared.Batch;

  for (int32 PatchIndex = 0;
       PatchIndex < Batch.StatePatches.Num(); ++PatchIndex)
  {
    const FCrowdWorkerStatePatch& Patch =
      Batch.StatePatches[PatchIndex];
    const int32 StableSlot =
      Prepared.StatePatchStableSlots[PatchIndex];
    if (StableSlot == INDEX_NONE)
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
      FCrowdWorkerResultApplyDirtyRecord& Dirty =
        PendingDirtyStates.FindOrAdd({Patch.EntityRef, Field});
      Dirty.StableSlot = StableSlot;
      Dirty.DomainState = State;
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
  PendingDirtyBatch.Generation = Batch.Generation;
  PendingDirtyBatch.PublishSequence = Batch.PublishSequence;
  PendingDirtyBatch.LastAppliedInputSequence =
    Batch.LastAppliedInputSequence;
  RebuildPendingDirtyBatch();
  ++Metrics.PublishedDirtyBatchCount;
  if (Batch.StatePatches.IsEmpty()
    && Batch.OrderedEvents.IsEmpty())
  {
    ++Metrics.AppliedEmptyBatchCount;
    return ECrowdWorkerResultApplyResult::AppliedEmpty;
  }
  return ECrowdWorkerResultApplyResult::Applied;
}

void FCrowdWorkerResultApplyProxy::CommitPreparedValidated(
  const FCrowdWorkerPreparedResultApply& Prepared)
{
  const ECrowdWorkerResultApplyResult CommitResult =
    ApplyPreparedNoFail(Prepared);
  checkf(CommitResult == ECrowdWorkerResultApplyResult::Applied
      || CommitResult == ECrowdWorkerResultApplyResult::AppliedEmpty,
    TEXT("Validated Result Apply proxy commit unexpectedly failed"));
}

ECrowdWorkerResultApplyResult
FCrowdWorkerResultApplyProxy::Apply(
  const FCrowdWorkerPublishedBatch& Batch)
{
  FCrowdWorkerPreparedResultApply Prepared;
  const ECrowdWorkerResultApplyResult PrepareResult =
    Prepare(Batch, Prepared);
  if (PrepareResult != ECrowdWorkerResultApplyResult::Applied
    && PrepareResult
      != ECrowdWorkerResultApplyResult::AppliedEmpty)
    return PrepareResult;
  return CommitPrepared(Prepared);
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

int32 FCrowdWorkerResultApplyProxy::FindStableEntitySlot(
  const FCrowdStableEntityRef& EntityRef) const
{
  const int32* Slot = StableEntitySlots.Find(EntityRef);
  return Slot ? *Slot : INDEX_NONE;
}

void FCrowdWorkerResultApplyProxy::RebuildPendingDirtyBatch()
{
  PendingDirtyBatch.Records.Reset(PendingDirtyStates.Num());
  for (const auto& Pair : PendingDirtyStates)
    PendingDirtyBatch.Records.Add(Pair.Value);
  PendingDirtyBatch.Records.Sort([](
    const FCrowdWorkerResultApplyDirtyRecord& A,
    const FCrowdWorkerResultApplyDirtyRecord& B)
  {
    if (A.StableSlot != B.StableSlot)
      return A.StableSlot < B.StableSlot;
    return A.DomainState.Field < B.DomainState.Field;
  });
}

bool FCrowdWorkerResultApplyProxy::AcknowledgeDirtyBatch(
  const uint64 PublishSequence)
{
  if (!PendingDirtyBatch.IsValid()
    || PendingDirtyBatch.PublishSequence != PublishSequence)
    return false;
  PendingDirtyStates.Reset();
  PendingDirtyBatch.Reset();
  ++Metrics.ConsumedDirtyBatchCount;
  Metrics.LastConsumedDirtyPublishSequence = PublishSequence;
  return true;
}
