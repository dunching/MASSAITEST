#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoSoftPressureRouteDiagnosticKernel.h"

namespace
{
  FCrowdDemoSoftPressureRouteStepSample MakeRouteSample(
    const int32 AgentId, const int32 Step, const FVector& Location)
  {
    FCrowdDemoSoftPressureRouteStepSample Sample;
    Sample.AgentId = AgentId;
    Sample.FixedStepIndex = Step;
    Sample.PredictStartLocation = Location;
    Sample.Location = Location;
    Sample.Goal = FVector::ZeroVector;
    Sample.FlowCellIndex = 10 + AgentId;
    Sample.FlowStableCellKey = 100 + AgentId;
    Sample.FlowStatus = ECrowdDemoFlowLocationStatus::Reachable;
    Sample.IntegrationCost = 1000;
    Sample.FlowDirection = FVector(1.0f, 0.0f, 0.0f);
    Sample.MaxSpeedCmps = 100.0f;
    Sample.DesiredVelocity = FVector(100.0f, 0.0f, 0.0f);
    Sample.PredictedVelocity = Sample.DesiredVelocity;
    Sample.AppliedVelocity = Sample.DesiredVelocity;
    Sample.FixedStepSeconds = 1.0f / 30.0f;
    return Sample;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSoftPressureRouteTransitionsTest,
  "CrowdDemo.SoftPressure.RouteDiagnostic.TransitionsAndDeadlock",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSoftPressureRouteTransitionsTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSoftPressureRouteDiagnosticRuntime Runtime;
  auto Outside = MakeRouteSample(1, 1, FVector(141.0f, 0.0f, 0.0f));
  auto Inside = MakeRouteSample(1, 2, FVector(139.0f, 0.0f, 0.0f));
  Inside.DesiredVelocity = FVector::ZeroVector;
  Inside.PredictedVelocity = FVector::ZeroVector;
  Inside.AppliedVelocity = FVector::ZeroVector;
  auto Left = MakeRouteSample(1, 3, FVector(141.0f, 0.0f, 0.0f));
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Outside}, Runtime);
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Inside}, Runtime);
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Left}, Runtime);
  TestEqual(TEXT("one reached-then-left transition"), Runtime.Agents[0].ReachedThenLeftCount, 1);
  TestEqual(TEXT("inside/outside transitions"), Runtime.Agents[0].GoalBoundaryTransitionCount, 2);
  TestEqual(TEXT("zero to max transition"), Runtime.Agents[0].ZeroToMaxSpeedTransitionCount, 1);
  TestEqual(TEXT("max to zero transition"), Runtime.Agents[0].MaxToZeroSpeedTransitionCount, 1);
  TestTrue(TEXT("ever reached independent from current inside"),
    Runtime.Agents[0].bEverReachedGoal && !Runtime.Agents[0].bCurrentInsideGoal);

  FCrowdDemoSoftPressureRouteDiagnosticRuntime BoundaryRuntime;
  auto BoundaryCrossing = MakeRouteSample(2, 1, FVector(139.0f, 0.0f, 0.0f));
  BoundaryCrossing.PredictStartLocation = FVector(141.0f, 0.0f, 0.0f);
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({BoundaryCrossing}, BoundaryRuntime);
  TestEqual(TEXT("flow contract compares the predict-stage position"),
    BoundaryRuntime.Agents[0].FlowContractViolationCount, 0);


  FCrowdDemoSoftPressureRouteDiagnosticRuntime StallRuntime;
  for (int32 Step = 0; Step < 46; ++Step)
  {
    auto Stalled = MakeRouteSample(2, Step, FVector(1000.0f, -1000.0f, 0.0f));
    Stalled.AppliedVelocity = FVector::ZeroVector;
    FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Stalled}, StallRuntime);
  }
  auto Recovered = MakeRouteSample(2, 46, FVector(1000.0f, -500.0f, 0.0f));
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Recovered}, StallRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary Summary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    StallRuntime, {}, Summary);
  TestEqual(TEXT("ever stalled retained"), Summary.CorridorEverStalledAgentCount, 1);
  TestEqual(TEXT("recovered agent is not final deadlock"), Summary.CorridorFinalDeadlockAgentCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSoftPressureRouteStableComponentTest,
  "CrowdDemo.SoftPressure.RouteDiagnostic.ComponentAndHash",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSoftPressureRouteStableComponentTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoSoftPressureRouteStepSample> Samples;
  auto A = MakeRouteSample(3, 1, FVector(300.0f, 0.0f, 0.0f));
  A.ActiveNeighborAgentIds = {2};
  A.PairSoftRealizedCorrection = FVector(-1.0f, 0.0f, 0.0f);
  auto B = MakeRouteSample(2, 1, FVector(100.0f, 0.0f, 0.0f));
  B.DesiredVelocity = FVector::ZeroVector;
  B.PredictedVelocity = FVector::ZeroVector;
  B.ActiveNeighborAgentIds = {1, 3};
  auto C = MakeRouteSample(1, 1, FVector(120.0f, 0.0f, 0.0f));
  C.DesiredVelocity = FVector::ZeroVector;
  C.PredictedVelocity = FVector::ZeroVector;
  C.ActiveNeighborAgentIds = {2};
  Samples = {A, B, C};
  FCrowdDemoSoftPressureRouteDiagnosticRuntime ForwardRuntime;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(Samples, ForwardRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary ForwardSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    ForwardRuntime, {}, ForwardSummary);
  TestEqual(TEXT("full three-agent component selected"), ForwardSummary.SelectedAgentCount, 3);
  TestTrue(TEXT("soft opposition uses negative flow dot"),
    ForwardSummary.NeverReachedSoftOppositionCmpsP95 >= 29.9f);

  Algo::Reverse(Samples);
  FCrowdDemoSoftPressureRouteDiagnosticRuntime ReverseRuntime;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(Samples, ReverseRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary ReverseSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    ReverseRuntime, {}, ReverseSummary);
  TestEqual(TEXT("input reversal keeps diagnostic hash"),
    ForwardSummary.StableHash, ReverseSummary.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSoftPressureRouteFailureOwnerTest,
  "CrowdDemo.SoftPressure.RouteDiagnostic.FailureOwnerCausality",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSoftPressureRouteFailureOwnerTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSoftPressureRouteCounterfactual BothEvidence;
  BothEvidence.bStickyValid = true;
  BothEvidence.bSoftDisabledValid = true;
  BothEvidence.BaselineNeverReachedForwardCmps = 0.0f;
  BothEvidence.StickyNeverReachedForwardCmps = 10.0f;
  BothEvidence.SoftDisabledNeverReachedForwardCmps = 20.0f;

  FCrowdDemoSoftPressureRouteDiagnosticRuntime ReachedViolationRuntime;
  auto GoalFailure = MakeRouteSample(1, 0, FVector(300.0f, 0.0f, 0.0f));
  auto Reached = MakeRouteSample(2, 0, FVector(100.0f, 0.0f, 0.0f));
  Reached.DesiredVelocity = FVector::ZeroVector;
  Reached.PredictedVelocity = FVector::ZeroVector;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    {GoalFailure, Reached}, ReachedViolationRuntime);
  auto ReachedViolation = MakeRouteSample(2, 1, FVector(141.0f, 0.0f, 0.0f));
  ReachedViolation.FlowDirection = FVector::ZeroVector;
  ReachedViolation.DesiredVelocity = FVector::ZeroVector;
  ReachedViolation.PredictedVelocity = FVector::ZeroVector;
  ReachedViolation.AppliedVelocity = FVector::ZeroVector;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    {GoalFailure, ReachedViolation}, ReachedViolationRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary ReachedViolationSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    ReachedViolationRuntime, BothEvidence, ReachedViolationSummary);
  TestEqual(TEXT("reached flow anomaly cannot own a goal failure"),
    ReachedViolationSummary.SelectedBranch,
    ECrowdDemoSoftPressureRouteBranch::MixedEvidence);
  TestEqual(TEXT("reached anomaly remains auxiliary"),
    ReachedViolationSummary.FailureOwnedFlowContractViolationCount, 0);

  FCrowdDemoSoftPressureRouteDiagnosticRuntime TransportOwnedRuntime;
  auto TransportOwned = MakeRouteSample(20, 0, FVector(300.0f, 0.0f, 0.0f));
  TransportOwned.bFlowGuidanceOwner = false;
  TransportOwned.FlowDirection = FVector::ZeroVector;
  TransportOwned.DesiredVelocity = FVector(0.0f, 120.0f, 0.0f);
  TransportOwned.PredictedVelocity = TransportOwned.DesiredVelocity;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    {TransportOwned}, TransportOwnedRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary TransportOwnedSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    TransportOwnedRuntime, {}, TransportOwnedSummary);
  TestEqual(TEXT("transport-owned guidance is not checked as Shared Flow"),
    TransportOwnedSummary.FlowContractViolationCount, 0);
  TestEqual(TEXT("transport-owned sample does not claim Flow ownership"),
    TransportOwnedRuntime.Agents[0].FlowGuidanceOwnedSampleCount, 0);

  FCrowdDemoSoftPressureRouteDiagnosticRuntime PseudoNeighborRuntime;
  auto PseudoNeighbor = MakeRouteSample(21, 0, FVector(300.0f, 0.0f, 0.0f));
  PseudoNeighbor.ActiveNeighborAgentIds = {-2};
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    {PseudoNeighbor}, PseudoNeighborRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary PseudoNeighborSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    PseudoNeighborRuntime, {}, PseudoNeighborSummary);
  TestTrue(TEXT("non-agent environment/target neighbor does not invalidate component"),
    PseudoNeighborSummary.bValid);
  TestEqual(TEXT("pseudo neighbor is excluded from selected agent count"),
    PseudoNeighborSummary.SelectedAgentCount, 1);

  FCrowdDemoSoftPressureRouteDiagnosticRuntime NeverReachedFlowRuntime;
  for (int32 Step = 0; Step < 46; ++Step)
  {
    auto ZeroFlow = MakeRouteSample(3, Step, FVector(300.0f, 0.0f, 0.0f));
    ZeroFlow.FlowDirection = FVector::ZeroVector;
    ZeroFlow.DesiredVelocity = FVector::ZeroVector;
    ZeroFlow.PredictedVelocity = FVector::ZeroVector;
    ZeroFlow.AppliedVelocity = FVector::ZeroVector;
    FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
      {ZeroFlow}, NeverReachedFlowRuntime);
  }
  FCrowdDemoSoftPressureRouteDiagnosticSummary NeverReachedFlowSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    NeverReachedFlowRuntime, {}, NeverReachedFlowSummary);
  TestEqual(TEXT("sustained zero flow can own a goal failure"),
    NeverReachedFlowSummary.SelectedBranch,
    ECrowdDemoSoftPressureRouteBranch::FlowContract);

  FCrowdDemoSoftPressureRouteDiagnosticRuntime CorridorRuntime;
  for (int32 Step = 0; Step < 46; ++Step)
  {
    auto Stalled = MakeRouteSample(4, Step, FVector(1000.0f, -1000.0f, 0.0f));
    Stalled.AppliedVelocity = FVector::ZeroVector;
    FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Stalled}, CorridorRuntime);
  }
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
    {Reached, ReachedViolation}, CorridorRuntime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary CorridorSummary;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
    CorridorRuntime, BothEvidence, CorridorSummary);
  TestEqual(TEXT("corridor deadlock precedes unrelated reached flow anomaly"),
    CorridorSummary.SelectedBranch,
    ECrowdDemoSoftPressureRouteBranch::CorridorContract);
  TestEqual(TEXT("corridor failure count"), CorridorSummary.CorridorFailureAgentCount, 1);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoSoftPressureRouteRollbackTest,
  "CrowdDemo.SoftPressure.RouteDiagnostic.RollbackReplay",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoSoftPressureRouteRollbackTest::RunTest(const FString& Parameters)
{
  FCrowdDemoSoftPressureRouteDiagnosticRuntime Runtime;
  auto First = MakeRouteSample(4, 1, FVector(141.0f, 0.0f, 0.0f));
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({First}, Runtime);
  const auto Checkpoint =
    FCrowdDemoSoftPressureRouteDiagnosticKernel::MakeCheckpoint(Runtime);
  auto Second = MakeRouteSample(4, 2, FVector(139.0f, 0.0f, 0.0f));
  Second.DesiredVelocity = FVector::ZeroVector;
  Second.PredictedVelocity = FVector::ZeroVector;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Second}, Runtime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary Control;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(Runtime, {}, Control);

  FCrowdDemoSoftPressureRouteDiagnosticKernel::RestoreCheckpoint(Checkpoint, Runtime);
  FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep({Second}, Runtime);
  FCrowdDemoSoftPressureRouteDiagnosticSummary Replayed;
  FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(Runtime, {}, Replayed);
  TestEqual(TEXT("rollback replay keeps hash"), Control.StableHash, Replayed.StableHash);
  TestEqual(TEXT("rollback replay does not duplicate transitions"),
    Runtime.Agents[0].GoalBoundaryTransitionCount, 1);
  return true;
}

#endif
