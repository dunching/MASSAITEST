#include "Mass/CrowdDemoBehaviorAdapters.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBehaviorAdaptersLogisticsCombatTest,
  "CrowdDemo.BehaviorAdapters.LogisticsCombat",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
  FCrowdBusinessCommitRequest MakeRequest(
    const ECrowdBusinessCommitKind Kind,
    const uint64 CommitId,
    const int32 Quantity = 1)
  {
    FCrowdBusinessCommitRequest Request;
    Request.Kind = Kind;
    Request.CommitId = CommitId;
    Request.FixedStepIndex = static_cast<int64>(CommitId);
    Request.TransitionRevision = static_cast<uint32>(CommitId);
    Request.AgentRef = {1, 10, 1};
    Request.TaskRef = {2, 500, 1};
    Request.TargetRef = {3, 900, 2};
    Request.PayloadKey = 42;
    Request.Quantity = Quantity;
    return Request;
  }
}

bool FCrowdDemoBehaviorAdaptersLogisticsCombatTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoBusinessCommitLedger Ledger;
  const FCrowdBusinessCommitRequest Pickup =
    MakeRequest(ECrowdBusinessCommitKind::CargoPickup, 10);
  TestEqual(TEXT("pickup applies"), Ledger.Apply(Pickup),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("pickup replay is idempotent"), Ledger.Apply(Pickup),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);
  TestEqual(TEXT("cargo carrier is recorded"),
    Ledger.GetCargoCarrier(500), 10ull);

  const FCrowdBusinessCommitRequest Deliver =
    MakeRequest(ECrowdBusinessCommitKind::CargoDeliver, 20);
  TestEqual(TEXT("delivery applies"), Ledger.Apply(Deliver),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("cargo is released"), Ledger.GetCargoCarrier(500), 0ull);
  TestEqual(TEXT("pickup count"), Ledger.GetPickupCount(), 1);
  TestEqual(TEXT("delivery count"), Ledger.GetDeliveryCount(), 1);

  const FCrowdBusinessCommitRequest Attack =
    MakeRequest(ECrowdBusinessCommitKind::CombatHit, 30, 25);
  TestEqual(TEXT("combat request applies"), Ledger.Apply(Attack),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("combat replay is idempotent"), Ledger.Apply(Attack),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);
  TestEqual(TEXT("combat quantity is recorded"),
    Ledger.GetCombatHitQuantity(900), 25);

  FCrowdBusinessCommitRequest Invalid = Attack;
  Invalid.CommitId = 0;
  TestEqual(TEXT("invalid request is rejected"), Ledger.Apply(Invalid),
    ECrowdDemoBusinessCommitAcceptResult::Rejected);
  return true;
}

#endif
