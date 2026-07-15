#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoTargetApproachKernel.h"
#include "Mass/CrowdDemoParticleConstraintKernel.h"

namespace CrowdDemoTargetApproachTests
{
FCrowdDemoTargetFact MakeTarget()
{
  FCrowdDemoTargetFact Target;
  Target.TargetId = 7;
  Target.TargetRevision = 3;
  Target.MotionStep = 11;
  Target.Location = FVector2f(100.0f, -50.0f);
  Target.PhysicalRadiusCm = 100.0f;
  return Target;
}

FCrowdDemoTargetApproachSettings MakeSettings()
{
  FCrowdDemoTargetApproachSettings Settings;
  Settings.bEnabled = true;
  Settings.TransitionRingRadiusCm = 600.0f;
  return Settings;
}

FCrowdDemoTargetApproachAgent MakeAgent(int32 AgentId, const FVector2f& Location)
{
  FCrowdDemoTargetApproachAgent Agent;
  Agent.AgentId = AgentId;
  Agent.Location = Location;
  Agent.PhysicalRadiusCm = 42.0f;
  Agent.MaxSpeedCmps = 800.0f;
  Agent.ExistingTargetRevision = 3;
  return Agent;
}

const FCrowdDemoTargetApproachResult* FindResult(
  const TArray<FCrowdDemoTargetApproachResult>& Results, int32 AgentId)
{
  return Results.FindByPredicate([AgentId](const auto& Result) { return Result.AgentId == AgentId; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachRingGeometryTest,
  "CrowdDemo.SoftPressure.TargetApproach.RingGeometry",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachRingGeometryTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  const FCrowdDemoTargetFact Target = MakeTarget();
  const FVector2f Point = FCrowdDemoTargetApproachKernel::FindNearestTransitionRingPoint(
    Target, Target.Location + FVector2f(900.0f, 0.0f), 1, 600.0f);
  TestTrue(TEXT("Nearest point lies on the continuous ring"),
    Point.Equals(Target.Location + FVector2f(600.0f, 0.0f), 0.001f));
  const FVector2f ZeroA = FCrowdDemoTargetApproachKernel::FindNearestTransitionRingPoint(
    Target, Target.Location, 19, 600.0f);
  const FVector2f ZeroB = FCrowdDemoTargetApproachKernel::FindNearestTransitionRingPoint(
    Target, Target.Location, 19, 600.0f);
  TestTrue(TEXT("Zero-distance direction is stable"), ZeroA.Equals(ZeroB, 0.001f));
  TestTrue(TEXT("Zero-distance point remains on ring"),
    FMath::IsNearlyEqual(FVector2f::Distance(ZeroA, Target.Location), 600.0f, 0.01f));

  TArray<FCrowdDemoTargetApproachAgent> Agents = {
    MakeAgent(1, Target.Location + FVector2f(900.0f, 0.0f)),
    MakeAgent(2, Target.Location + FVector2f(901.0f, 1.0f))};
  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {}, Agents, 10, Results, Summary);
  TestTrue(TEXT("Geometry-only solve is valid"), Summary.bValid);
  TestEqual(TEXT("No discrete ring owner is created"),
    Summary.FunctionalSlotOccupied + Summary.FillSlotOccupied, 0);
  TestEqual(TEXT("Both remain Approach outside ring"), Summary.ApproachAgentCount, 2);
  TestTrue(TEXT("Nearby approach directions may share nearby guidance"),
    FVector2f::Distance(Results[0].DesiredLocation, Results[1].DesiredLocation) < 2.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachIndependentHandoffTest,
  "CrowdDemo.SoftPressure.TargetApproach.IndependentHandoff",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachIndependentHandoffTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  const FCrowdDemoTargetFact Target = MakeTarget();
  FCrowdDemoTargetSlotSpec Functional;
  Functional.SlotId = 100;
  Functional.Kind = ECrowdDemoTargetSlotKind::Functional;
  Functional.TargetRelativeOffset = FVector2f(300.0f, 0.0f);
  TArray<FCrowdDemoTargetApproachAgent> Agents = {
    MakeAgent(1, Target.Location + FVector2f(600.0f, 0.0f)),
    MakeAgent(2, Target.Location + FVector2f(900.0f, 0.0f))};
  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {Functional}, Agents, 12,
    Results, Summary);
  const auto* First = FindResult(Results, 1);
  const auto* Second = FindResult(Results, 2);
  TestNotNull(TEXT("First result exists"), First);
  TestNotNull(TEXT("Second result exists"), Second);
  if (First == nullptr || Second == nullptr)
    return false;
  TestEqual(TEXT("First entrant immediately receives slot"), First->AssignedSlotId, 100);
  TestEqual(TEXT("First entrant does not wait for group"), First->State,
    ECrowdDemoTargetApproachState::SlotIngress);
  TestEqual(TEXT("Second agent remains Approach"), Second->State,
    ECrowdDemoTargetApproachState::Approach);
  TestEqual(TEXT("Ring waiting is structurally absent"), Summary.RingWaitingCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachMatchingTest,
  "CrowdDemo.SoftPressure.TargetApproach.FunctionalFillMatching",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachMatchingTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  const FCrowdDemoTargetFact Target = MakeTarget();
  FCrowdDemoTargetSlotSpec F1;
  F1.SlotId = 10;
  F1.Kind = ECrowdDemoTargetSlotKind::Functional;
  F1.TargetRelativeOffset = FVector2f(300.0f, 0.0f);
  F1.RequiredCapabilityMask = 1;
  FCrowdDemoTargetSlotSpec F2 = F1;
  F2.SlotId = 11;
  F2.TargetRelativeOffset = FVector2f(0.0f, 300.0f);
  F2.RequiredCapabilityMask = 2;
  FCrowdDemoTargetSlotSpec Fill;
  Fill.SlotId = 20;
  Fill.Kind = ECrowdDemoTargetSlotKind::Fill;
  Fill.TargetRelativeOffset = FVector2f(-300.0f, 0.0f);
  Fill.RequiredCapabilityMask = 0;
  TArray<FCrowdDemoTargetSlotSpec> Slots = {F1, F2, Fill};

  FCrowdDemoTargetApproachAgent A = MakeAgent(1, Target.Location + FVector2f(300.0f, 0.0f));
  A.CapabilityMask = 3;
  FCrowdDemoTargetApproachAgent B = MakeAgent(2, Target.Location + FVector2f(290.0f, 0.0f));
  B.CapabilityMask = 1;
  FCrowdDemoTargetApproachAgent C = MakeAgent(3, Target.Location + FVector2f(-300.0f, 0.0f));
  FCrowdDemoTargetApproachAgent D = MakeAgent(4, Target.Location + FVector2f(-310.0f, 0.0f));
  TArray<FCrowdDemoTargetApproachAgent> Agents = {A, B, C, D};
  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), Slots, Agents, 20, Results, Summary);
  TestTrue(TEXT("Matching result valid"), Summary.bValid);
  TestEqual(TEXT("Functional maximum cardinality is two"), Summary.FunctionalSlotOccupied, 2);
  TestEqual(TEXT("Fill runs after Functional"), Summary.FillSlotOccupied, 1);
  TestEqual(TEXT("Overflow enters FreeSettle immediately"), Summary.FreeSettleCount, 1);
  TestEqual(TEXT("Flexible A is displaced to its second edge"), FindResult(Results, 1)->AssignedSlotId, 11);
  TestEqual(TEXT("Restricted B receives its only edge"), FindResult(Results, 2)->AssignedSlotId, 10);
  TestEqual(TEXT("No duplicate owner"), Summary.DuplicateSlotOwnerCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachLifecycleTest,
  "CrowdDemo.SoftPressure.TargetApproach.OwnerLifecycle",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachLifecycleTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  FCrowdDemoTargetFact Target = MakeTarget();
  FCrowdDemoTargetSlotSpec Slot;
  Slot.SlotId = 42;
  Slot.Kind = ECrowdDemoTargetSlotKind::Functional;
  Slot.TargetRelativeOffset = FVector2f(300.0f, 0.0f);

  FCrowdDemoTargetApproachAgent Owner = MakeAgent(8, Target.Location + FVector2f(300.0f, 0.0f));
  Owner.ExistingState = ECrowdDemoTargetApproachState::SlotOccupied;
  Owner.ExistingSlotId = 42;
  Owner.RingEnterFixedStep = 5;
  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {Slot}, {Owner}, 30, Results, Summary);
  TestEqual(TEXT("Legal occupied owner retained"), Results[0].AssignedSlotId, 42);
  TestEqual(TEXT("Owner stays occupied inside exit tolerance"), Results[0].State,
    ECrowdDemoTargetApproachState::SlotOccupied);

  Target.Location += FVector2f(10.0f, 0.0f);
  ++Target.MotionStep;
  Owner.Location += FVector2f(10.0f, 0.0f);
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {Slot}, {Owner}, 31, Results, Summary);
  TestEqual(TEXT("Continuous target motion retains owner"), Results[0].AssignedSlotId, 42);

  Owner.Location += FVector2f(100.0f, 0.0f);
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {Slot}, {Owner}, 32, Results, Summary);
  TestEqual(TEXT("Pushed occupied owner keeps slot"), Results[0].AssignedSlotId, 42);
  TestEqual(TEXT("Pushed occupied owner returns to ingress"), Results[0].State,
    ECrowdDemoTargetApproachState::SlotIngress);

  ++Target.TargetRevision;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {}, {Owner}, 33, Results, Summary);
  TestEqual(TEXT("Hard revision invalidation releases missing slot"), Results[0].AssignedSlotId, INDEX_NONE);
  TestEqual(TEXT("No replacement slot means FreeSettle"), Results[0].State,
    ECrowdDemoTargetApproachState::FreeSettle);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachDeterminismTest,
  "CrowdDemo.SoftPressure.TargetApproach.DeterminismAndDisabledIsolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachDeterminismTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  const FCrowdDemoTargetFact Target = MakeTarget();
  FCrowdDemoTargetSlotSpec Functional;
  Functional.SlotId = 1;
  Functional.Kind = ECrowdDemoTargetSlotKind::Functional;
  Functional.TargetRelativeOffset = FVector2f(250.0f, 0.0f);
  FCrowdDemoTargetSlotSpec Fill = Functional;
  Fill.SlotId = 2;
  Fill.Kind = ECrowdDemoTargetSlotKind::Fill;
  Fill.TargetRelativeOffset = FVector2f(-250.0f, 0.0f);
  TArray<FCrowdDemoTargetSlotSpec> Slots = {Functional, Fill};
  TArray<FCrowdDemoTargetApproachAgent> Agents = {
    MakeAgent(10, Target.Location + FVector2f(300.0f, 0.0f)),
    MakeAgent(11, Target.Location + FVector2f(-300.0f, 0.0f)),
    MakeAgent(12, Target.Location + FVector2f(0.0f, 300.0f))};
  TArray<FCrowdDemoTargetApproachResult> Forward;
  TArray<FCrowdDemoTargetApproachResult> Reverse;
  FCrowdDemoTargetApproachSummary ForwardSummary;
  FCrowdDemoTargetApproachSummary ReverseSummary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), Slots, Agents, 40,
    Forward, ForwardSummary);
  Algo::Reverse(Slots);
  Algo::Reverse(Agents);
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), Slots, Agents, 40,
    Reverse, ReverseSummary);
  TestEqual(TEXT("Reversed input keeps hash"), ReverseSummary.ApproachHash,
    ForwardSummary.ApproachHash);
  TestEqual(TEXT("Reversed input keeps result count"), Reverse.Num(), Forward.Num());
  for (int32 Index = 0; Index < Forward.Num() && Index < Reverse.Num(); ++Index)
  {
    TestEqual(TEXT("Stable result AgentId"), Reverse[Index].AgentId, Forward[Index].AgentId);
    TestEqual(TEXT("Stable result owner"), Reverse[Index].AssignedSlotId,
      Forward[Index].AssignedSlotId);
  }

  FCrowdDemoTargetSlotSpec QuantumSlot;
  QuantumSlot.SlotId = 99;
  QuantumSlot.Kind = ECrowdDemoTargetSlotKind::Functional;
  QuantumSlot.TargetRelativeOffset = FVector2f(250.0f, 0.0f);
  TArray<FCrowdDemoTargetApproachAgent> LowerTail = {
    MakeAgent(21, FVector2f(300.2499f, -50.0f))};
  TArray<FCrowdDemoTargetApproachAgent> UpperTail = LowerTail;
  UpperTail[0].Location.X = 300.2501f;
  FCrowdDemoTargetApproachSummary LowerSummary;
  FCrowdDemoTargetApproachSummary UpperSummary;
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {QuantumSlot}, LowerTail, 40,
    Forward, LowerSummary);
  FCrowdDemoTargetApproachKernel::Solve(Target, MakeSettings(), {QuantumSlot}, UpperTail, 40,
    Reverse, UpperSummary);
  TestEqual(TEXT("Sub-quantum tails have one canonical input contract"),
    UpperSummary.FullInputHash, LowerSummary.FullInputHash);
  TestEqual(TEXT("Sub-quantum tails cannot change quantized guidance velocity"),
    UpperSummary.GuidanceVelocityHash, LowerSummary.GuidanceVelocityHash);
  TestTrue(TEXT("Sub-quantum tails produce the same guidance velocity"),
    Reverse[0].DesiredVelocity.Equals(Forward[0].DesiredVelocity, 0.001f));

  FCrowdDemoTargetApproachSettings Disabled = MakeSettings();
  Disabled.bEnabled = false;
  const TArray<FCrowdDemoTargetApproachAgent> DisabledAgents = {MakeAgent(77, FVector2f(0.0f, 0.0f))};
  FCrowdDemoTargetApproachKernel::Solve(Target, Disabled, Slots, DisabledAgents, 41,
    Forward, ForwardSummary);
  TestTrue(TEXT("Disabled solve remains valid"), ForwardSummary.bValid);
  TestEqual(TEXT("Disabled solve preserves state"), Forward[0].State,
    DisabledAgents[0].ExistingState);
  TestTrue(TEXT("Disabled solve preserves velocity"),
    Forward[0].DesiredVelocity.Equals(DisabledAgents[0].Velocity, 0.001f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetApproachKinematicParticleTest,
  "CrowdDemo.SoftPressure.TargetApproach.KinematicTargetParticle",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetApproachKinematicParticleTest::RunTest(const FString& Parameters)
{
  constexpr int32 TargetParticleId = -1000000001;
  FCrowdDemoParticleConstraintAgent Target;
  Target.AgentId = TargetParticleId;
  Target.StartPosition = FVector(0.0f, 0.0f, 60.0f);
  Target.PredictedPosition = FVector(10.0f, 0.0f, 60.0f);
  Target.PhysicalRadiusCm = 100.0f;
  Target.HardSafetyGapCm = 10.0f;
  Target.SoftMarginCm = 17.0f;
  Target.Mobility = 0.0f;
  FCrowdDemoParticleConstraintAgent Agent;
  Agent.AgentId = 4;
  Agent.StartPosition = FVector(220.0f, 0.0f, 60.0f);
  Agent.PredictedPosition = FVector(120.0f, 0.0f, 60.0f);
  Agent.PhysicalRadiusCm = 42.0f;
  Agent.HardSafetyGapCm = 10.0f;
  Agent.SoftMarginCm = 17.0f;
  Agent.Mobility = 1.0f;
  FCrowdDemoParticleConstraintSettings Settings;
  FCrowdDemoParticleConstraintEnvironment Environment;
  Environment.bConstrainToFlowBounds = false;
  TArray<FCrowdDemoParticleConstraintAgent> ForwardAgents = {Target, Agent};
  TArray<FCrowdDemoParticleConstraintPair> ForwardPairs;
  TArray<FCrowdDemoParticleConstraintResult> ForwardResults;
  FCrowdDemoParticleConstraintSummary ForwardSummary;
  FCrowdDemoParticleConstraintKernel::Solve(
    ForwardAgents, Environment, Settings, ForwardPairs, ForwardResults, ForwardSummary);
  TestTrue(TEXT("Kinematic target pair remains hard/swept safe"), ForwardSummary.bValid);
  TestEqual(TEXT("Kinematic target produces no hard violation"),
    ForwardSummary.HardPairViolationCount, 0);
  TestEqual(TEXT("Kinematic target produces no swept violation"),
    ForwardSummary.SweptPairViolationCount, 0);
  const auto* TargetResult = ForwardResults.FindByPredicate([](const auto& Result)
  { return Result.AgentId == TargetParticleId; });
  TestNotNull(TEXT("Target result exists"), TargetResult);
  if (TargetResult != nullptr)
    TestTrue(TEXT("Zero-mobility target follows fixed fact exactly"),
      TargetResult->CorrectedPosition.Equals(Target.PredictedPosition, 0.01f));

  Algo::Reverse(ForwardAgents);
  TArray<FCrowdDemoParticleConstraintPair> ReversePairs;
  TArray<FCrowdDemoParticleConstraintResult> ReverseResults;
  FCrowdDemoParticleConstraintSummary ReverseSummary;
  FCrowdDemoParticleConstraintKernel::Solve(
    ForwardAgents, Environment, Settings, ReversePairs, ReverseResults, ReverseSummary);
  TestEqual(TEXT("Target particle input reversal keeps candidate hash"),
    ReverseSummary.CandidateHash, ForwardSummary.CandidateHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRelativeMovingGuidanceTest,
  "CrowdDemo.SoftPressure.TargetApproach.TargetRelativeMovingGuidance",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRelativeMovingGuidanceTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetApproachTests;
  FCrowdDemoTargetFact MovingTarget = MakeTarget();
  MovingTarget.Velocity = FVector2f(80.0f, 0.0f);
  const FCrowdDemoTargetApproachSettings Settings = MakeSettings();

  FCrowdDemoTargetSlotSpec Slot;
  Slot.SlotId = 501;
  Slot.Kind = ECrowdDemoTargetSlotKind::Functional;
  Slot.TargetRelativeOffset = FVector2f(300.0f, 0.0f);
  Slot.CenterDistanceCm = 300.0f;

  FCrowdDemoTargetApproachAgent SlotAgent = MakeAgent(
    1, MovingTarget.Location + Slot.TargetRelativeOffset);
  SlotAgent.Velocity = MovingTarget.Velocity;
  SlotAgent.ExistingState = ECrowdDemoTargetApproachState::SlotIngress;
  SlotAgent.ExistingSlotId = Slot.SlotId;

  TArray<FCrowdDemoTargetApproachResult> Results;
  FCrowdDemoTargetApproachSummary Summary;
  FCrowdDemoTargetApproachKernel::Solve(MovingTarget, Settings, {Slot}, {SlotAgent}, 100,
    Results, Summary, 7);
  TestTrue(TEXT("Moving slot solve valid"), Summary.bValid && Results.Num() == 1);
  if (Results.Num() != 1)
    return false;
  TestEqual(TEXT("Target-relative zero velocity settles slot"), Results[0].State,
    ECrowdDemoTargetApproachState::SlotOccupied);
  TestTrue(TEXT("Slot guidance includes target velocity"),
    Results[0].DesiredVelocity.Equals(MovingTarget.Velocity, 0.001f));

  FCrowdDemoTargetApproachAgent FreeAgent = MakeAgent(
    2, MovingTarget.Location + FVector2f(152.0f, 0.0f));
  FreeAgent.Velocity = MovingTarget.Velocity;
  FreeAgent.ExistingState = ECrowdDemoTargetApproachState::FreeSettle;
  FCrowdDemoTargetApproachKernel::Solve(MovingTarget, Settings, {}, {FreeAgent}, 101,
    Results, Summary, 7);
  TestTrue(TEXT("Target-relative zero velocity settles overflow"),
    Results.Num() == 1 && Results[0].bSettled);
  TestTrue(TEXT("FreeSettle guidance includes target velocity"), Results.Num() == 1
    && Results[0].DesiredVelocity.Equals(MovingTarget.Velocity, 0.001f));

  FCrowdDemoTargetFact StaticTarget = MovingTarget;
  StaticTarget.Velocity = FVector2f::ZeroVector;
  SlotAgent.Velocity = FVector2f::ZeroVector;
  FCrowdDemoTargetApproachKernel::Solve(StaticTarget, Settings, {Slot}, {SlotAgent}, 102,
    Results, Summary, 7);
  TestTrue(TEXT("Static target remains the zero-feed-forward special case"),
    Results.Num() == 1
      && Results[0].State == ECrowdDemoTargetApproachState::SlotOccupied
      && Results[0].DesiredVelocity.IsNearlyZero(0.001f));

  constexpr float FixedStepSeconds = 1.0f / 30.0f;
  const FCrowdDemoTargetFact Previous = FCrowdDemoTargetApproachKernel::BuildLinearMotionFact(
    7, 3, 869, FVector2f(0.0f, 0.0f), FVector2f(80.0f, 0.0f),
    0.0f, 0.0f, 100.0f, FixedStepSeconds, 1.0f, 1.0f);
  const FCrowdDemoTargetFact Current = FCrowdDemoTargetApproachKernel::BuildLinearMotionFact(
    7, 3, 870, FVector2f(0.0f, 0.0f), FVector2f(80.0f, 0.0f),
    0.0f, 0.0f, 100.0f, FixedStepSeconds, 1.0f, 1.0f);
  TestTrue(TEXT("Quantized target start equals the previous applied target position"),
    (Current.Location - Current.Velocity * FixedStepSeconds).Equals(
      Previous.Location, 0.001f));
  TestTrue(TEXT("Quantized target velocity represents the applied lattice motion"),
    Current.Velocity.Equals(FVector2f(90.0f, 0.0f), 0.001f));
  return true;
}

#endif
