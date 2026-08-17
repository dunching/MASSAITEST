#include "CrowdParticleConstraintKernel.h"
#include "Algo/Unique.h"

#include "CrowdSharedFlowFieldKernel.h"

namespace
{
  constexpr float ConstraintEpsilonCm = 0.01f;

  FVector Flatten2D(const FVector& Value)
  {
    return FVector(Value.X, Value.Y, 0.0f);
  }

  float Dot2D(const FVector& A, const FVector& B)
  {
    return A.X * B.X + A.Y * B.Y;
  }

  uint32 FoldHash(uint32 Hash, const int32 Value)
  {
    return (Hash ^ static_cast<uint32>(Value)) * 16777619u;
  }

  int64 MakeCellKey(const FIntPoint Cell)
  {
    return static_cast<int64>((static_cast<uint64>(static_cast<uint32>(Cell.Y)) << 32)
      | static_cast<uint32>(Cell.X));
  }

  FVector MakeStablePairDirection(const int32 MinAgentId, const int32 MaxAgentId)
  {
    uint32 Hash = 2166136261u;
    Hash = FoldHash(Hash, MinAgentId);
    Hash = FoldHash(Hash, MaxAgentId);
    const int32 DirectionIndex = static_cast<int32>(Hash & 7u);
    const float Angle = static_cast<float>(DirectionIndex) * (PI / 4.0f);
    return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
  }

  float PairHardDistance(
    const FCrowdParticleConstraintAgent& A,
    const FCrowdParticleConstraintAgent& B)
  {
    return FMath::Max(1.0f, A.PhysicalRadiusCm + B.PhysicalRadiusCm
      + FMath::Max(A.HardSafetyGapCm, B.HardSafetyGapCm));
  }

  float PairSoftDistance(
    const FCrowdParticleConstraintAgent& A,
    const FCrowdParticleConstraintAgent& B)
  {
    return PairHardDistance(A, B)
      + FMath::Max(0.0f, A.SoftMarginCm)
      + FMath::Max(0.0f, B.SoftMarginCm);
  }

  float AgentEnvironmentHardDistance(
    const FCrowdParticleConstraintAgent& Agent)
  {
    return FMath::Max(
      FMath::Max(0.0f, Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm),
      FMath::Max(0.0f, Agent.EnvironmentHardClearanceCm));
  }

  FVector ContactFaceNormal(const ECrowdParticleEnvironmentFace Face)
  {
    switch (Face)
    {
    case ECrowdParticleEnvironmentFace::MinX: return FVector(-1.0f, 0.0f, 0.0f);
    case ECrowdParticleEnvironmentFace::MaxX: return FVector(1.0f, 0.0f, 0.0f);
    case ECrowdParticleEnvironmentFace::MinY: return FVector(0.0f, -1.0f, 0.0f);
    case ECrowdParticleEnvironmentFace::MaxY: return FVector(0.0f, 1.0f, 0.0f);
    default: return FVector::ForwardVector;
    }
  }

  bool IsInsideBox2D(const FVector& Position, const FVector& Min, const FVector& Max)
  {
    return Position.X >= Min.X && Position.X <= Max.X
      && Position.Y >= Min.Y && Position.Y <= Max.Y;
  }

  float FaceEscapeDistance(
    const FVector& Position,
    const FVector& Min,
    const FVector& Max,
    const ECrowdParticleEnvironmentFace Face)
  {
    switch (Face)
    {
    case ECrowdParticleEnvironmentFace::MinX: return Position.X - Min.X;
    case ECrowdParticleEnvironmentFace::MaxX: return Max.X - Position.X;
    case ECrowdParticleEnvironmentFace::MinY: return Position.Y - Min.Y;
    case ECrowdParticleEnvironmentFace::MaxY: return Max.Y - Position.Y;
    default: return TNumericLimits<float>::Max();
    }
  }

  ECrowdParticleEnvironmentFace SelectEscapeFace(
    const FVector& Position,
    const FVector& Min,
    const FVector& Max)
  {
    ECrowdParticleEnvironmentFace BestFace = ECrowdParticleEnvironmentFace::MinX;
    float BestDistance = FaceEscapeDistance(Position, Min, Max, BestFace);
    for (int32 FaceValue = 1; FaceValue <= 3; ++FaceValue)
    {
      const auto Face = static_cast<ECrowdParticleEnvironmentFace>(FaceValue);
      const float Distance = FaceEscapeDistance(Position, Min, Max, Face);
      if (Distance < BestDistance - KINDA_SMALL_NUMBER)
      {
        BestDistance = Distance;
        BestFace = Face;
      }
    }
    return BestFace;
  }

  FVector ClosestPointOnFace(
    const FVector& Position,
    const FVector& Min,
    const FVector& Max,
    const ECrowdParticleEnvironmentFace Face)
  {
    FVector Result = Position;
    switch (Face)
    {
    case ECrowdParticleEnvironmentFace::MinX:
      Result.X = Min.X;
      Result.Y = FMath::Clamp(Position.Y, Min.Y, Max.Y);
      break;
    case ECrowdParticleEnvironmentFace::MaxX:
      Result.X = Max.X;
      Result.Y = FMath::Clamp(Position.Y, Min.Y, Max.Y);
      break;
    case ECrowdParticleEnvironmentFace::MinY:
      Result.X = FMath::Clamp(Position.X, Min.X, Max.X);
      Result.Y = Min.Y;
      break;
    case ECrowdParticleEnvironmentFace::MaxY:
      Result.X = FMath::Clamp(Position.X, Min.X, Max.X);
      Result.Y = Max.Y;
      break;
    }
    return Result;
  }

  float FaceThreshold(
    const FVector& Min,
    const FVector& Max,
    const ECrowdParticleEnvironmentFace Face,
    const float ExtraClearance = 0.0f)
  {
    const FVector Normal = ContactFaceNormal(Face);
    const FVector Point = ClosestPointOnFace(FVector::ZeroVector, Min, Max, Face)
      + Normal * ExtraClearance;
    return FVector::DotProduct(Point, Normal);
  }

  bool SegmentBoxEntryFace(
    const FVector& Start,
    const FVector& End,
    const FVector& Min,
    const FVector& Max,
    float& OutEntryTime,
    ECrowdParticleEnvironmentFace& OutFace)
  {
    if (IsInsideBox2D(Start, Min, Max))
    {
      OutEntryTime = 0.0f;
      OutFace = SelectEscapeFace(Start, Min, Max);
      return true;
    }
    const FVector Delta = End - Start;
    float Entry = 0.0f;
    float Exit = 1.0f;
    ECrowdParticleEnvironmentFace EntryFace = ECrowdParticleEnvironmentFace::MinX;
    bool bHasEntryFace = false;
    for (int32 Axis = 0; Axis < 2; ++Axis)
    {
      const float Origin = Axis == 0 ? Start.X : Start.Y;
      const float Direction = Axis == 0 ? Delta.X : Delta.Y;
      const float AxisMin = Axis == 0 ? Min.X : Min.Y;
      const float AxisMax = Axis == 0 ? Max.X : Max.Y;
      if (FMath::IsNearlyZero(Direction))
      {
        if (Origin < AxisMin || Origin > AxisMax) return false;
        continue;
      }
      float NearTime = (AxisMin - Origin) / Direction;
      float FarTime = (AxisMax - Origin) / Direction;
      ECrowdParticleEnvironmentFace NearFace = Axis == 0
        ? ECrowdParticleEnvironmentFace::MinX : ECrowdParticleEnvironmentFace::MinY;
      if (NearTime > FarTime)
      {
        Swap(NearTime, FarTime);
        NearFace = Axis == 0
          ? ECrowdParticleEnvironmentFace::MaxX : ECrowdParticleEnvironmentFace::MaxY;
      }
      if (NearTime > Entry + KINDA_SMALL_NUMBER
        || (FMath::IsNearlyEqual(NearTime, Entry)
          && (!bHasEntryFace || static_cast<uint8>(NearFace) < static_cast<uint8>(EntryFace))))
      {
        Entry = NearTime;
        EntryFace = NearFace;
        bHasEntryFace = true;
      }
      Exit = FMath::Min(Exit, FarTime);
      if (Entry > Exit) return false;
    }
    if (!bHasEntryFace || Exit < 0.0f || Entry > 1.0f) return false;
    OutEntryTime = FMath::Clamp(Entry, 0.0f, 1.0f);
    OutFace = EntryFace;
    return true;
  }

  int32 QuantizeNormalQ15(const float Value)
  {
    return FMath::Clamp(FMath::RoundToInt(Value * 32767.0f), -32767, 32767);
  }

  bool SameDualKey(
    const FCrowdParticleHardDualState& State,
    const FCrowdParticleHardConstraint& Constraint)
  {
    return State.Kind == Constraint.Kind
      && State.MinAgentId == Constraint.MinAgentId
      && State.MaxAgentId == Constraint.MaxAgentId
      && State.EnvironmentId == Constraint.EnvironmentId
      && State.Face == Constraint.Face
      && State.NormalXQ15 == QuantizeNormalQ15(Constraint.Normal.X)
      && State.NormalYQ15 == QuantizeNormalQ15(Constraint.Normal.Y);
  }

  bool ConstraintLess(
    const FCrowdParticleHardConstraint& A,
    const FCrowdParticleHardConstraint& B)
  {
    const auto Rank = [](const ECrowdParticleHardConstraintKind Kind)
    {
      switch (Kind)
      {
      case ECrowdParticleHardConstraintKind::FlowBounds: return 0;
      case ECrowdParticleHardConstraintKind::ObstacleSwept: return 1;
      case ECrowdParticleHardConstraintKind::ObstacleEndpoint: return 2;
      case ECrowdParticleHardConstraintKind::PairSwept: return 3;
      case ECrowdParticleHardConstraintKind::PairEndpoint: return 4;
      default: return 5;
      }
    };
    const int32 ARank = Rank(A.Kind);
    const int32 BRank = Rank(B.Kind);
    if (ARank != BRank) return ARank < BRank;
    if (A.MinAgentId != B.MinAgentId) return A.MinAgentId > B.MinAgentId;
    if (A.MaxAgentId != B.MaxAgentId) return A.MaxAgentId > B.MaxAgentId;
    if (A.EnvironmentId != B.EnvironmentId) return A.EnvironmentId > B.EnvironmentId;
    if (A.Face != B.Face) return static_cast<uint8>(A.Face) > static_cast<uint8>(B.Face);
    const int32 AX = QuantizeNormalQ15(A.Normal.X);
    const int32 BX = QuantizeNormalQ15(B.Normal.X);
    if (AX != BX) return AX > BX;
    return QuantizeNormalQ15(A.Normal.Y) > QuantizeNormalQ15(B.Normal.Y);
  }

  FVector QuantizeVector2D(const FVector& Value, const float Quantum)
  {
    if (Quantum <= SMALL_NUMBER)
    {
      return FVector(Value.X, Value.Y, Value.Z);
    }
    return FVector(
      FMath::RoundToFloat(Value.X / Quantum) * Quantum,
      FMath::RoundToFloat(Value.Y / Quantum) * Quantum,
      Value.Z);
  }

  FVector QuantizeVector2DAlongCorrection(
    const FVector& Value,
    const FVector& CorrectionOrigin,
    const float Quantum)
  {
    if (Quantum <= SMALL_NUMBER) return FVector(Value.X, Value.Y, Value.Z);
    const auto QuantizeAxis = [Quantum](const float Coordinate, const float Origin)
    {
      if (Coordinate > Origin + ConstraintEpsilonCm)
        return FMath::CeilToFloat(Coordinate / Quantum) * Quantum;
      if (Coordinate < Origin - ConstraintEpsilonCm)
        return FMath::FloorToFloat(Coordinate / Quantum) * Quantum;
      return FMath::RoundToFloat(Coordinate / Quantum) * Quantum;
    };
    return FVector(
      QuantizeAxis(Value.X, CorrectionOrigin.X),
      QuantizeAxis(Value.Y, CorrectionOrigin.Y),
      Value.Z);
  }

  float Percentile(TArray<float> Values, const float Fraction)
  {
    if (Values.IsEmpty()) return 0.0f;
    Values.Sort();
    const int32 Index = FMath::Clamp(
      FMath::CeilToInt(Fraction * static_cast<float>(Values.Num())) - 1,
      0,
      Values.Num() - 1);
    return Values[Index];
  }

  struct FSweptDistance
  {
    float Time = 0.0f;
    float Distance = 0.0f;
    FVector Normal = FVector::ForwardVector;
  };

  FSweptDistance EvaluateSweptDistance(
    const FVector& StartA,
    const FVector& EndA,
    const FVector& StartB,
    const FVector& EndB,
    const int32 MinAgentId,
    const int32 MaxAgentId)
  {
    const FVector RelativeStart = Flatten2D(StartA - StartB);
    const FVector RelativeDelta = Flatten2D((EndA - StartA) - (EndB - StartB));
    const float DeltaSizeSq = RelativeDelta.SizeSquared2D();
    FSweptDistance Result;
    Result.Time = DeltaSizeSq > SMALL_NUMBER
      ? FMath::Clamp(-Dot2D(RelativeStart, RelativeDelta) / DeltaSizeSq, 0.0f, 1.0f)
      : 0.0f;
    const FVector Closest = RelativeStart + RelativeDelta * Result.Time;
    Result.Distance = Closest.Size2D();
    if (Result.Distance > ConstraintEpsilonCm)
    {
      Result.Normal = Closest / Result.Distance;
    }
    else if (DeltaSizeSq > SMALL_NUMBER)
    {
      Result.Normal = FVector(-RelativeDelta.Y, RelativeDelta.X, 0.0f).GetSafeNormal2D();
      uint32 Hash = FoldHash(FoldHash(2166136261u, MinAgentId), MaxAgentId);
      if ((Hash & 1u) != 0u) Result.Normal *= -1.0f;
    }
    else
    {
      Result.Normal = MakeStablePairDirection(MinAgentId, MaxAgentId);
    }
    return Result;
  }

  bool IsSweptPairViolation(
    const float StartDistance,
    const float SweptDistance,
    const float RequiredDistance)
  {
    // A quantized prior state can begin with a sub-centimeter inherited
    // overlap. Such a pair must not penetrate deeper, but a path that exits
    // the overlap is recovery rather than a newly introduced swept collision.
    const float SafetyThreshold = StartDistance + ConstraintEpsilonCm
      < RequiredDistance ? StartDistance : RequiredDistance;
    return SweptDistance + ConstraintEpsilonCm < SafetyThreshold;
  }

  bool ApplyPairCorrection(
    const FCrowdParticleConstraintPair& Pair,
    TConstArrayView<FCrowdParticleConstraintAgent> Agents,
    TArray<FVector>& Positions,
    const FVector& Normal,
    const float CorrectionCm,
    const int32 Iteration,
    TArray<int32>& FirstInfluencedIteration,
    TArray<int32>& CorrectedPairCounts)
  {
    if (CorrectionCm <= ConstraintEpsilonCm) return false;
    const float MobilityA = FMath::Max(0.0f, Agents[Pair.MinAgentIndex].Mobility);
    const float MobilityB = FMath::Max(0.0f, Agents[Pair.MaxAgentIndex].Mobility);
    const float TotalMobility = MobilityA + MobilityB;
    if (TotalMobility <= SMALL_NUMBER) return false;
    const FVector CorrectionA = Normal * (CorrectionCm * MobilityA / TotalMobility);
    const FVector CorrectionB = -Normal * (CorrectionCm * MobilityB / TotalMobility);
    Positions[Pair.MinAgentIndex] += CorrectionA;
    Positions[Pair.MaxAgentIndex] += CorrectionB;
    if (!CorrectionA.IsNearlyZero(ConstraintEpsilonCm))
    {
      if (FirstInfluencedIteration[Pair.MinAgentIndex] == INDEX_NONE)
        FirstInfluencedIteration[Pair.MinAgentIndex] = Iteration + 1;
      ++CorrectedPairCounts[Pair.MinAgentIndex];
    }
    if (!CorrectionB.IsNearlyZero(ConstraintEpsilonCm))
    {
      if (FirstInfluencedIteration[Pair.MaxAgentIndex] == INDEX_NONE)
        FirstInfluencedIteration[Pair.MaxAgentIndex] = Iteration + 1;
      ++CorrectedPairCounts[Pair.MaxAgentIndex];
    }
    return true;
  }

  FCrowdSharedFlowConstraintResult ConstrainParticleMovement(
    const FCrowdParticleConstraintAgent& Agent,
    const FCrowdParticleConstraintEnvironment& Environment,
    const FVector& Proposed,
    const float FixedStepSeconds)
  {
    FCrowdSharedFlowFieldConfig Config = Environment.FlowConfig;
    Config.AgentInflateCm = AgentEnvironmentHardDistance(Agent);
    FVector DomainProposed = Proposed;
    if (Environment.bConstrainToFlowBounds)
    {
      const float Clearance = Config.AgentInflateCm;
      const float MinX = Config.BoundsMin.X + Clearance;
      const float MaxX = Config.BoundsMax.X - Clearance;
      const float MinY = Config.BoundsMin.Y + Clearance;
      const float MaxY = Config.BoundsMax.Y - Clearance;
      DomainProposed.X = MinX <= MaxX ? FMath::Clamp(DomainProposed.X, MinX, MaxX) : Agent.StartPosition.X;
      DomainProposed.Y = MinY <= MaxY ? FMath::Clamp(DomainProposed.Y, MinY, MaxY) : Agent.StartPosition.Y;
    }
    return FCrowdSharedFlowFieldKernel::ConstrainMovement(
      Config, Agent.StartPosition, DomainProposed, FixedStepSeconds, false);
  }

  bool IsOutsideParticleBounds(
    const FCrowdParticleConstraintAgent& Agent,
    const FCrowdParticleConstraintEnvironment& Environment,
    const FVector& Position)
  {
    if (!Environment.bConstrainToFlowBounds) return false;
    const float Clearance = AgentEnvironmentHardDistance(Agent);
    return Position.X < Environment.FlowConfig.BoundsMin.X + Clearance - ConstraintEpsilonCm
      || Position.X > Environment.FlowConfig.BoundsMax.X - Clearance + ConstraintEpsilonCm
      || Position.Y < Environment.FlowConfig.BoundsMin.Y + Clearance - ConstraintEpsilonCm
      || Position.Y > Environment.FlowConfig.BoundsMax.Y - Clearance + ConstraintEpsilonCm;
  }

  struct FEnvironmentSoftFact
  {
    int32 Iteration = 0;
    int32 AgentId = INDEX_NONE;
    int32 EnvironmentId = INDEX_NONE;
    int32 Kind = 0;
    int32 Face = 0;
    FVector Normal = FVector::ZeroVector;
    FVector BeforePosition = FVector::ZeroVector;
    float ErrorCm = 0.0f;
    float RequestedCm = 0.0f;
    float RealizedCm = 0.0f;
  };

  struct FEnvironmentContactFact
  {
    int32 Iteration = 0;
    int32 Stage = 0;
    FCrowdParticleEnvironmentContact Contact;
  };

  struct FUnifiedHardFact
  {
    int32 Iteration = 0;
    int32 Stage = 0;
    FCrowdParticleHardConstraint Constraint;
    FCrowdParticleHardDualState Dual;
  };

}

void FCrowdParticleConstraintKernel::BuildCandidatePairs(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> EndPositions,
  TArray<FCrowdParticleConstraintPair>& OutPairs)
{
  OutPairs.Reset();
  if (Agents.Num() != EndPositions.Num() || Agents.Num() < 2) return;

  TArray<int32> SortedIndices;
  SortedIndices.Reserve(Agents.Num());
  float CellSizeCm = 1.0f;
  for (int32 Index = 0; Index < Agents.Num(); ++Index)
  {
    SortedIndices.Add(Index);
    CellSizeCm = FMath::Max(CellSizeCm,
      2.0f * Agents[Index].PhysicalRadiusCm + Agents[Index].HardSafetyGapCm
      + 2.0f * Agents[Index].SoftMarginCm);
  }
  SortedIndices.Sort([Agents](const int32 A, const int32 B)
  {
    return Agents[A].AgentId < Agents[B].AgentId;
  });

  TMap<int64, TArray<int32>> Grid;
  for (const int32 AgentIndex : SortedIndices)
  {
    const FCrowdParticleConstraintAgent& Agent = Agents[AgentIndex];
    const float Reach = FMath::Max(1.0f,
      Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm + Agent.SoftMarginCm);
    const FVector& Start = Agent.StartPosition;
    const FVector& End = EndPositions[AgentIndex];
    const int32 MinCellX = FMath::FloorToInt((FMath::Min(Start.X, End.X) - Reach) / CellSizeCm);
    const int32 MaxCellX = FMath::FloorToInt((FMath::Max(Start.X, End.X) + Reach) / CellSizeCm);
    const int32 MinCellY = FMath::FloorToInt((FMath::Min(Start.Y, End.Y) - Reach) / CellSizeCm);
    const int32 MaxCellY = FMath::FloorToInt((FMath::Max(Start.Y, End.Y) + Reach) / CellSizeCm);
    for (int32 Y = MinCellY; Y <= MaxCellY; ++Y)
      for (int32 X = MinCellX; X <= MaxCellX; ++X)
        Grid.FindOrAdd(MakeCellKey(FIntPoint(X, Y))).Add(AgentIndex);
  }

  TArray<int64> CellKeys;
  Grid.GetKeys(CellKeys);
  CellKeys.Sort();
  TSet<uint64> SeenPairKeys;
  SeenPairKeys.Reserve(Agents.Num() * 8);
  OutPairs.Reserve(Agents.Num() * 8);
  for (const int64 CellKey : CellKeys)
  {
    TArray<int32>& CellAgents = Grid.FindChecked(CellKey);
    CellAgents.Sort([Agents](const int32 A, const int32 B)
    {
      return Agents[A].AgentId < Agents[B].AgentId;
    });
    for (int32 A = 0; A < CellAgents.Num(); ++A)
    {
      for (int32 B = A + 1; B < CellAgents.Num(); ++B)
      {
        if (Agents[CellAgents[A]].InteractionLayer
          != Agents[CellAgents[B]].InteractionLayer)
          continue;
        const int32 MinAgentId =
          Agents[CellAgents[A]].AgentId;
        const int32 MaxAgentId =
          Agents[CellAgents[B]].AgentId;
        const uint64 PairKey =
          (static_cast<uint64>(
            static_cast<uint32>(MinAgentId)) << 32)
          | static_cast<uint32>(MaxAgentId);
        if (SeenPairKeys.Contains(PairKey))
          continue;
        SeenPairKeys.Add(PairKey);
        FCrowdParticleConstraintPair& Pair = OutPairs.AddDefaulted_GetRef();
        Pair.MinAgentIndex = CellAgents[A];
        Pair.MaxAgentIndex = CellAgents[B];
        Pair.MinAgentId = MinAgentId;
        Pair.MaxAgentId = MaxAgentId;
      }
    }
  }
  OutPairs.Sort([](const FCrowdParticleConstraintPair& A, const FCrowdParticleConstraintPair& B)
  {
    return A.MinAgentId < B.MinAgentId
      || (A.MinAgentId == B.MinAgentId && A.MaxAgentId < B.MaxAgentId);
  });
  int32 UniqueCount = 0;
  for (int32 PairIndex = 0; PairIndex < OutPairs.Num(); ++PairIndex)
  {
    if (UniqueCount > 0
      && OutPairs[UniqueCount - 1].MinAgentId == OutPairs[PairIndex].MinAgentId
      && OutPairs[UniqueCount - 1].MaxAgentId == OutPairs[PairIndex].MaxAgentId)
      continue;
    if (UniqueCount != PairIndex) OutPairs[UniqueCount] = OutPairs[PairIndex];
    ++UniqueCount;
  }
  OutPairs.SetNum(UniqueCount);
  OutPairs.RemoveAll([Agents, EndPositions](const FCrowdParticleConstraintPair& Pair)
  {
    const auto& A = Agents[Pair.MinAgentIndex];
    const auto& B = Agents[Pair.MaxAgentIndex];
    if (FVector::DistSquared2D(EndPositions[Pair.MinAgentIndex], EndPositions[Pair.MaxAgentIndex])
      < FMath::Square(PairSoftDistance(A, B)))
      return false;
    const FSweptDistance Swept = EvaluateSweptDistance(
      A.StartPosition, EndPositions[Pair.MinAgentIndex],
      B.StartPosition, EndPositions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
    return Swept.Distance >= PairHardDistance(A, B);
  });
}

bool FCrowdParticleConstraintKernel::BuildEnvironmentContacts(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> Positions,
  const FCrowdParticleConstraintEnvironment& Environment,
  TArray<FCrowdParticleEnvironmentContact>& OutContacts)
{
  OutContacts.Reset();
  if (Agents.Num() != Positions.Num()) return false;

  TArray<FCrowdSharedFlowObstacleSpec> Obstacles = Environment.FlowConfig.ObstacleSpecs;
  Obstacles.Sort([](const auto& A, const auto& B)
  {
    if (A.ObstacleId != B.ObstacleId) return A.ObstacleId < B.ObstacleId;
    const FVector AC = FVector(A.Center);
    const FVector BC = FVector(B.Center);
    if (!FMath::IsNearlyEqual(AC.X, BC.X)) return AC.X < BC.X;
    if (!FMath::IsNearlyEqual(AC.Y, BC.Y)) return AC.Y < BC.Y;
    const FVector AE = FVector(A.Extent);
    const FVector BE = FVector(B.Extent);
    if (!FMath::IsNearlyEqual(AE.X, BE.X)) return AE.X < BE.X;
    return AE.Y < BE.Y;
  });
  for (int32 ObstacleIndex = 0; ObstacleIndex < Obstacles.Num(); ++ObstacleIndex)
  {
    const FVector Extent = FVector(Obstacles[ObstacleIndex].Extent);
    if (Obstacles[ObstacleIndex].ObstacleId < 0 || Extent.X < 0.0f || Extent.Y < 0.0f
      || (ObstacleIndex > 0
        && Obstacles[ObstacleIndex - 1].ObstacleId == Obstacles[ObstacleIndex].ObstacleId))
      return false;
  }

  const auto AddBoundsContact = [&](const int32 AgentIndex,
    const ECrowdParticleEnvironmentFace Face,
    const FVector& Normal,
    const float HardPlane,
    const float SoftPlane,
    const float Coordinate)
  {
    const float HardDeficit = HardPlane - Coordinate;
    const float SoftError = SoftPlane - Coordinate;
    if (SoftError <= ConstraintEpsilonCm && HardDeficit <= ConstraintEpsilonCm) return;
    FCrowdParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
    Contact.AgentId = Agents[AgentIndex].AgentId;
    Contact.AgentIndex = AgentIndex;
    Contact.EnvironmentId = -1000 - static_cast<int32>(Face);
    Contact.ContactKind = ECrowdParticleEnvironmentContactKind::FlowBounds;
    Contact.Face = Face;
    Contact.CorrectionNormal = Normal;
    Contact.HardDistanceCm = AgentEnvironmentHardDistance(Agents[AgentIndex]);
    Contact.SoftDistanceCm = Contact.HardDistanceCm + Agents[AgentIndex].SoftMarginCm;
    Contact.SoftErrorCm = FMath::Max(0.0f, SoftError);
    Contact.HardDeficitCm = FMath::Max(0.0f, HardDeficit);
    Contact.ConstraintThreshold = HardPlane;
    Contact.SweptTime = 1.0f;
    Contact.ClosestPoint = Positions[AgentIndex] + Normal * Contact.SoftErrorCm;
  };

  for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
  {
    const auto& Agent = Agents[AgentIndex];
    const FVector Position = Positions[AgentIndex];
    const float HardDistance = AgentEnvironmentHardDistance(Agent);
    const float SoftDistance = HardDistance + FMath::Max(0.0f, Agent.SoftMarginCm);

    if (Environment.bConstrainToFlowBounds)
    {
      const FVector BoundsMin = FVector(Environment.FlowConfig.BoundsMin);
      const FVector BoundsMax = FVector(Environment.FlowConfig.BoundsMax);
      if (BoundsMin.X + HardDistance > BoundsMax.X - HardDistance
        || BoundsMin.Y + HardDistance > BoundsMax.Y - HardDistance)
        return false;
      AddBoundsContact(AgentIndex, ECrowdParticleEnvironmentFace::MinX,
        FVector(1.0f, 0.0f, 0.0f), BoundsMin.X + HardDistance,
        BoundsMin.X + SoftDistance, Position.X);
      AddBoundsContact(AgentIndex, ECrowdParticleEnvironmentFace::MaxX,
        FVector(-1.0f, 0.0f, 0.0f), -(BoundsMax.X - HardDistance),
        -(BoundsMax.X - SoftDistance), -Position.X);
      AddBoundsContact(AgentIndex, ECrowdParticleEnvironmentFace::MinY,
        FVector(0.0f, 1.0f, 0.0f), BoundsMin.Y + HardDistance,
        BoundsMin.Y + SoftDistance, Position.Y);
      AddBoundsContact(AgentIndex, ECrowdParticleEnvironmentFace::MaxY,
        FVector(0.0f, -1.0f, 0.0f), -(BoundsMax.Y - HardDistance),
        -(BoundsMax.Y - SoftDistance), -Position.Y);
    }

    for (const auto& Obstacle : Obstacles)
    {
      const FVector Center = FVector(Obstacle.Center);
      const FVector Extent = FVector(Obstacle.Extent);
      const FVector RawMin = Center - Extent;
      const FVector RawMax = Center + Extent;
      const FVector HardInflate(HardDistance, HardDistance, 0.0f);
      const FVector SoftInflate(SoftDistance, SoftDistance, 0.0f);
      const FVector HardMin = RawMin - HardInflate;
      const FVector HardMax = RawMax + HardInflate;
      const FVector SoftMin = RawMin - SoftInflate;
      const FVector SoftMax = RawMax + SoftInflate;
      const bool bInsideHard = IsInsideBox2D(Position, HardMin, HardMax);
      const bool bInsideSoft = IsInsideBox2D(Position, SoftMin, SoftMax);
      float EntryTime = 0.0f;
      ECrowdParticleEnvironmentFace EntryFace = ECrowdParticleEnvironmentFace::MinX;
      const bool bSweptIntersects = SegmentBoxEntryFace(
        Agent.StartPosition, Position, HardMin, HardMax, EntryTime, EntryFace);
      if (bInsideHard || bInsideSoft)
      {
        const ECrowdParticleEnvironmentFace Face = bInsideHard && bSweptIntersects
          && !IsInsideBox2D(Agent.StartPosition, HardMin, HardMax)
          ? EntryFace
          : (bInsideHard ? SelectEscapeFace(Position, HardMin, HardMax)
            : SelectEscapeFace(Position, SoftMin, SoftMax));
        const FVector Normal = ContactFaceNormal(Face);
        FCrowdParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
        Contact.AgentId = Agent.AgentId;
        Contact.AgentIndex = AgentIndex;
        Contact.EnvironmentId = Obstacle.ObstacleId;
        Contact.ContactKind = ECrowdParticleEnvironmentContactKind::ObstacleEndpoint;
        Contact.Face = Face;
        Contact.ClosestPoint = ClosestPointOnFace(Position, RawMin, RawMax, Face);
        Contact.CorrectionNormal = Normal;
        Contact.HardDistanceCm = HardDistance;
        Contact.SoftDistanceCm = SoftDistance;
        Contact.SoftErrorCm = bInsideSoft
          ? FMath::Max(0.0f, FaceEscapeDistance(Position, SoftMin, SoftMax, Face)) : 0.0f;
        Contact.HardDeficitCm = bInsideHard
          ? FMath::Max(0.0f, FaceEscapeDistance(Position, HardMin, HardMax, Face)
            + ConstraintEpsilonCm) : 0.0f;
        Contact.ConstraintThreshold = FaceThreshold(HardMin, HardMax, Face, ConstraintEpsilonCm);
        Contact.SweptTime = 1.0f;
      }

      if (bSweptIntersects)
      {
        const FVector Normal = ContactFaceNormal(EntryFace);
        const float Threshold = FaceThreshold(HardMin, HardMax, EntryFace, ConstraintEpsilonCm);
        const float Deficit = Threshold - FVector::DotProduct(Position, Normal);
        if (Deficit > ConstraintEpsilonCm || EntryTime <= ConstraintEpsilonCm)
        {
          FCrowdParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
          Contact.AgentId = Agent.AgentId;
          Contact.AgentIndex = AgentIndex;
          Contact.EnvironmentId = Obstacle.ObstacleId;
          Contact.ContactKind = ECrowdParticleEnvironmentContactKind::ObstacleSwept;
          Contact.Face = EntryFace;
          Contact.ClosestPoint = ClosestPointOnFace(
            Agent.StartPosition + (Position - Agent.StartPosition) * EntryTime,
            RawMin, RawMax, EntryFace);
          Contact.CorrectionNormal = Normal;
          Contact.HardDistanceCm = HardDistance;
          Contact.SoftDistanceCm = SoftDistance;
          Contact.HardDeficitCm = FMath::Max(0.0f, Deficit);
          Contact.SweptTime = EntryTime;
          Contact.ConstraintThreshold = Threshold;
        }
      }
    }
  }

  OutContacts.Sort([](const auto& A, const auto& B)
  {
    if (A.AgentId != B.AgentId) return A.AgentId < B.AgentId;
    if (A.EnvironmentId != B.EnvironmentId) return A.EnvironmentId < B.EnvironmentId;
    if (A.ContactKind != B.ContactKind)
      return static_cast<uint8>(A.ContactKind) < static_cast<uint8>(B.ContactKind);
    if (A.Face != B.Face) return static_cast<uint8>(A.Face) < static_cast<uint8>(B.Face);
    const int32 AX = QuantizeNormalQ15(A.CorrectionNormal.X);
    const int32 BX = QuantizeNormalQ15(B.CorrectionNormal.X);
    if (AX != BX) return AX < BX;
    const int32 AY = QuantizeNormalQ15(A.CorrectionNormal.Y);
    const int32 BY = QuantizeNormalQ15(B.CorrectionNormal.Y);
    if (AY != BY) return AY < BY;
    if (!FMath::IsNearlyEqual(A.ClosestPoint.X, B.ClosestPoint.X))
      return A.ClosestPoint.X < B.ClosestPoint.X;
    return A.ClosestPoint.Y < B.ClosestPoint.Y;
  });
  return true;
}

void FCrowdParticleConstraintKernel::BuildUnifiedHardConstraints(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> Positions,
  const TConstArrayView<FCrowdParticleConstraintPair> Pairs,
  const TConstArrayView<FCrowdParticleEnvironmentContact> Contacts,
  TArray<FCrowdParticleHardConstraint>& OutConstraints)
{
  OutConstraints.Reset();
  if (Agents.Num() != Positions.Num()) return;
  for (const auto& Pair : Pairs)
  {
    if (!Agents.IsValidIndex(Pair.MinAgentIndex) || !Agents.IsValidIndex(Pair.MaxAgentIndex)) continue;
    const auto& A = Agents[Pair.MinAgentIndex];
    const auto& B = Agents[Pair.MaxAgentIndex];
    const float HardDistance = PairHardDistance(A, B);
    const FVector Delta = Flatten2D(
      Positions[Pair.MinAgentIndex] - Positions[Pair.MaxAgentIndex]);
    const float Distance = Delta.Size2D();
    FCrowdParticleHardConstraint& EndpointConstraint = OutConstraints.AddDefaulted_GetRef();
    EndpointConstraint.Kind = ECrowdParticleHardConstraintKind::PairEndpoint;
    EndpointConstraint.MinAgentId = Pair.MinAgentId;
    EndpointConstraint.MaxAgentId = Pair.MaxAgentId;
    EndpointConstraint.MinAgentIndex = Pair.MinAgentIndex;
    EndpointConstraint.MaxAgentIndex = Pair.MaxAgentIndex;
    EndpointConstraint.Normal = Distance > SMALL_NUMBER
      ? Delta / Distance : MakeStablePairDirection(Pair.MinAgentId, Pair.MaxAgentId);
    EndpointConstraint.Threshold = HardDistance;
    EndpointConstraint.InitialDeficitCm = HardDistance - Distance;

    const FSweptDistance Swept = EvaluateSweptDistance(
      A.StartPosition, Positions[Pair.MinAgentIndex],
      B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
    const float Scale = FMath::Max(0.05f, Swept.Time);
    const FVector RelativeStart = Flatten2D(A.StartPosition - B.StartPosition);
    FCrowdParticleHardConstraint& SweptConstraint = OutConstraints.AddDefaulted_GetRef();
    SweptConstraint.Kind = ECrowdParticleHardConstraintKind::PairSwept;
    SweptConstraint.MinAgentId = Pair.MinAgentId;
    SweptConstraint.MaxAgentId = Pair.MaxAgentId;
    SweptConstraint.MinAgentIndex = Pair.MinAgentIndex;
    SweptConstraint.MaxAgentIndex = Pair.MaxAgentIndex;
    SweptConstraint.Normal = Swept.Normal;
    SweptConstraint.CoefficientScale = Scale;
    SweptConstraint.Threshold = HardDistance
      - (1.0f - Scale) * Dot2D(Swept.Normal, RelativeStart);
    SweptConstraint.InitialDeficitCm =
      SweptConstraint.Threshold - Scale * Dot2D(Swept.Normal, Delta);
  }

  for (const auto& Contact : Contacts)
  {
    if (Contact.ContactKind == ECrowdParticleEnvironmentContactKind::ObstacleSwept
      && Contact.HardDeficitCm <= ConstraintEpsilonCm)
      continue;
    FCrowdParticleHardConstraint& Constraint = OutConstraints.AddDefaulted_GetRef();
    Constraint.Kind = Contact.ContactKind == ECrowdParticleEnvironmentContactKind::ObstacleEndpoint
      ? ECrowdParticleHardConstraintKind::ObstacleEndpoint
      : (Contact.ContactKind == ECrowdParticleEnvironmentContactKind::ObstacleSwept
        ? ECrowdParticleHardConstraintKind::ObstacleSwept
        : ECrowdParticleHardConstraintKind::FlowBounds);
    Constraint.MinAgentId = Contact.AgentId;
    Constraint.MinAgentIndex = Contact.AgentIndex;
    Constraint.EnvironmentId = Contact.EnvironmentId;
    Constraint.Face = Contact.Face;
    Constraint.Normal = Contact.CorrectionNormal;
    Constraint.Threshold = Contact.ConstraintThreshold;
    Constraint.InitialDeficitCm = Contact.HardDeficitCm;
  }
  OutConstraints.Sort(ConstraintLess);
}

void FCrowdParticleConstraintKernel::SolveUnifiedHardClosure(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const FCrowdParticleConstraintSettings& Settings,
  const TConstArrayView<FCrowdParticleHardConstraint> Constraints,
  TArray<FVector>& InOutPositions,
  TArray<FCrowdParticleHardDualState>& InOutDualStates,
  FCrowdParticleUnifiedHardSummary& OutSummary,
  const int32 StableSweepIndex)
{
  OutSummary = FCrowdParticleUnifiedHardSummary();
  OutSummary.ConstraintCount = Constraints.Num();
  if (InOutPositions.Num() != Agents.Num())
  {
    OutSummary.bValid = false;
    OutSummary.InfeasibleConstraintCount = Constraints.Num();
    return;
  }

  TArray<FCrowdParticleHardConstraint> SortedConstraints(Constraints);
  SortedConstraints.Sort(ConstraintLess);

  // The overwhelmingly common safety pass is already feasible after the
  // previous projection/quantization.  Building component arrays and an
  // active-set system for a set with no positive residual is pure overhead.
  // Preserve the complete path whenever a prior dual can still relax or any
  // constraint is currently violated.  Structural validation remains part of
  // this fast path so invalid public-kernel inputs cannot be accepted.
  bool bStructurallyValid = true;
  bool bHasPositiveResidual = false;
  for (const auto& Constraint : SortedConstraints)
  {
    int32 AgentAIndex = Constraint.MinAgentIndex;
    int32 AgentBIndex = Constraint.MaxAgentIndex;
    if (!Agents.IsValidIndex(AgentAIndex)
      || Agents[AgentAIndex].AgentId != Constraint.MinAgentId)
      AgentAIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Constraint.MinAgentId; });
    if (Constraint.MaxAgentId != INDEX_NONE
      && (!Agents.IsValidIndex(AgentBIndex)
        || Agents[AgentBIndex].AgentId != Constraint.MaxAgentId))
      AgentBIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      { return Agent.AgentId == Constraint.MaxAgentId; });
    const bool bIndicesValid = Agents.IsValidIndex(AgentAIndex)
      && (Constraint.MaxAgentId == INDEX_NONE || Agents.IsValidIndex(AgentBIndex));
    const bool bNumericValid =
      FMath::Abs(Constraint.Normal.SizeSquared2D() - 1.0f) <= 0.01f
      && FMath::Abs(Constraint.Normal.Z) <= ConstraintEpsilonCm
      && Constraint.CoefficientScale > 0.0f;
    bStructurallyValid &= bIndicesValid && bNumericValid;
    if (bIndicesValid && bNumericValid)
    {
      const float Scale = FMath::Max(0.0001f, Constraint.CoefficientScale);
      const float Lhs = Scale * Dot2D(InOutPositions[AgentAIndex], Constraint.Normal)
        - (Constraint.MaxAgentId != INDEX_NONE
          ? Scale * Dot2D(InOutPositions[AgentBIndex], Constraint.Normal) : 0.0f);
      bHasPositiveResidual |= Constraint.Threshold - Lhs > ConstraintEpsilonCm;
    }
  }
  if (!bStructurallyValid)
  {
    OutSummary.bValid = false;
    OutSummary.InfeasibleConstraintCount = SortedConstraints.Num();
    return;
  }
  if (!bHasPositiveResidual && InOutDualStates.IsEmpty())
  {
    OutSummary.bValid = true;
    return;
  }

  // Build deterministic connected components in agent-id space. Environment
  // constraints remain attached to their agent; pair constraints union both
  // endpoints. Each existing outer iteration still performs exactly one
  // sweep, but odd iterations reverse the stable order inside each component
  // so one terminal constraint cannot permanently erase the opposite end.
  TArray<int32> Parents;
  Parents.SetNumUninitialized(Agents.Num());
  for (int32 Index = 0; Index < Parents.Num(); ++Index) Parents[Index] = Index;
  const auto FindRoot = [&Parents](int32 Index)
  {
    while (Parents[Index] != Index) Index = Parents[Index];
    return Index;
  };
  const auto UnionAgents = [&Parents, &Agents, &FindRoot](const int32 A, const int32 B)
  {
    if (!Agents.IsValidIndex(A) || !Agents.IsValidIndex(B)) return;
    const int32 RootA = FindRoot(A);
    const int32 RootB = FindRoot(B);
    if (RootA == RootB) return;
    if (Agents[RootA].AgentId < Agents[RootB].AgentId)
      Parents[RootB] = RootA;
    else
      Parents[RootA] = RootB;
  };
  for (const auto& Constraint : SortedConstraints)
  {
    if (Constraint.MaxAgentId != INDEX_NONE)
      UnionAgents(Constraint.MinAgentIndex, Constraint.MaxAgentIndex);
  }
  TArray<TArray<FCrowdParticleHardConstraint>> ComponentConstraints;
  ComponentConstraints.SetNum(Agents.Num());
  for (const auto& Constraint : SortedConstraints)
  {
    int32 AgentIndex = Constraint.MinAgentIndex;
    if (!Agents.IsValidIndex(AgentIndex) || Agents[AgentIndex].AgentId != Constraint.MinAgentId)
      AgentIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Constraint.MinAgentId;
      });
    if (Agents.IsValidIndex(AgentIndex))
      ComponentConstraints[FindRoot(AgentIndex)].Add(Constraint);
  }
  TArray<int32> ComponentRoots;
  for (int32 Root = 0; Root < ComponentConstraints.Num(); ++Root)
    if (!ComponentConstraints[Root].IsEmpty()) ComponentRoots.Add(Root);
  ComponentRoots.Sort([&](const int32 A, const int32 B)
  {
    return Agents[A].AgentId < Agents[B].AgentId;
  });
  TArray<FCrowdParticleHardConstraint> SweepConstraints;
  SweepConstraints.Reserve(SortedConstraints.Num());
  if (StableSweepIndex == INDEX_NONE)
  {
    SweepConstraints = SortedConstraints;
  }
  else
  {
    const bool bStartFromBack = (StableSweepIndex & 1) != 0;
    for (const int32 Root : ComponentRoots)
    {
      auto& Component = ComponentConstraints[Root];
      Component.Sort(ConstraintLess);
      int32 Front = 0;
      int32 Back = Component.Num() - 1;
      bool bTakeBack = bStartFromBack;
      while (Front <= Back)
      {
        SweepConstraints.Add(bTakeBack ? Component[Back--] : Component[Front++]);
        bTakeBack = !bTakeBack;
      }
    }
  }

  TArray<FCrowdParticleHardDualState> NewDualStates;
  NewDualStates.Reserve(Constraints.Num());
  const float HardCap = FMath::Max(0.0f, Settings.HardMaxPairCorrectionPerIterationCm);
  TArray<TArray<FVector>> ActiveEnvironmentNormals;
  ActiveEnvironmentNormals.SetNum(Agents.Num());
  constexpr float ActiveEnvironmentBandCm = 2.0f;
  for (const auto& Constraint : SweepConstraints)
  {
    if (Constraint.MaxAgentId != INDEX_NONE) continue;
    int32 AgentIndex = Constraint.MinAgentIndex;
    if (!Agents.IsValidIndex(AgentIndex) || Agents[AgentIndex].AgentId != Constraint.MinAgentId)
      AgentIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Constraint.MinAgentId;
      });
    if (!InOutPositions.IsValidIndex(AgentIndex)) continue;
    const float Slack = Dot2D(InOutPositions[AgentIndex], Constraint.Normal)
      - Constraint.Threshold;
    if (Slack > ActiveEnvironmentBandCm) continue;
    TArray<FVector>& Normals = ActiveEnvironmentNormals[AgentIndex];
    const bool bDuplicate = Normals.ContainsByPredicate([&](const FVector& Existing)
    {
      return Dot2D(Existing, Constraint.Normal) > 0.999f;
    });
    if (!bDuplicate) Normals.Add(Constraint.Normal.GetSafeNormal2D());
  }
  const auto ProjectAllowedDirection = [&](const int32 AgentIndex, FVector Direction)
  {
    if (!ActiveEnvironmentNormals.IsValidIndex(AgentIndex)) return Direction;
    for (const FVector& Normal : ActiveEnvironmentNormals[AgentIndex])
    {
      const float IntoWall = Dot2D(Direction, Normal);
      if (IntoWall < 0.0f) Direction -= Normal * IntoWall;
    }
    return Direction;
  };
  if (StableSweepIndex == INDEX_NONE)
  {
    for (const auto& Constraint : SweepConstraints)
    {
      int32 AgentAIndex = Constraint.MinAgentIndex;
      int32 AgentBIndex = Constraint.MaxAgentIndex;
      if (!Agents.IsValidIndex(AgentAIndex) || Agents[AgentAIndex].AgentId != Constraint.MinAgentId)
        AgentAIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
        {
          return Agent.AgentId == Constraint.MinAgentId;
        });
      if (Constraint.MaxAgentId != INDEX_NONE
        && (!Agents.IsValidIndex(AgentBIndex) || Agents[AgentBIndex].AgentId != Constraint.MaxAgentId))
        AgentBIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
        {
          return Agent.AgentId == Constraint.MaxAgentId;
        });
      if (!Agents.IsValidIndex(AgentAIndex)
        || (Constraint.MaxAgentId != INDEX_NONE && !Agents.IsValidIndex(AgentBIndex))
        || FMath::Abs(Constraint.Normal.SizeSquared2D() - 1.0f) > 0.01f
        || FMath::Abs(Constraint.Normal.Z) > ConstraintEpsilonCm
        || Constraint.CoefficientScale <= 0.0f)
      {
        OutSummary.bValid = false;
        ++OutSummary.InfeasibleConstraintCount;
        continue;
      }

      FCrowdParticleHardDualState State;
      State.Kind = Constraint.Kind;
      State.MinAgentId = Constraint.MinAgentId;
      State.MaxAgentId = Constraint.MaxAgentId;
      State.EnvironmentId = Constraint.EnvironmentId;
      State.Face = Constraint.Face;
      State.NormalXQ15 = QuantizeNormalQ15(Constraint.Normal.X);
      State.NormalYQ15 = QuantizeNormalQ15(Constraint.Normal.Y);
      if (const auto* Existing = InOutDualStates.FindByPredicate(
        [&](const auto& Candidate) { return SameDualKey(Candidate, Constraint); }))
        State.Lambda = FMath::Max(0.0f, Existing->Lambda);

      const float Scale = FMath::Max(0.0001f, Constraint.CoefficientScale);
      const float MobilityA = FMath::Max(0.0f, Agents[AgentAIndex].Mobility);
      const float MobilityB = Constraint.MaxAgentId != INDEX_NONE
        ? FMath::Max(0.0f, Agents[AgentBIndex].Mobility) : 0.0f;
      const float Lhs = Scale * Dot2D(InOutPositions[AgentAIndex], Constraint.Normal)
        - (Constraint.MaxAgentId != INDEX_NONE
          ? Scale * Dot2D(InOutPositions[AgentBIndex], Constraint.Normal) : 0.0f);
      const float Residual = Constraint.Threshold - Lhs;
      const FVector CoefficientA = Constraint.Normal * Scale;
      const FVector CoefficientB = -Constraint.Normal * Scale;
      const FVector MotionA = ProjectAllowedDirection(AgentAIndex, CoefficientA) * MobilityA;
      const FVector MotionB = Constraint.MaxAgentId != INDEX_NONE
        ? ProjectAllowedDirection(AgentBIndex, CoefficientB) * MobilityB : FVector::ZeroVector;
      const float Denominator = Dot2D(CoefficientA, MotionA)
        + (Constraint.MaxAgentId != INDEX_NONE
          ? Dot2D(CoefficientB, MotionB) : 0.0f);
      if (Denominator <= SMALL_NUMBER)
      {
        if (Residual > ConstraintEpsilonCm)
        {
          OutSummary.bValid = false;
          ++OutSummary.InfeasibleConstraintCount;
          OutSummary.MaxResidualCm = FMath::Max(OutSummary.MaxResidualCm, Residual);
        }
        NewDualStates.Add(State);
        continue;
      }

      const float DesiredLambda = FMath::Max(0.0f, State.Lambda + Residual / Denominator);
      float DeltaLambda = DesiredLambda - State.Lambda;
      const float EndpointMotionPerLambda = MotionA.Size2D() + MotionB.Size2D();
      const float MaxDeltaLambda = EndpointMotionPerLambda > SMALL_NUMBER
        ? HardCap / EndpointMotionPerLambda : 0.0f;
      DeltaLambda = FMath::Clamp(DeltaLambda, -MaxDeltaLambda, MaxDeltaLambda);
      State.Lambda = FMath::Max(0.0f, State.Lambda + DeltaLambda);
      const FVector CorrectionA = MotionA * DeltaLambda;
      const FVector CorrectionB = MotionB * DeltaLambda;
      InOutPositions[AgentAIndex] += CorrectionA;
      if (Constraint.MaxAgentId != INDEX_NONE) InOutPositions[AgentBIndex] += CorrectionB;
      OutSummary.MaxAppliedCorrectionCm = FMath::Max(OutSummary.MaxAppliedCorrectionCm,
        FMath::Max(CorrectionA.Size2D(), CorrectionB.Size2D()));

      NewDualStates.Add(State);
    }
  }

  if (StableSweepIndex != INDEX_NONE)
  {
    const auto ProjectComponentFeasible = [&](const TArray<FCrowdParticleHardConstraint>& Component)
    {
      if (Component.IsEmpty()) return true;
      TArray<FCrowdParticleHardConstraint> StableComponent = Component;
      StableComponent.Sort(ConstraintLess);
      TArray<FCrowdParticleHardConstraint> ProjectionConstraints;
      for (const auto& SourceConstraint : StableComponent)
      {
        FCrowdParticleHardConstraint Normalized = SourceConstraint;
        const float Scale = FMath::Max(0.0001f, SourceConstraint.CoefficientScale);
        Normalized.CoefficientScale = 1.0f;
        Normalized.Threshold /= Scale;
        Normalized.InitialDeficitCm /= Scale;
        auto* Existing = ProjectionConstraints.FindByPredicate([&](const auto& Candidate)
        {
          return Candidate.MinAgentId == Normalized.MinAgentId
            && Candidate.MaxAgentId == Normalized.MaxAgentId
            && Candidate.EnvironmentId == Normalized.EnvironmentId
            && Candidate.Face == Normalized.Face
            && QuantizeNormalQ15(Candidate.Normal.X) == QuantizeNormalQ15(Normalized.Normal.X)
            && QuantizeNormalQ15(Candidate.Normal.Y) == QuantizeNormalQ15(Normalized.Normal.Y);
        });
        if (!Existing)
        {
          ProjectionConstraints.Add(Normalized);
        }
        else if (Normalized.Threshold > Existing->Threshold + ConstraintEpsilonCm)
        {
          *Existing = Normalized;
        }
      }
      ProjectionConstraints.Sort(ConstraintLess);
      const int32 ConstraintCount = ProjectionConstraints.Num();
      TArray<double> Requirements;
      Requirements.SetNumZeroed(ConstraintCount);
      const auto AgentIndexForId = [&](const int32 AgentId, const int32 SuggestedIndex)
      {
        if (Agents.IsValidIndex(SuggestedIndex)
          && Agents[SuggestedIndex].AgentId == AgentId)
          return SuggestedIndex;
        return Agents.IndexOfByPredicate([&](const auto& Agent)
        {
          return Agent.AgentId == AgentId;
        });
      };
      const auto Coefficient = [&](const FCrowdParticleHardConstraint& Constraint,
        const int32 AgentIndex)
      {
        const float Scale = FMath::Max(0.0001f, Constraint.CoefficientScale);
        if (Agents[AgentIndex].AgentId == Constraint.MinAgentId)
          return Constraint.Normal * Scale;
        if (Agents[AgentIndex].AgentId == Constraint.MaxAgentId)
          return -Constraint.Normal * Scale;
        return FVector::ZeroVector;
      };
      for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
      {
        const auto& Constraint = ProjectionConstraints[ConstraintIndex];
        const int32 A = AgentIndexForId(Constraint.MinAgentId, Constraint.MinAgentIndex);
        const int32 B = Constraint.MaxAgentId == INDEX_NONE ? INDEX_NONE
          : AgentIndexForId(Constraint.MaxAgentId, Constraint.MaxAgentIndex);
        if (!Agents.IsValidIndex(A)
          || (Constraint.MaxAgentId != INDEX_NONE && !Agents.IsValidIndex(B)))
          return false;
        double Lhs = Dot2D(InOutPositions[A], Coefficient(Constraint, A));
        if (B != INDEX_NONE)
          Lhs += Dot2D(InOutPositions[B], Coefficient(Constraint, B));
        Requirements[ConstraintIndex] = static_cast<double>(Constraint.Threshold) - Lhs;
      }

      TArray<int32> Active;
      TArray<double> ActiveLambda;
      TArray<FVector> Delta;
      Delta.Init(FVector::ZeroVector, Agents.Num());
      bool bConverged = false;
      const int32 MaxActiveSetSteps = FMath::Max(8, ConstraintCount * 4);
      for (int32 Step = 0; Step < MaxActiveSetSteps; ++Step)
      {
        Delta.Init(FVector::ZeroVector, Agents.Num());
        ActiveLambda.Init(0.0, Active.Num());
        if (!Active.IsEmpty())
        {
          const int32 N = Active.Num();
          TArray<TArray<double>> Matrix;
          Matrix.SetNum(N);
          for (int32 Row = 0; Row < N; ++Row)
          {
            Matrix[Row].SetNumZeroed(N + 1);
            const auto& RowConstraint = ProjectionConstraints[Active[Row]];
            for (int32 Column = 0; Column < N; ++Column)
            {
              const auto& ColumnConstraint = ProjectionConstraints[Active[Column]];
              double Gram = 0.0;
              for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
              {
                const double Mobility = FMath::Max(0.0f, Agents[AgentIndex].Mobility);
                if (Mobility <= 0.0) continue;
                Gram += Mobility * Dot2D(
                  Coefficient(RowConstraint, AgentIndex),
                  Coefficient(ColumnConstraint, AgentIndex));
              }
              Matrix[Row][Column] = Gram + (Row == Column ? 1.0e-8 : 0.0);
            }
            Matrix[Row][N] = Requirements[Active[Row]];
          }
          bool bLinearSolveValid = true;
          for (int32 PivotColumn = 0; PivotColumn < N; ++PivotColumn)
          {
            int32 PivotRow = PivotColumn;
            double PivotMagnitude = FMath::Abs(Matrix[PivotRow][PivotColumn]);
            for (int32 CandidateRow = PivotColumn + 1; CandidateRow < N; ++CandidateRow)
            {
              const double CandidateMagnitude = FMath::Abs(Matrix[CandidateRow][PivotColumn]);
              if (CandidateMagnitude > PivotMagnitude + 1.0e-12)
              {
                PivotMagnitude = CandidateMagnitude;
                PivotRow = CandidateRow;
              }
            }
            if (PivotMagnitude <= 1.0e-12)
            {
              bLinearSolveValid = false;
              break;
            }
            if (PivotRow != PivotColumn) Swap(Matrix[PivotRow], Matrix[PivotColumn]);
            const double Pivot = Matrix[PivotColumn][PivotColumn];
            for (int32 Column = PivotColumn; Column <= N; ++Column)
              Matrix[PivotColumn][Column] /= Pivot;
            for (int32 Row = 0; Row < N; ++Row)
            {
              if (Row == PivotColumn) continue;
              const double Factor = Matrix[Row][PivotColumn];
              if (FMath::Abs(Factor) <= 1.0e-15) continue;
              for (int32 Column = PivotColumn; Column <= N; ++Column)
                Matrix[Row][Column] -= Factor * Matrix[PivotColumn][Column];
            }
          }
          if (!bLinearSolveValid) return false;
          for (int32 Index = 0; Index < N; ++Index)
            ActiveLambda[Index] = Matrix[Index][N];

          int32 NegativeLambdaIndex = INDEX_NONE;
          double MostNegativeLambda = -1.0e-7;
          for (int32 Index = 0; Index < ActiveLambda.Num(); ++Index)
          {
            if (ActiveLambda[Index] < MostNegativeLambda)
            {
              MostNegativeLambda = ActiveLambda[Index];
              NegativeLambdaIndex = Index;
            }
          }
          if (NegativeLambdaIndex != INDEX_NONE)
          {
            Active.RemoveAt(NegativeLambdaIndex);
            continue;
          }
          for (int32 ActiveIndex = 0; ActiveIndex < Active.Num(); ++ActiveIndex)
          {
            const auto& Constraint = ProjectionConstraints[Active[ActiveIndex]];
            const double Lambda = FMath::Max(0.0, ActiveLambda[ActiveIndex]);
            for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
            {
              const double Mobility = FMath::Max(0.0f, Agents[AgentIndex].Mobility);
              if (Mobility <= 0.0) continue;
              Delta[AgentIndex] += Coefficient(Constraint, AgentIndex)
                * static_cast<float>(Mobility * Lambda);
            }
          }
        }

        int32 MostViolated = INDEX_NONE;
        double MaximumViolation = ConstraintEpsilonCm;
        for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
        {
          const auto& Constraint = ProjectionConstraints[ConstraintIndex];
          double Achieved = 0.0;
          for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
            Achieved += Dot2D(Delta[AgentIndex], Coefficient(Constraint, AgentIndex));
          const double Violation = Requirements[ConstraintIndex] - Achieved;
          if (Violation > MaximumViolation + 1.0e-9)
          {
            MaximumViolation = Violation;
            MostViolated = ConstraintIndex;
          }
        }
        if (MostViolated == INDEX_NONE)
        {
          bConverged = true;
          break;
        }
        if (Active.Contains(MostViolated)) return false;
        Active.Add(MostViolated);
        Active.Sort();
      }
      if (!bConverged) return false;

      float MaximumConstraintMotion = 0.0f;
      for (const auto& Constraint : StableComponent)
      {
        const int32 A = AgentIndexForId(Constraint.MinAgentId, Constraint.MinAgentIndex);
        const int32 B = Constraint.MaxAgentId == INDEX_NONE ? INDEX_NONE
          : AgentIndexForId(Constraint.MaxAgentId, Constraint.MaxAgentIndex);
        float Motion = Agents.IsValidIndex(A) ? Delta[A].Size2D() : 0.0f;
        if (Agents.IsValidIndex(B)) Motion += Delta[B].Size2D();
        MaximumConstraintMotion = FMath::Max(MaximumConstraintMotion, Motion);
      }
      // HardCap is a per-constraint/component, per-iteration correction cap.
      // MaxAppliedCorrectionCm is an aggregate maximum for reporting; treating
      // it as a shared budget makes later disconnected components lose their
      // lattice repair whenever an earlier component used the cap.
      const float Scale = MaximumConstraintMotion > HardCap && MaximumConstraintMotion > SMALL_NUMBER
        ? HardCap / MaximumConstraintMotion : 1.0f;
      for (int32 AgentIndex = 0; AgentIndex < InOutPositions.Num(); ++AgentIndex)
        InOutPositions[AgentIndex] += Delta[AgentIndex] * Scale;
      OutSummary.MaxAppliedCorrectionCm = FMath::Max(
        OutSummary.MaxAppliedCorrectionCm, MaximumConstraintMotion * Scale);
      for (int32 ActiveIndex = 0; ActiveIndex < Active.Num(); ++ActiveIndex)
      {
        const auto& Constraint = ProjectionConstraints[Active[ActiveIndex]];
        auto* State = NewDualStates.FindByPredicate([&](const auto& Candidate)
        {
          return SameDualKey(Candidate, Constraint);
        });
        if (!State)
        {
          FCrowdParticleHardDualState& Added = NewDualStates.AddDefaulted_GetRef();
          Added.Kind = Constraint.Kind;
          Added.MinAgentId = Constraint.MinAgentId;
          Added.MaxAgentId = Constraint.MaxAgentId;
          Added.EnvironmentId = Constraint.EnvironmentId;
          Added.Face = Constraint.Face;
          Added.NormalXQ15 = QuantizeNormalQ15(Constraint.Normal.X);
          Added.NormalYQ15 = QuantizeNormalQ15(Constraint.Normal.Y);
          State = &Added;
        }
        State->Lambda = FMath::Max(State->Lambda,
          static_cast<float>(ActiveLambda[ActiveIndex] * Scale));
      }
      return true;
    };

    for (const int32 Root : ComponentRoots)
    {
      if (!ProjectComponentFeasible(ComponentConstraints[Root]))
      {
        OutSummary.bValid = false;
        ++OutSummary.InfeasibleConstraintCount;
      }
    }
  }
  InOutDualStates = MoveTemp(NewDualStates);
  OutSummary.MaxResidualCm = 0.0f;
  for (const auto& Constraint : SortedConstraints)
  {
    int32 AgentAIndex = Constraint.MinAgentIndex;
    int32 AgentBIndex = Constraint.MaxAgentIndex;
    if (!Agents.IsValidIndex(AgentAIndex) || Agents[AgentAIndex].AgentId != Constraint.MinAgentId)
      AgentAIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Constraint.MinAgentId;
      });
    if (Constraint.MaxAgentId != INDEX_NONE
      && (!Agents.IsValidIndex(AgentBIndex) || Agents[AgentBIndex].AgentId != Constraint.MaxAgentId))
      AgentBIndex = Agents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Constraint.MaxAgentId;
      });
    if (!InOutPositions.IsValidIndex(AgentAIndex)
      || (Constraint.MaxAgentId != INDEX_NONE && !InOutPositions.IsValidIndex(AgentBIndex)))
      continue;
    const float Scale = FMath::Max(0.0001f, Constraint.CoefficientScale);
    const float Lhs = Scale * Dot2D(InOutPositions[AgentAIndex], Constraint.Normal)
      - (Constraint.MaxAgentId != INDEX_NONE
        ? Scale * Dot2D(InOutPositions[AgentBIndex], Constraint.Normal) : 0.0f);
    OutSummary.MaxResidualCm = FMath::Max(OutSummary.MaxResidualCm,
      FMath::Max(0.0f, Constraint.Threshold - Lhs));
  }
  if (OutSummary.MaxResidualCm > ConstraintEpsilonCm)
    OutSummary.bValid = false;
}

void FCrowdParticleConstraintKernel::Solve(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const FCrowdParticleConstraintEnvironment& Environment,
  const FCrowdParticleConstraintSettings& Settings,
  TArray<FCrowdParticleConstraintPair>& OutPairs,
  TArray<FCrowdParticleConstraintResult>& OutResults,
  FCrowdParticleConstraintSummary& OutSummary,
  FCrowdParticleConstraintTrace* OutTrace)
{
  OutPairs.Reset();
  OutResults.Reset();
  OutSummary = FCrowdParticleConstraintSummary();
  if (OutTrace) *OutTrace = FCrowdParticleConstraintTrace();
  if (Agents.IsEmpty())
  {
    OutSummary.bValid = true;
    return;
  }

  TArray<FCrowdParticleConstraintAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    const auto& Agent = SortedAgents[Index];
    if (Agent.AgentId == INDEX_NONE || Agent.PhysicalRadiusCm <= 0.0f || Agent.HardSafetyGapCm < 0.0f
      || Agent.EnvironmentHardClearanceCm < 0.0f
      || !FMath::IsFinite(Agent.EnvironmentHardClearanceCm)
      || Agent.SoftMarginCm < 0.0f || Agent.Mobility < 0.0f
      || (Index > 0 && SortedAgents[Index - 1].AgentId == Agent.AgentId))
      return;
  }

  TArray<FVector> Positions;
  TArray<int32> FirstInfluencedIterations;
  TArray<int32> CorrectedPairCounts;
  TArray<FEnvironmentSoftFact> EnvironmentSoftFacts;
  TArray<FEnvironmentContactFact> EnvironmentContactFacts;
  TArray<FUnifiedHardFact> UnifiedHardFacts;
  TArray<FCrowdParticleHardDualState> MainDualStates;
  TArray<FCrowdParticleHardDualState> SafetyDualStates;
  TArray<FVector> PairSoftRequestedCorrections;
  TArray<FVector> PairSoftRealizedCorrections;
  TArray<FVector> EnvironmentSoftRequestedCorrections;
  TArray<FVector> EnvironmentSoftRealizedCorrections;
  TArray<FVector> UnifiedHardCorrections;
  TSet<int32> EnvironmentSoftAppliedAgents;
  bool bEnvironmentInputValid = true;
  Positions.Reserve(SortedAgents.Num());
  FirstInfluencedIterations.Init(INDEX_NONE, SortedAgents.Num());
  CorrectedPairCounts.Init(0, SortedAgents.Num());
  PairSoftRequestedCorrections.Init(FVector::ZeroVector, SortedAgents.Num());
  PairSoftRealizedCorrections.Init(FVector::ZeroVector, SortedAgents.Num());
  EnvironmentSoftRequestedCorrections.Init(FVector::ZeroVector, SortedAgents.Num());
  EnvironmentSoftRealizedCorrections.Init(FVector::ZeroVector, SortedAgents.Num());
  UnifiedHardCorrections.Init(FVector::ZeroVector, SortedAgents.Num());
  TArray<FCrowdParticleSoftPairInfluence> SoftPairInfluences;
  for (const auto& Agent : SortedAgents) Positions.Add(Agent.PredictedPosition);
  if (OutTrace)
  {
    for (const auto& Agent : SortedAgents)
    {
      OutTrace->AgentIds.Add(Agent.AgentId);
      OutTrace->StartPositions.Add(Agent.StartPosition);
      OutTrace->PredictPositions.Add(Agent.PredictedPosition);
    }
  }

  const int32 IterationCount = FMath::Max(1, Settings.IterationCount);
  const float SoftIterationResponse = 1.0f - FMath::Exp(
    -FMath::Max(0.0f, Settings.SoftResponsePerSecond)
    * FMath::Max(0.0f, Settings.FixedStepSeconds)
    / static_cast<float>(IterationCount));
  for (int32 Iteration = 0; Iteration < IterationCount; ++Iteration)
  {
    BuildCandidatePairs(SortedAgents, Positions, OutPairs);
    OutSummary.CandidatePairCount = FMath::Max(OutSummary.CandidatePairCount, OutPairs.Num());

    for (const auto& Pair : OutPairs)
    {
      FVector Delta = Flatten2D(
        Positions[Pair.MinAgentIndex] - Positions[Pair.MaxAgentIndex]);
      float Distance = Delta.Size2D();
      const float SoftDistance = PairSoftDistance(
        SortedAgents[Pair.MinAgentIndex], SortedAgents[Pair.MaxAgentIndex]);
      if (Distance + ConstraintEpsilonCm >= SoftDistance) continue;
      const FVector Normal = Distance > SMALL_NUMBER
        ? Delta / Distance
        : MakeStablePairDirection(Pair.MinAgentId, Pair.MaxAgentId);
      const float Correction = FMath::Min(
        (SoftDistance - Distance) * SoftIterationResponse,
        FMath::Max(0.0f, Settings.SoftMaxPairCorrectionPerIterationCm));
      const FVector BeforeMin = Positions[Pair.MinAgentIndex];
      const FVector BeforeMax = Positions[Pair.MaxAgentIndex];
      ApplyPairCorrection(Pair, SortedAgents, Positions, Normal, Correction, Iteration,
        FirstInfluencedIterations, CorrectedPairCounts);
      if (Settings.bCaptureRouteDiagnostic)
      {
        const FVector MinDelta = Positions[Pair.MinAgentIndex] - BeforeMin;
        const FVector MaxDelta = Positions[Pair.MaxAgentIndex] - BeforeMax;
        PairSoftRequestedCorrections[Pair.MinAgentIndex] += MinDelta;
        PairSoftRequestedCorrections[Pair.MaxAgentIndex] += MaxDelta;
        PairSoftRealizedCorrections[Pair.MinAgentIndex] += MinDelta;
        PairSoftRealizedCorrections[Pair.MaxAgentIndex] += MaxDelta;
        auto* Influence = SoftPairInfluences.FindByPredicate([&](const auto& Candidate)
        {
          return Candidate.MinAgentId == Pair.MinAgentId
            && Candidate.MaxAgentId == Pair.MaxAgentId;
        });
        if (!Influence)
        {
          FCrowdParticleSoftPairInfluence& Added = SoftPairInfluences.AddDefaulted_GetRef();
          Added.MinAgentId = Pair.MinAgentId;
          Added.MaxAgentId = Pair.MaxAgentId;
          Influence = &Added;
        }
        Influence->RequestedCorrectionA += MinDelta;
        Influence->RequestedCorrectionB += MaxDelta;
      }
    }
    if (OutTrace && Iteration + 1 == IterationCount) OutTrace->SoftPositions = Positions;

    TArray<FCrowdParticleEnvironmentContact> SoftContacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, SoftContacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    for (const auto& Contact : SoftContacts)
    {
      FEnvironmentContactFact& Fact = EnvironmentContactFacts.AddDefaulted_GetRef();
      Fact.Iteration = Iteration;
      Fact.Stage = 0;
      Fact.Contact = Contact;
    }
    const int32 FirstSoftFact = EnvironmentSoftFacts.Num();
    for (const auto& Contact : SoftContacts)
    {
      if (Contact.ContactKind == ECrowdParticleEnvironmentContactKind::ObstacleSwept
        || Contact.SoftErrorCm <= ConstraintEpsilonCm
        || !SortedAgents.IsValidIndex(Contact.AgentIndex))
        continue;
      const float Requested = FMath::Min(
        Contact.SoftErrorCm * SoftIterationResponse,
        FMath::Max(0.0f, Settings.SoftMaxEnvironmentCorrectionPerIterationCm));
      if (Requested <= ConstraintEpsilonCm
        || SortedAgents[Contact.AgentIndex].Mobility <= SMALL_NUMBER)
        continue;
      FEnvironmentSoftFact& Fact = EnvironmentSoftFacts.AddDefaulted_GetRef();
      Fact.Iteration = Iteration;
      Fact.AgentId = Contact.AgentId;
      Fact.EnvironmentId = Contact.EnvironmentId;
      Fact.Kind = static_cast<int32>(Contact.ContactKind);
      Fact.Face = static_cast<int32>(Contact.Face);
      Fact.Normal = Contact.CorrectionNormal;
      Fact.BeforePosition = Positions[Contact.AgentIndex];
      Fact.ErrorCm = Contact.SoftErrorCm;
      Fact.RequestedCm = Requested;
      Positions[Contact.AgentIndex] += Contact.CorrectionNormal * Requested;
      if (Settings.bCaptureRouteDiagnostic)
        EnvironmentSoftRequestedCorrections[Contact.AgentIndex] +=
          Contact.CorrectionNormal * Requested;
      EnvironmentSoftAppliedAgents.Add(Contact.AgentId);
      if (FirstInfluencedIterations[Contact.AgentIndex] == INDEX_NONE)
        FirstInfluencedIterations[Contact.AgentIndex] = Iteration + 1;
      OutSummary.EnvironmentSoftRequestedCorrectionCmMax = FMath::Max(
        OutSummary.EnvironmentSoftRequestedCorrectionCmMax, Requested);
    }
    if (OutTrace && Iteration + 1 == IterationCount)
      OutTrace->EnvironmentSoftPositions = Positions;

    BuildCandidatePairs(SortedAgents, Positions, OutPairs);
    TArray<FCrowdParticleEnvironmentContact> HardContacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, HardContacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    TArray<FCrowdParticleHardConstraint> Constraints;
    BuildUnifiedHardConstraints(SortedAgents, Positions, OutPairs, HardContacts, Constraints);
    for (int32 ConstraintIndex = Constraints.Num() - 1; ConstraintIndex >= 0; --ConstraintIndex)
    {
      const auto& Constraint = Constraints[ConstraintIndex];
      if (Constraint.Kind != ECrowdParticleHardConstraintKind::PairEndpoint
        && Constraint.Kind != ECrowdParticleHardConstraintKind::PairSwept)
        continue;
      if (Constraint.InitialDeficitCm <= ConstraintEpsilonCm)
        Constraints.RemoveAt(ConstraintIndex);
    }
    // Main-loop contacts are re-linearized after both soft stages.  Their
    // thresholds therefore belong to a new local problem even when the
    // quantized normal happens to match the previous iteration.
    MainDualStates.Reset();
    const TArray<FVector> PreClosurePositions = Settings.bCaptureRouteDiagnostic
      ? Positions : TArray<FVector>();
    FCrowdParticleUnifiedHardSummary ClosureSummary;
    SolveUnifiedHardClosure(SortedAgents, Settings, Constraints, Positions,
      MainDualStates, ClosureSummary);
    if (Settings.bCaptureRouteDiagnostic)
      for (int32 AgentIndex = 0; AgentIndex < Positions.Num(); ++AgentIndex)
        UnifiedHardCorrections[AgentIndex] += Positions[AgentIndex]
          - PreClosurePositions[AgentIndex];
    OutSummary.UnifiedHardConstraintCount = FMath::Max(
      OutSummary.UnifiedHardConstraintCount, ClosureSummary.ConstraintCount);
    OutSummary.UnifiedHardInfeasibleCount += ClosureSummary.InfeasibleConstraintCount;
    OutSummary.UnifiedHardResidualCmMax = ClosureSummary.MaxResidualCm;
    for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
    {
      FUnifiedHardFact& Fact = UnifiedHardFacts.AddDefaulted_GetRef();
      Fact.Iteration = Iteration;
      Fact.Stage = 0;
      Fact.Constraint = Constraints[ConstraintIndex];
      if (const auto* Dual = MainDualStates.FindByPredicate([&](const auto& Candidate)
      {
        return SameDualKey(Candidate, Constraints[ConstraintIndex]);
      })) Fact.Dual = *Dual;
    }
    for (int32 FactIndex = FirstSoftFact; FactIndex < EnvironmentSoftFacts.Num(); ++FactIndex)
    {
      FEnvironmentSoftFact& Fact = EnvironmentSoftFacts[FactIndex];
      const int32 AgentIndex = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Fact.AgentId;
      });
      if (!Positions.IsValidIndex(AgentIndex)) continue;
      Fact.RealizedCm = FMath::Clamp(Dot2D(
        Positions[AgentIndex] - Fact.BeforePosition, Fact.Normal), 0.0f, Fact.RequestedCm);
      if (Settings.bCaptureRouteDiagnostic)
        EnvironmentSoftRealizedCorrections[AgentIndex] += Fact.Normal * Fact.RealizedCm;
      OutSummary.EnvironmentSoftRealizedCorrectionCmMax = FMath::Max(
        OutSummary.EnvironmentSoftRealizedCorrectionCmMax, Fact.RealizedCm);
    }
    if (OutTrace && Iteration + 1 == IterationCount)
    {
      OutTrace->UnifiedHardPositions = Positions;
      OutTrace->HardPositions = Positions;
      OutTrace->SweptPositions = Positions;
      OutTrace->ObstaclePositions = Positions;
    }
  }

  // The comfort shell is deliberately absent from this closure.  Once the
  // fixed-step soft response is complete, only non-compressible constraints
  // are allowed to change the quantized candidate.
  for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    Positions[AgentIndex] = QuantizeVector2D(Positions[AgentIndex], Settings.PositionQuantumCm);
  }
  if (OutTrace) OutTrace->QuantizedPositions = Positions;

  constexpr float HardProtectionBandCm = 2.0f;
  const int32 SafetyIterationCount = FMath::Max(1, Settings.SafetyIterationCount);
  const auto TryFastQuantizeHardComponents = [&](const TArray<FVector>& ContinuousPositions,
    const TArray<FCrowdParticleConstraintPair>& PairGraph,
    TArray<FVector>& OutQuantizedPositions)
  {
    const float Quantum = FMath::Max(Settings.PositionQuantumCm, KINDA_SMALL_NUMBER);
    if (ContinuousPositions.Num() != SortedAgents.Num()) return false;
    OutQuantizedPositions.SetNumUninitialized(ContinuousPositions.Num());
    for (int32 AgentIndex = 0; AgentIndex < ContinuousPositions.Num(); ++AgentIndex)
    {
      // Zero-mobility agents use the existing specialized lattice rule.  The
      // production crowd has positive mobility, so keep that uncommon case on
      // the complete path instead of duplicating its semantics here.
      if (SortedAgents[AgentIndex].Mobility <= SMALL_NUMBER) return false;
      const FVector& Continuous = ContinuousPositions[AgentIndex];
      const int32 FloorX = FMath::FloorToInt(Continuous.X / Quantum);
      const int32 CeilX = FMath::CeilToInt(Continuous.X / Quantum);
      const int32 FloorY = FMath::FloorToInt(Continuous.Y / Quantum);
      const int32 CeilY = FMath::CeilToInt(Continuous.Y / Quantum);
      bool bHasBest = false;
      double BestErrorSquared = 0.0;
      int32 BestX = 0;
      int32 BestY = 0;
      const int32 XValues[2] = {FloorX, CeilX};
      const int32 YValues[2] = {FloorY, CeilY};
      for (const int32 X : XValues)
      {
        for (const int32 Y : YValues)
        {
          const FVector Candidate(X * Quantum, Y * Quantum, Continuous.Z);
          const double ErrorSquared = FVector::DistSquared2D(Candidate, Continuous);
          const bool bBetter = !bHasBest
            || ErrorSquared < BestErrorSquared - 1.0e-9
            || (FMath::IsNearlyEqual(ErrorSquared, BestErrorSquared, 1.0e-9)
              && (X < BestX || (X == BestX && Y < BestY)));
          if (!bBetter) continue;
          bHasBest = true;
          BestErrorSquared = ErrorSquared;
          BestX = X;
          BestY = Y;
        }
      }
      OutQuantizedPositions[AgentIndex] = FVector(
        BestX * Quantum, BestY * Quantum, Continuous.Z);
    }

    TArray<FCrowdParticleEnvironmentContact> Contacts;
    if (!BuildEnvironmentContacts(SortedAgents, OutQuantizedPositions,
        Environment, Contacts)
      || Contacts.ContainsByPredicate([](const auto& Contact)
      {
        return Contact.HardDeficitCm > ConstraintEpsilonCm;
      }))
      return false;

    for (const auto& Pair : PairGraph)
    {
      if (!SortedAgents.IsValidIndex(Pair.MinAgentIndex)
        || !SortedAgents.IsValidIndex(Pair.MaxAgentIndex)) return false;
      const auto& A = SortedAgents[Pair.MinAgentIndex];
      const auto& B = SortedAgents[Pair.MaxAgentIndex];
      const float Required = PairHardDistance(A, B);
      if (FVector::Dist2D(OutQuantizedPositions[Pair.MinAgentIndex],
          OutQuantizedPositions[Pair.MaxAgentIndex]) + ConstraintEpsilonCm < Required)
        return false;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, OutQuantizedPositions[Pair.MinAgentIndex],
        B.StartPosition, OutQuantizedPositions[Pair.MaxAgentIndex],
        Pair.MinAgentId, Pair.MaxAgentId);
      if (IsSweptPairViolation(
        FVector::Dist2D(A.StartPosition, B.StartPosition),
        Swept.Distance, Required)) return false;
    }
    return true;
  };
  const auto QuantizeHardComponents = [&](const TArray<FVector>& ContinuousPositions,
    const TArray<FCrowdParticleConstraintPair>& PairGraph,
    TArray<FVector>& OutQuantizedPositions)
  {
    const float Quantum = FMath::Max(Settings.PositionQuantumCm, KINDA_SMALL_NUMBER);
    const int32 AgentCount = SortedAgents.Num();
    if (ContinuousPositions.Num() != AgentCount) return false;

    struct FLatticeCandidate
    {
      FVector Position = FVector::ZeroVector;
      double ErrorSquared = 0.0;
      int32 X = 0;
      int32 Y = 0;
    };
    TArray<TArray<FLatticeCandidate>> Candidates;
    Candidates.SetNum(AgentCount);
    for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
    {
      const FVector& Continuous = ContinuousPositions[AgentIndex];
      const int32 FloorX = FMath::FloorToInt(Continuous.X / Quantum);
      const int32 CeilX = FMath::CeilToInt(Continuous.X / Quantum);
      const int32 FloorY = FMath::FloorToInt(Continuous.Y / Quantum);
      const int32 CeilY = FMath::CeilToInt(Continuous.Y / Quantum);
      const int32 XValues[2] = {FloorX, CeilX};
      const int32 YValues[2] = {FloorY, CeilY};
      for (const int32 X : XValues)
      {
        for (const int32 Y : YValues)
        {
          if (Candidates[AgentIndex].ContainsByPredicate([&](const FLatticeCandidate& Existing)
          {
            return Existing.X == X && Existing.Y == Y;
          })) continue;
          FLatticeCandidate Candidate;
          Candidate.X = X;
          Candidate.Y = Y;
          Candidate.Position = FVector(X * Quantum, Y * Quantum, Continuous.Z);
          Candidate.ErrorSquared = FVector::DistSquared2D(Candidate.Position, Continuous);
          Candidates[AgentIndex].Add(Candidate);
        }
      }
      Candidates[AgentIndex].Sort([](const FLatticeCandidate& A, const FLatticeCandidate& B)
      {
        if (!FMath::IsNearlyEqual(A.ErrorSquared, B.ErrorSquared, 1.0e-9))
          return A.ErrorSquared < B.ErrorSquared;
        if (A.X != B.X) return A.X < B.X;
        return A.Y < B.Y;
      });
    }

    // Environment constraints are unary. Remove a lattice point if the exact
    // endpoint/sweep/bounds contract rejects it before the pair CSP is solved.
    for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
    {
      if (SortedAgents[AgentIndex].Mobility <= SMALL_NUMBER)
      {
        const FVector Fixed = QuantizeVector2D(
          ContinuousPositions[AgentIndex], Settings.PositionQuantumCm);
        Candidates[AgentIndex].RemoveAll([&](const FLatticeCandidate& Candidate)
        {
          return !Candidate.Position.Equals(Fixed, 0.001f);
        });
      }
      for (int32 CandidateIndex = Candidates[AgentIndex].Num() - 1;
        CandidateIndex >= 0; --CandidateIndex)
      {
        TArray<FVector> ProbePositions = ContinuousPositions;
        ProbePositions[AgentIndex] = Candidates[AgentIndex][CandidateIndex].Position;
        TArray<FCrowdParticleEnvironmentContact> ProbeContacts;
        if (!BuildEnvironmentContacts(SortedAgents, ProbePositions, Environment, ProbeContacts)
          || ProbeContacts.ContainsByPredicate([&](const auto& Contact)
          {
            return Contact.AgentIndex == AgentIndex
              && Contact.HardDeficitCm > ConstraintEpsilonCm;
          }))
        {
          Candidates[AgentIndex].RemoveAt(CandidateIndex);
        }
      }
      if (Candidates[AgentIndex].IsEmpty()) return false;
    }

    TArray<int32> Parents;
    Parents.SetNumUninitialized(AgentCount);
    for (int32 Index = 0; Index < AgentCount; ++Index) Parents[Index] = Index;
    const auto FindRoot = [&Parents](int32 Index)
    {
      while (Parents[Index] != Index) Index = Parents[Index];
      return Index;
    };
    const auto Union = [&Parents, &FindRoot, &SortedAgents](const int32 A, const int32 B)
    {
      const int32 RootA = FindRoot(A);
      const int32 RootB = FindRoot(B);
      if (RootA == RootB) return;
      if (SortedAgents[RootA].AgentId < SortedAgents[RootB].AgentId)
        Parents[RootB] = RootA;
      else
        Parents[RootA] = RootB;
    };
    for (const auto& Pair : PairGraph)
      if (SortedAgents.IsValidIndex(Pair.MinAgentIndex)
        && SortedAgents.IsValidIndex(Pair.MaxAgentIndex))
        Union(Pair.MinAgentIndex, Pair.MaxAgentIndex);

    TArray<TArray<int32>> PairIndicesByAgent;
    PairIndicesByAgent.SetNum(AgentCount);
    for (int32 PairIndex = 0; PairIndex < PairGraph.Num(); ++PairIndex)
    {
      const auto& Pair = PairGraph[PairIndex];
      if (!PairIndicesByAgent.IsValidIndex(Pair.MinAgentIndex)
        || !PairIndicesByAgent.IsValidIndex(Pair.MaxAgentIndex)) continue;
      PairIndicesByAgent[Pair.MinAgentIndex].Add(PairIndex);
      PairIndicesByAgent[Pair.MaxAgentIndex].Add(PairIndex);
    }
    const auto PairIsSafe = [&](const FCrowdParticleConstraintPair& Pair,
      const FVector& MinPosition, const FVector& MaxPosition)
    {
      const auto& A = SortedAgents[Pair.MinAgentIndex];
      const auto& B = SortedAgents[Pair.MaxAgentIndex];
      const float Required = PairHardDistance(A, B);
      if (FVector::Dist2D(MinPosition, MaxPosition) + ConstraintEpsilonCm < Required)
        return false;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, MinPosition, B.StartPosition, MaxPosition,
        Pair.MinAgentId, Pair.MaxAgentId);
      return !IsSweptPairViolation(
        FVector::Dist2D(A.StartPosition, B.StartPosition),
        Swept.Distance, Required);
    };

    OutQuantizedPositions = ContinuousPositions;
    TArray<bool> Assigned;
    Assigned.Init(false, AgentCount);
    TArray<TArray<int32>> Components;
    Components.SetNum(AgentCount);
    for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
      Components[FindRoot(AgentIndex)].Add(AgentIndex);
    TArray<int32> Roots;
    for (int32 Root = 0; Root < Components.Num(); ++Root)
      if (!Components[Root].IsEmpty()) Roots.Add(Root);
    Roots.Sort([&](const int32 A, const int32 B)
    {
      return SortedAgents[Components[A][0]].AgentId < SortedAgents[Components[B][0]].AgentId;
    });

    for (const int32 Root : Roots)
    {
      TArray<int32>& Component = Components[Root];
      Component.Sort([&](const int32 A, const int32 B)
      {
        const int32 DegreeA = PairIndicesByAgent[A].Num();
        const int32 DegreeB = PairIndicesByAgent[B].Num();
        if (DegreeA != DegreeB) return DegreeA > DegreeB;
        return SortedAgents[A].AgentId < SortedAgents[B].AgentId;
      });
      int32 SearchNodeCount = 0;
      constexpr int32 MaxSearchNodes = 1000000;
      TFunction<bool(int32)> AssignComponent = [&](const int32 LocalIndex)
      {
        if (++SearchNodeCount > MaxSearchNodes) return false;
        if (LocalIndex == Component.Num()) return true;
        const int32 AgentIndex = Component[LocalIndex];
        for (const FLatticeCandidate& Candidate : Candidates[AgentIndex])
        {
          bool bCompatible = true;
          for (const int32 PairIndex : PairIndicesByAgent[AgentIndex])
          {
            const auto& Pair = PairGraph[PairIndex];
            const int32 OtherIndex = Pair.MinAgentIndex == AgentIndex
              ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
            if (!Assigned[OtherIndex]) continue;
            const FVector& MinPosition = Pair.MinAgentIndex == AgentIndex
              ? Candidate.Position : OutQuantizedPositions[Pair.MinAgentIndex];
            const FVector& MaxPosition = Pair.MaxAgentIndex == AgentIndex
              ? Candidate.Position : OutQuantizedPositions[Pair.MaxAgentIndex];
            if (!PairIsSafe(Pair, MinPosition, MaxPosition))
            {
              bCompatible = false;
              break;
            }
          }
          if (!bCompatible) continue;

          // Stable forward checking prevents a locally cheap lattice choice
          // from consuming the last feasible corner of an unassigned neighbor.
          for (const int32 PairIndex : PairIndicesByAgent[AgentIndex])
          {
            const auto& Pair = PairGraph[PairIndex];
            const int32 OtherIndex = Pair.MinAgentIndex == AgentIndex
              ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
            if (Assigned[OtherIndex]) continue;
            const bool bNeighborHasCandidate = Candidates[OtherIndex].ContainsByPredicate(
              [&](const FLatticeCandidate& OtherCandidate)
              {
                const FVector& MinPosition = Pair.MinAgentIndex == AgentIndex
                  ? Candidate.Position : OtherCandidate.Position;
                const FVector& MaxPosition = Pair.MaxAgentIndex == AgentIndex
                  ? Candidate.Position : OtherCandidate.Position;
                return PairIsSafe(Pair, MinPosition, MaxPosition);
              });
            if (!bNeighborHasCandidate)
            {
              bCompatible = false;
              break;
            }
          }
          if (!bCompatible) continue;
          OutQuantizedPositions[AgentIndex] = Candidate.Position;
          Assigned[AgentIndex] = true;
          if (AssignComponent(LocalIndex + 1)) return true;
          Assigned[AgentIndex] = false;
        }
        return false;
      };
      if (!AssignComponent(0)) return false;
    }
    return true;
  };
  const auto AreHardConstraintsExactlySafe = [&](const TArray<FVector>& CandidatePositions)
  {
    if (CandidatePositions.Num() != SortedAgents.Num()) return false;
    TArray<FCrowdParticleConstraintPair> CandidatePairs;
    BuildCandidatePairs(SortedAgents, CandidatePositions, CandidatePairs);
    for (const FCrowdParticleConstraintPair& Pair : CandidatePairs)
    {
      if (!SortedAgents.IsValidIndex(Pair.MinAgentIndex)
        || !SortedAgents.IsValidIndex(Pair.MaxAgentIndex)) return false;
      const FCrowdParticleConstraintAgent& A = SortedAgents[Pair.MinAgentIndex];
      const FCrowdParticleConstraintAgent& B = SortedAgents[Pair.MaxAgentIndex];
      const float Required = PairHardDistance(A, B);
      if (FVector::Dist2D(CandidatePositions[Pair.MinAgentIndex],
          CandidatePositions[Pair.MaxAgentIndex]) + ConstraintEpsilonCm < Required)
        return false;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, CandidatePositions[Pair.MinAgentIndex],
        B.StartPosition, CandidatePositions[Pair.MaxAgentIndex],
        Pair.MinAgentId, Pair.MaxAgentId);
      if (IsSweptPairViolation(
        FVector::Dist2D(A.StartPosition, B.StartPosition),
        Swept.Distance, Required)) return false;
    }

    TArray<FCrowdParticleEnvironmentContact> CandidateContacts;
    return BuildEnvironmentContacts(
        SortedAgents, CandidatePositions, Environment, CandidateContacts)
      && !CandidateContacts.ContainsByPredicate([](const auto& Contact)
      {
        return Contact.HardDeficitCm > ConstraintEpsilonCm;
      });
  };
  const auto TryQuantizedProgressClosure = [&](const TArray<FVector>& CandidatePositions,
    TArray<FVector>& OutSafePositions)
  {
    if (CandidatePositions.Num() != SortedAgents.Num()) return false;
    constexpr int32 ProgressDenominator = 256;
    TArray<FVector> ProbePositions;
    ProbePositions.SetNumUninitialized(SortedAgents.Num());
    for (int32 ProgressNumerator = ProgressDenominator;
      ProgressNumerator >= 0; --ProgressNumerator)
    {
      const float Progress = static_cast<float>(ProgressNumerator)
        / static_cast<float>(ProgressDenominator);
      for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
      {
        const FVector& Start = SortedAgents[AgentIndex].StartPosition;
        const FVector& End = CandidatePositions[AgentIndex];
        const FVector ProgressPosition = SortedAgents[AgentIndex].Mobility <= SMALL_NUMBER
          ? Start
          : FMath::Lerp(Start, End, Progress);
        ProbePositions[AgentIndex] = QuantizeVector2D(
          ProgressPosition, Settings.PositionQuantumCm);
      }
      if (!AreHardConstraintsExactlySafe(ProbePositions)) continue;
      OutSafePositions = ProbePositions;
      return true;
    }

    // A fixed body can begin in an inherited overlap, so a single global
    // progress value may have no solution: one agent must finish evacuating
    // the fixed body while a neighbor yields to avoid a swept crossing. Build
    // a deterministic exceptional-path CSP over each agent's quantized
    // start-to-candidate progress samples.
    TArray<TArray<FVector>> ProgressCandidates;
    ProgressCandidates.SetNum(SortedAgents.Num());
    const auto IsCandidateEnvironmentSafe = [&](const int32 AgentIndex,
      const FVector& Candidate)
    {
      const FCrowdParticleConstraintAgent& Agent = SortedAgents[AgentIndex];
      if (IsOutsideParticleBounds(Agent, Environment, Candidate)) return false;
      const FCrowdSharedFlowConstraintResult Constraint = ConstrainParticleMovement(
        Agent, Environment, Candidate, Settings.FixedStepSeconds);
      return !Constraint.bPenetrating && !Constraint.bHitObstacle
        && Constraint.Location.Equals(Candidate, ConstraintEpsilonCm);
    };
    for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
    {
      const int32 FirstProgress = SortedAgents[AgentIndex].Mobility <= SMALL_NUMBER
        ? 0 : ProgressDenominator;
      for (int32 ProgressNumerator = FirstProgress;
        ProgressNumerator >= 0; --ProgressNumerator)
      {
        const float Progress = static_cast<float>(ProgressNumerator)
          / static_cast<float>(ProgressDenominator);
        const FVector& Start = SortedAgents[AgentIndex].StartPosition;
        const FVector ProgressPosition = SortedAgents[AgentIndex].Mobility <= SMALL_NUMBER
          ? Start
          : FMath::Lerp(Start, CandidatePositions[AgentIndex], Progress);
        const FVector Quantized = QuantizeVector2D(
          ProgressPosition, Settings.PositionQuantumCm);
        if (ProgressCandidates[AgentIndex].ContainsByPredicate(
          [&](const FVector& Existing) { return Existing.Equals(Quantized, 0.001f); }))
          continue;
        if (IsCandidateEnvironmentSafe(AgentIndex, Quantized))
          ProgressCandidates[AgentIndex].Add(Quantized);
      }

      if (SortedAgents[AgentIndex].Mobility > SMALL_NUMBER)
      {
        const FVector& Start = SortedAgents[AgentIndex].StartPosition;
        const FVector Displacement = CandidatePositions[AgentIndex] - Start;
        constexpr int32 RotatedProgressNumerators[] = {
          256, 224, 192, 160, 128, 96, 64, 32};
        for (int32 AngleStep = 1; AngleStep <= 32; ++AngleStep)
        {
          const int32 SignCount = AngleStep == 32 ? 1 : 2;
          for (int32 SignIndex = 0; SignIndex < SignCount; ++SignIndex)
          {
            const float Sign = SignIndex == 0 ? 1.0f : -1.0f;
            const float Radians = Sign * PI * static_cast<float>(AngleStep) / 32.0f;
            const float Cos = FMath::Cos(Radians);
            const float Sin = FMath::Sin(Radians);
            const FVector Rotated(
              Displacement.X * Cos - Displacement.Y * Sin,
              Displacement.X * Sin + Displacement.Y * Cos,
              Displacement.Z);
            for (const int32 ProgressNumerator : RotatedProgressNumerators)
            {
              const float Progress = static_cast<float>(ProgressNumerator)
                / static_cast<float>(ProgressDenominator);
              const FVector Quantized = QuantizeVector2D(
                Start + Rotated * Progress, Settings.PositionQuantumCm);
              if (ProgressCandidates[AgentIndex].ContainsByPredicate(
                [&](const FVector& Existing)
                {
                  return Existing.Equals(Quantized, 0.001f);
                })) continue;
              if (IsCandidateEnvironmentSafe(AgentIndex, Quantized))
                ProgressCandidates[AgentIndex].Add(Quantized);
            }
          }
        }
      }
      if (ProgressCandidates[AgentIndex].IsEmpty()) return false;
    }

    TArray<FCrowdParticleConstraintPair> ProgressPairs;
    for (int32 AIndex = 0; AIndex < SortedAgents.Num(); ++AIndex)
    {
      for (int32 BIndex = AIndex + 1; BIndex < SortedAgents.Num(); ++BIndex)
      {
        if (SortedAgents[AIndex].InteractionLayer
          != SortedAgents[BIndex].InteractionLayer) continue;
        FCrowdParticleConstraintPair& Pair = ProgressPairs.AddDefaulted_GetRef();
        Pair.MinAgentIndex = AIndex;
        Pair.MaxAgentIndex = BIndex;
        Pair.MinAgentId = SortedAgents[AIndex].AgentId;
        Pair.MaxAgentId = SortedAgents[BIndex].AgentId;
      }
    }

    TArray<TArray<int32>> PairIndicesByAgent;
    PairIndicesByAgent.SetNum(SortedAgents.Num());
    for (int32 PairIndex = 0; PairIndex < ProgressPairs.Num(); ++PairIndex)
    {
      const FCrowdParticleConstraintPair& Pair = ProgressPairs[PairIndex];
      PairIndicesByAgent[Pair.MinAgentIndex].Add(PairIndex);
      PairIndicesByAgent[Pair.MaxAgentIndex].Add(PairIndex);
    }
    const auto PairIsSafe = [&](const FCrowdParticleConstraintPair& Pair,
      const FVector& MinPosition, const FVector& MaxPosition)
    {
      const FCrowdParticleConstraintAgent& A = SortedAgents[Pair.MinAgentIndex];
      const FCrowdParticleConstraintAgent& B = SortedAgents[Pair.MaxAgentIndex];
      const float Required = PairHardDistance(A, B);
      if (FVector::Dist2D(MinPosition, MaxPosition)
        + ConstraintEpsilonCm < Required) return false;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, MinPosition, B.StartPosition, MaxPosition,
        Pair.MinAgentId, Pair.MaxAgentId);
      return !IsSweptPairViolation(
        FVector::Dist2D(A.StartPosition, B.StartPosition),
        Swept.Distance, Required);
    };

    for (int32 OrderPass = 0; OrderPass < 4; ++OrderPass)
    {
      TArray<int32> Order;
      for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
        Order.Add(AgentIndex);
      Order.Sort([&](const int32 A, const int32 B)
      {
        const bool bFixedA = SortedAgents[A].Mobility <= SMALL_NUMBER;
        const bool bFixedB = SortedAgents[B].Mobility <= SMALL_NUMBER;
        if (bFixedA != bFixedB) return bFixedA;
        if (OrderPass < 2
          && PairIndicesByAgent[A].Num() != PairIndicesByAgent[B].Num())
          return PairIndicesByAgent[A].Num() > PairIndicesByAgent[B].Num();
        return (OrderPass & 1) == 0
          ? SortedAgents[A].AgentId < SortedAgents[B].AgentId
          : SortedAgents[A].AgentId > SortedAgents[B].AgentId;
      });

      TArray<FVector> TrialPositions = CandidatePositions;
      TArray<bool> Assigned;
      Assigned.Init(false, SortedAgents.Num());
      bool bOrderSucceeded = true;
      for (const int32 AgentIndex : Order)
      {
        bool bAssignedCandidate = false;
        for (const FVector& Candidate : ProgressCandidates[AgentIndex])
        {
          bool bCompatible = true;
          for (const int32 PairIndex : PairIndicesByAgent[AgentIndex])
          {
            const FCrowdParticleConstraintPair& Pair = ProgressPairs[PairIndex];
            const int32 OtherIndex = Pair.MinAgentIndex == AgentIndex
              ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
            if (!Assigned[OtherIndex]) continue;
            const FVector& MinPosition = Pair.MinAgentIndex == AgentIndex
              ? Candidate : TrialPositions[Pair.MinAgentIndex];
            const FVector& MaxPosition = Pair.MaxAgentIndex == AgentIndex
              ? Candidate : TrialPositions[Pair.MaxAgentIndex];
            if (!PairIsSafe(Pair, MinPosition, MaxPosition))
            {
              bCompatible = false;
              break;
            }
          }
          if (!bCompatible) continue;
          TrialPositions[AgentIndex] = Candidate;
          Assigned[AgentIndex] = true;
          bAssignedCandidate = true;
          break;
        }
        if (bAssignedCandidate) continue;
        bOrderSucceeded = false;
        break;
      }
      if (!bOrderSucceeded || !AreHardConstraintsExactlySafe(TrialPositions))
      {
        TrialPositions = CandidatePositions;
        Assigned.Init(false, SortedAgents.Num());
        int32 SearchNodeCount = 0;
        constexpr int32 MaxBoundedSearchNodes = 1000;
        TFunction<bool(int32)> AssignBounded = [&](const int32 LocalIndex)
        {
          if (++SearchNodeCount > MaxBoundedSearchNodes) return false;
          if (LocalIndex == Order.Num()) return true;
          const int32 AgentIndex = Order[LocalIndex];
          for (const FVector& Candidate : ProgressCandidates[AgentIndex])
          {
            bool bCompatible = true;
            for (const int32 PairIndex : PairIndicesByAgent[AgentIndex])
            {
              const FCrowdParticleConstraintPair& Pair = ProgressPairs[PairIndex];
              const int32 OtherIndex = Pair.MinAgentIndex == AgentIndex
                ? Pair.MaxAgentIndex : Pair.MinAgentIndex;
              if (!Assigned[OtherIndex]) continue;
              const FVector& MinPosition = Pair.MinAgentIndex == AgentIndex
                ? Candidate : TrialPositions[Pair.MinAgentIndex];
              const FVector& MaxPosition = Pair.MaxAgentIndex == AgentIndex
                ? Candidate : TrialPositions[Pair.MaxAgentIndex];
              if (!PairIsSafe(Pair, MinPosition, MaxPosition))
              {
                bCompatible = false;
                break;
              }
            }
            if (!bCompatible) continue;
            TrialPositions[AgentIndex] = Candidate;
            Assigned[AgentIndex] = true;
            if (AssignBounded(LocalIndex + 1)) return true;
            Assigned[AgentIndex] = false;
          }
          return false;
        };
        if (!AssignBounded(0)
          || !AreHardConstraintsExactlySafe(TrialPositions)) continue;
      }
      OutSafePositions = MoveTemp(TrialPositions);
      return true;
    }
    return false;
  };
  const auto CaptureSafetyStage = [&](const int32 SafetyIteration,
    const ECrowdParticleSafetyStage Stage)
  {
    if (!OutTrace || !Settings.bCaptureSafetyStageTrace) return;
    FCrowdParticleSafetyStageTrace& StageTrace =
      OutTrace->SafetyStages.AddDefaulted_GetRef();
    StageTrace.Iteration = SafetyIteration;
    StageTrace.Stage = Stage;
    TArray<FCrowdParticleConstraintPair> StagePairs;
    BuildCandidatePairs(SortedAgents, Positions, StagePairs);
    for (const auto& Pair : StagePairs)
    {
      const auto& A = SortedAgents[Pair.MinAgentIndex];
      const auto& B = SortedAgents[Pair.MaxAgentIndex];
      const float Required = PairHardDistance(A, B);
      const float EndpointMargin = FVector::Dist2D(
        Positions[Pair.MinAgentIndex], Positions[Pair.MaxAgentIndex]) - Required;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, Positions[Pair.MinAgentIndex],
        B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
      const float SweptMargin = Swept.Distance - Required;
      StageTrace.MinimumEndpointMarginCm = FMath::Min(
        StageTrace.MinimumEndpointMarginCm, EndpointMargin);
      StageTrace.MinimumSweptMarginCm = FMath::Min(
        StageTrace.MinimumSweptMarginCm, SweptMargin);
      if (EndpointMargin < -ConstraintEpsilonCm) ++StageTrace.HardPairViolationCount;
      if (IsSweptPairViolation(
        FVector::Dist2D(A.StartPosition, B.StartPosition),
        Swept.Distance, Required)) ++StageTrace.SweptPairViolationCount;
    }
    TArray<FCrowdParticleEnvironmentContact> StageContacts;
    if (BuildEnvironmentContacts(SortedAgents, Positions, Environment, StageContacts))
    {
      for (const auto& Contact : StageContacts)
      {
        if (Contact.HardDeficitCm <= ConstraintEpsilonCm) continue;
        StageTrace.MaximumEnvironmentDeficitCm = FMath::Max(
          StageTrace.MaximumEnvironmentDeficitCm, Contact.HardDeficitCm);
        if (Contact.ContactKind == ECrowdParticleEnvironmentContactKind::FlowBounds)
          ++StageTrace.BoundsViolationCount;
        else
          ++StageTrace.ObstacleViolationCount;
      }
    }
    uint32 Hash = 2166136261u;
    for (int32 Index = 0; Index < Positions.Num(); ++Index)
    {
      Hash = FoldHash(Hash, SortedAgents[Index].AgentId);
      Hash = FoldHash(Hash, FMath::RoundToInt(Positions[Index].X));
      Hash = FoldHash(Hash, FMath::RoundToInt(Positions[Index].Y));
    }
    StageTrace.PositionHash = Hash;
  };
  for (int32 SafetyIteration = 0; SafetyIteration < SafetyIterationCount; ++SafetyIteration)
  {
    // Quantization and prior projections change both pair normals and the
    // earliest swept/environment witness.  Rebuild the complete local problem
    // at every fixed safety iteration; reusing the first linearization is not
    // a valid closure of the nonlinear hard constraints.
    BuildCandidatePairs(SortedAgents, Positions, OutPairs);
    TArray<FCrowdParticleEnvironmentContact> SafetyContacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, SafetyContacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    for (const auto& Contact : SafetyContacts)
    {
      FEnvironmentContactFact& Fact = EnvironmentContactFacts.AddDefaulted_GetRef();
      Fact.Iteration = SafetyIteration;
      Fact.Stage = 1;
      Fact.Contact = Contact;
    }
    TArray<FCrowdParticleHardConstraint> SafetyConstraints;
    BuildUnifiedHardConstraints(SortedAgents, Positions, OutPairs,
      SafetyContacts, SafetyConstraints);
    for (int32 ConstraintIndex = SafetyConstraints.Num() - 1;
      ConstraintIndex >= 0; --ConstraintIndex)
    {
      const auto& Constraint = SafetyConstraints[ConstraintIndex];
      if (Constraint.Kind == ECrowdParticleHardConstraintKind::PairSwept
        && Constraint.InitialDeficitCm < -HardProtectionBandCm)
        SafetyConstraints.RemoveAt(ConstraintIndex);
    }
    // A continuous boundary solution can round back to the same illegal 1 cm
    // lattice point forever. Add a lattice-interior cut only for constraints
    // that are actually violated at this rebuilt iteration. Inflating every
    // safe constraint simultaneously is intentionally forbidden because it
    // can make an otherwise feasible connected component inconsistent.
    for (auto& Constraint : SafetyConstraints)
    {
      if (Constraint.InitialDeficitCm <= ConstraintEpsilonCm) continue;
      const float LatticeRepairMargin = Constraint.MaxAgentId != INDEX_NONE
        ? FMath::Sqrt(2.0f) * FMath::Max(0.0f, Settings.PositionQuantumCm)
          + ConstraintEpsilonCm
        : FMath::Sqrt(0.5f) * FMath::Max(0.0f, Settings.PositionQuantumCm)
          + ConstraintEpsilonCm;
      Constraint.Threshold += LatticeRepairMargin;
      Constraint.InitialDeficitCm += LatticeRepairMargin;
    }
    CaptureSafetyStage(SafetyIteration, ECrowdParticleSafetyStage::Input);

    // Contacts are rebuilt above. SolveUnifiedHardClosure only inherits a
    // dual when the stable constraint key and quantized normal still match;
    // changed witnesses therefore start at zero while an unchanged local
    // half-plane can continue the deterministic Hildreth closure.
    const TArray<FVector> SafetyIterationInput = Positions;
    FCrowdParticleUnifiedHardSummary ClosureSummary;
    SolveUnifiedHardClosure(SortedAgents, Settings, SafetyConstraints, Positions,
      SafetyDualStates, ClosureSummary, SafetyIteration);
    OutSummary.UnifiedHardConstraintCount = FMath::Max(
      OutSummary.UnifiedHardConstraintCount, ClosureSummary.ConstraintCount);
    OutSummary.UnifiedHardInfeasibleCount += ClosureSummary.InfeasibleConstraintCount;
    OutSummary.UnifiedHardResidualCmMax = ClosureSummary.MaxResidualCm;
    for (int32 ConstraintIndex = 0; ConstraintIndex < SafetyConstraints.Num(); ++ConstraintIndex)
    {
      FUnifiedHardFact& Fact = UnifiedHardFacts.AddDefaulted_GetRef();
      Fact.Iteration = SafetyIteration;
      Fact.Stage = 1;
      Fact.Constraint = SafetyConstraints[ConstraintIndex];
      if (const auto* Dual = SafetyDualStates.FindByPredicate([&](const auto& Candidate)
      {
        return SameDualKey(Candidate, SafetyConstraints[ConstraintIndex]);
      })) Fact.Dual = *Dual;
    }
    CaptureSafetyStage(SafetyIteration, ECrowdParticleSafetyStage::UnifiedHard);
    TArray<FCrowdParticleConstraintPair> LatticePairGraph;
    BuildCandidatePairs(SortedAgents, Positions, LatticePairGraph);
    TArray<FVector> JointQuantizedPositions;
    if (TryFastQuantizeHardComponents(
        Positions, LatticePairGraph, JointQuantizedPositions)
      || QuantizeHardComponents(Positions, LatticePairGraph, JointQuantizedPositions))
    {
      Positions = MoveTemp(JointQuantizedPositions);
    }
    else
    {
      for (int32 AgentIndex = 0; AgentIndex < Positions.Num(); ++AgentIndex)
        Positions[AgentIndex] = QuantizeVector2DAlongCorrection(
          Positions[AgentIndex], SafetyIterationInput[AgentIndex],
          Settings.PositionQuantumCm);
    }
    CaptureSafetyStage(SafetyIteration, ECrowdParticleSafetyStage::Quantized);
    if (OutTrace && SafetyIteration + 1 == SafetyIterationCount)
    {
      OutTrace->FinalEnvironmentContacts = SafetyContacts;
      OutTrace->FinalHardConstraints = SafetyConstraints;
    }
  }
  // The iterative half-plane closure and component lattice CSP are local
  // solvers.  A later component correction can introduce a new nonlinear
  // swept witness that was absent from an earlier pair graph.  Validate the
  // entire quantized result once more and, only on that exceptional path,
  // retain the greatest globally safe fraction of this tick's movement.
  // The descending fixed grid is deterministic and handles non-monotonic
  // one-centimetre rounding without relying on an invalid binary search.
  if (!AreHardConstraintsExactlySafe(Positions))
  {
    TArray<FVector> SafeProgressPositions;
    if (TryQuantizedProgressClosure(Positions, SafeProgressPositions))
      Positions = MoveTemp(SafeProgressPositions);
  }
  if (OutTrace) OutTrace->FinalSafetyPositions = Positions;
  BuildCandidatePairs(SortedAgents, Positions, OutPairs);
  OutSummary.CandidatePairCount = OutPairs.Num();

  TArray<FCrowdParticleEnvironmentContact> FinalEnvironmentContacts;
  if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, FinalEnvironmentContacts))
    bEnvironmentInputValid = false;
  for (const auto& Contact : FinalEnvironmentContacts)
  {
    FEnvironmentContactFact& Fact = EnvironmentContactFacts.AddDefaulted_GetRef();
    Fact.Iteration = 0;
    Fact.Stage = 2;
    Fact.Contact = Contact;
  }
  TArray<float> EnvironmentSoftErrors;
  for (const auto& Contact : FinalEnvironmentContacts)
  {
    if (Contact.ContactKind == ECrowdParticleEnvironmentContactKind::ObstacleSwept
      || Contact.SoftErrorCm <= ConstraintEpsilonCm)
      continue;
    ++OutSummary.EnvironmentSoftContactCount;
    EnvironmentSoftErrors.Add(Contact.SoftErrorCm);
  }
  OutSummary.EnvironmentSoftAppliedAgentCount = EnvironmentSoftAppliedAgents.Num();
  OutSummary.EnvironmentSoftErrorCmP50 = Percentile(EnvironmentSoftErrors, 0.50f);
  OutSummary.EnvironmentSoftErrorCmP95 = Percentile(EnvironmentSoftErrors, 0.95f);
  OutSummary.EnvironmentSoftErrorCmMax = EnvironmentSoftErrors.IsEmpty()
    ? 0.0f : FMath::Max(EnvironmentSoftErrors);
  if (OutTrace)
  {
    OutTrace->FinalEnvironmentContacts = FinalEnvironmentContacts;
    TArray<FCrowdParticleHardConstraint> FinalConstraints;
    BuildUnifiedHardConstraints(SortedAgents, Positions, OutPairs,
      FinalEnvironmentContacts, FinalConstraints);
    OutTrace->FinalHardConstraints = MoveTemp(FinalConstraints);
  }

  TArray<float> SoftErrors;
  for (const auto& Pair : OutPairs)
  {
    const auto& A = SortedAgents[Pair.MinAgentIndex];
    const auto& B = SortedAgents[Pair.MaxAgentIndex];
    const float EndDistance = FVector::Dist2D(Positions[Pair.MinAgentIndex], Positions[Pair.MaxAgentIndex]);
    const float SoftDistance = PairSoftDistance(A, B);
    ++OutSummary.SoftPairCount;
    const float SoftError = FMath::Max(0.0f, SoftDistance - EndDistance);
    SoftErrors.Add(SoftError);
    if (SoftError > ConstraintEpsilonCm) ++OutSummary.SoftViolatingPairCount;
    if (EndDistance + ConstraintEpsilonCm < PairHardDistance(A, B))
      ++OutSummary.HardPairViolationCount;
    const FSweptDistance Swept = EvaluateSweptDistance(
      A.StartPosition, Positions[Pair.MinAgentIndex],
      B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
    if (IsSweptPairViolation(
      FVector::Dist2D(A.StartPosition, B.StartPosition),
      Swept.Distance, PairHardDistance(A, B)))
      ++OutSummary.SweptPairViolationCount;
  }
  OutSummary.SoftErrorCmP50 = Percentile(SoftErrors, 0.50f);
  OutSummary.SoftErrorCmP95 = Percentile(SoftErrors, 0.95f);
  OutSummary.SoftErrorCmMax = SoftErrors.IsEmpty() ? 0.0f : FMath::Max(SoftErrors);

  if (OutTrace && Settings.bCaptureRouteDiagnostic)
  {
    OutTrace->PairSoftRequestedCorrections = MoveTemp(PairSoftRequestedCorrections);
    OutTrace->PairSoftRealizedCorrections = MoveTemp(PairSoftRealizedCorrections);
    OutTrace->EnvironmentSoftRequestedCorrections = MoveTemp(EnvironmentSoftRequestedCorrections);
    OutTrace->EnvironmentSoftRealizedCorrections = MoveTemp(EnvironmentSoftRealizedCorrections);
    OutTrace->UnifiedHardCorrections = MoveTemp(UnifiedHardCorrections);
    SoftPairInfluences.Sort([](const auto& A, const auto& B)
    {
      if (A.MinAgentId != B.MinAgentId) return A.MinAgentId < B.MinAgentId;
      return A.MaxAgentId < B.MaxAgentId;
    });
    for (auto& Influence : SoftPairInfluences)
    {
      const int32 MinIndex = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
        { return Agent.AgentId == Influence.MinAgentId; });
      const int32 MaxIndex = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
        { return Agent.AgentId == Influence.MaxAgentId; });
      auto Retained = [&](const int32 AgentIndex, const FVector& Requested)
      {
        const float RequestedLength = Requested.Size2D();
        if (AgentIndex == INDEX_NONE || RequestedLength <= KINDA_SMALL_NUMBER)
          return FVector::ZeroVector;
        const FVector Direction = Requested / RequestedLength;
        const FVector Net = Positions[AgentIndex] - SortedAgents[AgentIndex].PredictedPosition;
        return Direction * FMath::Clamp(FVector::DotProduct(Net, Direction), 0.0f, RequestedLength);
      };
      Influence.RealizedCorrectionA = Retained(MinIndex, Influence.RequestedCorrectionA);
      Influence.RealizedCorrectionB = Retained(MaxIndex, Influence.RequestedCorrectionB);
    }
    OutTrace->SoftPairInfluences = MoveTemp(SoftPairInfluences);
    OutTrace->ActiveNeighborAgentIds.SetNum(SortedAgents.Num());
    for (const auto& Pair : OutPairs)
    {
      const auto& A = SortedAgents[Pair.MinAgentIndex];
      const auto& B = SortedAgents[Pair.MaxAgentIndex];
      const float EndDistance = FVector::Dist2D(
        Positions[Pair.MinAgentIndex], Positions[Pair.MaxAgentIndex]);
      const float SoftError = PairSoftDistance(A, B) - EndDistance;
      const FSweptDistance Swept = EvaluateSweptDistance(
        A.StartPosition, Positions[Pair.MinAgentIndex],
        B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
      const bool bActive = SoftError > ConstraintEpsilonCm
        || EndDistance + ConstraintEpsilonCm < PairHardDistance(A, B)
        || IsSweptPairViolation(
          FVector::Dist2D(A.StartPosition, B.StartPosition),
          Swept.Distance, PairHardDistance(A, B));
      if (!bActive) continue;
      OutTrace->ActiveNeighborAgentIds[Pair.MinAgentIndex].Add(Pair.MaxAgentId);
      OutTrace->ActiveNeighborAgentIds[Pair.MaxAgentIndex].Add(Pair.MinAgentId);
    }
    for (TArray<int32>& Neighbors : OutTrace->ActiveNeighborAgentIds)
    {
      Neighbors.Sort();
      Neighbors.SetNum(Algo::Unique(Neighbors));
    }
  }

  OutResults.Reserve(SortedAgents.Num());
  for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    const auto& Agent = SortedAgents[AgentIndex];
    const FCrowdSharedFlowConstraintResult EnvironmentConstraint = ConstrainParticleMovement(
      Agent, Environment, Positions[AgentIndex], Settings.FixedStepSeconds);
    if (EnvironmentConstraint.bPenetrating || EnvironmentConstraint.bHitObstacle
      || FCrowdSharedFlowFieldKernel::IsInsideInflatedObstacle(
        [&]() { auto C = Environment.FlowConfig;
          C.AgentInflateCm = AgentEnvironmentHardDistance(Agent); return C; }(),
        Positions[AgentIndex]))
      ++OutSummary.ObstaclePenetrationCount;
    if (IsOutsideParticleBounds(Agent, Environment, Positions[AgentIndex]))
      ++OutSummary.BoundsViolationCount;
    FCrowdParticleConstraintResult& Result = OutResults.AddDefaulted_GetRef();
    Result.AgentId = Agent.AgentId;
    Result.CorrectedPosition = Positions[AgentIndex];
    Result.RealizedCorrection = Positions[AgentIndex] - Agent.PredictedPosition;
    Result.CorrectedVelocity = Settings.FixedStepSeconds > SMALL_NUMBER
      ? QuantizeVector2D((Positions[AgentIndex] - Agent.StartPosition) / Settings.FixedStepSeconds,
        Settings.VelocityQuantumCmps)
      : FVector::ZeroVector;
    Result.FirstInfluencedIteration = FirstInfluencedIterations[AgentIndex];
    Result.CorrectedPairCount = CorrectedPairCounts[AgentIndex];
    const float CorrectionCm = Result.RealizedCorrection.Size2D();
    if (CorrectionCm > ConstraintEpsilonCm) ++OutSummary.CorrectedAgentCount;
    if (Result.FirstInfluencedIteration != INDEX_NONE)
    {
      ++OutSummary.PressureInfluencedAgentCount;
      OutSummary.FirstInfluencedIterationMax = FMath::Max(
        OutSummary.FirstInfluencedIterationMax, Result.FirstInfluencedIteration);
    }
    OutSummary.MaxAgentCorrectionCm = FMath::Max(OutSummary.MaxAgentCorrectionCm, CorrectionCm);
  }

  uint32 Hash = FoldHash(2166136261u, 5); // Particle candidate contract v5: diagnostics are excluded.
  const auto FoldFloat = [&Hash](const float Value, const float Scale)
  {
    Hash = FoldHash(Hash, FMath::RoundToInt(Value * Scale));
  };
  const auto FoldVector = [&Hash](const FVector& Value)
  {
    Hash = FoldHash(Hash, FMath::RoundToInt(Value.X * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Value.Y * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Value.Z * 1000.0f));
  };
  FoldFloat(Settings.FixedStepSeconds, 1000000.0f);
  Hash = FoldHash(Hash, IterationCount);
  Hash = FoldHash(Hash, SafetyIterationCount);
  FoldFloat(Settings.SoftResponsePerSecond, 1000.0f);
  FoldFloat(Settings.SoftMaxPairCorrectionPerIterationCm, 1000.0f);
  FoldFloat(Settings.SoftMaxEnvironmentCorrectionPerIterationCm, 1000.0f);
  FoldFloat(Settings.HardMaxPairCorrectionPerIterationCm, 1000.0f);
  FoldFloat(Settings.PositionQuantumCm, 1000.0f);
  FoldFloat(Settings.VelocityQuantumCmps, 1000.0f);
  Hash = FoldHash(Hash, Environment.bConstrainToFlowBounds ? 1 : 0);
  Hash = FoldHash(Hash, Environment.FlowConfig.Revision);
  Hash = FoldHash(Hash, Environment.FlowConfig.ConnectivityContractVersion);
  FoldVector(FVector(Environment.FlowConfig.BoundsMin));
  FoldVector(FVector(Environment.FlowConfig.BoundsMax));
  FoldFloat(Environment.FlowConfig.CellSizeCm, 1000.0f);
  FoldFloat(Environment.FlowConfig.AgentInflateCm, 1000.0f);
  FoldVector(FVector(Environment.FlowConfig.GoalLocation));
  TArray<FCrowdSharedFlowObstacleSpec> SortedObstacles =
    Environment.FlowConfig.ObstacleSpecs;
  SortedObstacles.Sort([](const auto& A, const auto& B)
  {
    if (A.ObstacleId != B.ObstacleId) return A.ObstacleId < B.ObstacleId;
    const FVector AC = FVector(A.Center);
    const FVector BC = FVector(B.Center);
    if (!FMath::IsNearlyEqual(AC.X, BC.X)) return AC.X < BC.X;
    return AC.Y < BC.Y;
  });
  Hash = FoldHash(Hash, SortedObstacles.Num());
  for (const auto& Obstacle : SortedObstacles)
  {
    Hash = FoldHash(Hash, Obstacle.ObstacleId);
    FoldVector(FVector(Obstacle.Center));
    FoldVector(FVector(Obstacle.Extent));
  }
  Hash = FoldHash(Hash, SortedAgents.Num());
  const bool bHasExplicitInteractionLayers =
    SortedAgents.ContainsByPredicate([](const auto& Agent)
    {
      return Agent.InteractionLayer != 0;
    });
  if (bHasExplicitInteractionLayers)
    Hash = FoldHash(Hash, 0x4c415952u);
  for (const auto& Agent : SortedAgents)
  {
    Hash = FoldHash(Hash, Agent.AgentId);
    if (bHasExplicitInteractionLayers)
      Hash = FoldHash(Hash, Agent.InteractionLayer);
    FoldVector(Agent.StartPosition);
    FoldVector(Agent.PredictedPosition);
    FoldFloat(Agent.PhysicalRadiusCm, 1000.0f);
    FoldFloat(Agent.HardSafetyGapCm, 1000.0f);
    FoldFloat(Agent.EnvironmentHardClearanceCm, 1000.0f);
    FoldFloat(Agent.SoftMarginCm, 1000.0f);
    FoldFloat(Agent.Mobility, 32767.0f);
  }
  for (const auto& Result : OutResults)
  {
    Hash = FoldHash(Hash, Result.AgentId);
    Hash = FoldHash(Hash, FMath::RoundToInt(Result.CorrectedPosition.X));
    Hash = FoldHash(Hash, FMath::RoundToInt(Result.CorrectedPosition.Y));
    Hash = FoldHash(Hash, FMath::RoundToInt(Result.CorrectedVelocity.X));
    Hash = FoldHash(Hash, FMath::RoundToInt(Result.CorrectedVelocity.Y));
    Hash = FoldHash(Hash, Result.FirstInfluencedIteration);
  }
  for (const auto& Pair : OutPairs)
  {
    Hash = FoldHash(Hash, Pair.MinAgentId);
    Hash = FoldHash(Hash, Pair.MaxAgentId);
  }
  // Trace facts are intentionally excluded: bCaptureRouteDiagnostic controls
  // their allocation and detail and must never alter the authoritative result.
  Hash = FoldHash(Hash, bEnvironmentInputValid ? 1 : 0);
  Hash = FoldHash(Hash, OutSummary.CandidatePairCount);
  Hash = FoldHash(Hash, OutSummary.SoftPairCount);
  Hash = FoldHash(Hash, OutSummary.SoftViolatingPairCount);
  Hash = FoldHash(Hash, OutSummary.HardPairViolationCount);
  Hash = FoldHash(Hash, OutSummary.SweptPairViolationCount);
  Hash = FoldHash(Hash, OutSummary.ObstaclePenetrationCount);
  Hash = FoldHash(Hash, OutSummary.BoundsViolationCount);
  Hash = FoldHash(Hash, OutSummary.EnvironmentSoftContactCount);
  Hash = FoldHash(Hash, OutSummary.EnvironmentSoftAppliedAgentCount);
  Hash = FoldHash(Hash, OutSummary.UnifiedHardConstraintCount);
  Hash = FoldHash(Hash, OutSummary.UnifiedHardInfeasibleCount);
  Hash = FoldHash(Hash, OutSummary.PressureInfluencedAgentCount);
  Hash = FoldHash(Hash, OutSummary.FirstInfluencedIterationMax);
  Hash = FoldHash(Hash, OutSummary.CorrectedAgentCount);
  FoldFloat(OutSummary.SoftErrorCmP50, 1000.0f);
  FoldFloat(OutSummary.SoftErrorCmP95, 1000.0f);
  FoldFloat(OutSummary.SoftErrorCmMax, 1000.0f);
  FoldFloat(OutSummary.EnvironmentSoftErrorCmP50, 1000.0f);
  FoldFloat(OutSummary.EnvironmentSoftErrorCmP95, 1000.0f);
  FoldFloat(OutSummary.EnvironmentSoftErrorCmMax, 1000.0f);
  FoldFloat(OutSummary.EnvironmentSoftRequestedCorrectionCmMax, 1000.0f);
  FoldFloat(OutSummary.EnvironmentSoftRealizedCorrectionCmMax, 1000.0f);
  FoldFloat(OutSummary.UnifiedHardResidualCmMax, 1000.0f);
  FoldFloat(OutSummary.MaxAgentCorrectionCm, 1000.0f);
  OutSummary.CandidateHash = Hash;
  OutSummary.bValid = bEnvironmentInputValid
    && OutSummary.HardPairViolationCount == 0
    && OutSummary.SweptPairViolationCount == 0
    && OutSummary.ObstaclePenetrationCount == 0
    && OutSummary.BoundsViolationCount == 0;
}

void FCrowdParticleConstraintKernel::AdvanceSettlingTracker(
  FCrowdParticleSettlingTracker& Tracker,
  const float MaxActualCorrectionCm,
  const float SoftErrorCmP95)
{
  ++Tracker.StepCount;
  const bool bSettledSample = MaxActualCorrectionCm <= 1.0f
    && Tracker.PreviousSoftErrorCmP95 >= 0.0f
    && FMath::Abs(SoftErrorCmP95 - Tracker.PreviousSoftErrorCmP95) <= 1.0f;
  Tracker.ConsecutiveSettledSampleCount = bSettledSample
    ? Tracker.ConsecutiveSettledSampleCount + 1 : 0;
  if (Tracker.SettlingSteps == INDEX_NONE && Tracker.ConsecutiveSettledSampleCount >= 15)
    Tracker.SettlingSteps = Tracker.StepCount - 14;
  Tracker.PreviousSoftErrorCmP95 = SoftErrorCmP95;
}

void FCrowdParticleConstraintKernel::BuildFailureFixture(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const TConstArrayView<FCrowdParticleAppliedState> AppliedStates,
  const FCrowdParticleConstraintTrace& Trace,
  const int32 FixedStepIndex,
  const uint32 CandidateHash,
  const uint32 AppliedStateHash,
  FCrowdParticleFailureFixture& OutFixture)
{
  OutFixture = FCrowdParticleFailureFixture();
  if (Agents.Num() != AppliedStates.Num() || Trace.AgentIds.Num() != Agents.Num()
    || Trace.StartPositions.Num() != Agents.Num()
    || Trace.PredictPositions.Num() != Agents.Num()
    || Trace.SoftPositions.Num() != Agents.Num()
    || Trace.EnvironmentSoftPositions.Num() != Agents.Num()
    || Trace.UnifiedHardPositions.Num() != Agents.Num()
    || Trace.HardPositions.Num() != Agents.Num()
    || Trace.SweptPositions.Num() != Agents.Num()
    || Trace.ObstaclePositions.Num() != Agents.Num()
    || Trace.QuantizedPositions.Num() != Agents.Num()
    || Trace.FinalSafetyPositions.Num() != Agents.Num()) return;

  TArray<FCrowdParticleConstraintAgent> SortedAgents(Agents);
  TArray<FCrowdParticleAppliedState> SortedApplied(AppliedStates);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  SortedApplied.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
    if (Trace.AgentIds[Index] != SortedAgents[Index].AgentId
      || SortedApplied[Index].AgentId != SortedAgents[Index].AgentId) return;

  int32 FailureA = INDEX_NONE;
  int32 FailureB = INDEX_NONE;
  bool bHard = false;
  bool bSwept = false;
  float RequiredHard = 0.0f;
  float EndpointDistance = 0.0f;
  float SweptDistance = 0.0f;
  for (int32 A = 0; A < SortedAgents.Num() && FailureA == INDEX_NONE; ++A)
  {
    for (int32 B = A + 1; B < SortedAgents.Num(); ++B)
    {
      const float Required = PairHardDistance(SortedAgents[A], SortedAgents[B]);
      const float Endpoint = FVector::Dist2D(
        Trace.FinalSafetyPositions[A], Trace.FinalSafetyPositions[B]);
      const FSweptDistance Swept = EvaluateSweptDistance(
        SortedAgents[A].StartPosition, Trace.FinalSafetyPositions[A],
        SortedAgents[B].StartPosition, Trace.FinalSafetyPositions[B],
        SortedAgents[A].AgentId, SortedAgents[B].AgentId);
      const bool bPairHard = Endpoint + ConstraintEpsilonCm < Required;
      const bool bPairSwept = IsSweptPairViolation(
        FVector::Dist2D(SortedAgents[A].StartPosition,
          SortedAgents[B].StartPosition),
        Swept.Distance, Required);
      if (!bPairHard && !bPairSwept) continue;
      FailureA = A;
      FailureB = B;
      bHard = bPairHard;
      bSwept = bPairSwept;
      RequiredHard = Required;
      EndpointDistance = Endpoint;
      SweptDistance = Swept.Distance;
      break;
    }
  }
  if (FailureA != INDEX_NONE)
  {
    const ECrowdParticleHardConstraintKind FailureKind = bHard
      ? ECrowdParticleHardConstraintKind::PairEndpoint
      : ECrowdParticleHardConstraintKind::PairSwept;
    if (const FCrowdParticleHardConstraint* Constraint =
      Trace.FinalHardConstraints.FindByPredicate([&](const auto& Candidate)
      {
        return Candidate.Kind == FailureKind
          && Candidate.MinAgentId == SortedAgents[FailureA].AgentId
          && Candidate.MaxAgentId == SortedAgents[FailureB].AgentId;
      }))
    {
      OutFixture.bHasFirstFailureConstraint = true;
      OutFixture.FirstFailureConstraint = *Constraint;
      OutFixture.FirstFailureConstraintKind = static_cast<int32>(Constraint->Kind);
    }
  }
  else
  {
    const FCrowdParticleEnvironmentContact* Contact =
      Trace.FinalEnvironmentContacts.FindByPredicate([](const auto& Candidate)
      {
        return Candidate.HardDeficitCm > ConstraintEpsilonCm;
      });
    if (!Contact) return;
    FailureA = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
    {
      return Agent.AgentId == Contact->AgentId;
    });
    if (FailureA == INDEX_NONE) return;
    OutFixture.bHasFirstFailureContact = true;
    OutFixture.FirstFailureContact = *Contact;
    OutFixture.FirstFailureEnvironmentId = Contact->EnvironmentId;
    RequiredHard = Contact->HardDistanceCm;
    if (const FCrowdParticleHardConstraint* Constraint =
      Trace.FinalHardConstraints.FindByPredicate([&](const auto& Candidate)
      {
        return Candidate.MinAgentId == Contact->AgentId
          && Candidate.MaxAgentId == INDEX_NONE
          && Candidate.EnvironmentId == Contact->EnvironmentId
          && Candidate.Face == Contact->Face;
      }))
    {
      OutFixture.bHasFirstFailureConstraint = true;
      OutFixture.FirstFailureConstraint = *Constraint;
      OutFixture.FirstFailureConstraintKind = static_cast<int32>(Constraint->Kind);
    }
  }
  if (!OutFixture.bHasFirstFailureContact)
  {
    if (const FCrowdParticleEnvironmentContact* Contact =
      Trace.FinalEnvironmentContacts.FindByPredicate([](const auto& Candidate)
      {
        return Candidate.HardDeficitCm > ConstraintEpsilonCm;
      }))
    {
      OutFixture.bHasFirstFailureContact = true;
      OutFixture.FirstFailureContact = *Contact;
      OutFixture.FirstFailureEnvironmentId = Contact->EnvironmentId;
    }
  }

  OutFixture.FixedStepIndex = FixedStepIndex;
  OutFixture.MinAgentId = SortedAgents[FailureA].AgentId;
  OutFixture.MaxAgentId = FailureB != INDEX_NONE
    ? SortedAgents[FailureB].AgentId : INDEX_NONE;
  OutFixture.bHardViolation = bHard;
  OutFixture.bSweptViolation = bSwept;
  OutFixture.RequiredHardDistanceCm = RequiredHard;
  OutFixture.FinalEndpointDistanceCm = EndpointDistance;
  OutFixture.FinalSweptDistanceCm = SweptDistance;
  OutFixture.CandidateHash = CandidateHash;
  OutFixture.AppliedStateHash = AppliedStateHash;
  OutFixture.SolveAgents = SortedAgents;

  TArray<int32> FailureIndices = {FailureA};
  if (FailureB != INDEX_NONE) FailureIndices.Add(FailureB);
  if (OutFixture.bHasFirstFailureContact)
  {
    const int32 ContactAgentIndex = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
    {
      return Agent.AgentId == OutFixture.FirstFailureContact.AgentId;
    });
    if (ContactAgentIndex != INDEX_NONE) FailureIndices.AddUnique(ContactAgentIndex);
  }
  for (const int32 Index : FailureIndices)
  {
    FCrowdParticleFailureFixtureAgent& FixtureAgent = OutFixture.Agents.AddDefaulted_GetRef();
    const auto& Agent = SortedAgents[Index];
    FixtureAgent.AgentId = Agent.AgentId;
    FixtureAgent.PhysicalRadiusCm = Agent.PhysicalRadiusCm;
    FixtureAgent.HardSafetyGapCm = Agent.HardSafetyGapCm;
    FixtureAgent.SoftMarginCm = Agent.SoftMarginCm;
    FixtureAgent.Mobility = Agent.Mobility;
    FixtureAgent.Start = Trace.StartPositions[Index];
    FixtureAgent.Predict = Trace.PredictPositions[Index];
    FixtureAgent.Soft = Trace.SoftPositions[Index];
    FixtureAgent.EnvironmentSoft = Trace.EnvironmentSoftPositions[Index];
    FixtureAgent.UnifiedHard = Trace.UnifiedHardPositions[Index];
    FixtureAgent.Hard = Trace.HardPositions[Index];
    FixtureAgent.Swept = Trace.SweptPositions[Index];
    FixtureAgent.Obstacle = Trace.ObstaclePositions[Index];
    FixtureAgent.Quantized = Trace.QuantizedPositions[Index];
    FixtureAgent.FinalSafety = Trace.FinalSafetyPositions[Index];
    FixtureAgent.Applied = SortedApplied[Index].Position;
  }

  uint32 Hash = FoldHash(2166136261u, 2); // Failure fixture contract v2.
  Hash = FoldHash(Hash, FixedStepIndex);
  Hash = FoldHash(Hash, OutFixture.MinAgentId);
  Hash = FoldHash(Hash, OutFixture.MaxAgentId);
  Hash = FoldHash(Hash, bHard ? 1 : 0);
  Hash = FoldHash(Hash, bSwept ? 1 : 0);
  Hash = FoldHash(Hash, FMath::RoundToInt(RequiredHard));
  Hash = FoldHash(Hash, FMath::RoundToInt(EndpointDistance));
  Hash = FoldHash(Hash, FMath::RoundToInt(SweptDistance));
  Hash = FoldHash(Hash, static_cast<int32>(CandidateHash));
  Hash = FoldHash(Hash, static_cast<int32>(AppliedStateHash));
  Hash = FoldHash(Hash, OutFixture.FirstFailureEnvironmentId);
  Hash = FoldHash(Hash, OutFixture.FirstFailureConstraintKind);
  Hash = FoldHash(Hash, OutFixture.bHasFirstFailureContact ? 1 : 0);
  if (OutFixture.bHasFirstFailureContact)
  {
    const auto& Contact = OutFixture.FirstFailureContact;
    Hash = FoldHash(Hash, Contact.AgentId);
    Hash = FoldHash(Hash, Contact.EnvironmentId);
    Hash = FoldHash(Hash, static_cast<int32>(Contact.ContactKind));
    Hash = FoldHash(Hash, static_cast<int32>(Contact.Face));
    Hash = FoldHash(Hash, QuantizeNormalQ15(Contact.CorrectionNormal.X));
    Hash = FoldHash(Hash, QuantizeNormalQ15(Contact.CorrectionNormal.Y));
    Hash = FoldHash(Hash, FMath::RoundToInt(Contact.HardDistanceCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Contact.SoftDistanceCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Contact.HardDeficitCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Contact.SweptTime * 1000000.0f));
  }
  Hash = FoldHash(Hash, OutFixture.bHasFirstFailureConstraint ? 1 : 0);
  if (OutFixture.bHasFirstFailureConstraint)
  {
    const auto& Constraint = OutFixture.FirstFailureConstraint;
    Hash = FoldHash(Hash, static_cast<int32>(Constraint.Kind));
    Hash = FoldHash(Hash, Constraint.MinAgentId);
    Hash = FoldHash(Hash, Constraint.MaxAgentId);
    Hash = FoldHash(Hash, Constraint.EnvironmentId);
    Hash = FoldHash(Hash, static_cast<int32>(Constraint.Face));
    Hash = FoldHash(Hash, QuantizeNormalQ15(Constraint.Normal.X));
    Hash = FoldHash(Hash, QuantizeNormalQ15(Constraint.Normal.Y));
    Hash = FoldHash(Hash, FMath::RoundToInt(Constraint.CoefficientScale * 1000000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Constraint.Threshold * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Constraint.InitialDeficitCm * 1000.0f));
  }
  Hash = FoldHash(Hash, OutFixture.SolveAgents.Num());
  for (const auto& Agent : OutFixture.SolveAgents)
  {
    Hash = FoldHash(Hash, Agent.AgentId);
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.StartPosition.X));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.StartPosition.Y));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.StartPosition.Z));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.PredictedPosition.X));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.PredictedPosition.Y));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.PredictedPosition.Z));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.PhysicalRadiusCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.HardSafetyGapCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.SoftMarginCm * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(Agent.Mobility * 32767.0f));
  }
  for (const auto& Agent : OutFixture.Agents)
  {
    Hash = FoldHash(Hash, Agent.AgentId);
    const FVector Stages[] = {Agent.Start, Agent.Predict, Agent.Soft,
      Agent.EnvironmentSoft, Agent.UnifiedHard, Agent.Hard, Agent.Swept,
      Agent.Obstacle, Agent.Quantized, Agent.FinalSafety, Agent.Applied};
    for (const FVector& Position : Stages)
    {
      Hash = FoldHash(Hash, FMath::RoundToInt(Position.X));
      Hash = FoldHash(Hash, FMath::RoundToInt(Position.Y));
    }
  }
  OutFixture.FixtureHash = Hash;
  OutFixture.bValid = true;
}

void FCrowdParticleConstraintKernel::EvaluateAppliedState(
  const TConstArrayView<FCrowdParticleConstraintAgent> Agents,
  const TConstArrayView<FCrowdParticleAppliedState> AppliedStates,
  const FCrowdParticleConstraintEnvironment& Environment,
  FCrowdParticleConstraintSummary& OutSummary,
  uint32& OutAppliedStateHash)
{
  OutSummary = FCrowdParticleConstraintSummary();
  OutAppliedStateHash = 2166136261u;
  if (Agents.Num() != AppliedStates.Num()) return;

  TArray<FCrowdParticleConstraintAgent> SortedAgents(Agents);
  TArray<FCrowdParticleAppliedState> SortedStates(AppliedStates);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  SortedStates.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TArray<FVector> Positions;
  Positions.Reserve(SortedStates.Num());
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
  {
    if (SortedStates[Index].AgentId != SortedAgents[Index].AgentId) return;
    Positions.Add(SortedStates[Index].Position);
    OutAppliedStateHash = FoldHash(OutAppliedStateHash, SortedStates[Index].AgentId);
    OutAppliedStateHash = FoldHash(OutAppliedStateHash, FMath::RoundToInt(SortedStates[Index].Position.X));
    OutAppliedStateHash = FoldHash(OutAppliedStateHash, FMath::RoundToInt(SortedStates[Index].Position.Y));
    OutAppliedStateHash = FoldHash(OutAppliedStateHash, FMath::RoundToInt(SortedStates[Index].Velocity.X));
    OutAppliedStateHash = FoldHash(OutAppliedStateHash, FMath::RoundToInt(SortedStates[Index].Velocity.Y));
    const float CorrectionCm = FVector::Dist2D(
      SortedStates[Index].Position, SortedAgents[Index].PredictedPosition);
    if (CorrectionCm > ConstraintEpsilonCm) ++OutSummary.CorrectedAgentCount;
    OutSummary.MaxAgentCorrectionCm = FMath::Max(OutSummary.MaxAgentCorrectionCm, CorrectionCm);
    if (FCrowdSharedFlowFieldKernel::IsInsideInflatedObstacle(
      [&]() { auto C = Environment.FlowConfig;
        C.AgentInflateCm = AgentEnvironmentHardDistance(SortedAgents[Index]); return C; }(),
      SortedStates[Index].Position))
      ++OutSummary.ObstaclePenetrationCount;
    if (IsOutsideParticleBounds(SortedAgents[Index], Environment, SortedStates[Index].Position))
      ++OutSummary.BoundsViolationCount;
  }

  TArray<FCrowdParticleConstraintPair> Pairs;
  BuildCandidatePairs(SortedAgents, Positions, Pairs);
  OutSummary.CandidatePairCount = Pairs.Num();
  TArray<float> SoftErrors;
  for (const auto& Pair : Pairs)
  {
    const auto& A = SortedAgents[Pair.MinAgentIndex];
    const auto& B = SortedAgents[Pair.MaxAgentIndex];
    const float EndDistance = FVector::Dist2D(
      Positions[Pair.MinAgentIndex], Positions[Pair.MaxAgentIndex]);
    const float SoftError = FMath::Max(0.0f, PairSoftDistance(A, B) - EndDistance);
    ++OutSummary.SoftPairCount;
    SoftErrors.Add(SoftError);
    if (SoftError > ConstraintEpsilonCm) ++OutSummary.SoftViolatingPairCount;
    if (EndDistance + ConstraintEpsilonCm < PairHardDistance(A, B))
      ++OutSummary.HardPairViolationCount;
    const FSweptDistance Swept = EvaluateSweptDistance(
      A.StartPosition, Positions[Pair.MinAgentIndex],
      B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
    if (IsSweptPairViolation(
      FVector::Dist2D(A.StartPosition, B.StartPosition),
      Swept.Distance, PairHardDistance(A, B)))
      ++OutSummary.SweptPairViolationCount;
  }
  OutSummary.SoftErrorCmP50 = Percentile(SoftErrors, 0.50f);
  OutSummary.SoftErrorCmP95 = Percentile(SoftErrors, 0.95f);
  OutSummary.SoftErrorCmMax = SoftErrors.IsEmpty() ? 0.0f : FMath::Max(SoftErrors);
  OutSummary.bValid = OutSummary.HardPairViolationCount == 0
    && OutSummary.SweptPairViolationCount == 0
    && OutSummary.ObstaclePenetrationCount == 0
    && OutSummary.BoundsViolationCount == 0;
}
