#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoOpenCohortMovementKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenCohortMovementContractTest,
  "CrowdDemo.SF.T2.OpenCohortMovement.Contract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenCohortMovementContractTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoOpenCohortMovementLayoutInput> Inputs;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto& Input = Inputs.AddDefaulted_GetRef();
    Input.AgentId = 200 + ((Index * 7) % 20);
    Input.FormationIndex = Index;
  }
  const auto Layout = FCrowdDemoOpenCohortMovementKernel::BuildLayout(Inputs);
  Algo::Reverse(Inputs);
  const auto Reversed = FCrowdDemoOpenCohortMovementKernel::BuildLayout(Inputs);
  TestTrue(TEXT("T2 layout is valid"), Layout.bValid);
  TestEqual(TEXT("T2 has twenty stable agents"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("input reversal preserves layout hash"),
    Layout.LayoutHash, Reversed.LayoutHash);

  const auto Config = FCrowdDemoOpenCohortMovementKernel::MakeOpenFlowConfig();
  TestEqual(TEXT("T2 open field has no obstacles"), Config.ObstacleSpecs.Num(), 0);
  TestEqual(TEXT("T2 retains the shared route goal"),
    static_cast<int32>(FMath::RoundToInt(FVector(Config.GoalLocation).Y)), 1900);
  FCrowdDemoSharedFlowField Field;
  TestTrue(TEXT("T2 shared field builds"),
    FCrowdDemoSharedFlowFieldKernel::Build(Config, Field));
  const auto Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
    Field, FVector(0.0f, -2850.0f, 60.0f));
  TestEqual(TEXT("T2 spawn is reachable"),
    static_cast<int32>(Sample.Status),
    static_cast<int32>(ECrowdDemoFlowLocationStatus::Reachable));
  const FVector GoalDirection = (FVector(Config.GoalLocation)
    - FVector(0.0f, -2850.0f, 60.0f)).GetSafeNormal2D();
  TestTrue(*FString::Printf(
    TEXT("T2 macro direction advances toward the goal: direction=(%.3f,%.3f)"),
    Sample.FlowDirection.X, Sample.FlowDirection.Y),
    FVector::DotProduct(Sample.FlowDirection, GoalDirection) > 0.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenCohortMovementPolarIsolationTest,
  "CrowdDemo.SF.T2.OpenCohortMovement.PolarIsolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenCohortMovementPolarIsolationTest::RunTest(const FString& Parameters)
{
  TestTrue(TEXT("T2 enables polar handoff"),
    FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
      ECrowdDemoSoftPressureTestCase::OpenCohortMovement));
  TestFalse(TEXT("T1 does not enable polar handoff"),
    FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
      ECrowdDemoSoftPressureTestCase::OpenSpawnRelaxation));
  TestFalse(TEXT("T3 does not enable polar handoff"),
    FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
      ECrowdDemoSoftPressureTestCase::BidirectionalSwap));
  TestFalse(TEXT("T4 does not enable polar handoff"),
    FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
      ECrowdDemoSoftPressureTestCase::ValidCorridorTransit));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenCohortMovementGuidanceOwnerTest,
  "CrowdDemo.SF.T2.OpenCohortMovement.GuidanceOwnerAndProgress",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenCohortMovementGuidanceOwnerTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTargetRegionTransportSettings Settings;
  Settings.TargetLocation = FVector2f::ZeroVector;
  Settings.MinimumCenterDistanceCm = 152.0f;
  Settings.MaximumCenterDistanceCm = 850.0f;
  Settings.InfluenceBlendWidthCm = 300.0f;
  Settings.TransportSpeedCmps = 300.0f;

  FCrowdDemoTargetPolarTopology Topology;
  Topology.bValid = true;
  Topology.Cells.SetNum(2);
  Topology.Cells[0].StableCellKey = 0;
  Topology.Cells[0].WorldAnchorCm = FVector2f(300.0f, 0.0f);
  Topology.Cells[1].StableCellKey = 1;
  Topology.Cells[1].WorldAnchorCm = FVector2f(0.0f, 300.0f);

  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  auto AddAgent = [&](const int32 Id, const FVector2f Location,
    const FVector2f FarPreferred)
  {
    auto& Agent = Agents.AddDefaulted_GetRef();
    Agent.AgentId = Id;
    Agent.Location = Location;
    Agent.FarFlowPreferredVelocity = FarPreferred;
    Agent.MaxSpeedCmps = 800.0f;
  };
  AddAgent(3, FVector2f(1300.0f, 0.0f), FVector2f(-700.0f, 0.0f));
  AddAgent(1, FVector2f(300.0f, 0.0f), FVector2f(-700.0f, 0.0f));
  AddAgent(2, FVector2f(0.0f, 300.0f), FVector2f(0.0f, -700.0f));

  FCrowdDemoTargetRegionDemandResult Demand;
  Demand.bValid = true;
  for (const auto& Agent : Agents)
  {
    auto& State = Demand.AgentStates.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.CurrentCellKey = Agent.AgentId == 2 ? 1 : 0;
    State.CurrentRegionKey = Agent.AgentId;
    State.bTerminalStay = Agent.AgentId == 2;
  }
  FCrowdDemoTargetRegionFlowPlan Plan;
  Plan.bValid = true;
  auto& Edge = Plan.EdgeFlows.AddDefaulted_GetRef();
  Edge.FromCellKey = 0;
  Edge.ToCellKey = 1;
  Edge.AgentQuota = 1;

  TArray<FCrowdDemoTargetRegionGuidanceResult> Results;
  FCrowdDemoTargetRegionGuidanceSummary Summary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Agents, Settings, Topology, Demand, Plan, Results, Summary);
  TestTrue(TEXT("one owner result per agent is valid"), Summary.bValid);
  TestEqual(TEXT("far flow retains one owner"), Summary.FarFlowAgentCount, 1);
  TestEqual(TEXT("transport owns one agent"), Summary.TransportAgentCount, 1);
  TestEqual(TEXT("terminal settle owns one agent"), Summary.TerminalSettleAgentCount, 1);
  const auto* Transport = Results.FindByPredicate([](const auto& Result)
  {
    return Result.AgentId == 1;
  });
  TestTrue(TEXT("agent outside the legacy 140cm point radius keeps transport guidance"),
    Transport && Transport->Mode == ECrowdDemoTargetRegionGuidanceMode::Transport
      && !Transport->DesiredVelocity.IsNearlyZero());

  const uint32 ForwardHash = Summary.GuidanceHash;
  Algo::Reverse(Agents);
  TArray<FCrowdDemoTargetRegionGuidanceResult> ReversedResults;
  FCrowdDemoTargetRegionGuidanceSummary ReversedSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Agents, Settings, Topology, Demand, Plan, ReversedResults, ReversedSummary);
  TestEqual(TEXT("agent input order preserves guidance hash"),
    ForwardHash, ReversedSummary.GuidanceHash);

  FCrowdDemoOpenCohortMovementProgress Progress;
  FCrowdDemoOpenCohortMovementKernel::UpdateProgress(Results, 3, 8, Progress);
  TestEqual(TEXT("two agents entered the polar approach"),
    Progress.FlowApproachEnteredAgentIds.Num(), 2);
  TestEqual(TEXT("two agents handed off to transport ownership"),
    Progress.TransportHandoffAgentIds.Num(), 2);
  TestEqual(TEXT("one agent is terminal settled"),
    Progress.TerminalSettledAgentIds.Num(), 1);
  for (auto& Result : ReversedResults)
    Result.Mode = ECrowdDemoTargetRegionGuidanceMode::TerminalSettle;
  FCrowdDemoOpenCohortMovementKernel::UpdateProgress(ReversedResults, 3, 9, Progress);
  TestEqual(TEXT("all agents eventually enter approach"),
    Progress.FlowApproachEnteredAgentIds.Num(), 3);
  TestEqual(TEXT("all agents eventually hand off"),
    Progress.TransportHandoffAgentIds.Num(), 3);
  TestEqual(TEXT("all agents are terminal settled"),
    Progress.TerminalSettledAgentIds.Num(), 3);
  TestEqual(TEXT("first all-terminal boundary is stable"), Progress.TerminalSettledStep, 9);
  const uint32 SettledHash = Progress.ProgressHash;
  FCrowdDemoOpenCohortMovementKernel::UpdateProgress(ReversedResults, 3, 10, Progress);
  TestEqual(TEXT("replay does not duplicate cumulative progress"),
    Progress.ProgressHash, SettledHash);
  TestEqual(TEXT("replay preserves the first terminal step"),
    Progress.TerminalSettledStep, 9);
  return true;
}

#endif
