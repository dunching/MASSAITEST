#include "Mass/CrowdDemoParticleConstraintKernel.h"

#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  constexpr float ConstraintEpsilonCm = 0.01f;

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
    const FCrowdDemoParticleConstraintAgent& A,
    const FCrowdDemoParticleConstraintAgent& B)
  {
    return FMath::Max(1.0f, A.PhysicalRadiusCm + B.PhysicalRadiusCm
      + FMath::Max(A.HardSafetyGapCm, B.HardSafetyGapCm));
  }

  float PairSoftDistance(
    const FCrowdDemoParticleConstraintAgent& A,
    const FCrowdDemoParticleConstraintAgent& B)
  {
    return PairHardDistance(A, B)
      + FMath::Max(0.0f, A.SoftMarginCm)
      + FMath::Max(0.0f, B.SoftMarginCm);
  }

  FVector ContactFaceNormal(const ECrowdDemoParticleEnvironmentFace Face)
  {
    switch (Face)
    {
    case ECrowdDemoParticleEnvironmentFace::MinX: return FVector(-1.0f, 0.0f, 0.0f);
    case ECrowdDemoParticleEnvironmentFace::MaxX: return FVector(1.0f, 0.0f, 0.0f);
    case ECrowdDemoParticleEnvironmentFace::MinY: return FVector(0.0f, -1.0f, 0.0f);
    case ECrowdDemoParticleEnvironmentFace::MaxY: return FVector(0.0f, 1.0f, 0.0f);
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
    const ECrowdDemoParticleEnvironmentFace Face)
  {
    switch (Face)
    {
    case ECrowdDemoParticleEnvironmentFace::MinX: return Position.X - Min.X;
    case ECrowdDemoParticleEnvironmentFace::MaxX: return Max.X - Position.X;
    case ECrowdDemoParticleEnvironmentFace::MinY: return Position.Y - Min.Y;
    case ECrowdDemoParticleEnvironmentFace::MaxY: return Max.Y - Position.Y;
    default: return TNumericLimits<float>::Max();
    }
  }

  ECrowdDemoParticleEnvironmentFace SelectEscapeFace(
    const FVector& Position,
    const FVector& Min,
    const FVector& Max)
  {
    ECrowdDemoParticleEnvironmentFace BestFace = ECrowdDemoParticleEnvironmentFace::MinX;
    float BestDistance = FaceEscapeDistance(Position, Min, Max, BestFace);
    for (int32 FaceValue = 1; FaceValue <= 3; ++FaceValue)
    {
      const auto Face = static_cast<ECrowdDemoParticleEnvironmentFace>(FaceValue);
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
    const ECrowdDemoParticleEnvironmentFace Face)
  {
    FVector Result = Position;
    switch (Face)
    {
    case ECrowdDemoParticleEnvironmentFace::MinX:
      Result.X = Min.X;
      Result.Y = FMath::Clamp(Position.Y, Min.Y, Max.Y);
      break;
    case ECrowdDemoParticleEnvironmentFace::MaxX:
      Result.X = Max.X;
      Result.Y = FMath::Clamp(Position.Y, Min.Y, Max.Y);
      break;
    case ECrowdDemoParticleEnvironmentFace::MinY:
      Result.X = FMath::Clamp(Position.X, Min.X, Max.X);
      Result.Y = Min.Y;
      break;
    case ECrowdDemoParticleEnvironmentFace::MaxY:
      Result.X = FMath::Clamp(Position.X, Min.X, Max.X);
      Result.Y = Max.Y;
      break;
    }
    return Result;
  }

  float FaceThreshold(
    const FVector& Min,
    const FVector& Max,
    const ECrowdDemoParticleEnvironmentFace Face,
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
    ECrowdDemoParticleEnvironmentFace& OutFace)
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
    ECrowdDemoParticleEnvironmentFace EntryFace = ECrowdDemoParticleEnvironmentFace::MinX;
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
      ECrowdDemoParticleEnvironmentFace NearFace = Axis == 0
        ? ECrowdDemoParticleEnvironmentFace::MinX : ECrowdDemoParticleEnvironmentFace::MinY;
      if (NearTime > FarTime)
      {
        Swap(NearTime, FarTime);
        NearFace = Axis == 0
          ? ECrowdDemoParticleEnvironmentFace::MaxX : ECrowdDemoParticleEnvironmentFace::MaxY;
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
    const FCrowdDemoParticleHardDualState& State,
    const FCrowdDemoParticleHardConstraint& Constraint)
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
    const FCrowdDemoParticleHardConstraint& A,
    const FCrowdDemoParticleHardConstraint& B)
  {
    if (A.Kind != B.Kind) return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
    if (A.MinAgentId != B.MinAgentId) return A.MinAgentId < B.MinAgentId;
    if (A.MaxAgentId != B.MaxAgentId) return A.MaxAgentId < B.MaxAgentId;
    if (A.EnvironmentId != B.EnvironmentId) return A.EnvironmentId < B.EnvironmentId;
    if (A.Face != B.Face) return static_cast<uint8>(A.Face) < static_cast<uint8>(B.Face);
    const int32 AX = QuantizeNormalQ15(A.Normal.X);
    const int32 BX = QuantizeNormalQ15(B.Normal.X);
    if (AX != BX) return AX < BX;
    return QuantizeNormalQ15(A.Normal.Y) < QuantizeNormalQ15(B.Normal.Y);
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
    const FVector RelativeStart = StartA - StartB;
    const FVector RelativeDelta = (EndA - StartA) - (EndB - StartB);
    const float DeltaSizeSq = RelativeDelta.SizeSquared2D();
    FSweptDistance Result;
    Result.Time = DeltaSizeSq > SMALL_NUMBER
      ? FMath::Clamp(-FVector::DotProduct(RelativeStart, RelativeDelta) / DeltaSizeSq, 0.0f, 1.0f)
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

  bool ApplyPairCorrection(
    const FCrowdDemoParticleConstraintPair& Pair,
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
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

  FCrowdDemoSharedFlowConstraintResult ConstrainParticleMovement(
    const FCrowdDemoParticleConstraintAgent& Agent,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    const FVector& Proposed,
    const float FixedStepSeconds)
  {
    FCrowdDemoSharedFlowFieldConfig Config = Environment.FlowConfig;
    Config.AgentInflateCm = FMath::Max(0.0f, Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm);
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
    return FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
      Config, Agent.StartPosition, DomainProposed, FixedStepSeconds, false);
  }

  bool IsOutsideParticleBounds(
    const FCrowdDemoParticleConstraintAgent& Agent,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    const FVector& Position)
  {
    if (!Environment.bConstrainToFlowBounds) return false;
    const float Clearance = FMath::Max(0.0f, Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm);
    return Position.X < Environment.FlowConfig.BoundsMin.X + Clearance - ConstraintEpsilonCm
      || Position.X > Environment.FlowConfig.BoundsMax.X - Clearance + ConstraintEpsilonCm
      || Position.Y < Environment.FlowConfig.BoundsMin.Y + Clearance - ConstraintEpsilonCm
      || Position.Y > Environment.FlowConfig.BoundsMax.Y - Clearance + ConstraintEpsilonCm;
  }

  struct FEnvironmentAwareProjectionFact
  {
    int32 Iteration = 0;
    int32 Stage = 0;
    int32 MinAgentId = INDEX_NONE;
    int32 MaxAgentId = INDEX_NONE;
    float RequestedCm = 0.0f;
    float CapacityMinCm = 0.0f;
    float CapacityMaxCm = 0.0f;
    float ActualMinCm = 0.0f;
    float ActualMaxCm = 0.0f;
  };

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

  struct FUnifiedHardFact
  {
    int32 Iteration = 0;
    int32 Stage = 0;
    FCrowdDemoParticleHardConstraint Constraint;
    FCrowdDemoParticleHardDualState Dual;
  };

  bool ApplyEnvironmentAwarePairCorrection(
    const FCrowdDemoParticleConstraintPair& Pair,
    TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
    const FCrowdDemoParticleConstraintEnvironment& Environment,
    const FCrowdDemoParticleConstraintSettings& Settings,
    TArray<FVector>& Positions,
    const FVector& Normal,
    const float RequiredCm,
    const float RequestedCm,
    const int32 Iteration,
    const int32 Stage,
    TArray<int32>& FirstInfluencedIteration,
    TArray<int32>& CorrectedPairCounts,
    TArray<FEnvironmentAwareProjectionFact>& OutFacts)
  {
    FEnvironmentAwareProjectionFact& Fact = OutFacts.AddDefaulted_GetRef();
    Fact.Iteration = Iteration;
    Fact.Stage = Stage;
    Fact.MinAgentId = Pair.MinAgentId;
    Fact.MaxAgentId = Pair.MaxAgentId;
    Fact.RequestedCm = FMath::Max(0.0f, RequestedCm);
    if (Fact.RequestedCm <= ConstraintEpsilonCm) return false;

    const auto& AgentA = Agents[Pair.MinAgentIndex];
    const auto& AgentB = Agents[Pair.MaxAgentIndex];
    const float MobilityA = FMath::Max(0.0f, AgentA.Mobility);
    const float MobilityB = FMath::Max(0.0f, AgentB.Mobility);
    const float TotalMobility = MobilityA + MobilityB;
    if (TotalMobility <= SMALL_NUMBER) return false;

    const FVector StartA = Positions[Pair.MinAgentIndex];
    const FVector StartB = Positions[Pair.MaxAgentIndex];
    const auto ConstrainQuantized = [&](const FCrowdDemoParticleConstraintAgent& Agent,
      const FVector& Proposed)
    {
      FVector Result = ConstrainParticleMovement(
        Agent, Environment, Proposed, Settings.FixedStepSeconds).Location;
      Result = QuantizeVector2D(Result, Settings.PositionQuantumCm);
      return ConstrainParticleMovement(
        Agent, Environment, Result, Settings.FixedStepSeconds).Location;
    };
    if (MobilityA > SMALL_NUMBER)
    {
      const FVector CapacityPosition = ConstrainQuantized(
        AgentA, StartA + Normal * Fact.RequestedCm);
      Fact.CapacityMinCm = FMath::Clamp(
        FVector::DotProduct(CapacityPosition - StartA, Normal), 0.0f, Fact.RequestedCm);
    }
    if (MobilityB > SMALL_NUMBER)
    {
      const FVector CapacityPosition = ConstrainQuantized(
        AgentB, StartB - Normal * Fact.RequestedCm);
      Fact.CapacityMaxCm = FMath::Clamp(
        FVector::DotProduct(CapacityPosition - StartB, -Normal), 0.0f, Fact.RequestedCm);
    }

    const float DesiredA = Fact.RequestedCm * MobilityA / TotalMobility;
    float AllocationA = 0.0f;
    float AllocationB = 0.0f;
    if (Fact.CapacityMinCm + Fact.CapacityMaxCm + ConstraintEpsilonCm >= Fact.RequestedCm)
    {
      const float MinA = FMath::Max(0.0f, Fact.RequestedCm - Fact.CapacityMaxCm);
      const float MaxA = FMath::Min(Fact.CapacityMinCm, Fact.RequestedCm);
      AllocationA = FMath::Clamp(DesiredA, MinA, MaxA);
      AllocationB = Fact.RequestedCm - AllocationA;
    }
    else
    {
      AllocationA = Fact.CapacityMinCm;
      AllocationB = Fact.CapacityMaxCm;
    }

    const auto EvaluateAllocation = [&](const float CandidateA, FVector& OutA, FVector& OutB,
      float& OutActualA, float& OutActualB)
    {
      const float CandidateB = Fact.RequestedCm - CandidateA;
      OutA = CandidateA > ConstraintEpsilonCm
        ? ConstrainQuantized(AgentA, StartA + Normal * CandidateA) : StartA;
      OutB = CandidateB > ConstraintEpsilonCm
        ? ConstrainQuantized(AgentB, StartB - Normal * CandidateB) : StartB;
      OutActualA = FMath::Max(0.0f, FVector::DotProduct(OutA - StartA, Normal));
      OutActualB = FMath::Max(0.0f, FVector::DotProduct(OutB - StartB, -Normal));
    };

    FVector ConstrainedA = StartA;
    FVector ConstrainedB = StartB;
    EvaluateAllocation(AllocationA, ConstrainedA, ConstrainedB,
      Fact.ActualMinCm, Fact.ActualMaxCm);
    if (Fact.ActualMinCm + Fact.ActualMaxCm + ConstraintEpsilonCm < RequiredCm)
    {
      const float SearchStep = FMath::Max(0.25f,
        FMath::Max(0.0f, Settings.PositionQuantumCm) * 0.25f);
      const int32 SearchCount = FMath::Max(1, FMath::CeilToInt(Fact.RequestedCm / SearchStep));
      bool bFoundFeasible = false;
      float BestCost = TNumericLimits<float>::Max();
      float BestActual = -1.0f;
      float BestAllocationA = AllocationA;
      FVector BestA = ConstrainedA;
      FVector BestB = ConstrainedB;
      float BestActualA = Fact.ActualMinCm;
      float BestActualB = Fact.ActualMaxCm;
      for (int32 SearchIndex = 0; SearchIndex <= SearchCount; ++SearchIndex)
      {
        const float CandidateA = SearchIndex == SearchCount
          ? Fact.RequestedCm
          : FMath::Min(Fact.RequestedCm, SearchStep * static_cast<float>(SearchIndex));
        FVector CandidatePositionA;
        FVector CandidatePositionB;
        float CandidateActualA = 0.0f;
        float CandidateActualB = 0.0f;
        EvaluateAllocation(CandidateA, CandidatePositionA, CandidatePositionB,
          CandidateActualA, CandidateActualB);
        const float CandidateActual = CandidateActualA + CandidateActualB;
        const bool bFeasible = CandidateActual + ConstraintEpsilonCm >= RequiredCm;
        const float Cost = FMath::Square(CandidateA - DesiredA);
        const bool bTake = (bFeasible && !bFoundFeasible)
          || (bFeasible == bFoundFeasible
            && (bFeasible
              ? (Cost < BestCost - KINDA_SMALL_NUMBER
                || (FMath::IsNearlyEqual(Cost, BestCost) && CandidateA > BestAllocationA))
              : (CandidateActual > BestActual + ConstraintEpsilonCm
                || (FMath::IsNearlyEqual(CandidateActual, BestActual)
                  && (Cost < BestCost - KINDA_SMALL_NUMBER
                    || (FMath::IsNearlyEqual(Cost, BestCost) && CandidateA > BestAllocationA))))));
        if (!bTake) continue;
        bFoundFeasible = bFeasible;
        BestCost = Cost;
        BestActual = CandidateActual;
        BestAllocationA = CandidateA;
        BestA = CandidatePositionA;
        BestB = CandidatePositionB;
        BestActualA = CandidateActualA;
        BestActualB = CandidateActualB;
      }
      ConstrainedA = BestA;
      ConstrainedB = BestB;
      Fact.ActualMinCm = BestActualA;
      Fact.ActualMaxCm = BestActualB;
    }
    Positions[Pair.MinAgentIndex] = ConstrainedA;
    Positions[Pair.MaxAgentIndex] = ConstrainedB;

    const auto RecordInfluence = [&](const int32 AgentIndex, const FVector& Before, const FVector& After)
    {
      if (FVector::DistSquared2D(Before, After) <= FMath::Square(ConstraintEpsilonCm)) return;
      if (FirstInfluencedIteration[AgentIndex] == INDEX_NONE)
        FirstInfluencedIteration[AgentIndex] = Iteration + 1;
      ++CorrectedPairCounts[AgentIndex];
    };
    RecordInfluence(Pair.MinAgentIndex, StartA, ConstrainedA);
    RecordInfluence(Pair.MaxAgentIndex, StartB, ConstrainedB);
    return Fact.ActualMinCm + Fact.ActualMaxCm > ConstraintEpsilonCm;
  }
}

void FCrowdDemoParticleConstraintKernel::BuildCandidatePairs(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> EndPositions,
  TArray<FCrowdDemoParticleConstraintPair>& OutPairs)
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
    const FCrowdDemoParticleConstraintAgent& Agent = Agents[AgentIndex];
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
        FCrowdDemoParticleConstraintPair& Pair = OutPairs.AddDefaulted_GetRef();
        Pair.MinAgentIndex = CellAgents[A];
        Pair.MaxAgentIndex = CellAgents[B];
        Pair.MinAgentId = Agents[Pair.MinAgentIndex].AgentId;
        Pair.MaxAgentId = Agents[Pair.MaxAgentIndex].AgentId;
      }
    }
  }
  OutPairs.Sort([](const FCrowdDemoParticleConstraintPair& A, const FCrowdDemoParticleConstraintPair& B)
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
  OutPairs.RemoveAll([Agents, EndPositions](const FCrowdDemoParticleConstraintPair& Pair)
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

bool FCrowdDemoParticleConstraintKernel::BuildEnvironmentContacts(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> Positions,
  const FCrowdDemoParticleConstraintEnvironment& Environment,
  TArray<FCrowdDemoParticleEnvironmentContact>& OutContacts)
{
  OutContacts.Reset();
  if (Agents.Num() != Positions.Num()) return false;

  TArray<FCrowdDemoSharedFlowObstacleSpec> Obstacles = Environment.FlowConfig.ObstacleSpecs;
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
    const ECrowdDemoParticleEnvironmentFace Face,
    const FVector& Normal,
    const float HardPlane,
    const float SoftPlane,
    const float Coordinate)
  {
    const float HardDeficit = HardPlane - Coordinate;
    const float SoftError = SoftPlane - Coordinate;
    if (SoftError <= ConstraintEpsilonCm && HardDeficit <= ConstraintEpsilonCm) return;
    FCrowdDemoParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
    Contact.AgentId = Agents[AgentIndex].AgentId;
    Contact.AgentIndex = AgentIndex;
    Contact.EnvironmentId = -1000 - static_cast<int32>(Face);
    Contact.ContactKind = ECrowdDemoParticleEnvironmentContactKind::FlowBounds;
    Contact.Face = Face;
    Contact.CorrectionNormal = Normal;
    Contact.HardDistanceCm = Agents[AgentIndex].PhysicalRadiusCm
      + Agents[AgentIndex].HardSafetyGapCm;
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
    const float HardDistance = FMath::Max(0.0f,
      Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm);
    const float SoftDistance = HardDistance + FMath::Max(0.0f, Agent.SoftMarginCm);

    if (Environment.bConstrainToFlowBounds)
    {
      const FVector BoundsMin = FVector(Environment.FlowConfig.BoundsMin);
      const FVector BoundsMax = FVector(Environment.FlowConfig.BoundsMax);
      if (BoundsMin.X + HardDistance > BoundsMax.X - HardDistance
        || BoundsMin.Y + HardDistance > BoundsMax.Y - HardDistance)
        return false;
      AddBoundsContact(AgentIndex, ECrowdDemoParticleEnvironmentFace::MinX,
        FVector(1.0f, 0.0f, 0.0f), BoundsMin.X + HardDistance,
        BoundsMin.X + SoftDistance, Position.X);
      AddBoundsContact(AgentIndex, ECrowdDemoParticleEnvironmentFace::MaxX,
        FVector(-1.0f, 0.0f, 0.0f), -(BoundsMax.X - HardDistance),
        -(BoundsMax.X - SoftDistance), -Position.X);
      AddBoundsContact(AgentIndex, ECrowdDemoParticleEnvironmentFace::MinY,
        FVector(0.0f, 1.0f, 0.0f), BoundsMin.Y + HardDistance,
        BoundsMin.Y + SoftDistance, Position.Y);
      AddBoundsContact(AgentIndex, ECrowdDemoParticleEnvironmentFace::MaxY,
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
      if (bInsideHard || bInsideSoft)
      {
        const ECrowdDemoParticleEnvironmentFace Face = bInsideHard
          ? SelectEscapeFace(Position, HardMin, HardMax)
          : SelectEscapeFace(Position, SoftMin, SoftMax);
        const FVector Normal = ContactFaceNormal(Face);
        FCrowdDemoParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
        Contact.AgentId = Agent.AgentId;
        Contact.AgentIndex = AgentIndex;
        Contact.EnvironmentId = Obstacle.ObstacleId;
        Contact.ContactKind = ECrowdDemoParticleEnvironmentContactKind::ObstacleEndpoint;
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

      float EntryTime = 0.0f;
      ECrowdDemoParticleEnvironmentFace EntryFace = ECrowdDemoParticleEnvironmentFace::MinX;
      if (SegmentBoxEntryFace(Agent.StartPosition, Position, HardMin, HardMax,
        EntryTime, EntryFace))
      {
        const FVector Normal = ContactFaceNormal(EntryFace);
        const float Threshold = FaceThreshold(HardMin, HardMax, EntryFace, ConstraintEpsilonCm);
        const float Deficit = Threshold - FVector::DotProduct(Position, Normal);
        if (Deficit > ConstraintEpsilonCm || EntryTime <= ConstraintEpsilonCm)
        {
          FCrowdDemoParticleEnvironmentContact& Contact = OutContacts.AddDefaulted_GetRef();
          Contact.AgentId = Agent.AgentId;
          Contact.AgentIndex = AgentIndex;
          Contact.EnvironmentId = Obstacle.ObstacleId;
          Contact.ContactKind = ECrowdDemoParticleEnvironmentContactKind::ObstacleSwept;
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

void FCrowdDemoParticleConstraintKernel::BuildUnifiedHardConstraints(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const TConstArrayView<FVector> Positions,
  const TConstArrayView<FCrowdDemoParticleConstraintPair> Pairs,
  const TConstArrayView<FCrowdDemoParticleEnvironmentContact> Contacts,
  TArray<FCrowdDemoParticleHardConstraint>& OutConstraints)
{
  OutConstraints.Reset();
  if (Agents.Num() != Positions.Num()) return;
  for (const auto& Pair : Pairs)
  {
    if (!Agents.IsValidIndex(Pair.MinAgentIndex) || !Agents.IsValidIndex(Pair.MaxAgentIndex)) continue;
    const auto& A = Agents[Pair.MinAgentIndex];
    const auto& B = Agents[Pair.MaxAgentIndex];
    const float HardDistance = PairHardDistance(A, B);
    const FVector Delta = Positions[Pair.MinAgentIndex] - Positions[Pair.MaxAgentIndex];
    const float Distance = Delta.Size2D();
    if (Distance + ConstraintEpsilonCm < HardDistance)
    {
      FCrowdDemoParticleHardConstraint& Constraint = OutConstraints.AddDefaulted_GetRef();
      Constraint.Kind = ECrowdDemoParticleHardConstraintKind::PairEndpoint;
      Constraint.MinAgentId = Pair.MinAgentId;
      Constraint.MaxAgentId = Pair.MaxAgentId;
      Constraint.MinAgentIndex = Pair.MinAgentIndex;
      Constraint.MaxAgentIndex = Pair.MaxAgentIndex;
      Constraint.Normal = Distance > SMALL_NUMBER
        ? Delta / Distance : MakeStablePairDirection(Pair.MinAgentId, Pair.MaxAgentId);
      Constraint.Threshold = HardDistance;
      Constraint.InitialDeficitCm = HardDistance - Distance;
    }

    const FSweptDistance Swept = EvaluateSweptDistance(
      A.StartPosition, Positions[Pair.MinAgentIndex],
      B.StartPosition, Positions[Pair.MaxAgentIndex], Pair.MinAgentId, Pair.MaxAgentId);
    if (Swept.Distance + ConstraintEpsilonCm < HardDistance)
    {
      const float Scale = FMath::Max(0.05f, Swept.Time);
      const FVector RelativeStart = A.StartPosition - B.StartPosition;
      FCrowdDemoParticleHardConstraint& Constraint = OutConstraints.AddDefaulted_GetRef();
      Constraint.Kind = ECrowdDemoParticleHardConstraintKind::PairSwept;
      Constraint.MinAgentId = Pair.MinAgentId;
      Constraint.MaxAgentId = Pair.MaxAgentId;
      Constraint.MinAgentIndex = Pair.MinAgentIndex;
      Constraint.MaxAgentIndex = Pair.MaxAgentIndex;
      Constraint.Normal = Swept.Normal;
      Constraint.CoefficientScale = Scale;
      Constraint.Threshold = HardDistance
        - (1.0f - Scale) * FVector::DotProduct(Swept.Normal, RelativeStart);
      Constraint.InitialDeficitCm = FMath::Max(0.0f,
        Constraint.Threshold - Scale * FVector::DotProduct(Swept.Normal, Delta));
    }
  }

  for (const auto& Contact : Contacts)
  {
    if (Contact.HardDeficitCm <= ConstraintEpsilonCm) continue;
    FCrowdDemoParticleHardConstraint& Constraint = OutConstraints.AddDefaulted_GetRef();
    Constraint.Kind = Contact.ContactKind == ECrowdDemoParticleEnvironmentContactKind::ObstacleEndpoint
      ? ECrowdDemoParticleHardConstraintKind::ObstacleEndpoint
      : (Contact.ContactKind == ECrowdDemoParticleEnvironmentContactKind::ObstacleSwept
        ? ECrowdDemoParticleHardConstraintKind::ObstacleSwept
        : ECrowdDemoParticleHardConstraintKind::FlowBounds);
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

void FCrowdDemoParticleConstraintKernel::SolveUnifiedHardClosure(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const FCrowdDemoParticleConstraintSettings& Settings,
  const TConstArrayView<FCrowdDemoParticleHardConstraint> Constraints,
  TArray<FVector>& InOutPositions,
  TArray<FCrowdDemoParticleHardDualState>& InOutDualStates,
  FCrowdDemoParticleUnifiedHardSummary& OutSummary)
{
  OutSummary = FCrowdDemoParticleUnifiedHardSummary();
  OutSummary.ConstraintCount = Constraints.Num();
  if (InOutPositions.Num() != Agents.Num())
  {
    OutSummary.bValid = false;
    OutSummary.InfeasibleConstraintCount = Constraints.Num();
    return;
  }

  TArray<FCrowdDemoParticleHardDualState> NewDualStates;
  NewDualStates.Reserve(Constraints.Num());
  const float HardCap = FMath::Max(0.0f, Settings.HardMaxPairCorrectionPerIterationCm);
  for (const auto& Constraint : Constraints)
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
      || Constraint.Normal.SizeSquared2D() < 0.99f)
    {
      OutSummary.bValid = false;
      ++OutSummary.InfeasibleConstraintCount;
      continue;
    }

    FCrowdDemoParticleHardDualState State;
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
    const float Lhs = Scale * FVector::DotProduct(InOutPositions[AgentAIndex], Constraint.Normal)
      - (Constraint.MaxAgentId != INDEX_NONE
        ? Scale * FVector::DotProduct(InOutPositions[AgentBIndex], Constraint.Normal) : 0.0f);
    const float Residual = Constraint.Threshold - Lhs;
    const float Denominator = (MobilityA + MobilityB) * Scale * Scale;
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
    const float EndpointMotionPerLambda = (MobilityA + MobilityB) * Scale;
    const float MaxDeltaLambda = EndpointMotionPerLambda > SMALL_NUMBER
      ? HardCap / EndpointMotionPerLambda : 0.0f;
    DeltaLambda = FMath::Clamp(DeltaLambda, -MaxDeltaLambda, MaxDeltaLambda);
    State.Lambda = FMath::Max(0.0f, State.Lambda + DeltaLambda);
    const FVector CorrectionA = Constraint.Normal * (MobilityA * Scale * DeltaLambda);
    const FVector CorrectionB = Constraint.MaxAgentId != INDEX_NONE
      ? -Constraint.Normal * (MobilityB * Scale * DeltaLambda) : FVector::ZeroVector;
    InOutPositions[AgentAIndex] += CorrectionA;
    if (Constraint.MaxAgentId != INDEX_NONE) InOutPositions[AgentBIndex] += CorrectionB;
    OutSummary.MaxAppliedCorrectionCm = FMath::Max(OutSummary.MaxAppliedCorrectionCm,
      FMath::Max(CorrectionA.Size2D(), CorrectionB.Size2D()));

    const float NewLhs = Scale * FVector::DotProduct(InOutPositions[AgentAIndex], Constraint.Normal)
      - (Constraint.MaxAgentId != INDEX_NONE
        ? Scale * FVector::DotProduct(InOutPositions[AgentBIndex], Constraint.Normal) : 0.0f);
    OutSummary.MaxResidualCm = FMath::Max(OutSummary.MaxResidualCm,
      FMath::Max(0.0f, Constraint.Threshold - NewLhs));
    NewDualStates.Add(State);
  }
  InOutDualStates = MoveTemp(NewDualStates);
}

void FCrowdDemoParticleConstraintKernel::Solve(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const FCrowdDemoParticleConstraintEnvironment& Environment,
  const FCrowdDemoParticleConstraintSettings& Settings,
  TArray<FCrowdDemoParticleConstraintPair>& OutPairs,
  TArray<FCrowdDemoParticleConstraintResult>& OutResults,
  FCrowdDemoParticleConstraintSummary& OutSummary,
  FCrowdDemoParticleConstraintTrace* OutTrace)
{
  OutPairs.Reset();
  OutResults.Reset();
  OutSummary = FCrowdDemoParticleConstraintSummary();
  if (OutTrace) *OutTrace = FCrowdDemoParticleConstraintTrace();
  if (Agents.IsEmpty())
  {
    OutSummary.bValid = true;
    return;
  }

  TArray<FCrowdDemoParticleConstraintAgent> SortedAgents(Agents);
  SortedAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  for (int32 Index = 0; Index < SortedAgents.Num(); ++Index)
  {
    const auto& Agent = SortedAgents[Index];
    if (Agent.AgentId == INDEX_NONE || Agent.PhysicalRadiusCm <= 0.0f || Agent.HardSafetyGapCm < 0.0f
      || Agent.SoftMarginCm < 0.0f || Agent.Mobility < 0.0f
      || (Index > 0 && SortedAgents[Index - 1].AgentId == Agent.AgentId))
      return;
  }

  TArray<FVector> Positions;
  TArray<int32> FirstInfluencedIterations;
  TArray<int32> CorrectedPairCounts;
  TArray<FEnvironmentSoftFact> EnvironmentSoftFacts;
  TArray<FUnifiedHardFact> UnifiedHardFacts;
  TArray<FCrowdDemoParticleHardDualState> MainDualStates;
  TArray<FCrowdDemoParticleHardDualState> SafetyDualStates;
  TSet<int32> EnvironmentSoftAppliedAgents;
  bool bEnvironmentInputValid = true;
  Positions.Reserve(SortedAgents.Num());
  FirstInfluencedIterations.Init(INDEX_NONE, SortedAgents.Num());
  CorrectedPairCounts.Init(0, SortedAgents.Num());
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
      FVector Delta = Positions[Pair.MinAgentIndex] - Positions[Pair.MaxAgentIndex];
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
      ApplyPairCorrection(Pair, SortedAgents, Positions, Normal, Correction, Iteration,
        FirstInfluencedIterations, CorrectedPairCounts);
    }
    if (OutTrace && Iteration + 1 == IterationCount) OutTrace->SoftPositions = Positions;

    TArray<FCrowdDemoParticleEnvironmentContact> SoftContacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, SoftContacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    const int32 FirstSoftFact = EnvironmentSoftFacts.Num();
    for (const auto& Contact : SoftContacts)
    {
      if (Contact.ContactKind == ECrowdDemoParticleEnvironmentContactKind::ObstacleSwept
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
      EnvironmentSoftAppliedAgents.Add(Contact.AgentId);
      if (FirstInfluencedIterations[Contact.AgentIndex] == INDEX_NONE)
        FirstInfluencedIterations[Contact.AgentIndex] = Iteration + 1;
      OutSummary.EnvironmentSoftRequestedCorrectionCmMax = FMath::Max(
        OutSummary.EnvironmentSoftRequestedCorrectionCmMax, Requested);
    }
    if (OutTrace && Iteration + 1 == IterationCount)
      OutTrace->EnvironmentSoftPositions = Positions;

    BuildCandidatePairs(SortedAgents, Positions, OutPairs);
    TArray<FCrowdDemoParticleEnvironmentContact> HardContacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, HardContacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    TArray<FCrowdDemoParticleHardConstraint> Constraints;
    BuildUnifiedHardConstraints(SortedAgents, Positions, OutPairs, HardContacts, Constraints);
    FCrowdDemoParticleUnifiedHardSummary ClosureSummary;
    SolveUnifiedHardClosure(SortedAgents, Settings, Constraints, Positions,
      MainDualStates, ClosureSummary);
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
      if (MainDualStates.IsValidIndex(ConstraintIndex)) Fact.Dual = MainDualStates[ConstraintIndex];
    }
    for (int32 FactIndex = FirstSoftFact; FactIndex < EnvironmentSoftFacts.Num(); ++FactIndex)
    {
      FEnvironmentSoftFact& Fact = EnvironmentSoftFacts[FactIndex];
      const int32 AgentIndex = SortedAgents.IndexOfByPredicate([&](const auto& Agent)
      {
        return Agent.AgentId == Fact.AgentId;
      });
      if (!Positions.IsValidIndex(AgentIndex)) continue;
      Fact.RealizedCm = FMath::Clamp(FVector::DotProduct(
        Positions[AgentIndex] - Fact.BeforePosition, Fact.Normal), 0.0f, Fact.RequestedCm);
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

  const int32 SafetyIterationCount = FMath::Max(1, Settings.SafetyIterationCount);
  for (int32 SafetyIteration = 0; SafetyIteration < SafetyIterationCount; ++SafetyIteration)
  {
    BuildCandidatePairs(SortedAgents, Positions, OutPairs);
    TArray<FCrowdDemoParticleEnvironmentContact> Contacts;
    if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, Contacts))
    {
      bEnvironmentInputValid = false;
      break;
    }
    TArray<FCrowdDemoParticleHardConstraint> Constraints;
    BuildUnifiedHardConstraints(SortedAgents, Positions, OutPairs, Contacts, Constraints);
    FCrowdDemoParticleUnifiedHardSummary ClosureSummary;
    SolveUnifiedHardClosure(SortedAgents, Settings, Constraints, Positions,
      SafetyDualStates, ClosureSummary);
    OutSummary.UnifiedHardConstraintCount = FMath::Max(
      OutSummary.UnifiedHardConstraintCount, ClosureSummary.ConstraintCount);
    OutSummary.UnifiedHardInfeasibleCount += ClosureSummary.InfeasibleConstraintCount;
    OutSummary.UnifiedHardResidualCmMax = ClosureSummary.MaxResidualCm;
    for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
    {
      FUnifiedHardFact& Fact = UnifiedHardFacts.AddDefaulted_GetRef();
      Fact.Iteration = SafetyIteration;
      Fact.Stage = 1;
      Fact.Constraint = Constraints[ConstraintIndex];
      if (SafetyDualStates.IsValidIndex(ConstraintIndex)) Fact.Dual = SafetyDualStates[ConstraintIndex];
    }
    for (FVector& Position : Positions)
      Position = QuantizeVector2D(Position, Settings.PositionQuantumCm);
    if (OutTrace && SafetyIteration + 1 == SafetyIterationCount)
    {
      OutTrace->FinalEnvironmentContacts = Contacts;
      OutTrace->FinalHardConstraints = Constraints;
    }
  }
  if (OutTrace) OutTrace->FinalSafetyPositions = Positions;
  BuildCandidatePairs(SortedAgents, Positions, OutPairs);
  OutSummary.CandidatePairCount = OutPairs.Num();

  TArray<FCrowdDemoParticleEnvironmentContact> FinalEnvironmentContacts;
  if (!BuildEnvironmentContacts(SortedAgents, Positions, Environment, FinalEnvironmentContacts))
    bEnvironmentInputValid = false;
  TArray<float> EnvironmentSoftErrors;
  for (const auto& Contact : FinalEnvironmentContacts)
  {
    if (Contact.ContactKind == ECrowdDemoParticleEnvironmentContactKind::ObstacleSwept
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
    TArray<FCrowdDemoParticleHardConstraint> FinalConstraints;
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
    if (Swept.Distance + ConstraintEpsilonCm < PairHardDistance(A, B))
      ++OutSummary.SweptPairViolationCount;
  }
  OutSummary.SoftErrorCmP50 = Percentile(SoftErrors, 0.50f);
  OutSummary.SoftErrorCmP95 = Percentile(SoftErrors, 0.95f);
  OutSummary.SoftErrorCmMax = SoftErrors.IsEmpty() ? 0.0f : FMath::Max(SoftErrors);

  OutResults.Reserve(SortedAgents.Num());
  for (int32 AgentIndex = 0; AgentIndex < SortedAgents.Num(); ++AgentIndex)
  {
    const auto& Agent = SortedAgents[AgentIndex];
    const FCrowdDemoSharedFlowConstraintResult EnvironmentConstraint = ConstrainParticleMovement(
      Agent, Environment, Positions[AgentIndex], Settings.FixedStepSeconds);
    if (EnvironmentConstraint.bPenetrating || EnvironmentConstraint.bHitObstacle
      || FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
        [&]() { auto C = Environment.FlowConfig; C.AgentInflateCm = Agent.PhysicalRadiusCm
          + Agent.HardSafetyGapCm; return C; }(), Positions[AgentIndex]))
      ++OutSummary.ObstaclePenetrationCount;
    if (IsOutsideParticleBounds(Agent, Environment, Positions[AgentIndex]))
      ++OutSummary.BoundsViolationCount;
    FCrowdDemoParticleConstraintResult& Result = OutResults.AddDefaulted_GetRef();
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

  uint32 Hash = FoldHash(2166136261u, 3); // Particle candidate contract v3.
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
  FoldVector(FVector(Environment.FlowConfig.BoundsMin));
  FoldVector(FVector(Environment.FlowConfig.BoundsMax));
  FoldFloat(Environment.FlowConfig.CellSizeCm, 1000.0f);
  FoldFloat(Environment.FlowConfig.AgentInflateCm, 1000.0f);
  FoldVector(FVector(Environment.FlowConfig.GoalLocation));
  TArray<FCrowdDemoSharedFlowObstacleSpec> SortedObstacles =
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
  for (const auto& Agent : SortedAgents)
  {
    Hash = FoldHash(Hash, Agent.AgentId);
    FoldVector(Agent.StartPosition);
    FoldVector(Agent.PredictedPosition);
    FoldFloat(Agent.PhysicalRadiusCm, 1000.0f);
    FoldFloat(Agent.HardSafetyGapCm, 1000.0f);
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
  for (const auto& Fact : EnvironmentSoftFacts)
  {
    Hash = FoldHash(Hash, Fact.Iteration);
    Hash = FoldHash(Hash, Fact.AgentId);
    Hash = FoldHash(Hash, Fact.EnvironmentId);
    Hash = FoldHash(Hash, Fact.Kind);
    Hash = FoldHash(Hash, Fact.Face);
    FoldVector(Fact.Normal);
    FoldFloat(Fact.ErrorCm, 1000.0f);
    FoldFloat(Fact.RequestedCm, 1000.0f);
    FoldFloat(Fact.RealizedCm, 1000.0f);
  }
  for (const auto& Fact : UnifiedHardFacts)
  {
    Hash = FoldHash(Hash, Fact.Iteration);
    Hash = FoldHash(Hash, Fact.Stage);
    Hash = FoldHash(Hash, static_cast<int32>(Fact.Constraint.Kind));
    Hash = FoldHash(Hash, Fact.Constraint.MinAgentId);
    Hash = FoldHash(Hash, Fact.Constraint.MaxAgentId);
    Hash = FoldHash(Hash, Fact.Constraint.EnvironmentId);
    Hash = FoldHash(Hash, static_cast<int32>(Fact.Constraint.Face));
    FoldVector(Fact.Constraint.Normal);
    FoldFloat(Fact.Constraint.CoefficientScale, 1000000.0f);
    FoldFloat(Fact.Constraint.Threshold, 1000.0f);
    FoldFloat(Fact.Constraint.InitialDeficitCm, 1000.0f);
    FoldFloat(Fact.Dual.Lambda, 1000000.0f);
  }
  OutSummary.CandidateHash = Hash;
  OutSummary.bValid = bEnvironmentInputValid
    && OutSummary.UnifiedHardInfeasibleCount == 0
    && OutSummary.HardPairViolationCount == 0
    && OutSummary.SweptPairViolationCount == 0
    && OutSummary.ObstaclePenetrationCount == 0
    && OutSummary.BoundsViolationCount == 0;
}

uint32 FCrowdDemoParticleConstraintKernel::HashAppliedRoundSimState(
  const int32 RoundId,
  const int32 PlanRevision,
  const int32 FixedStepIndex,
  const float BoundaryServerTimeSeconds,
  const TConstArrayView<FCrowdDemoParticleAppliedRoundSimState> States)
{
  TArray<FCrowdDemoParticleAppliedRoundSimState> SortedStates(States);
  SortedStates.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  uint32 Hash = FoldHash(2166136261u, 2); // Applied RoundSim contract v2.
  Hash = FoldHash(Hash, RoundId);
  Hash = FoldHash(Hash, PlanRevision);
  Hash = FoldHash(Hash, FixedStepIndex);
  Hash = FoldHash(Hash, FMath::RoundToInt(BoundaryServerTimeSeconds * 1000000.0f));
  Hash = FoldHash(Hash, SortedStates.Num());
  for (int32 Index = 0; Index < SortedStates.Num(); ++Index)
  {
    const auto& State = SortedStates[Index];
    if (State.AgentId == INDEX_NONE
      || (Index > 0 && SortedStates[Index - 1].AgentId == State.AgentId))
      return 0;
    Hash = FoldHash(Hash, State.AgentId);
    Hash = FoldHash(Hash, State.LifecycleSerial);
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Position.X * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Position.Y * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Position.Z * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Velocity.X * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Velocity.Y * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.Velocity.Z * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.YawDegrees * 1000.0f));
    Hash = FoldHash(Hash, FMath::RoundToInt(State.RadiusCm * 1000.0f));
    Hash = FoldHash(Hash, State.bInitialized ? 1 : 0);
  }
  return Hash;
}

void FCrowdDemoParticleConstraintKernel::AdvanceSettlingTracker(
  FCrowdDemoParticleSettlingTracker& Tracker,
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

void FCrowdDemoParticleConstraintKernel::BuildFailureFixture(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const TConstArrayView<FCrowdDemoParticleAppliedState> AppliedStates,
  const FCrowdDemoParticleConstraintTrace& Trace,
  const int32 FixedStepIndex,
  const uint32 CandidateHash,
  const uint32 AppliedStateHash,
  FCrowdDemoParticleFailureFixture& OutFixture)
{
  OutFixture = FCrowdDemoParticleFailureFixture();
  if (Agents.Num() != AppliedStates.Num() || Trace.AgentIds.Num() != Agents.Num()
    || Trace.FinalSafetyPositions.Num() != Agents.Num()) return;

  TArray<FCrowdDemoParticleConstraintAgent> SortedAgents(Agents);
  TArray<FCrowdDemoParticleAppliedState> SortedApplied(AppliedStates);
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
      const bool bPairSwept = Swept.Distance + ConstraintEpsilonCm < Required;
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
  if (FailureA == INDEX_NONE) return;

  OutFixture.FixedStepIndex = FixedStepIndex;
  OutFixture.MinAgentId = SortedAgents[FailureA].AgentId;
  OutFixture.MaxAgentId = SortedAgents[FailureB].AgentId;
  OutFixture.bHardViolation = bHard;
  OutFixture.bSweptViolation = bSwept;
  OutFixture.RequiredHardDistanceCm = RequiredHard;
  OutFixture.FinalEndpointDistanceCm = EndpointDistance;
  OutFixture.FinalSweptDistanceCm = SweptDistance;
  OutFixture.CandidateHash = CandidateHash;
  OutFixture.AppliedStateHash = AppliedStateHash;
  OutFixture.SolveAgents = SortedAgents;

  const int32 Indices[2] = {FailureA, FailureB};
  for (const int32 Index : Indices)
  {
    FCrowdDemoParticleFailureFixtureAgent& FixtureAgent = OutFixture.Agents.AddDefaulted_GetRef();
    const auto& Agent = SortedAgents[Index];
    FixtureAgent.AgentId = Agent.AgentId;
    FixtureAgent.PhysicalRadiusCm = Agent.PhysicalRadiusCm;
    FixtureAgent.HardSafetyGapCm = Agent.HardSafetyGapCm;
    FixtureAgent.SoftMarginCm = Agent.SoftMarginCm;
    FixtureAgent.Mobility = Agent.Mobility;
    FixtureAgent.Start = Trace.StartPositions[Index];
    FixtureAgent.Predict = Trace.PredictPositions[Index];
    FixtureAgent.Soft = Trace.SoftPositions[Index];
    FixtureAgent.Hard = Trace.HardPositions[Index];
    FixtureAgent.Swept = Trace.SweptPositions[Index];
    FixtureAgent.Obstacle = Trace.ObstaclePositions[Index];
    FixtureAgent.Quantized = Trace.QuantizedPositions[Index];
    FixtureAgent.FinalSafety = Trace.FinalSafetyPositions[Index];
    FixtureAgent.Applied = SortedApplied[Index].Position;
  }

  uint32 Hash = 2166136261u;
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
    const FVector Stages[] = {Agent.Start, Agent.Predict, Agent.Soft, Agent.Hard,
      Agent.Swept, Agent.Obstacle, Agent.Quantized, Agent.FinalSafety, Agent.Applied};
    for (const FVector& Position : Stages)
    {
      Hash = FoldHash(Hash, FMath::RoundToInt(Position.X));
      Hash = FoldHash(Hash, FMath::RoundToInt(Position.Y));
    }
  }
  OutFixture.FixtureHash = Hash;
  OutFixture.bValid = true;
}

void FCrowdDemoParticleConstraintKernel::EvaluateAppliedState(
  const TConstArrayView<FCrowdDemoParticleConstraintAgent> Agents,
  const TConstArrayView<FCrowdDemoParticleAppliedState> AppliedStates,
  const FCrowdDemoParticleConstraintEnvironment& Environment,
  FCrowdDemoParticleConstraintSummary& OutSummary,
  uint32& OutAppliedStateHash)
{
  OutSummary = FCrowdDemoParticleConstraintSummary();
  OutAppliedStateHash = 2166136261u;
  if (Agents.Num() != AppliedStates.Num()) return;

  TArray<FCrowdDemoParticleConstraintAgent> SortedAgents(Agents);
  TArray<FCrowdDemoParticleAppliedState> SortedStates(AppliedStates);
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
    if (FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
      [&]() { auto C = Environment.FlowConfig; C.AgentInflateCm = SortedAgents[Index].PhysicalRadiusCm
        + SortedAgents[Index].HardSafetyGapCm; return C; }(), SortedStates[Index].Position))
      ++OutSummary.ObstaclePenetrationCount;
    if (IsOutsideParticleBounds(SortedAgents[Index], Environment, SortedStates[Index].Position))
      ++OutSummary.BoundsViolationCount;
  }

  TArray<FCrowdDemoParticleConstraintPair> Pairs;
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
    if (Swept.Distance + ConstraintEpsilonCm < PairHardDistance(A, B))
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
