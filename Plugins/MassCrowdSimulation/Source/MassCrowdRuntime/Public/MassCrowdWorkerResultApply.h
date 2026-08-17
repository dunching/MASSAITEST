#pragma once

#include "CoreMinimal.h"
#include "MassCrowdWorkerContracts.h"
#include "MassCrowdWorkerRuntimeV2.h"
#include "Templates/Function.h"

namespace CrowdWorkerResultFields
{
  constexpr uint64 PresentationDiagnosticProxy = 1ull << 0;
  constexpr uint64 AllowedPw5Mask =
    PresentationDiagnosticProxy;
}

enum class ECrowdWorkerResultApplyResult : uint8
{
  Applied = 0,
  AppliedEmpty,
  RejectedNotInitialized,
  RejectedBatch,
  RejectedOwnerMask,
  RejectedEventSequence,
  RejectedPreparedState,
  Violation
};

struct MASSCROWDRUNTIME_API
FCrowdWorkerPresentationDiagnosticProxyState
{
  FCrowdStableEntityRef EntityRef;
  FCrowdWorkerPublishedState State;
  uint64 DirtyMask = 0;
  uint64 WorkerEpoch = 0;
  uint64 SourceInputSequence = 0;
  uint64 PublishSequence = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerResultApplyMetrics
{
  uint64 Generation = 0;
  uint64 LastConsumedPublishSequence = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 AppliedBatchCount = 0;
  uint64 AppliedEmptyBatchCount = 0;
  uint64 AppliedPatchCount = 0;
  uint64 StaleLifecyclePatchCount = 0;
  uint64 AppliedEventCount = 0;
  uint64 LastAppliedEventSequence = 0;
  int32 CurrentEntityCount = 0;
  int32 ProxyStateCount = 0;
  int32 DomainStateCount = 0;
  uint64 AppliedDomainPatchCount = 0;
  uint64 StableEntityViewRevision = 0;
  uint64 PublishedDirtyBatchCount = 0;
  uint64 ConsumedDirtyBatchCount = 0;
  uint64 LastConsumedDirtyPublishSequence = 0;
  bool bViolation = false;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerDomainProxyState
{
  FCrowdStableEntityRef EntityRef;
  ECrowdWorkerField Field = ECrowdWorkerField::Count;
  FCrowdWorkerPublishedState State;
  uint64 WorkerEpoch = 0;
  uint64 SourceInputSequence = 0;
  uint64 PublishSequence = 0;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerResultApplyDirtyRecord
{
  int32 StableSlot = INDEX_NONE;
  FCrowdWorkerDomainProxyState DomainState;
};

struct MASSCROWDRUNTIME_API FCrowdWorkerResultApplyDirtyBatch
{
  uint64 Generation = 0;
  uint64 PublishSequence = 0;
  uint64 LastAppliedInputSequence = 0;
  TArray<FCrowdWorkerResultApplyDirtyRecord> Records;

  bool IsValid() const
  {
    return Generation != 0 && PublishSequence != 0;
  }

  void Reset()
  {
    Generation = 0;
    PublishSequence = 0;
    LastAppliedInputSequence = 0;
    Records.Reset();
  }
};

struct MASSCROWDRUNTIME_API FCrowdWorkerPreparedResultApply
{
  FCrowdWorkerPublishedBatch Batch;
  TArray<int32> StatePatchStableSlots;
  uint64 BaseConsumedPublishSequence = 0;
  uint64 BaseAppliedEventSequence = 0;
  uint64 BaseStableEntityViewRevision = 0;
  bool bPrepared = false;

  bool IsValid() const
  {
    return bPrepared && Batch.Generation != 0
      && Batch.PublishSequence != 0
      && StatePatchStableSlots.Num() == Batch.StatePatches.Num();
  }

  void Reset()
  {
    Batch = {};
    StatePatchStableSlots.Reset();
    BaseConsumedPublishSequence = 0;
    BaseAppliedEventSequence = 0;
    BaseStableEntityViewRevision = 0;
    bPrepared = false;
  }
};

// Runtime-owned identity of one prepared Worker result candidate. Host-owned
// revisions and lifecycle tokens remain in the host prepared commit plan and
// are checked by the host final-validation adapter.
struct MASSCROWDRUNTIME_API FCrowdWorkerResultCommitToken
{
  uint64 Generation = 0;
  uint64 PublishSequence = 0;
  uint64 LastAppliedInputSequence = 0;
  uint64 BaseConsumedPublishSequence = 0;
  uint64 BaseAppliedEventSequence = 0;
  uint64 BaseStableEntityViewRevision = 0;

  static FCrowdWorkerResultCommitToken FromPrepared(
    const FCrowdWorkerPreparedResultApply& Prepared);

  bool Matches(const FCrowdWorkerPreparedResultApply& Prepared) const;
  bool IsValid() const;
};

enum class ECrowdWorkerResultOwnerCommitResult : uint8
{
  Committed = 0,
  RejectedCandidate,
  RejectedProxyState,
  RejectedHostState
};

class FCrowdWorkerResultApplyProxy;

// The single Runtime owner barrier for a prepared Worker result. All
// fallible Runtime and host validation occurs before either no-fail callback.
class MASSCROWDRUNTIME_API FCrowdWorkerResultOwnerCommitBarrier
{
public:
  static ECrowdWorkerResultOwnerCommitResult Commit(
    FCrowdWorkerResultApplyProxy& Proxy,
    const FCrowdWorkerPreparedResultApply& Prepared,
    const FCrowdWorkerResultCommitToken& CommitToken,
    TFunctionRef<bool()> HostFinalValidate,
    TFunctionRef<void()> HostApplyNoFail,
    TFunctionRef<void()> HostCommitSideEffectsNoFail);
};

class MASSCROWDRUNTIME_API FCrowdWorkerResultApplyProxy
{
public:
  bool ResetQuiescent(
    uint64 Generation,
    const FCrowdWorkerContractLimits& Limits);

  bool ResetFromCheckpoint(
    uint64 Generation,
    const FCrowdWorkerContractLimits& Limits,
    TConstArrayView<FCrowdStableEntityRef> EntityRefs,
    uint64 LastAppliedInputSequence,
    uint64 LastAppliedEventSequence);

  bool UpdateCurrentEntities(
    uint64 Generation,
    TConstArrayView<FCrowdStableEntityRef> EntityRefs);

  ECrowdWorkerResultApplyResult Apply(
    const FCrowdWorkerPublishedBatch& Batch);

  ECrowdWorkerResultApplyResult Prepare(
    const FCrowdWorkerPublishedBatch& Batch,
    FCrowdWorkerPreparedResultApply& OutPrepared);

  ECrowdWorkerResultApplyResult CommitPrepared(
    const FCrowdWorkerPreparedResultApply& Prepared);

  // Read-only final owner-barrier validation. This deliberately does not
  // latch a violation: the host may reject a prepared batch because its Mass
  // lifecycle changed while asynchronous work was in flight.
  ECrowdWorkerResultApplyResult ValidatePreparedState(
    const FCrowdWorkerPreparedResultApply& Prepared) const;

  // The host must call ValidatePreparedState immediately before its external
  // no-fail write. With the GT owner held, no proxy-observable state can
  // change between that validation and this commit.
  void CommitPreparedValidated(
    const FCrowdWorkerPreparedResultApply& Prepared);

  const FCrowdWorkerPresentationDiagnosticProxyState* Find(
    const FCrowdStableEntityRef& EntityRef) const;

  const FCrowdWorkerDomainProxyState* FindDomain(
    const FCrowdStableEntityRef& EntityRef,
    ECrowdWorkerField Field) const;

  TConstArrayView<FCrowdStableEntityRef> GetStableEntityView() const
  {
    return StableEntities;
  }

  int32 FindStableEntitySlot(
    const FCrowdStableEntityRef& EntityRef) const;

  const FCrowdWorkerResultApplyDirtyBatch* PeekDirtyBatch() const
  {
    return PendingDirtyBatch.IsValid() ? &PendingDirtyBatch : nullptr;
  }

  bool AcknowledgeDirtyBatch(uint64 PublishSequence);

  const FCrowdWorkerResultApplyMetrics& GetMetrics() const
  {
    return Metrics;
  }

private:
  void LatchViolation();
  ECrowdWorkerResultApplyResult ApplyPreparedNoFail(
    const FCrowdWorkerPreparedResultApply& Prepared);
  void RebuildPendingDirtyBatch();

  FCrowdWorkerContractLimits Limits;
  TArray<FCrowdStableEntityRef> StableEntities;
  TMap<FCrowdStableEntityRef, int32> StableEntitySlots;
  TMap<FCrowdStableEntityRef,
    FCrowdWorkerPresentationDiagnosticProxyState> ProxyStates;
  TMap<FCrowdWorkerDirtyStateKey,
    FCrowdWorkerDomainProxyState> DomainStates;
  TMap<FCrowdWorkerDirtyStateKey,
    FCrowdWorkerResultApplyDirtyRecord> PendingDirtyStates;
  FCrowdWorkerResultApplyDirtyBatch PendingDirtyBatch;
  FCrowdWorkerResultApplyMetrics Metrics;
  bool bInitialized = false;
};
