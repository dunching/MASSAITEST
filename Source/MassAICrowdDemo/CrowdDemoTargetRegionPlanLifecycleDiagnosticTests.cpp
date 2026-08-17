#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Mass/CrowdDemoTargetFactKernel.h"
#include "Mass/CrowdDemoTargetRegionPlanLifecycleDiagnosticKernel.h"

namespace
{
FCrowdDemoTargetPolarTopology MakeTopology()
{
  FCrowdDemoTargetPolarTopology Topology;
  Topology.bValid = true;
  for (int32 Index = 0; Index < 3; ++Index)
  {
    auto& Cell = Topology.Cells.AddDefaulted_GetRef();
    Cell.StableCellKey = Index;
    Cell.PrimaryDemandRegionKey = Index;
    Cell.bFeasible = true;
    Cell.bTerminal = Index == 2;
  }
  for (int32 Index = 0; Index < 2; ++Index)
  {
    auto& Edge = Topology.Edges.AddDefaulted_GetRef();
    Edge.FromCellKey = Index;
    Edge.ToCellKey = Index + 1;
    Edge.GeometryCostCm = 100;
  }
  Topology.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(Topology);
  return Topology;
}

FCrowdDemoTargetRegionPlanLifecycleBoundaryInput MakeInput()
{
  FCrowdDemoTargetRegionPlanLifecycleBoundaryInput Input;
  Input.FixedStepIndex = 10;
  Input.CapabilityProfileKey = 77;
  Input.PlanLifetimeSteps = 15;
  Input.TargetRevision = 3;
  Input.Topology = MakeTopology();
  Input.Demand.bValid = true;
  Input.Demand.MembershipHash = 44;
  Input.Demand.DemandHash = 55;
  Input.Demand.TotalDeficit = 1;
  for (int32 Index = 0; Index < 2; ++Index)
  {
    auto& State = Input.Demand.AgentStates.AddDefaulted_GetRef();
    State.AgentId = 10 + Index;
    State.CurrentCellKey = Index;
    State.CurrentRegionKey = Index;
    State.bSupply = true;
    auto& Agent = Input.Agents.AddDefaulted_GetRef();
    Agent.AgentId = State.AgentId;
    Agent.Location = FVector2f(Index * 100.0f, 0.0f);
  }
  Input.PreviousPlan.bValid = true;
  Input.PreviousPlan.PlanEpoch = 2;
  Input.PreviousPlan.BuildFixedStepIndex = 0;
  Input.PreviousPlan.TargetRevision = 3;
  Input.PreviousPlan.FeasibleGraphHash = Input.Topology.FeasibleGraphHash;
  Input.PreviousPlan.MembershipHash = Input.Demand.MembershipHash;
  Input.PreviousPlan.TransportHash = 123;
  Input.PreviousPlan.RoutedAgentCount = 1;
  Input.PreviousPlan.EdgeFlows = {{0, 1, 1, 0}, {1, 2, 1, 0}};
  Input.NewPlan = Input.PreviousPlan;
  Input.NewPlan.PlanEpoch = 3;
  Input.NewPlan.TransportHash = 124;
  FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(
    Input.PreviousPlan, Input.PreviousExecution);
  Input.PreviousExecution.ActiveClaims.Add({10, 0, 1});
  Input.PreviousValidation.bValid = true;
  return Input;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionPlanLifecycleReasonTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.ReasonAndAllConditions",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionPlanLifecycleReasonTest::RunTest(const FString& Parameters)
{
  auto Input = MakeInput();
  Input.FixedStepIndex = 20;
  Input.TargetRevision = 4;
  Input.Topology.Cells[1].bFeasible = false;
  Input.Topology.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(Input.Topology);
  Input.Demand.MembershipHash = 45;
  Input.PreviousValidation.bValid = false;
  Input.PreviousValidation.FlowConservationFailureCount = 1;
  const uint32 Mask =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeConditionMask(Input);
  TestTrue(TEXT("target condition retained"), (Mask & CrowdDemoPlanCondition_TargetRevision) != 0);
  TestTrue(TEXT("graph condition retained"), (Mask & CrowdDemoPlanCondition_FeasibleGraph) != 0);
  TestTrue(TEXT("membership condition retained"), (Mask & CrowdDemoPlanCondition_Membership) != 0);
  TestTrue(TEXT("lifetime condition retained"), (Mask & CrowdDemoPlanCondition_Lifetime) != 0);
  TestTrue(TEXT("execution condition retained"), (Mask & CrowdDemoPlanCondition_ExecutionInvalid) != 0);
  TestEqual(TEXT("precedence remains processor contract"),
    static_cast<int32>(FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::SelectReason(Mask)), 2);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionPlanLifecycleGraphTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.GraphComponentsAndOrder",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionPlanLifecycleGraphTest::RunTest(const FString& Parameters)
{
  auto Topology = MakeTopology();
  const auto Base = FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Topology);
  auto Cost = Topology;
  ++Cost.Edges[0].SoftClearancePenaltyCm;
  const auto CostHashes = FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Cost);
  TestEqual(TEXT("cost leaves cell hash"), CostHashes.CellFeasibilityHash, Base.CellFeasibilityHash);
  TestEqual(TEXT("cost leaves edge set"), CostHashes.EdgeSetHash, Base.EdgeSetHash);
  TestNotEqual(TEXT("cost hash changes"), CostHashes.EdgeCostHash, Base.EdgeCostHash);
  auto Feasibility = Topology;
  Feasibility.Cells[1].bFeasible = false;
  const auto FeasibilityHashes =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Feasibility);
  TestNotEqual(TEXT("cell feasibility changes"),
    FeasibilityHashes.CellFeasibilityHash, Base.CellFeasibilityHash);
  auto Reversed = Topology;
  Algo::Reverse(Reversed.Cells);
  Algo::Reverse(Reversed.Edges);
  const auto ReversedHashes =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Reversed);
  TestEqual(TEXT("cell input order stable"), ReversedHashes.CellFeasibilityHash, Base.CellFeasibilityHash);
  TestEqual(TEXT("edge input order stable"), ReversedHashes.EdgeSetHash, Base.EdgeSetHash);
  TestEqual(TEXT("cost input order stable"), ReversedHashes.EdgeCostHash, Base.EdgeCostHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionPlanLifecycleMigrationTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.MigratedClaimIsNotDropped",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionPlanLifecycleMigrationTest::RunTest(const FString& Parameters)
{
  auto Input = MakeInput();
  Input.SelectedReason = static_cast<int32>(
    ECrowdDemoTargetRegionPlanRebuildReason::Lifetime);
  FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(
    Input.NewPlan, Input.NewExecution);
  Input.NewExecution.ActiveClaims.Add({10, 0, 1});
  FCrowdDemoTargetRegionPlanLifecycleRuntime Runtime;
  Runtime.PlanGraph =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Input.Topology);
  Runtime.bHasPlanGraph = true;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(Input, Runtime);
  TestEqual(TEXT("eligible claim is counted"),
    Runtime.Summary.GeometryEligibleClaimCount, 1);
  TestEqual(TEXT("source supply claim is migration eligible"),
    Runtime.Summary.SupplyEligibleClaimCount, 1);
  TestEqual(TEXT("migrated claim is counted"),
    Runtime.Summary.MigratedClaimCount, 1);
  TestEqual(TEXT("source claim is not already completed"),
    Runtime.Summary.CompletedAtReplacementClaimCount, 0);
  TestEqual(TEXT("migrated claim is not reported as dropped"),
    Runtime.Summary.DroppedStillFeasibleClaimCount, 0);
  TestFalse(TEXT("migration does not pin a claim-drop fixture"),
    Runtime.FirstClaimDropFixture.bValid);

  auto CompletedInput = MakeInput();
  CompletedInput.SelectedReason = static_cast<int32>(
    ECrowdDemoTargetRegionPlanRebuildReason::Lifetime);
  CompletedInput.Demand.AgentStates[0].CurrentCellKey = 1;
  FCrowdDemoTargetRegionPlanLifecycleRuntime CompletedRuntime;
  CompletedRuntime.PlanGraph =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(
      CompletedInput.Topology);
  CompletedRuntime.bHasPlanGraph = true;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(
    CompletedInput, CompletedRuntime);
  TestEqual(TEXT("claim already at destination is completed"),
    CompletedRuntime.Summary.CompletedAtReplacementClaimCount, 1);
  TestEqual(TEXT("completed claim is no longer supply eligible"),
    CompletedRuntime.Summary.SupplyEligibleClaimCount, 0);
  TestEqual(TEXT("completed claim is not reported as dropped"),
    CompletedRuntime.Summary.DroppedStillFeasibleClaimCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionPlanLifecycleClaimRollbackTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.ClaimFixtureAggregateRollback",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionPlanLifecycleClaimRollbackTest::RunTest(const FString& Parameters)
{
  auto Input = MakeInput();
  Input.SelectedReason = 6;
  Input.PreviousValidation.bValid = false;
  Input.PreviousValidation.InsufficientOutgoingQuotaCellCount = 1;
  FCrowdDemoTargetRegionPlanLifecycleRuntime Runtime;
  Runtime.PlanGraph =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(Input.Topology);
  Runtime.bHasPlanGraph = true;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(Input, Runtime);
  TestEqual(TEXT("one rebuild"), Runtime.Summary.RebuildCount, 1);
  TestEqual(TEXT("eligible claim observed"), Runtime.Summary.GeometryEligibleClaimCount, 1);
  TestTrue(TEXT("claim fixture pinned"), Runtime.FirstClaimDropFixture.bValid);
  const auto Checkpoint = Runtime;
  Input.FixedStepIndex = 11;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(Input, Runtime);
  Runtime = Checkpoint;
  TestEqual(TEXT("rollback restores sample length"), Runtime.Summary.SampleBoundaryCount, 1);
  TestEqual(TEXT("rollback restores hash"), Runtime.Summary.StableHash,
    Checkpoint.Summary.StableHash);
  FCrowdDemoTargetRegionPlanLifecycleFixture Fixture;
  const TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime> Runtimes = {Runtime};
  const auto Summary =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
      Runtimes, 77, 3, Fixture);
  TestTrue(TEXT("aggregate count closes"), Summary.bValid);
  TestTrue(TEXT("missing cohort selects fixture"), Fixture.bValid);
  TestEqual(TEXT("final missing region is preserved"), Fixture.FinalMissingRegionKey, 3);
  TestEqual(TEXT("fixture hash propagated"), Summary.FixtureHash, Fixture.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionPlanLifecycleFinalRegionFixtureTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.FinalRegionLatestSupplyGap",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionPlanLifecycleFinalRegionFixtureTest::RunTest(
  const FString& Parameters)
{
  auto Input = MakeInput();
  Input.SelectedReason = static_cast<int32>(
    ECrowdDemoTargetRegionPlanRebuildReason::ExecutionInvalid);
  Input.PreviousValidation.bValid = false;
  Input.PreviousValidation.InsufficientOutgoingQuotaCellCount = 1;
  Input.PreviousExecution.Edges[1].ConsumedQuota =
    Input.PreviousExecution.Edges[1].InitialQuota;
  auto& Region = Input.Demand.Regions.AddDefaulted_GetRef();
  Region.StableRegionKey = 3;
  Region.bFeasible = true;
  Region.DesiredPopulation = 1;
  Region.Deficit = 1;
  FCrowdDemoTargetRegionPlanLifecycleRuntime Runtime;
  Runtime.PlanGraph =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(
      Input.Topology);
  Runtime.bHasPlanGraph = true;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(Input, Runtime);
  const auto RollbackPoint = Runtime;
  Input.FixedStepIndex = 20;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(Input, Runtime);
  FCrowdDemoTargetRegionPlanLifecycleFixture Fixture;
  const auto Summary =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
      TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime>{Runtime}, 77, 3, Fixture);
  TestTrue(TEXT("final-region fixture is valid"), Summary.bFixtureValid);
  TestEqual(TEXT("latest related supply gap wins"), Fixture.FixedStepIndex, 20);
  TestEqual(TEXT("fixture is tied to final region"),
    Fixture.ObservedDeficitRegionKey, 3);
  TestEqual(TEXT("fixture selection is supply gap"),
    static_cast<int32>(Fixture.SelectionKind),
    static_cast<int32>(
      ECrowdDemoTargetRegionLifecycleFixtureSelection::FinalRegionSupplyWithoutOutgoing));
  TestTrue(TEXT("fixture records missing outgoing quota"),
    Fixture.ExecutionInvalid.SupplyWithoutOutgoingQuotaCount > 0);

  Runtime = RollbackPoint;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
    TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime>{Runtime}, 77, 3, Fixture);
  TestEqual(TEXT("rollback restores latest related boundary"),
    Fixture.FixedStepIndex, 10);

  auto ReversedInput = MakeInput();
  ReversedInput.SelectedReason = static_cast<int32>(
    ECrowdDemoTargetRegionPlanRebuildReason::ExecutionInvalid);
  ReversedInput.PreviousValidation.bValid = false;
  ReversedInput.PreviousValidation.InsufficientOutgoingQuotaCellCount = 1;
  ReversedInput.PreviousExecution.Edges[1].ConsumedQuota =
    ReversedInput.PreviousExecution.Edges[1].InitialQuota;
  ReversedInput.Demand.Regions.Add(Region);
  Algo::Reverse(ReversedInput.Topology.Cells);
  Algo::Reverse(ReversedInput.Topology.Edges);
  Algo::Reverse(ReversedInput.Demand.AgentStates);
  Algo::Reverse(ReversedInput.Agents);
  FCrowdDemoTargetRegionPlanLifecycleRuntime ReversedRuntime;
  ReversedRuntime.PlanGraph =
    FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::ComputeGraphHashes(
      ReversedInput.Topology);
  ReversedRuntime.bHasPlanGraph = true;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::RecordBoundary(
    ReversedInput, ReversedRuntime);
  FCrowdDemoTargetRegionPlanLifecycleFixture ReversedFixture;
  FCrowdDemoTargetRegionPlanLifecycleDiagnosticKernel::BuildAggregateSummary(
    TArray<FCrowdDemoTargetRegionPlanLifecycleRuntime>{ReversedRuntime},
    77, 3, ReversedFixture);
  TestEqual(TEXT("input reversal preserves related fixture hash"),
    ReversedFixture.StableHash, Fixture.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoMovingTargetRevisionContractTest,
  "CrowdDemo.SF.TargetRegionPlanLifecycle.MovingTargetRevisionConstant",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoMovingTargetRevisionContractTest::RunTest(const FString& Parameters)
{
  const auto A = FCrowdDemoTargetFactKernel::BuildLinearMotionFact(
    1, 9, 10, FVector2f::ZeroVector, FVector2f(80.0f, 0.0f),
    0.0f, 0.0f, 100.0f, 1.0f / 30.0f, 1.0f, 1.0f);
  const auto B = FCrowdDemoTargetFactKernel::BuildLinearMotionFact(
    1, 9, 11, FVector2f::ZeroVector, FVector2f(80.0f, 0.0f),
    0.0f, 0.0f, 100.0f, 1.0f / 30.0f, 1.0f, 1.0f);
  TestFalse(TEXT("location changes"), A.Location.Equals(B.Location, 0.001f));
  TestEqual(TEXT("revision remains plan fact"), A.TargetRevision, B.TargetRevision);
  return true;
}

#endif
