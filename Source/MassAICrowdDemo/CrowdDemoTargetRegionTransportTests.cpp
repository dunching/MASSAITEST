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
  for (FCrowdDemoTargetPolarCellRegionLink& Link : BoundaryTopology.RegionLinks)
  {
    Link.bTerminal = Link.RegionKey == 0 || Link.RegionKey >= 11;
  }
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
  TestFalse(TEXT("edge cost graph change invalidates reused plan"), GraphChangedValidation.bValid);
  TestNotEqual(TEXT("edge cost changes feasible graph hash"),
    GraphChanged.FeasibleGraphHash, Topology.FeasibleGraphHash);

  auto EdgeRemoved = Topology;
  EdgeRemoved.Edges.RemoveAt(0);
  EdgeRemoved.FeasibleGraphHash =
    FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(EdgeRemoved);
  TestNotEqual(TEXT("edge set changes feasible graph hash"),
    EdgeRemoved.FeasibleGraphHash, Topology.FeasibleGraphHash);

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

#endif
