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

#endif
