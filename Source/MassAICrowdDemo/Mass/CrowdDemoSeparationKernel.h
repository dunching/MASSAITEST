#pragma once

#include "CoreMinimal.h"

struct FCrowdDemoSeparationKernelAgent
{
  int32 AgentId = INDEX_NONE;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float ContactRadiusCm = 42.0f;
  float SeparationRadiusCm = 78.0f;
};

struct FCrowdDemoSeparationKernelSettings
{
  float CellSizeCm = 96.0f;
  float SoftPushSpeedCmPerSecond = 120.0f;
  float HardPushSpeedCmPerSecond = 260.0f;
};

struct FCrowdDemoSeparationKernelResult
{
  int32 AgentId = INDEX_NONE;
  FVector PushVelocity = FVector::ZeroVector;
  int32 NeighborCount = 0;
  int32 OverlapCount = 0;
  int32 SevereOverlapCount = 0;
  bool bHardSeparation = false;
};

struct FCrowdDemoSeparationKernelSummary
{
  int32 GridCellCount = 0;
  int32 AppliedAgentCount = 0;
  int32 OverlapPairCount = 0;
  int32 SevereOverlapPairCount = 0;
};

class MASSAICROWDDEMO_API FCrowdDemoSeparationKernel
{
public:
  static void Solve(
    TConstArrayView<FCrowdDemoSeparationKernelAgent> Agents,
    const FCrowdDemoSeparationKernelSettings& Settings,
    TArray<FCrowdDemoSeparationKernelResult>& OutResults,
    FCrowdDemoSeparationKernelSummary& OutSummary);
};
