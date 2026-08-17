#include "Mass/CrowdDemoValidCorridorTransitKernel.h"

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

FCrowdDemoSharedFlowFieldConfig FCrowdDemoValidCorridorTransitKernel::MakeFlowConfig()
{
  FCrowdDemoSharedFlowFieldConfig Config =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Config.AgentInflateCm = 52.0f;
  Config.ConnectivityContractVersion = 2;
  return Config;
}

FCrowdDemoValidCorridorTransitLayout
FCrowdDemoValidCorridorTransitKernel::BuildLayout(
  const TConstArrayView<FCrowdDemoValidCorridorTransitLayoutInput> Inputs,
  const float PhysicalRadiusCm,
  const float HardSafetyGapCm,
  const FVector& SpawnOrigin,
  const float LayoutSpacingCm)
{
  FCrowdDemoValidCorridorTransitLayout Out;
  TArray<FCrowdDemoValidCorridorTransitLayoutInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FormationIndex != B.FormationIndex)
      return A.FormationIndex < B.FormationIndex;
    return A.AgentId < B.AgentId;
  });
  if (Sorted.Num() != AgentCount || PhysicalRadiusCm <= 0.0f
    || HardSafetyGapCm < 0.0f
    || !FMath::IsFinite(LayoutSpacingCm)
    || LayoutSpacingCm + KINDA_SMALL_NUMBER
      < PhysicalRadiusCm * 2.0f + HardSafetyGapCm)
    return Out;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    if (Sorted[Index].AgentId == INDEX_NONE || Sorted[Index].FormationIndex != Index
      || (Index > 0 && Sorted[Index - 1].AgentId == Sorted[Index].AgentId))
      return Out;

  const int32 Rows = FMath::CeilToInt(
    static_cast<float>(AgentCount) / static_cast<float>(FormationColumns));
  const FCrowdDemoSharedFlowFieldConfig Config = MakeFlowConfig();
  const float Clearance = PhysicalRadiusCm + HardSafetyGapCm;
  for (const auto& Input : Sorted)
  {
    auto& Agent = Out.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Input.AgentId;
    Agent.FormationIndex = Input.FormationIndex;
    Agent.SpawnLocation = SpawnOrigin + FVector(
      (Input.FormationIndex % FormationColumns
        - 0.5f * static_cast<float>(FormationColumns - 1)) * LayoutSpacingCm,
      (Input.FormationIndex / FormationColumns
        - 0.5f * static_cast<float>(Rows - 1)) * LayoutSpacingCm,
      0.0f);
    if (Agent.SpawnLocation.X < FVector(Config.BoundsMin).X + Clearance
      || Agent.SpawnLocation.X > FVector(Config.BoundsMax).X - Clearance
      || Agent.SpawnLocation.Y < FVector(Config.BoundsMin).Y + Clearance
      || Agent.SpawnLocation.Y > FVector(Config.BoundsMax).Y - Clearance
      || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        Config, Agent.SpawnLocation))
      return Out;
  }

  Out.LayoutHash = HashOffset;
  Out.LayoutHash = Fold(Out.LayoutHash, AgentCount);
  Out.LayoutHash = Fold(Out.LayoutHash, FormationColumns);
  Out.LayoutHash = Fold(Out.LayoutHash, FMath::RoundToInt(LayoutSpacingCm));
  for (const auto& Agent : Out.Agents)
  {
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.AgentId);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.FormationIndex);
    Out.LayoutHash = FoldVector(Out.LayoutHash, Agent.SpawnLocation);
  }
  Out.bValid = true;
  return Out;
}

void FCrowdDemoValidCorridorTransitKernel::UpdateProgress(
  const TConstArrayView<FCrowdDemoValidCorridorTransitStepAgent> Agents,
  const int32 FixedStepIndex,
  FCrowdDemoValidCorridorTransitProgress& InOutProgress)
{
  TArray<FCrowdDemoValidCorridorTransitStepAgent> Sorted(Agents);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  bool bStepValid = Sorted.Num() == AgentCount && FixedStepIndex >= 0;
  TSet<int32> CurrentDeadlocks;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    const auto& Agent = Sorted[Index];
    if (Agent.AgentId == INDEX_NONE
      || (Index > 0 && Sorted[Index - 1].AgentId == Agent.AgentId))
    {
      bStepValid = false;
      continue;
    }
    if (Agent.Location.Y >= WallPassPlaneY)
      InOutProgress.WallPassedAgentIds.Add(Agent.AgentId);
    if (Agent.Location.Y >= CorridorExitPlaneY)
      InOutProgress.CorridorExitedAgentIds.Add(Agent.AgentId);
    const bool bComplete = Agent.Location.Y >= CompletionPlaneY;
    if (bComplete)
    {
      InOutProgress.CompletedAgentIds.Add(Agent.AgentId);
      InOutProgress.ConsecutiveLowSpeedStepsByAgentId.Add(Agent.AgentId, 0);
      if (!InOutProgress.CompletionStepByAgentId.Contains(Agent.AgentId))
        InOutProgress.CompletionStepByAgentId.Add(Agent.AgentId, FixedStepIndex);
    }
    else
    {
      int32& LowSteps =
        InOutProgress.ConsecutiveLowSpeedStepsByAgentId.FindOrAdd(Agent.AgentId);
      LowSteps = Agent.Velocity.Size2D() <= 10.0f ? LowSteps + 1 : 0;
      if (LowSteps >= 90) CurrentDeadlocks.Add(Agent.AgentId);
    }
    if (Agent.FlowStatus != ECrowdDemoFlowLocationStatus::Reachable)
      ++InOutProgress.UnreachableSampleCount;
  }
  const bool bGroupComplete = InOutProgress.CompletedAgentIds.Num() == AgentCount;
  if (bGroupComplete && InOutProgress.GroupCompletionStep == INDEX_NONE)
    InOutProgress.GroupCompletionStep = FixedStepIndex;
  if (bGroupComplete)
  {
    TSet<int32> CurrentSettled;
    for (const FCrowdDemoValidCorridorTransitStepAgent& Agent : Sorted)
    {
      int32& SettledSteps = InOutProgress.
        ConsecutivePostCompletionSettledStepsByAgentId.FindOrAdd(Agent.AgentId);
      SettledSteps = Agent.Velocity.Size2D() <= StableExitSpeedCmps
        ? SettledSteps + 1 : 0;
      if (SettledSteps >= StableExitSteps)
        CurrentSettled.Add(Agent.AgentId);
    }
    InOutProgress.FinalSettledAgentIds = MoveTemp(CurrentSettled);
    if (InOutProgress.FinalSettledAgentIds.Num() == AgentCount
      && InOutProgress.GroupSettledStep == INDEX_NONE)
      InOutProgress.GroupSettledStep = FixedStepIndex;
  }
  else
  {
    InOutProgress.FinalSettledAgentIds.Reset();
    InOutProgress.ConsecutivePostCompletionSettledStepsByAgentId.Reset();
  }
  InOutProgress.FinalDeadlockAgentIds = MoveTemp(CurrentDeadlocks);
  InOutProgress.LastFixedStepIndex = FixedStepIndex;
  InOutProgress.bValid = InOutProgress.bValid && bStepValid;

  uint32 Hash = HashOffset;
  Hash = Fold(Hash, InOutProgress.bValid ? 1 : 0);
  Hash = Fold(Hash, InOutProgress.LastFixedStepIndex);
  Hash = Fold(Hash, InOutProgress.UnreachableSampleCount);
  FoldSortedSet(Hash, InOutProgress.WallPassedAgentIds);
  FoldSortedSet(Hash, InOutProgress.CorridorExitedAgentIds);
  FoldSortedSet(Hash, InOutProgress.CompletedAgentIds);
  FoldSortedSet(Hash, InOutProgress.FinalSettledAgentIds);
  FoldSortedSet(Hash, InOutProgress.FinalDeadlockAgentIds);
  FoldSortedMap(Hash, InOutProgress.ConsecutiveLowSpeedStepsByAgentId);
  FoldSortedMap(Hash,
    InOutProgress.ConsecutivePostCompletionSettledStepsByAgentId);
  FoldSortedMap(Hash, InOutProgress.CompletionStepByAgentId);
  Hash = Fold(Hash, InOutProgress.GroupCompletionStep);
  Hash = Fold(Hash, InOutProgress.GroupSettledStep);
  InOutProgress.ProgressHash = Hash;
}
