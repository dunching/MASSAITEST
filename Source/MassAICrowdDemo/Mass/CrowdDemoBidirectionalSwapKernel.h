#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoSharedFlowFieldKernel.h"
#include "MassCrowdWorkerFlowBinding.h"

struct FCrowdDemoBidirectionalSwapLayoutInput
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
};

struct FCrowdDemoBidirectionalSwapLayoutAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  uint32 CohortKey = 0;
  FCrowdWorkerObjectiveRef ObjectiveRef;
  uint64 FlowResourceId = 0;
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
  uint32 CohortKey = 0;
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
  static constexpr uint32 NorthboundCohortKey = 1;
  static constexpr uint32 SouthboundCohortKey = 2;
  static constexpr uint64 NorthObjectiveId = 0x330001ull;
  static constexpr uint64 SouthObjectiveId = 0x330002ull;
  static constexpr uint64 NorthFlowResourceId =
    CrowdWorkerResourceIds::FlowResource(0x330101ull);
  static constexpr uint64 SouthFlowResourceId =
    CrowdWorkerResourceIds::FlowResource(0x330102ull);

  static bool IsCohortKeyValid(uint32 CohortKey);
  static FCrowdWorkerObjectiveRef ObjectiveForCohort(uint32 CohortKey);
  static uint64 FlowResourceForCohort(uint32 CohortKey);
  static FCrowdDemoSharedFlowFieldConfig MakeFlowConfig(uint32 CohortKey);

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
