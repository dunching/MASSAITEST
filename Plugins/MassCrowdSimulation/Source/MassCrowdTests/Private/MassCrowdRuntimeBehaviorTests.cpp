#include "MassCrowdRuntimeBehavior.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdRuntimeBehaviorCommitContractTest,
  "MassCrowd.RuntimeBehavior.CommitContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdRuntimeBehaviorCommitContractTest::RunTest(
  const FString& Parameters)
{
  FCrowdRuntimeBehaviorContext Context;
  Context.AgentFacts.StableEntityRef = {1, 10, 1};
  Context.TaskRef = {2, 20, 1};
  Context.TargetRef = {3, 30, 1};
  Context.FixedStepIndex = 100;
  Context.TransitionRevision = 7;
  Context.InteractionPayloadKey = 9;
  Context.InteractionQuantity = 3;

  const uint64 First = FCrowdBehaviorCommitId::Make(
    ECrowdBusinessCommitKind::CargoPickup, Context);
  const uint64 Replay = FCrowdBehaviorCommitId::Make(
    ECrowdBusinessCommitKind::CargoPickup, Context);
  TestTrue(TEXT("commit id is non-zero"), First != 0);
  TestEqual(TEXT("commit id is deterministic"), Replay, First);

  Context.AgentFacts.FactionKey = 999;
  TestEqual(TEXT("faction is not capability or commit identity"),
    FCrowdBehaviorCommitId::Make(
      ECrowdBusinessCommitKind::CargoPickup, Context), First);

  Context.ExternalCommitId = 7001;
  TestEqual(TEXT("external event identity is preserved"),
    FCrowdBehaviorCommitId::Make(
      ECrowdBusinessCommitKind::CombatHit, Context), 7001ull);

  FCrowdBusinessCommitRequest Valid;
  Valid.Kind = ECrowdBusinessCommitKind::CombatHit;
  Valid.CommitId = 7001;
  Valid.FixedStepIndex = 100;
  Valid.TransitionRevision = 7;
  Valid.AgentRef = Context.AgentFacts.StableEntityRef;
  Valid.TargetRef = Context.TargetRef;
  Valid.PayloadKey = 9;
  Valid.Quantity = 3;
  TestTrue(TEXT("complete business request is valid"), Valid.IsValid());
  Valid.Quantity = 0;
  TestFalse(TEXT("zero quantity is rejected"), Valid.IsValid());
  return true;
}

#endif
