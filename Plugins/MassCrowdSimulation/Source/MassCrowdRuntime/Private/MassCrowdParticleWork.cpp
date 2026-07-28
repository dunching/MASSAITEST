#include "MassCrowdParticleWork.h"

#define Fold ParticleWork_Fold
#define IsFiniteVector ParticleWork_IsFiniteVector

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }
}

FCrowdMassParticleWorkOutput FCrowdMassParticleWork::Solve(
  const FCrowdMassParticleWorkInput& Input)
{
  FCrowdMassParticleWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Agents.IsEmpty()
    || !FMath::IsFinite(Input.Settings.FixedStepSeconds)
    || Input.Settings.FixedStepSeconds <= 0.0f
    || Input.Settings.IterationCount <= 0
    || Input.Settings.SafetyIterationCount <= 0)
    return Output;

  TArray<FCrowdParticleConstraintAgent> Agents = Input.Agents;
  Agents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    const FCrowdParticleConstraintAgent& Agent = Agents[Index];
    if (Agent.AgentId == INDEX_NONE
      || (Index > 0 && Agents[Index - 1].AgentId == Agent.AgentId)
      || !IsFiniteVector(Agent.StartPosition)
      || !IsFiniteVector(Agent.PredictedPosition)
      || !FMath::IsFinite(Agent.PhysicalRadiusCm)
      || !FMath::IsFinite(Agent.HardSafetyGapCm)
      || !FMath::IsFinite(Agent.EnvironmentHardClearanceCm)
      || !FMath::IsFinite(Agent.SoftMarginCm)
      || !FMath::IsFinite(Agent.Mobility)
      || Agent.PhysicalRadiusCm <= 0.0f
      || Agent.HardSafetyGapCm < 0.0f
      || Agent.EnvironmentHardClearanceCm < 0.0f
      || Agent.SoftMarginCm < 0.0f
      || Agent.Mobility < 0.0f)
      return Output;
  }

  FCrowdParticleConstraintKernel::Solve(
    Agents, Input.Environment, Input.Settings, Output.Pairs, Output.Results,
    Output.Summary, Input.bCaptureTrace ? &Output.Trace : nullptr);

  if (Output.Results.Num() != Agents.Num()) return Output;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
    if (Output.Results[Index].AgentId != Agents[Index].AgentId)
      return Output;

  TArray<FCrowdParticleAppliedState> AppliedStates;
  AppliedStates.Reserve(Agents.Num());
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    const FCrowdParticleConstraintAgent& Agent = Agents[Index];
    const FCrowdParticleConstraintResult& Result = Output.Results[Index];
    FCrowdParticleAppliedState& Applied = AppliedStates.AddDefaulted_GetRef();
    Applied.AgentId = Agent.AgentId;
    Applied.Position = Output.Summary.bValid
      ? Result.CorrectedPosition : Agent.StartPosition;
    Applied.Velocity = Output.Summary.bValid
      ? Result.CorrectedVelocity : FVector::ZeroVector;
  }
  FCrowdParticleConstraintKernel::EvaluateAppliedState(
    Agents, AppliedStates, Input.Environment, Output.AppliedSummary,
    Output.AppliedStateHash);

  Output.StableHash = Fold(
    Fold(Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex)),
      static_cast<uint32>(Input.PlanRevision)),
    Fold(Output.Summary.CandidateHash, Output.AppliedStateHash));
  Output.bCompleted = true;
  return Output;
}

#undef IsFiniteVector
#undef Fold
