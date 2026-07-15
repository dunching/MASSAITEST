#pragma once

#include "CoreMinimal.h"

struct FCrowdDemoTargetInfluenceSettings
{
  bool bEnabled = false;
  float InfluenceBlendWidthCm = 300.0f;
  float RadialGainPerSecond = 2.0f;
  float MaxRadialSpeedCmps = 300.0f;
  float FixedStepSeconds = 1.0f / 30.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  int32 AngularSectorCount = 16;
  float RadialBandWidthCm = 100.0f;
  int32 DensitySmoothingPassCount = 1;
  int32 DensityMinimumDifference = 1;
  float DensitySpeedPerExcessAgentCmps = 20.0f;
  float MaximumDensityTangentialSpeedCmps = 120.0f;
};

struct FCrowdDemoTargetDensityCell
{
  int32 RadialBandIndex = 0;
  int32 AngularSectorIndex = 0;
  int32 AgentCount = 0;
  int32 SmoothedWeight = 0;
};

struct FCrowdDemoTargetDensityField
{
  int32 AngularSectorCount = 0;
  int32 RadialBandCount = 0;
  float RadialBandWidthCm = 0.0f;
  TArray<FCrowdDemoTargetDensityCell> Cells;
  uint32 FieldHash = 0;
  bool bValid = false;
};

struct FCrowdDemoTargetDensitySummary
{
  int32 ContributingAgentCount = 0;
  int32 OccupiedCellCount = 0;
  int32 MaximumCellPopulation = 0;
  int32 DensityGuidedAgentCount = 0;
  int32 ClockwiseAgentCount = 0;
  int32 CounterClockwiseAgentCount = 0;
  float TangentialSpeedCmpsP95 = 0.0f;
  float MaximumTangentialSpeedCmps = 0.0f;
  int32 OccupiedAngularSectorCount = 0;
  int32 MaximumAngularSectorPopulation = 0;
  int32 LargestEmptySectorRun = 0;
  uint32 FieldHash = 0;
};

struct FCrowdDemoTargetInfluenceAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  float MaxSpeedCmps = 0.0f;
  float PhysicalRadiusCm = 0.0f;
  float HardSafetyGapCm = 0.0f;
  FVector2f FarFlowPreferredVelocity = FVector2f::ZeroVector;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  FVector2f TargetVelocity = FVector2f::ZeroVector;
  float TargetPhysicalRadiusCm = 0.0f;
  float TargetHardSafetyGapCm = 0.0f;
  float MinimumCombatCenterDistanceCm = 0.0f;
  float MaximumCombatCenterDistanceCm = 0.0f;
};

struct FCrowdDemoTargetInfluenceResult
{
  int32 AgentId = INDEX_NONE;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
  FVector2f RadialCorrection = FVector2f::ZeroVector;
  FVector2f DensityVelocity = FVector2f::ZeroVector;
  float DistanceToTargetCm = 0.0f;
  float NormalizedMinimumDistanceCm = 0.0f;
  float RadialErrorCm = 0.0f;
  float RelativeSpeedCmps = 0.0f;
  float FollowLagCm = 0.0f;
  int32 InfluenceWeightQ15 = 0;
  int32 RadialBandIndex = INDEX_NONE;
  int32 AngularSectorIndex = INDEX_NONE;
  int32 DensityDirectionSign = 0;
  int32 DensityLeftWeight = 0;
  int32 DensityCurrentWeight = 0;
  int32 DensityRightWeight = 0;
  int32 DensityDifference = 0;
  float TangentialSpeedCmps = 0.0f;
  bool bInsideMinimum = false;
  bool bInsideEffectiveBand = false;
  bool bOutsideMaximum = false;
  bool bValid = false;
  uint32 StableHash = 0;
};

struct FCrowdDemoTargetInfluenceSummary
{
  bool bValid = false;
  int32 AgentCount = 0;
  int32 InfluenceAgentCount = 0;
  int32 InsideEffectiveBandCount = 0;
  int32 OutsideMaximumCount = 0;
  int32 InsideMinimumCount = 0;
  int32 NormalizedMinimumRaisedCount = 0;
  float RadialErrorCmP50 = 0.0f;
  float RadialErrorCmP95 = 0.0f;
  float RadialErrorCmMax = 0.0f;
  float RelativeSpeedCmpsP95 = 0.0f;
  float FollowLagCmP95 = 0.0f;
  int32 OccupiedAngularSectorCount = 0;
  int32 AngularCoverageQ15 = 0;
  int32 MaxAngularSectorPopulation = 0;
  int32 OccupiedRadialBandCount = 0;
  FCrowdDemoTargetDensitySummary Density;
  uint32 StableHash = 2166136261u;
};

class FCrowdDemoTargetInfluenceKernel
{
public:
  static void Solve(
    TConstArrayView<FCrowdDemoTargetInfluenceAgent> Agents,
    const FCrowdDemoTargetInfluenceSettings& Settings,
    TArray<FCrowdDemoTargetInfluenceResult>& OutResults,
    FCrowdDemoTargetInfluenceSummary& OutSummary);

  static void BuildPolarDensityField(
    TConstArrayView<FCrowdDemoTargetInfluenceAgent> Agents,
    const FCrowdDemoTargetInfluenceSettings& Settings,
    FCrowdDemoTargetDensityField& OutField,
    FCrowdDemoTargetDensitySummary& OutSummary);
};
