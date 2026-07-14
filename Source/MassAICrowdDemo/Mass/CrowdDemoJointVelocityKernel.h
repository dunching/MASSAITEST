#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

enum class ECrowdDemoSpacingPairOwner : uint8
{
  None = 0,
  JointSolver = 1,
  SoftSeparation = 2
};

enum class ECrowdDemoJointVelocityStatus : uint8
{
  Solved,
  HardInfeasible,
  IterationLimit,
  ClearanceNotAchieved,
  NoForwardGain,
  NumericalFailure,
  QuantizedValidationFailure,
  OversizeFallback,
  InvalidInput
};

struct FCrowdDemoAdaptiveSpacingSettings
{
  float HardSafetyGapCm = 0.0f;
  float PreferredSpacingGapCm = 0.0f;
  int32 DefaultContextScaleQ15 = 0;
  float FixedStepSeconds = 1.0f / 30.0f;
  float PositionQuantumCm = 1.0f;
  float VelocityQuantumCmps = 1.0f;
  float ConstraintEpsilonCmps = 0.1f;
  float MaximumInputMagnitude = 10000000.0f;
  int32 MaximumWeightQ8 = 1048576;
  int32 MaximumComponentAgents = 8;
  int32 SolverIterations = 32;
  int32 FeasibilityPolishIterations = 32;
  int32 RelaxationQ15 = 8192;
  int32 QuantizationRepairCandidateLimit = 65536;
  float NominalTransitRadiusCm = 42.0f;
  float TransitPredictionHorizonSeconds = 0.75f;
  float YieldBudgetCm = 30.0f;
  int32 TransitClearanceWeightQ8 = 0;
  int32 TransitClearanceSpeedLimitQ15 = 29490;
};

struct FCrowdDemoTransitCapacitySettings
{
  float PhysicalRadiusACm = 42.0f;
  float PhysicalRadiusBCm = 42.0f;
  float NominalTransitRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  float YieldBudgetACm = 30.0f;
  float YieldBudgetBCm = 30.0f;
  int32 RequiredPositionCapacity = 20;
  int32 RequiredHoldingCapacity = 20;
  float PositionQuantumCm = 1.0f;
};

struct FCrowdDemoTransitCapacityCandidate
{
  int32 StableId = INDEX_NONE;
  FVector2f Location = FVector2f::ZeroVector;
};

struct FCrowdDemoTransitApertureResult
{
  int32 HardPairDistanceCm = 0;
  int32 RequiredTransitApertureCm = 0;
  int32 BaselinePairDistanceCm = 0;
  int32 PreferredSpacingGapCm = 0;
  int32 AvailableTransitRadiusCm = 0;
  int32 ApertureDeficitCm = 0;
  int32 YieldBudgetRequiredCm = 0;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTransitCapacityResult
{
  FCrowdDemoTransitApertureResult Aperture;
  TArray<int32> SelectedPositionIds;
  TArray<int32> SelectedHoldingIds;
  int32 PositionCapacity = 0;
  int32 HoldingCapacity = 0;
  int32 PositionCapacityDeficit = 0;
  int32 HoldingCapacityDeficit = 0;
  uint32 CapacityHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoJointVelocityAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f BaselinePriorityOrcaVelocity = FVector2f::ZeroVector;
  FVector2f AssignedPosition = FVector2f::ZeroVector;
  FVector2f ExternalVelocity = FVector2f::ZeroVector;
  float PhysicalRadiusCm = 42.0f;
  float MaxSpeedCmps = 800.0f;
  float AssignedSpacingCm = 0.0f;
  int32 MotionWeightQ8 = 256;
  int32 RecoveryWeightQ8 = 0;
  bool bHasAssignedPosition = false;
  bool bTransitSeed = false;
  bool bExternalVelocityFixed = false;
};

struct FCrowdDemoTransitIntent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  FVector2f PredictedEnd = FVector2f::ZeroVector;
  float PhysicalRadiusCm = 42.0f;
  float NominalClearanceRadiusCm = 42.0f;
  float PredictionHorizonSeconds = 0.75f;
  int32 PriorityQ8 = 256;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTransitClearancePair
{
  int32 TransitAgentId = INDEX_NONE;
  int32 YieldingAgentId = INDEX_NONE;
  FVector2f CapsuleStart = FVector2f::ZeroVector;
  FVector2f CapsuleEnd = FVector2f::ZeroVector;
  float RequiredCenterClearanceCm = 0.0f;
  float InitialClearanceDeficitCm = 0.0f;
};

struct FCrowdDemoJointVelocityEnvironment
{
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  FVector2f TargetLocation = FVector2f::ZeroVector;
  float TargetExclusionRadiusCm = 0.0f;
  bool bValidateFlowAndObstacles = false;
  bool bValidateTargetExclusion = false;
};

struct FCrowdDemoJointVelocityPair
{
  int32 AgentAId = INDEX_NONE;
  int32 AgentBId = INDEX_NONE;
  float HardSafetyGapCm = 0.0f;
  float PreferredSpacingGapCm = 0.0f;
  int32 ContextScaleQ15 = 0;
  int32 SpacingWeightQ8 = 256;
  uint8 RequestedOwnerMask = 0;
  ECrowdDemoSpacingPairOwner Owner = ECrowdDemoSpacingPairOwner::None;
  FCrowdDemoOrcaCanonicalPairGeometry Canonical;
};

struct FCrowdDemoJointVelocityComponent
{
  int32 ComponentId = INDEX_NONE;
  TArray<int32> AgentIds;
  TArray<int32> DirectTransitRelevantAgentIds;
  TArray<int32> HardSafetyClosureAgentIds;
  TArray<int32> PairIndexes;
  bool bOversize = false;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoJointVelocityAgentResult
{
  int32 AgentId = INDEX_NONE;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f JointCandidateVelocity = FVector2f::ZeroVector;
  FVector2f BaselineVelocity = FVector2f::ZeroVector;
  bool bUsedJointVelocity = false;
  bool bJointCandidateFinite = false;
};

struct FCrowdDemoJointVelocityPairResidual
{
  int32 AgentAId = INDEX_NONE;
  int32 AgentBId = INDEX_NONE;
  float JointHardDeficitCm = 0.0f;
  float BaselineHardDeficitCm = 0.0f;
  float JointCanonicalDeficitCmps = 0.0f;
  float BaselineCanonicalDeficitCmps = 0.0f;
  float JointPreferredSpacingDeficitCm = 0.0f;
  float BaselinePreferredSpacingDeficitCm = 0.0f;
};

struct FCrowdDemoJointVelocityComponentResult
{
  int32 ComponentId = INDEX_NONE;
  ECrowdDemoJointVelocityStatus Status = ECrowdDemoJointVelocityStatus::InvalidInput;
  TArray<FCrowdDemoJointVelocityAgentResult> Agents;
  int32 PairCount = 0;
  int32 YieldingAgentCount = 0;
  int32 HardPairDistanceViolationCount = 0;
  int32 JointCandidateHardPairViolationCount = 0;
  int32 BaselineFallbackHardPairViolationCount = 0;
  int32 PreferredSpacingSatisfiedPairCount = 0;
  int32 SpacingCompressedPairCount = 0;
  float PreferredSpacingDeficitCmMax = 0.0f;
  int32 TransitClearancePairCount = 0;
  int32 TransitYieldingAgentCount = 0;
  float TransitCapsuleClearanceDeficitCmMax = 0.0f;
  float JointCandidateClearanceDeficitCmMax = 0.0f;
  float BaselineFallbackClearanceDeficitCmMax = 0.0f;
  float MaximumYieldDisplacementCm = 0.0f;
  int32 FlowBoundsViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 TargetViolationCount = 0;
  int32 JointCandidateFlowBoundsViolationCount = 0;
  int32 JointCandidateObstacleViolationCount = 0;
  int32 JointCandidateTargetViolationCount = 0;
  int32 BaselineFallbackFlowBoundsViolationCount = 0;
  int32 BaselineFallbackObstacleViolationCount = 0;
  int32 BaselineFallbackTargetViolationCount = 0;
  TArray<FCrowdDemoJointVelocityPairResidual> PairResiduals;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoJointVelocitySummary
{
  int32 ComponentCount = 0;
  int32 MaximumComponentSize = 0;
  int32 OversizeCount = 0;
  int32 SolvedCount = 0;
  int32 HardInfeasibleCount = 0;
  int32 IterationLimitCount = 0;
  int32 ClearanceNotAchievedCount = 0;
  int32 NoForwardGainCount = 0;
  int32 NumericalFailureCount = 0;
  int32 QuantizedValidationFailureCount = 0;
  int32 QuantizationRepairCount = 0;
  int32 QuantizationRepairSearchExhaustedCount = 0;
  int32 InvalidInputCount = 0;
  int32 YieldingAgentCount = 0;
  int32 PreferredSpacingSatisfiedPairCount = 0;
  int32 SpacingCompressedPairCount = 0;
  int32 HardPairDistanceViolationCount = 0;
  int32 JointCandidateHardPairViolationCount = 0;
  int32 BaselineFallbackHardPairViolationCount = 0;
  int32 SpacingPairDoubleOwnerCount = 0;
  int32 DegenerateNormalCount = 0;
  int32 TransitIntentCount = 0;
  int32 TransitDirectRelevantAgentCount = 0;
  int32 HardSafetyClosureAgentCount = 0;
  int32 TransitClearancePairCount = 0;
  int32 TransitYieldingAgentCount = 0;
  float TransitCapsuleClearanceDeficitCmMax = 0.0f;
  float JointCandidateClearanceDeficitCmMax = 0.0f;
  float BaselineFallbackClearanceDeficitCmMax = 0.0f;
  float MaximumYieldDisplacementCm = 0.0f;
  int32 FlowBoundsViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 TargetViolationCount = 0;
  int32 JointCandidateFlowBoundsViolationCount = 0;
  int32 JointCandidateObstacleViolationCount = 0;
  int32 JointCandidateTargetViolationCount = 0;
  int32 BaselineFallbackFlowBoundsViolationCount = 0;
  int32 BaselineFallbackObstacleViolationCount = 0;
  int32 BaselineFallbackTargetViolationCount = 0;
  uint32 PairOwnerHash = 2166136261u;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoTransitCapacityShadowSummary
{
  int32 ComponentCount = 0;
  int32 MaximumComponentSize = 0;
  int32 Component2Count = 0;
  int32 Component5Count = 0;
  int32 Component8Count = 0;
  int32 Component12Count = 0;
  int32 Component20Count = 0;
  int32 OversizeCount = 0;
  int32 SolvedCount = 0;
  int32 InfeasibleCount = 0;
  int32 HardInfeasibleCount = 0;
  int32 IterationLimitCount = 0;
  int32 ClearanceNotAchievedCount = 0;
  int32 NoForwardGainCount = 0;
  int32 InvalidInputCount = 0;
  int32 NumericalFailureCount = 0;
  int32 QuantizedFailureCount = 0;
  int32 YieldingAgentCount = 0;
  int32 TransitDirectRelevantAgentCount = 0;
  int32 HardSafetyClosureAgentCount = 0;
  int32 HardPairViolationCount = 0;
  int32 JointCandidateHardPairViolationCount = 0;
  int32 BaselineFallbackHardPairViolationCount = 0;
  int32 ObstacleViolationCount = 0;
  int32 FlowBoundsViolationCount = 0;
  int32 TargetViolationCount = 0;
  int32 JointCandidateFlowBoundsViolationCount = 0;
  int32 JointCandidateObstacleViolationCount = 0;
  int32 JointCandidateTargetViolationCount = 0;
  int32 BaselineFallbackFlowBoundsViolationCount = 0;
  int32 BaselineFallbackObstacleViolationCount = 0;
  int32 BaselineFallbackTargetViolationCount = 0;
  int32 PairDoubleOwnerCount = 0;
  int32 TransitForwardSpeedRatioQ15 = 0;
  float PreferredSpacingDeficitCmMax = 0.0f;
  float ApertureDeficitCmMax = 0.0f;
  float TransitCapsuleClearanceDeficitCmMax = 0.0f;
  float JointCandidateClearanceDeficitCmMax = 0.0f;
  float BaselineFallbackClearanceDeficitCmMax = 0.0f;
  float MaximumYieldDisplacementCm = 0.0f;
  float SolverMs = 0.0f;
  uint32 ComponentHash = 2166136261u;
  uint32 JointHash = 2166136261u;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoTransitCapacityEnvironmentDiagnostic
{
  int32 AgentId = INDEX_NONE;
  FCrowdDemoSharedFlowConstraintDiagnostic JointCandidateConstraint;
  FCrowdDemoSharedFlowConstraintDiagnostic BaselineConstraint;
  bool bJointCandidateTargetViolation = false;
  bool bBaselineTargetViolation = false;
};

struct FCrowdDemoTransitCapacityFailureFixture
{
  FCrowdDemoJointVelocityComponent Component;
  TArray<FCrowdDemoJointVelocityAgent> Agents;
  TArray<FCrowdDemoJointVelocityPair> Pairs;
  TArray<FCrowdDemoTransitIntent> TransitIntents;
  FCrowdDemoJointVelocityComponentResult Result;
  TArray<FCrowdDemoTransitCapacityEnvironmentDiagnostic> EnvironmentDiagnostics;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

enum class ECrowdDemoTransitDownstreamZeroStage : uint8
{
  None,
  MovementPredict,
  ObstacleConstraint,
  HardPbd,
  ObstacleReproject,
  MovementFinalize
};

struct FCrowdDemoTransitJointDiagnosticAgent
{
  FCrowdDemoJointVelocityAgent JointAgent;
  int32 SteeringState = INDEX_NONE;
  FVector2f StartLocation = FVector2f::ZeroVector;
  FVector2f PredictedLocation = FVector2f::ZeroVector;
  FVector2f ObstacleLocation = FVector2f::ZeroVector;
  FVector2f PbdLocation = FVector2f::ZeroVector;
  FVector2f ReprojectLocation = FVector2f::ZeroVector;
  FVector2f FinalLocation = FVector2f::ZeroVector;
  FVector2f PriorityOrcaVelocity = FVector2f::ZeroVector;
  FVector2f PredictedVelocity = FVector2f::ZeroVector;
  FVector2f ObstacleVelocity = FVector2f::ZeroVector;
  FVector2f PbdVelocity = FVector2f::ZeroVector;
  FVector2f ReprojectVelocity = FVector2f::ZeroVector;
  FVector2f FinalVelocity = FVector2f::ZeroVector;
  FVector2f PbdCorrection = FVector2f::ZeroVector;
  FVector2f ObstacleReprojectDelta = FVector2f::ZeroVector;
  TArray<FCrowdDemoOrcaConstraint> PriorityConstraints;
};

struct FCrowdDemoTransitJointDiagnosticSummary
{
  int32 PrimaryAgentId = INDEX_NONE;
  int32 ComponentAgentCount = 0;
  int32 ComponentPairCount = 0;
  int32 ConstraintCount = 0;
  int32 PriorityForwardSpeedCmps = 0;
  int32 PredictedSpeedCmps = 0;
  int32 ObstacleSpeedCmps = 0;
  int32 PbdSpeedCmps = 0;
  int32 ReprojectSpeedCmps = 0;
  int32 FinalSpeedCmps = 0;
  int32 JointForwardSpeedCmps = 0;
  int32 JointHardViolationCount = 0;
  ECrowdDemoJointVelocityStatus JointStatus = ECrowdDemoJointVelocityStatus::InvalidInput;
  ECrowdDemoTransitDownstreamZeroStage DownstreamZeroStage =
    ECrowdDemoTransitDownstreamZeroStage::None;
  bool bFixtureTooLarge = false;
  bool bPriorityNonZeroDownstreamZero = false;
  bool bJointQuantizedSafeForward = false;
};

struct FCrowdDemoTransitJointDiagnosticFixture
{
  TArray<FCrowdDemoTransitJointDiagnosticAgent> Agents;
  TArray<FCrowdDemoJointVelocityPair> Pairs;
  FCrowdDemoJointVelocityComponentResult JointResult;
  FCrowdDemoTransitJointDiagnosticSummary Summary;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoJointVelocityKernel
{
public:
  static void EvaluateTransitAperture(
    const FCrowdDemoTransitCapacitySettings& Settings,
    float CurrentPairDistanceCm,
    FCrowdDemoTransitApertureResult& OutResult);

  static void EvaluateTransitCapacity(
    const FCrowdDemoTransitCapacitySettings& Settings,
    TConstArrayView<FCrowdDemoTransitCapacityCandidate> PositionCandidates,
    TConstArrayView<FCrowdDemoTransitCapacityCandidate> HoldingCandidates,
    FCrowdDemoTransitCapacityResult& OutResult);

  static float HardPairDistanceCm(
    float PhysicalRadiusACm, float PhysicalRadiusBCm, float PairHardSafetyGapCm);

  static float PreferredPairDistanceCm(
    float HardPairDistanceCm, float PairPreferredSpacingGapCm, int32 ContextScaleQ15);

  static bool BuildPair(
    const FCrowdDemoJointVelocityAgent& AgentA,
    const FCrowdDemoJointVelocityAgent& AgentB,
    const FCrowdDemoAdaptiveSpacingSettings& SpacingSettings,
    const FCrowdDemoOrcaSettings& OrcaSettings,
    FCrowdDemoJointVelocityPair& OutPair);

  static bool BuildTransitIntents(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    TArray<FCrowdDemoTransitIntent>& OutIntents,
    uint32& OutHash);

  static bool ValidateComponentEnvironment(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    const FCrowdDemoJointVelocityComponentResult& Result,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    const FCrowdDemoJointVelocityEnvironment& Environment,
    FCrowdDemoJointVelocityComponentResult& OutValidatedResult);

  static bool BuildLocalComponents(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    TArray<FCrowdDemoJointVelocityPair>& InOutPairs,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    TArray<FCrowdDemoJointVelocityComponent>& OutComponents,
    FCrowdDemoJointVelocitySummary& OutSummary);

  static void Solve(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
    TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    TArray<FCrowdDemoJointVelocityComponentResult>& OutResults,
    FCrowdDemoJointVelocitySummary& InOutSummary);

  static void BuildTransitCapacityFailureFixture(
    TConstArrayView<FCrowdDemoJointVelocityAgent> Agents,
    TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
    TConstArrayView<FCrowdDemoJointVelocityComponent> Components,
    TConstArrayView<FCrowdDemoJointVelocityComponentResult> Results,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    const FCrowdDemoJointVelocityEnvironment& Environment,
    FCrowdDemoTransitCapacityFailureFixture& OutFixture);

  static void BuildDiagnosticFixture(
    int32 PrimaryAgentId,
    TConstArrayView<FCrowdDemoTransitJointDiagnosticAgent> Agents,
    TConstArrayView<FCrowdDemoJointVelocityPair> Pairs,
    const FCrowdDemoAdaptiveSpacingSettings& Settings,
    FCrowdDemoTransitJointDiagnosticFixture& OutFixture);
};
