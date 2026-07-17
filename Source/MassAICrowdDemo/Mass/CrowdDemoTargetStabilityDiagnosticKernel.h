#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoTargetRegionTransportKernel.h"

enum class ECrowdDemoTargetStabilityPrimaryCause : uint8
{
  Stable = 0,
  MergeCapacity = 1,
  TerminalChatter = 2,
  ParticleNotSettled = 3,
  Mixed = 4,
  InsufficientSamples = 5,
  InvalidInput = 6
};

enum class ECrowdDemoTargetRegionCoverageLossStage : uint8
{
  None = 0,
  Demand = 1,
  PlanQuota = 2,
  Guidance = 3,
  TerminalRetention = 4,
  Mixed = 5,
};

struct FCrowdDemoTargetStabilitySettings
{
  int32 ExpectedAgentCount = 20;
  int32 StableWindowSteps = 90;
  int32 PositionWindowSteps = 30;
  int32 MergeBlockedSteps = 15;
  int32 TerminalChatterWindowSteps = 15;
  int32 ParticleSettlingSteps = 15;
  float RequestedSpeedThresholdCmps = 30.0f;
  float AppliedSpeedThresholdCmps = 10.0f;
  float ParticleCorrectionThresholdCm = 1.0f;
  float SoftErrorDeltaThresholdCm = 1.0f;
  float PositionQuantumCm = 1.0f;
};

struct FCrowdDemoTargetStabilityAgentSample
{
  int32 AgentId = INDEX_NONE;
  int32 CurrentCellKey = INDEX_NONE;
  int32 NextCellKey = INDEX_NONE;
  int32 CurrentRegionKey = INDEX_NONE;
  ECrowdDemoTargetRegionGuidanceMode GuidanceMode =
    ECrowdDemoTargetRegionGuidanceMode::Unrouted;
  FVector2f Location = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  FVector2f TargetVelocity = FVector2f::ZeroVector;
  FVector2f DesiredVelocity = FVector2f::ZeroVector;
  FVector2f LocalVelocity = FVector2f::ZeroVector;
  FVector2f PredictedVelocity = FVector2f::ZeroVector;
  FVector2f AppliedVelocity = FVector2f::ZeroVector;
  FVector2f PairSoftCorrection = FVector2f::ZeroVector;
  FVector2f TotalParticleCorrection = FVector2f::ZeroVector;
  bool bTerminal = false;
  bool bTerminalStay = false;
  bool bSupply = false;
  int32 RegionSurplusCount = 0;
  int32 LocalNeighborCount = 0;
  int32 LocalConstraintCount = 0;
  int32 LocalBlockedAgeSteps = 0;
  bool bLocalValid = false;
  bool bLocalGranted = false;
  bool bLocalYielding = false;
};

struct FCrowdDemoTargetStabilityRegionSample
{
  uint32 CohortKey = 0;
  int32 RegionKey = INDEX_NONE;
  int32 AvailableCapacity = 0;
  int32 CurrentPopulation = 0;
  int32 DesiredPopulation = 0;
  int32 Deficit = 0;
  int32 Surplus = 0;
  int32 PrimaryIncomingPlanQuota = 0;
  int32 PrimaryIncomingConsumedQuota = 0;
  int32 GuidanceTargetCount = 0;
  bool bFeasible = false;
  TArray<int32> TerminalAgentIds;
  TArray<int32> TerminalSettleAgentIds;
  TArray<int32> SupplyAgentIds;
};

struct FCrowdDemoTargetStabilityEdgeSample
{
  uint32 CohortKey = 0;
  int32 FromCellKey = INDEX_NONE;
  int32 ToCellKey = INDEX_NONE;
  int32 FromRegionKey = INDEX_NONE;
  int32 ToRegionKey = INDEX_NONE;
  int32 AgentQuota = 0;
  int32 ConsumedQuota = 0;
  bool bToTerminal = false;
};

struct FCrowdDemoTargetStabilityStepSample
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 TargetRevision = INDEX_NONE;
  uint32 FeasibleGraphHash = 0;
  int32 InsideBandCount = 0;
  int32 CoverageCount = 0;
  int32 RequiredCoverageCount = 0;
  float FixedStepSeconds = 1.0f / 30.0f;
  float ParticleSoftErrorCmP95 = 0.0f;
  float ParticleMaxActualCorrectionCm = 0.0f;
  TArray<FCrowdDemoTargetStabilityAgentSample> Agents;
  TArray<FCrowdDemoTargetStabilityRegionSample> Regions;
  TArray<FCrowdDemoTargetStabilityEdgeSample> Edges;
};

struct FCrowdDemoTargetStabilityRuntime
{
  FCrowdDemoTargetStabilitySettings Settings;
  TArray<FCrowdDemoTargetStabilityStepSample> Steps;
  bool bInputValid = true;
};

struct FCrowdDemoTargetStabilityCheckpoint
{
  FCrowdDemoTargetStabilityRuntime Runtime;
};

struct FCrowdDemoTargetStabilitySummary
{
  bool bValid = false;
  ECrowdDemoTargetStabilityPrimaryCause PrimaryCause =
    ECrowdDemoTargetStabilityPrimaryCause::InsufficientSamples;
  uint32 StableHash = 2166136261u;
  int32 SampleStepCount = 0;
  int32 WindowStepCount = 0;
  int32 AgentCount = 0;
  int32 InsideBandMin = 0;
  int32 CoverageMin = 0;
  int32 CoverageRequired = 0;
  int32 ContendedStepCount = 0;
  int32 ContendedGroupCount = 0;
  int32 MergeBlockedAgentCount = 0;
  int32 MergeBlockedMaxConsecutiveSteps = 0;
  int32 TerminalChatterAgentCount = 0;
  int32 TerminalChatterCount = 0;
  int32 AttractionRejectionCycleCount = 0;
  int32 ParticleSettledWindowCount = 0;
  int32 ParticleSettledMaxConsecutiveSteps = 0;
  float TargetRelativeSpeedCmpsP95 = 0.0f;
  float TargetRelativeSpeedCmpsMax = 0.0f;
  float PositionPeakToPeakCmP95 = 0.0f;
  float PositionPeakToPeakCmMax = 0.0f;
  int32 FirstWitnessStep = INDEX_NONE;
  int32 FirstWitnessAgentId = INDEX_NONE;
  int32 FirstWitnessNextCellKey = INDEX_NONE;
  int32 FinalMissingRegionCount = 0;
  uint32 FirstMissingCohortKey = 0;
  int32 FirstMissingRegionKey = INDEX_NONE;
  ECrowdDemoTargetRegionCoverageLossStage FirstMissingRegionStage =
    ECrowdDemoTargetRegionCoverageLossStage::None;
  int32 RegionDemandGapStepCount = 0;
  int32 RegionPlanQuotaGapStepCount = 0;
  int32 RegionGuidanceGapStepCount = 0;
  int32 RegionTerminalRetentionGapStepCount = 0;
  int32 RegionTerminalEnterCount = 0;
  int32 RegionTerminalExitCount = 0;
  int32 FinalSubQuantumSupplyAgentCount = 0;
  int32 FirstSubQuantumSupplyAgentId = INDEX_NONE;
  float MinimumExecutableSpeedCmps = 0.0f;
  TArray<FCrowdDemoTargetStabilityAgentSample> FinalAgents;
  TArray<FCrowdDemoTargetStabilityRegionSample> FinalRegions;
  TArray<FCrowdDemoTargetStabilityEdgeSample> FinalEdges;
};

class MASSAICROWDDEMO_API FCrowdDemoTargetStabilityDiagnosticKernel
{
public:
  static void RecordStep(
    const FCrowdDemoTargetStabilityStepSample& Step,
    FCrowdDemoTargetStabilityRuntime& InOutRuntime);

  static FCrowdDemoTargetStabilityCheckpoint MakeCheckpoint(
    const FCrowdDemoTargetStabilityRuntime& Runtime);

  static void RestoreCheckpoint(
    const FCrowdDemoTargetStabilityCheckpoint& Checkpoint,
    FCrowdDemoTargetStabilityRuntime& InOutRuntime);

  static void BuildSummary(
    const FCrowdDemoTargetStabilityRuntime& Runtime,
    FCrowdDemoTargetStabilitySummary& OutSummary);
};
