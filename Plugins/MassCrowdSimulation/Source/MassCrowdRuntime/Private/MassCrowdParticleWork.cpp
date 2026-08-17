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

  int64 MakeParticleCellKey(
    const FVector& Position,
    const float CellSizeCm)
  {
    const int32 X = FMath::FloorToInt(Position.X / CellSizeCm);
    const int32 Y = FMath::FloorToInt(Position.Y / CellSizeCm);
    return static_cast<int64>(
      (static_cast<uint64>(static_cast<uint32>(Y)) << 32)
      | static_cast<uint32>(X));
  }

  void AccumulateSummary(
    FCrowdParticleConstraintSummary& Target,
    const FCrowdParticleConstraintSummary& Source)
  {
    Target.bValid = Target.bValid && Source.bValid;
    Target.CandidatePairCount += Source.CandidatePairCount;
    Target.SoftPairCount += Source.SoftPairCount;
    Target.SoftViolatingPairCount += Source.SoftViolatingPairCount;
    Target.HardPairViolationCount += Source.HardPairViolationCount;
    Target.SweptPairViolationCount += Source.SweptPairViolationCount;
    Target.ObstaclePenetrationCount += Source.ObstaclePenetrationCount;
    Target.BoundsViolationCount += Source.BoundsViolationCount;
    Target.EnvironmentSoftContactCount +=
      Source.EnvironmentSoftContactCount;
    Target.EnvironmentSoftAppliedAgentCount +=
      Source.EnvironmentSoftAppliedAgentCount;
    Target.UnifiedHardConstraintCount +=
      Source.UnifiedHardConstraintCount;
    Target.UnifiedHardInfeasibleCount +=
      Source.UnifiedHardInfeasibleCount;
    Target.PressureInfluencedAgentCount +=
      Source.PressureInfluencedAgentCount;
    Target.FirstInfluencedIterationMax = FMath::Max(
      Target.FirstInfluencedIterationMax,
      Source.FirstInfluencedIterationMax);
    Target.CorrectedAgentCount += Source.CorrectedAgentCount;
    Target.SoftErrorCmP50 = FMath::Max(
      Target.SoftErrorCmP50, Source.SoftErrorCmP50);
    Target.SoftErrorCmP95 = FMath::Max(
      Target.SoftErrorCmP95, Source.SoftErrorCmP95);
    Target.SoftErrorCmMax = FMath::Max(
      Target.SoftErrorCmMax, Source.SoftErrorCmMax);
    Target.EnvironmentSoftErrorCmP50 = FMath::Max(
      Target.EnvironmentSoftErrorCmP50,
      Source.EnvironmentSoftErrorCmP50);
    Target.EnvironmentSoftErrorCmP95 = FMath::Max(
      Target.EnvironmentSoftErrorCmP95,
      Source.EnvironmentSoftErrorCmP95);
    Target.EnvironmentSoftErrorCmMax = FMath::Max(
      Target.EnvironmentSoftErrorCmMax,
      Source.EnvironmentSoftErrorCmMax);
    Target.EnvironmentSoftRequestedCorrectionCmMax = FMath::Max(
      Target.EnvironmentSoftRequestedCorrectionCmMax,
      Source.EnvironmentSoftRequestedCorrectionCmMax);
    Target.EnvironmentSoftRealizedCorrectionCmMax = FMath::Max(
      Target.EnvironmentSoftRealizedCorrectionCmMax,
      Source.EnvironmentSoftRealizedCorrectionCmMax);
    Target.UnifiedHardResidualCmMax = FMath::Max(
      Target.UnifiedHardResidualCmMax,
      Source.UnifiedHardResidualCmMax);
    Target.MaxAgentCorrectionCm = FMath::Max(
      Target.MaxAgentCorrectionCm, Source.MaxAgentCorrectionCm);
    Target.CandidateHash = Fold(
      Fold(Target.CandidateHash, Source.CandidateHash),
      static_cast<uint32>(Source.CandidatePairCount));
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

  const auto SolveMonolithic = [&]()
  {
    FCrowdParticleConstraintKernel::Solve(
      Agents, Input.Environment, Input.Settings,
      Output.Pairs, Output.Results, Output.Summary,
      Input.bCaptureTrace ? &Output.Trace : nullptr);
  };

  float CellSizeCm = 1.0f;
  for (const FCrowdParticleConstraintAgent& Agent : Agents)
  {
    CellSizeCm = FMath::Max(CellSizeCm,
      2.0f * Agent.PhysicalRadiusCm
        + Agent.HardSafetyGapCm
        + 2.0f * Agent.SoftMarginCm);
  }
  TSet<int64> OccupiedCells;
  for (const FCrowdParticleConstraintAgent& Agent : Agents)
    OccupiedCells.Add(MakeParticleCellKey(
      Agent.PredictedPosition, CellSizeCm));
  Output.CellShardCount = OccupiedCells.Num();

  // Inflate the closure graph by the largest correction budget that can be
  // introduced during this fixed step. Components separated after this
  // conservative expansion cannot exchange a pair constraint in the solve.
  TArray<FCrowdParticleConstraintAgent> ClosureAgents = Agents;
  const float ClosureCorrectionReach =
    static_cast<float>(Input.Settings.IterationCount)
      * (FMath::Max(0.0f,
          Input.Settings.SoftMaxPairCorrectionPerIterationCm)
        + FMath::Max(0.0f,
          Input.Settings.SoftMaxEnvironmentCorrectionPerIterationCm))
    + static_cast<float>(Input.Settings.IterationCount
        + Input.Settings.SafetyIterationCount)
      * FMath::Max(0.0f,
          Input.Settings.HardMaxPairCorrectionPerIterationCm);
  for (FCrowdParticleConstraintAgent& Agent : ClosureAgents)
    Agent.SoftMarginCm += ClosureCorrectionReach;
  TArray<FVector> PredictedPositions;
  PredictedPositions.Reserve(Agents.Num());
  for (const FCrowdParticleConstraintAgent& Agent : Agents)
    PredictedPositions.Add(Agent.PredictedPosition);
  TArray<FCrowdParticleConstraintPair> ClosurePairs;
  FCrowdParticleConstraintKernel::BuildCandidatePairs(
    ClosureAgents, PredictedPositions, ClosurePairs);
  TArray<int32> Parents;
  Parents.SetNumUninitialized(Agents.Num());
  for (int32 Index = 0; Index < Parents.Num(); ++Index)
    Parents[Index] = Index;
  const auto FindRoot = [&Parents](int32 Index)
  {
    while (Parents[Index] != Index)
    {
      Parents[Index] = Parents[Parents[Index]];
      Index = Parents[Index];
    }
    return Index;
  };
  const auto Union = [&Parents, &FindRoot, &Agents](
    const int32 A, const int32 B)
  {
    const int32 RootA = FindRoot(A);
    const int32 RootB = FindRoot(B);
    if (RootA == RootB) return;
    if (Agents[RootA].AgentId < Agents[RootB].AgentId)
      Parents[RootB] = RootA;
    else
      Parents[RootA] = RootB;
  };
  for (const FCrowdParticleConstraintPair& Pair : ClosurePairs)
  {
    Union(Pair.MinAgentIndex, Pair.MaxAgentIndex);
    const int64 MinCell = MakeParticleCellKey(
      Agents[Pair.MinAgentIndex].PredictedPosition, CellSizeCm);
    const int64 MaxCell = MakeParticleCellKey(
      Agents[Pair.MaxAgentIndex].PredictedPosition, CellSizeCm);
    Output.CrossCellPairCount += MinCell != MaxCell ? 1 : 0;
  }
  TMap<int32, TArray<int32>> ComponentByRoot;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
    ComponentByRoot.FindOrAdd(FindRoot(Index)).Add(Index);
  TArray<TArray<int32>> Components;
  ComponentByRoot.GenerateValueArray(Components);
  Components.Sort([&Agents](const auto& A, const auto& B)
  {
    return Agents[A[0]].AgentId < Agents[B[0]].AgentId;
  });
  Output.InteractionIslandCount = Components.Num();
  for (const TArray<int32>& Component : Components)
    Output.MaxIslandAgentCount = FMath::Max(
      Output.MaxIslandAgentCount, Component.Num());

  if (Input.bCaptureTrace || Components.Num() <= 1)
  {
    SolveMonolithic();
  }
  else
  {
    Output.bUsedIslandSharding = true;
    Output.Summary = {};
    Output.Summary.bValid = true;
    Output.Summary.CandidateHash = 2166136261u;
    TMap<int32, int32> GlobalIndexByAgentId;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
      GlobalIndexByAgentId.Add(Agents[Index].AgentId, Index);
    for (const TArray<int32>& Component : Components)
    {
      TArray<FCrowdParticleConstraintAgent> IslandAgents;
      IslandAgents.Reserve(Component.Num());
      for (const int32 Index : Component)
        IslandAgents.Add(Agents[Index]);
      TArray<FCrowdParticleConstraintPair> IslandPairs;
      TArray<FCrowdParticleConstraintResult> IslandResults;
      FCrowdParticleConstraintSummary IslandSummary;
      FCrowdParticleConstraintKernel::Solve(
        IslandAgents, Input.Environment, Input.Settings,
        IslandPairs, IslandResults, IslandSummary, nullptr);
      AccumulateSummary(Output.Summary, IslandSummary);
      for (FCrowdParticleConstraintPair& Pair : IslandPairs)
      {
        const int32* MinIndex =
          GlobalIndexByAgentId.Find(Pair.MinAgentId);
        const int32* MaxIndex =
          GlobalIndexByAgentId.Find(Pair.MaxAgentId);
        if (!MinIndex || !MaxIndex) return Output;
        Pair.MinAgentIndex = *MinIndex;
        Pair.MaxAgentIndex = *MaxIndex;
        Output.Pairs.Add(Pair);
      }
      Output.Results.Append(MoveTemp(IslandResults));
    }
    Output.Pairs.Sort([](const auto& A, const auto& B)
    {
      return A.MinAgentId < B.MinAgentId
        || (A.MinAgentId == B.MinAgentId
          && A.MaxAgentId < B.MaxAgentId);
    });
    Output.Results.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
  }

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
  if (Output.bUsedIslandSharding && !Output.AppliedSummary.bValid)
  {
    Output.bUsedMonolithicFallback = true;
    Output.Pairs.Reset();
    Output.Results.Reset();
    Output.Summary = {};
    SolveMonolithic();
    AppliedStates.Reset();
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
      FCrowdParticleAppliedState& Applied =
        AppliedStates.AddDefaulted_GetRef();
      Applied.AgentId = Agents[Index].AgentId;
      Applied.Position = Output.Summary.bValid
        ? Output.Results[Index].CorrectedPosition
        : Agents[Index].StartPosition;
      Applied.Velocity = Output.Summary.bValid
        ? Output.Results[Index].CorrectedVelocity
        : FVector::ZeroVector;
    }
    FCrowdParticleConstraintKernel::EvaluateAppliedState(
      Agents, AppliedStates, Input.Environment,
      Output.AppliedSummary, Output.AppliedStateHash);
  }

  // Failure diagnostics must replay the exact immutable work input. Rebuilding
  // it later on the game thread can observe a different predicted-state epoch.
  if ((!Output.Summary.bValid || !Output.AppliedSummary.bValid)
    && !Input.bCaptureTrace)
  {
    FCrowdParticleConstraintSettings DiagnosticSettings = Input.Settings;
    DiagnosticSettings.bCaptureRouteDiagnostic = true;
    TArray<FCrowdParticleConstraintPair> DiagnosticPairs;
    TArray<FCrowdParticleConstraintResult> DiagnosticResults;
    FCrowdParticleConstraintSummary DiagnosticSummary;
    FCrowdParticleConstraintTrace DiagnosticTrace;
    FCrowdParticleConstraintKernel::Solve(
      Agents, Input.Environment, DiagnosticSettings, DiagnosticPairs,
      DiagnosticResults, DiagnosticSummary, &DiagnosticTrace);
    Output.bFailureTraceReplayAttempted = true;
    Output.FailureTraceReplayCandidateHash = DiagnosticSummary.CandidateHash;
    Output.bFailureTraceReplayValid = DiagnosticSummary.bValid;
    Output.bFailureTraceReplayMatched =
      DiagnosticSummary.CandidateHash == Output.Summary.CandidateHash
      && DiagnosticSummary.bValid == Output.Summary.bValid;
    Output.Trace = MoveTemp(DiagnosticTrace);
  }

  Output.StableHash = Fold(
    Fold(Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex)),
      static_cast<uint32>(Input.PlanRevision)),
    Fold(Output.Summary.CandidateHash, Output.AppliedStateHash));
  Output.bCompleted = true;
  return Output;
}

#undef IsFiniteVector
#undef Fold
