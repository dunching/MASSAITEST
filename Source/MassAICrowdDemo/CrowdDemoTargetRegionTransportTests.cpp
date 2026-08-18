#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"

namespace CrowdDemoTargetRegionTransportTests
{
  FCrowdDemoSharedFlowFieldConfig MakeFlowConfig()
  {
    FCrowdDemoSharedFlowFieldConfig Config;
    Config.Revision = 91;
    Config.BoundsMin = FVector(-2000.0f, -2000.0f, 0.0f);
    Config.BoundsMax = FVector(2000.0f, 2000.0f, 0.0f);
    Config.CellSizeCm = 100.0f;
    Config.AgentInflateCm = 48.0f;
    Config.GoalLocation = FVector::ZeroVector;
    return Config;
  }

  FCrowdDemoTargetRegionTransportSettings MakeSettings()
  {
    FCrowdDemoTargetRegionTransportSettings Settings;
    Settings.TargetLocation = FVector2f::ZeroVector;
    Settings.MinimumCenterDistanceCm = 152.0f;
    Settings.MaximumCenterDistanceCm = 850.0f;
    Settings.InfluenceBlendWidthCm = 300.0f;
    return Settings;
  }

  FCrowdDemoTargetRegionTransportAgent MakeAgent(
    const int32 AgentId, const int32 Region, const float Radius)
  {
    const float Angle = (static_cast<float>(Region) + 0.5f) * 2.0f * PI / 16.0f;
    FCrowdDemoTargetRegionTransportAgent Agent;
    Agent.AgentId = AgentId;
    Agent.Location = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
    Agent.FarFlowPreferredVelocity = -Agent.Location.GetSafeNormal() * 600.0f;
    Agent.MaxSpeedCmps = 800.0f;
    return Agent;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionAcquireThenHoldTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.AcquireThenHold",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionAcquireThenHoldTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  auto FlowConfig = MakeFlowConfig();
  auto Settings = MakeSettings();
  Settings.MinimumCenterDistanceCm = 700.0f;
  Settings.MaximumCenterDistanceCm = 850.0f;
  Settings.DistanceResponsePolicy =
    ECrowdDemoTargetDistanceResponsePolicy::AcquireThenHold;
  const auto SupplyDecision =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      Settings.DistanceResponsePolicy, false, true, true,
      800.0f, 700.0f, 850.0f, 100.0f);
  TestFalse(TEXT("surplus terminal supply cannot acquire hold"),
    SupplyDecision.bEngagedHold);
  const auto AcquireDecision =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      Settings.DistanceResponsePolicy, false, true, false,
      800.0f, 700.0f, 850.0f, 100.0f);
  TestTrue(TEXT("demand-satisfying terminal acquires hold"),
    AcquireDecision.bAcquired && AcquireDecision.bEngagedHold);
  const auto SuppressedRetreat =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      Settings.DistanceResponsePolicy, true, true, false,
      650.0f, 700.0f, 850.0f, 100.0f);
  TestTrue(TEXT("engaged ranged agent holds when target approaches"),
    SuppressedRetreat.bEngagedHold && SuppressedRetreat.bSuppressedRetreat);
  const auto SurplusRelease =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      Settings.DistanceResponsePolicy, true, false, true,
      650.0f, 700.0f, 850.0f, 100.0f);
  TestTrue(TEXT("engaged hold releases when its region becomes surplus"),
    SurplusRelease.bReleased && !SurplusRelease.bEngagedHold);
  const auto ReleaseDecision =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      Settings.DistanceResponsePolicy, true, true, false,
      951.0f, 700.0f, 850.0f, 100.0f);
  TestTrue(TEXT("engaged ranged agent releases beyond hysteresis"),
    ReleaseDecision.bReleased && !ReleaseDecision.bEngagedHold);
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    Settings, FlowConfig, Topology, TopologySummary);
  FCrowdDemoTargetRegionTransportAgent Agent = MakeAgent(10, 0, 650.0f);
  Agent.bEngagedHold = true;
  Settings.TargetVelocity = Agent.Location.GetSafeNormal() * 80.0f;
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    {Agent}, Settings, FlowConfig, nullptr, Topology, Demand);
  TestTrue(TEXT("engaged hold demand remains valid below minimum"), Demand.bValid);
  TestEqual(TEXT("engaged hold counts as terminal population"),
    Demand.CurrentTerminalPopulation, 1);
  TestTrue(TEXT("engaged hold retains terminal ownership"),
    Demand.AgentStates[0].bTerminalStay);
  TestTrue(TEXT("engaged hold fact is explicit"),
    Demand.AgentStates[0].bEngagedHold);

  FCrowdDemoTargetRegionFlowPlan Plan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 0, 1, Plan);
  FCrowdDemoTargetRegionQuotaExecutionState Execution;
  FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(Plan, Execution);
  TArray<FCrowdDemoTargetRegionGuidanceResult> Guidance;
  FCrowdDemoTargetRegionGuidanceSummary GuidanceSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    {Agent}, Settings, Topology, Demand, Plan, Execution, Guidance, GuidanceSummary);
  TestTrue(TEXT("engaged hold guidance valid"), GuidanceSummary.bValid);
  TestEqual(TEXT("engaged hold uses explicit guidance mode"),
    Guidance[0].Mode, ECrowdDemoTargetRegionGuidanceMode::EngagedHold);
  TestTrue(TEXT("target approach does not create proactive retreat"),
    Guidance[0].DesiredVelocity.IsNearlyZero());

  const FVector2f Receding =
    FCrowdDemoTargetRegionTransportKernel::ComposeEngagedHoldVelocity(
      Agent.Location, Settings.TargetLocation,
      -Agent.Location.GetSafeNormal() * 80.0f, Agent.MaxSpeedCmps);
  TestTrue(TEXT("engaged hold follows a receding target"),
    Receding.Equals(-Agent.Location.GetSafeNormal() * 80.0f, 0.01f));
  const FVector2f Tangent(-Agent.Location.GetSafeNormal().Y,
    Agent.Location.GetSafeNormal().X);
  const FVector2f Tangential =
    FCrowdDemoTargetRegionTransportKernel::ComposeEngagedHoldVelocity(
      Agent.Location, Settings.TargetLocation, Tangent * 80.0f,
      Agent.MaxSpeedCmps);
  TestTrue(TEXT("engaged hold preserves tangential target motion"),
    Tangential.Equals(Tangent * 80.0f, 0.01f));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportTopologyTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.Topology",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportTopologyTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  TestEqual(TEXT("199cm uses 8 sectors"),
    FCrowdDemoTargetRegionTransportKernel::SectorCountForRadius(199.0f), 8);
  TestEqual(TEXT("200cm uses 16 sectors"),
    FCrowdDemoTargetRegionTransportKernel::SectorCountForRadius(200.0f), 16);
  TestEqual(TEXT("400cm uses 32 sectors"),
    FCrowdDemoTargetRegionTransportKernel::SectorCountForRadius(400.0f), 32);
  TestEqual(TEXT("800cm uses 64 sectors"),
    FCrowdDemoTargetRegionTransportKernel::SectorCountForRadius(800.0f), 64);

  auto Flow = MakeFlowConfig();
  auto Settings = MakeSettings();
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary Summary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Topology, Summary);
  TestTrue(TEXT("open topology is valid"), Summary.bValid);
  TestTrue(TEXT("topology has feasible cells"), Summary.FeasibleCellCount > 0);
  TestTrue(TEXT("topology has cross-band edges"), Summary.CrossBandEdgeCount > 0);
  TestEqual(TEXT("50cm band uses 8 sectors"), Topology.BandSectorCounts[0], 8);
  TestEqual(TEXT("250cm band uses 16 sectors"), Topology.BandSectorCounts[2], 16);
  TestEqual(TEXT("450cm band uses 32 sectors"), Topology.BandSectorCounts[4], 32);
  TestEqual(TEXT("850cm band uses 64 sectors"), Topology.BandSectorCounts[8], 64);

  FCrowdDemoSharedFlowObstacleSpec ObstacleA;
  ObstacleA.ObstacleId = 2;
  ObstacleA.Center = FVector(500.0f, 0.0f, 0.0f);
  ObstacleA.Extent = FVector(100.0f, 100.0f, 100.0f);
  FCrowdDemoSharedFlowObstacleSpec ObstacleB = ObstacleA;
  ObstacleB.ObstacleId = 1;
  ObstacleB.Center.Y = 500.0f;
  Flow.ObstacleSpecs = {ObstacleA, ObstacleB};
  FCrowdDemoTargetPolarTopology Forward;
  FCrowdDemoTargetPolarTopologySummary ForwardSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Forward, ForwardSummary);
  Algo::Reverse(Flow.ObstacleSpecs);
  FCrowdDemoTargetPolarTopology Reverse;
  FCrowdDemoTargetPolarTopologySummary ReverseSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Reverse, ReverseSummary);
  TestEqual(TEXT("obstacle input reversal preserves topology hash"),
    Forward.TopologyHash, Reverse.TopologyHash);
  TestEqual(TEXT("obstacle input reversal preserves environment hash"),
    Forward.EnvironmentHash, Reverse.EnvironmentHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportDemandTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.Demand",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportDemandTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  TestEqual(TEXT("moving target advection is added to far Shared Flow"),
    FCrowdDemoTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
      FVector2f(300.0f, 0.0f), FVector2f(-80.0f, 0.0f), 800.0f),
    FVector2f(220.0f, 0.0f));
  const FVector2f ClampedAdvection =
    FCrowdDemoTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
      FVector2f(800.0f, 0.0f), FVector2f(0.0f, 80.0f), 800.0f);
  TestTrue(TEXT("moving target advection respects max speed"),
    FMath::IsNearlyEqual(ClampedAdvection.Size(), 800.0f, 0.01f));
  const auto Flow = MakeFlowConfig();
  const auto Settings = MakeSettings();
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Topology, TopologySummary);
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
    Agents.Add(MakeAgent(Index + 1, 0, 1000.0f));
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  TestTrue(TEXT("20-agent demand is valid"), Demand.bValid);
  TestEqual(TEXT("all sixteen demand regions are feasible"), Demand.FeasibleRegionCount, 16);
  TestEqual(TEXT("desired population is conserved"), Demand.DesiredPopulationTotal, 20);
  TestEqual(TEXT("all outside agents are supply"), Demand.SupplyAgentCount, 20);
  int32 DesiredSum = 0;
  int32 MaxDesired = 0;
  for (const auto& Region : Demand.Regions)
  {
    DesiredSum += Region.DesiredPopulation;
    MaxDesired = FMath::Max(MaxDesired, Region.DesiredPopulation);
  }
  TestEqual(TEXT("region desired sum is twenty"), DesiredSum, 20);
  TestEqual(TEXT("fair division has maximum two"), MaxDesired, 2);

  const uint32 Hash = Demand.DemandHash;
  Algo::Reverse(Agents);
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  TestEqual(TEXT("agent input reversal preserves demand hash"), Demand.DemandHash, Hash);

  FCrowdDemoTargetRegionDemandResult CachedDemand = Demand;
  Agents[0].Location = MakeAgent(Agents[0].AgentId, 3, 500.0f).Location;
  FCrowdDemoTargetRegionDemandResult RebuiltDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, RebuiltDemand);
  FCrowdDemoTargetRegionTransportKernel::UpdateStaticDemandPopulation(
    Agents, Settings, Flow, nullptr, Topology, CachedDemand);
  TestTrue(TEXT("static population update remains valid"), CachedDemand.bValid);
  TestEqual(TEXT("static population update preserves full demand hash"),
    CachedDemand.DemandHash, RebuiltDemand.DemandHash);
  TestEqual(TEXT("static population update preserves supply count"),
    CachedDemand.SupplyAgentCount, RebuiltDemand.SupplyAgentCount);
  TestEqual(TEXT("static population update preserves terminal population"),
    CachedDemand.CurrentTerminalPopulation, RebuiltDemand.CurrentTerminalPopulation);

  TArray<FCrowdDemoTargetRegionTransportAgent> ExternalAgents;
  ExternalAgents.Add(MakeAgent(1001, 2, 500.0f));
  ExternalAgents.Add(MakeAgent(1002, 3, 600.0f));
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, CachedDemand, ExternalAgents);
  ExternalAgents[0].Location = MakeAgent(1001, 7, 450.0f).Location;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, RebuiltDemand, ExternalAgents);
  FCrowdDemoTargetRegionTransportKernel::UpdateStaticDemandPopulation(
    Agents, Settings, Flow, nullptr, Topology, CachedDemand, ExternalAgents);
  TestTrue(TEXT("static population update with external cohort remains valid"),
    CachedDemand.bValid);
  TestEqual(TEXT("external cohort update preserves full demand hash"),
    CachedDemand.DemandHash, RebuiltDemand.DemandHash);
  TestEqual(TEXT("external cohort update preserves occupied-cell count"),
    CachedDemand.ExternalOccupiedCellCount, RebuiltDemand.ExternalOccupiedCellCount);

  TArray<FCrowdDemoTargetRegionTransportAgent> PhaseAgents;
  for (int32 Index = 0; Index < 3; ++Index)
    PhaseAgents.Add(MakeAgent(Index + 101, 0, 1000.0f));
  auto PhaseZeroSettings = Settings;
  PhaseZeroSettings.DemandRegionPhaseOffset = 0;
  FCrowdDemoTargetRegionDemandResult PhaseZeroDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    PhaseAgents, PhaseZeroSettings, Flow, nullptr, Topology, PhaseZeroDemand);
  TestTrue(TEXT("phase-zero demand valid"), PhaseZeroDemand.bValid);
  TestEqual(TEXT("phase-zero region zero"), PhaseZeroDemand.Regions[0].DesiredPopulation, 1);
  TestEqual(TEXT("phase-zero region one"), PhaseZeroDemand.Regions[1].DesiredPopulation, 1);
  TestEqual(TEXT("phase-zero region two"), PhaseZeroDemand.Regions[2].DesiredPopulation, 1);
  auto PhaseFiveSettings = Settings;
  PhaseFiveSettings.DemandRegionPhaseOffset = 5;
  FCrowdDemoTargetRegionDemandResult PhaseFiveDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    PhaseAgents, PhaseFiveSettings, Flow, nullptr, Topology, PhaseFiveDemand);
  TestTrue(TEXT("phase-five demand valid"), PhaseFiveDemand.bValid);
  TestEqual(TEXT("phase-five region five"), PhaseFiveDemand.Regions[5].DesiredPopulation, 1);
  TestEqual(TEXT("phase-five region six"), PhaseFiveDemand.Regions[6].DesiredPopulation, 1);
  TestEqual(TEXT("phase-five region seven"), PhaseFiveDemand.Regions[7].DesiredPopulation, 1);
  TestNotEqual(TEXT("phase participates in demand hash"),
    PhaseFiveDemand.DemandHash, PhaseZeroDemand.DemandHash);
  const uint32 PhaseFiveHash = PhaseFiveDemand.DemandHash;
  Algo::Reverse(PhaseAgents);
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    PhaseAgents, PhaseFiveSettings, Flow, nullptr, Topology, PhaseFiveDemand);
  TestEqual(TEXT("phase demand remains input-order independent"),
    PhaseFiveDemand.DemandHash, PhaseFiveHash);

  auto BoundaryTopology = Topology;
  for (FCrowdDemoTargetPolarCell& Cell : BoundaryTopology.Cells)
  {
    const bool bRetainedRegion = Cell.PrimaryDemandRegionKey == 0
      || Cell.PrimaryDemandRegionKey >= 11;
    Cell.bTerminal = Cell.bTerminal && bRetainedRegion;
    if (!Cell.bTerminal) Cell.Capacity = 0;
  }
  for (FCrowdDemoTargetPolarCellRegionLink& Link : BoundaryTopology.RegionLinks)
    Link.bTerminal = BoundaryTopology.Cells[Link.CellKey].bTerminal;
  auto BoundaryPhaseSettings = Settings;
  BoundaryPhaseSettings.DemandRegionPhaseOffset = 10;
  FCrowdDemoTargetRegionDemandResult BoundaryPhaseDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    PhaseAgents, BoundaryPhaseSettings, Flow, nullptr,
    BoundaryTopology, BoundaryPhaseDemand);
  TestTrue(TEXT("boundary-filtered phase demand valid"), BoundaryPhaseDemand.bValid);
  TestEqual(TEXT("boundary-filtered feasible region count"),
    BoundaryPhaseDemand.FeasibleRegionCount, 6);
  TestEqual(TEXT("normalized phase selects feasible ordinal thirteen"),
    BoundaryPhaseDemand.Regions[13].DesiredPopulation, 1);
  TestEqual(TEXT("normalized phase selects feasible ordinal fourteen"),
    BoundaryPhaseDemand.Regions[14].DesiredPopulation, 1);
  TestEqual(TEXT("normalized phase selects feasible ordinal fifteen"),
    BoundaryPhaseDemand.Regions[15].DesiredPopulation, 1);
  TestEqual(TEXT("raw-key eleven is no longer the collapsed phase start"),
    BoundaryPhaseDemand.Regions[11].DesiredPopulation, 0);

  auto RouteFlow = FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  RouteFlow.AgentInflateCm = 52.0f;
  RouteFlow.ConnectivityContractVersion = 2;
  FCrowdDemoSharedFlowField SharedField;
  TestTrue(TEXT("SF1 shared flow builds for source attachment"),
    FCrowdDemoSharedFlowFieldKernel::Build(RouteFlow, SharedField));
  auto RouteSettings = MakeSettings();
  RouteSettings.TargetLocation = FVector2f(
    RouteFlow.GoalLocation.X, RouteFlow.GoalLocation.Y);
  FCrowdDemoTargetPolarTopology RouteTopology;
  FCrowdDemoTargetPolarTopologySummary RouteTopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    RouteSettings, RouteFlow, RouteTopology, RouteTopologySummary);
  TArray<FCrowdDemoTargetRegionTransportAgent> FarAgents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    auto Agent = MakeAgent(Index + 1, 0, 1000.0f);
    Agent.Location = FVector2f(
      (static_cast<float>(Index % 10) - 4.5f) * 128.0f,
      -2850.0f + (static_cast<float>(Index / 10) - 0.5f) * 128.0f);
    FarAgents.Add(Agent);
  }
  FCrowdDemoTargetRegionDemandResult RouteDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    FarAgents, RouteSettings, RouteFlow, &SharedField, RouteTopology, RouteDemand);
  TestTrue(TEXT("far agents attach through shared-flow navigation chain"), RouteDemand.bValid);
  TestEqual(TEXT("far shared-flow attachment has no failures"),
    RouteDemand.SourceAttachmentFailureCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportSolveTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.Transport",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportSolveTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  const auto Flow = MakeFlowConfig();
  const auto Settings = MakeSettings();
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Topology, TopologySummary);
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  for (int32 Index = 0; Index < 20; ++Index)
    Agents.Add(MakeAgent(Index + 1, Index % 4, 1000.0f));
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  FCrowdDemoTargetRegionFlowPlan Plan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 0, 7, Plan);
  TestTrue(TEXT("transport plan is valid"), Plan.bValid);
  TestEqual(TEXT("all demand is routed"), Plan.RoutedAgentCount, Demand.TotalDeficit);
  TestEqual(TEXT("no demand remains unrouted"), Plan.UnroutedAgentCount, 0);
  TestTrue(TEXT("transport has stable edge quotas"), !Plan.EdgeFlows.IsEmpty());

  TArray<FCrowdDemoTargetRegionGuidanceResult> Guidance;
  FCrowdDemoTargetRegionGuidanceSummary GuidanceSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Agents, Settings, Topology, Demand, Plan, Guidance, GuidanceSummary);
  TestTrue(TEXT("guidance is valid"), GuidanceSummary.bValid);
  TestEqual(TEXT("all outside agents receive transport guidance"),
    GuidanceSummary.TransportAgentCount, 20);
  TestEqual(TEXT("no outside agent is silently unrouted"),
    GuidanceSummary.UnroutedAgentCount, 0);

  const uint32 DemandHash = Demand.DemandHash;
  const uint32 TransportHash = Plan.TransportHash;
  const uint32 GuidanceHash = GuidanceSummary.GuidanceHash;
  Algo::Reverse(Agents);
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 0, 7, Plan);
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Agents, Settings, Topology, Demand, Plan, Guidance, GuidanceSummary);
  TestEqual(TEXT("reversal preserves demand"), Demand.DemandHash, DemandHash);
  TestEqual(TEXT("reversal preserves transport"), Plan.TransportHash, TransportHash);
  TestEqual(TEXT("reversal preserves guidance"), GuidanceSummary.GuidanceHash, GuidanceHash);

  TestTrue(TEXT("baseline transport exposes a route edge"), !Plan.EdgeFlows.IsEmpty());
  if (!Plan.EdgeFlows.IsEmpty())
  {
    const int32 OccupiedCell = Plan.EdgeFlows[0].ToCellKey;
    FCrowdDemoTargetRegionTransportAgent External = MakeAgent(1001, 0, 1000.0f);
    External.Location = Topology.Cells[OccupiedCell].WorldAnchorCm;
    TArray<FCrowdDemoTargetRegionTransportAgent> ExternalOne = {External};
    FCrowdDemoTargetRegionDemandResult SharedPopulationDemand;
    FCrowdDemoTargetRegionTransportKernel::BuildDemand(
      Agents, Settings, Flow, nullptr, Topology, SharedPopulationDemand, ExternalOne);
    TestTrue(TEXT("shared population demand remains valid"), SharedPopulationDemand.bValid);
    TestEqual(TEXT("one external agent contributes population"),
      SharedPopulationDemand.ExternalPopulationAgentCount, 1);
    TestEqual(TEXT("external route cell is occupied"),
      SharedPopulationDemand.ExternalPopulationByCell[OccupiedCell], 1);
    TestEqual(TEXT("external congestion uses the pair soft distance"),
      SharedPopulationDemand.ExternalCongestionCostByCellCm[OccupiedCell], 128);
    TestNotEqual(TEXT("external population participates in demand hash"),
      SharedPopulationDemand.DemandHash, DemandHash);
    FCrowdDemoTargetRegionFlowPlan SharedPopulationPlan;
    FCrowdDemoTargetRegionTransportKernel::SolveTransport(
      Topology, SharedPopulationDemand, nullptr, 1, 0, 7, SharedPopulationPlan);
    TestTrue(TEXT("shared population plan remains valid"), SharedPopulationPlan.bValid);
    TestNotEqual(TEXT("shared population changes transport cost or route"),
      SharedPopulationPlan.TransportHash, TransportHash);

    FCrowdDemoTargetRegionTransportAgent LargeExternal = External;
    LargeExternal.PhysicalRadiusCm = 60.0f;
    TArray<FCrowdDemoTargetRegionTransportAgent> LargeExternalAgents = {LargeExternal};
    FCrowdDemoTargetRegionDemandResult LargeExternalDemand;
    FCrowdDemoTargetRegionTransportKernel::BuildDemand(
      Agents, Settings, Flow, nullptr, Topology, LargeExternalDemand, LargeExternalAgents);
    TestEqual(TEXT("heterogeneous external congestion uses compatible soft distance"),
      LargeExternalDemand.ExternalCongestionCostByCellCm[OccupiedCell], 146);
    TestNotEqual(TEXT("heterogeneous geometry participates in demand hash"),
      LargeExternalDemand.DemandHash, SharedPopulationDemand.DemandHash);

    FCrowdDemoTargetRegionTransportAgent ExternalTwo = External;
    ExternalTwo.AgentId = 1002;
    ExternalTwo.Location = Topology.Cells[Plan.EdgeFlows.Last().ToCellKey].WorldAnchorCm;
    TArray<FCrowdDemoTargetRegionTransportAgent> ExternalAgents = {External, ExternalTwo};
    FCrowdDemoTargetRegionDemandResult ForwardExternalDemand;
    FCrowdDemoTargetRegionTransportKernel::BuildDemand(
      Agents, Settings, Flow, nullptr, Topology, ForwardExternalDemand, ExternalAgents);
    Algo::Reverse(ExternalAgents);
    FCrowdDemoTargetRegionDemandResult ReverseExternalDemand;
    FCrowdDemoTargetRegionTransportKernel::BuildDemand(
      Agents, Settings, Flow, nullptr, Topology, ReverseExternalDemand, ExternalAgents);
    TestEqual(TEXT("external population input reversal preserves demand hash"),
      ReverseExternalDemand.DemandHash, ForwardExternalDemand.DemandHash);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportBalancedTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.BalancedZeroFlow",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportBalancedTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  const auto Flow = MakeFlowConfig();
  const auto Settings = MakeSettings();
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Topology, TopologySummary);
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  for (int32 Region = 0; Region < 16; ++Region)
    Agents.Add(MakeAgent(Region + 1, Region, 550.0f));
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  FCrowdDemoTargetRegionFlowPlan Plan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 0, 8, Plan);
  TestTrue(TEXT("balanced plan is valid"), Plan.bValid);
  TestEqual(TEXT("balanced demand has no deficit"), Demand.TotalDeficit, 0);
  TestEqual(TEXT("balanced state creates no flow"), Plan.RoutedAgentCount, 0);
  TestTrue(TEXT("balanced state creates no edge quota"), Plan.EdgeFlows.IsEmpty());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportDynamicContractTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.DynamicContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportDynamicContractTest::RunTest(const FString& Parameters)
{
  using namespace CrowdDemoTargetRegionTransportTests;
  auto Flow = MakeFlowConfig();
  Flow.BoundsMin = FVector(-4000.0f, -4000.0f, 0.0f);
  Flow.BoundsMax = FVector(4000.0f, 4000.0f, 0.0f);
  const auto Settings = MakeSettings();
  FCrowdDemoTargetPolarTopology Topology;
  FCrowdDemoTargetPolarTopologySummary TopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(Settings, Flow, Topology, TopologySummary);

  TArray<FCrowdDemoTargetRegionTransportAgent> Agents;
  auto BoundaryAgent = MakeAgent(15, 0, 850.37f);
  Agents.Add(BoundaryAgent);
  FCrowdDemoTargetRegionDemandResult Demand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    Agents, Settings, Flow, nullptr, Topology, Demand);
  TestTrue(TEXT("boundary fixture demand is valid"), Demand.bValid);
  TestTrue(TEXT("850.37cm agent is not terminal"),
    Demand.AgentStates.Num() == 1 && !Demand.AgentStates[0].bTerminal);
  TestTrue(TEXT("attached anchor is a terminal topology cell"),
    Demand.AgentStates.Num() == 1
      && Topology.Cells[Demand.AgentStates[0].CurrentCellKey].bTerminal);

  FCrowdDemoTargetRegionFlowPlan Plan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, Demand, nullptr, 1, 331, 1, Plan);
  FCrowdDemoTargetRegionPlanValidationResult Validation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, Plan, 1, Validation);
  TestTrue(TEXT("valid plan must contain consumable outgoing quota for boundary supply"),
    Validation.bValid);
  TestEqual(TEXT("boundary supply has no insufficient quota cell"),
    Validation.InsufficientOutgoingQuotaCellCount, 0);

  TArray<FCrowdDemoTargetRegionGuidanceResult> Guidance;
  FCrowdDemoTargetRegionGuidanceSummary GuidanceSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    Agents, Settings, Topology, Demand, Plan, Guidance, GuidanceSummary);
  TestTrue(TEXT("validated boundary plan produces valid guidance"), GuidanceSummary.bValid);
  TestEqual(TEXT("validated plan produces no unrouted boundary agent"),
    GuidanceSummary.UnroutedAgentCount, 0);
  TestTrue(TEXT("same-region terminal handoff prioritizes radial band entry"),
    Guidance.Num() == 1
      && FVector2f::DotProduct(Guidance[0].DesiredVelocity,
        -BoundaryAgent.Location.GetSafeNormal()) > 290.0f);

  auto RetentionAgent = MakeAgent(16, 0, 750.0f);
  const float NearBoundaryAngle = FMath::DegreesToRadians(1.0f);
  RetentionAgent.Location = FVector2f(FMath::Cos(NearBoundaryAngle),
    FMath::Sin(NearBoundaryAngle)) * 750.0f;
  TArray<FCrowdDemoTargetRegionTransportAgent> RetentionAgents = {RetentionAgent};
  FCrowdDemoTargetRegionDemandResult RetentionDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    RetentionAgents, Settings, Flow, nullptr, Topology, RetentionDemand);
  FCrowdDemoTargetRegionFlowPlan RetentionPlan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    Topology, RetentionDemand, nullptr, 1, 0, 1, RetentionPlan);
  TArray<FCrowdDemoTargetRegionGuidanceResult> RetentionGuidance;
  FCrowdDemoTargetRegionGuidanceSummary RetentionSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
    RetentionAgents, Settings, Topology, RetentionDemand, RetentionPlan,
    RetentionGuidance, RetentionSummary);
  const FVector2f RetentionNormal = RetentionAgent.Location.GetSafeNormal();
  const FVector2f RetentionTangent(-RetentionNormal.Y, RetentionNormal.X);
  TestTrue(TEXT("terminal region boundary applies inward tangential retention"),
    RetentionGuidance.Num() == 1
      && RetentionGuidance[0].Mode
        == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
      && FVector2f::DotProduct(RetentionGuidance[0].DesiredVelocity,
        RetentionTangent) > 1.0f);

  const uint32 ValidValidationHash = Validation.ValidationHash;
  auto ReversedDemand = Demand;
  Algo::Reverse(ReversedDemand.AgentStates);
  FCrowdDemoTargetRegionPlanValidationResult ReversedValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, ReversedDemand, Plan, 1, ReversedValidation);
  TestTrue(TEXT("reversed validation input stays valid"), ReversedValidation.bValid);
  TestEqual(TEXT("reversed validation input preserves hash"),
    ReversedValidation.ValidationHash, ValidValidationHash);

  auto MissingEdgePlan = Plan;
  MissingEdgePlan.EdgeFlows[0].ToCellKey = Topology.Cells.Num() + 10;
  FCrowdDemoTargetRegionPlanValidationResult MissingEdgeValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, MissingEdgePlan, 1, MissingEdgeValidation);
  TestTrue(TEXT("missing edge is rejected"), MissingEdgeValidation.MissingEdgeCount > 0);

  auto ZeroQuotaPlan = Plan;
  ZeroQuotaPlan.EdgeFlows[0].AgentQuota = 0;
  FCrowdDemoTargetRegionPlanValidationResult ZeroQuotaValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, ZeroQuotaPlan, 1, ZeroQuotaValidation);
  TestTrue(TEXT("zero quota is rejected"), ZeroQuotaValidation.MissingEdgeCount > 0);

  auto DuplicatePlan = Plan;
  const FCrowdDemoTargetPolarEdgeFlow DuplicateFlow = DuplicatePlan.EdgeFlows[0];
  DuplicatePlan.EdgeFlows.Insert(DuplicateFlow, 0);
  FCrowdDemoTargetRegionPlanValidationResult DuplicateValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, DuplicatePlan, 1, DuplicateValidation);
  TestTrue(TEXT("duplicate edge quota is rejected"), DuplicateValidation.MissingEdgeCount > 0);

  auto InsufficientPlan = Plan;
  const int32 SupplyCell = Demand.AgentStates[0].CurrentCellKey;
  InsufficientPlan.EdgeFlows.RemoveAll(
    [SupplyCell](const auto& FlowValue) { return FlowValue.FromCellKey == SupplyCell; });
  FCrowdDemoTargetRegionPlanValidationResult InsufficientValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, InsufficientPlan, 1, InsufficientValidation);
  TestTrue(TEXT("current supply cell without quota is rejected"),
    InsufficientValidation.InsufficientOutgoingQuotaCellCount > 0);

  auto ConservationPlan = Plan;
  ++ConservationPlan.EdgeFlows[0].AgentQuota;
  FCrowdDemoTargetRegionPlanValidationResult ConservationValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, ConservationPlan, 1, ConservationValidation);
  TestTrue(TEXT("intermediate flow conservation failure is rejected"),
    ConservationValidation.FlowConservationFailureCount > 0);

  auto InfeasibleTopology = Topology;
  const int32 InfeasibleCell = Plan.EdgeFlows[0].ToCellKey;
  InfeasibleTopology.Cells[InfeasibleCell].bFeasible = false;
  InfeasibleTopology.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(InfeasibleTopology);
  auto InfeasiblePlan = Plan;
  InfeasiblePlan.FeasibleGraphHash = InfeasibleTopology.FeasibleGraphHash;
  FCrowdDemoTargetRegionPlanValidationResult InfeasibleValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    InfeasibleTopology, Demand, InfeasiblePlan, 1, InfeasibleValidation);
  TestTrue(TEXT("edge to newly infeasible cell is rejected"),
    InfeasibleValidation.InfeasibleEdgeCount > 0);

  auto GraphChanged = Topology;
  ++GraphChanged.Edges[0].GeometryCostCm;
  GraphChanged.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(GraphChanged);
  FCrowdDemoTargetRegionPlanValidationResult GraphChangedValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    GraphChanged, Demand, Plan, 1, GraphChangedValidation);
  TestTrue(TEXT("edge cost refresh preserves immutable short plan"),
    GraphChangedValidation.bValid);
  TestEqual(TEXT("edge cost does not change feasibility contract hash"),
    GraphChanged.FeasibleGraphHash, Topology.FeasibleGraphHash);

  auto EdgeRemoved = Topology;
  EdgeRemoved.Edges.RemoveAt(0);
  EdgeRemoved.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(EdgeRemoved);
  TestNotEqual(TEXT("edge set changes feasible graph hash"),
    EdgeRemoved.FeasibleGraphHash, Topology.FeasibleGraphHash);
  FCrowdDemoTargetRegionPlanValidationResult EdgeRemovedValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    EdgeRemoved, Demand, Plan, 1, EdgeRemovedValidation);
  TestFalse(TEXT("removed edge invalidates short plan"), EdgeRemovedValidation.bValid);

  FCrowdDemoTargetRegionPlanValidationResult TargetRevisionValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, Demand, Plan, 2, TargetRevisionValidation);
  TestFalse(TEXT("target revision mismatch is rejected"), TargetRevisionValidation.bValid);
  auto MembershipChanged = Demand;
  ++MembershipChanged.MembershipHash;
  FCrowdDemoTargetRegionPlanValidationResult MembershipValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    Topology, MembershipChanged, Plan, 1, MembershipValidation);
  TestFalse(TEXT("membership mismatch is rejected"), MembershipValidation.bValid);

  auto ReversedFlow = Flow;
  FCrowdDemoSharedFlowObstacleSpec A;
  A.ObstacleId = 2; A.Center = FVector(1200.0f, 1200.0f, 0.0f);
  A.Extent = FVector(20.0f, 20.0f, 50.0f);
  FCrowdDemoSharedFlowObstacleSpec B = A;
  B.ObstacleId = 1; B.Center.Y = -1200.0f;
  ReversedFlow.ObstacleSpecs = {A, B};
  FCrowdDemoTargetPolarTopology ForwardTopology, ReverseTopology;
  FCrowdDemoTargetPolarTopologySummary ForwardSummary, ReverseSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    Settings, ReversedFlow, ForwardTopology, ForwardSummary);
  Algo::Reverse(ReversedFlow.ObstacleSpecs);
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    Settings, ReversedFlow, ReverseTopology, ReverseSummary);
  TestEqual(TEXT("obstacle reversal preserves feasible graph hash"),
    ForwardTopology.FeasibleGraphHash, ReverseTopology.FeasibleGraphHash);

  auto ClearanceSettings = Settings;
  ClearanceSettings.TargetLocation = FVector2f(0.0f, -1000.0f);
  ClearanceSettings.TargetPhysicalRadiusCm = 0.0f;
  TestEqual(TEXT("69cm clearance costs zero"),
    FCrowdDemoTargetRegionTransportKernel::ComputeEdgeSoftClearancePenaltyCm(
      FVector2f(-100.0f, 69.0f), FVector2f(100.0f, 69.0f), ClearanceSettings,
      Flow), 0);
  FCrowdDemoSharedFlowFieldConfig ClearanceFlow = Flow;
  FCrowdDemoSharedFlowObstacleSpec Wall;
  Wall.ObstacleId = 7; Wall.Center = FVector(0.0f, 0.0f, 0.0f);
  Wall.Extent = FVector(1000.0f, 1.0f, 50.0f);
  ClearanceFlow.ObstacleSpecs = {Wall};
  TestEqual(TEXT("60cm clearance costs nine"),
    FCrowdDemoTargetRegionTransportKernel::ComputeEdgeSoftClearancePenaltyCm(
      FVector2f(-100.0f, 61.0f), FVector2f(100.0f, 61.0f), ClearanceSettings, ClearanceFlow), 9);
  TestEqual(TEXT("53cm clearance costs sixteen"),
    FCrowdDemoTargetRegionTransportKernel::ComputeEdgeSoftClearancePenaltyCm(
      FVector2f(-100.0f, 54.0f), FVector2f(100.0f, 54.0f), ClearanceSettings, ClearanceFlow), 16);
  TestTrue(TEXT("20cm clearance is not the old binary seventeen"),
    FCrowdDemoTargetRegionTransportKernel::ComputeEdgeSoftClearancePenaltyCm(
      FVector2f(-100.0f, 21.0f), FVector2f(100.0f, 21.0f), ClearanceSettings, ClearanceFlow) != 17);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionQuotaExecutionTest,
  "CrowdDemo.SoftPressure.TargetRegionTransport.MultiEdgeQuotaExecution",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionQuotaExecutionTest::RunTest(const FString& Parameters)
{
  FCrowdDemoTargetPolarTopology Topology;
  Topology.bValid = true;
  Topology.FeasibleGraphHash = 12345u;
  for (int32 CellKey = 0; CellKey < 3; ++CellKey)
  {
    FCrowdDemoTargetPolarCell& Cell = Topology.Cells.AddDefaulted_GetRef();
    Cell.StableCellKey = CellKey;
    Cell.WorldAnchorCm = FVector2f(static_cast<float>(CellKey) * 100.0f, 0.0f);
    Cell.bFeasible = true;
    Cell.bTerminal = CellKey == 2;
    Cell.Capacity = CellKey == 2 ? 1 : 0;
    Cell.PrimaryDemandRegionKey = 0;
  }
  for (int32 CellKey = 0; CellKey < 2; ++CellKey)
  {
    FCrowdDemoTargetPolarEdge& Edge = Topology.Edges.AddDefaulted_GetRef();
    Edge.FromCellKey = CellKey;
    Edge.ToCellKey = CellKey + 1;
    Edge.GeometryCostCm = 100;
  }

  FCrowdDemoTargetRegionFlowPlan Plan;
  Plan.PlanEpoch = 7;
  Plan.TargetRevision = 3;
  Plan.FeasibleGraphHash = Topology.FeasibleGraphHash;
  Plan.MembershipHash = 77u;
  Plan.TransportHash = 999u;
  Plan.RoutedAgentCount = 1;
  Plan.TotalFeasibleCapacity = 1;
  Plan.AssignablePopulation = 1;
  Plan.bValid = true;
  Plan.EdgeFlows = {{0, 1, 1, 0}, {1, 2, 1, 0}};

  FCrowdDemoTargetRegionDemandResult Demand;
  Demand.bValid = true;
  Demand.MembershipHash = Plan.MembershipHash;
  Demand.TotalDeficit = 1;
  Demand.SupplyAgentCount = 1;
  Demand.FeasibleRegionCount = 1;
  Demand.DesiredPopulationTotal = 1;
  Demand.TotalFeasibleCapacity = 1;
  Demand.AssignablePopulation = 1;
  Demand.AvailableCapacityByCell = {0, 0, 1};
  Demand.AdmittedPopulationByCell = {0, 0, 0};
  FCrowdDemoTargetDemandRegion& Region = Demand.Regions.AddDefaulted_GetRef();
  Region.StableRegionKey = 0;
  Region.AvailableCapacity = 1;
  Region.DesiredPopulation = 1;
  Region.Deficit = 1;
  Region.bFeasible = true;
  FCrowdDemoTargetPolarCellRegionLink& Link = Topology.RegionLinks.AddDefaulted_GetRef();
  Link.CellKey = 2;
  Link.RegionKey = 0;
  Link.bTerminal = true;
  FCrowdDemoTargetRegionAgentDemandState& DemandState =
    Demand.AgentStates.AddDefaulted_GetRef();
  DemandState.AgentId = 10;
  DemandState.CurrentCellKey = 0;
  DemandState.CurrentRegionKey = 0;
  DemandState.AssignedRegionKey = 0;
  DemandState.bSupply = true;
  DemandState.bCapacityAdmitted = true;

  FCrowdDemoTargetRegionTransportAgent Agent;
  Agent.AgentId = 10;
  Agent.Location = Topology.Cells[0].WorldAnchorCm;
  Agent.MaxSpeedCmps = 300.0f;
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents = {Agent};
  FCrowdDemoTargetRegionTransportSettings Settings;
  Settings.TargetLocation = Topology.Cells[2].WorldAnchorCm;
  Settings.MinimumCenterDistanceCm = 0.0f;
  Settings.MaximumCenterDistanceCm = 1000.0f;
  Settings.InfluenceBlendWidthCm = 0.0f;

  FCrowdDemoTargetRegionQuotaExecutionState Execution;
  FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(Plan, Execution);
  TArray<FCrowdDemoTargetRegionGuidanceResult> Guidance;
  FCrowdDemoTargetRegionGuidanceSummary Summary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Agents, Settings, Topology, Demand, Plan, Execution, Guidance, Summary);
  TestTrue(TEXT("first edge claim is valid"), Summary.bValid);
  TestEqual(TEXT("first edge selected"), Guidance[0].NextCellKey, 1);
  TestEqual(TEXT("claim is reserved but not consumed"),
    Execution.Edges[0].ConsumedQuota, 0);
  TestEqual(TEXT("one transient claim exists"), Execution.ActiveClaims.Num(), 1);
  FCrowdDemoTargetRegionDemandResult OverbookDemand = Demand;
  OverbookDemand.AvailableCapacityByCell[2] = 0;
  FCrowdDemoTargetRegionPlanValidationResult OverbookValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
    Topology, OverbookDemand, Plan, Execution, 3,
    OverbookValidation);
  TestFalse(TEXT("overbooked admission fails closed"),
    OverbookValidation.bValid);
  TestEqual(TEXT("overbooked destination is explicit"),
    OverbookValidation.OverbookedCellCount, 1);

  FCrowdDemoTargetRegionFlowPlan ReplacedPlan;
  FCrowdDemoTargetRegionQuotaExecutionState ReplacedExecution;
  FCrowdDemoTargetRegionPlanReplacementSummary Replacement;
  FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
    Topology, Demand, Plan, Execution, 8, 15, 3,
    ReplacedPlan, ReplacedExecution, Replacement);
  TestTrue(TEXT("replacement with frozen claim is valid"), Replacement.bValid);
  TestEqual(TEXT("eligible source claim migrates"), Replacement.MigratedClaimCount, 1);
  TestEqual(TEXT("no eligible claim is released"), Replacement.ReleasedClaimCount, 0);
  TestTrue(TEXT("migrated execution retains agent edge"),
    ReplacedExecution.ActiveClaims.Num() == 1
      && ReplacedExecution.ActiveClaims[0].AgentId == 10
      && ReplacedExecution.ActiveClaims[0].FromCellKey == 0
      && ReplacedExecution.ActiveClaims[0].ToCellKey == 1);
  FCrowdDemoTargetRegionPlanValidationResult ReplacementValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
    Topology, Demand, ReplacedPlan, ReplacedExecution, 3,
    ReplacementValidation);
  TestTrue(TEXT("migrated execution validates atomically"),
    ReplacementValidation.bValid);

  FCrowdDemoTargetPolarTopology ReversedTopology = Topology;
  Algo::Reverse(ReversedTopology.Cells);
  Algo::Reverse(ReversedTopology.Edges);
  Algo::Reverse(ReversedTopology.RegionLinks);
  FCrowdDemoTargetRegionDemandResult ReversedDemand = Demand;
  Algo::Reverse(ReversedDemand.AgentStates);
  Algo::Reverse(ReversedDemand.Regions);
  FCrowdDemoTargetRegionQuotaExecutionState ReversedOldExecution = Execution;
  Algo::Reverse(ReversedOldExecution.Edges);
  Algo::Reverse(ReversedOldExecution.ActiveClaims);
  FCrowdDemoTargetRegionFlowPlan ReversedPlan;
  FCrowdDemoTargetRegionQuotaExecutionState ReversedExecution;
  FCrowdDemoTargetRegionPlanReplacementSummary ReversedReplacement;
  FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
    ReversedTopology, ReversedDemand, Plan, ReversedOldExecution, 8, 15, 3,
    ReversedPlan, ReversedExecution, ReversedReplacement);
  TestTrue(TEXT("replacement remains valid after input reversal"),
    ReversedReplacement.bValid);
  TestEqual(TEXT("replacement hash ignores input order"),
    ReversedReplacement.ReplacementHash, Replacement.ReplacementHash);
  TestEqual(TEXT("replacement plan ignores input order"),
    ReversedPlan.TransportHash, ReplacedPlan.TransportHash);
  TestEqual(TEXT("replacement execution ignores input order"),
    ReversedExecution.ExecutionHash, ReplacedExecution.ExecutionHash);

  const FCrowdDemoTargetRegionQuotaExecutionState RollbackPoint = Execution;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Agents, Settings, Topology, Demand, Plan, Execution, Guidance, Summary);
  TestTrue(TEXT("remaining in source preserves the same claim"), Summary.bValid);
  TestEqual(TEXT("source wait does not double-consume quota"),
    Execution.Edges[0].ConsumedQuota, 0);

  Demand.AgentStates[0].CurrentCellKey = 1;
  Agent.Location = Topology.Cells[1].WorldAnchorCm;
  Agents[0] = Agent;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Agents, Settings, Topology, Demand, Plan, Execution, Guidance, Summary);
  TestTrue(TEXT("crossing first edge advances within immutable plan"), Summary.bValid);
  TestEqual(TEXT("first edge consumed exactly once"), Execution.Edges[0].ConsumedQuota, 1);
  TestEqual(TEXT("second edge selected without rebuilding plan"), Guidance[0].NextCellKey, 2);
  const uint32 AdvancedHash = Execution.ExecutionHash;

  Execution = RollbackPoint;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Agents, Settings, Topology, Demand, Plan, Execution, Guidance, Summary);
  TestEqual(TEXT("rollback replay reproduces execution hash"),
    Execution.ExecutionHash, AdvancedHash);

  Demand.AgentStates[0].CurrentCellKey = 2;
  Demand.AgentStates[0].bSupply = false;
  Demand.AgentStates[0].bTerminal = true;
  Demand.AgentStates[0].bTerminalStay = true;
  Demand.AgentStates[0].bCapacityAdmitted = true;
  Demand.AdmittedPopulationByCell[2] = 1;
  Demand.TotalDeficit = 0;
  Demand.SupplyAgentCount = 0;
  Agent.Location = Topology.Cells[2].WorldAnchorCm;
  Agents[0] = Agent;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Agents, Settings, Topology, Demand, Plan, Execution, Guidance, Summary);
  TestTrue(TEXT("terminal arrival completes the short plan"), Summary.bValid);
  TestEqual(TEXT("second edge consumed exactly once"), Execution.Edges[1].ConsumedQuota, 1);
  TestEqual(TEXT("terminal agent has no persistent owner claim"),
    Execution.ActiveClaims.Num(), 0);
  TestEqual(TEXT("terminal mode is settle"), Guidance[0].Mode,
    ECrowdDemoTargetRegionGuidanceMode::TerminalSettle);
  return true;
}

#endif
