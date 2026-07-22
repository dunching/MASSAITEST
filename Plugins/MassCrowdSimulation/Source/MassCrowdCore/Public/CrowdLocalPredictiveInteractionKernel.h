#pragma once

#include "CoreMinimal.h"
#include "CrowdSharedFlowFieldKernel.h"
#include "CrowdVelocityHalfPlaneKernel.h"

struct FCrowdLocalPredictiveSettings
{
  float FixedStepSeconds = 1.0f / 30.0f;
  float TimeHorizonSeconds = 1.25f;
  float SpatialCellSizeCm = 600.0f;
  float VelocityQuantumCmps = 1.0f;
  float ConstraintEpsilonCmps = 0.1f;
  float RequestedProgressThresholdCmps = 30.0f;
  float BlockedProgressThresholdCmps = 10.0f;
  float GrantedResponsibility = 0.25f;
  int32 GrantDurationSteps = 30;
  int32 JointIterationCount = 64;
};

struct FCrowdLocalPredictiveAgent
{
  int32 AgentId = INDEX_NONE;
  FVector2f Position = FVector2f::ZeroVector;
  FVector2f Velocity = FVector2f::ZeroVector;
  FVector2f PreferredVelocity = FVector2f::ZeroVector;
  float PhysicalRadiusCm = 42.0f;
  float HardSafetyGapCm = 10.0f;
  float MaxSpeedCmps = 800.0f;
  int32 BlockedAgeSteps = 0;
};

struct FCrowdLocalPredictivePair
{
  int32 MinAgentId = INDEX_NONE;
  int32 MaxAgentId = INDEX_NONE;
  int32 MinAgentIndex = INDEX_NONE;
  int32 MaxAgentIndex = INDEX_NONE;
  int32 DistanceBucket = 0;
  float ClosestTimeSeconds = 0.0f;
  float PredictedSeparationCm = 0.0f;
  float RequiredSeparationCm = 0.0f;
  float MinAgentResponsibility = 0.5f;
  float MaxAgentResponsibility = 0.5f;
};

struct FCrowdLocalPredictiveGrantState
{
  uint32 ComponentKey = 0;
  int32 GrantedAgentId = INDEX_NONE;
  int32 GrantEpoch = 0;
  int32 RemainingSteps = 0;
};

struct FCrowdLocalPredictiveResult
{
  int32 AgentId = INDEX_NONE;
  FVector2f Velocity = FVector2f::ZeroVector;
  int32 NeighborCount = 0;
  int32 ConstraintCount = 0;
  int32 NextBlockedAgeSteps = 0;
  uint32 ComponentKey = 0;
  int32 GrantEpoch = 0;
  bool bAdjusted = false;
  bool bGranted = false;
  bool bYielding = false;
  bool bValid = false;
};

struct FCrowdLocalPredictiveSummary
{
  bool bValid = false;
  uint32 CandidateHash = 2166136261u;
  int32 ProcessedAgentCount = 0;
  int32 CandidatePairCount = 0;
  int32 ConflictPairCount = 0;
  int32 ComponentCount = 0;
  int32 MaxComponentSize = 0;
  int32 AdjustedAgentCount = 0;
  int32 GrantedAgentCount = 0;
  int32 YieldingAgentCount = 0;
  int32 InfeasibleAgentCount = 0;
  int32 QuantizationFailureCount = 0;
  int32 JointValidationFailureCount = 0;
  int32 JointComponentResolutionCount = 0;
  int32 CoherentTranslationComponentCount = 0;
  int32 CoherentTranslationAgentCount = 0;
  float CoherentTranslationMaxCmps = 0.0f;
  int32 JointPreferredRecoveryComponentCount = 0;
  int32 JointPreferredRecoveryAgentCount = 0;
  float JointPreferredRecoveryMaxGainCmps = 0.0f;
  int32 EnvironmentConstraintCount = 0;
  int32 GrantSwitchCount = 0;
  int32 BlockedAgeMax = 0;
};

struct FCrowdLocalPredictiveTraceVelocity
{
  int32 AgentId = INDEX_NONE;
  FVector2f Velocity = FVector2f::ZeroVector;
};

struct FCrowdLocalPredictiveComponentTrace
{
  uint32 ComponentKey = 2166136261u;
  TArray<int32> AgentIds;
  int32 GrantedAgentId = INDEX_NONE;
  FVector2f CommonVelocity = FVector2f::ZeroVector;
  int32 SafeAlphaQ15 = 0;
  bool bCommonVelocityValid = false;
  bool bFullJointVelocitySafe = false;
  bool bCoherentTranslationApplied = false;
  FVector2f CoherentTranslation = FVector2f::ZeroVector;
  bool bJointPreferredRecoveryApplied = false;
  TArray<FCrowdLocalPredictiveTraceVelocity> PreTranslationVelocities;
  TArray<FCrowdLocalPredictiveTraceVelocity> PreRecoveryVelocities;
  TArray<FCrowdLocalPredictiveTraceVelocity> RecoveredVelocities;
  TArray<FCrowdLocalPredictiveTraceVelocity> JointProjectedVelocities;
  TArray<FCrowdLocalPredictiveTraceVelocity> FinalVelocities;
};

struct FCrowdLocalPredictiveDiagnosticTrace
{
  TArray<FCrowdLocalPredictiveResult> InitialIndependentResults;
  TArray<FCrowdLocalPredictiveResult> CompletedIndependentResults;
  TArray<FCrowdLocalPredictiveComponentTrace> Components;
};

struct FCrowdLocalPredictiveComponentFixture
{
  bool bValid = false;
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdLocalPredictiveSettings Settings;
  FCrowdSharedFlowFieldConfig FlowConfig;
  TArray<FCrowdLocalPredictiveAgent> Agents;
  TArray<FCrowdLocalPredictiveGrantState> PreviousGrantStates;
  TArray<FCrowdLocalPredictivePair> ConflictPairs;
  TArray<FCrowdLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdLocalPredictiveResult> Results;
  FCrowdLocalPredictiveSummary Summary;
  FCrowdLocalPredictiveDiagnosticTrace Trace;
  TArray<int32> WitnessAgentIds;
  uint32 StableHash = 2166136261u;
};

struct FCrowdLocalPredictiveDiagnosticFrame
{
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdLocalPredictiveSettings Settings;
  TArray<FCrowdLocalPredictiveAgent> Agents;
  TArray<FCrowdLocalPredictiveGrantState> PreviousGrantStates;
  TArray<FCrowdLocalPredictivePair> ConflictPairs;
  TArray<FCrowdLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdLocalPredictiveResult> Results;
  FCrowdLocalPredictiveSummary Summary;
  FCrowdLocalPredictiveDiagnosticTrace Trace;
};

class MASSCROWDCORE_API FCrowdLocalPredictiveInteractionKernel
{
public:
  static void BuildCandidatePairs(
    TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
    const FCrowdLocalPredictiveSettings& Settings,
    TArray<FCrowdLocalPredictivePair>& OutPairs);

  static bool ValidateJointResult(
    TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
    const FCrowdSharedFlowFieldConfig& FlowConfig,
    const FCrowdLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdLocalPredictivePair> ConflictPairs,
    TConstArrayView<FCrowdLocalPredictiveResult> Results);

  static void Solve(
    TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
    const FCrowdSharedFlowFieldConfig& FlowConfig,
    const FCrowdLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdLocalPredictiveGrantState> PreviousGrantStates,
    TArray<FCrowdLocalPredictivePair>& OutConflictPairs,
    TArray<FCrowdLocalPredictiveGrantState>& OutGrantStates,
    TArray<FCrowdLocalPredictiveResult>& OutResults,
    FCrowdLocalPredictiveSummary& OutSummary,
    FCrowdLocalPredictiveDiagnosticTrace* OutTrace = nullptr);

  static bool BuildComponentFixture(
    int32 FixedStepIndex,
    TConstArrayView<FCrowdLocalPredictiveAgent> Agents,
    const FCrowdSharedFlowFieldConfig& FlowConfig,
    const FCrowdLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdLocalPredictiveGrantState> PreviousGrantStates,
    TConstArrayView<FCrowdLocalPredictivePair> ConflictPairs,
    TConstArrayView<FCrowdLocalPredictiveGrantState> GrantStates,
    TConstArrayView<FCrowdLocalPredictiveResult> Results,
    const FCrowdLocalPredictiveSummary& Summary,
    const FCrowdLocalPredictiveDiagnosticTrace& Trace,
    TConstArrayView<int32> WitnessAgentIds,
    FCrowdLocalPredictiveComponentFixture& OutFixture);
};
