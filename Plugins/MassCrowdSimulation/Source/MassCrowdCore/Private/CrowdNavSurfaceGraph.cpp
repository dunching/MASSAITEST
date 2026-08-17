#include "CrowdNavSurfaceGraph.h"

namespace
{
  constexpr uint64 FnvOffset64 = 14695981039346656037ull;
  constexpr uint64 FnvPrime64 = 1099511628211ull;

  template <typename T>
  void FoldUnsigned(uint64& Hash, const T Value)
  {
    static_assert(std::is_unsigned_v<T>);
    for (uint32 ByteIndex = 0; ByteIndex < sizeof(T); ++ByteIndex)
    {
      Hash ^= static_cast<uint8>(Value >> (ByteIndex * 8));
      Hash *= FnvPrime64;
    }
  }

  int32 QuantizeCm(const double Value)
  {
    return FMath::RoundToInt(FMath::Clamp(Value, -100000000.0, 100000000.0));
  }

  uint64 BuildStableNodeId(const FCrowdNavSurfacePolygonInput& Polygon)
  {
    uint64 Hash = FnvOffset64;
    FoldUnsigned(Hash, Polygon.NavLayerHint);
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Polygon.Center.X)));
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Polygon.Center.Y)));
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Polygon.Center.Z)));
    TArray<FIntVector> Vertices;
    Vertices.Reserve(Polygon.Vertices.Num());
    for (const FVector& Vertex : Polygon.Vertices)
    {
      Vertices.Emplace(
        QuantizeCm(Vertex.X), QuantizeCm(Vertex.Y), QuantizeCm(Vertex.Z));
    }
    Vertices.Sort([](const FIntVector& A, const FIntVector& B)
    {
      if (A.X != B.X) return A.X < B.X;
      if (A.Y != B.Y) return A.Y < B.Y;
      return A.Z < B.Z;
    });
    for (const FIntVector& Vertex : Vertices)
    {
      FoldUnsigned(Hash, static_cast<uint32>(Vertex.X));
      FoldUnsigned(Hash, static_cast<uint32>(Vertex.Y));
      FoldUnsigned(Hash, static_cast<uint32>(Vertex.Z));
    }
    return Hash == 0 ? 1 : Hash;
  }

  bool IsFiniteVector(const FVector& Value)
  {
    return FMath::IsFinite(Value.X)
      && FMath::IsFinite(Value.Y)
      && FMath::IsFinite(Value.Z);
  }

  double DistanceSquaredToConvexPolygon(
    const FVector& Location,
    const FCrowdNavSurfaceNode& Node)
  {
    const FVector Normal = Node.SurfaceNormal.GetSafeNormal();
    const double PlaneDistance = FVector::DotProduct(Location - Node.Vertices[0], Normal);
    const FVector Projected = Location - Normal * PlaneDistance;
    bool bInside = true;
    double WindingSign = 0.0;
    for (int32 Index = 0; Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B = Node.Vertices[(Index + 1) % Node.Vertices.Num()];
      const double Side = FVector::DotProduct(FVector::CrossProduct(B - A, Projected - A), Normal);
      if (FMath::Abs(Side) <= UE_DOUBLE_KINDA_SMALL_NUMBER) continue;
      if (WindingSign == 0.0) WindingSign = Side;
      else if ((Side > 0.0) != (WindingSign > 0.0))
      {
        bInside = false;
        break;
      }
    }
    if (bInside) return FMath::Square(PlaneDistance);

    double BestDistanceSquared = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Node.Vertices.Num(); ++Index)
    {
      const FVector& A = Node.Vertices[Index];
      const FVector& B = Node.Vertices[(Index + 1) % Node.Vertices.Num()];
      BestDistanceSquared = FMath::Min(BestDistanceSquared,
        FVector::DistSquared(Location, FMath::ClosestPointOnSegment(Location, A, B)));
    }
    return BestDistanceSquared;
  }

  bool AttachInternal(
    const FCrowdNavSurfaceGraph& Graph,
    const FVector& Location,
    const TOptional<uint32> NavLayer,
    const float MaxDistanceCm,
    uint64& OutStableNodeId,
    uint32& OutNavLayer)
  {
    OutStableNodeId = 0;
    OutNavLayer = 0;
    if (!Graph.IsValid() || !IsFiniteVector(Location) || MaxDistanceCm <= 0.0f)
      return false;
    double BestDistanceSquared = FMath::Square(static_cast<double>(MaxDistanceCm));
    for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
    {
      if (NavLayer.IsSet() && Node.NavLayer != NavLayer.GetValue()) continue;
      const double DistanceSquared = DistanceSquaredToConvexPolygon(Location, Node);
      if (DistanceSquared < BestDistanceSquared
        || (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
          && (OutStableNodeId == 0 || Node.StableNodeId < OutStableNodeId)))
      {
        BestDistanceSquared = DistanceSquared;
        OutStableNodeId = Node.StableNodeId;
        OutNavLayer = Node.NavLayer;
      }
    }
    return OutStableNodeId != 0;
  }

  struct FQueueItem
  {
    uint32 Cost = MAX_uint32;
    int32 NodeIndex = INDEX_NONE;
  };

  void HeapPush(TArray<FQueueItem>& Heap, const FQueueItem Item)
  {
    Heap.Add(Item);
    int32 Index = Heap.Num() - 1;
    while (Index > 0)
    {
      const int32 Parent = (Index - 1) / 2;
      if (Heap[Parent].Cost < Heap[Index].Cost
        || (Heap[Parent].Cost == Heap[Index].Cost
          && Heap[Parent].NodeIndex <= Heap[Index].NodeIndex)) break;
      Swap(Heap[Parent], Heap[Index]);
      Index = Parent;
    }
  }

  FQueueItem HeapPop(TArray<FQueueItem>& Heap)
  {
    const FQueueItem Result = Heap[0];
    Heap[0] = Heap.Last();
    Heap.Pop(EAllowShrinking::No);
    int32 Index = 0;
    while (true)
    {
      const int32 Left = Index * 2 + 1;
      if (Left >= Heap.Num()) break;
      const int32 Right = Left + 1;
      int32 Best = Left;
      if (Right < Heap.Num()
        && (Heap[Right].Cost < Heap[Left].Cost
          || (Heap[Right].Cost == Heap[Left].Cost
            && Heap[Right].NodeIndex < Heap[Left].NodeIndex))) Best = Right;
      if (Heap[Index].Cost < Heap[Best].Cost
        || (Heap[Index].Cost == Heap[Best].Cost
          && Heap[Index].NodeIndex <= Heap[Best].NodeIndex)) break;
      Swap(Heap[Index], Heap[Best]);
      Index = Best;
    }
    return Result;
  }
}

void FCrowdNavSurfaceGraph::Reset()
{
  Nodes.Reset();
  TopologyHash = 0;
  RejectedPortalCount = 0;
  RejectedMissingNeighborCount = 0;
  RejectedNarrowPortalCount = 0;
  MinRejectedPortalWidthCm = MAX_uint32;
  MaxRejectedPortalWidthCm = 0;
  RejectedStepPortalCount = 0;
  RejectedSlopePortalCount = 0;
}

bool FCrowdNavSurfaceGraph::IsValid() const
{
  if (Nodes.IsEmpty() || TopologyHash == 0) return false;
  uint64 Previous = 0;
  for (const FCrowdNavSurfaceNode& Node : Nodes)
  {
    if (Node.StableNodeId == 0 || Node.StableNodeId <= Previous
      || !IsFiniteVector(Node.Center) || !IsFiniteVector(Node.SurfaceNormal)
      || Node.Vertices.Num() < 3) return false;
    for (const FVector& Vertex : Node.Vertices)
      if (!IsFiniteVector(Vertex)) return false;
    Previous = Node.StableNodeId;
    uint64 PreviousNeighbor = 0;
    for (const FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      if (Edge.ToStableNodeId == 0 || Edge.ToStableNodeId <= PreviousNeighbor
        || Edge.CostQ == 0 || Edge.WidthCm == 0
        || FindNodeIndex(Edge.ToStableNodeId) == INDEX_NONE) return false;
      PreviousNeighbor = Edge.ToStableNodeId;
    }
  }
  return true;
}

int32 FCrowdNavSurfaceGraph::FindNodeIndex(const uint64 StableNodeId) const
{
  int32 Low = 0;
  int32 High = Nodes.Num() - 1;
  while (Low <= High)
  {
    const int32 Middle = Low + (High - Low) / 2;
    if (Nodes[Middle].StableNodeId == StableNodeId) return Middle;
    if (Nodes[Middle].StableNodeId < StableNodeId) Low = Middle + 1;
    else High = Middle - 1;
  }
  return INDEX_NONE;
}

bool FCrowdNavSurfaceFlow::IsValid() const
{
  return TopologyHash != 0 && GoalStableNodeId != 0
    && Revision != 0 && IntegrationHash != 0 && !Nodes.IsEmpty();
}

bool FCrowdNavSurfaceGraphKernel::Build(
  const TConstArrayView<FCrowdNavSurfacePolygonInput> Polygons,
  const FCrowdNavSurfaceGraphBuildConfig& Config,
  FCrowdNavSurfaceGraph& OutGraph)
{
  OutGraph.Reset();
  if (Polygons.IsEmpty() || Polygons.Num() > Config.MaxNodes
    || Config.MaxNodes <= 0 || Config.MaxVerticesPerPolygon < 3
    || Config.MaxPortalsPerPolygon <= 0 || Config.MinPortalWidthCm == 0
    || Config.MaxPortalStepHeightCm == 0
    || Config.MaxSlopeMilliDegrees > 90000 || Config.MaxEdgeCostQ == 0) return false;

  struct FWorkingNode
  {
    uint64 SourceKey = 0;
    const FCrowdNavSurfacePolygonInput* Polygon = nullptr;
    FCrowdNavSurfaceNode Node;
  };
  TArray<FWorkingNode> Working;
  Working.Reserve(Polygons.Num());
  TSet<uint64> SourceKeys;
  TSet<uint64> StableIds;
  for (const FCrowdNavSurfacePolygonInput& Polygon : Polygons)
  {
    if (Polygon.SourcePolygonKey == 0
      || SourceKeys.Contains(Polygon.SourcePolygonKey)
      || Polygon.Vertices.Num() < 3
      || Polygon.Vertices.Num() > Config.MaxVerticesPerPolygon
      || Polygon.Portals.Num() > Config.MaxPortalsPerPolygon
      || !IsFiniteVector(Polygon.Center)
      || !IsFiniteVector(Polygon.SurfaceNormal)
      || Polygon.SurfaceNormal.IsNearlyZero()
      || Polygon.AreaCostQ == 0) return false;
    for (const FVector& Vertex : Polygon.Vertices)
      if (!IsFiniteVector(Vertex)) return false;
    SourceKeys.Add(Polygon.SourcePolygonKey);
    const uint64 StableId = BuildStableNodeId(Polygon);
    if (StableIds.Contains(StableId)) return false;
    StableIds.Add(StableId);
    FWorkingNode& Item = Working.AddDefaulted_GetRef();
    Item.SourceKey = Polygon.SourcePolygonKey;
    Item.Polygon = &Polygon;
    Item.Node.StableNodeId = StableId;
    Item.Node.NavLayer = Polygon.NavLayerHint;
    Item.Node.Center = Polygon.Center;
    Item.Node.SurfaceNormal = Polygon.SurfaceNormal.GetSafeNormal();
    Item.Node.Vertices = Polygon.Vertices;
  }
  Working.Sort([](const FWorkingNode& A, const FWorkingNode& B)
  {
    return A.Node.StableNodeId < B.Node.StableNodeId;
  });
  TMap<uint64, int32> IndexBySource;
  for (int32 Index = 0; Index < Working.Num(); ++Index)
    IndexBySource.Add(Working[Index].SourceKey, Index);

  for (FWorkingNode& Item : Working)
  {
    TMap<uint64, FCrowdNavSurfaceEdge> BestEdgeByStableId;
    for (const FCrowdNavSurfacePortalInput& Portal : Item.Polygon->Portals)
    {
      const int32* NeighborIndex = IndexBySource.Find(Portal.ToSourcePolygonKey);
      if (!NeighborIndex || *NeighborIndex == IndexBySource.FindChecked(Item.SourceKey)
        || !IsFiniteVector(Portal.Left) || !IsFiniteVector(Portal.Right))
      {
        ++OutGraph.RejectedPortalCount;
        ++OutGraph.RejectedMissingNeighborCount;
        continue;
      }
      const FWorkingNode& Neighbor = Working[*NeighborIndex];
      const uint32 WidthCm = static_cast<uint32>(FMath::Max(
        0, FMath::RoundToInt(FVector::Distance(Portal.Left, Portal.Right))));
      const FVector Delta = Neighbor.Node.Center - Item.Node.Center;
      const FVector PortalMidpoint = (Portal.Left + Portal.Right) * 0.5;
      const uint32 PortalStepHeightCm = static_cast<uint32>(FMath::Max(0,
        FMath::RoundToInt(FMath::Max(
          FMath::Abs(FVector::DotProduct(
            PortalMidpoint - Item.Node.Center, Item.Node.SurfaceNormal)),
          FMath::Abs(FVector::DotProduct(
            PortalMidpoint - Neighbor.Node.Center, Neighbor.Node.SurfaceNormal))))));
      const double FromSlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(
        FMath::Clamp(FMath::Abs(Item.Node.SurfaceNormal.Z), 0.0, 1.0)));
      const double ToSlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(
        FMath::Clamp(FMath::Abs(Neighbor.Node.SurfaceNormal.Z), 0.0, 1.0)));
      const double CenterSlopeDegrees = FMath::RadiansToDegrees(FMath::Atan2(
        FMath::Abs(Delta.Z),
        FMath::Max(FVector2D(Delta.X, Delta.Y).Size(), 1.0)));
      const double SurfaceSlopeDegrees = FMath::Max(
        FromSlopeDegrees, ToSlopeDegrees);
      const uint32 SlopeMilliDegrees = static_cast<uint32>(FMath::RoundToInt(
        (SurfaceSlopeDegrees > 0.1
          ? SurfaceSlopeDegrees : CenterSlopeDegrees) * 1000.0));
      if (WidthCm < Config.MinPortalWidthCm)
      {
        ++OutGraph.RejectedPortalCount;
        ++OutGraph.RejectedNarrowPortalCount;
        OutGraph.MinRejectedPortalWidthCm = FMath::Min(
          OutGraph.MinRejectedPortalWidthCm, WidthCm);
        OutGraph.MaxRejectedPortalWidthCm = FMath::Max(
          OutGraph.MaxRejectedPortalWidthCm, WidthCm);
        continue;
      }
      if (PortalStepHeightCm > Config.MaxPortalStepHeightCm)
      {
        ++OutGraph.RejectedPortalCount;
        ++OutGraph.RejectedStepPortalCount;
        continue;
      }
      if (SlopeMilliDegrees > Config.MaxSlopeMilliDegrees)
      {
        ++OutGraph.RejectedPortalCount;
        ++OutGraph.RejectedSlopePortalCount;
        continue;
      }
      const uint64 BaseDistance = static_cast<uint64>(FMath::Max(
        1, FMath::RoundToInt(Delta.Size())));
      const uint64 Cost = BaseDistance
        + static_cast<uint64>(SlopeMilliDegrees / 1000)
          * Config.SlopeCostPerDegreeQ
        + Item.Polygon->AreaCostQ;
      if (Cost == 0 || Cost > Config.MaxEdgeCostQ)
      {
        ++OutGraph.RejectedPortalCount;
        continue;
      }
      FCrowdNavSurfaceEdge Edge{
        Neighbor.Node.StableNodeId,
        WidthCm,
        SlopeMilliDegrees,
        static_cast<uint32>(Cost)};
      FCrowdNavSurfaceEdge* Existing = BestEdgeByStableId.Find(Edge.ToStableNodeId);
      if (!Existing || Edge.CostQ < Existing->CostQ) BestEdgeByStableId.Add(
        Edge.ToStableNodeId, Edge);
    }
    BestEdgeByStableId.GenerateValueArray(Item.Node.Edges);
    Item.Node.Edges.Sort([](const FCrowdNavSurfaceEdge& A, const FCrowdNavSurfaceEdge& B)
    {
      return A.ToStableNodeId < B.ToStableNodeId;
    });
  }

  uint64 Hash = FnvOffset64;
  for (const FWorkingNode& Item : Working)
  {
    OutGraph.Nodes.Add(Item.Node);
    FoldUnsigned(Hash, Item.Node.StableNodeId);
    FoldUnsigned(Hash, Item.Node.NavLayer);
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Item.Node.Center.X)));
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Item.Node.Center.Y)));
    FoldUnsigned(Hash, static_cast<uint32>(QuantizeCm(Item.Node.Center.Z)));
    for (const FCrowdNavSurfaceEdge& Edge : Item.Node.Edges)
    {
      FoldUnsigned(Hash, Edge.ToStableNodeId);
      FoldUnsigned(Hash, Edge.WidthCm);
      FoldUnsigned(Hash, Edge.SlopeMilliDegrees);
      FoldUnsigned(Hash, Edge.CostQ);
    }
  }
  OutGraph.TopologyHash = Hash == 0 ? 1 : Hash;
  return OutGraph.IsValid();
}

bool FCrowdNavSurfaceGraphKernel::BuildFlow(
  const FCrowdNavSurfaceGraph& Graph,
  const uint64 GoalStableNodeId,
  const uint32 Revision,
  FCrowdNavSurfaceFlow& OutFlow)
{
  OutFlow = {};
  if (!Graph.IsValid() || GoalStableNodeId == 0 || Revision == 0) return false;
  const int32 GoalIndex = Graph.FindNodeIndex(GoalStableNodeId);
  if (GoalIndex == INDEX_NONE) return false;

  OutFlow.TopologyHash = Graph.TopologyHash;
  OutFlow.GoalStableNodeId = GoalStableNodeId;
  OutFlow.Revision = Revision;
  OutFlow.Nodes.SetNum(Graph.Nodes.Num());
  TArray<uint32> Costs;
  Costs.Init(MAX_uint32, Graph.Nodes.Num());
  TArray<int32> Next;
  Next.Init(INDEX_NONE, Graph.Nodes.Num());
  struct FIncomingEdge
  {
    int32 FromIndex = INDEX_NONE;
    uint32 CostQ = 0;
  };
  TArray<TArray<FIncomingEdge>> IncomingEdges;
  IncomingEdges.SetNum(Graph.Nodes.Num());
  for (int32 FromIndex = 0; FromIndex < Graph.Nodes.Num(); ++FromIndex)
  {
    for (const FCrowdNavSurfaceEdge& Edge : Graph.Nodes[FromIndex].Edges)
    {
      const int32 ToIndex = Graph.FindNodeIndex(Edge.ToStableNodeId);
      if (ToIndex == INDEX_NONE) return false;
      IncomingEdges[ToIndex].Add({FromIndex, Edge.CostQ});
    }
  }
  Costs[GoalIndex] = 0;
  TArray<FQueueItem> Heap;
  HeapPush(Heap, {0, GoalIndex});
  while (!Heap.IsEmpty())
  {
    const FQueueItem Current = HeapPop(Heap);
    if (Current.Cost != Costs[Current.NodeIndex]) continue;
    const uint64 CurrentId = Graph.Nodes[Current.NodeIndex].StableNodeId;
    for (const FIncomingEdge& Incoming : IncomingEdges[Current.NodeIndex])
    {
      const int32 CandidateIndex = Incoming.FromIndex;
      const uint64 NewCost64 = static_cast<uint64>(Current.Cost) + Incoming.CostQ;
      const uint32 NewCost = NewCost64 >= MAX_uint32
        ? MAX_uint32 : static_cast<uint32>(NewCost64);
      if (NewCost < Costs[CandidateIndex]
        || (NewCost == Costs[CandidateIndex]
          && (Next[CandidateIndex] == INDEX_NONE
            || CurrentId < Graph.Nodes[Next[CandidateIndex]].StableNodeId)))
      {
        Costs[CandidateIndex] = NewCost;
        Next[CandidateIndex] = Current.NodeIndex;
        HeapPush(Heap, {NewCost, CandidateIndex});
      }
    }
  }

  uint64 Hash = FnvOffset64;
  for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
  {
    FCrowdNavSurfaceFlowNode& Node = OutFlow.Nodes[Index];
    Node.StableNodeId = Graph.Nodes[Index].StableNodeId;
    Node.IntegrationCostQ = Costs[Index];
    if (Next[Index] != INDEX_NONE)
    {
      Node.NextStableNodeId = Graph.Nodes[Next[Index]].StableNodeId;
      const FVector RawDirection =
        Graph.Nodes[Next[Index]].Center - Graph.Nodes[Index].Center;
      Node.Direction = FVector::VectorPlaneProject(
        RawDirection, Graph.Nodes[Index].SurfaceNormal).GetSafeNormal();
    }
    FoldUnsigned(Hash, Node.StableNodeId);
    FoldUnsigned(Hash, Node.IntegrationCostQ);
    FoldUnsigned(Hash, Node.NextStableNodeId);
    FoldUnsigned(Hash, static_cast<uint32>(FMath::RoundToInt(Node.Direction.X * 10000.0)));
    FoldUnsigned(Hash, static_cast<uint32>(FMath::RoundToInt(Node.Direction.Y * 10000.0)));
    FoldUnsigned(Hash, static_cast<uint32>(FMath::RoundToInt(Node.Direction.Z * 10000.0)));
  }
  OutFlow.IntegrationHash = Hash == 0 ? 1 : Hash;
  return OutFlow.IsValid();
}

bool FCrowdNavSurfaceGraphKernel::Attach(
  const FCrowdNavSurfaceGraph& Graph,
  const FVector& Location,
  const uint32 NavLayer,
  const float MaxDistanceCm,
  uint64& OutStableNodeId)
{
  uint32 AttachedLayer = 0;
  return AttachInternal(Graph, Location, NavLayer, MaxDistanceCm,
    OutStableNodeId, AttachedLayer);
}

bool FCrowdNavSurfaceGraphKernel::AttachClosest(
  const FCrowdNavSurfaceGraph& Graph,
  const FVector& Location,
  const float MaxDistanceCm,
  uint64& OutStableNodeId,
  uint32& OutNavLayer)
{
  return AttachInternal(Graph, Location, TOptional<uint32>(), MaxDistanceCm,
    OutStableNodeId, OutNavLayer);
}
