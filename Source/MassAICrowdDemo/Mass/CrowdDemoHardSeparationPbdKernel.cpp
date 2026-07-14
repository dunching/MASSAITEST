#include "Mass/CrowdDemoHardSeparationPbdKernel.h"

namespace
{
  FVector MakeStablePairDirection(const int32 MinAgentId, const int32 MaxAgentId)
  {
    uint32 Hash = 2166136261u;
    Hash = (Hash ^ static_cast<uint32>(MinAgentId)) * 16777619u;
    Hash = (Hash ^ static_cast<uint32>(MaxAgentId)) * 16777619u;
    const float AngleRadians = static_cast<float>(Hash & 7u) * (PI / 4.0f);
    return FVector(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0f);
  }
}

void FCrowdDemoHardSeparationPbdKernel::BuildOverlapPairs(
  const TConstArrayView<FCrowdDemoHardSeparationPbdAgent> Agents,
  const float DistanceThresholdCm,
  TArray<FCrowdDemoHardSeparationPbdPair>& OutPairs)
{
  TArray<FCrowdDemoHardSeparationPbdAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const FCrowdDemoHardSeparationPbdAgent& A, const FCrowdDemoHardSeparationPbdAgent& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutPairs.Reset();
  const float CellSize = FMath::Max(1.0f, DistanceThresholdCm);
  TMap<FIntPoint, TArray<int32>> Grid;
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    const FVector& Location = SortedAgents[Index].Location;
    Grid.FindOrAdd(FIntPoint(
      FMath::FloorToInt(Location.X / CellSize),
      FMath::FloorToInt(Location.Y / CellSize))).Add(Index);
  }
  for (TPair<FIntPoint, TArray<int32>>& Cell : Grid)
  {
    Cell.Value.Sort([&](const int32 A, const int32 B)
    {
      return SortedAgents[A].AgentId < SortedAgents[B].AgentId;
    });
  }
  const float ThresholdSquared = FMath::Square(DistanceThresholdCm);
  for (int32 A = 0; A < SortedAgents.Num(); ++A)
  {
    const FVector& Location = SortedAgents[A].Location;
    const FIntPoint Base(
      FMath::FloorToInt(Location.X / CellSize),
      FMath::FloorToInt(Location.Y / CellSize));
    for (int32 DY = -1; DY <= 1; ++DY)
    {
      for (int32 DX = -1; DX <= 1; ++DX)
      {
        const TArray<int32>* Cell = Grid.Find(Base + FIntPoint(DX, DY));
        if (!Cell) continue;
        for (const int32 B : *Cell)
        {
          if (B <= A) continue;
          if (FVector::DistSquared2D(Location, SortedAgents[B].Location) < ThresholdSquared)
          {
            FCrowdDemoHardSeparationPbdPair& Pair = OutPairs.AddDefaulted_GetRef();
            Pair.MinAgentId = SortedAgents[A].AgentId;
            Pair.MaxAgentId = SortedAgents[B].AgentId;
            Pair.MinAgentIndex = A;
            Pair.MaxAgentIndex = B;
          }
        }
      }
    }
  }
  OutPairs.Sort([](const FCrowdDemoHardSeparationPbdPair& A, const FCrowdDemoHardSeparationPbdPair& B)
  {
    return A.MinAgentId < B.MinAgentId
      || (A.MinAgentId == B.MinAgentId && A.MaxAgentId < B.MaxAgentId);
  });
}

void FCrowdDemoHardSeparationPbdKernel::Solve(
  const TConstArrayView<FCrowdDemoHardSeparationPbdAgent> Agents,
  const FCrowdDemoHardSeparationPbdSettings& Settings,
  TArray<FCrowdDemoHardSeparationPbdPair>& OutPairs,
  TArray<FCrowdDemoHardSeparationPbdResult>& OutResults,
  FCrowdDemoHardSeparationPbdSummary& OutSummary,
  TArray<FCrowdDemoHardSeparationPbdIterationDiagnostic>* OutIterationDiagnostics)
{
  OutPairs.Reset();
  OutResults.Reset();
  OutSummary = FCrowdDemoHardSeparationPbdSummary();
  if (OutIterationDiagnostics)
  {
    OutIterationDiagnostics->Reset();
  }
  if (Agents.IsEmpty())
  {
    return;
  }

  TArray<FCrowdDemoHardSeparationPbdAgent> SortedAgents;
  SortedAgents.Append(Agents.GetData(), Agents.Num());
  SortedAgents.Sort([](
    const FCrowdDemoHardSeparationPbdAgent& A,
    const FCrowdDemoHardSeparationPbdAgent& B)
  {
    return A.AgentId < B.AgentId;
  });

  const int32 IterationCount = FMath::Max(1, Settings.IterationCount);
  const float MaxPairCorrectionCm = FMath::Max(0.0f, Settings.MaxPairCorrectionPerIterationCm);
  const float CandidateMarginCm = MaxPairCorrectionCm * static_cast<float>(IterationCount) * 4.0f;
  float MaxRadiusCm = 1.0f;
  for (const FCrowdDemoHardSeparationPbdAgent& Agent : SortedAgents)
  {
    MaxRadiusCm = FMath::Max(MaxRadiusCm, Agent.RadiusCm);
  }
  const float GridCellSizeCm = FMath::Max(1.0f, MaxRadiusCm * 2.0f + CandidateMarginCm);
  TMap<FIntPoint, TArray<int32>> CellAgentIndices;
  for (int32 A = 0; A < SortedAgents.Num(); ++A)
  {
    const FVector& Location = SortedAgents[A].Location;
    const FIntPoint Cell(
      FMath::FloorToInt(Location.X / GridCellSizeCm),
      FMath::FloorToInt(Location.Y / GridCellSizeCm));
    CellAgentIndices.FindOrAdd(Cell).Add(A);
  }
  for (int32 A = 0; A < SortedAgents.Num(); ++A)
  {
    const FVector& Location = SortedAgents[A].Location;
    const FIntPoint Cell(
      FMath::FloorToInt(Location.X / GridCellSizeCm),
      FMath::FloorToInt(Location.Y / GridCellSizeCm));
    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
    {
      for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
      {
        const TArray<int32>* NeighborIndices = CellAgentIndices.Find(
          Cell + FIntPoint(OffsetX, OffsetY));
        if (!NeighborIndices)
        {
          continue;
        }
        for (const int32 B : *NeighborIndices)
        {
          if (B <= A)
          {
            continue;
          }
          const float ContactDistanceCm = FMath::Max(
            1.0f,
            SortedAgents[A].RadiusCm + SortedAgents[B].RadiusCm);
          const float CandidateDistanceCm = ContactDistanceCm + CandidateMarginCm;
          if (FVector::DistSquared2D(SortedAgents[A].Location, SortedAgents[B].Location)
            > FMath::Square(CandidateDistanceCm))
          {
            continue;
          }
          FCrowdDemoHardSeparationPbdPair& Pair = OutPairs.AddDefaulted_GetRef();
          Pair.MinAgentId = SortedAgents[A].AgentId;
          Pair.MaxAgentId = SortedAgents[B].AgentId;
          Pair.MinAgentIndex = A;
          Pair.MaxAgentIndex = B;
        }
      }
    }
  }
  OutPairs.Sort([](
    const FCrowdDemoHardSeparationPbdPair& A,
    const FCrowdDemoHardSeparationPbdPair& B)
  {
    return A.MinAgentId < B.MinAgentId
      || (A.MinAgentId == B.MinAgentId && A.MaxAgentId < B.MaxAgentId);
  });

  TArray<FVector> Positions;
  TArray<FVector> TotalCorrections;
  TArray<int32> CorrectedPairCounts;
  TArray<uint8> CorrectedPairs;
  Positions.Reserve(SortedAgents.Num());
  TotalCorrections.SetNumZeroed(SortedAgents.Num());
  CorrectedPairCounts.SetNumZeroed(SortedAgents.Num());
  CorrectedPairs.SetNumZeroed(OutPairs.Num());
  for (const FCrowdDemoHardSeparationPbdAgent& Agent : SortedAgents)
  {
    Positions.Add(Agent.Location);
  }

  for (int32 Iteration = 0; Iteration < IterationCount; ++Iteration)
  {
    FCrowdDemoHardSeparationPbdIterationDiagnostic* IterationDiagnostic = nullptr;
    if (OutIterationDiagnostics)
    {
      IterationDiagnostic = &OutIterationDiagnostics->AddDefaulted_GetRef();
      IterationDiagnostic->IterationIndex = Iteration;
    }
    for (int32 PairIndex = 0; PairIndex < OutPairs.Num(); ++PairIndex)
    {
      const FCrowdDemoHardSeparationPbdPair& Pair = OutPairs[PairIndex];
      FVector Delta = Positions[Pair.MinAgentIndex] - Positions[Pair.MaxAgentIndex];
      float DistanceCm = Delta.Size2D();
      const float ContactDistanceCm = FMath::Max(
        1.0f,
        SortedAgents[Pair.MinAgentIndex].RadiusCm + SortedAgents[Pair.MaxAgentIndex].RadiusCm);
      if (DistanceCm >= ContactDistanceCm)
      {
        continue;
      }
      if (DistanceCm <= KINDA_SMALL_NUMBER)
      {
        Delta = MakeStablePairDirection(Pair.MinAgentId, Pair.MaxAgentId);
        DistanceCm = 0.0f;
      }
      else
      {
        Delta /= DistanceCm;
      }
      const float PairCorrectionCm = FMath::Min(
        ContactDistanceCm - DistanceCm,
        MaxPairCorrectionCm);
      const float EqualMassHalfCorrectionCm = PairCorrectionCm * 0.5f;
      if (EqualMassHalfCorrectionCm <= KINDA_SMALL_NUMBER)
      {
        continue;
      }
      const FVector EqualMassHalfCorrection = Delta * EqualMassHalfCorrectionCm;
      Positions[Pair.MinAgentIndex] += EqualMassHalfCorrection;
      Positions[Pair.MaxAgentIndex] -= EqualMassHalfCorrection;
      TotalCorrections[Pair.MinAgentIndex] += EqualMassHalfCorrection;
      TotalCorrections[Pair.MaxAgentIndex] -= EqualMassHalfCorrection;
      OutSummary.MaxPairCorrectionCm = FMath::Max(OutSummary.MaxPairCorrectionCm, PairCorrectionCm);
      if (IterationDiagnostic)
      {
        FCrowdDemoHardSeparationPbdIterationPairCorrection& Correction =
          IterationDiagnostic->PairCorrections.AddDefaulted_GetRef();
        Correction.MinAgentId = Pair.MinAgentId;
        Correction.MaxAgentId = Pair.MaxAgentId;
        Correction.PairCorrectionCm = PairCorrectionCm;
        Correction.MinAgentCorrection = EqualMassHalfCorrection;
        Correction.MaxAgentCorrection = -EqualMassHalfCorrection;
      }
      if (CorrectedPairs[PairIndex] == 0)
      {
        CorrectedPairs[PairIndex] = 1;
        ++CorrectedPairCounts[Pair.MinAgentIndex];
        ++CorrectedPairCounts[Pair.MaxAgentIndex];
      }
    }
    if (IterationDiagnostic)
    {
      uint32 Hash = 2166136261u;
      const auto Fold = [](uint32 H, const int32 V)
      {
        return (H ^ static_cast<uint32>(V)) * 16777619u;
      };
      Hash = Fold(Hash, Iteration);
      IterationDiagnostic->AgentResults.Reserve(SortedAgents.Num());
      for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
      {
        FCrowdDemoHardSeparationPbdResult& Result =
          IterationDiagnostic->AgentResults.AddDefaulted_GetRef();
        Result.AgentId = SortedAgents[AgentIndex].AgentId;
        Result.CorrectedLocation = Positions[AgentIndex];
        Result.Correction = TotalCorrections[AgentIndex];
        Result.CorrectedPairCount = CorrectedPairCounts[AgentIndex];
        Hash = Fold(Hash, Result.AgentId);
        Hash = Fold(Hash, FMath::RoundToInt(Result.CorrectedLocation.X));
        Hash = Fold(Hash, FMath::RoundToInt(Result.CorrectedLocation.Y));
      }
      for (const FCrowdDemoHardSeparationPbdIterationPairCorrection& Correction
        : IterationDiagnostic->PairCorrections)
      {
        Hash = Fold(Hash, Correction.MinAgentId);
        Hash = Fold(Hash, Correction.MaxAgentId);
        Hash = Fold(Hash, FMath::RoundToInt(Correction.PairCorrectionCm * 1000.0f));
      }
      IterationDiagnostic->StableHash = Hash;
    }
  }

  OutSummary.CandidatePairCount = OutPairs.Num();
  OutSummary.IterationCount = IterationCount;
  for (const uint8 bCorrected : CorrectedPairs)
  {
    OutSummary.CorrectedPairCount += bCorrected != 0 ? 1 : 0;
  }
  OutResults.Reserve(SortedAgents.Num());
  for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    FCrowdDemoHardSeparationPbdResult& Result = OutResults.AddDefaulted_GetRef();
    Result.AgentId = SortedAgents[AgentIndex].AgentId;
    Result.CorrectedLocation = Positions[AgentIndex];
    Result.Correction = TotalCorrections[AgentIndex];
    OutSummary.MaxAgentTotalCorrectionCm = FMath::Max(
      OutSummary.MaxAgentTotalCorrectionCm,
      Result.Correction.Size2D());
    Result.CorrectedPairCount = CorrectedPairCounts[AgentIndex];
    if (Result.CorrectedPairCount > 0)
    {
      ++OutSummary.CorrectedAgentCount;
    }
  }
}
