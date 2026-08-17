#pragma once

#include "CoreMinimal.h"
#include "CrowdTargetRegionTransportKernel.h"

struct FCrowdMassTargetRegionTopologyInput
{
  FCrowdTargetRegionTransportSettings Settings;
  FCrowdSharedFlowFieldConfig FlowConfig;
};

struct FCrowdMassTargetRegionTopologyOutput
{
  FCrowdTargetPolarTopology Topology;
  FCrowdTargetPolarTopologySummary Summary;
  bool bValid = false;
};

struct FCrowdMassTargetRegionDemandInput
{
  TArray<FCrowdTargetRegionTransportAgent> Agents;
  TArray<FCrowdTargetRegionTransportAgent> ExternalAgents;
  FCrowdTargetRegionTransportSettings Settings;
  FCrowdSharedFlowFieldConfig FlowConfig;
  const FCrowdSharedFlowField* SharedFlowField = nullptr;
  FCrowdTargetPolarTopology Topology;
  FCrowdTargetRegionDemandResult PreviousDemand;
  bool bUpdateStaticPopulation = false;
  bool bRefreshSourceAttachments = true;
};

struct FCrowdMassTargetRegionDemandOutput
{
  FCrowdTargetRegionDemandResult Demand;
  bool bUsedStaticUpdate = false;
  bool bValid = false;
};

struct FCrowdMassTargetRegionPlanInput
{
  FCrowdTargetPolarTopology Topology;
  FCrowdTargetRegionDemandResult Demand;
  FCrowdTargetRegionFlowPlan PreviousPlan;
  FCrowdTargetRegionQuotaExecutionState PreviousExecution;
  int32 FixedStepIndex = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  int32 PlanLifetimeSteps = 0;
};

struct FCrowdMassTargetRegionPlanOutput
{
  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionQuotaExecutionState Execution;
  FCrowdTargetRegionPlanReplacementSummary Replacement;
  FCrowdTargetRegionPlanValidationResult Validation;
  int32 RebuildReason = 0;
  double SolverMilliseconds = 0.0;
  bool bValid = false;
};

struct FCrowdMassTargetRegionGuidanceInput
{
  TArray<FCrowdTargetRegionTransportAgent> Agents;
  FCrowdTargetRegionTransportSettings Settings;
  FCrowdTargetPolarTopology Topology;
  FCrowdTargetRegionDemandResult Demand;
  FCrowdTargetRegionFlowPlan Plan;
  FCrowdTargetRegionQuotaExecutionState Execution;
};

struct FCrowdMassTargetRegionGuidanceOutput
{
  TArray<FCrowdTargetRegionGuidanceResult> Results;
  FCrowdTargetRegionGuidanceSummary Summary;
  FCrowdTargetRegionQuotaExecutionState Execution;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdMassTargetRegionWork
{
public:
  static FCrowdMassTargetRegionTopologyOutput BuildTopology(
    const FCrowdMassTargetRegionTopologyInput& Input);

  static FCrowdMassTargetRegionDemandOutput BuildDemand(
    const FCrowdMassTargetRegionDemandInput& Input);

  static FCrowdMassTargetRegionPlanOutput SolvePlan(
    const FCrowdMassTargetRegionPlanInput& Input);

  static FCrowdTargetRegionPlanValidationResult ValidateExecution(
    const FCrowdMassTargetRegionPlanInput& Input);

  static FCrowdMassTargetRegionGuidanceOutput BuildGuidance(
    const FCrowdMassTargetRegionGuidanceInput& Input);

  static FCrowdMassTargetRegionGuidanceOutput BuildGuidanceSharded(
    const FCrowdMassTargetRegionGuidanceInput& Input,
    int32 ShardEntityCount = 128);
};
