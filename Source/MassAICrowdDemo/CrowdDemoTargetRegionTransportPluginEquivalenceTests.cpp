#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CrowdTargetRegionTransportKernel.h"
#include "MassCrowdTargetRegionWork.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetRegionTransportPluginEquivalenceTest,
  "CrowdDemo.SF.TargetRegionTransport.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetRegionTransportPluginEquivalenceTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoSharedFlowFieldConfig DemoFlow;
  DemoFlow.Revision = 91;
  DemoFlow.BoundsMin = FVector(-2000.0f, -2000.0f, 0.0f);
  DemoFlow.BoundsMax = FVector(2000.0f, 2000.0f, 0.0f);
  DemoFlow.CellSizeCm = 100.0f;
  DemoFlow.AgentInflateCm = 48.0f;
  DemoFlow.GoalLocation = FVector::ZeroVector;
  FCrowdSharedFlowFieldConfig CoreFlow;
  CoreFlow.Revision = DemoFlow.Revision;
  CoreFlow.BoundsMin = FVector(DemoFlow.BoundsMin);
  CoreFlow.BoundsMax = FVector(DemoFlow.BoundsMax);
  CoreFlow.CellSizeCm = DemoFlow.CellSizeCm;
  CoreFlow.AgentInflateCm = DemoFlow.AgentInflateCm;
  CoreFlow.GoalLocation = FVector(DemoFlow.GoalLocation);

  FCrowdDemoTargetRegionTransportSettings DemoSettings;
  DemoSettings.MinimumCenterDistanceCm = 152.0f;
  DemoSettings.MaximumCenterDistanceCm = 850.0f;
  DemoSettings.InfluenceBlendWidthCm = 300.0f;
  FCrowdTargetRegionTransportSettings CoreSettings;
  CoreSettings.MinimumCenterDistanceCm = DemoSettings.MinimumCenterDistanceCm;
  CoreSettings.MaximumCenterDistanceCm = DemoSettings.MaximumCenterDistanceCm;
  CoreSettings.InfluenceBlendWidthCm = DemoSettings.InfluenceBlendWidthCm;

  const FCrowdDemoTargetEngagementDecision DemoEngagement =
    FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
      ECrowdDemoTargetDistanceResponsePolicy::AcquireThenHold,
      false, true, false, 700.0f, 152.0f, 850.0f, 100.0f);
  const FCrowdTargetEngagementDecision CoreEngagement =
    FCrowdTargetRegionTransportKernel::ResolveTargetEngagement(
      ECrowdTargetDistanceResponsePolicy::AcquireThenHold,
      false, true, false, 700.0f, 152.0f, 850.0f, 100.0f);
  TestEqual(TEXT("engagement hold"),
    DemoEngagement.bEngagedHold, CoreEngagement.bEngagedHold);
  TestEqual(TEXT("engagement acquired"),
    DemoEngagement.bAcquired, CoreEngagement.bAcquired);
  const FVector2f HoldAgent(700.0f, 0.0f);
  const FVector2f HoldTargetVelocity(80.0f, 40.0f);
  TestTrue(TEXT("engaged hold velocity"),
    FCrowdDemoTargetRegionTransportKernel::ComposeEngagedHoldVelocity(
      HoldAgent, FVector2f::ZeroVector, HoldTargetVelocity, 300.0f).Equals(
      FCrowdTargetRegionTransportKernel::ComposeEngagedHoldVelocity(
        HoldAgent, FVector2f::ZeroVector, HoldTargetVelocity, 300.0f),
      KINDA_SMALL_NUMBER));

  FCrowdDemoTargetPolarTopology DemoTopology;
  FCrowdDemoTargetPolarTopologySummary DemoTopologySummary;
  FCrowdDemoTargetRegionTransportKernel::BuildTopology(
    DemoSettings, DemoFlow, DemoTopology, DemoTopologySummary);
  FCrowdTargetPolarTopology CoreTopology;
  FCrowdTargetPolarTopologySummary CoreTopologySummary;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    CoreSettings, CoreFlow, CoreTopology, CoreTopologySummary);
  TestEqual(TEXT("topology hash"), DemoTopology.TopologyHash, CoreTopology.TopologyHash);
  TestEqual(TEXT("feasible graph hash"),
    DemoTopology.FeasibleGraphHash, CoreTopology.FeasibleGraphHash);
  TestEqual(TEXT("cell count"), DemoTopology.Cells.Num(), CoreTopology.Cells.Num());
  TestEqual(TEXT("edge count"), DemoTopology.Edges.Num(), CoreTopology.Edges.Num());
  TestEqual(TEXT("feasible cell count"),
    DemoTopologySummary.FeasibleCellCount, CoreTopologySummary.FeasibleCellCount);
  FCrowdMassTargetRegionTopologyInput RuntimeTopologyInput;
  RuntimeTopologyInput.Settings = CoreSettings;
  RuntimeTopologyInput.FlowConfig = CoreFlow;
  const FCrowdMassTargetRegionTopologyOutput RuntimeTopology =
    FCrowdMassTargetRegionWork::BuildTopology(RuntimeTopologyInput);
  TestEqual(TEXT("runtime topology hash"),
    RuntimeTopology.Topology.TopologyHash, DemoTopology.TopologyHash);

  TArray<FCrowdDemoTargetRegionTransportAgent> DemoAgents;
  TArray<FCrowdTargetRegionTransportAgent> CoreAgents;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    const float Angle = 0.5f * 2.0f * PI / 16.0f;
    const FVector2f Location = FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 1000.0f;
    FCrowdDemoTargetRegionTransportAgent& DemoAgent =
      DemoAgents.AddDefaulted_GetRef();
    DemoAgent.AgentId = Index + 1;
    DemoAgent.Location = Location;
    DemoAgent.FarFlowPreferredVelocity = -Location.GetSafeNormal() * 600.0f;
    DemoAgent.MaxSpeedCmps = 800.0f;
    FCrowdTargetRegionTransportAgent& CoreAgent = CoreAgents.AddDefaulted_GetRef();
    CoreAgent.AgentId = DemoAgent.AgentId;
    CoreAgent.Location = DemoAgent.Location;
    CoreAgent.FarFlowPreferredVelocity = DemoAgent.FarFlowPreferredVelocity;
    CoreAgent.MaxSpeedCmps = DemoAgent.MaxSpeedCmps;
  }

  FCrowdDemoTargetRegionDemandResult DemoDemand;
  FCrowdDemoTargetRegionTransportKernel::BuildDemand(
    DemoAgents, DemoSettings, DemoFlow, nullptr, DemoTopology, DemoDemand);
  FCrowdTargetRegionDemandResult CoreDemand;
  FCrowdTargetRegionTransportKernel::BuildDemand(
    CoreAgents, CoreSettings, CoreFlow, nullptr, CoreTopology, CoreDemand);
  TestEqual(TEXT("demand hash"), DemoDemand.DemandHash, CoreDemand.DemandHash);
  TestEqual(TEXT("membership hash"),
    DemoDemand.MembershipHash, CoreDemand.MembershipHash);
  TestEqual(TEXT("supply count"),
    DemoDemand.SupplyAgentCount, CoreDemand.SupplyAgentCount);
  FCrowdMassTargetRegionDemandInput RuntimeDemandInput;
  RuntimeDemandInput.Agents = CoreAgents;
  RuntimeDemandInput.Settings = CoreSettings;
  RuntimeDemandInput.FlowConfig = CoreFlow;
  RuntimeDemandInput.Topology = RuntimeTopology.Topology;
  const FCrowdMassTargetRegionDemandOutput RuntimeDemand =
    FCrowdMassTargetRegionWork::BuildDemand(RuntimeDemandInput);
  TestEqual(TEXT("runtime demand hash"),
    RuntimeDemand.Demand.DemandHash, DemoDemand.DemandHash);

  FCrowdDemoTargetRegionFlowPlan DemoPlan;
  FCrowdDemoTargetRegionTransportKernel::SolveTransport(
    DemoTopology, DemoDemand, nullptr, 1, 0, 91, DemoPlan);
  FCrowdTargetRegionFlowPlan CorePlan;
  FCrowdTargetRegionTransportKernel::SolveTransport(
    CoreTopology, CoreDemand, nullptr, 1, 0, 91, CorePlan);
  TestEqual(TEXT("transport hash"), DemoPlan.TransportHash, CorePlan.TransportHash);
  TestEqual(TEXT("routed count"), DemoPlan.RoutedAgentCount, CorePlan.RoutedAgentCount);
  TestEqual(TEXT("unrouted count"),
    DemoPlan.UnroutedAgentCount, CorePlan.UnroutedAgentCount);
  TestEqual(TEXT("physical cost"), DemoPlan.TotalPhysicalCost, CorePlan.TotalPhysicalCost);
  FCrowdMassTargetRegionPlanInput RuntimePlanInput;
  RuntimePlanInput.Topology = RuntimeTopology.Topology;
  RuntimePlanInput.Demand = RuntimeDemand.Demand;
  RuntimePlanInput.FixedStepIndex = 0;
  RuntimePlanInput.TargetRevision = 91;
  RuntimePlanInput.PlanLifetimeSteps = 15;
  const FCrowdMassTargetRegionPlanOutput RuntimePlan =
    FCrowdMassTargetRegionWork::SolvePlan(RuntimePlanInput);
  TestEqual(TEXT("runtime plan hash"),
    RuntimePlan.Plan.TransportHash, DemoPlan.TransportHash);
  FCrowdDemoTargetRegionPlanValidationResult DemoPlanValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
    DemoTopology, DemoDemand, DemoPlan, 91, DemoPlanValidation);
  FCrowdTargetRegionPlanValidationResult CorePlanValidation;
  FCrowdTargetRegionTransportKernel::ValidatePlanForDemand(
    CoreTopology, CoreDemand, CorePlan, 91, CorePlanValidation);
  TestEqual(TEXT("plan validation hash"),
    DemoPlanValidation.ValidationHash, CorePlanValidation.ValidationHash);
  TestEqual(TEXT("plan validation result"),
    DemoPlanValidation.bValid, CorePlanValidation.bValid);

  FCrowdDemoTargetRegionQuotaExecutionState DemoExecution;
  FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(
    DemoPlan, DemoExecution);
  TArray<FCrowdDemoTargetRegionGuidanceResult> DemoGuidance;
  FCrowdDemoTargetRegionGuidanceSummary DemoGuidanceSummary;
  FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
    DemoAgents, DemoSettings, DemoTopology, DemoDemand, DemoPlan,
    DemoExecution, DemoGuidance, DemoGuidanceSummary);
  FCrowdTargetRegionQuotaExecutionState CoreExecution;
  FCrowdTargetRegionTransportKernel::InitializeQuotaExecutionState(
    CorePlan, CoreExecution);
  TArray<FCrowdTargetRegionGuidanceResult> CoreGuidance;
  FCrowdTargetRegionGuidanceSummary CoreGuidanceSummary;
  FCrowdTargetRegionTransportKernel::BuildGuidanceWithExecution(
    CoreAgents, CoreSettings, CoreTopology, CoreDemand, CorePlan,
    CoreExecution, CoreGuidance, CoreGuidanceSummary);
  TestEqual(TEXT("guidance hash"),
    DemoGuidanceSummary.GuidanceHash, CoreGuidanceSummary.GuidanceHash);
  TestEqual(TEXT("execution hash"),
    DemoGuidanceSummary.ExecutionHash, CoreGuidanceSummary.ExecutionHash);
  TestEqual(TEXT("guidance count"), DemoGuidance.Num(), CoreGuidance.Num());
  FCrowdMassTargetRegionGuidanceInput RuntimeGuidanceInput;
  RuntimeGuidanceInput.Agents = CoreAgents;
  RuntimeGuidanceInput.Settings = CoreSettings;
  RuntimeGuidanceInput.Topology = RuntimeTopology.Topology;
  RuntimeGuidanceInput.Demand = RuntimeDemand.Demand;
  RuntimeGuidanceInput.Plan = RuntimePlan.Plan;
  RuntimeGuidanceInput.Execution = RuntimePlan.Execution;
  const FCrowdMassTargetRegionGuidanceOutput RuntimeGuidance =
    FCrowdMassTargetRegionWork::BuildGuidance(RuntimeGuidanceInput);
  TestEqual(TEXT("runtime guidance hash"),
    RuntimeGuidance.Summary.GuidanceHash, DemoGuidanceSummary.GuidanceHash);
  for (int32 Index = 0; Index < DemoGuidance.Num() && Index < CoreGuidance.Num(); ++Index)
  {
    TestEqual(FString::Printf(TEXT("agent %d id"), Index),
      DemoGuidance[Index].AgentId, CoreGuidance[Index].AgentId);
    TestEqual(FString::Printf(TEXT("agent %d mode"), Index),
      static_cast<uint8>(DemoGuidance[Index].Mode),
      static_cast<uint8>(CoreGuidance[Index].Mode));
    TestTrue(FString::Printf(TEXT("agent %d velocity"), Index),
      DemoGuidance[Index].DesiredVelocity.Equals(
        CoreGuidance[Index].DesiredVelocity, KINDA_SMALL_NUMBER));
  }

  FCrowdDemoTargetRegionPlanValidationResult DemoExecutionValidation;
  FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
    DemoTopology, DemoDemand, DemoPlan, DemoExecution, 91,
    DemoExecutionValidation);
  FCrowdTargetRegionPlanValidationResult CoreExecutionValidation;
  FCrowdTargetRegionTransportKernel::ValidateQuotaExecutionState(
    CoreTopology, CoreDemand, CorePlan, CoreExecution, 91,
    CoreExecutionValidation);
  TestEqual(TEXT("execution validation hash"),
    DemoExecutionValidation.ValidationHash,
    CoreExecutionValidation.ValidationHash);

  FCrowdDemoTargetRegionFlowPlan DemoReplacementPlan;
  FCrowdDemoTargetRegionQuotaExecutionState DemoReplacementExecution;
  FCrowdDemoTargetRegionPlanReplacementSummary DemoReplacementSummary;
  FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
    DemoTopology, DemoDemand, DemoPlan, DemoExecution,
    2, 15, 91, DemoReplacementPlan, DemoReplacementExecution,
    DemoReplacementSummary);
  FCrowdTargetRegionFlowPlan CoreReplacementPlan;
  FCrowdTargetRegionQuotaExecutionState CoreReplacementExecution;
  FCrowdTargetRegionPlanReplacementSummary CoreReplacementSummary;
  FCrowdTargetRegionTransportKernel::ReplacePlanPreservingClaims(
    CoreTopology, CoreDemand, CorePlan, CoreExecution,
    2, 15, 91, CoreReplacementPlan, CoreReplacementExecution,
    CoreReplacementSummary);
  TestEqual(TEXT("replacement plan hash"),
    DemoReplacementPlan.TransportHash, CoreReplacementPlan.TransportHash);
  TestEqual(TEXT("replacement execution hash"),
    DemoReplacementExecution.ExecutionHash,
    CoreReplacementExecution.ExecutionHash);
  TestEqual(TEXT("replacement summary hash"),
    DemoReplacementSummary.ReplacementHash,
    CoreReplacementSummary.ReplacementHash);
  return true;
}

#endif
