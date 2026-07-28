#include "Misc/AutomationTest.h"

#include "MassCrowdAgentFacts.h"
#include "MassCrowdRuntimeFragments.h"

#include <type_traits>

static_assert(std::is_trivially_copyable_v<FCrowdStableEntityRef>);
static_assert(std::is_trivially_copyable_v<FCrowdCapabilitySet>);
static_assert(std::is_trivially_copyable_v<FCrowdAgentFacts>);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAgentFactsTest,
  "MassCrowd.Core.AgentFacts",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAgentFactsTest::RunTest(const FString& Parameters)
{
  const FCrowdStableEntityRef FirstLife{2, 1001, 7};
  const FCrowdStableEntityRef SameLife{2, 1001, 7};
  const FCrowdStableEntityRef NextLife{2, 1001, 8};
  TestTrue(TEXT("stable reference is valid"), FirstLife.IsValid());
  TestTrue(TEXT("same lifecycle is the same reference"), FirstLife == SameLife);
  TestTrue(TEXT("new lifecycle retains the entity slot"), FirstLife.IsSameEntitySlot(NextLife));
  TestFalse(TEXT("new lifecycle is not the same reference"), FirstLife == NextLife);

  FCrowdCapabilitySet Capabilities;
  Capabilities.Add(ECrowdCapability::Move);
  Capabilities.Add(ECrowdCapability::Haul);
  Capabilities.Add(ECrowdCapability::Attack);
  TestTrue(TEXT("known capability bits are valid"), Capabilities.IsValid());
  TestTrue(TEXT("capability presence is explicit"),
    Capabilities.Has(ECrowdCapability::Haul));
  TestFalse(TEXT("move does not imply move-to capability"),
    Capabilities.Has(ECrowdCapability::MoveTo));
  Capabilities.Remove(ECrowdCapability::Haul);
  TestFalse(TEXT("removed capability is absent"),
    Capabilities.Has(ECrowdCapability::Haul));

  FCrowdCapabilitySet UnknownCapability;
  UnknownCapability.Bits = uint64{1} << static_cast<uint8>(ECrowdCapability::Count);
  TestFalse(TEXT("unknown capability bits are rejected"), UnknownCapability.IsValid());

  FCrowdAgentFacts Facts;
  Facts.StableEntityRef = FirstLife;
  Facts.FactionKey = 11;
  Facts.CapabilitySet.Add(ECrowdCapability::Attack);
  Facts.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::Attack);
  Facts.BusinessTaskRef = FCrowdStableEntityRef{20, 500, 1};
  Facts.TargetRef = FCrowdStableEntityRef{21, 600, 3};
  TestTrue(TEXT("complete supported facts are well formed"), Facts.IsWellFormed());

  FCrowdAgentFacts OtherFaction = Facts;
  OtherFaction.FactionKey = 99;
  TestTrue(TEXT("faction does not change capability set"),
    OtherFaction.CapabilitySet.Bits == Facts.CapabilitySet.Bits);

  FCrowdAgentFacts PartialOptionalRef = Facts;
  PartialOptionalRef.TargetRef = FCrowdStableEntityRef{21, 0, 0};
  TestFalse(TEXT("partial optional references are rejected"),
    PartialOptionalRef.IsWellFormed());

  FCrowdAgentFacts ArbitraryDiagnosticLabel = Facts;
  ArbitraryDiagnosticLabel.DerivedBehaviorLabel = MAX_uint32;
  TestTrue(TEXT("diagnostic label has no authority over validity"),
    ArbitraryDiagnosticLabel.IsWellFormed());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAgentFactsRuntimeMappingTest,
  "MassCrowd.Runtime.AgentFactsMapping",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAgentFactsRuntimeMappingTest::RunTest(const FString& Parameters)
{
  FCrowdMassAgentFragment Identity;
  Identity.AgentId = 17;
  Identity.SetStableEntityRef(FCrowdStableEntityRef{3, 9001, 4});

  FCrowdAgentFacts Input;
  Input.StableEntityRef = Identity.GetStableEntityRef();
  Input.FactionKey = 7;
  Input.CapabilitySet.Add(ECrowdCapability::Move);
  Input.CapabilitySet.Add(ECrowdCapability::Haul);
  Input.CapabilitySet.Add(ECrowdCapability::Attack);
  Input.DerivedBehaviorLabel =
    static_cast<uint32>(ECrowdActiveBehavior::HaulDeliver);
  Input.BusinessTaskRef = FCrowdStableEntityRef{30, 4001, 2};
  Input.TargetRef = FCrowdStableEntityRef{31, 4002, 5};
  Input.MovementProfileKey = 12;
  Input.PresentationProfileKey = 13;
  Input.RuntimeState = 14;

  FCrowdMassBehaviorFragment Behavior;
  Behavior.SetAgentFacts(Input);
  const FCrowdAgentFacts Output = Behavior.GetAgentFacts(Identity);
  TestTrue(TEXT("runtime mapping preserves well-formed facts"), Output.IsWellFormed());
  TestTrue(TEXT("runtime mapping preserves stable identity"),
    Output.StableEntityRef == Input.StableEntityRef);
  TestTrue(TEXT("runtime mapping preserves capabilities"),
    Output.CapabilitySet.Bits == Input.CapabilitySet.Bits);
  TestTrue(TEXT("runtime mapping preserves diagnostic label"),
    Output.DerivedBehaviorLabel == Input.DerivedBehaviorLabel);
  TestTrue(TEXT("runtime mapping preserves business task"),
    Output.BusinessTaskRef == Input.BusinessTaskRef);
  TestTrue(TEXT("runtime mapping preserves target"), Output.TargetRef == Input.TargetRef);
  TestTrue(TEXT("runtime mapping preserves profile and state keys"),
    Output.FactionKey == Input.FactionKey
      && Output.MovementProfileKey == Input.MovementProfileKey
      && Output.PresentationProfileKey == Input.PresentationProfileKey
      && Output.RuntimeState == Input.RuntimeState);

  Identity.SetStableEntityRef(FCrowdStableEntityRef{3, 9001, 5});
  const FCrowdAgentFacts NextLife = Behavior.GetAgentFacts(Identity);
  TestTrue(TEXT("identity lifecycle can advance independently"),
    NextLife.StableEntityRef == FCrowdStableEntityRef{3, 9001, 5});
  TestTrue(TEXT("old lifecycle remains distinguishable"),
    NextLife.StableEntityRef.IsSameEntitySlot(Input.StableEntityRef)
      && !(NextLife.StableEntityRef == Input.StableEntityRef));
  TestTrue(TEXT("identity update does not alter task and target"),
    NextLife.BusinessTaskRef == Input.BusinessTaskRef
      && NextLife.TargetRef == Input.TargetRef);
  return true;
}
