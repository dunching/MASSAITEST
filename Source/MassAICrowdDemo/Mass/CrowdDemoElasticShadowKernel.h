#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoDeterministicOrcaKernel.h"
#include "Mass/CrowdDemoElasticCrowdKernel.h"
#include "Mass/CrowdDemoHardSeparationPbdKernel.h"
#include "Mass/CrowdDemoPursuitPositioningKernel.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

enum class ECrowdDemoElasticShadowStage : uint8
{
  Preferred,
  Orca,
  Predict,
  Obstacle,
  Pbd1,
  Pbd2,
  Pbd3,
  Reproject,
  Count
};

enum class ECrowdDemoElasticShadowFailureKind : uint8
{
  None,
  HardPair,
  ObstaclePenetration,
  TargetExclusion,
  OrcaStopViolation,
  SourceForwardRegression,
  SourceZeroProgress
};

enum class ECrowdDemoElasticShadowAttribution : uint8
{
  None,
  InheritedAtStepStart,
  SharedByBoth,
  ElasticIntroduced,
  ElasticWorsened,
  ElasticImproved
};

struct FCrowdDemoElasticShadowAgentInput
{
  FCrowdDemoElasticCrowdAgent Agent;
  FVector2f FlowDirection = FVector2f::ZeroVector;
  ECrowdDemoFlowLocationStatus FlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
  ECrowdDemoPursuitSteeringState SteeringState = ECrowdDemoPursuitSteeringState::Pursuit;
  FVector2f HoldingLocation = FVector2f::ZeroVector;
  FVector2f AssignedPosition = FVector2f::ZeroVector;
  bool bHasAssignment = false;
};

struct FCrowdDemoElasticShadowAgentStage
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowHardPairWitness
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  float RequiredClearanceCm = 0.0f;
  float ActualDistanceCm = 0.0f;
  float MarginCm = 0.0f;
};

struct FCrowdDemoElasticShadowObstacleDiagnostic
{
  int32 AgentId = INDEX_NONE;
  FCrowdDemoSharedFlowConstraintDiagnostic Constraint;
  FVector2f ConstrainedPosition = FVector2f::ZeroVector;
  FVector2f ConstrainedVelocity = FVector2f::ZeroVector;
  float PositionDeltaCm = 0.0f;
  float VelocityDeltaCmps = 0.0f;
  bool bPenetrating = false;
  bool bClipped = false;
  bool bUsedSlideX = false;
  bool bUsedSlideY = false;
  bool bStopped = false;
};

struct FCrowdDemoElasticShadowStageSummary
{
  ECrowdDemoElasticShadowStage Stage = ECrowdDemoElasticShadowStage::Preferred;
  int32 HardPairViolationCount = 0;
  int32 TargetViolationCount = 0;
  int32 ObstaclePenetrationCount = 0;
  int32 ObstacleClippedCount = 0;
  int32 ObstacleSlideCount = 0;
  int32 ObstacleStoppedCount = 0;
  int32 FlowBoundsHitCount = 0;
  int32 ValidSourceSampleCount = 0;
  int32 ZeroDesiredSourceSampleCount = 0;
  int64 DesiredSourceForwardCmps = 0;
  int64 ActualSourceForwardCmps = 0;
  int32 SourceForwardRatioQ15 = 32767;
  float MinimumHardPairMarginCm = MAX_flt;
  float MaximumHardPairPenetrationCm = 0.0f;
  float MaximumObstacleDeltaCm = 0.0f;
  FCrowdDemoElasticShadowHardPairWitness FirstHardPair;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowSafetyPolishAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
};

struct FCrowdDemoElasticShadowSafetyPolishSummary
{
  bool bValid = false;
  int32 BeforeHardPairViolationCount = 0;
  int32 AfterHardPairViolationCount = 0;
  int32 AppliedPairCount = 0;
  int32 OneSidedCorrectionCount = 0;
  int32 ObstacleRejectedCandidateCount = 0;
  int32 TargetRejectedCandidateCount = 0;
  float BeforeMaximumPenetrationCm = 0.0f;
  float AfterMaximumPenetrationCm = 0.0f;
  TArray<FCrowdDemoElasticShadowSafetyPolishAgent> Agents;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowBranchResult
{
  bool bValid = false;
  TStaticArray<TArray<FCrowdDemoElasticShadowAgentStage>,
    static_cast<int32>(ECrowdDemoElasticShadowStage::Count)> Stages;
  TStaticArray<FCrowdDemoElasticShadowStageSummary,
    static_cast<int32>(ECrowdDemoElasticShadowStage::Count)> StageSummaries;
  TArray<FCrowdDemoElasticShadowObstacleDiagnostic> ObstacleDiagnostics;
  TArray<FCrowdDemoElasticShadowObstacleDiagnostic> ReprojectDiagnostics;
  TArray<FCrowdDemoElasticCrowdResult> ElasticResults;
  FCrowdDemoElasticCrowdSummary ElasticSummary;
  TArray<FCrowdDemoOrcaResult> OrcaResults;
  FCrowdDemoOrcaSummary OrcaSummary;
  TArray<FCrowdDemoHardSeparationPbdPair> PbdPairs;
  TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic> PbdIterations;
  FCrowdDemoElasticShadowSafetyPolishSummary SafetyPolish;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowStepInput
{
  int32 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoElasticShadowAgentInput> Agents;
  FCrowdDemoElasticCrowdSettings ElasticSettings;
  FCrowdDemoElasticCrowdEnvironment Environment;
  FCrowdDemoOrcaSettings OrcaSettings;
  FCrowdDemoHardSeparationPbdSettings PbdSettings;
  float FixedStepSeconds = 1.0f / 30.0f;
};

struct FCrowdDemoElasticShadowTwinResult
{
  bool bValid = false;
  FCrowdDemoElasticShadowBranchResult Baseline;
  FCrowdDemoElasticShadowBranchResult Elastic;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowFixtureAgent
{
  FCrowdDemoElasticShadowAgentInput Input;
  TArray<FCrowdDemoElasticShadowAgentStage> BaselineStages;
  TArray<FCrowdDemoElasticShadowAgentStage> ElasticStages;
  TArray<FCrowdDemoOrcaConstraint> BaselineConstraints;
  TArray<FCrowdDemoOrcaConstraint> ElasticConstraints;
  FCrowdDemoOrcaResult BaselineOrcaResult;
  FCrowdDemoOrcaResult ElasticOrcaResult;
  FCrowdDemoElasticShadowObstacleDiagnostic BaselineObstacle;
  FCrowdDemoElasticShadowObstacleDiagnostic ElasticObstacle;
  FCrowdDemoElasticShadowObstacleDiagnostic BaselineReproject;
  FCrowdDemoElasticShadowObstacleDiagnostic ElasticReproject;
};

struct FCrowdDemoElasticShadowOrcaReplayDiagnostic
{
  bool bConstraintCountsMatch = false;
  bool bConstraintsExactlyMatch = false;
  int32 FirstConstraintMismatchIndex = INDEX_NONE;
  bool bBaselineVelocityInsideElasticSpeedCircle = false;
  bool bBaselineVelocitySatisfiesElasticConstraints = false;
  float BaselineVelocityMinimumElasticResidualCmps = -MAX_flt;
  ECrowdDemoOrcaSolveStatus ElasticContinuousStatus =
    ECrowdDemoOrcaSolveStatus::NumericalFailure;
  FVector2f ElasticContinuousVelocity = FVector2f::ZeroVector;
  ECrowdDemoOrcaQuantizationResult ElasticQuantizationResult =
    ECrowdDemoOrcaQuantizationResult::NoSolution;
  FVector2f ElasticQuantizedVelocity = FVector2f::ZeroVector;
  uint8 ElasticFallbackStage = 0;
};

struct FCrowdDemoElasticShadowFailureFixture
{
  bool bValid = false;
  bool bFixtureTooLarge = false;
  int32 FixedStepIndex = INDEX_NONE;
  ECrowdDemoElasticShadowStage Stage = ECrowdDemoElasticShadowStage::Preferred;
  ECrowdDemoElasticShadowFailureKind FailureKind =
    ECrowdDemoElasticShadowFailureKind::None;
  ECrowdDemoElasticShadowAttribution Attribution =
    ECrowdDemoElasticShadowAttribution::None;
  int32 PrimaryAgentId = INDEX_NONE;
  int32 OtherAgentId = INDEX_NONE;
  int32 ClosureAgentCount = 0;
  int32 ZeroProgressStepMax = 0;
  float OrcaConstraintEpsilonCmps = 0.0f;
  float OrcaVelocityQuantumCmps = 0.0f;
  FCrowdDemoElasticShadowOrcaReplayDiagnostic OrcaReplay;
  TArray<FCrowdDemoElasticShadowFixtureAgent> Agents;
  TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic> BaselinePbdIterations;
  TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic> ElasticPbdIterations;
  TArray<FCrowdDemoElasticShadowStageSummary> BaselineStageSummaries;
  TArray<FCrowdDemoElasticShadowStageSummary> ElasticStageSummaries;
  FCrowdDemoElasticShadowSafetyPolishSummary BaselineSafetyPolish;
  FCrowdDemoElasticShadowSafetyPolishSummary ElasticSafetyPolish;
  uint32 BaselineHash = 2166136261u;
  uint32 ElasticHash = 2166136261u;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowParallelSummary
{
  int32 CompletedStepCount = 0;
  int32 EligibleRecoveryAgentCount = 0;
  int32 BaselineRecoveryCompletedCount = 0;
  int32 ElasticRecoveryCompletedCount = 0;
  int32 BaselineImprovedCount = 0;
  int32 ElasticImprovedCount = 0;
  int32 BaselinePermanentHoleCount = 0;
  int32 ElasticPermanentHoleCount = 0;
  int32 BaselineHardPairViolationCount = 0;
  int32 ElasticHardPairViolationCount = 0;
  int32 BaselineObstaclePenetrationCount = 0;
  int32 ElasticObstaclePenetrationCount = 0;
  int32 BaselineTargetViolationCount = 0;
  int32 ElasticTargetViolationCount = 0;
  int32 BaselineOrcaStopViolationCount = 0;
  int32 ElasticOrcaStopViolationCount = 0;
  int64 BaselineDesiredSourceForwardCmps = 0;
  int64 BaselineActualSourceForwardCmps = 0;
  int64 ElasticDesiredSourceForwardCmps = 0;
  int64 ElasticActualSourceForwardCmps = 0;
  TArray<float> BaselineRecoveryTimesSeconds;
  TArray<float> ElasticRecoveryTimesSeconds;
  TArray<float> BaselineEndErrorsCm;
  TArray<float> ElasticEndErrorsCm;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoElasticShadowParallelState
{
  bool bActive = false;
  bool bCompleted = false;
  int32 StartFixedStepIndex = INDEX_NONE;
  int32 StepIndex = 0;
  TArray<FCrowdDemoElasticShadowAgentInput> BaselineAgents;
  TArray<FCrowdDemoElasticShadowAgentInput> ElasticAgents;
  FCrowdDemoSharedFlowField FrozenFlowField;
  FCrowdDemoPursuitPositioningSettings PositioningSettings;
  TMap<int32, float> BaselineRecoveryStartError;
  TMap<int32, float> ElasticRecoveryStartError;
  TMap<int32, int32> BaselineRecoveryCompletionStep;
  TMap<int32, int32> ElasticRecoveryCompletionStep;
  FCrowdDemoElasticShadowParallelSummary Summary;
};

class MASSAICROWDDEMO_API FCrowdDemoElasticShadowKernel
{
public:
  static bool RunBranch(
    const FCrowdDemoElasticShadowStepInput& Input,
    bool bApplyElastic,
    FCrowdDemoElasticShadowBranchResult& OutResult);

  static bool RunTwinStep(
    const FCrowdDemoElasticShadowStepInput& Input,
    FCrowdDemoElasticShadowTwinResult& OutResult);

  static bool PolishReprojectHardPairs(
    const FCrowdDemoElasticShadowStepInput& Input,
    TConstArrayView<FCrowdDemoElasticShadowSafetyPolishAgent> Agents,
    FCrowdDemoElasticShadowSafetyPolishSummary& OutSummary);

  static bool BuildFirstFailureFixture(
    const FCrowdDemoElasticShadowStepInput& Input,
    const FCrowdDemoElasticShadowTwinResult& Twin,
    TConstArrayView<int32> ZeroProgressAgentIds,
    int32 ZeroProgressStepMax,
    FCrowdDemoElasticShadowFailureFixture& OutFixture);

  static bool InitializeParallelRollout(
    const FCrowdDemoElasticShadowStepInput& Input,
    const FCrowdDemoSharedFlowField& FlowField,
    const FCrowdDemoPursuitPositioningSettings& PositioningSettings,
    FCrowdDemoElasticShadowParallelState& OutState);

  static bool AdvanceParallelRollout(
    const FCrowdDemoElasticShadowStepInput& TemplateInput,
    FCrowdDemoElasticShadowParallelState& InOutState,
    FCrowdDemoElasticShadowTwinResult& OutStep);
};
