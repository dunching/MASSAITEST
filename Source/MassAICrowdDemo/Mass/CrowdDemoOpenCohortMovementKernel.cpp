#include "Mass/CrowdDemoOpenCohortMovementKernel.h"

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

  void FoldSortedIds(uint32& Hash, const TSet<int32>& Ids)
  {
    TArray<int32> Sorted = Ids.Array();
    Sorted.Sort();
    Hash = Fold(Hash, Sorted.Num());
    for (const int32 Id : Sorted) Hash = Fold(Hash, Id);
  }
}

bool FCrowdDemoOpenCohortMovementKernel::ShouldEnablePolarHandoff(
  const ECrowdDemoSoftPressureTestCase TestCase)
{
  return TestCase == ECrowdDemoSoftPressureTestCase::OpenCohortMovement;
}

FCrowdDemoSharedFlowFieldConfig FCrowdDemoOpenCohortMovementKernel::MakeOpenFlowConfig()
{
  FCrowdDemoSharedFlowFieldConfig Config =
    FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(1);
  Config.ObstacleSpecs.Reset();
  Config.ConnectivityContractVersion = 2;
  return Config;
}

FCrowdDemoOpenCohortMovementLayout FCrowdDemoOpenCohortMovementKernel::BuildLayout(
  const TConstArrayView<FCrowdDemoOpenCohortMovementLayoutInput> Inputs,
  const float PhysicalRadiusCm,
  const float HardSafetyGapCm,
  const int32 FormationColumns,
  const float FormationSpacingCm,
  const FVector& SpawnOrigin)
{
  FCrowdDemoOpenCohortMovementLayout Out;
  TArray<FCrowdDemoOpenCohortMovementLayoutInput> Sorted(Inputs);
  Sorted.Sort([](const auto& A, const auto& B)
  {
    if (A.FormationIndex != B.FormationIndex)
      return A.FormationIndex < B.FormationIndex;
    return A.AgentId < B.AgentId;
  });
  if (Sorted.Num() != 20 || FormationColumns <= 0
    || FormationSpacingCm + KINDA_SMALL_NUMBER
      < PhysicalRadiusCm * 2.0f + HardSafetyGapCm)
    return Out;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
    if (Sorted[Index].AgentId == INDEX_NONE || Sorted[Index].FormationIndex != Index
      || (Index > 0 && Sorted[Index - 1].AgentId == Sorted[Index].AgentId))
      return Out;

  const int32 Rows = FMath::CeilToInt(
    static_cast<float>(Sorted.Num()) / static_cast<float>(FormationColumns));
  const FCrowdDemoSharedFlowFieldConfig Config = MakeOpenFlowConfig();
  for (const auto& Input : Sorted)
  {
    FCrowdDemoOpenCohortMovementLayoutAgent& Agent = Out.Agents.AddDefaulted_GetRef();
    Agent.AgentId = Input.AgentId;
    Agent.FormationIndex = Input.FormationIndex;
    Agent.SpawnLocation = SpawnOrigin + FVector(
      (Input.FormationIndex % FormationColumns - 0.5f * (FormationColumns - 1))
        * FormationSpacingCm,
      (Input.FormationIndex / FormationColumns - 0.5f * (Rows - 1))
        * FormationSpacingCm,
      0.0f);
    const float Clearance = PhysicalRadiusCm + HardSafetyGapCm;
    if (Agent.SpawnLocation.X < FVector(Config.BoundsMin).X + Clearance
      || Agent.SpawnLocation.X > FVector(Config.BoundsMax).X - Clearance
      || Agent.SpawnLocation.Y < FVector(Config.BoundsMin).Y + Clearance
      || Agent.SpawnLocation.Y > FVector(Config.BoundsMax).Y - Clearance)
      return Out;
  }

  Out.LayoutHash = HashOffset;
  for (const auto& Agent : Out.Agents)
  {
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.AgentId);
    Out.LayoutHash = Fold(Out.LayoutHash, Agent.FormationIndex);
    Out.LayoutHash = FoldVector(Out.LayoutHash, Agent.SpawnLocation);
  }
  Out.bValid = true;
  return Out;
}

void FCrowdDemoOpenCohortMovementKernel::UpdateProgress(
  const TConstArrayView<FCrowdDemoTargetRegionGuidanceResult> Guidance,
  const int32 ExpectedAgentCount,
  const int32 FixedStepIndex,
  FCrowdDemoOpenCohortMovementProgress& InOutProgress)
{
  TArray<FCrowdDemoTargetRegionGuidanceResult> Sorted(Guidance);
  Sorted.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TSet<int32> CurrentTerminal;
  bool bValid = ExpectedAgentCount > 0 && Sorted.Num() == ExpectedAgentCount;
  for (int32 Index = 0; Index < Sorted.Num(); ++Index)
  {
    const auto& Result = Sorted[Index];
    if (Result.AgentId == INDEX_NONE
      || (Index > 0 && Sorted[Index - 1].AgentId == Result.AgentId))
    {
      bValid = false;
      continue;
    }
    if (Result.Mode != ECrowdDemoTargetRegionGuidanceMode::FarFlow)
      InOutProgress.FlowApproachEnteredAgentIds.Add(Result.AgentId);
    if (Result.Mode == ECrowdDemoTargetRegionGuidanceMode::Transport
      || Result.Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle)
      InOutProgress.TransportHandoffAgentIds.Add(Result.AgentId);
    if (Result.Mode == ECrowdDemoTargetRegionGuidanceMode::TerminalSettle)
      CurrentTerminal.Add(Result.AgentId);
  }
  InOutProgress.TerminalSettledAgentIds = MoveTemp(CurrentTerminal);
  if (InOutProgress.TerminalSettledStep == INDEX_NONE
    && InOutProgress.TerminalSettledAgentIds.Num() == ExpectedAgentCount)
    InOutProgress.TerminalSettledStep = FixedStepIndex;
  InOutProgress.bValid = InOutProgress.bValid && bValid;
  uint32 Hash = HashOffset;
  Hash = Fold(Hash, InOutProgress.bValid ? 1 : 0);
  Hash = Fold(Hash, ExpectedAgentCount);
  FoldSortedIds(Hash, InOutProgress.FlowApproachEnteredAgentIds);
  FoldSortedIds(Hash, InOutProgress.TransportHandoffAgentIds);
  FoldSortedIds(Hash, InOutProgress.TerminalSettledAgentIds);
  Hash = Fold(Hash, InOutProgress.TerminalSettledStep);
  InOutProgress.ProgressHash = Hash;
}
