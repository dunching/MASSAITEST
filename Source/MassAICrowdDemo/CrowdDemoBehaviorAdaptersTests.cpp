#include "Mass/CrowdDemoBehaviorAdapters.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBehaviorAdaptersLogisticsCombatTest,
  "CrowdDemo.BehaviorAdapters.LogisticsCombat",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
  FCrowdAgentFacts MakeHostAgent(const uint32 FactionKey)
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = {1, 10, 1};
    Facts.FactionKey = FactionKey;
    Facts.ActiveBehavior = ECrowdActiveBehavior::Idle;
    Facts.CapabilitySet.Add(ECrowdCapability::Haul);
    Facts.CapabilitySet.Add(ECrowdCapability::Attack);
    return Facts;
  }

  FCrowdRuntimeBehaviorContext MakeContext(
    const FCrowdAgentFacts& Facts,
    const ECrowdActiveBehavior Behavior,
    const int64 FixedStep,
    const uint32 Revision)
  {
    FCrowdRuntimeBehaviorContext Context;
    Context.AgentFacts = Facts;
    Context.RequestedBehavior = Behavior;
    Context.FixedStepIndex = FixedStep;
    Context.TransitionRevision = Revision;
    Context.TaskRef = {2, 500, 1};
    Context.TargetRef = {3, 900, 2};
    Context.TargetLocation = FVector(500.0, 100.0, 0.0);
    Context.ObjectiveKey = 7;
    Context.MovementProfileKey = 11;
    Context.InteractionPayloadKey = 42;
    Context.InteractionQuantity = 3;
    Context.bInteractionReady = true;
    return Context;
  }
}

bool FCrowdDemoBehaviorAdaptersLogisticsCombatTest::RunTest(const FString& Parameters)
{
  FCrowdDemoBehaviorProviderSet Providers;
  FCrowdDemoBusinessCommitLedger Ledger;
  FCrowdAgentFacts Facts = MakeHostAgent(1);

  FCrowdRuntimeBehaviorContext Pickup = MakeContext(
    Facts, ECrowdActiveBehavior::HaulPickup, 10, 1);
  FCrowdRuntimeBehaviorOutput PickupOutput;
  TestTrue(TEXT("haul pickup evaluates through provider set"),
    FCrowdRuntimeBehaviorTransition::Evaluate(Providers, Pickup, PickupOutput));
  TestEqual(TEXT("pickup intent"), PickupOutput.InteractionIntent.Kind,
    ECrowdInteractionIntentKind::Pickup);
  TestEqual(TEXT("pickup commit request"), PickupOutput.BusinessCommitRequest.Kind,
    ECrowdBusinessCommitKind::CargoPickup);
  TestTrue(TEXT("pickup transition commits"),
    FCrowdRuntimeBehaviorTransition::Commit(PickupOutput, Facts));
  TestEqual(TEXT("pickup business commit applies"),
    Ledger.Apply(PickupOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("cargo carrier set"), Ledger.GetCargoCarrier(500), 10ull);

  FCrowdRuntimeBehaviorContext Deliver = MakeContext(
    Facts, ECrowdActiveBehavior::HaulDeliver, 20, 2);
  FCrowdRuntimeBehaviorOutput DeliverOutput;
  TestTrue(TEXT("haul deliver evaluates through same provider set"),
    FCrowdRuntimeBehaviorTransition::Evaluate(Providers, Deliver, DeliverOutput));
  TestEqual(TEXT("deliver intent"), DeliverOutput.InteractionIntent.Kind,
    ECrowdInteractionIntentKind::Deliver);
  TestEqual(TEXT("deliver business commit applies"),
    Ledger.Apply(DeliverOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("cargo released"), Ledger.GetCargoCarrier(500), 0ull);
  TestEqual(TEXT("pickup count"), Ledger.GetPickupCount(), 1);
  TestEqual(TEXT("delivery count"), Ledger.GetDeliveryCount(), 1);

  FCrowdRuntimeBehaviorContext Attack = MakeContext(
    Facts, ECrowdActiveBehavior::Attack, 30, 3);
  FCrowdRuntimeBehaviorOutput AttackOutput;
  TestTrue(TEXT("attack evaluates through same provider set"),
    FCrowdRuntimeBehaviorTransition::Evaluate(Providers, Attack, AttackOutput));
  TestEqual(TEXT("attack intent"), AttackOutput.InteractionIntent.Kind,
    ECrowdInteractionIntentKind::Attack);
  TestEqual(TEXT("combat hit applies once"),
    Ledger.Apply(AttackOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("combat quantity applied"),
    Ledger.GetCombatHitQuantity(900), 3);

  FCrowdRuntimeBehaviorOutput RollbackReplayOutput;
  TestTrue(TEXT("rollback replay re-evaluates deterministically"),
    FCrowdRuntimeBehaviorTransition::Evaluate(Providers, Attack, RollbackReplayOutput));
  TestEqual(TEXT("rollback replay keeps commit id"),
    RollbackReplayOutput.BusinessCommitRequest.CommitId,
    AttackOutput.BusinessCommitRequest.CommitId);
  TestEqual(TEXT("rollback replay is idempotent"),
    Ledger.Apply(RollbackReplayOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);
  TestEqual(TEXT("combat quantity not duplicated"),
    Ledger.GetCombatHitQuantity(900), 3);

  FCrowdDemoHitFact HitFact;
  HitFact.HitEventId = 7001;
  HitFact.ApplyFixedStep = 40;
  HitFact.SourceAgentId = 10;
  HitFact.SourceLifecycleSerial = 1;
  HitFact.TargetAgentId = 900;
  HitFact.TargetLifecycleSerial = 2;
  HitFact.HitPosition = FVector(600.0, 0.0, 60.0);
  HitFact.HitDirection = FVector::ForwardVector;
  HitFact.Damage = 25.0f;
  HitFact.HitFlashProfileKey = 9;
  FCrowdDemoCombatBehaviorAdapter CombatAdapter;
  FCrowdRuntimeBehaviorContext HitContext;
  TestTrue(TEXT("real Demo HitFact maps to shared behavior context"),
    CombatAdapter.BuildContextFromHitFact(HitFact, Facts, 5, HitContext));
  FCrowdRuntimeBehaviorOutput HitOutput;
  TestTrue(TEXT("real Demo HitFact evaluates through shared behavior output"),
    FCrowdRuntimeBehaviorTransition::Evaluate(
      CombatAdapter, HitContext, HitOutput));
  TestEqual(TEXT("HitEventId becomes idempotency key"),
    HitOutput.BusinessCommitRequest.CommitId, HitFact.HitEventId);

  TArray<FCrowdDemoCombatAgentState> CombatAgents;
  FCrowdDemoCombatAgentState TargetCombat;
  TargetCombat.AgentId = 900;
  TargetCombat.LifecycleSerial = 2;
  CombatAgents.Add(TargetCombat);
  FCrowdDemoHitResponseSummary HitSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    40, 4.0f, MakeArrayView(&HitFact, 1), {}, CombatAgents, HitSummary);
  TestEqual(TEXT("existing Combat kernel applies mapped hit"),
    HitSummary.AppliedHitCount, 1);
  TestEqual(TEXT("existing Combat kernel applies damage"),
    CombatAgents[0].Health, 75.0f);
  TestEqual(TEXT("mapped real hit business commit applies"),
    Ledger.Apply(HitOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("mapped real hit quantity recorded"),
    Ledger.GetCombatHitQuantity(900), 28);

  FCrowdDemoHitResponseSummary ReplayHitSummary;
  FCrowdDemoCombatStateKernel::ResolveHitFacts(
    40, 4.0f, MakeArrayView(&HitFact, 1), {}, CombatAgents, ReplayHitSummary);
  TestEqual(TEXT("existing Combat kernel rejects replay duplicate"),
    ReplayHitSummary.DuplicateHitCount, 1);
  TestEqual(TEXT("shared ledger rejects replay duplicate"),
    Ledger.Apply(HitOutput.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);
  TestEqual(TEXT("replay does not apply damage twice"),
    CombatAgents[0].Health, 75.0f);

  FCrowdAgentFacts OtherFaction = MakeHostAgent(999);
  FCrowdRuntimeBehaviorContext OtherFactionAttack = MakeContext(
    OtherFaction, ECrowdActiveBehavior::Attack, 31, 4);
  FCrowdRuntimeBehaviorOutput OtherFactionOutput;
  TestTrue(TEXT("different faction with capability can attack"),
    FCrowdRuntimeBehaviorTransition::Evaluate(
      Providers, OtherFactionAttack, OtherFactionOutput));

  OtherFaction.CapabilitySet.Remove(ECrowdCapability::Attack);
  OtherFactionAttack.AgentFacts = OtherFaction;
  TestFalse(TEXT("faction does not grant missing capability"),
    FCrowdRuntimeBehaviorTransition::Evaluate(
      Providers, OtherFactionAttack, OtherFactionOutput));
  return true;
}

#endif
