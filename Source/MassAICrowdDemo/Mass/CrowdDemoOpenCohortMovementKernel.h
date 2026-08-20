#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoSharedFlowFieldKernel.h"
#include "CrowdDemoTargetRegionTransportKernel.h"

struct FCrowdDemoOpenCohortMovementLayoutInput
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
};

struct FCrowdDemoOpenCohortMovementLayoutAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  FVector SpawnLocation = FVector::ZeroVector;
};

struct FCrowdDemoOpenCohortMovementLayout
{
  bool bValid = false;
  uint32 LayoutHash = 2166136261u;
  TArray<FCrowdDemoOpenCohortMovementLayoutAgent> Agents;
};

struct FCrowdDemoOpenCohortMovementProgress
{
  bool bValid = true;
  TSet<int32> FlowApproachEnteredAgentIds;
  TSet<int32> TransportHandoffAgentIds;
  TSet<int32> InsideEffectiveBandAgentIds;
  TSet<int32> CurrentUnroutedAgentIds;
  TSet<int32> TerminalSettledAgentIds;
  int32 TerminalSettledStep = INDEX_NONE;
  uint32 ProgressHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoOpenCohortMovementKernel
{
public:
  static bool ShouldEnablePolarHandoff(ECrowdDemoSoftPressureTestCase TestCase);

  static FCrowdDemoSharedFlowFieldConfig MakeOpenFlowConfig();

  static FCrowdDemoOpenCohortMovementLayout BuildLayout(
    TConstArrayView<FCrowdDemoOpenCohortMovementLayoutInput> Inputs,
    float PhysicalRadiusCm = 42.0f,
    float HardSafetyGapCm = 10.0f,
    int32 FormationColumns = 10,
    float FormationSpacingCm = 128.0f,
    const FVector& SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f));

  static void UpdateProgress(
    TConstArrayView<FCrowdDemoTargetRegionGuidanceResult> Guidance,
    int32 ExpectedAgentCount,
    int32 FixedStepIndex,
    FCrowdDemoOpenCohortMovementProgress& InOutProgress);
};
