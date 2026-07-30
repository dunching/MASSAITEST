#include "MassCrowdFacingWork.h"

#include "Async/ParallelFor.h"

#define FnvPrime FacingWork_FnvPrime
#define Fold FacingWork_Fold

namespace
{
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }
}

FCrowdMassFacingWorkOutput FCrowdMassFacingWork::Resolve(
  const FCrowdMassFacingWorkInput& Input)
{
  FCrowdMassFacingWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Agents.IsEmpty())
    return Output;
  FCrowdFacingKernel::Resolve(Input.Agents, Input.Settings, Output.Summary);
  if (!Output.Summary.bValid
    || Output.Summary.Results.Num() != Input.Agents.Num())
    return Output;
  uint32 Hash = Fold(2166136261u, 1u);
  Hash = Fold(Hash, static_cast<uint32>(Input.FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint32>(Input.PlanRevision));
  Hash = Fold(Hash, Output.Summary.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}

FCrowdMassFacingWorkOutput FCrowdMassFacingWork::ResolveSharded(
  const FCrowdMassFacingWorkInput& Input,
  const int32 ShardSize,
  const bool bReverseDispatchOrder)
{
  FCrowdMassFacingWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Agents.IsEmpty() || ShardSize <= 0)
    return Output;
  TArray<FCrowdFacingInput> SortedAgents = Input.Agents;
  SortedAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  const int32 ShardCount =
    FMath::DivideAndRoundUp(SortedAgents.Num(), ShardSize);
  TArray<FCrowdMassFacingWorkOutput> ShardOutputs;
  ShardOutputs.SetNum(ShardCount);
  ParallelFor(ShardCount, [&](const int32 DispatchIndex)
  {
    const int32 ShardIndex = bReverseDispatchOrder
      ? ShardCount - DispatchIndex - 1
      : DispatchIndex;
    const int32 Begin = ShardIndex * ShardSize;
    const int32 Count = FMath::Min(
      ShardSize, SortedAgents.Num() - Begin);
    FCrowdMassFacingWorkInput ShardInput;
    ShardInput.FixedStepIndex = Input.FixedStepIndex;
    ShardInput.PlanRevision = Input.PlanRevision;
    ShardInput.Settings = Input.Settings;
    ShardInput.Agents.Append(
      SortedAgents.GetData() + Begin, Count);
    ShardOutputs[ShardIndex] = Resolve(ShardInput);
  });

  for (const FCrowdMassFacingWorkOutput& Shard : ShardOutputs)
  {
    if (!Shard.bCompleted)
      return Output;
    Output.Summary.Results.Append(Shard.Summary.Results);
    Output.Summary.TargetFacingAgentCount +=
      Shard.Summary.TargetFacingAgentCount;
    Output.Summary.AutonomousFacingAgentCount +=
      Shard.Summary.AutonomousFacingAgentCount;
    Output.Summary.HeldYawAgentCount +=
      Shard.Summary.HeldYawAgentCount;
    Output.Summary.MaximumAppliedYawDeltaDegrees = FMath::Max(
      Output.Summary.MaximumAppliedYawDeltaDegrees,
      Shard.Summary.MaximumAppliedYawDeltaDegrees);
  }
  Output.Summary.Results.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  uint32 SummaryHash = Fold(2166136261u, 1u);
  for (const FCrowdFacingResult& Result : Output.Summary.Results)
  {
    SummaryHash = Fold(
      SummaryHash, static_cast<uint32>(Result.AgentId));
    SummaryHash = Fold(
      SummaryHash,
      static_cast<uint32>(FMath::RoundToInt(
        Result.DesiredYawDegrees
          / Input.Settings.AngleQuantumDegrees)));
    SummaryHash = Fold(
      SummaryHash,
      static_cast<uint32>(FMath::RoundToInt(
        Result.ResolvedYawDegrees
          / Input.Settings.AngleQuantumDegrees)));
    SummaryHash = Fold(
      SummaryHash, Result.bFacingTarget ? 1u : 0u);
    SummaryHash = Fold(
      SummaryHash, Result.bHeldCurrentYaw ? 1u : 0u);
  }
  Output.Summary.StableHash = SummaryHash;
  Output.Summary.bValid =
    Output.Summary.Results.Num() == SortedAgents.Num();
  uint32 Hash = Fold(2166136261u, 1u);
  Hash = Fold(Hash, static_cast<uint32>(Input.FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint32>(Input.PlanRevision));
  Hash = Fold(Hash, Output.Summary.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = Output.Summary.bValid;
  return Output;
}

#undef Fold
#undef FnvPrime
