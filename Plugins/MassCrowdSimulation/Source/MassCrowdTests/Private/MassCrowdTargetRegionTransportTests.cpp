#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdTargetRegionTransportKernel.h"
#include "Misc/AutomationTest.h"

namespace MassCrowdTargetRegionTransportTests
{
  FCrowdSharedFlowFieldConfig MakeFlowConfig()
  {
    FCrowdSharedFlowFieldConfig Config;
    Config.Revision = 91;
    Config.BoundsMin = FVector(-2000.0f, -2000.0f, 0.0f);
    Config.BoundsMax = FVector(2000.0f, 2000.0f, 0.0f);
    Config.CellSizeCm = 100.0f;
    Config.AgentInflateCm = 48.0f;
    Config.GoalLocation = FVector::ZeroVector;
    return Config;
  }

  FCrowdTargetRegionTransportSettings MakeSettings()
  {
    FCrowdTargetRegionTransportSettings Settings;
    Settings.TargetLocation = FVector2f::ZeroVector;
    Settings.MinimumCenterDistanceCm = 152.0f;
    Settings.MaximumCenterDistanceCm = 850.0f;
    Settings.InfluenceBlendWidthCm = 300.0f;
    return Settings;
  }

  FCrowdTargetRegionTransportAgent MakeAgent(
    const int32 AgentId,
    const int32 Region,
    const float Radius)
  {
    const float Angle = (static_cast<float>(Region) + 0.5f) * 2.0f * PI / 16.0f;
    FCrowdTargetRegionTransportAgent Agent;
    Agent.AgentId = AgentId;
    Agent.Location = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
    Agent.FarFlowPreferredVelocity = -Agent.Location.GetSafeNormal() * 600.0f;
    Agent.MaxSpeedCmps = 800.0f;
    return Agent;
  }

  TArray<FCrowdTargetRegionTransportAgent> MakeAgentsAt(
    const int32 Count,
    const FVector2f& Location)
  {
    TArray<FCrowdTargetRegionTransportAgent> Agents;
    Agents.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
      FCrowdTargetRegionTransportAgent Agent;
      Agent.AgentId = Index + 1;
      Agent.Location = Location;
      Agent.FarFlowPreferredVelocity = FVector2f(600.0f, 0.0f);
      Agent.MaxSpeedCmps = 800.0f;
      Agents.Add(Agent);
    }
    return Agents;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdTargetRegionTransportCoreTest,
  "MassCrowd.Core.TargetRegionTransport.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdTargetRegionTransportCoreTest::RunTest(const FString& Parameters)
{
  using namespace MassCrowdTargetRegionTransportTests;
  TestEqual(TEXT("199cm uses 8 sectors"),
    FCrowdTargetRegionTransportKernel::SectorCountForRadius(199.0f), 8);
  TestEqual(TEXT("200cm uses 16 sectors"),
    FCrowdTargetRegionTransportKernel::SectorCountForRadius(200.0f), 16);
  TestEqual(TEXT("400cm uses 32 sectors"),
    FCrowdTargetRegionTransportKernel::SectorCountForRadius(400.0f), 32);
  TestEqual(TEXT("800cm uses 64 sectors"),
    FCrowdTargetRegionTransportKernel::SectorCountForRadius(800.0f), 64);

  const FCrowdTargetRegionTransportSettings Settings = MakeSettings();
  const FCrowdSharedFlowFieldConfig Flow = MakeFlowConfig();
  FCrowdTargetPolarTopology Topology;
  FCrowdTargetPolarTopologySummary TopologySummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    Settings, Flow, Topology, TopologySummary);
  TestTrue(TEXT("Core topology is valid"), Topology.bValid && TopologySummary.bValid);

  TArray<FCrowdTargetRegionTransportAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
    Agents.Add(MakeAgent(Index + 1, 0, 1000.0f));
  FCrowdTargetRegionDemandResult Demand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 0, 91, Plan);
  TestTrue(TEXT("Core demand is valid"), Demand.bValid);
  TestTrue(TEXT("Core plan is valid"), Plan.bValid);
  TestEqual(TEXT("all agents are routed"), Plan.RoutedAgentCount, 20);
  TestEqual(TEXT("no agent is unrouted"), Plan.UnroutedAgentCount, 0);

  const uint32 TopologyHash = Topology.TopologyHash;
  const uint32 DemandHash = Demand.DemandHash;
  const uint32 TransportHash = Plan.TransportHash;
  Algo::Reverse(Agents);
  FCrowdTargetRegionDemandResult ReversedDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, ReversedDemand);
  FCrowdTargetRegionFlowPlan ReversedPlan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    Topology, ReversedDemand, nullptr, 1, 0, 91, ReversedPlan);
  TestEqual(TEXT("topology hash remains stable"), Topology.TopologyHash, TopologyHash);
  TestEqual(TEXT("agent order does not alter demand hash"),
    ReversedDemand.DemandHash, DemandHash);
  TestEqual(TEXT("agent order does not alter transport hash"),
    ReversedPlan.TransportHash, TransportHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdTargetRegionBoundaryCapacityTest,
  "MassCrowd.Core.TargetRegionTransport.BoundaryCapacity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdTargetRegionBoundaryCapacityTest::RunTest(
  const FString& Parameters)
{
  using namespace MassCrowdTargetRegionTransportTests;
  (void)Parameters;
  const FCrowdSharedFlowFieldConfig Flow = MakeFlowConfig();
  FCrowdTargetRegionTransportSettings CenterSettings = MakeSettings();
  FCrowdTargetPolarTopology CenterTopology;
  FCrowdTargetPolarTopologySummary CenterSummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    CenterSettings, Flow, CenterTopology, CenterSummary);
  TestTrue(TEXT("center topology valid"), CenterTopology.bValid);
  TestTrue(TEXT("center capacity positive"),
    CenterSummary.TotalFeasibleCapacity > 0);
  for (const FCrowdTargetPolarCell& Cell : CenterTopology.Cells)
    if (!Cell.bFeasible || !Cell.bTerminal)
      TestEqual(TEXT("invalid/non-terminal capacity is zero"),
        Cell.Capacity, 0);

  FCrowdTargetRegionTransportSettings EdgeSettings = CenterSettings;
  EdgeSettings.TargetLocation = FVector2f(-1900.0f, 0.0f);
  FCrowdTargetPolarTopology EdgeTopology;
  FCrowdTargetPolarTopologySummary EdgeSummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    EdgeSettings, Flow, EdgeTopology, EdgeSummary);
  TestTrue(TEXT("edge clipped topology valid"), EdgeTopology.bValid);
  TestTrue(TEXT("edge capacity is clipped"),
    EdgeSummary.TotalFeasibleCapacity > 0
      && EdgeSummary.TotalFeasibleCapacity
        < CenterSummary.TotalFeasibleCapacity);

  FCrowdTargetRegionTransportSettings CornerSettings = CenterSettings;
  CornerSettings.TargetLocation = FVector2f(-1900.0f, -1900.0f);
  FCrowdTargetPolarTopology CornerTopology;
  FCrowdTargetPolarTopologySummary CornerSummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    CornerSettings, Flow, CornerTopology, CornerSummary);
  TestTrue(TEXT("corner clipped topology valid"), CornerTopology.bValid);
  TestTrue(TEXT("corner capacity is finite and clipped"),
    CornerSummary.TotalFeasibleCapacity > 0
      && CornerSummary.TotalFeasibleCapacity
        < EdgeSummary.TotalFeasibleCapacity);

  FCrowdTargetRegionTransportSettings SmallProfile = CenterSettings;
  SmallProfile.RadialBandWidthCm = 300.0f;
  SmallProfile.MaximumCenterDistanceCm = 1000.0f;
  FCrowdTargetPolarTopology SmallProfileTopology;
  FCrowdTargetPolarTopologySummary SmallProfileSummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    SmallProfile, Flow, SmallProfileTopology, SmallProfileSummary);
  FCrowdTargetRegionTransportSettings LargeProfile = SmallProfile;
  LargeProfile.PhysicalRadiusCm = 84.0f;
  LargeProfile.HardSafetyGapCm = 20.0f;
  LargeProfile.SoftMarginCm = 34.0f;
  FCrowdTargetPolarTopology LargeProfileTopology;
  FCrowdTargetPolarTopologySummary LargeProfileSummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    LargeProfile, Flow, LargeProfileTopology, LargeProfileSummary);
  TestTrue(TEXT("nominal geometry supports capacity greater than one"),
    SmallProfileTopology.Cells.ContainsByPredicate(
      [](const FCrowdTargetPolarCell& Cell) { return Cell.Capacity > 1; }));
  TestTrue(TEXT("larger profile cannot increase capacity"),
    LargeProfileSummary.TotalFeasibleCapacity
      <= SmallProfileSummary.TotalFeasibleCapacity);

  const FCrowdTargetPolarCell* SourceCell =
    CornerTopology.Cells.FindByPredicate(
      [](const FCrowdTargetPolarCell& Cell)
      { return Cell.bFeasible && !Cell.bTerminal; });
  if (!SourceCell)
    SourceCell = CornerTopology.Cells.FindByPredicate(
      [](const FCrowdTargetPolarCell& Cell) { return Cell.bFeasible; });
  TestNotNull(TEXT("corner source cell exists"), SourceCell);
  if (!SourceCell) return false;

  const int32 Capacity = CornerSummary.TotalFeasibleCapacity;
  TArray<FCrowdTargetRegionTransportAgent> SaturatedAgents =
    MakeAgentsAt(Capacity, SourceCell->WorldAnchorCm);
  FCrowdTargetRegionDemandResult SaturatedDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    SaturatedAgents, CornerSettings, Flow, nullptr,
    CornerTopology, SaturatedDemand);
  TestTrue(TEXT("exact saturation demand valid"), SaturatedDemand.bValid);
  TestEqual(TEXT("exact saturation assigned"),
    SaturatedDemand.AssignablePopulation, Capacity);
  TestEqual(TEXT("exact saturation overflow"),
    SaturatedDemand.OverflowPopulation, 0);

  TArray<FCrowdTargetRegionTransportAgent> OverflowAgents =
    MakeAgentsAt(Capacity + 3, SourceCell->WorldAnchorCm);
  FCrowdTargetRegionDemandResult OverflowDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    OverflowAgents, CornerSettings, Flow, nullptr,
    CornerTopology, OverflowDemand);
  TestTrue(TEXT("over-capacity demand remains valid"), OverflowDemand.bValid);
  TestEqual(TEXT("over-capacity assigned"),
    OverflowDemand.AssignablePopulation, Capacity);
  TestEqual(TEXT("over-capacity overflow retained"),
    OverflowDemand.OverflowPopulation, 3);

  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    CornerTopology, OverflowDemand, nullptr, 1, 0, 1, Plan);
  TestTrue(TEXT("capacity-limited plan valid"), Plan.bValid);
  FCrowdTargetRegionQuotaExecutionState Execution;
  FCrowdTargetRegionTransportKernel::InitializeQuotaExecutionState(
    Plan, Execution);
  TArray<FCrowdTargetRegionGuidanceResult> Guidance;
  FCrowdTargetRegionGuidanceSummary GuidanceSummary;
  FCrowdTargetRegionTransportKernel::BuildGuidanceWithExecution(
    OverflowAgents, CornerSettings, CornerTopology, OverflowDemand,
    Plan, Execution, Guidance, GuidanceSummary);
  AddInfo(FString::Printf(
    TEXT("boundary capacity diagnostic capacity=%d assigned=%d overflow=%d routed=%d unrouted=%d holds=%d execution_valid=%d guidance_valid=%d claims=%d"),
    Capacity, OverflowDemand.AssignablePopulation,
    OverflowDemand.OverflowPopulation, Plan.RoutedAgentCount,
    GuidanceSummary.UnroutedAgentCount,
    GuidanceSummary.CapacityHoldAgentCount,
    Execution.bValid ? 1 : 0, GuidanceSummary.bValid ? 1 : 0,
    Execution.ActiveClaims.Num()));
  TestTrue(TEXT("capacity-hold guidance valid"), GuidanceSummary.bValid);
  TestEqual(TEXT("capacity-hold count equals overflow"),
    GuidanceSummary.CapacityHoldAgentCount, 3);
  for (const FCrowdTargetRegionGuidanceResult& Result : Guidance)
    if (Result.Mode == ECrowdTargetRegionGuidanceMode::CapacityHold)
      TestTrue(TEXT("capacity hold has zero inward pressure"),
        Result.DesiredVelocity.IsNearlyZero());

  TArray<FCrowdTargetRegionTransportAgent> Reordered = OverflowAgents;
  Algo::Reverse(Reordered);
  FCrowdTargetRegionDemandResult ReorderedDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    Reordered, CornerSettings, Flow, nullptr,
    CornerTopology, ReorderedDemand);
  FCrowdTargetRegionFlowPlan ReorderedPlan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    CornerTopology, ReorderedDemand, nullptr, 1, 0, 1,
    ReorderedPlan);
  TestEqual(TEXT("capacity demand deterministic"),
    ReorderedDemand.DemandHash, OverflowDemand.DemandHash);
  TestEqual(TEXT("capacity plan deterministic"),
    ReorderedPlan.TransportHash, Plan.TransportHash);

  const int32 FirstHeldAgentId = Capacity + 1;
  OverflowAgents.RemoveAt(0);
  FCrowdTargetRegionDemandResult RefillDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    OverflowAgents, CornerSettings, Flow, nullptr,
    CornerTopology, RefillDemand);
  const FCrowdTargetRegionAgentDemandState* Refilled =
    RefillDemand.AgentStates.FindByPredicate(
      [FirstHeldAgentId](const FCrowdTargetRegionAgentDemandState& State)
      { return State.AgentId == FirstHeldAgentId; });
  TestTrue(TEXT("released capacity deterministically refills"),
    Refilled && Refilled->bCapacityAdmitted && !Refilled->bCapacityHold);
  TestNotEqual(TEXT("moving edge-in changes capacity hash"),
    CenterTopology.FeasibleGraphHash, CornerTopology.FeasibleGraphHash);

  FCrowdTargetRegionTransportSettings SplitSettings = CenterSettings;
  SplitSettings.RadialBandWidthCm = 100.0f;
  SplitSettings.MinimumCenterDistanceCm = 0.0f;
  SplitSettings.MaximumCenterDistanceCm = 300.0f;
  SplitSettings.DemandRegionCount = 2;
  FCrowdTargetPolarTopology SplitTopology;
  SplitTopology.BandCellOffsets = {0, 1, 2};
  SplitTopology.BandSectorCounts = {1, 1, 1};
  SplitTopology.Cells.SetNum(3);
  for (int32 CellKey = 0; CellKey < SplitTopology.Cells.Num(); ++CellKey)
  {
    FCrowdTargetPolarCell& Cell = SplitTopology.Cells[CellKey];
    Cell.StableCellKey = CellKey;
    Cell.SectorCount = 1;
    Cell.RelativeAnchorCm = FVector2f(50.0f + 100.0f * CellKey, 0.0f);
    Cell.WorldAnchorCm = Cell.RelativeAnchorCm;
    Cell.bFeasible = true;
  }
  SplitTopology.Cells[0].bTerminal = true;
  SplitTopology.Cells[0].Capacity = 1;
  SplitTopology.Cells[0].PrimaryDemandRegionKey = 1;
  SplitTopology.Cells[1].bTerminal = true;
  SplitTopology.Cells[1].Capacity = 1;
  SplitTopology.Cells[1].PrimaryDemandRegionKey = 0;
  FCrowdTargetPolarEdge& SplitEdge = SplitTopology.Edges.AddDefaulted_GetRef();
  SplitEdge.FromCellKey = 2;
  SplitEdge.ToCellKey = 1;
  SplitEdge.GeometryCostCm = 100;
  FCrowdTargetPolarCellRegionLink& ReachableLink =
    SplitTopology.RegionLinks.AddDefaulted_GetRef();
  ReachableLink.CellKey = 1;
  ReachableLink.RegionKey = 0;
  ReachableLink.bTerminal = true;
  FCrowdTargetPolarCellRegionLink& UnreachableLink =
    SplitTopology.RegionLinks.AddDefaulted_GetRef();
  UnreachableLink.CellKey = 0;
  UnreachableLink.RegionKey = 1;
  UnreachableLink.bTerminal = true;
  SplitTopology.FeasibleGraphHash = 41u;
  SplitTopology.TopologyHash = 43u;
  SplitTopology.bValid = true;
  const TArray<FCrowdTargetRegionTransportAgent> SplitAgents =
    MakeAgentsAt(2, SplitTopology.Cells[2].WorldAnchorCm);
  FCrowdTargetRegionDemandResult SplitDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    SplitAgents, SplitSettings, Flow, nullptr, SplitTopology, SplitDemand);
  TestTrue(TEXT("split reachability demand remains valid"), SplitDemand.bValid);
  TestEqual(TEXT("only reachable terminal capacity is assignable"),
    SplitDemand.AssignablePopulation, 1);
  TestEqual(TEXT("unreachable spare capacity becomes hold"),
    SplitDemand.OverflowPopulation, 1);
  TestEqual(TEXT("reachable region receives the admission"),
    SplitDemand.Regions[0].Deficit, 1);
  TestEqual(TEXT("unreachable region receives no deficit"),
    SplitDemand.Regions[1].Deficit, 0);
  FCrowdTargetRegionFlowPlan SplitPlan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    SplitTopology, SplitDemand, nullptr, 1, 0, 1, SplitPlan);
  TestTrue(TEXT("reachability-aware split plan is valid"), SplitPlan.bValid);
  FCrowdTargetRegionQuotaExecutionState SplitExecution;
  FCrowdTargetRegionTransportKernel::InitializeQuotaExecutionState(
    SplitPlan, SplitExecution);
  TArray<FCrowdTargetRegionGuidanceResult> SplitGuidance;
  FCrowdTargetRegionGuidanceSummary SplitGuidanceSummary;
  FCrowdTargetRegionTransportKernel::BuildGuidanceWithExecution(
    SplitAgents, SplitSettings, SplitTopology, SplitDemand, SplitPlan,
    SplitExecution, SplitGuidance, SplitGuidanceSummary);
  TestTrue(TEXT("reachability-aware split guidance is valid"),
    SplitGuidanceSummary.bValid);
  TestEqual(TEXT("split guidance emits one legal capacity hold"),
    SplitGuidanceSummary.CapacityHoldAgentCount, 1);
  return true;
}

#endif
