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

  bool IsInsideContractBounds(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Point)
  {
    if (Config.ConnectivityContractVersion <= 0) return true;
    const FVector Min = FVector(Config.BoundsMin)
      + FVector(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
    const FVector Max = FVector(Config.BoundsMax)
      - FVector(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
    return Point.X >= Min.X && Point.X <= Max.X
      && Point.Y >= Min.Y && Point.Y <= Max.Y;
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
  NavigationSafeIntervals.Reset();
  NavigationNodes.Reset();
  NavigationCellNodes.Reset();
  NavigationEdges.Reset();
  NavigationIntegrationCost.Reset();
  NavigationNextNodeIndex.Reset();
  GoalAttachmentNodeIndices.Reset();
  Width = 0;
  Height = 0;
  GoalCellIndex = INDEX_NONE;
  BlockedCellCount = 0;
  ValidDirectedEdgeCount = 0;
  NavigationCenterAnchorCount = 0;
  NavigationConnectionPointCount = 0;
  NavigationSafeIntervalCount = 0;
  NavigationInternalEdgeCount = 0;
  CenterInvalidButConnectedCellCount = 0;
  GoalAttachmentCount = 0;
  TopologyHash = 0;
  IntegrationHash = 0;
  BuildHash = 0;
}

bool FCrowdDemoSharedFlowFieldKernel::CanTraverseCellEdge(
  const FCrowdDemoSharedFlowField& Field,
  const int32 FromCellIndex,
  const int32 ToCellIndex)
{
  if (!Field.IsValid()
    || FromCellIndex < 0 || FromCellIndex >= Field.Width * Field.Height
    || ToCellIndex < 0 || ToCellIndex >= Field.Width * Field.Height
    || Field.Blocked[FromCellIndex] || Field.Blocked[ToCellIndex])
    return false;
  const int32 FromX = FromCellIndex % Field.Width;
  const int32 FromY = FromCellIndex / Field.Width;
  const int32 ToX = ToCellIndex % Field.Width;
  const int32 ToY = ToCellIndex / Field.Width;
  const int32 DeltaX = FMath::Abs(ToX - FromX);
  const int32 DeltaY = FMath::Abs(ToY - FromY);
  if (DeltaX > 1 || DeltaY > 1 || DeltaX + DeltaY == 0) return false;
  if (DeltaX == 1 && DeltaY == 1)
  {
    const int32 OrthogonalA = FromY * Field.Width + ToX;
    const int32 OrthogonalB = ToY * Field.Width + FromX;
    if (Field.Blocked[OrthogonalA] || Field.Blocked[OrthogonalB]) return false;
  }
  return Field.Config.ConnectivityContractVersion <= 0
    || CanTraverseWorldSegment(
      Field.Config, Field.CellCenter(FromCellIndex), Field.CellCenter(ToCellIndex));
}

bool FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  const FVector& Start,
  const FVector& End)
{
  return IsInsideContractBounds(Config, Start)
    && IsInsideContractBounds(Config, End)
    && IsSegmentClear(Config, Start, End);
}

namespace
{
  uint64 FoldNavigationKey(uint64 Hash, const int64 Value)
  {
    uint64 Bits = static_cast<uint64>(Value);
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= (Bits >> (Byte * 8)) & 0xffull;
      Hash *= 1099511628211ull;
    }
    return Hash;
  }

  uint64 MakeCenterNodeKey(const int32 Version, const int32 CellKey)
  {
    uint64 Hash = 1469598103934665603ull;
    Hash = FoldNavigationKey(Hash, Version);
    Hash = FoldNavigationKey(Hash,
      static_cast<int32>(ECrowdDemoNavigationNodeKind::CenterAnchor));
    return FoldNavigationKey(Hash, CellKey);
  }

  uint64 MakeConnectionNodeKey(
    const int32 Version,
    const ECrowdDemoNavigationNodeKind Kind,
    const int32 PrimaryCell,
    const int32 SecondaryCell,
    const int32 IntervalMin,
    const int32 IntervalMax,
    const int32 Ordinal)
  {
    uint64 Hash = 1469598103934665603ull;
    Hash = FoldNavigationKey(Hash, Version);
    Hash = FoldNavigationKey(Hash, static_cast<int32>(Kind));
    Hash = FoldNavigationKey(Hash, PrimaryCell);
    Hash = FoldNavigationKey(Hash, SecondaryCell);
    Hash = FoldNavigationKey(Hash, IntervalMin);
    Hash = FoldNavigationKey(Hash, IntervalMax);
    return FoldNavigationKey(Hash, Ordinal);
  }

  FVector NavigationNodeLocation(
    const FCrowdDemoSharedFlowField& Field,
    const FCrowdDemoNavigationNode& Node)
  {
    return FVector(
      static_cast<float>(Node.QuantizedLocationCm.X),
      static_cast<float>(Node.QuantizedLocationCm.Y),
      FVector(Field.Config.GoalLocation).Z);
  }

  bool NavigationNodeBelongsToCell(
    const FCrowdDemoNavigationNode& Node,
    const int32 CellKey)
  {
    return Node.PrimaryCellKey == CellKey || Node.SecondaryCellKey == CellKey;
  }

  int32 NavigationNodeRingDistance(
    const FCrowdDemoSharedFlowField& Field,
    const FCrowdDemoNavigationNode& Node,
    const int32 BaseCell)
  {
    if (BaseCell == INDEX_NONE) return MAX_int32;
    const int32 BaseX = BaseCell % Field.Width;
    const int32 BaseY = BaseCell / Field.Width;
    auto CellRing = [&](const int32 Cell)
    {
      if (Cell == INDEX_NONE) return MAX_int32;
      return FMath::Max(
        FMath::Abs(Cell % Field.Width - BaseX),
        FMath::Abs(Cell / Field.Width - BaseY));
    };
    return FMath::Min(CellRing(Node.PrimaryCellKey), CellRing(Node.SecondaryCellKey));
  }

  struct FNavigationAttachmentCandidate
  {
    int32 NodeIndex = INDEX_NONE;
    int32 Ring = MAX_int32;
    int32 QuantizedDistanceCm = MAX_int32;
    int32 IntegrationCost = MAX_int32;
    uint64 StableNodeKey = 0;
  };

  void SortNavigationAttachmentCandidates(TArray<FNavigationAttachmentCandidate>& Candidates)
  {
    Candidates.Sort([](const auto& A, const auto& B)
    {
      if (A.Ring != B.Ring) return A.Ring < B.Ring;
      if (A.QuantizedDistanceCm != B.QuantizedDistanceCm)
        return A.QuantizedDistanceCm < B.QuantizedDistanceCm;
      if (A.IntegrationCost != B.IntegrationCost)
        return A.IntegrationCost < B.IntegrationCost;
      return A.StableNodeKey < B.StableNodeKey;
    });
  }

  TArray<FNavigationAttachmentCandidate> FindNavigationAttachments(
    const FCrowdDemoSharedFlowField& Field,
    const FVector& Location,
    const bool bRequireReachable)
  {
    TArray<FNavigationAttachmentCandidate> Candidates;
    const int32 BaseCell = Field.LocationToCellIndex(Location);
    if (BaseCell == INDEX_NONE) return Candidates;
    const int32 MaxRing = FMath::Max(Field.Width, Field.Height);
    const bool bHasCellNodeCache = Field.NavigationCellNodes.Num()
      == Field.Width * Field.Height;
    for (int32 Ring = 0; Ring <= MaxRing && Candidates.IsEmpty(); ++Ring)
    {
      TArray<int32> RingNodeIndices;
      if (bHasCellNodeCache)
      {
        const int32 BaseX = BaseCell % Field.Width;
        const int32 BaseY = BaseCell / Field.Width;
        const int32 MinX = FMath::Max(0, BaseX - Ring);
        const int32 MaxX = FMath::Min(Field.Width - 1, BaseX + Ring);
        const int32 MinY = FMath::Max(0, BaseY - Ring);
        const int32 MaxY = FMath::Min(Field.Height - 1, BaseY + Ring);
        auto AppendCell = [&](const int32 X, const int32 Y)
        {
          RingNodeIndices.Append(Field.NavigationCellNodes[Y * Field.Width + X]);
        };
        for (int32 X = MinX; X <= MaxX; ++X)
        {
          AppendCell(X, MinY);
          if (MaxY != MinY) AppendCell(X, MaxY);
        }
        for (int32 Y = MinY + 1; Y < MaxY; ++Y)
        {
          AppendCell(MinX, Y);
          if (MaxX != MinX) AppendCell(MaxX, Y);
        }
        RingNodeIndices.Sort();
        for (int32 Index = RingNodeIndices.Num() - 1; Index > 0; --Index)
          if (RingNodeIndices[Index] == RingNodeIndices[Index - 1])
            RingNodeIndices.RemoveAt(Index, 1, EAllowShrinking::No);
      }
      else
      {
        RingNodeIndices.Reserve(Field.NavigationNodes.Num());
        for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
          RingNodeIndices.Add(NodeIndex);
      }
      for (const int32 NodeIndex : RingNodeIndices)
      {
        const auto& Node = Field.NavigationNodes[NodeIndex];
        if (NavigationNodeRingDistance(Field, Node, BaseCell) != Ring) continue;
        if (bRequireReachable
          && (!Field.NavigationIntegrationCost.IsValidIndex(NodeIndex)
            || Field.NavigationIntegrationCost[NodeIndex] == InfiniteCost))
          continue;
        const FVector NodeLocation = NavigationNodeLocation(Field, Node);
        if (!FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
            Field.Config, Location, NodeLocation))
          continue;
        const float DistanceCm = FVector::Dist2D(Location, NodeLocation);
        Candidates.Add({NodeIndex, Ring, FMath::RoundToInt(DistanceCm),
          bRequireReachable ? Field.NavigationIntegrationCost[NodeIndex] : 0,
          Node.StableNodeKey});
      }
    }
    SortNavigationAttachmentCandidates(Candidates);
    return Candidates;
  }

  struct FBlockedNavigationInterval
  {
    int32 MinCm = 0;
    int32 MaxCm = 0;
    int32 ObstacleId = INDEX_NONE;
  };

  void BuildConnectionIntervals(
    const FCrowdDemoSharedFlowField& Field,
    const ECrowdDemoNavigationNodeKind Kind,
    const int32 PrimaryCell,
    const int32 SecondaryCell,
    const float FixedCoordinate,
    const float SegmentMin,
    const float SegmentMax,
    TArray<FCrowdDemoNavigationSafeInterval>& OutIntervals,
    TArray<FCrowdDemoNavigationNode>& OutNodes)
  {
    const auto& Config = Field.Config;
    const FVector ContractMin = FVector(Config.BoundsMin)
      + FVector(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
    const FVector ContractMax = FVector(Config.BoundsMax)
      - FVector(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
    const bool bVertical = Kind == ECrowdDemoNavigationNodeKind::VerticalEdgeConnection;
    const float FixedMin = bVertical ? ContractMin.X : ContractMin.Y;
    const float FixedMax = bVertical ? ContractMax.X : ContractMax.Y;
    if (FixedCoordinate < FixedMin || FixedCoordinate > FixedMax) return;
    const float ClippedMin = FMath::Max(
      SegmentMin, bVertical ? ContractMin.Y : ContractMin.X);
    const float ClippedMax = FMath::Min(
      SegmentMax, bVertical ? ContractMax.Y : ContractMax.X);
    if (ClippedMax < ClippedMin) return;

    TArray<FBlockedNavigationInterval> Blockers;
    for (const auto& Obstacle : Config.ObstacleSpecs)
    {
      const FVector Inflate(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
      const FVector Min = FVector(Obstacle.Center) - FVector(Obstacle.Extent) - Inflate;
      const FVector Max = FVector(Obstacle.Center) + FVector(Obstacle.Extent) + Inflate;
      const float ObstacleFixedMin = bVertical ? Min.X : Min.Y;
      const float ObstacleFixedMax = bVertical ? Max.X : Max.Y;
      if (FixedCoordinate < ObstacleFixedMin || FixedCoordinate > ObstacleFixedMax) continue;
      const float VariableMin = bVertical ? Min.Y : Min.X;
      const float VariableMax = bVertical ? Max.Y : Max.X;
      const int32 QuantizedMin = FMath::CeilToInt(FMath::Max(ClippedMin, VariableMin));
      const int32 QuantizedMax = FMath::FloorToInt(FMath::Min(ClippedMax, VariableMax));
      if (QuantizedMin <= QuantizedMax)
        Blockers.Add({QuantizedMin, QuantizedMax, Obstacle.ObstacleId});
    }
    Blockers.Sort([](const auto& A, const auto& B)
    {
      if (A.MinCm != B.MinCm) return A.MinCm < B.MinCm;
      if (A.MaxCm != B.MaxCm) return A.MaxCm < B.MaxCm;
      return A.ObstacleId < B.ObstacleId;
    });

    const int32 First = FMath::CeilToInt(ClippedMin);
    const int32 Last = FMath::FloorToInt(ClippedMax);
    int32 RunStart = INDEX_NONE;
    int32 Ordinal = 0;
    auto FlushRun = [&](const int32 RunEnd)
    {
      if (RunStart == INDEX_NONE || RunEnd < RunStart) return;
      FCrowdDemoNavigationSafeInterval Interval;
      Interval.Kind = Kind;
      Interval.PrimaryCellKey = PrimaryCell;
      Interval.SecondaryCellKey = SecondaryCell;
      Interval.IntervalOrdinal = Ordinal++;
      Interval.QuantizedMinCm = RunStart;
      Interval.QuantizedMaxCm = RunEnd;
      const int32 Midpoint = RunStart + (RunEnd - RunStart) / 2;
      const FIntPoint Point = bVertical
        ? FIntPoint(FMath::RoundToInt(FixedCoordinate), Midpoint)
        : FIntPoint(Midpoint, FMath::RoundToInt(FixedCoordinate));
      // ClippedMin/ClippedMax already apply the contracted flow bounds and
      // Blockers are the inclusive projections of every inflated AABB onto
      // this axis.  Re-testing the midpoint through CanTraverseWorldSegment
      // would scan every obstacle again without adding a different contract.
      OutIntervals.Add(Interval);
      FCrowdDemoNavigationNode Node;
      Node.Kind = Kind;
      Node.PrimaryCellKey = PrimaryCell;
      Node.SecondaryCellKey = SecondaryCell;
      Node.IntervalOrdinal = Interval.IntervalOrdinal;
      Node.QuantizedLocationCm = Point;
      Node.StableNodeKey = MakeConnectionNodeKey(
        Config.ConnectivityContractVersion, Kind, PrimaryCell, SecondaryCell,
        RunStart, RunEnd, Interval.IntervalOrdinal);
      OutNodes.Add(Node);
    };

    for (int32 Coordinate = First; Coordinate <= Last; ++Coordinate)
    {
      bool bBlocked = false;
      for (const auto& Blocker : Blockers)
      {
        if (Coordinate < Blocker.MinCm) break;
        if (Coordinate <= Blocker.MaxCm)
        {
          bBlocked = true;
          break;
        }
      }
      // The point predicate is exactly the inclusive interval test above for
      // the production AABB obstacle contract.  Avoid the former O(cm samples
      // * obstacle count) duplicate scan; interval endpoints remain unchanged.
      if (!bBlocked && RunStart == INDEX_NONE) RunStart = Coordinate;
      if (bBlocked && RunStart != INDEX_NONE)
      {
        FlushRun(Coordinate - 1);
        RunStart = INDEX_NONE;
      }
    }
    if (RunStart != INDEX_NONE) FlushRun(Last);
  }

  bool BuildV2NavigationGraph(FCrowdDemoSharedFlowField& Field)
  {
    const int32 CellCount = Field.Width * Field.Height;
    const auto& Config = Field.Config;
    for (int32 Cell = 0; Cell < CellCount; ++Cell)
    {
      if (Field.Blocked[Cell]) continue;
      FCrowdDemoNavigationNode Node;
      Node.StableNodeKey = MakeCenterNodeKey(Config.ConnectivityContractVersion, Cell);
      Node.Kind = ECrowdDemoNavigationNodeKind::CenterAnchor;
      Node.PrimaryCellKey = Cell;
      const FVector Center = Field.CellCenter(Cell);
      Node.QuantizedLocationCm = FIntPoint(
        FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
      Field.NavigationNodes.Add(Node);
      ++Field.NavigationCenterAnchorCount;
    }

    const FVector BoundsMin = FVector(Config.BoundsMin);
    for (int32 Y = 0; Y < Field.Height; ++Y)
    {
      for (int32 X = 0; X < Field.Width; ++X)
      {
        const int32 Cell = Y * Field.Width + X;
        if (X + 1 < Field.Width)
        {
          BuildConnectionIntervals(Field,
            ECrowdDemoNavigationNodeKind::VerticalEdgeConnection,
            Cell, Cell + 1,
            BoundsMin.X + static_cast<float>(X + 1) * Config.CellSizeCm,
            BoundsMin.Y + static_cast<float>(Y) * Config.CellSizeCm,
            BoundsMin.Y + static_cast<float>(Y + 1) * Config.CellSizeCm,
            Field.NavigationSafeIntervals, Field.NavigationNodes);
        }
        if (Y + 1 < Field.Height)
        {
          BuildConnectionIntervals(Field,
            ECrowdDemoNavigationNodeKind::HorizontalEdgeConnection,
            Cell, Cell + Field.Width,
            BoundsMin.Y + static_cast<float>(Y + 1) * Config.CellSizeCm,
            BoundsMin.X + static_cast<float>(X) * Config.CellSizeCm,
            BoundsMin.X + static_cast<float>(X + 1) * Config.CellSizeCm,
            Field.NavigationSafeIntervals, Field.NavigationNodes);
        }
      }
    }
    Field.NavigationSafeIntervals.Sort([](const auto& A, const auto& B)
    {
      if (A.Kind != B.Kind) return static_cast<int32>(A.Kind) < static_cast<int32>(B.Kind);
      if (A.PrimaryCellKey != B.PrimaryCellKey) return A.PrimaryCellKey < B.PrimaryCellKey;
      if (A.SecondaryCellKey != B.SecondaryCellKey) return A.SecondaryCellKey < B.SecondaryCellKey;
      if (A.QuantizedMinCm != B.QuantizedMinCm) return A.QuantizedMinCm < B.QuantizedMinCm;
      if (A.QuantizedMaxCm != B.QuantizedMaxCm) return A.QuantizedMaxCm < B.QuantizedMaxCm;
      return A.IntervalOrdinal < B.IntervalOrdinal;
    });
    Field.NavigationNodes.Sort([](const auto& A, const auto& B)
    {
      return A.StableNodeKey < B.StableNodeKey;
    });
    for (int32 Index = 1; Index < Field.NavigationNodes.Num(); ++Index)
      if (Field.NavigationNodes[Index - 1].StableNodeKey
        == Field.NavigationNodes[Index].StableNodeKey)
        return false;
    Field.NavigationConnectionPointCount =
      Field.NavigationNodes.Num() - Field.NavigationCenterAnchorCount;
    Field.NavigationSafeIntervalCount = Field.NavigationSafeIntervals.Num();
    if (Field.NavigationNodes.IsEmpty()) return false;

    Field.NavigationCellNodes.SetNum(CellCount);
    for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
    {
      const auto& Node = Field.NavigationNodes[NodeIndex];
      if (Field.NavigationCellNodes.IsValidIndex(Node.PrimaryCellKey))
        Field.NavigationCellNodes[Node.PrimaryCellKey].Add(NodeIndex);
      if (Node.SecondaryCellKey != INDEX_NONE
        && Node.SecondaryCellKey != Node.PrimaryCellKey
        && Field.NavigationCellNodes.IsValidIndex(Node.SecondaryCellKey))
        Field.NavigationCellNodes[Node.SecondaryCellKey].Add(NodeIndex);
    }
    for (int32 Cell = 0; Cell < CellCount; ++Cell)
    {
      Field.NavigationCellNodes[Cell].Sort([&](const int32 A, const int32 B)
      {
        return Field.NavigationNodes[A].StableNodeKey
          < Field.NavigationNodes[B].StableNodeKey;
      });
      for (int32 A = 0; A < Field.NavigationCellNodes[Cell].Num(); ++A)
      {
        for (int32 B = A + 1; B < Field.NavigationCellNodes[Cell].Num(); ++B)
        {
          const auto& NodeA = Field.NavigationNodes[Field.NavigationCellNodes[Cell][A]];
          const auto& NodeB = Field.NavigationNodes[Field.NavigationCellNodes[Cell][B]];
          if (!FCrowdDemoSharedFlowFieldKernel::CanTraverseWorldSegment(
              Config, NavigationNodeLocation(Field, NodeA),
              NavigationNodeLocation(Field, NodeB)))
            continue;
          FCrowdDemoNavigationEdge Edge;
          Edge.MinNodeKey = FMath::Min(NodeA.StableNodeKey, NodeB.StableNodeKey);
          Edge.MaxNodeKey = FMath::Max(NodeA.StableNodeKey, NodeB.StableNodeKey);
          Edge.QuantizedCost = FMath::Max(1, FMath::RoundToInt(
            FVector::Dist2D(NavigationNodeLocation(Field, NodeA),
              NavigationNodeLocation(Field, NodeB)) * 10.0f));
          Field.NavigationEdges.Add(Edge);
        }
      }
    }
    Field.NavigationEdges.Sort([](const auto& A, const auto& B)
    {
      if (A.MinNodeKey != B.MinNodeKey) return A.MinNodeKey < B.MinNodeKey;
      if (A.MaxNodeKey != B.MaxNodeKey) return A.MaxNodeKey < B.MaxNodeKey;
      return A.QuantizedCost < B.QuantizedCost;
    });
    for (int32 Index = Field.NavigationEdges.Num() - 1; Index > 0; --Index)
    {
      if (Field.NavigationEdges[Index].MinNodeKey == Field.NavigationEdges[Index - 1].MinNodeKey
        && Field.NavigationEdges[Index].MaxNodeKey == Field.NavigationEdges[Index - 1].MaxNodeKey)
        Field.NavigationEdges.RemoveAt(Index, 1, EAllowShrinking::No);
    }
    Field.NavigationInternalEdgeCount = Field.NavigationEdges.Num();
    Field.ValidDirectedEdgeCount = Field.NavigationEdges.Num() * 2;

    TMap<uint64, int32> NodeIndexByKey;
    for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
      NodeIndexByKey.Add(Field.NavigationNodes[NodeIndex].StableNodeKey, NodeIndex);
    TSet<uint64> EdgeIndexKeys;
    EdgeIndexKeys.Reserve(Field.NavigationEdges.Num());
    TArray<TArray<TPair<int32, int32>>> Adjacency;
    Adjacency.SetNum(Field.NavigationNodes.Num());
    for (const auto& Edge : Field.NavigationEdges)
    {
      const int32* A = NodeIndexByKey.Find(Edge.MinNodeKey);
      const int32* B = NodeIndexByKey.Find(Edge.MaxNodeKey);
      if (!A || !B) return false;
      const uint32 MinIndex = static_cast<uint32>(FMath::Min(*A, *B));
      const uint32 MaxIndex = static_cast<uint32>(FMath::Max(*A, *B));
      EdgeIndexKeys.Add((static_cast<uint64>(MinIndex) << 32) | MaxIndex);
      Adjacency[*A].Emplace(*B, Edge.QuantizedCost);
      Adjacency[*B].Emplace(*A, Edge.QuantizedCost);
    }
    for (auto& Neighbors : Adjacency)
      Neighbors.Sort([&](const auto& A, const auto& B)
      {
        const uint64 KeyA = Field.NavigationNodes[A.Key].StableNodeKey;
        const uint64 KeyB = Field.NavigationNodes[B.Key].StableNodeKey;
        return KeyA != KeyB ? KeyA < KeyB : A.Value < B.Value;
      });

    Field.GoalCellIndex = Field.LocationToCellIndex(FVector(Config.GoalLocation));
    if (Field.GoalCellIndex == INDEX_NONE) return false;
    const TArray<FNavigationAttachmentCandidate> GoalAttachments =
      FindNavigationAttachments(Field, FVector(Config.GoalLocation), false);
    if (GoalAttachments.IsEmpty()) return false;
    for (const auto& Attachment : GoalAttachments)
      Field.GoalAttachmentNodeIndices.Add(Attachment.NodeIndex);
    Field.GoalAttachmentCount = Field.GoalAttachmentNodeIndices.Num();

    Field.NavigationIntegrationCost.Init(InfiniteCost, Field.NavigationNodes.Num());
    Field.NavigationNextNodeIndex.Init(INDEX_NONE, Field.NavigationNodes.Num());
    TArray<FQueueNode> Heap;
    for (const int32 NodeIndex : Field.GoalAttachmentNodeIndices)
    {
      const int32 GoalCost = FMath::RoundToInt(FVector::Dist2D(
        NavigationNodeLocation(Field, Field.NavigationNodes[NodeIndex]),
        FVector(Config.GoalLocation)) * 10.0f);
      if (GoalCost < Field.NavigationIntegrationCost[NodeIndex])
      {
        Field.NavigationIntegrationCost[NodeIndex] = GoalCost;
        HeapPush(Heap, {GoalCost, NodeIndex});
      }
    }
    while (!Heap.IsEmpty())
    {
      const FQueueNode Current = HeapPop(Heap);
      if (Current.Cost != Field.NavigationIntegrationCost[Current.CellIndex]) continue;
      for (const auto& Neighbor : Adjacency[Current.CellIndex])
      {
        if (Current.Cost > InfiniteCost - Neighbor.Value) continue;
        const int32 NewCost = Current.Cost + Neighbor.Value;
        const int32 ExistingNext = Field.NavigationNextNodeIndex[Neighbor.Key];
        const bool bStableTie = NewCost == Field.NavigationIntegrationCost[Neighbor.Key]
          && (ExistingNext == INDEX_NONE
            || Field.NavigationNodes[Current.CellIndex].StableNodeKey
              < Field.NavigationNodes[ExistingNext].StableNodeKey);
        if (NewCost < Field.NavigationIntegrationCost[Neighbor.Key] || bStableTie)
        {
          Field.NavigationIntegrationCost[Neighbor.Key] = NewCost;
          Field.NavigationNextNodeIndex[Neighbor.Key] = Current.CellIndex;
          HeapPush(Heap, {NewCost, Neighbor.Key});
        }
      }
    }

    Field.IntegrationCost.Init(InfiniteCost, CellCount);
    Field.Unreachable.Init(true, CellCount);
    Field.FlowDirection.Init(FVector::ZeroVector, CellCount);
    Field.NextCellIndex.Init(INDEX_NONE, CellCount);
    for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
    {
      if (Field.NavigationIntegrationCost[NodeIndex] == InfiniteCost) continue;
      const auto& Node = Field.NavigationNodes[NodeIndex];
      const int32 Cells[2] = {Node.PrimaryCellKey, Node.SecondaryCellKey};
      for (const int32 Cell : Cells)
      {
        if (!Field.IntegrationCost.IsValidIndex(Cell)) continue;
        Field.Unreachable[Cell] = false;
        if (Field.NavigationIntegrationCost[NodeIndex] < Field.IntegrationCost[Cell])
        {
          Field.IntegrationCost[Cell] = Field.NavigationIntegrationCost[NodeIndex];
          const int32 NextNode = Field.NavigationNextNodeIndex[NodeIndex];
          if (NextNode != INDEX_NONE)
          {
            const auto& Next = Field.NavigationNodes[NextNode];
            Field.NextCellIndex[Cell] = Next.PrimaryCellKey;
            Field.FlowDirection[Cell] = (
              NavigationNodeLocation(Field, Next) - Field.CellCenter(Cell)).GetSafeNormal2D();
          }
        }
      }
    }
    for (int32 Cell = 0; Cell < CellCount; ++Cell)
    {
      if (!Field.Blocked[Cell]) continue;
      bool bConnected = false;
      const TArray<int32>& CellNodes = Field.NavigationCellNodes[Cell];
      for (int32 A = 0; A < CellNodes.Num() && !bConnected; ++A)
      {
        for (int32 B = A + 1; B < CellNodes.Num(); ++B)
        {
          const uint32 MinIndex = static_cast<uint32>(
            FMath::Min(CellNodes[A], CellNodes[B]));
          const uint32 MaxIndex = static_cast<uint32>(
            FMath::Max(CellNodes[A], CellNodes[B]));
          if (EdgeIndexKeys.Contains(
              (static_cast<uint64>(MinIndex) << 32) | MaxIndex))
          {
            bConnected = true;
            break;
          }
        }
      }
      Field.CenterInvalidButConnectedCellCount += bConnected ? 1 : 0;
    }

    uint32 Hash = 2166136261u;
    Hash = HashInt(Hash, Config.ConnectivityContractVersion);
    Hash = HashInt(Hash, Quantize10(Config.AgentInflateCm));
    Hash = HashInt(Hash, Field.NavigationNodes.Num());
    for (const auto& Node : Field.NavigationNodes)
    {
      Hash = HashInt(Hash, static_cast<int32>(Node.StableNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Node.StableNodeKey >> 32));
      Hash = HashInt(Hash, static_cast<int32>(Node.Kind));
      Hash = HashInt(Hash, Node.PrimaryCellKey);
      Hash = HashInt(Hash, Node.SecondaryCellKey);
      Hash = HashInt(Hash, Node.IntervalOrdinal);
      Hash = HashInt(Hash, Node.QuantizedLocationCm.X);
      Hash = HashInt(Hash, Node.QuantizedLocationCm.Y);
    }
    Hash = HashInt(Hash, Field.NavigationSafeIntervals.Num());
    for (const auto& Interval : Field.NavigationSafeIntervals)
    {
      Hash = HashInt(Hash, static_cast<int32>(Interval.Kind));
      Hash = HashInt(Hash, Interval.PrimaryCellKey);
      Hash = HashInt(Hash, Interval.SecondaryCellKey);
      Hash = HashInt(Hash, Interval.IntervalOrdinal);
      Hash = HashInt(Hash, Interval.QuantizedMinCm);
      Hash = HashInt(Hash, Interval.QuantizedMaxCm);
    }
    Hash = HashInt(Hash, Field.NavigationEdges.Num());
    for (const auto& Edge : Field.NavigationEdges)
    {
      Hash = HashInt(Hash, static_cast<int32>(Edge.MinNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MinNodeKey >> 32));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MaxNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MaxNodeKey >> 32));
      Hash = HashInt(Hash, Edge.QuantizedCost);
    }
    for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
    {
      Hash = HashInt(Hash, Field.NavigationIntegrationCost[NodeIndex]);
      const int32 Next = Field.NavigationNextNodeIndex[NodeIndex];
      Hash = HashInt(Hash, Next == INDEX_NONE ? 0
        : static_cast<int32>(Field.NavigationNodes[Next].StableNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, Next == INDEX_NONE ? 0
        : static_cast<int32>(Field.NavigationNodes[Next].StableNodeKey >> 32));
    }
    Hash = HashInt(Hash, Field.GoalAttachmentNodeIndices.Num());
    for (const int32 NodeIndex : Field.GoalAttachmentNodeIndices)
    {
      Hash = HashInt(Hash,
        static_cast<int32>(Field.NavigationNodes[NodeIndex].StableNodeKey & 0xffffffffull));
      Hash = HashInt(Hash,
        static_cast<int32>(Field.NavigationNodes[NodeIndex].StableNodeKey >> 32));
    }
    Field.BuildHash = Hash;
    return Field.IsValid();
  }

  uint32 HashV2Topology(const FCrowdDemoSharedFlowField& Field)
  {
    uint32 Hash = 2166136261u;
    Hash = HashInt(Hash, Field.Config.ConnectivityContractVersion);
    Hash = HashInt(Hash, Quantize10(Field.Config.AgentInflateCm));
    Hash = HashInt(Hash, Quantize10(FVector(Field.Config.BoundsMin).X));
    Hash = HashInt(Hash, Quantize10(FVector(Field.Config.BoundsMin).Y));
    Hash = HashInt(Hash, Quantize10(FVector(Field.Config.BoundsMax).X));
    Hash = HashInt(Hash, Quantize10(FVector(Field.Config.BoundsMax).Y));
    Hash = HashInt(Hash, Quantize10(Field.Config.CellSizeCm));
    Hash = HashInt(Hash, Field.NavigationNodes.Num());
    for (const auto& Node : Field.NavigationNodes)
    {
      Hash = HashInt(Hash, static_cast<int32>(Node.StableNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Node.StableNodeKey >> 32));
      Hash = HashInt(Hash, Node.QuantizedLocationCm.X);
      Hash = HashInt(Hash, Node.QuantizedLocationCm.Y);
    }
    Hash = HashInt(Hash, Field.NavigationEdges.Num());
    for (const auto& Edge : Field.NavigationEdges)
    {
      Hash = HashInt(Hash, static_cast<int32>(Edge.MinNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MinNodeKey >> 32));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MaxNodeKey & 0xffffffffull));
      Hash = HashInt(Hash, static_cast<int32>(Edge.MaxNodeKey >> 32));
      Hash = HashInt(Hash, Edge.QuantizedCost);
    }
    return Hash;
  }
}

bool FCrowdDemoSharedFlowField::IsValid() const
{
  const bool bCellArraysValid = Width > 0
    && Height > 0
    && IntegrationCost.Num() == Width * Height
    && FlowDirection.Num() == Width * Height
    && NextCellIndex.Num() == Width * Height;
  if (!bCellArraysValid) return false;
  if (Config.ConnectivityContractVersion >= 2)
  {
    return GoalCellIndex != INDEX_NONE
      && !NavigationNodes.IsEmpty()
      && NavigationIntegrationCost.Num() == NavigationNodes.Num()
      && NavigationNextNodeIndex.Num() == NavigationNodes.Num()
      && !GoalAttachmentNodeIndices.IsEmpty();
  }
  return GoalCellIndex != INDEX_NONE;
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
  if (Config.ConnectivityContractVersion >= 2)
  {
    return BuildTopology(Config, OutField);
  }
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
    const FVector Center = OutField.CellCenter(CellIndex);
    const bool bBlocked = IsInsideInflatedObstacle(Config, Center)
      || !IsInsideContractBounds(Config, Center);
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

  for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
  {
    const int32 CellX = CellIndex % OutField.Width;
    const int32 CellY = CellIndex / OutField.Width;
    for (int32 NeighborOrder = 0; NeighborOrder < 8; ++NeighborOrder)
    {
      const int32 NextX = CellX + NeighborDx[NeighborOrder];
      const int32 NextY = CellY + NeighborDy[NeighborOrder];
      if (NextX < 0 || NextX >= OutField.Width || NextY < 0 || NextY >= OutField.Height)
        continue;
      if (CanTraverseCellEdge(
          OutField, CellIndex, NextY * OutField.Width + NextX))
        ++OutField.ValidDirectedEdgeCount;
    }
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
      const bool bDiagonal = NeighborDx[NeighborOrder] != 0 && NeighborDy[NeighborOrder] != 0;
      if (!CanTraverseCellEdge(OutField, Current.CellIndex, NextIndex)) continue;
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
      if (!CanTraverseCellEdge(OutField, CellIndex, NextIndex)) continue;
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
  if (Config.ConnectivityContractVersion > 0)
  {
    Hash = HashInt(Hash, Config.ConnectivityContractVersion);
    Hash = HashInt(Hash, Quantize10(Min.X));
    Hash = HashInt(Hash, Quantize10(Min.Y));
    Hash = HashInt(Hash, Quantize10(Max.X));
    Hash = HashInt(Hash, Quantize10(Max.Y));
    TArray<FCrowdDemoSharedFlowObstacleSpec> SortedObstacles = Config.ObstacleSpecs;
    SortedObstacles.Sort([](const auto& A, const auto& B)
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
    Hash = HashInt(Hash, SortedObstacles.Num());
    for (const auto& Obstacle : SortedObstacles)
    {
      Hash = HashInt(Hash, Obstacle.ObstacleId);
      Hash = HashInt(Hash, Quantize10(FVector(Obstacle.Center).X));
      Hash = HashInt(Hash, Quantize10(FVector(Obstacle.Center).Y));
      Hash = HashInt(Hash, Quantize10(FVector(Obstacle.Extent).X));
      Hash = HashInt(Hash, Quantize10(FVector(Obstacle.Extent).Y));
    }
    Hash = HashInt(Hash, OutField.ValidDirectedEdgeCount);
    for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
    {
      const int32 CellX = CellIndex % OutField.Width;
      const int32 CellY = CellIndex / OutField.Width;
      for (int32 NeighborOrder = 0; NeighborOrder < 8; ++NeighborOrder)
      {
        const int32 NextX = CellX + NeighborDx[NeighborOrder];
        const int32 NextY = CellY + NeighborDy[NeighborOrder];
        if (NextX < 0 || NextX >= OutField.Width || NextY < 0 || NextY >= OutField.Height)
          continue;
        const int32 NextIndex = NextY * OutField.Width + NextX;
        Hash = HashInt(Hash, CellIndex);
        Hash = HashInt(Hash, NextIndex);
        Hash = HashInt(Hash, CanTraverseCellEdge(OutField, CellIndex, NextIndex) ? 1 : 0);
      }
    }
  }
  OutField.BuildHash = Hash;
  return true;
}

bool FCrowdDemoSharedFlowFieldKernel::BuildTopology(
  const FCrowdDemoSharedFlowFieldConfig& Config,
  FCrowdDemoSharedFlowField& OutField)
{
  OutField.Reset();
  OutField.Config = Config;
  const FVector Min = FVector(Config.BoundsMin);
  const FVector Max = FVector(Config.BoundsMax);
  if (Config.ConnectivityContractVersion < 2 || Config.CellSizeCm <= 1.0f
    || Max.X <= Min.X || Max.Y <= Min.Y)
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
    const FVector Center = OutField.CellCenter(CellIndex);
    const bool bBlocked = IsInsideInflatedObstacle(Config, Center)
      || !IsInsideContractBounds(Config, Center);
    OutField.Blocked[CellIndex] = bBlocked;
    OutField.BlockedCellCount += bBlocked ? 1 : 0;
  }
  if (!BuildV2NavigationGraph(OutField)) return false;
  OutField.TopologyHash = HashV2Topology(OutField);
  OutField.IntegrationHash = OutField.BuildHash;
  return true;
}

bool FCrowdDemoSharedFlowFieldKernel::ResolveGoalAnchor(
  const FCrowdDemoSharedFlowField& Field,
  const FVector& TargetLocation,
  int32& OutAnchorCellKey,
  FVector& OutAnchorLocation)
{
  OutAnchorCellKey = INDEX_NONE;
  OutAnchorLocation = FVector::ZeroVector;
  if (Field.Config.ConnectivityContractVersion < 2
    || Field.Width <= 0 || Field.Height <= 0
    || Field.Blocked.Num() != Field.Width * Field.Height)
  {
    return false;
  }

  const int32 DirectCell = Field.LocationToCellIndex(TargetLocation);
  if (Field.Blocked.IsValidIndex(DirectCell) && !Field.Blocked[DirectCell])
  {
    OutAnchorCellKey = DirectCell;
    OutAnchorLocation = Field.CellCenter(DirectCell);
    return true;
  }

  float BestDistanceSq = MAX_flt;
  for (int32 Cell = 0; Cell < Field.Width * Field.Height; ++Cell)
  {
    if (Field.Blocked[Cell]) continue;
    const float DistanceSq = FVector::DistSquared2D(Field.CellCenter(Cell), TargetLocation);
    if (DistanceSq < BestDistanceSq
      || (FMath::IsNearlyEqual(DistanceSq, BestDistanceSq) && Cell < OutAnchorCellKey))
    {
      BestDistanceSq = DistanceSq;
      OutAnchorCellKey = Cell;
    }
  }
  if (OutAnchorCellKey == INDEX_NONE) return false;
  OutAnchorLocation = Field.CellCenter(OutAnchorCellKey);
  return true;
}

bool FCrowdDemoSharedFlowFieldKernel::BuildIntegrationForAnchor(
  const int32 AnchorCellKey,
  const FVector& AnchorLocation,
  FCrowdDemoSharedFlowField& Field)
{
  if (Field.Config.ConnectivityContractVersion < 2
    || Field.NavigationNodes.IsEmpty() || Field.NavigationEdges.IsEmpty()
    || !Field.Blocked.IsValidIndex(AnchorCellKey) || Field.Blocked[AnchorCellKey])
  {
    return false;
  }

  Field.Config.GoalLocation = AnchorLocation;
  Field.GoalCellIndex = AnchorCellKey;
  Field.GoalAttachmentNodeIndices.Reset();
  const TArray<FNavigationAttachmentCandidate> Attachments =
    FindNavigationAttachments(Field, AnchorLocation, false);
  if (Attachments.IsEmpty()) return false;
  for (const auto& Attachment : Attachments)
    Field.GoalAttachmentNodeIndices.Add(Attachment.NodeIndex);
  Field.GoalAttachmentCount = Field.GoalAttachmentNodeIndices.Num();

  TMap<uint64, int32> NodeIndexByKey;
  for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
    NodeIndexByKey.Add(Field.NavigationNodes[NodeIndex].StableNodeKey, NodeIndex);
  TArray<TArray<TPair<int32, int32>>> Adjacency;
  Adjacency.SetNum(Field.NavigationNodes.Num());
  for (const FCrowdDemoNavigationEdge& Edge : Field.NavigationEdges)
  {
    const int32* A = NodeIndexByKey.Find(Edge.MinNodeKey);
    const int32* B = NodeIndexByKey.Find(Edge.MaxNodeKey);
    if (!A || !B) return false;
    Adjacency[*A].Emplace(*B, Edge.QuantizedCost);
    Adjacency[*B].Emplace(*A, Edge.QuantizedCost);
  }
  for (auto& Neighbors : Adjacency)
    Neighbors.Sort([&](const auto& A, const auto& B)
    {
      const uint64 KeyA = Field.NavigationNodes[A.Key].StableNodeKey;
      const uint64 KeyB = Field.NavigationNodes[B.Key].StableNodeKey;
      return KeyA != KeyB ? KeyA < KeyB : A.Value < B.Value;
    });

  Field.NavigationIntegrationCost.Init(InfiniteCost, Field.NavigationNodes.Num());
  Field.NavigationNextNodeIndex.Init(INDEX_NONE, Field.NavigationNodes.Num());
  TArray<FQueueNode> Heap;
  for (const int32 NodeIndex : Field.GoalAttachmentNodeIndices)
  {
    const int32 GoalCost = FMath::RoundToInt(FVector::Dist2D(
      NavigationNodeLocation(Field, Field.NavigationNodes[NodeIndex]), AnchorLocation) * 10.0f);
    if (GoalCost < Field.NavigationIntegrationCost[NodeIndex])
    {
      Field.NavigationIntegrationCost[NodeIndex] = GoalCost;
      HeapPush(Heap, {GoalCost, NodeIndex});
    }
  }
  while (!Heap.IsEmpty())
  {
    const FQueueNode Current = HeapPop(Heap);
    if (Current.Cost != Field.NavigationIntegrationCost[Current.CellIndex]) continue;
    for (const auto& Neighbor : Adjacency[Current.CellIndex])
    {
      if (Current.Cost > InfiniteCost - Neighbor.Value) continue;
      const int32 NewCost = Current.Cost + Neighbor.Value;
      const int32 ExistingNext = Field.NavigationNextNodeIndex[Neighbor.Key];
      const bool bStableTie = NewCost == Field.NavigationIntegrationCost[Neighbor.Key]
        && (ExistingNext == INDEX_NONE
          || Field.NavigationNodes[Current.CellIndex].StableNodeKey
            < Field.NavigationNodes[ExistingNext].StableNodeKey);
      if (NewCost < Field.NavigationIntegrationCost[Neighbor.Key] || bStableTie)
      {
        Field.NavigationIntegrationCost[Neighbor.Key] = NewCost;
        Field.NavigationNextNodeIndex[Neighbor.Key] = Current.CellIndex;
        HeapPush(Heap, {NewCost, Neighbor.Key});
      }
    }
  }

  const int32 CellCount = Field.Width * Field.Height;
  Field.IntegrationCost.Init(InfiniteCost, CellCount);
  Field.Unreachable.Init(true, CellCount);
  Field.FlowDirection.Init(FVector::ZeroVector, CellCount);
  Field.NextCellIndex.Init(INDEX_NONE, CellCount);
  for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
  {
    if (Field.NavigationIntegrationCost[NodeIndex] == InfiniteCost) continue;
    const FCrowdDemoNavigationNode& Node = Field.NavigationNodes[NodeIndex];
    const int32 Cells[2] = {Node.PrimaryCellKey, Node.SecondaryCellKey};
    for (const int32 Cell : Cells)
    {
      if (!Field.IntegrationCost.IsValidIndex(Cell)) continue;
      Field.Unreachable[Cell] = false;
      if (Field.NavigationIntegrationCost[NodeIndex] < Field.IntegrationCost[Cell])
      {
        Field.IntegrationCost[Cell] = Field.NavigationIntegrationCost[NodeIndex];
        const int32 NextNode = Field.NavigationNextNodeIndex[NodeIndex];
        if (NextNode != INDEX_NONE)
        {
          Field.NextCellIndex[Cell] = Field.NavigationNodes[NextNode].PrimaryCellKey;
          Field.FlowDirection[Cell] = (
            NavigationNodeLocation(Field, Field.NavigationNodes[NextNode])
              - Field.CellCenter(Cell)).GetSafeNormal2D();
        }
      }
    }
  }

  uint32 Hash = 2166136261u;
  Hash = HashInt(Hash, AnchorCellKey);
  Hash = HashInt(Hash, Quantize10(AnchorLocation.X));
  Hash = HashInt(Hash, Quantize10(AnchorLocation.Y));
  for (int32 NodeIndex = 0; NodeIndex < Field.NavigationNodes.Num(); ++NodeIndex)
  {
    Hash = HashInt(Hash, Field.NavigationIntegrationCost[NodeIndex]);
    const int32 Next = Field.NavigationNextNodeIndex[NodeIndex];
    Hash = HashInt(Hash, Next == INDEX_NONE ? 0
      : static_cast<int32>(Field.NavigationNodes[Next].StableNodeKey & 0xffffffffull));
    Hash = HashInt(Hash, Next == INDEX_NONE ? 0
      : static_cast<int32>(Field.NavigationNodes[Next].StableNodeKey >> 32));
  }
  for (const int32 NodeIndex : Field.GoalAttachmentNodeIndices)
  {
    Hash = HashInt(Hash,
      static_cast<int32>(Field.NavigationNodes[NodeIndex].StableNodeKey & 0xffffffffull));
    Hash = HashInt(Hash,
      static_cast<int32>(Field.NavigationNodes[NodeIndex].StableNodeKey >> 32));
  }
  Field.IntegrationHash = Hash;
  Field.BuildHash = HashInt(Field.TopologyHash, Field.IntegrationHash);
  return Field.IsValid();
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
  if (Field.Config.ConnectivityContractVersion >= 2)
  {
    Result.bBlocked = Field.Blocked[CellIndex];
    Result.bUnreachable = Field.Unreachable[CellIndex];
    Result.Status = Result.bBlocked
      ? ECrowdDemoFlowLocationStatus::BlockedRasterCell
      : (Result.bUnreachable
        ? ECrowdDemoFlowLocationStatus::UnreachableFreeCell
        : ECrowdDemoFlowLocationStatus::Reachable);
    const TArray<FNavigationAttachmentCandidate> Attachments =
      FindNavigationAttachments(Field, Location, true);
    if (Attachments.IsEmpty())
    {
      Result.FlowDirection = FVector::ZeroVector;
      Result.bUnreachable = true;
      if (!Result.bBlocked)
        Result.Status = ECrowdDemoFlowLocationStatus::UnreachableFreeCell;
      return Result;
    }
    const int32 NodeIndex = Attachments[0].NodeIndex;
    const auto& Node = Field.NavigationNodes[NodeIndex];
    Result.NavigationNodeKey = Node.StableNodeKey;
    Result.IntegrationCost = Field.NavigationIntegrationCost[NodeIndex];
    Result.bSourceAttached = true;
    Result.bRecoveredFromRasterMismatch = Result.bBlocked || Result.bUnreachable
      || Node.Kind != ECrowdDemoNavigationNodeKind::CenterAnchor;
    Result.bBlocked = false;
    Result.bUnreachable = false;
    Result.Status = ECrowdDemoFlowLocationStatus::Reachable;
    FVector Target = NavigationNodeLocation(Field, Node);
    const int32 NextNode = Field.NavigationNextNodeIndex[NodeIndex];
    if (NextNode != INDEX_NONE)
    {
      const FVector NextTarget = NavigationNodeLocation(
        Field, Field.NavigationNodes[NextNode]);
      if (CanTraverseWorldSegment(Field.Config, Location, NextTarget))
      {
        Target = NextTarget;
        Result.NextNavigationNodeKey = Field.NavigationNodes[NextNode].StableNodeKey;
      }
    }
    else if (Field.GoalAttachmentNodeIndices.Contains(NodeIndex)
      && CanTraverseWorldSegment(
        Field.Config, Location, FVector(Field.Config.GoalLocation)))
    {
      Target = FVector(Field.Config.GoalLocation);
    }
    Result.GuidanceDistanceCm = FVector::Dist2D(Location, Target);
    Result.FlowDirection = (Target - Location).GetSafeNormal2D();
    return Result;
  }
  Result.IntegrationCost = Field.IntegrationCost[CellIndex];
  Result.bBlocked = Field.Blocked[CellIndex];
  Result.bUnreachable = Field.Unreachable[CellIndex];
  Result.Status = Result.bBlocked
    ? ECrowdDemoFlowLocationStatus::BlockedRasterCell
    : (Result.bUnreachable
      ? ECrowdDemoFlowLocationStatus::UnreachableFreeCell
      : ECrowdDemoFlowLocationStatus::Reachable);
  const int32 NextCellIndex = Field.NextCellIndex[CellIndex];
  if (Field.Config.ConnectivityContractVersion <= 0)
  {
    Result.FlowDirection = NextCellIndex != INDEX_NONE
      ? (Field.CellCenter(NextCellIndex) - Location).GetSafeNormal2D()
      : Field.FlowDirection[CellIndex];
    return Result;
  }
  const FVector DirectTarget = Field.CellCenter(
    NextCellIndex != INDEX_NONE ? NextCellIndex : CellIndex);
  const bool bDirectNextSafe = !Result.bBlocked && !Result.bUnreachable
    && CanTraverseWorldSegment(Field.Config, Location, DirectTarget);
  if (bDirectNextSafe)
  {
    Result.GuidanceDistanceCm = NextCellIndex != INDEX_NONE
      ? FVector::Dist2D(Location, Field.CellCenter(NextCellIndex)) : 0.0f;
    Result.FlowDirection = NextCellIndex != INDEX_NONE
      ? (Field.CellCenter(NextCellIndex) - Location).GetSafeNormal2D()
      : Field.FlowDirection[CellIndex];
    return Result;
  }
  const FCrowdDemoReachableFlowCellSearchResult Recovery =
    FindNearestReachableCell(Field, Location, FMath::Max(Field.Width, Field.Height));
  if (Recovery.bFound)
  {
    Result.StableCellKey = Recovery.StableCellKey;
    Result.Status = ECrowdDemoFlowLocationStatus::Reachable;
    Result.FlowDirection = (Recovery.CellCenter - Location).GetSafeNormal2D();
    Result.IntegrationCost = Recovery.IntegrationCost;
    Result.GuidanceDistanceCm = Recovery.WorldDistanceCm;
    Result.bBlocked = false;
    Result.bUnreachable = false;
    Result.bRecoveredFromRasterMismatch = true;
  }
  else
  {
    Result.FlowDirection = FVector::ZeroVector;
    Result.bUnreachable = true;
    if (!Result.bBlocked)
      Result.Status = ECrowdDemoFlowLocationStatus::UnreachableFreeCell;
  }
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
  for (int32 Ring = 0; Ring <= MaxRing && Candidates.IsEmpty(); ++Ring)
  {
    const int32 MinY = FMath::Max(0, BaseY - Ring);
    const int32 MaxY = FMath::Min(Field.Height - 1, BaseY + Ring);
    const int32 MinX = FMath::Max(0, BaseX - Ring);
    const int32 MaxX = FMath::Min(Field.Width - 1, BaseX + Ring);
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
      for (int32 X = MinX; X <= MaxX; ++X)
      {
        if (FMath::Max(FMath::Abs(X - BaseX), FMath::Abs(Y - BaseY)) != Ring)
          continue;
        const int32 CellIndex = Y * Field.Width + X;
        if (Field.Blocked[CellIndex] || Field.Unreachable[CellIndex]) continue;
        const FVector Center = Field.CellCenter(CellIndex);
        if (!CanTraverseWorldSegment(Field.Config, Location, Center)) continue;
        const float DistanceCm = FVector::Dist2D(Location, Center);
        Candidates.Add({CellIndex, Ring, FMath::RoundToInt(DistanceCm),
          Field.IntegrationCost[CellIndex], DistanceCm});
      }
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
  if (Field.Config.ConnectivityContractVersion > 0)
    Result.FlowDirection = (Result.CellCenter - Location).GetSafeNormal2D();
  else
  {
    const int32 Next = Field.NextCellIndex[Best.CellIndex];
    Result.FlowDirection = Next != INDEX_NONE
      ? (Field.CellCenter(Next) - Location).GetSafeNormal2D()
      : Field.FlowDirection[Best.CellIndex];
  }
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
