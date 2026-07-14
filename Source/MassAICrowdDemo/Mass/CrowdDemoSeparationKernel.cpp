#include "Mass/CrowdDemoSeparationKernel.h"

namespace
{
  int64 MakeSeparationCellKey(const FIntPoint Cell)
  {
    return static_cast<int64>((static_cast<uint64>(static_cast<uint32>(Cell.X)) << 32)
      | static_cast<uint32>(Cell.Y));
  }
}

void FCrowdDemoSeparationKernel::Solve(
  const TConstArrayView<FCrowdDemoSeparationKernelAgent> Agents,
  const FCrowdDemoSeparationKernelSettings& Settings,
  TArray<FCrowdDemoSeparationKernelResult>& OutResults,
  FCrowdDemoSeparationKernelSummary& OutSummary)
{
  OutResults.Reset(Agents.Num());
  OutSummary = FCrowdDemoSeparationKernelSummary();
  if (Agents.IsEmpty())
  {
    return;
  }

  TArray<int32> SortedIndices;
  SortedIndices.Reserve(Agents.Num());
  float MaxSeparationRadiusCm = 1.0f;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    SortedIndices.Add(Index);
    MaxSeparationRadiusCm = FMath::Max(MaxSeparationRadiusCm, Agents[Index].SeparationRadiusCm);
  }
  SortedIndices.Sort([Agents](const int32 A, const int32 B)
  {
    return Agents[A].AgentId < Agents[B].AgentId;
  });

  const float CellSizeCm = FMath::Max(1.0f, Settings.CellSizeCm);
  const int32 NeighborCellRadius = FMath::Max(1, FMath::CeilToInt(MaxSeparationRadiusCm / CellSizeCm));
  auto MakeCell = [CellSizeCm](const FVector& Location)
  {
    return FIntPoint(
      FMath::FloorToInt(Location.X / CellSizeCm),
      FMath::FloorToInt(Location.Y / CellSizeCm));
  };

  TMap<int64, TArray<int32>> Grid;
  Grid.Reserve(Agents.Num());
  for (const int32 AgentIndex : SortedIndices)
  {
    Grid.FindOrAdd(MakeSeparationCellKey(MakeCell(Agents[AgentIndex].Location))).Add(AgentIndex);
  }
  OutSummary.GridCellCount = Grid.Num();
  for (TPair<int64, TArray<int32>>& Pair : Grid)
  {
    Pair.Value.Sort([Agents](const int32 A, const int32 B)
    {
      return Agents[A].AgentId < Agents[B].AgentId;
    });
  }

  TArray<FVector> PushVelocities;
  TArray<int32> NeighborCounts;
  TArray<int32> OverlapCounts;
  TArray<int32> SevereOverlapCounts;
  PushVelocities.SetNumZeroed(Agents.Num());
  NeighborCounts.SetNumZeroed(Agents.Num());
  OverlapCounts.SetNumZeroed(Agents.Num());
  SevereOverlapCounts.SetNumZeroed(Agents.Num());

  for (const int32 AgentIndex : SortedIndices)
  {
    const FCrowdDemoSeparationKernelAgent& Agent = Agents[AgentIndex];
    const FIntPoint AgentCell = MakeCell(Agent.Location);
    TArray<int64, TInlineAllocator<25>> NeighborKeys;
    for (int32 OffsetY = -NeighborCellRadius; OffsetY <= NeighborCellRadius; ++OffsetY)
    {
      for (int32 OffsetX = -NeighborCellRadius; OffsetX <= NeighborCellRadius; ++OffsetX)
      {
        NeighborKeys.Add(MakeSeparationCellKey(AgentCell + FIntPoint(OffsetX, OffsetY)));
      }
    }
    NeighborKeys.Sort();

    for (const int64 NeighborKey : NeighborKeys)
    {
      const TArray<int32>* NeighborIndices = Grid.Find(NeighborKey);
      if (!NeighborIndices)
      {
        continue;
      }

      for (const int32 OtherIndex : *NeighborIndices)
      {
        const FCrowdDemoSeparationKernelAgent& Other = Agents[OtherIndex];
        if (Other.AgentId <= Agent.AgentId)
        {
          continue;
        }

        const float SoftRadiusCm = FMath::Max(1.0f, (Agent.SeparationRadiusCm + Other.SeparationRadiusCm) * 0.5f);
        const float HardRadiusCm = FMath::Clamp(
          (Agent.ContactRadiusCm + Other.ContactRadiusCm) * 0.5f,
          1.0f,
          SoftRadiusCm);
        FVector Delta = Agent.Location - Other.Location;
        float DistanceCm = Delta.Size2D();
        if (DistanceCm >= SoftRadiusCm)
        {
          continue;
        }

        ++NeighborCounts[AgentIndex];
        ++NeighborCounts[OtherIndex];
        ++OverlapCounts[AgentIndex];
        ++OverlapCounts[OtherIndex];
        ++OutSummary.OverlapPairCount;
        if (DistanceCm <= KINDA_SMALL_NUMBER)
        {
          const uint32 PairHash = HashCombineFast(GetTypeHash(Agent.AgentId), GetTypeHash(Other.AgentId));
          const float AngleRadians = static_cast<float>(PairHash & 7u) * (PI / 4.0f);
          Delta = FVector(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
          DistanceCm = 0.0f;
        }
        else
        {
          Delta /= DistanceCm;
        }

        const float SoftAlpha = (SoftRadiusCm - DistanceCm) / SoftRadiusCm;
        float PushSpeedCmPerSecond = Settings.SoftPushSpeedCmPerSecond * SoftAlpha;
        if (DistanceCm < HardRadiusCm)
        {
          const float HardAlpha = (HardRadiusCm - DistanceCm) / HardRadiusCm;
          PushSpeedCmPerSecond += Settings.HardPushSpeedCmPerSecond * HardAlpha;
          ++SevereOverlapCounts[AgentIndex];
          ++SevereOverlapCounts[OtherIndex];
          ++OutSummary.SevereOverlapPairCount;
        }

        const FVector PairPushVelocity = Delta * PushSpeedCmPerSecond * 0.5f;
        PushVelocities[AgentIndex] += PairPushVelocity;
        PushVelocities[OtherIndex] -= PairPushVelocity;
      }
    }
  }

  for (const int32 AgentIndex : SortedIndices)
  {
    FCrowdDemoSeparationKernelResult& Result = OutResults.AddDefaulted_GetRef();
    Result.AgentId = Agents[AgentIndex].AgentId;
    Result.PushVelocity = PushVelocities[AgentIndex];
    Result.NeighborCount = NeighborCounts[AgentIndex];
    Result.OverlapCount = OverlapCounts[AgentIndex];
    Result.SevereOverlapCount = SevereOverlapCounts[AgentIndex];
    Result.bHardSeparation = Result.SevereOverlapCount > 0;
    if (!Result.PushVelocity.IsNearlyZero(0.001f))
    {
      ++OutSummary.AppliedAgentCount;
    }
  }
}
