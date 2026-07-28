#include "MassCrowdRuntimeBehavior.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdRuntimeBehaviorTransitionsTest,
  "MassCrowd.RuntimeBehavior.Transitions",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
  FCrowdAgentFacts MakeBehaviorFacts()
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = {1, 10, 1};
    Facts.FactionKey = 77;
    Facts.ActiveBehavior = ECrowdActiveBehavior::Idle;
    Facts.CapabilitySet.Add(ECrowdCapability::Wander);
    Facts.CapabilitySet.Add(ECrowdCapability::MoveTo);
    Facts.CapabilitySet.Add(ECrowdCapability::Pursue);
    Facts.CapabilitySet.Add(ECrowdCapability::Guard);
    Facts.CapabilitySet.Add(ECrowdCapability::Flee);
    return Facts;
  }
}

bool FCrowdRuntimeBehaviorTransitionsTest::RunTest(const FString& Parameters)
{
  FCrowdRuntimeBasicBehaviorProvider Provider;
  FCrowdAgentFacts Facts = MakeBehaviorFacts();
  const FCrowdStableEntityRef TargetRef{1, 99, 4};
  const ECrowdActiveBehavior Behaviors[] = {
    ECrowdActiveBehavior::Wander,
    ECrowdActiveBehavior::MoveTo,
    ECrowdActiveBehavior::Pursue,
    ECrowdActiveBehavior::Guard,
    ECrowdActiveBehavior::Flee};

  uint32 Revision = 1;
  for (const ECrowdActiveBehavior Behavior : Behaviors)
  {
    FCrowdRuntimeBehaviorContext Context;
    Context.AgentFacts = Facts;
    Context.RequestedBehavior = Behavior;
    Context.FixedStepIndex = 100 + Revision;
    Context.TransitionRevision = Revision++;
    Context.TargetRef = TargetRef;
    Context.TargetLocation = FVector(100.0, 200.0, 30.0);
    Context.ObjectiveKey = 5;
    Context.MovementProfileKey = 9;
    FCrowdRuntimeBehaviorOutput Output;
    TestTrue(TEXT("supported transition evaluates"),
      FCrowdRuntimeBehaviorTransition::Evaluate(Provider, Context, Output));
    TestEqual(TEXT("behavior output matches request"), Output.Behavior, Behavior);
    TestEqual(TEXT("movement profile is explicit"), Output.MovementProfileKey, 9u);
    TestTrue(TEXT("target is explicit"), Output.Target.IsValid());
    TestTrue(TEXT("objective is explicit"), Output.Objective.IsValid());
    TestTrue(TEXT("interaction intent is explicit"), Output.InteractionIntent.IsValid());
    TestEqual(TEXT("basic behaviors do not emit business commit"),
      Output.BusinessCommitRequest.Kind, ECrowdBusinessCommitKind::None);
    TestTrue(TEXT("transition commits to AgentFacts"),
      FCrowdRuntimeBehaviorTransition::Commit(Output, Facts));
    TestEqual(TEXT("AgentFacts active behavior changed"), Facts.ActiveBehavior, Behavior);
  }

  FCrowdAgentFacts MissingCapability = MakeBehaviorFacts();
  MissingCapability.CapabilitySet.Remove(ECrowdCapability::Flee);
  FCrowdRuntimeBehaviorContext Rejected;
  Rejected.AgentFacts = MissingCapability;
  Rejected.RequestedBehavior = ECrowdActiveBehavior::Flee;
  Rejected.FixedStepIndex = 200;
  Rejected.TransitionRevision = 20;
  Rejected.TargetRef = TargetRef;
  Rejected.TargetLocation = FVector(1.0, 2.0, 3.0);
  Rejected.ObjectiveKey = 1;
  Rejected.MovementProfileKey = 1;
  FCrowdRuntimeBehaviorOutput Output;
  TestFalse(TEXT("missing capability rejects transition"),
    FCrowdRuntimeBehaviorTransition::Evaluate(Provider, Rejected, Output));
  return true;
}

#endif
