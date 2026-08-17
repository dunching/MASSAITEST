#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoBidirectionalSwapLayoutInput
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
};

struct FCrowdDemoBidirectionalSwapLayoutAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  int32 CohortId = INDEX_NONE;
  FVector SpawnLocation = FVector::ZeroVector;
};

struct FCrowdDemoBidirectionalSwapLayout
{
  bool bValid = false;
  uint32 LayoutHash = 2166136261u;
  TArray<FCrowdDemoBidirectionalSwapLayoutAgent> Agents;
};

struct FCrowdDemoBidirectionalSwapStepAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  ECrowdDemoFlowLocationStatus FlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
};

struct FCrowdDemoBidirectionalSwapProgress
{
  bool bValid = true;
  int32 LastFixedStepIndex = INDEX_NONE;
  int32 UnreachableSampleCount = 0;
  TSet<int32> CenterCrossedAgentIds;
  TSet<int32> CompletedAgentIds;
  TSet<int32> FinalDeadlockAgentIds;
  TMap<int32, int32> ConsecutiveLowForwardStepsByAgentId;
  TMap<int32, int32> CompletionStepByAgentId;
  uint32 ProgressHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoBidirectionalSwapKernel
{
public:
  static constexpr int32 AgentCount = 20;
  static constexpr int32 AgentsPerCohort = 10;
  static constexpr float SouthSpawnY = -2850.0f;
  static constexpr float NorthSpawnY = 2850.0f;
  static constexpr float SouthCompletionPlaneY = -2200.0f;
  static constexpr float NorthCompletionPlaneY = 2200.0f;
  static constexpr float GoalLateralOffsetCm = 400.0f;

  static int32 CohortIdForFormationIndex(int32 FormationIndex);
  static FCrowdDemoSharedFlowFieldConfig MakeFlowConfig(int32 CohortId);

  static FCrowdDemoBidirectionalSwapLayout BuildLayout(
    TConstArrayView<FCrowdDemoBidirectionalSwapLayoutInput> Inputs,
    float PhysicalRadiusCm = 42.0f,
    float HardSafetyGapCm = 10.0f,
    float FormationSpacingCm = 128.0f);

  static void UpdateProgress(
    TConstArrayView<FCrowdDemoBidirectionalSwapStepAgent> Agents,
    int32 FixedStepIndex,
    FCrowdDemoBidirectionalSwapProgress& InOutProgress);
};
