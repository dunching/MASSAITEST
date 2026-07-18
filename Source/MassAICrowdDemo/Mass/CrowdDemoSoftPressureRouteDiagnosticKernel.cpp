#include "Mass/CrowdDemoSoftPressureRouteDiagnosticKernel.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"

namespace
{
  constexpr float GoalRadiusCm = 140.0f;
  constexpr float LowSpeedThresholdCmps = 10.0f;
  constexpr float CorridorMinY = -2050.0f;
  constexpr float CorridorMaxY = -650.0f;

  uint32 Fold(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  float Percentile(TArray<float> Values, const float P)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    const int32 Index = FMath::Clamp(
      FMath::CeilToInt(P * static_cast<float>(Values.Num())) - 1, 0, Values.Num() - 1);
    return Values[Index];
  }

  float Maximum(const TConstArrayView<float> Values)
  {
    float Result = 0.0f;
    for (const float Value : Values) Result = FMath::Max(Result, Value);
    return Result;
  }

  float Forward(const FVector& Value, const FVector& Direction)
  {
    const FVector Unit = Direction.GetSafeNormal2D();
    return Unit.IsNearlyZero() ? 0.0f : FVector::DotProduct(Value, Unit);
  }

  int8 DesiredRegime(const FVector& Desired, const float MaxSpeed)
  {
    const float Speed = Desired.Size2D();
    if (Speed <= 1.0f) return 0;
    if (Speed >= FMath::Max(1.0f, MaxSpeed - 1.0f)) return 1;
    return -1;
  }

  bool IsInsideCorridor(const FVector& Location)
  {
    return Location.Y > CorridorMinY && Location.Y < CorridorMaxY;
  }

  FCrowdDemoSoftPressureRouteAgentAccumulator* FindAgent(
    TArray<FCrowdDemoSoftPressureRouteAgentAccumulator>& Agents, const int32 AgentId)
  {
    return Agents.FindByPredicate([&](const auto& Agent) { return Agent.AgentId == AgentId; });
  }

  const FCrowdDemoSoftPressureRouteAgentAccumulator* FindAgent(
    const TArray<FCrowdDemoSoftPressureRouteAgentAccumulator>& Agents, const int32 AgentId)
  {
    return Agents.FindByPredicate([&](const auto& Agent) { return Agent.AgentId == AgentId; });
  }
}

void FCrowdDemoSoftPressureRouteDiagnosticKernel::RecordStep(
  const TConstArrayView<FCrowdDemoSoftPressureRouteStepSample> InputSamples,
  FCrowdDemoSoftPressureRouteDiagnosticRuntime& InOutRuntime)
{
  TArray<FCrowdDemoSoftPressureRouteStepSample> Samples(InputSamples);
  Samples.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  int32 InsideGoalCount = 0;
  for (int32 Index = 0; Index < Samples.Num(); ++Index)
  {
    const auto& Sample = Samples[Index];
    if (Sample.AgentId == INDEX_NONE
      || (Index > 0 && Samples[Index - 1].AgentId == Sample.AgentId))
      continue;
    auto* Agent = FindAgent(InOutRuntime.Agents, Sample.AgentId);
    if (!Agent)
    {
      FCrowdDemoSoftPressureRouteAgentAccumulator& Added =
        InOutRuntime.Agents.AddDefaulted_GetRef();
      Added.AgentId = Sample.AgentId;
      Agent = &Added;
    }
    const float GoalDistance = FVector::Dist2D(Sample.Location, Sample.Goal);
    const bool bInsideGoal = GoalDistance <= GoalRadiusCm;
    InsideGoalCount += bInsideGoal ? 1 : 0;
    if (Agent->bHasPreviousInsideGoal && Agent->bPreviousInsideGoal != bInsideGoal)
    {
      ++Agent->GoalBoundaryTransitionCount;
      if (Agent->bPreviousInsideGoal && !bInsideGoal) ++Agent->ReachedThenLeftCount;
    }
    Agent->bHasPreviousInsideGoal = true;
    Agent->bPreviousInsideGoal = bInsideGoal;
    Agent->bCurrentInsideGoal = bInsideGoal;
    Agent->bEverReachedGoal |= bInsideGoal;

    const int8 Regime = DesiredRegime(Sample.DesiredVelocity, Sample.MaxSpeedCmps);
    if (Agent->PreviousDesiredRegime == 0 && Regime == 1)
      ++Agent->ZeroToMaxSpeedTransitionCount;
    else if (Agent->PreviousDesiredRegime == 1 && Regime == 0)
      ++Agent->MaxToZeroSpeedTransitionCount;
    if (Regime >= 0) Agent->PreviousDesiredRegime = Regime;

    if (!Agent->bWallReached && Sample.Location.Y > -1950.0f)
    {
      Agent->bWallReached = true;
      Agent->FirstWallStep = Sample.FixedStepIndex;
    }
    if (!Agent->bCorridorReached && Sample.Location.Y > -650.0f)
    {
      Agent->bCorridorReached = true;
      Agent->FirstCorridorStep = Sample.FixedStepIndex;
    }
    if (!Agent->bTurnReached && Sample.Location.Y > 750.0f)
    {
      Agent->bTurnReached = true;
      Agent->FirstTurnStep = Sample.FixedStepIndex;
    }

    if (IsInsideCorridor(Sample.Location) && Sample.AppliedVelocity.Size2D() < LowSpeedThresholdCmps)
      ++Agent->CurrentLowSpeedSteps;
    else
      Agent->CurrentLowSpeedSteps = 0;
    Agent->MaxLowSpeedSteps = FMath::Max(Agent->MaxLowSpeedSteps, Agent->CurrentLowSpeedSteps);
    Agent->bEverCorridorStalled |= Agent->CurrentLowSpeedSteps > 45;
    Agent->bFinalCorridorDeadlock = IsInsideCorridor(Sample.Location)
      && Agent->CurrentLowSpeedSteps > 45;

    const bool bInsideGoalAtPredict = FVector::DistSquared2D(
      Sample.PredictStartLocation, Sample.Goal) <= FMath::Square(GoalRadiusCm);
    if (Sample.bFlowGuidanceOwner)
      ++Agent->FlowGuidanceOwnedSampleCount;
    if (Sample.bFlowGuidanceOwner && !bInsideGoalAtPredict)
    {
      const FVector ExpectedDesired = Sample.FlowDirection * Sample.MaxSpeedCmps;
      int32 FlowViolationMask = 0;
      FlowViolationMask |= Sample.FlowStatus != ECrowdDemoFlowLocationStatus::Reachable ? 1 : 0;
      FlowViolationMask |= Sample.IntegrationCost == MAX_int32 ? 2 : 0;
      FlowViolationMask |= Sample.FlowDirection.IsNearlyZero() ? 4 : 0;
      FlowViolationMask |= !Sample.DesiredVelocity.Equals(ExpectedDesired, 1.0f) ? 8 : 0;
      FlowViolationMask |= !Sample.PredictedVelocity.Equals(Sample.DesiredVelocity, 1.0f) ? 16 : 0;
      const bool bFlowInvalid = FlowViolationMask != 0;
      Agent->FlowContractViolationCount += bFlowInvalid ? 1 : 0;
      if (bFlowInvalid && Agent->FirstFlowContractViolationStep == INDEX_NONE)
      {
        Agent->FirstFlowContractViolationStep = Sample.FixedStepIndex;
        Agent->FirstFlowContractViolationMask = FlowViolationMask;
        Agent->FirstFlowContractViolationPredictDistanceCm = FVector::Dist2D(
          Sample.PredictStartLocation, Sample.Goal);
      }
    }

    Agent->SampleCount++;
    Agent->FinalLocation = Sample.Location;
    Agent->FinalGoalDistanceCm = GoalDistance;
    Agent->MinimumGoalDistanceCm = FMath::Min(Agent->MinimumGoalDistanceCm, GoalDistance);
    Agent->FinalFlowDirection = Sample.FlowDirection;
    Agent->FinalDesiredVelocity = Sample.DesiredVelocity;
    Agent->FinalPredictedVelocity = Sample.PredictedVelocity;
    Agent->FinalAppliedVelocity = Sample.AppliedVelocity;
    Agent->FinalPairSoftRequestedCorrection = Sample.PairSoftRequestedCorrection;
    Agent->FinalPairSoftRealizedCorrection = Sample.PairSoftRealizedCorrection;
    Agent->FinalEnvironmentSoftRequestedCorrection = Sample.EnvironmentSoftRequestedCorrection;
    Agent->FinalEnvironmentSoftRealizedCorrection = Sample.EnvironmentSoftRealizedCorrection;
    Agent->FinalUnifiedHardCorrection = Sample.UnifiedHardCorrection;
    Agent->FinalTotalParticleCorrection = Sample.TotalParticleCorrection;
    Agent->FinalFlowCellIndex = Sample.FlowCellIndex;
    Agent->FinalFlowStableCellKey = Sample.FlowStableCellKey;
    Agent->FinalFlowStatus = Sample.FlowStatus;
    Agent->FinalIntegrationCost = Sample.IntegrationCost;
    Agent->FinalDesiredForwardCmps = Forward(Sample.DesiredVelocity, Sample.FlowDirection);
    Agent->FinalAppliedForwardCmps = Forward(Sample.AppliedVelocity, Sample.FlowDirection);
    Agent->FinalActiveNeighborAgentIds = Sample.ActiveNeighborAgentIds;
    Agent->FinalActiveNeighborAgentIds.Sort();
    Agent->FinalActiveNeighborAgentIds.SetNum(Algo::Unique(Agent->FinalActiveNeighborAgentIds));

    FCrowdDemoSoftPressureRouteCompactSample& Compact =
      InOutRuntime.Samples.AddDefaulted_GetRef();
    Compact.AgentId = Sample.AgentId;
    Compact.GoalDistanceCm = GoalDistance;
    Compact.DesiredForwardCmps = Agent->FinalDesiredForwardCmps;
    Compact.AppliedForwardCmps = Agent->FinalAppliedForwardCmps;
    const float Dt = FMath::Max(0.001f, Sample.FixedStepSeconds);
    Compact.PairSoftOppositionCmps = FMath::Max(0.0f,
      -Forward(Sample.PairSoftRealizedCorrection, Sample.FlowDirection) / Dt);
    Compact.TotalCorrectionForwardCmps =
      Forward(Sample.TotalParticleCorrection, Sample.FlowDirection) / Dt;
  }
  InOutRuntime.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  InOutRuntime.InsideGoalCountSamples.Add(static_cast<float>(InsideGoalCount));
}

FCrowdDemoSoftPressureRouteDiagnosticCheckpoint
FCrowdDemoSoftPressureRouteDiagnosticKernel::MakeCheckpoint(
  const FCrowdDemoSoftPressureRouteDiagnosticRuntime& Runtime)
{
  FCrowdDemoSoftPressureRouteDiagnosticCheckpoint Result;
  Result.Agents = Runtime.Agents;
  Result.SampleCount = Runtime.Samples.Num();
  Result.InsideGoalSampleCount = Runtime.InsideGoalCountSamples.Num();
  return Result;
}

void FCrowdDemoSoftPressureRouteDiagnosticKernel::RestoreCheckpoint(
  const FCrowdDemoSoftPressureRouteDiagnosticCheckpoint& Checkpoint,
  FCrowdDemoSoftPressureRouteDiagnosticRuntime& InOutRuntime)
{
  InOutRuntime.Agents = Checkpoint.Agents;
  InOutRuntime.Samples.SetNum(FMath::Min(InOutRuntime.Samples.Num(), Checkpoint.SampleCount));
  InOutRuntime.InsideGoalCountSamples.SetNum(FMath::Min(
    InOutRuntime.InsideGoalCountSamples.Num(), Checkpoint.InsideGoalSampleCount));
}

void FCrowdDemoSoftPressureRouteDiagnosticKernel::BuildSummary(
  const FCrowdDemoSoftPressureRouteDiagnosticRuntime& Runtime,
  const FCrowdDemoSoftPressureRouteCounterfactual& Counterfactual,
  FCrowdDemoSoftPressureRouteDiagnosticSummary& OutSummary)
{
  OutSummary = {};
  OutSummary.Counterfactual = Counterfactual;
  TArray<FCrowdDemoSoftPressureRouteAgentAccumulator> Agents = Runtime.Agents;
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  OutSummary.TotalAgentCount = Agents.Num();
  if (Agents.IsEmpty()) return;

  TSet<int32> SelectedIds;
  TArray<int32> Queue;
  bool bAllCorridorReached = true;
  bool bCorridorOwnedFlowEvidence = false;
  bool bGoalOwnedFlowEvidence = false;
  for (const auto& Agent : Agents)
  {
    OutSummary.NeverReachedAgentCount += Agent.bEverReachedGoal ? 0 : 1;
    OutSummary.ReachedThenLeftAgentCount += Agent.ReachedThenLeftCount > 0 ? 1 : 0;
    OutSummary.GoalBoundaryTransitionCount += Agent.GoalBoundaryTransitionCount;
    OutSummary.ZeroToMaxSpeedTransitionCount += Agent.ZeroToMaxSpeedTransitionCount;
    OutSummary.MaxToZeroSpeedTransitionCount += Agent.MaxToZeroSpeedTransitionCount;
    OutSummary.CorridorEverStalledAgentCount += Agent.bEverCorridorStalled ? 1 : 0;
    OutSummary.CorridorFinalDeadlockAgentCount += Agent.bFinalCorridorDeadlock ? 1 : 0;
    OutSummary.FlowContractViolationCount += Agent.FlowContractViolationCount;
    const bool bCorridorFailure = !Agent.bCorridorReached || Agent.bFinalCorridorDeadlock;
    const bool bGoalFailure = Agent.bCorridorReached && !Agent.bEverReachedGoal;
    OutSummary.CorridorFailureAgentCount += bCorridorFailure ? 1 : 0;
    OutSummary.GoalFailureAgentCount += bGoalFailure ? 1 : 0;
    const bool bFlowConsequence = Agent.CurrentLowSpeedSteps > 0
      || Agent.FinalAppliedForwardCmps + 1.0f < Agent.FinalDesiredForwardCmps
      || (bCorridorFailure && !Agent.bCorridorReached)
      || ((bCorridorFailure || bGoalFailure) && Agent.SampleCount > 45);
    const bool bFailureOwnedFlowViolation = Agent.FlowContractViolationCount > 0
      && bFlowConsequence && (bCorridorFailure || bGoalFailure);
    OutSummary.FailureOwnedFlowContractViolationCount +=
      bFailureOwnedFlowViolation ? Agent.FlowContractViolationCount : 0;
    bCorridorOwnedFlowEvidence |= bCorridorFailure && bFailureOwnedFlowViolation;
    bGoalOwnedFlowEvidence |= bGoalFailure && bFailureOwnedFlowViolation;
    bAllCorridorReached &= Agent.bCorridorReached;
    if (!Agent.bEverReachedGoal || Agent.bEverCorridorStalled)
    {
      SelectedIds.Add(Agent.AgentId);
      Queue.Add(Agent.AgentId);
    }
  }
  Queue.Sort();
  for (int32 Head = 0; Head < Queue.Num(); ++Head)
  {
    const auto* Agent = FindAgent(Agents, Queue[Head]);
    if (!Agent) continue;
    for (const int32 NeighborId : Agent->FinalActiveNeighborAgentIds)
      if (FindAgent(Agents, NeighborId) && !SelectedIds.Contains(NeighborId))
      {
        SelectedIds.Add(NeighborId);
        Queue.Add(NeighborId);
      }
  }
  OutSummary.SelectedAgentCount = SelectedIds.Num();
  if (OutSummary.SelectedAgentCount > 20) return;

  TArray<float> NeverDistances, NeverDesired, NeverApplied, NeverSoft;
  for (const auto& Agent : Agents)
  {
    if (!SelectedIds.Contains(Agent.AgentId)) continue;
    FCrowdDemoSoftPressureRouteAgentResult& Result = OutSummary.Agents.AddDefaulted_GetRef();
    Result.Agent = Agent;
    TSet<int32> Component;
    TArray<int32> ComponentQueue{Agent.AgentId};
    Component.Add(Agent.AgentId);
    for (int32 Head = 0; Head < ComponentQueue.Num(); ++Head)
      if (const auto* Current = FindAgent(Agents, ComponentQueue[Head]))
        for (const int32 NeighborId : Current->FinalActiveNeighborAgentIds)
          if (FindAgent(Agents, NeighborId) && !Component.Contains(NeighborId))
          {
            Component.Add(NeighborId);
            ComponentQueue.Add(NeighborId);
          }
    Result.ConstraintComponentSize = Component.Num();
    for (const int32 NeighborId : Agent.FinalActiveNeighborAgentIds)
      if (const auto* Neighbor = FindAgent(Agents, NeighborId))
      {
        Result.ReachedNeighborCount += Neighbor->bEverReachedGoal ? 1 : 0;
        Result.NonReachedNeighborCount += Neighbor->bEverReachedGoal ? 0 : 1;
      }
    TArray<float> Desired, Applied, Soft;
    for (const auto& Sample : Runtime.Samples)
      if (Sample.AgentId == Agent.AgentId)
      {
        Desired.Add(Sample.DesiredForwardCmps);
        Applied.Add(Sample.AppliedForwardCmps);
        Soft.Add(Sample.PairSoftOppositionCmps);
      }
    Result.DesiredForwardCmpsP50 = Percentile(Desired, 0.50f);
    Result.DesiredForwardCmpsP95 = Percentile(Desired, 0.95f);
    Result.AppliedForwardCmpsP50 = Percentile(Applied, 0.50f);
    Result.AppliedForwardCmpsP95 = Percentile(Applied, 0.95f);
    Result.PairSoftOppositionCmpsP50 = Percentile(Soft, 0.50f);
    Result.PairSoftOppositionCmpsP95 = Percentile(Soft, 0.95f);
    if (!Agent.bEverReachedGoal)
    {
      NeverDistances.Add(Agent.FinalGoalDistanceCm);
      NeverDesired.Append(Desired);
      NeverApplied.Append(Applied);
      NeverSoft.Append(Soft);
    }
  }
  OutSummary.InsideGoalCountP50 = Percentile(Runtime.InsideGoalCountSamples, 0.50f);
  OutSummary.InsideGoalCountP95 = Percentile(Runtime.InsideGoalCountSamples, 0.95f);
  OutSummary.InsideGoalCountMax = Maximum(Runtime.InsideGoalCountSamples);
  OutSummary.NeverReachedDistanceCmP50 = Percentile(NeverDistances, 0.50f);
  OutSummary.NeverReachedDistanceCmP95 = Percentile(NeverDistances, 0.95f);
  OutSummary.NeverReachedDistanceCmMax = Maximum(NeverDistances);
  OutSummary.NeverReachedDesiredForwardCmpsP50 = Percentile(NeverDesired, 0.50f);
  OutSummary.NeverReachedDesiredForwardCmpsP95 = Percentile(NeverDesired, 0.95f);
  OutSummary.NeverReachedAppliedForwardCmpsP50 = Percentile(NeverApplied, 0.50f);
  OutSummary.NeverReachedAppliedForwardCmpsP95 = Percentile(NeverApplied, 0.95f);
  OutSummary.NeverReachedSoftOppositionCmpsP50 = Percentile(NeverSoft, 0.50f);
  OutSummary.NeverReachedSoftOppositionCmpsP95 = Percentile(NeverSoft, 0.95f);

  const float StickyImprovement = Counterfactual.StickyNeverReachedForwardCmps
    - Counterfactual.BaselineNeverReachedForwardCmps;
  const float SoftImprovement = Counterfactual.SoftDisabledNeverReachedForwardCmps
    - Counterfactual.BaselineNeverReachedForwardCmps;
  const bool bStickyEvidence = Counterfactual.bStickyValid && StickyImprovement >= 1.0f
    && OutSummary.ReachedThenLeftAgentCount > 0;
  const bool bSoftEvidence = Counterfactual.bSoftDisabledValid && SoftImprovement >= 1.0f;
  if (bCorridorOwnedFlowEvidence)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::FlowContract;
  else if (OutSummary.CorridorFailureAgentCount > 0)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::CorridorContract;
  else if (bGoalOwnedFlowEvidence)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::FlowContract;
  else if (OutSummary.NeverReachedAgentCount == 0 && bAllCorridorReached
    && OutSummary.CorridorEverStalledAgentCount > 0
    && OutSummary.CorridorFinalDeadlockAgentCount == 0)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::DeadlockMetricOnly;
  else if (bStickyEvidence && bSoftEvidence)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::MixedEvidence;
  else if (bStickyEvidence)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::GoalCompletionOscillation;
  else if (bSoftEvidence)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::SoftPressureOpposition;
  else if (OutSummary.NeverReachedAgentCount > 0
    && OutSummary.NeverReachedAppliedForwardCmpsP50 >= LowSpeedThresholdCmps)
    OutSummary.SelectedBranch = ECrowdDemoSoftPressureRouteBranch::TimeLimited;

  uint32 Hash = 2166136261u;
  Hash = Fold(Hash, OutSummary.TotalAgentCount);
  Hash = Fold(Hash, OutSummary.SelectedAgentCount);
  Hash = Fold(Hash, static_cast<int32>(OutSummary.SelectedBranch));
  Hash = Fold(Hash, OutSummary.FlowContractViolationCount);
  Hash = Fold(Hash, OutSummary.FailureOwnedFlowContractViolationCount);
  Hash = Fold(Hash, OutSummary.CorridorFailureAgentCount);
  Hash = Fold(Hash, OutSummary.GoalFailureAgentCount);
  Hash = Fold(Hash, FMath::RoundToInt(Counterfactual.BaselineNeverReachedForwardCmps));
  Hash = Fold(Hash, FMath::RoundToInt(Counterfactual.StickyNeverReachedForwardCmps));
  Hash = Fold(Hash, FMath::RoundToInt(Counterfactual.SoftDisabledNeverReachedForwardCmps));
  for (const auto& Result : OutSummary.Agents)
  {
    const auto& Agent = Result.Agent;
    Hash = Fold(Hash, Agent.AgentId);
    Hash = Fold(Hash, FMath::RoundToInt(Agent.FinalLocation.X));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.FinalLocation.Y));
    Hash = Fold(Hash, FMath::RoundToInt(Agent.FinalGoalDistanceCm));
    Hash = Fold(Hash, Agent.bEverReachedGoal ? 1 : 0);
    Hash = Fold(Hash, Agent.GoalBoundaryTransitionCount);
    Hash = Fold(Hash, Agent.CurrentLowSpeedSteps);
    Hash = Fold(Hash, Agent.FlowGuidanceOwnedSampleCount);
    Hash = Fold(Hash, Result.ConstraintComponentSize);
    for (const int32 NeighborId : Agent.FinalActiveNeighborAgentIds)
      Hash = Fold(Hash, NeighborId);
  }
  OutSummary.StableHash = Hash;
  OutSummary.bValid = true;
}
