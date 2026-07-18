#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoValidCorridorTransitLayoutInput
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
};

struct FCrowdDemoValidCorridorTransitLayoutAgent
{
  int32 AgentId = INDEX_NONE;
  int32 FormationIndex = INDEX_NONE;
  FVector SpawnLocation = FVector::ZeroVector;
};

struct FCrowdDemoValidCorridorTransitLayout
{
  bool bValid = false;
  uint32 LayoutHash = 2166136261u;
  TArray<FCrowdDemoValidCorridorTransitLayoutAgent> Agents;
};

struct FCrowdDemoValidCorridorTransitStepAgent
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  ECrowdDemoFlowLocationStatus FlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
};

struct FCrowdDemoValidCorridorTransitProgress
{
  bool bValid = true;
  int32 LastFixedStepIndex = INDEX_NONE;
  int32 UnreachableSampleCount = 0;
  TSet<int32> WallPassedAgentIds;
  TSet<int32> CorridorExitedAgentIds;
  TSet<int32> CompletedAgentIds;
  TSet<int32> FinalSettledAgentIds;
  TSet<int32> FinalDeadlockAgentIds;
  TMap<int32, int32> ConsecutiveLowSpeedStepsByAgentId;
  TMap<int32, int32> ConsecutivePostCompletionSettledStepsByAgentId;
  TMap<int32, int32> CompletionStepByAgentId;
  int32 GroupCompletionStep = INDEX_NONE;
  int32 GroupSettledStep = INDEX_NONE;
  uint32 ProgressHash = 2166136261u;
};

class MASSAICROWDDEMO_API FCrowdDemoValidCorridorTransitKernel
{
public:
  static constexpr int32 AgentCount = 20;
  static constexpr int32 FormationColumns = 10;
  static constexpr float FormationSpacingCm = 128.0f;
  static constexpr float WallPassPlaneY = -1950.0f;
  static constexpr float CorridorExitPlaneY = -650.0f;
  static constexpr float CompletionPlaneY = 750.0f;
  static constexpr int32 StableExitSteps = 15;
  static constexpr float StableExitSpeedCmps = 10.0f;

  static FCrowdDemoSharedFlowFieldConfig MakeFlowConfig();

  static FCrowdDemoValidCorridorTransitLayout BuildLayout(
    TConstArrayView<FCrowdDemoValidCorridorTransitLayoutInput> Inputs,
    float PhysicalRadiusCm = 42.0f,
    float HardSafetyGapCm = 10.0f,
    const FVector& SpawnOrigin = FVector(0.0f, -2850.0f, 60.0f),
    float LayoutSpacingCm = FormationSpacingCm);

  static void UpdateProgress(
    TConstArrayView<FCrowdDemoValidCorridorTransitStepAgent> Agents,
    int32 FixedStepIndex,
    FCrowdDemoValidCorridorTransitProgress& InOutProgress);

  static bool ShouldHoldCompletedGroup(
    const FCrowdDemoValidCorridorTransitProgress& Progress)
  {
    return Progress.CompletedAgentIds.Num() == AgentCount;
  }
};
