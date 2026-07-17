#include "Mass/CrowdDemoTargetStabilityDiagnosticKernel.h"

#include "Algo/BinarySearch.h"

namespace
{
constexpr uint32 FnvOffset = 2166136261u;
constexpr uint32 FnvPrime = 16777619u;

void Fold(uint32& Hash, const int32 Value)
{
  Hash ^= static_cast<uint32>(Value);
  Hash *= FnvPrime;
}

int32 Q(const float Value)
{
  return FMath::RoundToInt(Value);
}

float Percentile(TArray<float> Values, const float Fraction)
{
  if (Values.IsEmpty()) return 0.0f;
  Values.Sort();
  const int32 Index = FMath::Clamp(
    FMath::CeilToInt(Fraction * static_cast<float>(Values.Num())) - 1,
    0, Values.Num() - 1);
  return Values[Index];
}

const FCrowdDemoTargetStabilityAgentSample* FindAgent(
  const FCrowdDemoTargetStabilityStepSample& Step, const int32 AgentId)
{
  const int32 Index = Algo::LowerBoundBy(Step.Agents, AgentId,
    [](const FCrowdDemoTargetStabilityAgentSample& Agent) { return Agent.AgentId; });
  return Step.Agents.IsValidIndex(Index) && Step.Agents[Index].AgentId == AgentId
    ? &Step.Agents[Index] : nullptr;
}

bool IsTerminal(const FCrowdDemoTargetStabilityAgentSample& Agent)
{
  return Agent.GuidanceMode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle
    || Agent.bTerminalStay;
}

ECrowdDemoTargetRegionCoverageLossStage ClassifyCoverageLoss(
  const FCrowdDemoTargetStabilityRegionSample& Region)
{
  if (!Region.bFeasible || Region.CurrentPopulation > 0)
    return ECrowdDemoTargetRegionCoverageLossStage::None;
  if (Region.DesiredPopulation <= 0)
    return ECrowdDemoTargetRegionCoverageLossStage::Demand;
  if (Region.PrimaryIncomingPlanQuota <= 0)
    return ECrowdDemoTargetRegionCoverageLossStage::PlanQuota;
  if (Region.PrimaryIncomingConsumedQuota <= 0
    && Region.GuidanceTargetCount <= 0)
    return ECrowdDemoTargetRegionCoverageLossStage::Guidance;
  return ECrowdDemoTargetRegionCoverageLossStage::TerminalRetention;
}
}

void FCrowdDemoTargetStabilityDiagnosticKernel::RecordStep(
  const FCrowdDemoTargetStabilityStepSample& Step,
  FCrowdDemoTargetStabilityRuntime& InOutRuntime)
{
  FCrowdDemoTargetStabilityStepSample Sorted = Step;
  Sorted.Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  Sorted.Regions.Sort([](const auto& A, const auto& B)
  {
    return A.CohortKey != B.CohortKey ? A.CohortKey < B.CohortKey
      : A.RegionKey < B.RegionKey;
  });
  for (auto& Region : Sorted.Regions)
  {
    Region.TerminalAgentIds.Sort();
    Region.TerminalSettleAgentIds.Sort();
    Region.SupplyAgentIds.Sort();
  }
  Sorted.Edges.Sort([](const auto& A, const auto& B)
  {
    if (A.CohortKey != B.CohortKey) return A.CohortKey < B.CohortKey;
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  const int32 Expected = InOutRuntime.Settings.ExpectedAgentCount;
  bool bValid = Sorted.FixedStepIndex >= 0 && Sorted.FixedStepSeconds > 0.0f
    && (Expected <= 0 || Sorted.Agents.Num() == Expected);
  for (int32 Index = 0; Index < Sorted.Agents.Num(); ++Index)
  {
    bValid &= Sorted.Agents[Index].AgentId != INDEX_NONE;
    bValid &= Index == 0 || Sorted.Agents[Index - 1].AgentId != Sorted.Agents[Index].AgentId;
  }
  for (int32 Index = 0; Index < Sorted.Regions.Num(); ++Index)
  {
    const auto& Region = Sorted.Regions[Index];
    bValid &= Region.RegionKey != INDEX_NONE;
    bValid &= Index == 0
      || Sorted.Regions[Index - 1].CohortKey != Region.CohortKey
      || Sorted.Regions[Index - 1].RegionKey != Region.RegionKey;
    auto IsStrictlySortedUnique = [](const TArray<int32>& Values)
    {
      for (int32 Item = 0; Item < Values.Num(); ++Item)
        if (Values[Item] == INDEX_NONE
          || (Item > 0 && Values[Item - 1] >= Values[Item])) return false;
      return true;
    };
    bValid &= IsStrictlySortedUnique(Region.TerminalAgentIds);
    bValid &= IsStrictlySortedUnique(Region.TerminalSettleAgentIds);
    bValid &= IsStrictlySortedUnique(Region.SupplyAgentIds);
  }
  for (int32 Index = 0; Index < Sorted.Edges.Num(); ++Index)
  {
    const auto& Edge = Sorted.Edges[Index];
    bValid &= Edge.FromCellKey != INDEX_NONE && Edge.ToCellKey != INDEX_NONE
      && Edge.AgentQuota >= 0 && Edge.ConsumedQuota >= 0
      && Edge.ConsumedQuota <= Edge.AgentQuota;
    bValid &= Index == 0
      || Sorted.Edges[Index - 1].CohortKey != Edge.CohortKey
      || Sorted.Edges[Index - 1].FromCellKey != Edge.FromCellKey
      || Sorted.Edges[Index - 1].ToCellKey != Edge.ToCellKey;
  }
  if (!InOutRuntime.Steps.IsEmpty())
    bValid &= Sorted.FixedStepIndex > InOutRuntime.Steps.Last().FixedStepIndex;
  InOutRuntime.bInputValid &= bValid;
  if (!bValid) return;
  InOutRuntime.Steps.Add(MoveTemp(Sorted));
  const int32 Keep = FMath::Max(1, InOutRuntime.Settings.StableWindowSteps);
  if (InOutRuntime.Steps.Num() > Keep)
    InOutRuntime.Steps.RemoveAt(0, InOutRuntime.Steps.Num() - Keep, EAllowShrinking::No);
}

FCrowdDemoTargetStabilityCheckpoint
FCrowdDemoTargetStabilityDiagnosticKernel::MakeCheckpoint(
  const FCrowdDemoTargetStabilityRuntime& Runtime)
{
  return {Runtime};
}

void FCrowdDemoTargetStabilityDiagnosticKernel::RestoreCheckpoint(
  const FCrowdDemoTargetStabilityCheckpoint& Checkpoint,
  FCrowdDemoTargetStabilityRuntime& InOutRuntime)
{
  InOutRuntime = Checkpoint.Runtime;
}

void FCrowdDemoTargetStabilityDiagnosticKernel::BuildSummary(
  const FCrowdDemoTargetStabilityRuntime& Runtime,
  FCrowdDemoTargetStabilitySummary& OutSummary)
{
  OutSummary = {};
  OutSummary.SampleStepCount = Runtime.Steps.Num();
  OutSummary.WindowStepCount = Runtime.Steps.Num();
  if (!Runtime.bInputValid)
  {
    OutSummary.PrimaryCause = ECrowdDemoTargetStabilityPrimaryCause::InvalidInput;
    return;
  }
  if (Runtime.Steps.IsEmpty()) return;
  OutSummary.AgentCount = Runtime.Steps.Last().Agents.Num();
  OutSummary.InsideBandMin = MAX_int32;
  OutSummary.CoverageMin = MAX_int32;
  TMap<int32, int32> ConsecutiveBlocked;
  TMap<int32, int32> MaxBlocked;
  TSet<int32> BlockedAgents;
  TSet<int32> ChatterAgents;
  TMap<int32, int32> PendingTerminalExitStep;
  TMap<int32, int32> PendingTerminalExitRevision;
  TMap<int32, uint32> PendingTerminalExitGraph;
  TMap<int32, bool> PreviousTerminal;
  TMap<int32, int32> PreviousTerminalRegionByAgent;
  bool bHasPreviousRegionMembership = false;
  TArray<float> RelativeSpeeds;
  int32 SettledConsecutive = 0;
  bool bHasPreviousSoft = false;
  float PreviousSoft = 0.0f;

  uint32 Hash = FnvOffset;
  for (const FCrowdDemoTargetStabilityStepSample& Step : Runtime.Steps)
  {
    OutSummary.InsideBandMin = FMath::Min(OutSummary.InsideBandMin, Step.InsideBandCount);
    OutSummary.CoverageMin = FMath::Min(OutSummary.CoverageMin, Step.CoverageCount);
    OutSummary.CoverageRequired = FMath::Max(
      OutSummary.CoverageRequired, Step.RequiredCoverageCount);
    Fold(Hash, Step.FixedStepIndex);
    Fold(Hash, Step.TargetRevision);
    Fold(Hash, static_cast<int32>(Step.FeasibleGraphHash));
    Fold(Hash, Step.InsideBandCount);
    Fold(Hash, Step.CoverageCount);
    Fold(Hash, Step.RequiredCoverageCount);
    Fold(Hash, Q(Step.ParticleSoftErrorCmP95));
    Fold(Hash, Q(Step.ParticleMaxActualCorrectionCm));

    bool bDemandGap = false;
    bool bPlanGap = false;
    bool bGuidanceGap = false;
    bool bRetentionGap = false;
    TMap<int32, int32> CurrentTerminalRegionByAgent;
    for (const FCrowdDemoTargetStabilityRegionSample& Region : Step.Regions)
    {
      Fold(Hash, static_cast<int32>(Region.CohortKey));
      Fold(Hash, Region.RegionKey);
      Fold(Hash, Region.AvailableCapacity);
      Fold(Hash, Region.CurrentPopulation);
      Fold(Hash, Region.DesiredPopulation);
      Fold(Hash, Region.Deficit);
      Fold(Hash, Region.Surplus);
      Fold(Hash, Region.PrimaryIncomingPlanQuota);
      Fold(Hash, Region.PrimaryIncomingConsumedQuota);
      Fold(Hash, Region.GuidanceTargetCount);
      Fold(Hash, Region.bFeasible ? 1 : 0);
      Fold(Hash, Region.TerminalAgentIds.Num());
      for (const int32 AgentId : Region.TerminalAgentIds)
      {
        Fold(Hash, AgentId);
        CurrentTerminalRegionByAgent.Add(AgentId, Region.RegionKey);
      }
      Fold(Hash, Region.TerminalSettleAgentIds.Num());
      for (const int32 AgentId : Region.TerminalSettleAgentIds) Fold(Hash, AgentId);
      Fold(Hash, Region.SupplyAgentIds.Num());
      for (const int32 AgentId : Region.SupplyAgentIds) Fold(Hash, AgentId);
      switch (ClassifyCoverageLoss(Region))
      {
      case ECrowdDemoTargetRegionCoverageLossStage::Demand: bDemandGap = true; break;
      case ECrowdDemoTargetRegionCoverageLossStage::PlanQuota: bPlanGap = true; break;
      case ECrowdDemoTargetRegionCoverageLossStage::Guidance: bGuidanceGap = true; break;
      case ECrowdDemoTargetRegionCoverageLossStage::TerminalRetention:
        bRetentionGap = true; break;
      default: break;
      }
    }
    for (const FCrowdDemoTargetStabilityEdgeSample& Edge : Step.Edges)
    {
      Fold(Hash, static_cast<int32>(Edge.CohortKey));
      Fold(Hash, Edge.FromCellKey);
      Fold(Hash, Edge.ToCellKey);
      Fold(Hash, Edge.FromRegionKey);
      Fold(Hash, Edge.ToRegionKey);
      Fold(Hash, Edge.AgentQuota);
      Fold(Hash, Edge.ConsumedQuota);
      Fold(Hash, Edge.bToTerminal ? 1 : 0);
    }
    OutSummary.RegionDemandGapStepCount += bDemandGap ? 1 : 0;
    OutSummary.RegionPlanQuotaGapStepCount += bPlanGap ? 1 : 0;
    OutSummary.RegionGuidanceGapStepCount += bGuidanceGap ? 1 : 0;
    OutSummary.RegionTerminalRetentionGapStepCount += bRetentionGap ? 1 : 0;
    if (bHasPreviousRegionMembership)
    {
      for (const auto& Current : CurrentTerminalRegionByAgent)
      {
        const int32* Previous = PreviousTerminalRegionByAgent.Find(Current.Key);
        if (!Previous || *Previous != Current.Value)
          ++OutSummary.RegionTerminalEnterCount;
      }
      for (const auto& Previous : PreviousTerminalRegionByAgent)
      {
        const int32* Current = CurrentTerminalRegionByAgent.Find(Previous.Key);
        if (!Current || *Current != Previous.Value)
          ++OutSummary.RegionTerminalExitCount;
      }
    }
    PreviousTerminalRegionByAgent = MoveTemp(CurrentTerminalRegionByAgent);
    bHasPreviousRegionMembership = true;

    const bool bSettled = Step.ParticleMaxActualCorrectionCm
        <= Runtime.Settings.ParticleCorrectionThresholdCm + KINDA_SMALL_NUMBER
      && (!bHasPreviousSoft || FMath::Abs(
        Step.ParticleSoftErrorCmP95 - PreviousSoft)
          <= Runtime.Settings.SoftErrorDeltaThresholdCm + KINDA_SMALL_NUMBER);
    SettledConsecutive = bSettled ? SettledConsecutive + 1 : 0;
    OutSummary.ParticleSettledMaxConsecutiveSteps = FMath::Max(
      OutSummary.ParticleSettledMaxConsecutiveSteps, SettledConsecutive);
    if (SettledConsecutive >= Runtime.Settings.ParticleSettlingSteps)
      ++OutSummary.ParticleSettledWindowCount;
    PreviousSoft = Step.ParticleSoftErrorCmP95;
    bHasPreviousSoft = true;

    TMap<int32, TArray<const FCrowdDemoTargetStabilityAgentSample*>> ByNextCell;
    for (const auto& Agent : Step.Agents)
    {
      Fold(Hash, Agent.AgentId);
      Fold(Hash, Agent.CurrentCellKey);
      Fold(Hash, Agent.NextCellKey);
      Fold(Hash, Agent.CurrentRegionKey);
      Fold(Hash, static_cast<int32>(Agent.GuidanceMode));
      Fold(Hash, Q(Agent.Location.X)); Fold(Hash, Q(Agent.Location.Y));
      Fold(Hash, Q(Agent.Velocity.X)); Fold(Hash, Q(Agent.Velocity.Y));
      Fold(Hash, Q(Agent.TargetLocation.X)); Fold(Hash, Q(Agent.TargetLocation.Y));
      Fold(Hash, Q(Agent.TargetVelocity.X)); Fold(Hash, Q(Agent.TargetVelocity.Y));
      Fold(Hash, Q(Agent.DesiredVelocity.X)); Fold(Hash, Q(Agent.DesiredVelocity.Y));
      Fold(Hash, Q(Agent.LocalVelocity.X)); Fold(Hash, Q(Agent.LocalVelocity.Y));
      Fold(Hash, Q(Agent.PredictedVelocity.X)); Fold(Hash, Q(Agent.PredictedVelocity.Y));
      Fold(Hash, Q(Agent.AppliedVelocity.X)); Fold(Hash, Q(Agent.AppliedVelocity.Y));
      Fold(Hash, Q(Agent.PairSoftCorrection.X)); Fold(Hash, Q(Agent.PairSoftCorrection.Y));
      Fold(Hash, Q(Agent.TotalParticleCorrection.X)); Fold(Hash, Q(Agent.TotalParticleCorrection.Y));
      Fold(Hash, Agent.bTerminal ? 1 : 0); Fold(Hash, Agent.bTerminalStay ? 1 : 0);
      Fold(Hash, Agent.bSupply ? 1 : 0); Fold(Hash, Agent.RegionSurplusCount);
      Fold(Hash, Agent.LocalNeighborCount); Fold(Hash, Agent.LocalConstraintCount);
      Fold(Hash, Agent.LocalBlockedAgeSteps); Fold(Hash, Agent.bLocalValid ? 1 : 0);
      Fold(Hash, Agent.bLocalGranted ? 1 : 0); Fold(Hash, Agent.bLocalYielding ? 1 : 0);
      RelativeSpeeds.Add((Agent.AppliedVelocity - Agent.TargetVelocity).Size());
      if (Agent.GuidanceMode == ECrowdDemoTargetRegionGuidanceMode::Transport
        && Agent.NextCellKey != INDEX_NONE)
        ByNextCell.FindOrAdd(Agent.NextCellKey).Add(&Agent);

      const bool bTerminal = IsTerminal(Agent);
      const bool bWasTerminal = PreviousTerminal.FindRef(Agent.AgentId);
      if (bWasTerminal && !bTerminal && !Agent.bSupply && Agent.RegionSurplusCount <= 0)
      {
        PendingTerminalExitStep.Add(Agent.AgentId, Step.FixedStepIndex);
        PendingTerminalExitRevision.Add(Agent.AgentId, Step.TargetRevision);
        PendingTerminalExitGraph.Add(Agent.AgentId, Step.FeasibleGraphHash);
      }
      if (!bWasTerminal && bTerminal)
      {
        const int32* ExitStep = PendingTerminalExitStep.Find(Agent.AgentId);
        const int32* Revision = PendingTerminalExitRevision.Find(Agent.AgentId);
        const uint32* Graph = PendingTerminalExitGraph.Find(Agent.AgentId);
        if (ExitStep && Revision && Graph
          && Step.FixedStepIndex - *ExitStep <= Runtime.Settings.TerminalChatterWindowSteps
          && *Revision == Step.TargetRevision && *Graph == Step.FeasibleGraphHash)
        {
          ++OutSummary.TerminalChatterCount;
          ChatterAgents.Add(Agent.AgentId);
          if (OutSummary.FirstWitnessStep == INDEX_NONE)
          {
            OutSummary.FirstWitnessStep = *ExitStep;
            OutSummary.FirstWitnessAgentId = Agent.AgentId;
          }
        }
        PendingTerminalExitStep.Remove(Agent.AgentId);
        PendingTerminalExitRevision.Remove(Agent.AgentId);
        PendingTerminalExitGraph.Remove(Agent.AgentId);
      }
      PreviousTerminal.Add(Agent.AgentId, bTerminal);
    }

    bool bStepContended = false;
    TSet<int32> EvaluatedBlockedAgents;
    TArray<int32> NextCellKeys;
    ByNextCell.GetKeys(NextCellKeys);
    NextCellKeys.Sort();
    for (const int32 NextCellKey : NextCellKeys)
    {
      TArray<const FCrowdDemoTargetStabilityAgentSample*>& Group = ByNextCell.FindChecked(NextCellKey);
      Group.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
      if (Group.Num() < 2) continue;
      bStepContended = true;
      ++OutSummary.ContendedGroupCount;
      for (const auto* Agent : Group)
      {
        EvaluatedBlockedAgents.Add(Agent->AgentId);
        const FVector2f Requested = Agent->DesiredVelocity - Agent->TargetVelocity;
        const float RequestedSpeed = Requested.Size();
        const FVector2f Direction = RequestedSpeed > UE_SMALL_NUMBER
          ? Requested / RequestedSpeed : FVector2f::ZeroVector;
        const float AppliedForward = FVector2f::DotProduct(
          Agent->AppliedVelocity - Agent->TargetVelocity, Direction);
        const float CorrectionForwardCmps = FVector2f::DotProduct(
          Agent->TotalParticleCorrection, Direction)
            / FMath::Max(Step.FixedStepSeconds, 0.001f);
        const bool bBlocked = RequestedSpeed + KINDA_SMALL_NUMBER
            >= Runtime.Settings.RequestedSpeedThresholdCmps
          && AppliedForward <= Runtime.Settings.AppliedSpeedThresholdCmps + KINDA_SMALL_NUMBER
          && CorrectionForwardCmps < -KINDA_SMALL_NUMBER;
        const int32 Count = bBlocked ? ConsecutiveBlocked.FindRef(Agent->AgentId) + 1 : 0;
        ConsecutiveBlocked.Add(Agent->AgentId, Count);
        MaxBlocked.Add(Agent->AgentId, FMath::Max(MaxBlocked.FindRef(Agent->AgentId), Count));
        if (Count >= Runtime.Settings.MergeBlockedSteps)
        {
          BlockedAgents.Add(Agent->AgentId);
          if (OutSummary.FirstWitnessStep == INDEX_NONE)
          {
            OutSummary.FirstWitnessStep = Step.FixedStepIndex;
            OutSummary.FirstWitnessAgentId = Agent->AgentId;
            OutSummary.FirstWitnessNextCellKey = NextCellKey;
          }
        }
      }
    }
    for (const auto& Agent : Step.Agents)
      if (!EvaluatedBlockedAgents.Contains(Agent.AgentId))
        ConsecutiveBlocked.Add(Agent.AgentId, 0);
    if (bStepContended) ++OutSummary.ContendedStepCount;
  }

  for (const auto& Pair : MaxBlocked)
    OutSummary.MergeBlockedMaxConsecutiveSteps = FMath::Max(
      OutSummary.MergeBlockedMaxConsecutiveSteps, Pair.Value);
  OutSummary.MergeBlockedAgentCount = BlockedAgents.Num();
  OutSummary.TerminalChatterAgentCount = ChatterAgents.Num();
  OutSummary.AttractionRejectionCycleCount = OutSummary.TerminalChatterCount;
  OutSummary.TargetRelativeSpeedCmpsP95 = Percentile(RelativeSpeeds, 0.95f);
  for (const float Value : RelativeSpeeds)
    OutSummary.TargetRelativeSpeedCmpsMax = FMath::Max(
      OutSummary.TargetRelativeSpeedCmpsMax, Value);

  TArray<float> JitterSamples;
  const int32 PositionWindow = FMath::Max(2, Runtime.Settings.PositionWindowSteps);
  if (Runtime.Steps.Num() >= PositionWindow)
  {
    const int32 First = Runtime.Steps.Num() - PositionWindow;
    for (const auto& LastAgent : Runtime.Steps.Last().Agents)
    {
      float MaxDistance = 0.0f;
      for (int32 A = First; A < Runtime.Steps.Num(); ++A)
      {
        const auto* SA = FindAgent(Runtime.Steps[A], LastAgent.AgentId);
        if (!SA) continue;
        const FVector2f PA = SA->Location - SA->TargetLocation;
        for (int32 B = A + 1; B < Runtime.Steps.Num(); ++B)
        {
          const auto* SB = FindAgent(Runtime.Steps[B], LastAgent.AgentId);
          if (SB) MaxDistance = FMath::Max(MaxDistance,
            (PA - (SB->Location - SB->TargetLocation)).Size());
        }
      }
      JitterSamples.Add(MaxDistance);
    }
  }
  OutSummary.PositionPeakToPeakCmP95 = Percentile(JitterSamples, 0.95f);
  for (const float Value : JitterSamples)
    OutSummary.PositionPeakToPeakCmMax = FMath::Max(
      OutSummary.PositionPeakToPeakCmMax, Value);
  OutSummary.FinalAgents = Runtime.Steps.Last().Agents;
  OutSummary.FinalRegions = Runtime.Steps.Last().Regions;
  OutSummary.FinalEdges = Runtime.Steps.Last().Edges;
  OutSummary.MinimumExecutableSpeedCmps =
    0.5f * FMath::Max(0.0f, Runtime.Settings.PositionQuantumCm)
      / FMath::Max(Runtime.Steps.Last().FixedStepSeconds, 0.001f);
  for (const FCrowdDemoTargetStabilityAgentSample& Agent : OutSummary.FinalAgents)
  {
    if (!Agent.bSupply || Agent.DesiredVelocity.IsNearlyZero()
      || Agent.LocalVelocity.IsNearlyZero()
      || Agent.LocalVelocity.Size() + KINDA_SMALL_NUMBER
        >= OutSummary.MinimumExecutableSpeedCmps)
      continue;
    ++OutSummary.FinalSubQuantumSupplyAgentCount;
    if (OutSummary.FirstSubQuantumSupplyAgentId == INDEX_NONE)
      OutSummary.FirstSubQuantumSupplyAgentId = Agent.AgentId;
  }
  for (const FCrowdDemoTargetStabilityRegionSample& Region : OutSummary.FinalRegions)
  {
    if (!Region.bFeasible || Region.CurrentPopulation > 0) continue;
    ++OutSummary.FinalMissingRegionCount;
    if (OutSummary.FirstMissingRegionKey == INDEX_NONE)
    {
      OutSummary.FirstMissingRegionKey = Region.RegionKey;
      OutSummary.FirstMissingCohortKey = Region.CohortKey;
      OutSummary.FirstMissingRegionStage = ClassifyCoverageLoss(Region);
    }
  }
  if (Runtime.Steps.Num() < Runtime.Settings.ParticleSettlingSteps)
  {
    OutSummary.PrimaryCause = ECrowdDemoTargetStabilityPrimaryCause::InsufficientSamples;
    Fold(Hash, static_cast<int32>(OutSummary.PrimaryCause));
    OutSummary.StableHash = Hash;
    return;
  }
  const bool bMerge = OutSummary.MergeBlockedAgentCount > 0;
  const bool bChatter = OutSummary.TerminalChatterCount > 0;
  const bool bTerminalCoverageComplete = OutSummary.InsideBandMin >= OutSummary.AgentCount
    && OutSummary.CoverageMin >= OutSummary.CoverageRequired;
  const bool bParticle = !bMerge && !bChatter && bTerminalCoverageComplete
    && OutSummary.ParticleSettledWindowCount == 0;
  const int32 CauseCount = (bMerge ? 1 : 0) + (bChatter ? 1 : 0) + (bParticle ? 1 : 0);
  OutSummary.PrimaryCause = CauseCount > 1 ? ECrowdDemoTargetStabilityPrimaryCause::Mixed
    : bMerge ? ECrowdDemoTargetStabilityPrimaryCause::MergeCapacity
    : bChatter ? ECrowdDemoTargetStabilityPrimaryCause::TerminalChatter
    : bParticle ? ECrowdDemoTargetStabilityPrimaryCause::ParticleNotSettled
    : ECrowdDemoTargetStabilityPrimaryCause::Stable;
  Fold(Hash, static_cast<int32>(OutSummary.PrimaryCause));
  Fold(Hash, OutSummary.MergeBlockedAgentCount);
  Fold(Hash, OutSummary.TerminalChatterCount);
  Fold(Hash, OutSummary.ParticleSettledWindowCount);
  Fold(Hash, Q(OutSummary.TargetRelativeSpeedCmpsP95));
  Fold(Hash, Q(OutSummary.PositionPeakToPeakCmP95));
  Fold(Hash, OutSummary.FinalMissingRegionCount);
  Fold(Hash, static_cast<int32>(OutSummary.FirstMissingCohortKey));
  Fold(Hash, OutSummary.FirstMissingRegionKey);
  Fold(Hash, static_cast<int32>(OutSummary.FirstMissingRegionStage));
  Fold(Hash, OutSummary.RegionDemandGapStepCount);
  Fold(Hash, OutSummary.RegionPlanQuotaGapStepCount);
  Fold(Hash, OutSummary.RegionGuidanceGapStepCount);
  Fold(Hash, OutSummary.RegionTerminalRetentionGapStepCount);
  Fold(Hash, OutSummary.RegionTerminalEnterCount);
  Fold(Hash, OutSummary.RegionTerminalExitCount);
  Fold(Hash, OutSummary.FinalSubQuantumSupplyAgentCount);
  Fold(Hash, OutSummary.FirstSubQuantumSupplyAgentId);
  Fold(Hash, Q(OutSummary.MinimumExecutableSpeedCmps));
  OutSummary.StableHash = Hash;
  OutSummary.bValid = Runtime.Steps.Num() >= Runtime.Settings.ParticleSettlingSteps;
}
