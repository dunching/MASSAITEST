#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  constexpr int32 StraightCost = 1000;
  constexpr int32 DiagonalCost = 1414;
  constexpr int32 InfiniteCost = MAX_int32;
  constexpr int32 NeighborDx[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
  constexpr int32 NeighborDy[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };

  struct FQueueNode
  {
    int32 Cost = InfiniteCost;
    int32 CellIndex = INDEX_NONE;
  };

  bool IsHigherPriority(const FQueueNode& A, const FQueueNode& B)
  {
    return A.Cost < B.Cost || (A.Cost == B.Cost && A.CellIndex < B.CellIndex);
  }

  void HeapPush(TArray<FQueueNode>& Heap, const FQueueNode Node)
  {
    int32 Index = Heap.Add(Node);
    while (Index > 0)
    {
      const int32 Parent = (Index - 1) / 2;
      if (IsHigherPriority(Heap[Parent], Heap[Index]))
      {
        break;
      }
      Heap.Swap(Parent, Index);
      Index = Parent;
    }
  }

  FQueueNode HeapPop(TArray<FQueueNode>& Heap)
  {
    const FQueueNode Result = Heap[0];
    Heap[0] = Heap.Last();
    Heap.Pop(EAllowShrinking::No);
    int32 Index = 0;
    while (true)
    {
      const int32 Left = Index * 2 + 1;
      const int32 Right = Left + 1;
      if (Left >= Heap.Num())
      {
        break;
      }
      int32 Best = Left;
      if (Right < Heap.Num() && IsHigherPriority(Heap[Right], Heap[Left]))
      {
        Best = Right;
      }
      if (IsHigherPriority(Heap[Index], Heap[Best]))
      {
        break;
      }
      Heap.Swap(Index, Best);
      Index = Best;
    }
    return Result;
  }

  uint32 HashInt(uint32 Hash, const int32 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    return Hash * 16777619u;
  }

  int32 Quantize10(const float Value)
  {
    return FMath::RoundToInt(Value * 10.0f);
  }

  bool SegmentIntersectsBox2D(
    const FVector& Start,
    const FVector& End,
    const FVector& Min,
    const FVector& Max)
  {
    float TMin = 0.0f;
    float TMax = 1.0f;
    const FVector Delta = End - Start;
    for (int32 Axis = 0; Axis < 2; ++Axis)
    {
      const float Origin = Axis == 0 ? Start.X : Start.Y;
      const float Direction = Axis == 0 ? Delta.X : Delta.Y;
      const float AxisMin = Axis == 0 ? Min.X : Min.Y;
      const float AxisMax = Axis == 0 ? Max.X : Max.Y;
      if (FMath::IsNearlyZero(Direction))
      {
        if (Origin >= AxisMin && Origin <= AxisMax)
        {
          continue;
        }
        return false;
      }
      const float InvDirection = 1.0f / Direction;
      float Enter = (AxisMin - Origin) * InvDirection;
      float Exit = (AxisMax - Origin) * InvDirection;
      if (Enter > Exit)
      {
        Swap(Enter, Exit);
      }
      TMin = FMath::Max(TMin, Enter);
      TMax = FMath::Min(TMax, Exit);
      if (TMin > TMax)
      {
        return false;
      }
    }
    return TMax >= 0.0f && TMin <= 1.0f;
  }

  bool SegmentBoxInterval2D(
    const FVector& Start,
    const FVector& End,
    const FVector& Min,
    const FVector& Max,
    float& OutEntryT,
    float& OutExitT)
  {
    float TMin = 0.0f;
    float TMax = 1.0f;
    const FVector Delta = End - Start;
    for (int32 Axis = 0; Axis < 2; ++Axis)
    {
      const float Origin = Axis == 0 ? Start.X : Start.Y;
      const float Direction = Axis == 0 ? Delta.X : Delta.Y;
      const float AxisMin = Axis == 0 ? Min.X : Min.Y;
      const float AxisMax = Axis == 0 ? Max.X : Max.Y;
      if (FMath::IsNearlyZero(Direction))
      {
        if (Origin >= AxisMin && Origin <= AxisMax) continue;
        OutEntryT = -1.0f;
        OutExitT = -1.0f;
        return false;
      }
      const float InvDirection = 1.0f / Direction;
      float Enter = (AxisMin - Origin) * InvDirection;
      float Exit = (AxisMax - Origin) * InvDirection;
      if (Enter > Exit) Swap(Enter, Exit);
      TMin = FMath::Max(TMin, Enter);
      TMax = FMath::Min(TMax, Exit);
      if (TMin > TMax)
      {
        OutEntryT = -1.0f;
        OutExitT = -1.0f;
        return false;
      }
    }
    const bool bIntersects = TMax >= 0.0f && TMin <= 1.0f;
    OutEntryT = bIntersects ? TMin : -1.0f;
    OutExitT = bIntersects ? TMax : -1.0f;
    return bIntersects;
  }

  bool IsPointInsideBox2D(const FVector& Point, const FVector& Min, const FVector& Max)
  {
    return Point.X >= Min.X && Point.X <= Max.X
      && Point.Y >= Min.Y && Point.Y <= Max.Y;
  }

  bool IsSegmentClear(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& End)
  {
    for (const FCrowdDemoSharedFlowObstacleSpec& Obstacle : Config.ObstacleSpecs)
    {
      const FVector Inflate(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
      const FVector Min = FVector(Obstacle.Center) - FVector(Obstacle.Extent) - Inflate;
      const FVector Max = FVector(Obstacle.Center) + FVector(Obstacle.Extent) + Inflate;
      if (SegmentIntersectsBox2D(Start, End, Min, Max))
      {
        return false;
      }
    }
    return true;
  }
}

void FCrowdDemoSharedFlowField::Reset()
{
  Config = FCrowdDemoSharedFlowFieldConfig();
  IntegrationCost.Reset();
  FlowDirection.Reset();
  NextCellIndex.Reset();
  Blocked.Reset();
  Unreachable.Reset();
  Width = 0;
  Height = 0;
  GoalCellIndex = INDEX_NONE;
  BlockedCellCount = 0;
  BuildHash = 0;
}

bool FCrowdDemoSharedFlowField::IsValid() const
{
  return Width > 0
    && Height > 0
    && GoalCellIndex != INDEX_NONE
    && IntegrationCost.Num() == Width * Height
    && FlowDirection.Num() == Width * Height
    && NextCellIndex.Num() == Width * Height;
}

int32 FCrowdDemoSharedFlowField::LocationToCellIndex(const FVector& Location) const
{
  if (Width <= 0 || Height <= 0 || Config.CellSizeCm <= 0.0f)
  {
    return INDEX_NONE;
  }
  const FVector Min = FVector(Config.BoundsMin);
  const int32 X = FMath::FloorToInt((Location.X - Min.X) / Config.CellSizeCm);
  const int32 Y = FMath::FloorToInt((Location.Y - Min.Y) / Config.CellSizeCm);
  return X >= 0 && X < Width && Y >= 0 && Y < Height ? Y * Width + X : INDEX_NONE;
}

FVector FCrowdDemoSharedFlowField::CellCenter(const int32 CellIndex) const
{
  if (CellIndex < 0 || CellIndex >= Width * Height)
  {
    return FVector::ZeroVector;
  }
  const int32 X = CellIndex % Width;
  const int32 Y = CellIndex / Width;
  const FVector Min = FVector(Config.BoundsMin);
  return FVector(
    Min.X + (static_cast<float>(X) + 0.5f) * Config.CellSizeCm,
    Min.Y + (static_cast<float>(Y) + 0.5f) * Config.CellSizeCm,
    FVector(Config.GoalLocation).Z);
}

FCrowdDemoSharedFlowFieldConfig FCrowdDemoSharedFlowFieldKernel::MakeSf1Config(const int32 Revision)
{
  FCrowdDemoSharedFlowFieldConfig Config;
  Config.Revision = Revision;
  Config.BoundsMin = FVector(-2600.0f, -3300.0f, 0.0f);
  Config.BoundsMax = FVector(2600.0f, 2300.0f, 0.0f);
  Config.CellSizeCm = 100.0f;
  Config.AgentInflateCm = 48.0f;
  Config.GoalLocation = FVector(0.0f, 1900.0f, 60.0f);

  auto AddObstacle = [&Config](const int32 Id, const FVector& Center, const FVector& Extent)
  {
    FCrowdDemoSharedFlowObstacleSpec& Spec = Config.ObstacleSpecs.AddDefaulted_GetRef();
    Spec.ObstacleId = Id;
    Spec.Center = Center;
    Spec.Extent = Extent;
  };

  AddObstacle(101, FVector(-700.0f, -2100.0f, 120.0f), FVector(1700.0f, 100.0f, 120.0f));
  AddObstacle(102, FVector(2050.0f, -2100.0f, 120.0f), FVector(350.0f, 100.0f, 120.0f));
  AddObstacle(103, FVector(900.0f, -1550.0f, 120.0f), FVector(100.0f, 400.0f, 120.0f));
  AddObstacle(104, FVector(1800.0f, -1550.0f, 120.0f), FVector(100.0f, 400.0f, 120.0f));
  AddObstacle(105, FVector(-1975.0f, -800.0f, 120.0f), FVector(425.0f, 100.0f, 120.0f));
  AddObstacle(106, FVector(775.0f, -800.0f, 120.0f), FVector(1625.0f, 100.0f, 120.0f));
  AddObstacle(107, FVector(-1650.0f, -100.0f, 120.0f), FVector(100.0f, 400.0f, 120.0f));
  AddObstacle(108, FVector(-750.0f, -100.0f, 120.0f), FVector(100.0f, 400.0f, 120.0f));
  AddObstacle(109, FVector(-700.0f, 600.0f, 120.0f), FVector(1700.0f, 100.0f, 120.0f));
  AddObstacle(110, FVector(2050.0f, 600.0f, 120.0f), FVector(350.0f, 100.0f, 120.0f));
  return Config;
}

bool FCrowdDemoSharedFlowFieldKernel::Build(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  FCrowdDemoSharedFlowField& OutField)
{
  OutField.Reset();
  OutField.Config = Config;
  const FVector Min = FVector(Config.BoundsMin);
  const FVector Max = FVector(Config.BoundsMax);
  if (Config.CellSizeCm <= 1.0f || Max.X <= Min.X || Max.Y <= Min.Y)
  {
    return false;
  }

  OutField.Width = FMath::Max(1, FMath::CeilToInt((Max.X - Min.X) / Config.CellSizeCm));
  OutField.Height = FMath::Max(1, FMath::CeilToInt((Max.Y - Min.Y) / Config.CellSizeCm));
  const int32 CellCount = OutField.Width * OutField.Height;
  OutField.IntegrationCost.Init(InfiniteCost, CellCount);
  OutField.FlowDirection.Init(FVector::ZeroVector, CellCount);
  OutField.NextCellIndex.Init(INDEX_NONE, CellCount);
  OutField.Blocked.Init(false, CellCount);
  OutField.Unreachable.Init(true, CellCount);

  for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
  {
    const bool bBlocked = IsInsideInflatedObstacle(Config, OutField.CellCenter(CellIndex));
    OutField.Blocked[CellIndex] = bBlocked;
    OutField.BlockedCellCount += bBlocked ? 1 : 0;
  }

  float BestGoalDistanceSq = MAX_flt;
  for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
  {
    if (OutField.Blocked[CellIndex])
    {
      continue;
    }
    const float DistanceSq = FVector::DistSquared2D(OutField.CellCenter(CellIndex), FVector(Config.GoalLocation));
    if (DistanceSq < BestGoalDistanceSq
      || (FMath::IsNearlyEqual(DistanceSq, BestGoalDistanceSq) && CellIndex < OutField.GoalCellIndex))
    {
      BestGoalDistanceSq = DistanceSq;
      OutField.GoalCellIndex = CellIndex;
    }
  }
  if (OutField.GoalCellIndex == INDEX_NONE)
  {
    return false;
  }

  TArray<FQueueNode> Heap;
  OutField.IntegrationCost[OutField.GoalCellIndex] = 0;
  HeapPush(Heap, { 0, OutField.GoalCellIndex });
  while (!Heap.IsEmpty())
  {
    const FQueueNode Current = HeapPop(Heap);
    if (Current.Cost != OutField.IntegrationCost[Current.CellIndex])
    {
      continue;
    }
    const int32 CurrentX = Current.CellIndex % OutField.Width;
    const int32 CurrentY = Current.CellIndex / OutField.Width;
    for (int32 NeighborOrder = 0; NeighborOrder < 8; ++NeighborOrder)
    {
      const int32 NextX = CurrentX + NeighborDx[NeighborOrder];
      const int32 NextY = CurrentY + NeighborDy[NeighborOrder];
      if (NextX < 0 || NextX >= OutField.Width || NextY < 0 || NextY >= OutField.Height)
      {
        continue;
      }
      const int32 NextIndex = NextY * OutField.Width + NextX;
      if (OutField.Blocked[NextIndex])
      {
        continue;
      }
      const bool bDiagonal = NeighborDx[NeighborOrder] != 0 && NeighborDy[NeighborOrder] != 0;
      if (bDiagonal)
      {
        const int32 OrthogonalA = CurrentY * OutField.Width + NextX;
        const int32 OrthogonalB = NextY * OutField.Width + CurrentX;
        if (OutField.Blocked[OrthogonalA] || OutField.Blocked[OrthogonalB])
        {
          continue;
        }
      }
      const int32 StepCost = bDiagonal ? DiagonalCost : StraightCost;
      if (Current.Cost > InfiniteCost - StepCost)
      {
        continue;
      }
      const int32 NewCost = Current.Cost + StepCost;
      if (NewCost < OutField.IntegrationCost[NextIndex])
      {
        OutField.IntegrationCost[NextIndex] = NewCost;
        HeapPush(Heap, { NewCost, NextIndex });
      }
    }
  }

  for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
  {
    if (OutField.Blocked[CellIndex] || OutField.IntegrationCost[CellIndex] == InfiniteCost)
    {
      continue;
    }
    OutField.Unreachable[CellIndex] = false;
    if (CellIndex == OutField.GoalCellIndex)
    {
      continue;
    }
    const int32 CellX = CellIndex % OutField.Width;
    const int32 CellY = CellIndex / OutField.Width;
    int32 BestIndex = INDEX_NONE;
    int32 BestCost = OutField.IntegrationCost[CellIndex];
    for (int32 NeighborOrder = 0; NeighborOrder < 8; ++NeighborOrder)
    {
      const int32 NextX = CellX + NeighborDx[NeighborOrder];
      const int32 NextY = CellY + NeighborDy[NeighborOrder];
      if (NextX < 0 || NextX >= OutField.Width || NextY < 0 || NextY >= OutField.Height)
      {
        continue;
      }
      const int32 NextIndex = NextY * OutField.Width + NextX;
      if (OutField.Blocked[NextIndex])
      {
        continue;
      }
      const bool bDiagonal = NeighborDx[NeighborOrder] != 0 && NeighborDy[NeighborOrder] != 0;
      if (bDiagonal)
      {
        const int32 OrthogonalA = CellY * OutField.Width + NextX;
        const int32 OrthogonalB = NextY * OutField.Width + CellX;
        if (OutField.Blocked[OrthogonalA] || OutField.Blocked[OrthogonalB])
        {
          continue;
        }
      }
      const int32 CandidateCost = OutField.IntegrationCost[NextIndex];
      if (CandidateCost < BestCost || (CandidateCost == BestCost && (BestIndex == INDEX_NONE || NextIndex < BestIndex)))
      {
        BestCost = CandidateCost;
        BestIndex = NextIndex;
      }
    }
    if (BestIndex != INDEX_NONE)
    {
      OutField.NextCellIndex[CellIndex] = BestIndex;
      OutField.FlowDirection[CellIndex] = (OutField.CellCenter(BestIndex) - OutField.CellCenter(CellIndex)).GetSafeNormal2D();
    }
  }

  uint32 Hash = 2166136261u;
  Hash = HashInt(Hash, Config.Revision);
  Hash = HashInt(Hash, Quantize10(Config.CellSizeCm));
  Hash = HashInt(Hash, Quantize10(Config.AgentInflateCm));
  Hash = HashInt(Hash, Quantize10(FVector(Config.GoalLocation).X));
  Hash = HashInt(Hash, Quantize10(FVector(Config.GoalLocation).Y));
  Hash = HashInt(Hash, OutField.Width);
  Hash = HashInt(Hash, OutField.Height);
  for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
  {
    Hash = HashInt(Hash, OutField.Blocked[CellIndex] ? -1 : OutField.IntegrationCost[CellIndex]);
  }
  OutField.BuildHash = Hash;
  return true;
}

FCrowdDemoSharedFlowSample FCrowdDemoSharedFlowFieldKernel::Sample(
  const FCrowdDemoSharedFlowField& Field,
  const FVector& Location)
{
  FCrowdDemoSharedFlowSample Result;
  const int32 CellIndex = Field.LocationToCellIndex(Location);
  Result.CellIndex = CellIndex;
  Result.StableCellKey = CellIndex;
  if (CellIndex == INDEX_NONE || !Field.IsValid())
  {
    Result.Status = ECrowdDemoFlowLocationStatus::OutOfBounds;
    return Result;
  }
  const int32 NextCellIndex = Field.NextCellIndex[CellIndex];
  Result.FlowDirection = NextCellIndex != INDEX_NONE
    ? (Field.CellCenter(NextCellIndex) - Location).GetSafeNormal2D()
    : Field.FlowDirection[CellIndex];
  Result.IntegrationCost = Field.IntegrationCost[CellIndex];
  Result.bBlocked = Field.Blocked[CellIndex];
  Result.bUnreachable = Field.Unreachable[CellIndex];
  Result.Status = Result.bBlocked
    ? ECrowdDemoFlowLocationStatus::BlockedRasterCell
    : (Result.bUnreachable
      ? ECrowdDemoFlowLocationStatus::UnreachableFreeCell
      : ECrowdDemoFlowLocationStatus::Reachable);
  return Result;
}

FCrowdDemoReachableFlowCellSearchResult FCrowdDemoSharedFlowFieldKernel::FindNearestReachableCell(
  const FCrowdDemoSharedFlowField& Field,
  const FVector& Location,
  const int32 MaximumRingDistance)
{
  struct FCandidate
  {
    int32 CellIndex = INDEX_NONE;
    int32 RingDistance = MAX_int32;
    int32 WorldDistanceBucket = MAX_int32;
    int32 IntegrationCost = MAX_int32;
    float WorldDistanceCm = MAX_flt;
  };
  FCrowdDemoReachableFlowCellSearchResult Result;
  if (!Field.IsValid()) return Result;
  const FVector Min = FVector(Field.Config.BoundsMin);
  const int32 BaseX = FMath::Clamp(
    FMath::FloorToInt((Location.X - Min.X) / Field.Config.CellSizeCm), 0, Field.Width - 1);
  const int32 BaseY = FMath::Clamp(
    FMath::FloorToInt((Location.Y - Min.Y) / Field.Config.CellSizeCm), 0, Field.Height - 1);
  TArray<FCandidate> Candidates;
  const int32 MaxRing = FMath::Max(0, MaximumRingDistance);
  for (int32 Y = FMath::Max(0, BaseY - MaxRing); Y <= FMath::Min(Field.Height - 1, BaseY + MaxRing); ++Y)
  {
    for (int32 X = FMath::Max(0, BaseX - MaxRing); X <= FMath::Min(Field.Width - 1, BaseX + MaxRing); ++X)
    {
      const int32 Ring = FMath::Max(FMath::Abs(X - BaseX), FMath::Abs(Y - BaseY));
      if (Ring > MaxRing) continue;
      const int32 CellIndex = Y * Field.Width + X;
      if (Field.Blocked[CellIndex] || Field.Unreachable[CellIndex]) continue;
      const FVector Center = Field.CellCenter(CellIndex);
      if (!IsSegmentClear(Field.Config, Location, Center)) continue;
      const float DistanceCm = FVector::Dist2D(Location, Center);
      Candidates.Add({CellIndex, Ring, FMath::RoundToInt(DistanceCm),
        Field.IntegrationCost[CellIndex], DistanceCm});
    }
  }
  Candidates.Sort([](const FCandidate& A, const FCandidate& B)
  {
    if (A.RingDistance != B.RingDistance) return A.RingDistance < B.RingDistance;
    if (A.WorldDistanceBucket != B.WorldDistanceBucket) return A.WorldDistanceBucket < B.WorldDistanceBucket;
    if (A.IntegrationCost != B.IntegrationCost) return A.IntegrationCost < B.IntegrationCost;
    return A.CellIndex < B.CellIndex;
  });
  if (Candidates.IsEmpty()) return Result;
  const FCandidate& Best = Candidates[0];
  Result.bFound = true;
  Result.CellIndex = Best.CellIndex;
  Result.StableCellKey = Best.CellIndex;
  Result.RingDistance = Best.RingDistance;
  Result.WorldDistanceCm = Best.WorldDistanceCm;
  Result.IntegrationCost = Best.IntegrationCost;
  Result.CellCenter = Field.CellCenter(Best.CellIndex);
  const int32 Next = Field.NextCellIndex[Best.CellIndex];
  Result.FlowDirection = Next != INDEX_NONE
    ? (Field.CellCenter(Next) - Location).GetSafeNormal2D()
    : Field.FlowDirection[Best.CellIndex];
  return Result;
}

FCrowdDemoSharedFlowConstraintResult FCrowdDemoSharedFlowFieldKernel::ConstrainMovement(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& Start,
  const FVector& Proposed,
  const float FixedStepSeconds,
  const bool bConstrainToFlowBounds)
{
  FCrowdDemoSharedFlowConstraintResult Result;
  FVector DomainProposed = Proposed;
  if (bConstrainToFlowBounds)
  {
    const FVector BoundsMin = FVector(Config.BoundsMin);
    const FVector BoundsMax = FVector(Config.BoundsMax);
    DomainProposed.X = FMath::Clamp(DomainProposed.X, BoundsMin.X, BoundsMax.X - 0.01f);
    DomainProposed.Y = FMath::Clamp(DomainProposed.Y, BoundsMin.Y, BoundsMax.Y - 0.01f);
    Result.FlowBoundsReprojectDeltaCm = FVector::Dist2D(DomainProposed, Proposed);
    Result.bHitFlowBounds = Result.FlowBoundsReprojectDeltaCm > 0.001f;
  }
  Result.Location = DomainProposed;
  Result.Velocity = FixedStepSeconds > SMALL_NUMBER ? (DomainProposed - Start) / FixedStepSeconds : FVector::ZeroVector;
  Result.bPenetrating = IsInsideInflatedObstacle(Config, Start);
  if (IsSegmentClear(Config, Start, DomainProposed) && !IsInsideInflatedObstacle(Config, DomainProposed))
  {
    return Result;
  }

  Result.bHitObstacle = true;
  const FVector SlideX(DomainProposed.X, Start.Y, Start.Z);
  const FVector SlideY(Start.X, DomainProposed.Y, Start.Z);
  const bool bSlideX = IsSegmentClear(Config, Start, SlideX) && !IsInsideInflatedObstacle(Config, SlideX);
  const bool bSlideY = IsSegmentClear(Config, Start, SlideY) && !IsInsideInflatedObstacle(Config, SlideY);
  if (bSlideX || bSlideY)
  {
    const float XProgressSq = bSlideX ? FVector::DistSquared2D(Start, SlideX) : -1.0f;
    const float YProgressSq = bSlideY ? FVector::DistSquared2D(Start, SlideY) : -1.0f;
    Result.Location = XProgressSq >= YProgressSq ? SlideX : SlideY;
    Result.Velocity = FixedStepSeconds > SMALL_NUMBER ? (Result.Location - Start) / FixedStepSeconds : FVector::ZeroVector;
    return Result;
  }

  Result.Location = Start;
  Result.Velocity = FVector::ZeroVector;
  return Result;
}

FCrowdDemoSharedFlowConstraintDiagnostic
FCrowdDemoSharedFlowFieldKernel::DiagnoseMovementConstraint(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& Start,
  const FVector& Proposed,
  const bool bConstrainToFlowBounds)
{
  struct FIntersection
  {
    int32 ObstacleId = INDEX_NONE;
    FVector Min = FVector::ZeroVector;
    FVector Max = FVector::ZeroVector;
    float EntryT = -1.0f;
    float ExitT = -1.0f;
    bool bStartInside = false;
    bool bEndInside = false;
  };
  FCrowdDemoSharedFlowConstraintDiagnostic Result;
  Result.Start = Start;
  Result.Proposed = Proposed;
  Result.DomainProposed = Proposed;
  if (bConstrainToFlowBounds)
  {
    const FVector BoundsMin = FVector(Config.BoundsMin);
    const FVector BoundsMax = FVector(Config.BoundsMax);
    Result.DomainProposed.X = FMath::Clamp(
      Result.DomainProposed.X, BoundsMin.X, BoundsMax.X - 0.01f);
    Result.DomainProposed.Y = FMath::Clamp(
      Result.DomainProposed.Y, BoundsMin.Y, BoundsMax.Y - 0.01f);
    Result.FlowBoundsReprojectDeltaCm = FVector::Dist2D(Result.DomainProposed, Proposed);
    Result.bHitFlowBounds = Result.FlowBoundsReprojectDeltaCm > 0.001f;
  }
  Result.SlideX = FVector(Result.DomainProposed.X, Start.Y, Start.Z);
  Result.SlideY = FVector(Start.X, Result.DomainProposed.Y, Start.Z);
  TArray<FIntersection> Intersections;
  for (const FCrowdDemoSharedFlowObstacleSpec& Obstacle : Config.ObstacleSpecs)
  {
    const FVector Inflate(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
    const FVector Min = FVector(Obstacle.Center) - FVector(Obstacle.Extent) - Inflate;
    const FVector Max = FVector(Obstacle.Center) + FVector(Obstacle.Extent) + Inflate;
    Result.bStartInsideAnyObstacle |= IsPointInsideBox2D(Start, Min, Max);
    Result.bEndInsideAnyObstacle |= IsPointInsideBox2D(Result.DomainProposed, Min, Max);
    float EntryT = -1.0f;
    float ExitT = -1.0f;
    if (!SegmentBoxInterval2D(Start, Result.DomainProposed, Min, Max, EntryT, ExitT)) continue;
    FIntersection& Intersection = Intersections.AddDefaulted_GetRef();
    Intersection.ObstacleId = Obstacle.ObstacleId;
    Intersection.Min = Min;
    Intersection.Max = Max;
    Intersection.EntryT = EntryT;
    Intersection.ExitT = ExitT;
    Intersection.bStartInside = IsPointInsideBox2D(Start, Min, Max);
    Intersection.bEndInside = IsPointInsideBox2D(Result.DomainProposed, Min, Max);
  }
  Intersections.Sort([](const FIntersection& A, const FIntersection& B)
  {
    const int32 AEntry = FMath::RoundToInt(A.EntryT * 1000000.0f);
    const int32 BEntry = FMath::RoundToInt(B.EntryT * 1000000.0f);
    if (AEntry != BEntry) return AEntry < BEntry;
    return A.ObstacleId < B.ObstacleId;
  });
  for (const FIntersection& Intersection : Intersections)
    Result.IntersectedObstacleIds.Add(Intersection.ObstacleId);
  Result.IntersectedObstacleIds.Sort();
  Result.bDirectSegmentClear = Intersections.IsEmpty() && !Result.bEndInsideAnyObstacle;
  Result.bSlideXClear = IsSegmentClear(Config, Start, Result.SlideX)
    && !IsInsideInflatedObstacle(Config, Result.SlideX);
  Result.bSlideYClear = IsSegmentClear(Config, Start, Result.SlideY)
    && !IsInsideInflatedObstacle(Config, Result.SlideY);
  if (!Intersections.IsEmpty())
  {
    const FIntersection& Selected = Intersections[0];
    Result.SelectedObstacleId = Selected.ObstacleId;
    Result.SelectedInflatedMin = Selected.Min;
    Result.SelectedInflatedMax = Selected.Max;
    Result.SelectedSegmentEntryT = Selected.EntryT;
    Result.SelectedSegmentExitT = Selected.ExitT;
    Result.bStartInsideSelectedObstacle = Selected.bStartInside;
    Result.bEndInsideSelectedObstacle = Selected.bEndInside;
  }
  uint32 Hash = 2166136261u;
  const auto Fold = [&](const int32 Value) { Hash = HashInt(Hash, Value); };
  const auto FoldPoint = [&](const FVector& Value)
  {
    Fold(FMath::RoundToInt(Value.X));
    Fold(FMath::RoundToInt(Value.Y));
  };
  FoldPoint(Result.Start);
  FoldPoint(Result.Proposed);
  FoldPoint(Result.DomainProposed);
  FoldPoint(Result.SlideX);
  FoldPoint(Result.SlideY);
  Fold(FMath::RoundToInt(Result.FlowBoundsReprojectDeltaCm));
  Fold(Result.bHitFlowBounds ? 1 : 0);
  Fold(Result.bStartInsideAnyObstacle ? 1 : 0);
  Fold(Result.bEndInsideAnyObstacle ? 1 : 0);
  Fold(Result.bDirectSegmentClear ? 1 : 0);
  Fold(Result.bSlideXClear ? 1 : 0);
  Fold(Result.bSlideYClear ? 1 : 0);
  Fold(Result.SelectedObstacleId);
  FoldPoint(Result.SelectedInflatedMin);
  FoldPoint(Result.SelectedInflatedMax);
  Fold(FMath::RoundToInt(Result.SelectedSegmentEntryT * 1000000.0f));
  Fold(FMath::RoundToInt(Result.SelectedSegmentExitT * 1000000.0f));
  Fold(Result.bStartInsideSelectedObstacle ? 1 : 0);
  Fold(Result.bEndInsideSelectedObstacle ? 1 : 0);
  for (const int32 ObstacleId : Result.IntersectedObstacleIds) Fold(ObstacleId);
  Result.StableHash = Hash;
  Result.bValid = true;
  return Result;
}

bool FCrowdDemoSharedFlowFieldKernel::IsInsideInflatedObstacle(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& Location)
{
  for (const FCrowdDemoSharedFlowObstacleSpec& Obstacle : Config.ObstacleSpecs)
  {
    const FVector Center = FVector(Obstacle.Center);
    const FVector Extent = FVector(Obstacle.Extent);
    if (FMath::Abs(Location.X - Center.X) <= Extent.X + Config.AgentInflateCm
      && FMath::Abs(Location.Y - Center.Y) <= Extent.Y + Config.AgentInflateCm)
    {
      return true;
    }
  }
  return false;
}
