#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

struct FCrowdDemoElasticCrowdAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f BasePreferredVelocity = FVector2f::ZeroVector;
  float PhysicalRadiusCm = 42.0f;
  float MaxSpeedCmps = 800.0f;
  int32 ContextScaleQ15 = 32767;
  int32 LocalPriority = 0;
  bool bTransitSource = false;
  float TransitSourceRadiusCm = 42.0f;
  FVector2f TransitDirection = FVector2f::ZeroVector;
};

struct FCrowdDemoElasticCrowdEnvironment
{
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  float TargetExclusionRadiusCm = 0.0f;
  bool bValidateFlowAndObstacles = false;
  bool bConstrainToFlowBounds = false;
  bool bValidateTargetExclusion = false;
};

struct FCrowdDemoElasticCrowdResult
{
  int32 AgentId = INDEX_NONE;
  FVector2f BasePreferredVelocity = FVector2f::ZeroVector;
  FVector2f SpacingDeltaVelocity = FVector2f::ZeroVector;
  FVector2f TransitDeltaVelocity = FVector2f::ZeroVector;
  FVector2f AdjustedPreferredVelocity = FVector2f::ZeroVector;
  int32 SpacingNeighborCount = 0;
  int32 TransitSourceCount = 0;
  int32 PropagationLayer = 0;
  float MaxSpacingDeficitCm = 0.0f;
  float MaxTransitDeficitCm = 0.0f;
};

struct FCrowdDemoElasticCrowdSummary
{
  int32 AgentCount = 0;
  int32 SourceCount = 0;
  int32 SpacingPairCount = 0;
  int32 InfluencedAgentCount = 0;
  int32 PropagationLayerCount = 0;
  int32 InvalidInputCount = 0;
  int32 HardPairViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 FlowBoundsViolationCount = 0;
  int32 TargetViolationCount = 0;
  float MaxSpacingDeficitCm = 0.0f;
  float MaxTransitDeficitCm = 0.0f;
  float MaxResponseSpeedCmps = 0.0f;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoElasticCrowdKernel
{
public:
  static bool Solve(
    TConstArrayView<FCrowdDemoElasticCrowdAgent> Agents,
    const FCrowdDemoElasticCrowdSettings& Settings,
    const FCrowdDemoElasticCrowdEnvironment& Environment,
    TArray<FCrowdDemoElasticCrowdResult>& OutResults,
    FCrowdDemoElasticCrowdSummary& OutSummary);
};
