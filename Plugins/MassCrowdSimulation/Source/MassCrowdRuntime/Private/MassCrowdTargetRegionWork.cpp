#include "MassCrowdTargetRegionWork.h"

FCrowdMassTargetRegionTopologyOutput FCrowdMassTargetRegionWork::BuildTopology(
  const FCrowdMassTargetRegionTopologyInput& Input)
{
  FCrowdMassTargetRegionTopologyOutput Output;
  FCrowdTargetRegionTransportKernel::BuildTopology(
    Input.Settings, Input.FlowConfig, Output.Topology, Output.Summary);
  Output.bValid = Output.Topology.bValid && Output.Summary.bValid;
  return Output;
}

FCrowdMassTargetRegionDemandOutput FCrowdMassTargetRegionWork::BuildDemand(
  const FCrowdMassTargetRegionDemandInput& Input)
{
  FCrowdMassTargetRegionDemandOutput Output;
  Output.Demand = Input.PreviousDemand;
  Output.bUsedStaticUpdate = Input.bUpdateStaticPopulation;
  if (Input.bUpdateStaticPopulation)
  {
    FCrowdTargetRegionTransportKernel::UpdateStaticDemandPopulation(
      Input.Agents, Input.Settings, Input.FlowConfig, Input.SharedFlowField,
      Input.Topology, Output.Demand, Input.ExternalAgents,
      Input.bRefreshSourceAttachments);
  }
  else
  {
    FCrowdTargetRegionTransportKernel::BuildDemand(
      Input.Agents, Input.Settings, Input.FlowConfig, Input.SharedFlowField,
      Input.Topology, Output.Demand, Input.ExternalAgents);
  }
  Output.bValid = Output.Demand.bValid;
  return Output;
}

FCrowdMassTargetRegionPlanOutput FCrowdMassTargetRegionWork::SolvePlan(
  const FCrowdMassTargetRegionPlanInput& Input)
{
  FCrowdMassTargetRegionPlanOutput Output;
  Output.Plan = Input.PreviousPlan;
  Output.Execution = Input.PreviousExecution;
  FCrowdTargetRegionTransportKernel::ValidateQuotaExecutionState(
    Input.Topology, Input.Demand, Output.Plan, Output.Execution,
    Input.TargetRevision, Output.Validation);
  if (!Output.Plan.bValid) Output.RebuildReason = 7;
  else if (Output.Plan.TargetRevision != Input.TargetRevision) Output.RebuildReason = 2;
  else if (Output.Plan.FeasibleGraphHash != Input.Topology.FeasibleGraphHash) Output.RebuildReason = 3;
  else if (Output.Plan.MembershipHash != Input.Demand.MembershipHash) Output.RebuildReason = 4;
  else if (Input.FixedStepIndex - Output.Plan.BuildFixedStepIndex
    >= Input.PlanLifetimeSteps) Output.RebuildReason = 1;
  else if (Input.Demand.TotalDeficit == 0 && Output.Plan.RoutedAgentCount > 0)
    Output.RebuildReason = 5;
  else if (!Output.Validation.bValid) Output.RebuildReason = 6;

  if (Output.RebuildReason != 0)
  {
    const double StartSeconds = FPlatformTime::Seconds();
    FCrowdTargetRegionFlowPlan NewPlan;
    FCrowdTargetRegionQuotaExecutionState NewExecution;
    FCrowdTargetRegionTransportKernel::ReplacePlanPreservingClaims(
      Input.Topology, Input.Demand, Output.Plan, Output.Execution,
      FMath::Max(1, Output.Plan.PlanEpoch + 1), Input.FixedStepIndex,
      Input.TargetRevision, NewPlan, NewExecution, Output.Replacement);
    Output.SolverMilliseconds =
      (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    Output.Plan = MoveTemp(NewPlan);
    Output.Execution = MoveTemp(NewExecution);
    FCrowdTargetRegionPlanValidationResult InitialValidation;
    FCrowdTargetRegionTransportKernel::ValidatePlanForDemand(
      Input.Topology, Input.Demand, Output.Plan, Input.TargetRevision,
      InitialValidation);
    if (!InitialValidation.bValid) Output.Plan.bValid = false;
    FCrowdTargetRegionTransportKernel::ValidateQuotaExecutionState(
      Input.Topology, Input.Demand, Output.Plan, Output.Execution,
      Input.TargetRevision, Output.Validation);
  }
  Output.bValid = Output.Plan.bValid && Output.Validation.bValid;
  return Output;
}

FCrowdTargetRegionPlanValidationResult
FCrowdMassTargetRegionWork::ValidateExecution(
  const FCrowdMassTargetRegionPlanInput& Input)
{
  FCrowdTargetRegionPlanValidationResult Output;
  FCrowdTargetRegionTransportKernel::ValidateQuotaExecutionState(
    Input.Topology, Input.Demand, Input.PreviousPlan,
    Input.PreviousExecution, Input.TargetRevision, Output);
  return Output;
}

FCrowdMassTargetRegionGuidanceOutput
FCrowdMassTargetRegionWork::BuildGuidance(
  const FCrowdMassTargetRegionGuidanceInput& Input)
{
  FCrowdMassTargetRegionGuidanceOutput Output;
  Output.Execution = Input.Execution;
  FCrowdTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Input.Agents, Input.Settings, Input.Topology, Input.Demand, Input.Plan,
    Output.Execution, Output.Results, Output.Summary);
  Output.bValid = Output.Summary.bValid;
  return Output;
}

FCrowdMassTargetRegionGuidanceOutput
FCrowdMassTargetRegionWork::BuildGuidanceSharded(
  const FCrowdMassTargetRegionGuidanceInput& Input,
  const int32 ShardEntityCount)
{
  FCrowdMassTargetRegionGuidanceOutput Output;
  Output.Execution = Input.Execution;
  FCrowdTargetRegionTransportKernel::BuildGuidanceWithExecution(
    Input.Agents, Input.Settings, Input.Topology, Input.Demand,
    Input.Plan, Output.Execution, Output.Results, Output.Summary,
    FMath::Max(1, ShardEntityCount));
  Output.bValid = Output.Summary.bValid;
  return Output;
}
