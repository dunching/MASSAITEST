#include "MassCrowdLocalPredictiveWork.h"

#define Fold LocalPredictiveWork_Fold

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }
}

FCrowdMassLocalPredictiveWorkOutput FCrowdMassLocalPredictiveWork::Solve(
  const FCrowdMassLocalPredictiveWorkInput& Input)
{
  FCrowdMassLocalPredictiveWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Agents.IsEmpty())
    return Output;

  TArray<FCrowdLocalPredictiveAgent> Agents = Input.Agents;
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
    if (Agents[Index].AgentId == INDEX_NONE
      || (Index > 0 && Agents[Index - 1].AgentId == Agents[Index].AgentId))
      return Output;

  FCrowdLocalPredictiveInteractionKernel::Solve(
    Agents, Input.Environment, Input.Settings, Input.PreviousGrantStates,
    Output.ConflictPairs, Output.GrantStates, Output.Results,
    Output.Summary,
    Input.bCaptureDiagnostic ? &Output.DiagnosticTrace : nullptr);

  if (!Output.Summary.bValid && !Input.bCaptureDiagnostic)
  {
    Output.bFailureTraceReplayAttempted = true;
    TArray<FCrowdLocalPredictivePair> ReplayConflictPairs;
    TArray<FCrowdLocalPredictiveGrantState> ReplayGrantStates;
    TArray<FCrowdLocalPredictiveResult> ReplayResults;
    FCrowdLocalPredictiveSummary ReplaySummary;
    FCrowdLocalPredictiveInteractionKernel::Solve(
      Agents, Input.Environment, Input.Settings, Input.PreviousGrantStates,
      ReplayConflictPairs, ReplayGrantStates, ReplayResults, ReplaySummary,
      &Output.DiagnosticTrace);
    Output.FailureTraceReplayCandidateHash = ReplaySummary.CandidateHash;
    Output.bFailureTraceReplayMatched =
      ReplaySummary.CandidateHash == Output.Summary.CandidateHash;
    Output.bFailureTraceReplayValid = Output.bFailureTraceReplayMatched
      && !ReplaySummary.bValid
      && ReplayResults.Num() == Output.Results.Num();
  }

  if (Output.Results.Num() != Agents.Num()) return Output;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
    if (Output.Results[Index].AgentId != Agents[Index].AgentId)
      return Output;

  Output.StableHash = Fold(
    Fold(Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex)),
      static_cast<uint32>(Input.PlanRevision)),
    Output.Summary.CandidateHash);
  Output.bCompleted = true;
  return Output;
}

#undef Fold
