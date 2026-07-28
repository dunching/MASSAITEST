#include "MassCrowdLogistics.h"

namespace
{
  constexpr uint64 FnvOffset = 14695981039346656037ull;
  constexpr uint64 FnvPrime = 1099511628211ull;

  uint64 Fold(uint64 Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffull);
      Hash *= FnvPrime;
    }
    return Hash;
  }

  uint64 FoldRef(uint64 Hash, const FCrowdStableEntityRef& Ref)
  {
    Hash = Fold(Hash, Ref.ProviderId);
    Hash = Fold(Hash, Ref.StableEntityId);
    return Fold(Hash, Ref.LifecycleSerial);
  }
}

bool FCrowdLogisticsInventoryFact::IsValid() const
{
  return OwnerRef.IsValid() && Revision > 0 && Capacity >= 0
    && OnHand >= 0 && ReservedInbound >= 0 && ReservedOutbound >= 0
    && InTransit >= 0
    && OnHand + ReservedInbound <= Capacity
    && ReservedOutbound <= OnHand;
}

bool FCrowdLogisticsTaskFact::IsValid() const
{
  return TaskRef.IsValid() && SourceRef.IsValid() && SinkRef.IsValid()
    && Quantity > 0 && Revision > 0
    && static_cast<uint8>(State)
      <= static_cast<uint8>(ECrowdLogisticsTaskState::Requeued)
    && (CarrierRef.IsUnset() || CarrierRef.IsValid())
    && (CargoRef.IsUnset() || CargoRef.IsValid());
}

bool FCrowdLogisticsCargoFact::IsValid() const
{
  return CargoRef.IsValid() && TaskRef.IsValid()
    && SourceRef.IsValid() && SinkRef.IsValid()
    && (CarrierRef.IsUnset() || CarrierRef.IsValid())
    && Quantity > 0 && Revision > 0;
}

bool FCrowdLogisticsKernel::ChooseCarrier(
  const TConstArrayView<FCrowdStableEntityRef> Candidates,
  FCrowdStableEntityRef& OutCarrier)
{
  OutCarrier = {};
  if (Candidates.IsEmpty()) return false;
  TArray<FCrowdStableEntityRef> Sorted(Candidates);
  Sorted.Sort();
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    if (!Sorted[Index].IsValid()
      || (Index > 0 && Sorted[Index] == Sorted[Index - 1]))
      return false;
  }
  OutCarrier = Sorted[0];
  return true;
}

uint64 FCrowdLogisticsKernel::CalculatePatchHash(
  const FCrowdLogisticsPreparedPatch& Patch)
{
  uint64 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, Patch.CommitId);
  Hash = FoldRef(Hash, Patch.Task.TaskRef);
  Hash = FoldRef(Hash, Patch.Task.CarrierRef);
  Hash = FoldRef(Hash, Patch.Task.CargoRef);
  Hash = Fold(Hash, Patch.Task.Quantity);
  Hash = Fold(Hash, static_cast<uint8>(Patch.Task.State));
  Hash = Fold(Hash, Patch.Task.Revision);
  Hash = Fold(Hash, Patch.Source.OnHand);
  Hash = Fold(Hash, Patch.Source.ReservedOutbound);
  Hash = Fold(Hash, Patch.Source.InTransit);
  Hash = Fold(Hash, Patch.Source.Revision);
  Hash = Fold(Hash, Patch.Sink.OnHand);
  Hash = Fold(Hash, Patch.Sink.ReservedInbound);
  Hash = Fold(Hash, Patch.Sink.InTransit);
  Hash = Fold(Hash, Patch.Sink.Revision);
  if (Patch.bHasCargo)
  {
    Hash = FoldRef(Hash, Patch.Cargo.CargoRef);
    Hash = FoldRef(Hash, Patch.Cargo.CarrierRef);
    Hash = Fold(Hash, Patch.Cargo.Quantity);
    Hash = Fold(Hash, Patch.Cargo.Revision);
  }
  return Hash;
}

bool FCrowdLogisticsKernel::ValidateConservation(
  const TConstArrayView<FCrowdLogisticsInventoryFact> Inventories,
  const TConstArrayView<FCrowdLogisticsCargoFact> Cargo,
  const int64 ExpectedTotalQuantity)
{
  if (ExpectedTotalQuantity < 0) return false;
  int64 Total = 0;
  TSet<FCrowdStableEntityRef> InventoryRefs;
  TSet<FCrowdStableEntityRef> CargoRefs;
  for (const FCrowdLogisticsInventoryFact& Inventory : Inventories)
  {
    if (!Inventory.IsValid() || InventoryRefs.Contains(Inventory.OwnerRef))
      return false;
    InventoryRefs.Add(Inventory.OwnerRef);
    Total += Inventory.OnHand;
  }
  for (const FCrowdLogisticsCargoFact& Lot : Cargo)
  {
    if (!Lot.IsValid() || CargoRefs.Contains(Lot.CargoRef))
      return false;
    CargoRefs.Add(Lot.CargoRef);
    Total += Lot.Quantity;
  }
  return Total == ExpectedTotalQuantity;
}

bool FCrowdLogisticsTransactionStore::Initialize(
  const FCrowdLogisticsInventoryFact& InSource,
  const FCrowdLogisticsInventoryFact& InSink,
  const FCrowdLogisticsTaskFact& InTask)
{
  bInitialized = false;
  if (!InSource.IsValid() || !InSink.IsValid() || !InTask.IsValid()
    || InTask.SourceRef != InSource.OwnerRef
    || InTask.SinkRef != InSink.OwnerRef
    || InTask.State != ECrowdLogisticsTaskState::Created
    || !InTask.CarrierRef.IsUnset() || !InTask.CargoRef.IsUnset()
    || InSource.ReservedOutbound < InTask.Quantity
    || InSink.ReservedInbound < InTask.Quantity)
  {
    return false;
  }
  Source = InSource;
  Sink = InSink;
  Task = InTask;
  Cargo = {};
  AppliedCommitIds.Reset();
  DuplicateCount = 0;
  ExpectedTotalQuantity =
    static_cast<int64>(Source.OnHand) + static_cast<int64>(Sink.OnHand);
  bInitialized = true;
  return true;
}

ECrowdLogisticsPrepareResult FCrowdLogisticsTransactionStore::Prepare(
  const FCrowdLogisticsCommitRequest& Request,
  FCrowdLogisticsPreparedPatch& OutPatch) const
{
  OutPatch = {};
  if (!bInitialized || Request.CommitId == 0
    || Request.TaskRef != Task.TaskRef)
  {
    return ECrowdLogisticsPrepareResult::MissingFact;
  }
  if (AppliedCommitIds.Contains(Request.CommitId))
  {
    return ECrowdLogisticsPrepareResult::Duplicate;
  }
  if (Request.ExpectedTaskRevision != Task.Revision
    || Request.ExpectedSourceRevision != Source.Revision
    || Request.ExpectedSinkRevision != Sink.Revision)
  {
    return ECrowdLogisticsPrepareResult::Stale;
  }

  OutPatch.CommitId = Request.CommitId;
  OutPatch.Task = Task;
  OutPatch.Source = Source;
  OutPatch.Sink = Sink;
  OutPatch.Cargo = Cargo;
  OutPatch.bHasCargo = Cargo.IsValid();
  switch (Request.Kind)
  {
  case ECrowdLogisticsCommitKind::Claim:
    if ((Task.State != ECrowdLogisticsTaskState::Created
        && Task.State != ECrowdLogisticsTaskState::Requeued)
      || !Request.CarrierRef.IsValid())
    {
      return ECrowdLogisticsPrepareResult::InvalidTransition;
    }
    OutPatch.Task.CarrierRef = Request.CarrierRef;
    if (OutPatch.bHasCargo)
    {
      OutPatch.Task.State = ECrowdLogisticsTaskState::Picked;
      OutPatch.Cargo.CarrierRef = Request.CarrierRef;
      ++OutPatch.Cargo.Revision;
    }
    else
    {
      OutPatch.Task.State = ECrowdLogisticsTaskState::Claimed;
    }
    ++OutPatch.Task.Revision;
    break;
  case ECrowdLogisticsCommitKind::Pickup:
    if (Task.State != ECrowdLogisticsTaskState::Claimed
      || Request.CarrierRef != Task.CarrierRef
      || Source.ReservedOutbound < Task.Quantity)
    {
      return ECrowdLogisticsPrepareResult::InvalidTransition;
    }
    OutPatch.Source.OnHand -= Task.Quantity;
    OutPatch.Source.ReservedOutbound -= Task.Quantity;
    OutPatch.Source.InTransit += Task.Quantity;
    ++OutPatch.Source.Revision;
    OutPatch.Task.State = ECrowdLogisticsTaskState::Picked;
    OutPatch.Task.CargoRef = {5, Task.TaskRef.StableEntityId, 1};
    ++OutPatch.Task.Revision;
    OutPatch.Cargo = {
      OutPatch.Task.CargoRef, Task.TaskRef, Task.SourceRef, Task.SinkRef,
      Task.CarrierRef, Task.Quantity, 1};
    OutPatch.bHasCargo = true;
    break;
  case ECrowdLogisticsCommitKind::Deliver:
    if (Task.State != ECrowdLogisticsTaskState::Picked
      || !Cargo.IsValid() || Request.CarrierRef != Task.CarrierRef
      || Sink.ReservedInbound < Task.Quantity
      || Source.InTransit < Task.Quantity
      || Sink.OnHand + Task.Quantity > Sink.Capacity)
    {
      return ECrowdLogisticsPrepareResult::InvalidTransition;
    }
    OutPatch.Source.InTransit -= Task.Quantity;
    ++OutPatch.Source.Revision;
    OutPatch.Sink.OnHand += Task.Quantity;
    OutPatch.Sink.ReservedInbound -= Task.Quantity;
    ++OutPatch.Sink.Revision;
    OutPatch.Task.State = ECrowdLogisticsTaskState::Delivered;
    OutPatch.Task.CargoRef = {};
    ++OutPatch.Task.Revision;
    OutPatch.Cargo = {};
    OutPatch.bHasCargo = false;
    break;
  case ECrowdLogisticsCommitKind::Cancel:
    if (Task.State != ECrowdLogisticsTaskState::Created
      && Task.State != ECrowdLogisticsTaskState::Claimed
      && Task.State != ECrowdLogisticsTaskState::Requeued)
    {
      return ECrowdLogisticsPrepareResult::InvalidTransition;
    }
    OutPatch.Source.ReservedOutbound -= Task.Quantity;
    OutPatch.Sink.ReservedInbound -= Task.Quantity;
    if (OutPatch.Source.ReservedOutbound < 0
      || OutPatch.Sink.ReservedInbound < 0)
    {
      return ECrowdLogisticsPrepareResult::InvariantFailure;
    }
    ++OutPatch.Source.Revision;
    ++OutPatch.Sink.Revision;
    OutPatch.Task.State = ECrowdLogisticsTaskState::Cancelled;
    OutPatch.Task.CarrierRef = {};
    ++OutPatch.Task.Revision;
    break;
  case ECrowdLogisticsCommitKind::Requeue:
    if (Task.State != ECrowdLogisticsTaskState::Claimed
      && Task.State != ECrowdLogisticsTaskState::Picked)
    {
      return ECrowdLogisticsPrepareResult::InvalidTransition;
    }
    OutPatch.Task.State = ECrowdLogisticsTaskState::Requeued;
    OutPatch.Task.CarrierRef = {};
    ++OutPatch.Task.RetryCount;
    ++OutPatch.Task.Revision;
    if (OutPatch.bHasCargo)
    {
      OutPatch.Cargo.CarrierRef = {};
      ++OutPatch.Cargo.Revision;
    }
    break;
  default:
    return ECrowdLogisticsPrepareResult::InvalidTransition;
  }

  OutPatch.StableHash = FCrowdLogisticsKernel::CalculatePatchHash(OutPatch);
  OutPatch.bValid = ValidatePrepared(OutPatch);
  return OutPatch.bValid
    ? ECrowdLogisticsPrepareResult::Prepared
    : ECrowdLogisticsPrepareResult::InvariantFailure;
}

bool FCrowdLogisticsTransactionStore::ValidatePrepared(
  const FCrowdLogisticsPreparedPatch& Patch) const
{
  if (!bInitialized || Patch.CommitId == 0 || !Patch.Task.IsValid()
    || !Patch.Source.IsValid() || !Patch.Sink.IsValid()
    || (Patch.bHasCargo && !Patch.Cargo.IsValid())
    || Patch.StableHash != FCrowdLogisticsKernel::CalculatePatchHash(Patch))
  {
    return false;
  }
  TArray<FCrowdLogisticsInventoryFact> Inventories = {
    Patch.Source, Patch.Sink};
  TArray<FCrowdLogisticsCargoFact> CargoLots;
  if (Patch.bHasCargo)
  {
    CargoLots.Add(Patch.Cargo);
  }
  return FCrowdLogisticsKernel::ValidateConservation(
    Inventories, CargoLots, ExpectedTotalQuantity);
}

void FCrowdLogisticsTransactionStore::ApplyPrepared(
  const FCrowdLogisticsPreparedPatch& Patch)
{
  if (AppliedCommitIds.Contains(Patch.CommitId))
  {
    ++DuplicateCount;
    return;
  }
  check(ValidatePrepared(Patch));
  Source = Patch.Source;
  Sink = Patch.Sink;
  Task = Patch.Task;
  Cargo = Patch.Cargo;
  AppliedCommitIds.Add(Patch.CommitId);
}

bool FCrowdLogisticsTransactionStore::RetargetSink(
  const FCrowdLogisticsInventoryFact& NewSink,
  const uint64 CommitId)
{
  if (!bInitialized || CommitId == 0
    || AppliedCommitIds.Contains(CommitId)
    || !NewSink.IsValid()
    || (Task.State != ECrowdLogisticsTaskState::Requeued
      && Task.State != ECrowdLogisticsTaskState::Created)
    || Sink.OnHand != 0
    || Sink.ReservedInbound < Task.Quantity
    || NewSink.ReservedInbound + Task.Quantity > NewSink.Capacity)
  {
    return false;
  }
  FCrowdLogisticsInventoryFact NewSource = Source;
  FCrowdLogisticsInventoryFact PreviousSink = Sink;
  FCrowdLogisticsInventoryFact Replacement = NewSink;
  PreviousSink.ReservedInbound -= Task.Quantity;
  ++PreviousSink.Revision;
  Replacement.ReservedInbound += Task.Quantity;
  ++Replacement.Revision;
  FCrowdLogisticsTaskFact NewTask = Task;
  NewTask.SinkRef = Replacement.OwnerRef;
  ++NewTask.Revision;
  FCrowdLogisticsCargoFact NewCargo = Cargo;
  if (NewCargo.IsValid())
  {
    NewCargo.SinkRef = Replacement.OwnerRef;
    ++NewCargo.Revision;
  }
  TArray<FCrowdLogisticsInventoryFact> Inventories = {
    NewSource, Replacement};
  TArray<FCrowdLogisticsCargoFact> CargoLots;
  if (NewCargo.IsValid())
  {
    CargoLots.Add(NewCargo);
  }
  if (!PreviousSink.IsValid() || !NewSource.IsValid()
    || !Replacement.IsValid() || !NewTask.IsValid()
    || (Cargo.IsValid() && !NewCargo.IsValid())
    || !FCrowdLogisticsKernel::ValidateConservation(
      Inventories, CargoLots, ExpectedTotalQuantity))
  {
    return false;
  }
  Source = NewSource;
  Sink = Replacement;
  Task = NewTask;
  Cargo = NewCargo;
  AppliedCommitIds.Add(CommitId);
  return true;
}
