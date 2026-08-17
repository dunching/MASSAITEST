#pragma once

#include "CoreMinimal.h"
#include "MassCrowdAgentFacts.h"

enum class ECrowdLogisticsTaskState : uint8
{
  Created = 0,
  Claimed,
  Picked,
  Delivered,
  Cancelled,
  Requeued
};

enum class ECrowdLogisticsCommitKind : uint8
{
  Claim = 0,
  Pickup,
  Deliver,
  Cancel,
  Requeue
};

struct MASSCROWDRUNTIME_API FCrowdLogisticsInventoryFact
{
  FCrowdStableEntityRef OwnerRef;
  int32 OnHand = 0;
  int32 ReservedInbound = 0;
  int32 ReservedOutbound = 0;
  int32 InTransit = 0;
  int32 Capacity = 0;
  uint32 Revision = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdLogisticsTaskFact
{
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef SourceRef;
  FCrowdStableEntityRef SinkRef;
  FCrowdStableEntityRef CarrierRef;
  FCrowdStableEntityRef CargoRef;
  int32 Quantity = 0;
  ECrowdLogisticsTaskState State = ECrowdLogisticsTaskState::Created;
  uint32 Revision = 0;
  uint32 RetryCount = 0;

  bool IsValid() const;
};

struct MASSCROWDRUNTIME_API FCrowdLogisticsCargoFact
{
  FCrowdStableEntityRef CargoRef;
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef SourceRef;
  FCrowdStableEntityRef SinkRef;
  FCrowdStableEntityRef CarrierRef;
  int32 Quantity = 0;
  uint32 Revision = 0;

  bool IsValid() const;
};

struct FCrowdLogisticsCommitRequest
{
  uint64 CommitId = 0;
  ECrowdLogisticsCommitKind Kind = ECrowdLogisticsCommitKind::Claim;
  FCrowdStableEntityRef TaskRef;
  FCrowdStableEntityRef CarrierRef;
  uint32 ExpectedTaskRevision = 0;
  uint32 ExpectedSourceRevision = 0;
  uint32 ExpectedSinkRevision = 0;
};

struct FCrowdLogisticsPreparedPatch
{
  uint64 CommitId = 0;
  FCrowdLogisticsTaskFact Task;
  FCrowdLogisticsInventoryFact Source;
  FCrowdLogisticsInventoryFact Sink;
  FCrowdLogisticsCargoFact Cargo;
  bool bHasCargo = false;
  uint64 StableHash = 0;
  bool bValid = false;
};

enum class ECrowdLogisticsPrepareResult : uint8
{
  Prepared = 0,
  Duplicate,
  Stale,
  InvalidTransition,
  InvariantFailure,
  MissingFact
};

class MASSCROWDRUNTIME_API ICrowdLogisticsCommitAdapter
{
public:
  virtual ~ICrowdLogisticsCommitAdapter() = default;

  virtual ECrowdLogisticsPrepareResult Prepare(
    const FCrowdLogisticsCommitRequest& Request,
    FCrowdLogisticsPreparedPatch& OutPatch) const = 0;
  virtual bool ValidatePrepared(
    const FCrowdLogisticsPreparedPatch& Patch) const = 0;
  virtual void ApplyPrepared(
    const FCrowdLogisticsPreparedPatch& Patch) = 0;
};

class MASSCROWDRUNTIME_API FCrowdLogisticsKernel
{
public:
  static bool ChooseCarrier(
    TConstArrayView<FCrowdStableEntityRef> Candidates,
    FCrowdStableEntityRef& OutCarrier);
  static uint64 CalculatePatchHash(
    const FCrowdLogisticsPreparedPatch& Patch);
  static bool ValidateConservation(
    TConstArrayView<FCrowdLogisticsInventoryFact> Inventories,
    TConstArrayView<FCrowdLogisticsCargoFact> Cargo,
    int64 ExpectedTotalQuantity);
};

class MASSCROWDRUNTIME_API FCrowdLogisticsTransactionStore final
  : public ICrowdLogisticsCommitAdapter
{
public:
  bool Initialize(
    const FCrowdLogisticsInventoryFact& InSource,
    const FCrowdLogisticsInventoryFact& InSink,
    const FCrowdLogisticsTaskFact& InTask);

  virtual ECrowdLogisticsPrepareResult Prepare(
    const FCrowdLogisticsCommitRequest& Request,
    FCrowdLogisticsPreparedPatch& OutPatch) const override;
  virtual bool ValidatePrepared(
    const FCrowdLogisticsPreparedPatch& Patch) const override;
  virtual void ApplyPrepared(
    const FCrowdLogisticsPreparedPatch& Patch) override;

  bool RetargetSink(
    const FCrowdLogisticsInventoryFact& NewSink,
    uint64 CommitId);
  bool IsInitialized() const { return bInitialized; }
  bool HasAppliedCommit(uint64 CommitId) const
  {
    return AppliedCommitIds.Contains(CommitId);
  }
  int32 GetDuplicateCount() const { return DuplicateCount; }
  int64 GetExpectedTotalQuantity() const { return ExpectedTotalQuantity; }
  const FCrowdLogisticsInventoryFact& GetSource() const { return Source; }
  const FCrowdLogisticsInventoryFact& GetSink() const { return Sink; }
  const FCrowdLogisticsTaskFact& GetTask() const { return Task; }
  const FCrowdLogisticsCargoFact& GetCargo() const { return Cargo; }

private:
  FCrowdLogisticsInventoryFact Source;
  FCrowdLogisticsInventoryFact Sink;
  FCrowdLogisticsTaskFact Task;
  FCrowdLogisticsCargoFact Cargo;
  TSet<uint64> AppliedCommitIds;
  int64 ExpectedTotalQuantity = 0;
  int32 DuplicateCount = 0;
  bool bInitialized = false;
};
