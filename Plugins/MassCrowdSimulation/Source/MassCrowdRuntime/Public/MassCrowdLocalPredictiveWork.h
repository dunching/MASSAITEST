#pragma once

#include "CoreMinimal.h"
#include "CrowdLocalPredictiveInteractionKernel.h"

struct FCrowdMassLocalPredictiveWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdSharedFlowFieldConfig Environment;
  FCrowdLocalPredictiveSettings Settings;
  TArray<FCrowdLocalPredictiveAgent> Agents;
  TArray<FCrowdLocalPredictiveGrantState> PreviousGrantStates;
  bool bCaptureDiagnostic = false;
};

struct FCrowdMassLocalPredictiveWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdLocalPredictivePair> ConflictPairs;
  TArray<FCrowdLocalPredictiveGrantState> GrantStates;
  TArray<FCrowdLocalPredictiveResult> Results;
  FCrowdLocalPredictiveSummary Summary;
  FCrowdLocalPredictiveDiagnosticTrace DiagnosticTrace;
  uint32 StableHash = 2166136261u;
  uint32 FailureTraceReplayCandidateHash = 2166136261u;
  bool bFailureTraceReplayAttempted = false;
  bool bFailureTraceReplayMatched = false;
  bool bFailureTraceReplayValid = false;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassLocalPredictiveWork
{
public:
  static FCrowdMassLocalPredictiveWorkOutput Solve(
    const FCrowdMassLocalPredictiveWorkInput& Input);
};
