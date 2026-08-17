#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "MassCrowdTargetRegionWork.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdMassTargetRegionTopologyInput MakeTopologyInput()
  {
    FCrowdMassTargetRegionTopologyInput Input;
    Input.FlowConfig.Revision = 91;
    Input.FlowConfig.BoundsMin = FVector(-2000.0f, -2000.0f, 0.0f);
    Input.FlowConfig.BoundsMax = FVector(2000.0f, 2000.0f, 0.0f);
    Input.FlowConfig.CellSizeCm = 100.0f;
    Input.FlowConfig.AgentInflateCm = 48.0f;
    Input.FlowConfig.GoalLocation = FVector::ZeroVector;
    Input.Settings.MinimumCenterDistanceCm = 152.0f;
    Input.Settings.MaximumCenterDistanceCm = 850.0f;
    Input.Settings.InfluenceBlendWidthCm = 300.0f;
    return Input;
  }

  TArray<FCrowdTargetRegionTransportAgent> MakeAgents(
    const int32 AgentCount = 20,
    const float BaseRadiusCm = 1000.0f)
  {
    TArray<FCrowdTargetRegionTransportAgent> Agents;
    Agents.Reserve(AgentCount);
    for (int32 Index = 0; Index < AgentCount; ++Index)
    {
      const float Angle =
        (static_cast<float>(Index % 64) + 0.5f)
        * 2.0f * PI / 64.0f;
      auto& Agent = Agents.AddDefaulted_GetRef();
      Agent.AgentId = Index + 1;
      Agent.Location = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle))
        * (BaseRadiusCm + static_cast<float>((Index / 64) % 20));
      Agent.FarFlowPreferredVelocity =
        -Agent.Location.GetSafeNormal() * 600.0f;
      Agent.MaxSpeedCmps = 800.0f;
    }
    return Agents;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdTargetRegionGuidance10kShardTest,
  "MassCrowd.Runtime.TargetRegion.GuidanceShard10k",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdTargetRegionGuidance10kShardTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdMassTargetRegionTopologyInput TopologyInput =
    MakeTopologyInput();
  TopologyInput.Settings.RadialBandWidthCm = 2.0f;
  TopologyInput.Settings.MaximumCenterDistanceCm = 852.0f;
  const FCrowdMassTargetRegionTopologyOutput Topology =
    FCrowdMassTargetRegionWork::BuildTopology(TopologyInput);
  FCrowdMassTargetRegionDemandInput DemandInput;
  DemandInput.Agents = MakeAgents(10000, 500.0f);
  DemandInput.Settings = TopologyInput.Settings;
  DemandInput.FlowConfig = TopologyInput.FlowConfig;
  DemandInput.Topology = Topology.Topology;
  const FCrowdMassTargetRegionDemandOutput Demand =
    FCrowdMassTargetRegionWork::BuildDemand(DemandInput);
  FCrowdMassTargetRegionPlanInput PlanInput;
  PlanInput.Topology = Topology.Topology;
  PlanInput.Demand = Demand.Demand;
  PlanInput.FixedStepIndex = 0;
  PlanInput.TargetRevision = 91;
  PlanInput.PlanLifetimeSteps = 15;
  const FCrowdMassTargetRegionPlanOutput Plan =
    FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
  FCrowdMassTargetRegionGuidanceInput GuidanceInput;
  GuidanceInput.Agents = DemandInput.Agents;
  GuidanceInput.Settings = TopologyInput.Settings;
  GuidanceInput.Topology = Topology.Topology;
  GuidanceInput.Demand = Demand.Demand;
  GuidanceInput.Plan = Plan.Plan;
  GuidanceInput.Execution = Plan.Execution;
  const FCrowdMassTargetRegionGuidanceOutput Guidance =
    FCrowdMassTargetRegionWork::BuildGuidanceSharded(
      GuidanceInput, 128);
  int32 AvailableCapacity = 0;
  for (const FCrowdTargetDemandRegion& Region :
    Demand.Demand.Regions)
    AvailableCapacity += Region.AvailableCapacity;
  AddInfo(FString::Printf(
    TEXT("10k target diagnostic cells=%d capacity=%d desired=%d source_failures=%d agent_states=%d"),
    Topology.Topology.Cells.Num(), AvailableCapacity,
    Demand.Demand.DesiredPopulationTotal,
    Demand.Demand.SourceAttachmentFailureCount,
    Demand.Demand.AgentStates.Num()));
  TestTrue(TEXT("10k topology valid"), Topology.bValid);
  TestTrue(TEXT("10k demand valid"), Demand.bValid);
  TestTrue(TEXT("10k plan valid"), Plan.bValid);
  TestTrue(TEXT("10k sharded guidance valid"), Guidance.bValid);
  TestEqual(TEXT("10k sharded result count"),
    Guidance.Results.Num(), 10000);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdTargetRegionWorkTest,
  "MassCrowd.Runtime.TargetRegion.WorkPipeline",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdTargetRegionWorkTest::RunTest(const FString& Parameters)
{
  const FCrowdMassTargetRegionTopologyInput TopologyInput = MakeTopologyInput();
  const FCrowdMassTargetRegionTopologyOutput Topology =
    FCrowdMassTargetRegionWork::BuildTopology(TopologyInput);
  TestTrue(TEXT("topology valid"), Topology.bValid);

  FCrowdMassTargetRegionDemandInput DemandInput;
  DemandInput.Agents = MakeAgents();
  DemandInput.Settings = TopologyInput.Settings;
  DemandInput.FlowConfig = TopologyInput.FlowConfig;
  DemandInput.Topology = Topology.Topology;
  const FCrowdMassTargetRegionDemandOutput Demand =
    FCrowdMassTargetRegionWork::BuildDemand(DemandInput);
  TestTrue(TEXT("demand valid"), Demand.bValid);

  FCrowdMassTargetRegionPlanInput PlanInput;
  PlanInput.Topology = Topology.Topology;
  PlanInput.Demand = Demand.Demand;
  PlanInput.FixedStepIndex = 0;
  PlanInput.TargetRevision = 91;
  PlanInput.PlanLifetimeSteps = 15;
  const FCrowdMassTargetRegionPlanOutput Plan =
    FCrowdMassTargetRegionWork::SolvePlan(PlanInput);
  TestTrue(TEXT("plan valid"), Plan.bValid);
  TestEqual(TEXT("initial rebuild reason"), Plan.RebuildReason, 7);

  FCrowdMassTargetRegionGuidanceInput GuidanceInput;
  GuidanceInput.Agents = DemandInput.Agents;
  GuidanceInput.Settings = TopologyInput.Settings;
  GuidanceInput.Topology = Topology.Topology;
  GuidanceInput.Demand = Demand.Demand;
  GuidanceInput.Plan = Plan.Plan;
  GuidanceInput.Execution = Plan.Execution;
  const FCrowdMassTargetRegionGuidanceOutput Guidance =
    FCrowdMassTargetRegionWork::BuildGuidance(GuidanceInput);
  TestTrue(TEXT("guidance valid"), Guidance.bValid);
  TestEqual(TEXT("guidance count"), Guidance.Results.Num(), 20);
  const FCrowdMassTargetRegionGuidanceOutput ShardedGuidance =
    FCrowdMassTargetRegionWork::BuildGuidanceSharded(
      GuidanceInput, 7);
  TestTrue(TEXT("sharded guidance valid"), ShardedGuidance.bValid);
  TestEqual(TEXT("sharded guidance result count stable"),
    ShardedGuidance.Results.Num(), Guidance.Results.Num());
  for (int32 Index = 0;
    Index < FMath::Min(
      ShardedGuidance.Results.Num(), Guidance.Results.Num());
    ++Index)
  {
    const FCrowdTargetRegionGuidanceResult& Actual =
      ShardedGuidance.Results[Index];
    const FCrowdTargetRegionGuidanceResult& Expected =
      Guidance.Results[Index];
    TestEqual(TEXT("sharded agent id stable"),
      Actual.AgentId, Expected.AgentId);
    TestEqual(TEXT("sharded current cell stable"),
      Actual.CurrentCellKey, Expected.CurrentCellKey);
    TestEqual(TEXT("sharded next cell stable"),
      Actual.NextCellKey, Expected.NextCellKey);
    TestEqual(TEXT("sharded demand region stable"),
      Actual.DemandRegionKey, Expected.DemandRegionKey);
    TestEqual(TEXT("sharded mode stable"),
      Actual.Mode, Expected.Mode);
    TestTrue(TEXT("sharded desired velocity stable"),
      Actual.DesiredVelocity.Equals(Expected.DesiredVelocity));
  }
  TestEqual(TEXT("sharded guidance hash stable"),
    ShardedGuidance.Summary.GuidanceHash,
    Guidance.Summary.GuidanceHash);
  TestEqual(TEXT("sharded execution hash stable"),
    ShardedGuidance.Execution.ExecutionHash,
    Guidance.Execution.ExecutionHash);

  FCrowdMassTargetRegionDemandInput ReversedDemandInput = DemandInput;
  Algo::Reverse(ReversedDemandInput.Agents);
  const FCrowdMassTargetRegionDemandOutput ReversedDemand =
    FCrowdMassTargetRegionWork::BuildDemand(ReversedDemandInput);
  TestEqual(TEXT("reverse demand hash"),
    ReversedDemand.Demand.DemandHash, Demand.Demand.DemandHash);
  FCrowdMassTargetRegionGuidanceInput ReversedGuidanceInput = GuidanceInput;
  Algo::Reverse(ReversedGuidanceInput.Agents);
  const FCrowdMassTargetRegionGuidanceOutput ReversedGuidance =
    FCrowdMassTargetRegionWork::BuildGuidance(ReversedGuidanceInput);
  TestEqual(TEXT("reverse guidance hash"),
    ReversedGuidance.Summary.GuidanceHash, Guidance.Summary.GuidanceHash);
  TestEqual(TEXT("reverse execution hash"),
    ReversedGuidance.Execution.ExecutionHash,
    Guidance.Execution.ExecutionHash);

  FCrowdMassTargetRegionPlanInput CacheInput = PlanInput;
  CacheInput.PreviousPlan = Plan.Plan;
  CacheInput.PreviousExecution = Plan.Execution;
  CacheInput.FixedStepIndex = 1;
  const FCrowdMassTargetRegionPlanOutput Cached =
    FCrowdMassTargetRegionWork::SolvePlan(CacheInput);
  TestEqual(TEXT("plan cache hit"), Cached.RebuildReason, 0);
  TestEqual(TEXT("cached plan hash"),
    Cached.Plan.TransportHash, Plan.Plan.TransportHash);

  CacheInput.FixedStepIndex = 15;
  const FCrowdMassTargetRegionPlanOutput Lifetime =
    FCrowdMassTargetRegionWork::SolvePlan(CacheInput);
  TestEqual(TEXT("lifetime rebuild"), Lifetime.RebuildReason, 1);
  TestTrue(TEXT("lifetime plan valid"), Lifetime.bValid);
  return true;
}

#endif
