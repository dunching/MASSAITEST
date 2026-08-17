#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "Mass/CrowdDemoVelocityHalfPlaneKernel.h"

struct FCrowdDemoLocalPredictiveSettings
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

struct FCrowdDemoLocalPredictiveAgent
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

struct FCrowdDemoLocalPredictivePair
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

struct FCrowdDemoLocalPredictiveGrantState
{
  uint32 ComponentKey = 0;
  int32 GrantedAgentId = INDEX_NONE;
  int32 GrantEpoch = 0;
  int32 RemainingSteps = 0;
};

struct FCrowdDemoLocalPredictiveResult
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

struct FCrowdDemoLocalPredictiveSummary
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

struct FCrowdDemoLocalPredictiveTraceVelocity
{
  int32 AgentId = INDEX_NONE;
  FVector2f Velocity = FVector2f::ZeroVector;
};

struct FCrowdDemoLocalPredictiveComponentTrace
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
  TArray<FCrowdDemoLocalPredictiveTraceVelocity> PreTranslationVelocities;
  TArray<FCrowdDemoLocalPredictiveTraceVelocity> PreRecoveryVelocities;
  TArray<FCrowdDemoLocalPredictiveTraceVelocity> RecoveredVelocities;
  TArray<FCrowdDemoLocalPredictiveTraceVelocity> JointProjectedVelocities;
  TArray<FCrowdDemoLocalPredictiveTraceVelocity> FinalVelocities;
};

struct FCrowdDemoLocalPredictiveDiagnosticTrace
{
  TArray<FCrowdDemoLocalPredictiveResult> InitialIndependentResults;
  TArray<FCrowdDemoLocalPredictiveResult> CompletedIndependentResults;
  TArray<FCrowdDemoLocalPredictiveComponentTrace> Components;
};

struct FCrowdDemoLocalPredictiveComponentFixture
{
  bool bValid = false;
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdDemoLocalPredictiveSettings Settings;
  FCrowdDemoSharedFlowFieldConfig FlowConfig;
  TArray<FCrowdDemoLocalPredictiveAgent> Agents;
  TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates;
  TArray<FCrowdDemoLocalPredictivePair> ConflictPairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdDemoLocalPredictiveResult> Results;
  FCrowdDemoLocalPredictiveSummary Summary;
  FCrowdDemoLocalPredictiveDiagnosticTrace Trace;
  TArray<int32> WitnessAgentIds;
  uint32 StableHash = 2166136261u;
};

struct FCrowdDemoLocalPredictiveDiagnosticFrame
{
  int32 FixedStepIndex = INDEX_NONE;
  FCrowdDemoLocalPredictiveSettings Settings;
  TArray<FCrowdDemoLocalPredictiveAgent> Agents;
  TArray<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates;
  TArray<FCrowdDemoLocalPredictivePair> ConflictPairs;
  TArray<FCrowdDemoLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdDemoLocalPredictiveResult> Results;
  FCrowdDemoLocalPredictiveSummary Summary;
  FCrowdDemoLocalPredictiveDiagnosticTrace Trace;
};

class MASSAICROWDDEMO_API FCrowdDemoLocalPredictiveInteractionKernel
{
public:
  static void BuildCandidatePairs(
    TConstArrayView<FCrowdDemoLocalPredictiveAgent> Agents,
    const FCrowdDemoLocalPredictiveSettings& Settings,
    TArray<FCrowdDemoLocalPredictivePair>& OutPairs);

  static bool ValidateJointResult(
    TConstArrayView<FCrowdDemoLocalPredictiveAgent> Agents,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdDemoLocalPredictivePair> ConflictPairs,
    TConstArrayView<FCrowdDemoLocalPredictiveResult> Results);

  static void Solve(
    TConstArrayView<FCrowdDemoLocalPredictiveAgent> Agents,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates,
    TArray<FCrowdDemoLocalPredictivePair>& OutConflictPairs,
    TArray<FCrowdDemoLocalPredictiveGrantState>& OutGrantStates,
    TArray<FCrowdDemoLocalPredictiveResult>& OutResults,
    FCrowdDemoLocalPredictiveSummary& OutSummary,
    FCrowdDemoLocalPredictiveDiagnosticTrace* OutTrace = nullptr);

  static bool BuildComponentFixture(
    int32 FixedStepIndex,
    TConstArrayView<FCrowdDemoLocalPredictiveAgent> Agents,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoLocalPredictiveSettings& Settings,
    TConstArrayView<FCrowdDemoLocalPredictiveGrantState> PreviousGrantStates,
    TConstArrayView<FCrowdDemoLocalPredictivePair> ConflictPairs,
    TConstArrayView<FCrowdDemoLocalPredictiveGrantState> GrantStates,
    TConstArrayView<FCrowdDemoLocalPredictiveResult> Results,
    const FCrowdDemoLocalPredictiveSummary& Summary,
    const FCrowdDemoLocalPredictiveDiagnosticTrace& Trace,
    TConstArrayView<int32> WitnessAgentIds,
    FCrowdDemoLocalPredictiveComponentFixture& OutFixture);
};
