#include "Mass/CrowdDemoBidirectionalSwapKernel.h"

namespace
{
  constexpr uint32 HashOffset = 2166136261u;
  constexpr uint32 HashPrime = 16777619u;

  uint32 Fold(uint32 Hash, const int32 Value)
  {
    const uint32 Bits = static_cast<uint32>(Value);
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= (Bits >> (Byte * 8)) & 0xffu;
      Hash *= HashPrime;
    }
    return Hash;
  }

  uint32 FoldVector(uint32 Hash, const FVector& Value)
  {
    Hash = Fold(Hash, FMath::RoundToInt(Value.X));
    Hash = Fold(Hash, FMath::RoundToInt(Value.Y));
    return Fold(Hash, FMath::RoundToInt(Value.Z));
  }

  void FoldSortedSet(uint32& Hash, const TSet<int32>& Values)
  {
    TArray<int32> Sorted = Values.Array();
    Sorted.Sort();
    Hash = Fold(Hash, Sorted.Num());
    for (const int32 Value : Sorted) Hash = Fold(Hash, Value);
  }

  void FoldSortedMap(uint32& Hash, const TMap<int32, int32>& Values)
  {
    TArray<int32> Keys;
    Values.GetKeys(Keys);
    Keys.Sort();
    Hash = Fold(Hash, Keys.Num());
    for (const int32 Key : Keys)
    {
      Hash = Fold(Hash, Key);
      Hash = Fold(Hash, Values.FindRef(Key));
    }
  }
}

bool FCrowdDemoBidirectionalSwapKernel::IsCohortKeyValid(
  const uint32 CohortKey)
{
  return CohortKey == NorthboundCohortKey
    || CohortKey == SouthboundCohortKey;
}

FCrowdWorkerObjectiveRef
FCrowdDemoBidirectionalSwapKernel::ObjectiveForCohort(
  const uint32 CohortKey)
{
  return {CohortKey == NorthboundCohortKey
    ? NorthObjectiveId
    : CohortKey == SouthboundCohortKey ? SouthObjectiveId : 0};
}

uint64 FCrowdDemoBidirectionalSwapKernel::FlowResourceForCohort(
  const uint32 CohortKey)
{
  return CohortKey == NorthboundCohortKey
    ? NorthFlowResourceId
    : CohortKey == SouthboundCohortKey ? SouthFlowResourceId : 0;
}

FCrowdDemoSharedFlowFieldConfig FCrowdDemoBidirectionalSwapKernel::MakeFlowConfig(
  const uint32 CohortKey)
{
  const int32 CohortIndex = CohortKey == NorthboundCohortKey ? 0 : 1;
  FCrowdDemoSharedFlowFieldConfig Config =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(310 + CohortIndex);
  Config.BoundsMin = FVector(-2600.0f, -3300.0f, 0.0f);
  Config.BoundsMax = FVector(2600.0f, 3300.0f, 0.0f);
  Config.GoalLocation = FVector(
    CohortKey == NorthboundCohortKey
      ? GoalLateralOffsetCm : -GoalLateralOffsetCm,
    CohortKey == NorthboundCohortKey ? NorthSpawnY : SouthSpawnY, 60.0f);
  Config.ObstacleSpecs.Reset();
  Config.ConnectivityContractVersion = 2;
  return Config;
}

FCrowdDemoBidirectionalSwapLayout FCrowdDemoBidirectionalSwapKernel::BuildLayout(
  const TConstArrayView<FCrowdDemoBidirectionalSwapLayoutInput> Inputs,
  const float PhysicalRadiusCm,
  const float HardSafetyGapCm,
  const float FormationSpacingCm)
{
  FCrowdDemoBidirectionalSwapLayout Out;
  TArray<FCrowdDemoBidirectionalSwapLayoutInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FormationIndex != B.FormationIndex)
      return A.FormationIndex < B.FormationIndex;
    return A.AgentId < B.AgentId;
  });
  if (Sorted.Num() != AgentCount
    || PhysicalRadiusCm <= 0.0f || HardSafetyGapCm < 0.0f
    || FormationSpacingCm + KINDA_SMALL_NUMBER
      < PhysicalRadiusCm * 2.0f + HardSafetyGapCm)
    return Out;

  const float HalfWidth = 0.5f * static_cast<float>(AgentsPerCohort - 1);
  const float CohortOneOffset = 0.5f * FormationSpacingCm;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    if (Sorted[Index].AgentId == INDEX_NONE || Sorted[Index].FormationIndex != Index
      || (Index > 0 && Sorted[Index - 1].AgentId == Sorted[Index].AgentId))
      return Out;
    // FormationIndex is fixture-only here: it assigns the explicit cohort once
    // at spawn and is never consulted by runtime flow routing.
    const uint32 CohortKey = Index < AgentsPerCohort
      ? NorthboundCohortKey : SouthboundCohortKey;
    const int32 LocalIndex = Index % AgentsPerCohort;
    auto& Agent = Out.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Sorted[Index].AgentId;
    Agent.FormationIndex = Index;
    Agent.CohortKey = CohortKey;
    Agent.ObjectiveRef = ObjectiveForCohort(CohortKey);
    Agent.FlowResourceId = FlowResourceForCohort(CohortKey);
    Agent.SpawnLocation = FVector(
      (static_cast<float>(LocalIndex) - HalfWidth) * FormationSpacingCm
        + (CohortKey == SouthboundCohortKey ? CohortOneOffset : 0.0f),
      CohortKey == NorthboundCohortKey ? SouthSpawnY : NorthSpawnY,
      60.0f);
    const FCrowdDemoSharedFlowFieldConfig Config = MakeFlowConfig(CohortKey);
    const float Clearance = PhysicalRadiusCm + HardSafetyGapCm;
    if (Agent.SpawnLocation.X < FVector(Config.BoundsMin).X + Clearance
      || Agent.SpawnLocation.X > FVector(Config.BoundsMax).X - Clearance
      || Agent.SpawnLocation.Y < FVector(Config.BoundsMin).Y + Clearance
      || Agent.SpawnLocation.Y > FVector(Config.BoundsMax).Y - Clearance)
      return Out;
  }

  Out.LayoutHash = HashOffset;
  Out.LayoutHash = Fold(Out.LayoutHash, AgentCount);
  Out.LayoutHash = Fold(Out.LayoutHash, AgentsPerCohort);
  Out.LayoutHash = Fold(Out.LayoutHash, FMath::RoundToInt(FormationSpacingCm));
  for (const auto& Agent : Out.Agents)
  {
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.AgentId);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.FormationIndex);
    Out.LayoutHash = Fold(Out.LayoutHash, static_cast<int32>(Agent.CohortKey));
    Out.LayoutHash = Fold(
      Out.LayoutHash, static_cast<int32>(Agent.ObjectiveRef.ObjectiveId));
    Out.LayoutHash = Fold(
      Out.LayoutHash, static_cast<int32>(Agent.FlowResourceId));
    Out.LayoutHash = FoldVector(Out.LayoutHash, Agent.SpawnLocation);
  }
  Out.bValid = true;
  return Out;
}

void FCrowdDemoBidirectionalSwapKernel::UpdateProgress(
  const TConstArrayView<FCrowdDemoBidirectionalSwapStepAgent> Agents,
  const int32 FixedStepIndex,
  FCrowdDemoBidirectionalSwapProgress& InOutProgress)
{
  TArray<FCrowdDemoBidirectionalSwapStepAgent> Sorted(Agents);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  bool bStepValid = Sorted.Num() == AgentCount && FixedStepIndex >= 0;
  TSet<int32> CurrentDeadlocks;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    const auto& Agent = Sorted[Index];
    if (Agent.AgentId == INDEX_NONE || !IsCohortKeyValid(Agent.CohortKey)
      || (Index > 0 && Sorted[Index - 1].AgentId == Agent.AgentId))
    {
      bStepValid = false;
      continue;
    }
    const float DirectionSign =
      Agent.CohortKey == NorthboundCohortKey ? 1.0f : -1.0f;
    const float ForwardPosition = Agent.Location.Y * DirectionSign;
    const float ForwardSpeed = Agent.Velocity.Y * DirectionSign;
    if (ForwardPosition >= 0.0f)
      InOutProgress.CenterCrossedAgentIds.Add(Agent.AgentId);
    const bool bComplete = ForwardPosition >= NorthCompletionPlaneY;
    if (bComplete)
    {
      InOutProgress.CompletedAgentIds.Add(Agent.AgentId);
      InOutProgress.ConsecutiveLowForwardStepsByAgentId.Add(Agent.AgentId, 0);
      if (!InOutProgress.CompletionStepByAgentId.Contains(Agent.AgentId))
        InOutProgress.CompletionStepByAgentId.Add(Agent.AgentId, FixedStepIndex);
    }
    else
    {
      int32& LowSteps =
        InOutProgress.ConsecutiveLowForwardStepsByAgentId.FindOrAdd(Agent.AgentId);
      LowSteps = ForwardSpeed <= 10.0f ? LowSteps + 1 : 0;
      if (LowSteps >= 90) CurrentDeadlocks.Add(Agent.AgentId);
    }
    if (Agent.FlowStatus != ECrowdDemoFlowLocationStatus::Reachable)
      ++InOutProgress.UnreachableSampleCount;
  }
  InOutProgress.FinalDeadlockAgentIds = MoveTemp(CurrentDeadlocks);
  InOutProgress.LastFixedStepIndex = FixedStepIndex;
  InOutProgress.bValid = InOutProgress.bValid && bStepValid;

  uint32 Hash = HashOffset;
  Hash = Fold(Hash, InOutProgress.bValid ? 1 : 0);
  Hash = Fold(Hash, InOutProgress.LastFixedStepIndex);
  Hash = Fold(Hash, InOutProgress.UnreachableSampleCount);
  FoldSortedSet(Hash, InOutProgress.CenterCrossedAgentIds);
  FoldSortedSet(Hash, InOutProgress.CompletedAgentIds);
  FoldSortedSet(Hash, InOutProgress.FinalDeadlockAgentIds);
  FoldSortedMap(Hash, InOutProgress.ConsecutiveLowForwardStepsByAgentId);
  FoldSortedMap(Hash, InOutProgress.CompletionStepByAgentId);
  InOutProgress.ProgressHash = Hash;
}
