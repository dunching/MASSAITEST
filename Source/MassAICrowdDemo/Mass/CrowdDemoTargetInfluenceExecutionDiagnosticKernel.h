#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoTargetInfluenceExecutionSample
{
  int32 AgentId = INDEX_NONE;
  int32 TargetRevision = 0;
  int32 FixedStepIndex = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  FVector2f DensityRequestedVelocity = FVector2f::ZeroVector;
  FVector2f InfluenceDesiredVelocity = FVector2f::ZeroVector;
  FVector2f MovementPredictVelocity = FVector2f::ZeroVector;
  FVector2f AppliedVelocity = FVector2f::ZeroVector;
  FVector2f PairSoftCorrection = FVector2f::ZeroVector;
  FVector2f EnvironmentSoftCorrection = FVector2f::ZeroVector;
  FVector2f UnifiedHardCorrection = FVector2f::ZeroVector;
  FVector2f FinalSafetyCorrection = FVector2f::ZeroVector;
  float FixedStepSeconds = 1.0f / 30.0f;
  float PhysicalRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  int32 RadialBandIndex = INDEX_NONE;
  int32 AngularSectorIndex = INDEX_NONE;
  int32 DensityDirectionSign = 0;
  int32 DensityLeftWeight = 0;
  int32 DensityCurrentWeight = 0;
  int32 DensityRightWeight = 0;
};

struct FCrowdDemoTargetPolarEnvironmentCell
{
  int32 RadialBandIndex = 0;
  int32 AngularSectorIndex = 0;
  bool bFeasible = false;
  bool bFlowBoundsBlocked = false;
  bool bObstacleBlocked = false;
  bool bOccupied = false;
};

struct FCrowdDemoTargetPolarEnvironmentSummary
{
  bool bValid = false;
  TArray<int32> FeasibleSectorCountByRadialBand;
  int32 OccupiedFeasibleSectorCount = 0;
  int32 OccupiedInfeasiblePolarCellCount = 0;
  int32 FeasibleButUnoccupiedSectorCount = 0;
  int32 LargestEmptyFeasibleSectorRun = 0;
  int32 FlowBoundsInfeasibleCellCount = 0;
  int32 ObstacleInfeasibleCellCount = 0;
  TArray<FCrowdDemoTargetPolarEnvironmentCell> Cells;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoTargetInfluenceExecutionAgentRuntime
{
  int32 AgentId = INDEX_NONE;
  int32 ValidSampleCount = 0;
  int32 RequestedSampleCount = 0;
  int32 DirectionFlipCount = 0;
  int32 AngularSectorTransitionCount = 0;
  int32 RadialBandTransitionCount = 0;
  int32 PreviousDirectionSign = 0;
  int32 PreviousAngularSector = INDEX_NONE;
  int32 PreviousRadialBand = INDEX_NONE;
  int64 RequestedTangentialCmpsQ = 0;
  int64 PredictedTangentialCmpsQ = 0;
  int64 AppliedSameDirectionCmpsQ = 0;
  int64 LostTangentialCmpsQ = 0;
  int64 EnvironmentOpposedCmpsQ = 0;
  int64 ParticleOpposedCmpsQ = 0;
  int32 LastRadialBand = INDEX_NONE;
  int32 LastAngularSector = INDEX_NONE;
  int32 LastDirectionSign = 0;
  int32 LastLeftWeight = 0;
  int32 LastCurrentWeight = 0;
  int32 LastRightWeight = 0;
  float LastRequestedTangentialCmps = 0.0f;
  float LastPredictedTangentialCmps = 0.0f;
  float LastAppliedTangentialCmps = 0.0f;
  bool bLastCellFeasible = false;
};

struct FCrowdDemoTargetInfluenceExecutionRuntime
{
  TArray<FCrowdDemoTargetInfluenceExecutionAgentRuntime> Agents;
  TArray<float> RequestedTangentialSamples;
  TArray<float> PredictedTangentialSamples;
  TArray<float> AppliedTangentialSamples;
  TArray<float> LostTangentialSamples;
  int32 ValidSampleCount = 0;
  int32 RequestedBelowThresholdSampleCount = 0;
  int32 AngularSectorTransitionCount = 0;
  int32 RadialBandTransitionCount = 0;
  int32 DirectionFlipCount = 0;
  uint32 RollingHash = 2166136261u;
  FCrowdDemoTargetPolarEnvironmentSummary Environment;
};

struct FCrowdDemoTargetInfluenceExecutionSummary
{
  bool bValid = false;
  int32 ValidSampleCount = 0;
  int32 RequestedAgentCount = 0;
  int32 RequestedBelowThresholdSampleCount = 0;
  float RequestedTangentialCmpsP50 = 0.0f;
  float RequestedTangentialCmpsP95 = 0.0f;
  float RequestedTangentialCmpsMax = 0.0f;
  float MovementPredictTangentialCmpsP50 = 0.0f;
  float MovementPredictTangentialCmpsP95 = 0.0f;
  float MovementPredictTangentialCmpsMax = 0.0f;
  float AppliedTangentialCmpsP50 = 0.0f;
  float AppliedTangentialCmpsP95 = 0.0f;
  float AppliedTangentialCmpsMax = 0.0f;
  float RequestedToAppliedRatioP50 = 0.0f;
  float RequestedToAppliedRatioP95 = 0.0f;
  float LostTangentialCmpsP50 = 0.0f;
  float LostTangentialCmpsP95 = 0.0f;
  float LostTangentialCmpsMax = 0.0f;
  int32 DirectionFlipAgentCount = 0;
  int32 DirectionFlipCount = 0;
  int32 AngularSectorTransitionCount = 0;
  int32 RadialBandTransitionCount = 0;
  int32 EnvironmentOpposedAgentCount = 0;
  int32 ParticleOpposedAgentCount = 0;
  FCrowdDemoTargetPolarEnvironmentSummary Environment;
  uint32 DiagnosticHash = 2166136261u;
};

struct FCrowdDemoTargetInfluenceExecutionCheckpoint
{
  TArray<FCrowdDemoTargetInfluenceExecutionAgentRuntime> Agents;
  int32 RequestedSampleCount = 0;
  int32 PredictedSampleCount = 0;
  int32 AppliedSampleCount = 0;
  int32 LostSampleCount = 0;
  int32 ValidSampleCount = 0;
  int32 RequestedBelowThresholdSampleCount = 0;
  int32 AngularSectorTransitionCount = 0;
  int32 RadialBandTransitionCount = 0;
  int32 DirectionFlipCount = 0;
  uint32 RollingHash = 2166136261u;
  FCrowdDemoTargetPolarEnvironmentSummary Environment;
};

class FCrowdDemoTargetInfluenceExecutionDiagnosticKernel
{
public:
  static void BuildEnvironmentFeasibility(
    const FVector2f& TargetLocation,
    int32 AngularSectorCount,
    int32 RadialBandCount,
    float RadialBandWidthCm,
    float HardClearanceCm,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    TConstArrayView<int32> OccupiedCellKeys,
    FCrowdDemoTargetPolarEnvironmentSummary& OutSummary);

  static void RecordStep(
    TConstArrayView<FCrowdDemoTargetInfluenceExecutionSample> Samples,
    const FCrowdDemoTargetPolarEnvironmentSummary& Environment,
    FCrowdDemoTargetInfluenceExecutionRuntime& Runtime);

  static void BuildSummary(
    const FCrowdDemoTargetInfluenceExecutionRuntime& Runtime,
    FCrowdDemoTargetInfluenceExecutionSummary& OutSummary);

  static FCrowdDemoTargetInfluenceExecutionCheckpoint MakeCheckpoint(
    const FCrowdDemoTargetInfluenceExecutionRuntime& Runtime);

  static void RestoreCheckpoint(
    const FCrowdDemoTargetInfluenceExecutionCheckpoint& Checkpoint,
    FCrowdDemoTargetInfluenceExecutionRuntime& Runtime);
};
