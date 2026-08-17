#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "MassCrowdLogistics.h"

namespace
{
  class FLogisticsFixtureAdapter final : public ICrowdLogisticsCommitAdapter
  {
  public:
    FCrowdLogisticsInventoryFact Source{
      {3, 1, 1}, 10, 0, 4, 0, 20, 1};
    FCrowdLogisticsInventoryFact Sink{
      {3, 2, 1}, 0, 4, 0, 0, 20, 1};
    FCrowdLogisticsTaskFact Task{
      {4, 1, 1}, Source.OwnerRef, Sink.OwnerRef,
      {}, {}, 4, ECrowdLogisticsTaskState::Created, 1, 0};
    FCrowdLogisticsCargoFact Cargo;
    TSet<uint64> AppliedCommitIds;
    int32 DuplicateCount = 0;

    virtual ECrowdLogisticsPrepareResult Prepare(
      const FCrowdLogisticsCommitRequest& Request,
      FCrowdLogisticsPreparedPatch& OutPatch) const override
    {
      OutPatch = {};
      if (AppliedCommitIds.Contains(Request.CommitId))
        return ECrowdLogisticsPrepareResult::Duplicate;
      if (Request.CommitId == 0 || Request.TaskRef != Task.TaskRef)
        return ECrowdLogisticsPrepareResult::MissingFact;
      if (Request.ExpectedTaskRevision != Task.Revision
        || Request.ExpectedSourceRevision != Source.Revision
        || Request.ExpectedSinkRevision != Sink.Revision)
        return ECrowdLogisticsPrepareResult::Stale;
      OutPatch.CommitId = Request.CommitId;
      OutPatch.Task = Task;
      OutPatch.Source = Source;
      OutPatch.Sink = Sink;
      OutPatch.Cargo = Cargo;
      switch (Request.Kind)
      {
      case ECrowdLogisticsCommitKind::Claim:
        if (Task.State != ECrowdLogisticsTaskState::Created
          && Task.State != ECrowdLogisticsTaskState::Requeued)
          return ECrowdLogisticsPrepareResult::InvalidTransition;
        if (!Request.CarrierRef.IsValid())
          return ECrowdLogisticsPrepareResult::InvariantFailure;
        OutPatch.Task.CarrierRef = Request.CarrierRef;
        OutPatch.Task.State = ECrowdLogisticsTaskState::Claimed;
        ++OutPatch.Task.Revision;
        break;
      case ECrowdLogisticsCommitKind::Pickup:
        if (Task.State != ECrowdLogisticsTaskState::Claimed
          || Request.CarrierRef != Task.CarrierRef
          || Source.ReservedOutbound < Task.Quantity)
          return ECrowdLogisticsPrepareResult::InvalidTransition;
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
          || Source.InTransit < Task.Quantity)
          return ECrowdLogisticsPrepareResult::InvalidTransition;
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
      default:
        return ECrowdLogisticsPrepareResult::InvalidTransition;
      }
      OutPatch.StableHash =
        FCrowdLogisticsKernel::CalculatePatchHash(OutPatch);
      OutPatch.bValid = ValidatePrepared(OutPatch);
      return OutPatch.bValid
        ? ECrowdLogisticsPrepareResult::Prepared
        : ECrowdLogisticsPrepareResult::InvariantFailure;
    }

    virtual bool ValidatePrepared(
      const FCrowdLogisticsPreparedPatch& Patch) const override
    {
      return Patch.CommitId != 0 && Patch.Task.IsValid()
        && Patch.Source.IsValid() && Patch.Sink.IsValid()
        && (!Patch.bHasCargo || Patch.Cargo.IsValid())
        && Patch.StableHash
          == FCrowdLogisticsKernel::CalculatePatchHash(Patch);
    }

    virtual void ApplyPrepared(
      const FCrowdLogisticsPreparedPatch& Patch) override
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
  };

  FCrowdLogisticsCommitRequest RequestFor(
    const FLogisticsFixtureAdapter& Adapter,
    const uint64 Id,
    const ECrowdLogisticsCommitKind Kind,
    const FCrowdStableEntityRef Carrier)
  {
    return {
      Id, Kind, Adapter.Task.TaskRef, Carrier,
      Adapter.Task.Revision, Adapter.Source.Revision, Adapter.Sink.Revision};
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdLogisticsTransactionsTest,
  "MassCrowd.Runtime.Logistics.TransactionsAndConservation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdLogisticsTransactionsTest::RunTest(
  const FString& Parameters)
{
  FLogisticsFixtureAdapter Adapter;
  TArray<FCrowdStableEntityRef> Candidates = {
    {1, 20, 1}, {1, 2, 1}, {1, 10, 1}};
  FCrowdStableEntityRef Carrier;
  TestTrue(TEXT("stable carrier competition resolves"),
    FCrowdLogisticsKernel::ChooseCarrier(Candidates, Carrier));
  TestTrue(TEXT("lowest stable ref wins"), Carrier == Candidates[1]);
  Algo::Reverse(Candidates);
  FCrowdStableEntityRef ReverseCarrier;
  TestTrue(TEXT("competition ignores input order"),
    FCrowdLogisticsKernel::ChooseCarrier(Candidates, ReverseCarrier)
      && ReverseCarrier == Carrier);

  uint64 CommitId = 1;
  for (const ECrowdLogisticsCommitKind Kind : {
    ECrowdLogisticsCommitKind::Claim,
    ECrowdLogisticsCommitKind::Pickup,
    ECrowdLogisticsCommitKind::Deliver})
  {
    FCrowdLogisticsPreparedPatch Patch;
    const FCrowdLogisticsCommitRequest Request =
      RequestFor(Adapter, CommitId++, Kind, Carrier);
    TestTrue(TEXT("transaction prepares"),
      Adapter.Prepare(Request, Patch)
        == ECrowdLogisticsPrepareResult::Prepared);
    const int32 BeforeSource = Adapter.Source.OnHand;
    const int32 BeforeSink = Adapter.Sink.OnHand;
    Adapter.ApplyPrepared(Patch);
    Adapter.ApplyPrepared(Patch);
    TestEqual(TEXT("duplicate commit is observed once"),
      Adapter.DuplicateCount, static_cast<int32>(CommitId - 1));
    TestTrue(TEXT("duplicate does not apply inventory twice"),
      Kind == ECrowdLogisticsCommitKind::Claim
        || Adapter.Source.OnHand == Patch.Source.OnHand);
    TestTrue(TEXT("authoritative inventory follows prepared patch"),
      Adapter.Source.OnHand == Patch.Source.OnHand
        && Adapter.Sink.OnHand == Patch.Sink.OnHand);
  }
  TArray<FCrowdLogisticsInventoryFact> Inventories = {
    Adapter.Source, Adapter.Sink};
  TArray<FCrowdLogisticsCargoFact> Cargo;
  if (Adapter.Cargo.IsValid()) Cargo.Add(Adapter.Cargo);
  TestTrue(TEXT("source cargo sink quantity is conserved"),
    FCrowdLogisticsKernel::ValidateConservation(
      Inventories, Cargo, 10));

  FCrowdLogisticsPreparedPatch StalePatch;
  FCrowdLogisticsCommitRequest Stale =
    RequestFor(Adapter, 99, ECrowdLogisticsCommitKind::Claim, Carrier);
  --Stale.ExpectedTaskRevision;
  TestTrue(TEXT("stale revision is rejected before mutation"),
    Adapter.Prepare(Stale, StalePatch)
      == ECrowdLogisticsPrepareResult::Stale);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdLogisticsRecoveryStoreTest,
  "MassCrowd.Runtime.Logistics.RecoveryStore",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdLogisticsRecoveryStoreTest::RunTest(
  const FString& Parameters)
{
  FCrowdLogisticsTransactionStore Store;
  const FCrowdLogisticsInventoryFact Source{
    {3, 1, 1}, 10, 0, 4, 0, 20, 1};
  const FCrowdLogisticsInventoryFact Sink{
    {3, 2, 1}, 0, 4, 0, 0, 20, 1};
  const FCrowdLogisticsTaskFact Task{
    {4, 1, 1}, Source.OwnerRef, Sink.OwnerRef,
    {}, {}, 4, ECrowdLogisticsTaskState::Created, 1, 0};
  TestTrue(TEXT("transaction store initializes"),
    Store.Initialize(Source, Sink, Task));

  const FCrowdStableEntityRef FirstCarrier{1, 1, 1};
  const FCrowdStableEntityRef RecoveryCarrier{1, 2, 1};
  auto Commit = [this, &Store](
    const uint64 CommitId,
    const ECrowdLogisticsCommitKind Kind,
    const FCrowdStableEntityRef Carrier)
  {
    const FCrowdLogisticsCommitRequest Request{
      CommitId, Kind, Store.GetTask().TaskRef, Carrier,
      Store.GetTask().Revision, Store.GetSource().Revision,
      Store.GetSink().Revision};
    FCrowdLogisticsPreparedPatch Patch;
    const bool bPrepared = Store.Prepare(Request, Patch)
      == ECrowdLogisticsPrepareResult::Prepared;
    TestTrue(TEXT("recovery transaction prepares"), bPrepared);
    if (bPrepared)
    {
      Store.ApplyPrepared(Patch);
    }
    return bPrepared;
  };

  TestTrue(TEXT("first carrier claims"),
    Commit(1, ECrowdLogisticsCommitKind::Claim, FirstCarrier));
  TestTrue(TEXT("first carrier picks up"),
    Commit(2, ECrowdLogisticsCommitKind::Pickup, FirstCarrier));
  TestTrue(TEXT("death requeues in-transit cargo"),
    Commit(3, ECrowdLogisticsCommitKind::Requeue, FirstCarrier));
  TestTrue(TEXT("in-transit cargo loses dead carrier"),
    Store.GetCargo().IsValid()
      && Store.GetCargo().CarrierRef.IsUnset()
      && Store.GetTask().State == ECrowdLogisticsTaskState::Requeued);

  FCrowdLogisticsInventoryFact FallbackSink{
    {3, 3, 1}, 0, 0, 0, 0, 20, 1};
  TestTrue(TEXT("destroyed sink retargets to fallback"),
    Store.RetargetSink(FallbackSink, 4));
  TestTrue(TEXT("fallback owns inbound reservation"),
    Store.GetTask().SinkRef == FallbackSink.OwnerRef
      && Store.GetSink().ReservedInbound == Task.Quantity
      && Store.GetCargo().SinkRef == FallbackSink.OwnerRef);
  TestTrue(TEXT("second carrier recovers cargo"),
    Commit(5, ECrowdLogisticsCommitKind::Claim, RecoveryCarrier));
  TestTrue(TEXT("recovered cargo remains picked"),
    Store.GetTask().State == ECrowdLogisticsTaskState::Picked
      && Store.GetCargo().CarrierRef == RecoveryCarrier);
  TestTrue(TEXT("recovery carrier delivers"),
    Commit(6, ECrowdLogisticsCommitKind::Deliver, RecoveryCarrier));
  TestTrue(TEXT("quantity remains conserved"),
    Store.GetSource().OnHand == 6
      && Store.GetSink().OnHand == 4
      && !Store.GetCargo().IsValid());

  FCrowdLogisticsPreparedPatch DuplicatePatch;
  const FCrowdLogisticsCommitRequest Duplicate{
    6, ECrowdLogisticsCommitKind::Deliver, Store.GetTask().TaskRef,
    RecoveryCarrier, Store.GetTask().Revision, Store.GetSource().Revision,
    Store.GetSink().Revision};
  TestTrue(TEXT("replayed commit is duplicate"),
    Store.Prepare(Duplicate, DuplicatePatch)
      == ECrowdLogisticsPrepareResult::Duplicate);
  return true;
}
