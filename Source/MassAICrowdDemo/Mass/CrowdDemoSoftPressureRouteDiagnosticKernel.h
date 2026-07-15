#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

enum class ECrowdDemoSoftPressureRouteBranch : uint8
{
  Unclassified = 0,
  GoalCompletionOscillation = 1,
  SoftPressureOpposition = 2,
  FlowContract = 3,
  DeadlockMetricOnly = 4,
  TimeLimited = 5,
  MixedEvidence = 6,
  CorridorContract = 7,
};

struct FCrowdDemoSoftPressureRouteStepSample
{
  int32 AgentId = INDEX_NONE;
  int32 FixedStepIndex = INDEX_NONE;
  FVector PredictStartLocation = FVector::ZeroVector;
  FVector Location = FVector::ZeroVector;
  FVector Goal = FVector::ZeroVector;
  int32 FlowCellIndex = INDEX_NONE;
  int32 FlowStableCellKey = INDEX_NONE;
  ECrowdDemoFlowLocationStatus FlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
  int32 IntegrationCost = MAX_int32;
  FVector FlowDirection = FVector::ZeroVector;
  FVector DesiredVelocity = FVector::ZeroVector;
  FVector PredictedVelocity = FVector::ZeroVector;
  FVector AppliedVelocity = FVector::ZeroVector;
  FVector PairSoftRequestedCorrection = FVector::ZeroVector;
  FVector PairSoftRealizedCorrection = FVector::ZeroVector;
  FVector EnvironmentSoftRequestedCorrection = FVector::ZeroVector;
  FVector EnvironmentSoftRealizedCorrection = FVector::ZeroVector;
  FVector UnifiedHardCorrection = FVector::ZeroVector;
  FVector TotalParticleCorrection = FVector::ZeroVector;
  TArray<int32> ActiveNeighborAgentIds;
  float FixedStepSeconds = 1.0f / 30.0f;
  float MaxSpeedCmps = 900.0f;
};

struct FCrowdDemoSoftPressureRouteAgentAccumulator
{
  int32 AgentId = INDEX_NONE;
  int32 SampleCount = 0;
  FVector FinalLocation = FVector::ZeroVector;
  FVector FinalFlowDirection = FVector::ZeroVector;
  FVector FinalDesiredVelocity = FVector::ZeroVector;
  FVector FinalPredictedVelocity = FVector::ZeroVector;
  FVector FinalAppliedVelocity = FVector::ZeroVector;
  FVector FinalPairSoftRequestedCorrection = FVector::ZeroVector;
  FVector FinalPairSoftRealizedCorrection = FVector::ZeroVector;
  FVector FinalEnvironmentSoftRequestedCorrection = FVector::ZeroVector;
  FVector FinalEnvironmentSoftRealizedCorrection = FVector::ZeroVector;
  FVector FinalUnifiedHardCorrection = FVector::ZeroVector;
  FVector FinalTotalParticleCorrection = FVector::ZeroVector;
  TArray<int32> FinalActiveNeighborAgentIds;
  float FinalGoalDistanceCm = 0.0f;
  float MinimumGoalDistanceCm = TNumericLimits<float>::Max();
  float FinalDesiredForwardCmps = 0.0f;
  float FinalAppliedForwardCmps = 0.0f;
  int32 FinalFlowCellIndex = INDEX_NONE;
  int32 FinalFlowStableCellKey = INDEX_NONE;
  ECrowdDemoFlowLocationStatus FinalFlowStatus = ECrowdDemoFlowLocationStatus::OutOfBounds;
  int32 FinalIntegrationCost = MAX_int32;
  bool bCurrentInsideGoal = false;
  bool bEverReachedGoal = false;
  bool bPreviousInsideGoal = false;
  bool bHasPreviousInsideGoal = false;
  int32 ReachedThenLeftCount = 0;
  int32 GoalBoundaryTransitionCount = 0;
  int32 ZeroToMaxSpeedTransitionCount = 0;
  int32 MaxToZeroSpeedTransitionCount = 0;
  int8 PreviousDesiredRegime = -1;
  bool bWallReached = false;
  bool bCorridorReached = false;
  bool bTurnReached = false;
  int32 FirstWallStep = INDEX_NONE;
  int32 FirstCorridorStep = INDEX_NONE;
  int32 FirstTurnStep = INDEX_NONE;
  int32 CurrentLowSpeedSteps = 0;
  int32 MaxLowSpeedSteps = 0;
  bool bEverCorridorStalled = false;
  bool bFinalCorridorDeadlock = false;
  int32 FlowContractViolationCount = 0;
  int32 FirstFlowContractViolationStep = INDEX_NONE;
  int32 FirstFlowContractViolationMask = 0;
  float FirstFlowContractViolationPredictDistanceCm = 0.0f;
};

struct FCrowdDemoSoftPressureRouteCompactSample
{
  int32 AgentId = INDEX_NONE;
  float GoalDistanceCm = 0.0f;
  float DesiredForwardCmps = 0.0f;
  float AppliedForwardCmps = 0.0f;
  float PairSoftOppositionCmps = 0.0f;
  float TotalCorrectionForwardCmps = 0.0f;
};

struct FCrowdDemoSoftPressureRouteDiagnosticRuntime
{
  TArray<FCrowdDemoSoftPressureRouteAgentAccumulator> Agents;
  TArray<FCrowdDemoSoftPressureRouteCompactSample> Samples;
  TArray<float> InsideGoalCountSamples;
};

struct FCrowdDemoSoftPressureRouteDiagnosticCheckpoint
{
  TArray<FCrowdDemoSoftPressureRouteAgentAccumulator> Agents;
  int32 SampleCount = 0;
  int32 InsideGoalSampleCount = 0;
};

struct FCrowdDemoSoftPressureRouteAgentResult
{
  FCrowdDemoSoftPressureRouteAgentAccumulator Agent;
  int32 ConstraintComponentSize = 0;
  int32 ReachedNeighborCount = 0;
  int32 NonReachedNeighborCount = 0;
  float DesiredForwardCmpsP50 = 0.0f;
  float DesiredForwardCmpsP95 = 0.0f;
  float AppliedForwardCmpsP50 = 0.0f;
  float AppliedForwardCmpsP95 = 0.0f;
  float PairSoftOppositionCmpsP50 = 0.0f;
  float PairSoftOppositionCmpsP95 = 0.0f;
};

struct FCrowdDemoSoftPressureRouteCounterfactual
{
  bool bStickyValid = false;
  bool bSoftDisabledValid = false;
  float BaselineNeverReachedForwardCmps = 0.0f;
  float StickyNeverReachedForwardCmps = 0.0f;
  float SoftDisabledNeverReachedForwardCmps = 0.0f;
};

struct FCrowdDemoSoftPressureRouteDiagnosticSummary
{
  bool bValid = false;
  uint32 StableHash = 2166136261u;
  ECrowdDemoSoftPressureRouteBranch SelectedBranch =
    ECrowdDemoSoftPressureRouteBranch::Unclassified;
  TArray<FCrowdDemoSoftPressureRouteAgentResult> Agents;
  int32 TotalAgentCount = 0;
  int32 SelectedAgentCount = 0;
  int32 NeverReachedAgentCount = 0;
  int32 ReachedThenLeftAgentCount = 0;
  int32 GoalBoundaryTransitionCount = 0;
  int32 ZeroToMaxSpeedTransitionCount = 0;
  int32 MaxToZeroSpeedTransitionCount = 0;
  int32 CorridorEverStalledAgentCount = 0;
  int32 CorridorFinalDeadlockAgentCount = 0;
  int32 FlowContractViolationCount = 0;
  int32 FailureOwnedFlowContractViolationCount = 0;
  int32 CorridorFailureAgentCount = 0;
  int32 GoalFailureAgentCount = 0;
  float InsideGoalCountP50 = 0.0f;
  float InsideGoalCountP95 = 0.0f;
  float InsideGoalCountMax = 0.0f;
  float NeverReachedDistanceCmP50 = 0.0f;
  float NeverReachedDistanceCmP95 = 0.0f;
  float NeverReachedDistanceCmMax = 0.0f;
  float NeverReachedDesiredForwardCmpsP50 = 0.0f;
  float NeverReachedDesiredForwardCmpsP95 = 0.0f;
  float NeverReachedAppliedForwardCmpsP50 = 0.0f;
  float NeverReachedAppliedForwardCmpsP95 = 0.0f;
  float NeverReachedSoftOppositionCmpsP50 = 0.0f;
  float NeverReachedSoftOppositionCmpsP95 = 0.0f;
  FCrowdDemoSoftPressureRouteCounterfactual Counterfactual;
};

class MASSAICROWDDEMO_API FCrowdDemoSoftPressureRouteDiagnosticKernel
{
public:
  static void RecordStep(
    TConstArrayView<FCrowdDemoSoftPressureRouteStepSample> Samples,
    FCrowdDemoSoftPressureRouteDiagnosticRuntime& InOutRuntime);

  static FCrowdDemoSoftPressureRouteDiagnosticCheckpoint MakeCheckpoint(
    const FCrowdDemoSoftPressureRouteDiagnosticRuntime& Runtime);

  static void RestoreCheckpoint(
    const FCrowdDemoSoftPressureRouteDiagnosticCheckpoint& Checkpoint,
    FCrowdDemoSoftPressureRouteDiagnosticRuntime& InOutRuntime);

  static void BuildSummary(
    const FCrowdDemoSoftPressureRouteDiagnosticRuntime& Runtime,
    const FCrowdDemoSoftPressureRouteCounterfactual& Counterfactual,
    FCrowdDemoSoftPressureRouteDiagnosticSummary& OutSummary);
};
