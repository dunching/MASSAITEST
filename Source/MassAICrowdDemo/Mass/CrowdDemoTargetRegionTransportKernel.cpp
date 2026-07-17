#include "Mass/CrowdDemoTargetRegionTransportKernel.h"

#include "Algo/Sort.h"
#include "Mass/CrowdDemoSharedFlowFieldKernel.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;
  constexpr int32 AngularUnits = 64;

  uint32 Fold(uint32 Hash, const int64 Value)
  {
    Hash ^= static_cast<uint32>(Value);
    Hash *= FnvPrime;
    Hash ^= static_cast<uint32>(Value >> 32);
    return Hash * FnvPrime;
  }

  int32 Q(const float Value, const float Quantum = 1.0f)
  {
    return FMath::RoundToInt(Value / FMath::Max(Quantum, UE_SMALL_NUMBER));
  }

  FVector2f Quantize(const FVector2f& Value, const float Quantum)
  {
    return FVector2f(Q(Value.X, Quantum) * Quantum, Q(Value.Y, Quantum) * Quantum);
  }

  bool PointInsideExpanded(const FVector2f& Point,
    const FCrowdDemoSharedFlowObstacleSpec& Obstacle, const float Expansion)
  {
    return Point.X >= Obstacle.Center.X - Obstacle.Extent.X - Expansion
      && Point.X <= Obstacle.Center.X + Obstacle.Extent.X + Expansion
      && Point.Y >= Obstacle.Center.Y - Obstacle.Extent.Y - Expansion
      && Point.Y <= Obstacle.Center.Y + Obstacle.Extent.Y + Expansion;
  }

  bool SegmentIntersectsRect(const FVector2f& Start, const FVector2f& End,
    const float MinX, const float MinY, const float MaxX, const float MaxY)
  {
    float T0 = 0.0f;
    float T1 = 1.0f;
    const FVector2f Delta = End - Start;
    auto Clip = [&](const float P, const float D, const float Min, const float Max)
    {
      if (FMath::Abs(D) <= UE_SMALL_NUMBER) return P >= Min && P <= Max;
      float A = (Min - P) / D;
      float B = (Max - P) / D;
      if (A > B) Swap(A, B);
      T0 = FMath::Max(T0, A);
      T1 = FMath::Min(T1, B);
      return T0 <= T1;
    };
    return Clip(Start.X, Delta.X, MinX, MaxX)
      && Clip(Start.Y, Delta.Y, MinY, MaxY);
  }

  float PointSegmentDistance(const FVector2f& Point, const FVector2f& Start,
    const FVector2f& End)
  {
    const FVector2f Delta = End - Start;
    const float Alpha = Delta.SizeSquared() > UE_SMALL_NUMBER
      ? FMath::Clamp(FVector2f::DotProduct(Point - Start, Delta) / Delta.SizeSquared(), 0.0f, 1.0f)
      : 0.0f;
    return (Point - (Start + Delta * Alpha)).Size();
  }

  float SegmentSegmentDistance(const FVector2f& A0, const FVector2f& A1,
    const FVector2f& B0, const FVector2f& B1)
  {
    const FVector2f U = A1 - A0;
    const FVector2f V = B1 - B0;
    const FVector2f W = A0 - B0;
    const float A = FVector2f::DotProduct(U, U);
    const float B = FVector2f::DotProduct(U, V);
    const float C = FVector2f::DotProduct(V, V);
    const float D = FVector2f::DotProduct(U, W);
    const float E = FVector2f::DotProduct(V, W);
    const float Denominator = A * C - B * B;
    float S = Denominator > UE_SMALL_NUMBER ? FMath::Clamp((B * E - C * D) / Denominator, 0.0f, 1.0f) : 0.0f;
    float T = C > UE_SMALL_NUMBER ? FMath::Clamp((B * S + E) / C, 0.0f, 1.0f) : 0.0f;
    if (A > UE_SMALL_NUMBER) S = FMath::Clamp((B * T - D) / A, 0.0f, 1.0f);
    if (C > UE_SMALL_NUMBER) T = FMath::Clamp((B * S + E) / C, 0.0f, 1.0f);
    return (A0 + U * S - (B0 + V * T)).Size();
  }

  float SegmentRectDistance(const FVector2f& Start, const FVector2f& End,
    const float MinX, const float MinY, const float MaxX, const float MaxY)
  {
    if (SegmentIntersectsRect(Start, End, MinX, MinY, MaxX, MaxY)) return 0.0f;
    const FVector2f C0(MinX, MinY), C1(MaxX, MinY), C2(MaxX, MaxY), C3(MinX, MaxY);
    return FMath::Min(
      FMath::Min(SegmentSegmentDistance(Start, End, C0, C1),
        SegmentSegmentDistance(Start, End, C1, C2)),
      FMath::Min(SegmentSegmentDistance(Start, End, C2, C3),
        SegmentSegmentDistance(Start, End, C3, C0)));
  }

  float SegmentMinimumEnvironmentClearance(const FVector2f& Start, const FVector2f& End,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig)
  {
    float Clearance = FMath::Min(
      FMath::Min(FMath::Min(Start.X, End.X) - FlowConfig.BoundsMin.X,
        FlowConfig.BoundsMax.X - FMath::Max(Start.X, End.X)),
      FMath::Min(FMath::Min(Start.Y, End.Y) - FlowConfig.BoundsMin.Y,
        FlowConfig.BoundsMax.Y - FMath::Max(Start.Y, End.Y)));
    TArray<FCrowdDemoSharedFlowObstacleSpec> Obstacles = FlowConfig.ObstacleSpecs;
    Obstacles.Sort([](const auto& A, const auto& B) { return A.ObstacleId < B.ObstacleId; });
    for (const auto& Obstacle : Obstacles)
      Clearance = FMath::Min(Clearance, SegmentRectDistance(Start, End,
        Obstacle.Center.X - Obstacle.Extent.X, Obstacle.Center.Y - Obstacle.Extent.Y,
        Obstacle.Center.X + Obstacle.Extent.X, Obstacle.Center.Y + Obstacle.Extent.Y));
    const float TargetCenterDistance = PointSegmentDistance(
      Settings.TargetLocation, Start, End) - Settings.TargetPhysicalRadiusCm;
    return FMath::Min(Clearance, TargetCenterDistance);
  }

  bool SegmentSafe(const FVector2f& Start, const FVector2f& End,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig, const float Clearance,
    const FVector2f& Target, const float TargetClearance)
  {
    if (Start.X < FlowConfig.BoundsMin.X + Clearance
      || Start.X > FlowConfig.BoundsMax.X - Clearance
      || Start.Y < FlowConfig.BoundsMin.Y + Clearance
      || Start.Y > FlowConfig.BoundsMax.Y - Clearance
      || End.X < FlowConfig.BoundsMin.X + Clearance
      || End.X > FlowConfig.BoundsMax.X - Clearance
      || End.Y < FlowConfig.BoundsMin.Y + Clearance
      || End.Y > FlowConfig.BoundsMax.Y - Clearance)
      return false;
    for (const auto& Obstacle : FlowConfig.ObstacleSpecs)
      if (SegmentIntersectsRect(Start, End,
        Obstacle.Center.X - Obstacle.Extent.X - Clearance,
        Obstacle.Center.Y - Obstacle.Extent.Y - Clearance,
        Obstacle.Center.X + Obstacle.Extent.X + Clearance,
        Obstacle.Center.Y + Obstacle.Extent.Y + Clearance))
        return false;
    const FVector2f Delta = End - Start;
    const float Alpha = Delta.SizeSquared() > UE_SMALL_NUMBER
      ? FMath::Clamp(FVector2f::DotProduct(Target - Start, Delta) / Delta.SizeSquared(), 0.0f, 1.0f)
      : 0.0f;
    return (Start + Delta * Alpha - Target).Size() + 0.01f >= TargetClearance;
  }

  int32 SectorForOffset(const FVector2f& Offset, const int32 Count)
  {
    float Angle = FMath::Atan2(Offset.Y, Offset.X);
    if (Angle < 0.0f) Angle += 2.0f * PI;
    return FMath::Clamp(FMath::FloorToInt(Angle / (2.0f * PI)
      * static_cast<float>(Count)), 0, Count - 1);
  }

  int32 RegionForOffset(const FVector2f& Offset, const int32 Count)
  {
    return SectorForOffset(Offset, Count);
  }

  const FCrowdDemoTargetPolarCell* FindCell(
    const FCrowdDemoTargetPolarTopology& Topology, const int32 CellKey)
  {
    return Topology.Cells.IsValidIndex(CellKey) ? &Topology.Cells[CellKey] : nullptr;
  }

  int32 DirectCellForLocation(const FVector2f& Location,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoTargetPolarTopology& Topology)
  {
    const FVector2f Offset = Location - Settings.TargetLocation;
    const int32 Band = FMath::FloorToInt(Offset.Size() / Settings.RadialBandWidthCm);
    if (!Topology.BandSectorCounts.IsValidIndex(Band)) return INDEX_NONE;
    const int32 Sector = SectorForOffset(Offset, Topology.BandSectorCounts[Band]);
    return Topology.BandCellOffsets[Band] + Sector;
  }

  int32 AttachSource(const FVector2f& Location,
    const FCrowdDemoTargetRegionTransportSettings& Settings,
    const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
    const FCrowdDemoSharedFlowField* SharedFlowField,
    const FCrowdDemoTargetPolarTopology& Topology)
  {
    const int32 Direct = DirectCellForLocation(Location, Settings, Topology);
    if (const auto* Cell = FindCell(Topology, Direct); Cell && Cell->bFeasible)
      return Direct;
    int32 Best = INDEX_NONE;
    int64 BestDistance = MAX_int64;
    const float HardClearance = Settings.PhysicalRadiusCm + Settings.HardSafetyGapCm;
    const float TargetClearance = Settings.TargetPhysicalRadiusCm + Settings.PhysicalRadiusCm
      + FMath::Max(Settings.HardSafetyGapCm, Settings.TargetHardSafetyGapCm);
    if (SharedFlowField && SharedFlowField->IsValid())
    {
      const FCrowdDemoSharedFlowSample Sample = FCrowdDemoSharedFlowFieldKernel::Sample(
        *SharedFlowField, FVector(Location.X, Location.Y, 0.0f));
      int32 NodeIndex = INDEX_NONE;
      for (int32 Index = 0; Index < SharedFlowField->NavigationNodes.Num(); ++Index)
        if (SharedFlowField->NavigationNodes[Index].StableNodeKey == Sample.NavigationNodeKey)
        { NodeIndex = Index; break; }
      TBitArray<> Visited(false, SharedFlowField->NavigationNodes.Num());
      for (int32 Hop = 0; NodeIndex != INDEX_NONE
        && Hop < SharedFlowField->NavigationNodes.Num(); ++Hop)
      {
        if (!Visited.IsValidIndex(NodeIndex) || Visited[NodeIndex]) break;
        Visited[NodeIndex] = true;
        const auto& Node = SharedFlowField->NavigationNodes[NodeIndex];
        const FVector2f NodeLocation(
          static_cast<float>(Node.QuantizedLocationCm.X),
          static_cast<float>(Node.QuantizedLocationCm.Y));
        const int32 CandidateKey = DirectCellForLocation(NodeLocation, Settings, Topology);
        if (const auto* Candidate = FindCell(Topology, CandidateKey);
          Candidate && Candidate->bFeasible
          && SegmentSafe(NodeLocation, Candidate->WorldAnchorCm, FlowConfig,
            HardClearance, Settings.TargetLocation, TargetClearance))
          return CandidateKey;
        NodeIndex = SharedFlowField->NavigationNextNodeIndex.IsValidIndex(NodeIndex)
          ? SharedFlowField->NavigationNextNodeIndex[NodeIndex] : INDEX_NONE;
      }
    }
    for (const auto& Cell : Topology.Cells)
    {
      if (!Cell.bFeasible) continue;
      if (!SegmentSafe(Location, Cell.WorldAnchorCm, FlowConfig, HardClearance,
        Settings.TargetLocation, TargetClearance)) continue;
      const int64 Distance = Q((Cell.WorldAnchorCm - Location).Size());
      if (Distance < BestDistance || (Distance == BestDistance && Cell.StableCellKey < Best))
      {
        BestDistance = Distance;
        Best = Cell.StableCellKey;
      }
    }
    return Best;
  }

  struct FCost
  {
    int64 Physical = MAX_int64 / 4;
    int64 Change = MAX_int64 / 4;
  };

  bool Less(const FCost& A, const FCost& B)
  {
    return A.Physical < B.Physical || (A.Physical == B.Physical && A.Change < B.Change);
  }

  FCost Add(const FCost& A, const int64 P, const int64 C)
  {
    if (A.Physical >= MAX_int64 / 8) return A;
    return {A.Physical + P, A.Change + C};
  }

  struct FResidualEdge
  {
    int32 To = INDEX_NONE;
    int32 Reverse = INDEX_NONE;
    int32 Capacity = 0;
    int64 PhysicalCost = 0;
    int64 ChangeCost = 0;
    int32 StableOrder = 0;
    int32 FromCellKey = INDEX_NONE;
    int32 ToCellKey = INDEX_NONE;
    bool bTopologyForward = false;
    bool bReuseArc = false;
    int32 InitialCapacity = 0;
  };

  void AddResidualEdge(TArray<TArray<FResidualEdge>>& Graph, const int32 From, const int32 To,
    const int32 Capacity, const int64 Physical, const int64 Change, const int32 StableOrder,
    const int32 FromCell = INDEX_NONE, const int32 ToCell = INDEX_NONE,
    const bool bTopology = false, const bool bReuse = false)
  {
    FResidualEdge Forward;
    Forward.To = To;
    Forward.Reverse = Graph[To].Num();
    Forward.Capacity = Capacity;
    Forward.InitialCapacity = Capacity;
    Forward.PhysicalCost = Physical;
    Forward.ChangeCost = Change;
    Forward.StableOrder = StableOrder;
    Forward.FromCellKey = FromCell;
    Forward.ToCellKey = ToCell;
    Forward.bTopologyForward = bTopology;
    Forward.bReuseArc = bReuse;
    FResidualEdge Reverse;
    Reverse.To = From;
    Reverse.Reverse = Graph[From].Num();
    Reverse.PhysicalCost = -Physical;
    Reverse.ChangeCost = -Change;
    Reverse.StableOrder = StableOrder;
    Graph[From].Add(Forward);
    Graph[To].Add(Reverse);
  }
}

int32 FCrowdDemoTargetRegionTransportKernel::SectorCountForRadius(const float RadiusCm)
{
  if (RadiusCm < 200.0f) return 8;
  if (RadiusCm < 400.0f) return 16;
  if (RadiusCm < 800.0f) return 32;
  return 64;
}

FVector2f FCrowdDemoTargetRegionTransportKernel::ComposeTargetAdvectedFarFlowVelocity(
  const FVector2f& SharedFlowPreferredVelocity,
  const FVector2f& TargetVelocity,
  const float MaxSpeedCmps)
{
  if (!FMath::IsFinite(SharedFlowPreferredVelocity.X)
    || !FMath::IsFinite(SharedFlowPreferredVelocity.Y)
    || !FMath::IsFinite(TargetVelocity.X)
    || !FMath::IsFinite(TargetVelocity.Y)
    || !FMath::IsFinite(MaxSpeedCmps)
    || MaxSpeedCmps <= 0.0f)
  {
    return FVector2f::ZeroVector;
  }
  const FVector2f Combined = SharedFlowPreferredVelocity + TargetVelocity;
  const float SpeedSquared = Combined.SizeSquared();
  return SpeedSquared > FMath::Square(MaxSpeedCmps)
    ? Combined.GetSafeNormal() * MaxSpeedCmps
    : Combined;
}

int32 FCrowdDemoTargetRegionTransportKernel::ComputeEdgeSoftClearancePenaltyCm(
  const FVector2f& Start, const FVector2f& End,
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig)
{
  const float SoftDistance = Settings.PhysicalRadiusCm + Settings.HardSafetyGapCm
    + Settings.SoftMarginCm;
  return FMath::Max(0, Q(SoftDistance - SegmentMinimumEnvironmentClearance(
    Start, End, Settings, FlowConfig)));
}

uint32 FCrowdDemoTargetRegionTransportKernel::ComputeFeasibleGraphHash(
  const FCrowdDemoTargetPolarTopology& Topology)
{
  uint32 Hash = Fold(FnvOffset, 2);
  for (const auto& Cell : Topology.Cells)
  {
    Hash = Fold(Hash, Cell.StableCellKey);
    Hash = Fold(Hash, Cell.bFeasible ? 1 : 0);
    Hash = Fold(Hash, Cell.bTerminal ? 1 : 0);
    Hash = Fold(Hash, Cell.PrimaryDemandRegionKey);
  }
  for (const auto& Edge : Topology.Edges)
  {
    Hash = Fold(Hash, Edge.FromCellKey);
    Hash = Fold(Hash, Edge.ToCellKey);
    Hash = Fold(Hash, Edge.GeometryCostCm);
    Hash = Fold(Hash, Edge.SoftClearancePenaltyCm);
    Hash = Fold(Hash, Edge.RadialDeviationPenaltyCm);
    Hash = Fold(Hash, Edge.bCrossBand ? 1 : 0);
  }
  return Hash;
}

void FCrowdDemoTargetRegionTransportKernel::BuildTopology(
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
  FCrowdDemoTargetPolarTopology& OutTopology,
  FCrowdDemoTargetPolarTopologySummary& OutSummary)
{
  OutTopology = {};
  OutSummary = {};
  const bool bSettingsValid = Settings.RadialBandWidthCm > 0.0f
    && Settings.DemandRegionCount == 16 && Settings.PositionQuantumCm > 0.0f
    && Settings.MaximumCenterDistanceCm >= Settings.MinimumCenterDistanceCm;
  if (!bSettingsValid) return;
  TArray<FCrowdDemoSharedFlowObstacleSpec> Obstacles = FlowConfig.ObstacleSpecs;
  Obstacles.Sort([](const auto& A, const auto& B) { return A.ObstacleId < B.ObstacleId; });
  for (int32 Index = 1; Index < Obstacles.Num(); ++Index)
    if (Obstacles[Index - 1].ObstacleId == Obstacles[Index].ObstacleId) return;
  FCrowdDemoSharedFlowFieldConfig StableFlow = FlowConfig;
  StableFlow.ObstacleSpecs = Obstacles;
  const float DomainRadius = Settings.MaximumCenterDistanceCm + Settings.InfluenceBlendWidthCm;
  const int32 BandCount = FMath::Max(1, FMath::CeilToInt(DomainRadius / Settings.RadialBandWidthCm));
  OutTopology.BandCellOffsets.SetNum(BandCount + 1);
  OutTopology.BandSectorCounts.SetNum(BandCount);
  int32 CellOffset = 0;
  const float HardClearance = Settings.PhysicalRadiusCm + Settings.HardSafetyGapCm;
  const float TargetHard = Settings.TargetPhysicalRadiusCm + Settings.PhysicalRadiusCm
    + FMath::Max(Settings.HardSafetyGapCm, Settings.TargetHardSafetyGapCm);
  uint32 FeasibleGraphHash = Fold(FnvOffset, 2);
  uint32 TopologyHash = Fold(FnvOffset, 1);
  for (int32 Band = 0; Band < BandCount; ++Band)
  {
    OutTopology.BandCellOffsets[Band] = CellOffset;
    const float Radius = (static_cast<float>(Band) + 0.5f) * Settings.RadialBandWidthCm;
    const int32 SectorCount = SectorCountForRadius(Radius);
    OutTopology.BandSectorCounts[Band] = SectorCount;
    for (int32 Sector = 0; Sector < SectorCount; ++Sector)
    {
      const float Angle = (static_cast<float>(Sector) + 0.5f)
        * 2.0f * PI / static_cast<float>(SectorCount);
      auto& Cell = OutTopology.Cells.AddDefaulted_GetRef();
      Cell.StableCellKey = CellOffset++;
      Cell.RadialBand = Band;
      Cell.AngularSector = Sector;
      Cell.SectorCount = SectorCount;
      Cell.RelativeAnchorCm = Quantize(FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius,
        Settings.PositionQuantumCm);
      Cell.WorldAnchorCm = Quantize(Settings.TargetLocation + Cell.RelativeAnchorCm,
        Settings.PositionQuantumCm);
      Cell.PrimaryDemandRegionKey = RegionForOffset(Cell.RelativeAnchorCm,
        Settings.DemandRegionCount);
      Cell.bTargetBlocked = Radius + 0.01f < TargetHard;
      Cell.bBoundsBlocked = Cell.WorldAnchorCm.X < FlowConfig.BoundsMin.X + HardClearance
        || Cell.WorldAnchorCm.X > FlowConfig.BoundsMax.X - HardClearance
        || Cell.WorldAnchorCm.Y < FlowConfig.BoundsMin.Y + HardClearance
        || Cell.WorldAnchorCm.Y > FlowConfig.BoundsMax.Y - HardClearance;
      for (const auto& Obstacle : Obstacles)
        if (PointInsideExpanded(Cell.WorldAnchorCm, Obstacle, HardClearance))
        { Cell.bObstacleBlocked = true; break; }
      Cell.bFeasible = !Cell.bTargetBlocked && !Cell.bBoundsBlocked && !Cell.bObstacleBlocked;
      Cell.bTerminal = Cell.bFeasible
        && Radius + 0.01f >= Settings.MinimumCenterDistanceCm
        && Radius <= Settings.MaximumCenterDistanceCm + 0.01f;
      OutSummary.BoundsBlockedCellCount += Cell.bBoundsBlocked ? 1 : 0;
      OutSummary.ObstacleBlockedCellCount += Cell.bObstacleBlocked ? 1 : 0;
      OutSummary.TargetBlockedCellCount += Cell.bTargetBlocked ? 1 : 0;
      OutSummary.FeasibleCellCount += Cell.bFeasible ? 1 : 0;
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.StableCellKey);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.bFeasible ? 1 : 0);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.bTerminal ? 1 : 0);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.PrimaryDemandRegionKey);
      TopologyHash = Fold(TopologyHash, Cell.StableCellKey);
      TopologyHash = Fold(TopologyHash, Band);
      TopologyHash = Fold(TopologyHash, Sector);
      TopologyHash = Fold(TopologyHash, SectorCount);
      TopologyHash = Fold(TopologyHash, Q(Cell.RelativeAnchorCm.X));
      TopologyHash = Fold(TopologyHash, Q(Cell.RelativeAnchorCm.Y));

      const int32 BeginUnit = Sector * AngularUnits / SectorCount;
      const int32 EndUnit = (Sector + 1) * AngularUnits / SectorCount;
      for (int32 Region = 0; Region < Settings.DemandRegionCount; ++Region)
      {
        const int32 RegionBegin = Region * AngularUnits / Settings.DemandRegionCount;
        const int32 RegionEnd = (Region + 1) * AngularUnits / Settings.DemandRegionCount;
        const int32 Overlap = FMath::Max(0, FMath::Min(EndUnit, RegionEnd)
          - FMath::Max(BeginUnit, RegionBegin));
        if (Overlap <= 0) continue;
        auto& Link = OutTopology.RegionLinks.AddDefaulted_GetRef();
        Link.CellKey = Cell.StableCellKey;
        Link.RegionKey = Region;
        Link.AngularOverlapQ15 = FMath::RoundToInt(
          static_cast<float>(Overlap) / static_cast<float>(EndUnit - BeginUnit) * 32767.0f);
        Link.bTerminal = Cell.bTerminal;
      }
    }
  }
  OutTopology.BandCellOffsets[BandCount] = CellOffset;

  auto AddEdge = [&](const FCrowdDemoTargetPolarCell& From,
    const FCrowdDemoTargetPolarCell& To, const bool bCrossBand)
  {
    if (!From.bFeasible || !To.bFeasible) return;
    if (!SegmentSafe(From.WorldAnchorCm, To.WorldAnchorCm, StableFlow,
      HardClearance, Settings.TargetLocation, TargetHard)) return;
    FCrowdDemoTargetPolarEdge Edge;
    Edge.FromCellKey = From.StableCellKey;
    Edge.ToCellKey = To.StableCellKey;
    Edge.GeometryCostCm = FMath::Max(1, Q((To.WorldAnchorCm - From.WorldAnchorCm).Size()));
    Edge.SoftClearancePenaltyCm = ComputeEdgeSoftClearancePenaltyCm(
      From.WorldAnchorCm, To.WorldAnchorCm, Settings, StableFlow);
    const float ToRadius = To.RelativeAnchorCm.Size();
    Edge.RadialDeviationPenaltyCm = ToRadius < Settings.MinimumCenterDistanceCm
      ? Q(Settings.MinimumCenterDistanceCm - ToRadius)
      : (ToRadius > Settings.MaximumCenterDistanceCm
        ? Q(ToRadius - Settings.MaximumCenterDistanceCm) : 0);
    Edge.bCrossBand = bCrossBand;
    OutTopology.Edges.Add(Edge);
  };

  for (int32 Band = 0; Band < BandCount; ++Band)
  {
    const int32 Count = OutTopology.BandSectorCounts[Band];
    const int32 Base = OutTopology.BandCellOffsets[Band];
    for (int32 Sector = 0; Sector < Count; ++Sector)
    {
      const auto& Cell = OutTopology.Cells[Base + Sector];
      AddEdge(Cell, OutTopology.Cells[Base + (Sector + 1) % Count], false);
      AddEdge(Cell, OutTopology.Cells[Base + (Sector - 1 + Count) % Count], false);
      if (Band + 1 < BandCount)
      {
        const int32 OuterCount = OutTopology.BandSectorCounts[Band + 1];
        const int32 OuterBase = OutTopology.BandCellOffsets[Band + 1];
        const int32 Begin = Sector * AngularUnits / Count;
        const int32 End = (Sector + 1) * AngularUnits / Count;
        for (int32 Outer = 0; Outer < OuterCount; ++Outer)
        {
          const int32 OuterBegin = Outer * AngularUnits / OuterCount;
          const int32 OuterEnd = (Outer + 1) * AngularUnits / OuterCount;
          if (FMath::Min(End, OuterEnd) <= FMath::Max(Begin, OuterBegin)) continue;
          AddEdge(Cell, OutTopology.Cells[OuterBase + Outer], true);
          AddEdge(OutTopology.Cells[OuterBase + Outer], Cell, true);
        }
      }
    }
  }
  OutTopology.Edges.Sort([](const auto& A, const auto& B)
  {
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  OutTopology.RegionLinks.Sort([](const auto& A, const auto& B)
  {
    return A.CellKey != B.CellKey ? A.CellKey < B.CellKey : A.RegionKey < B.RegionKey;
  });
  for (const auto& Link : OutTopology.RegionLinks)
  {
    TopologyHash = Fold(TopologyHash, Link.CellKey);
    TopologyHash = Fold(TopologyHash, Link.RegionKey);
    TopologyHash = Fold(TopologyHash, Link.AngularOverlapQ15);
    TopologyHash = Fold(TopologyHash, Link.bTerminal ? 1 : 0);
  }
  for (const auto& Edge : OutTopology.Edges)
  {
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.FromCellKey);
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.ToCellKey);
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.GeometryCostCm);
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.SoftClearancePenaltyCm);
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.RadialDeviationPenaltyCm);
    FeasibleGraphHash = Fold(FeasibleGraphHash, Edge.bCrossBand ? 1 : 0);
    TopologyHash = Fold(TopologyHash, Edge.FromCellKey);
    TopologyHash = Fold(TopologyHash, Edge.ToCellKey);
    TopologyHash = Fold(TopologyHash, Edge.GeometryCostCm);
    TopologyHash = Fold(TopologyHash, Edge.SoftClearancePenaltyCm);
    TopologyHash = Fold(TopologyHash, Edge.RadialDeviationPenaltyCm);
    TopologyHash = Fold(TopologyHash, Edge.bCrossBand ? 1 : 0);
    OutSummary.CrossBandEdgeCount += Edge.bCrossBand ? 1 : 0;
  }
  FeasibleGraphHash = ComputeFeasibleGraphHash(OutTopology);
  OutTopology.FeasibleGraphHash = FeasibleGraphHash;
  OutTopology.EnvironmentHash = FeasibleGraphHash;
  OutTopology.TopologyHash = TopologyHash;
  OutTopology.bValid = !OutTopology.Cells.IsEmpty() && !OutTopology.Edges.IsEmpty();
  OutSummary.CellCount = OutTopology.Cells.Num();
  OutSummary.EdgeCount = OutTopology.Edges.Num();
  OutSummary.FeasibleGraphHash = FeasibleGraphHash;
  OutSummary.EnvironmentHash = FeasibleGraphHash;
  OutSummary.TopologyHash = TopologyHash;
  OutSummary.bValid = OutTopology.bValid;
}

void FCrowdDemoTargetRegionTransportKernel::BuildDemand(
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputAgents,
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
  const FCrowdDemoSharedFlowField* SharedFlowField,
  const FCrowdDemoTargetPolarTopology& Topology,
  FCrowdDemoTargetRegionDemandResult& OutDemand,
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputExternalAgents)
{
  OutDemand = {};
  if (!Topology.bValid || Settings.DemandRegionCount <= 0) return;
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TArray<FCrowdDemoTargetRegionTransportAgent> ExternalAgents(InputExternalAgents);
  ExternalAgents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TSet<int32> OwnAgentIds;
  for (const auto& Agent : Agents)
  {
    if (OwnAgentIds.Contains(Agent.AgentId)) return;
    OwnAgentIds.Add(Agent.AgentId);
  }
  int32 PreviousExternalId = INDEX_NONE;
  OutDemand.ExternalPopulationByCell.Init(0, Topology.Cells.Num());
  OutDemand.ExternalCongestionCostByCellCm.Init(0, Topology.Cells.Num());
  uint32 ExternalPopulationHash = Fold(FnvOffset, 2);
  for (const auto& ExternalAgent : ExternalAgents)
  {
    if (ExternalAgent.AgentId <= PreviousExternalId
      || OwnAgentIds.Contains(ExternalAgent.AgentId)
      || !FMath::IsFinite(ExternalAgent.PhysicalRadiusCm)
      || !FMath::IsFinite(ExternalAgent.HardSafetyGapCm)
      || !FMath::IsFinite(ExternalAgent.SoftMarginCm)
      || ExternalAgent.PhysicalRadiusCm <= 0.0f
      || ExternalAgent.HardSafetyGapCm < 0.0f
      || ExternalAgent.SoftMarginCm < 0.0f) return;
    PreviousExternalId = ExternalAgent.AgentId;
    const int32 DirectCell = DirectCellForLocation(
      ExternalAgent.Location, Settings, Topology);
    const FCrowdDemoTargetPolarCell* Cell = FindCell(Topology, DirectCell);
    const int32 OccupiedCell = Cell && Cell->bFeasible ? DirectCell : INDEX_NONE;
    if (OutDemand.ExternalPopulationByCell.IsValidIndex(OccupiedCell))
    {
      ++OutDemand.ExternalPopulationByCell[OccupiedCell];
      const int32 PairSoftDistanceCm = FMath::RoundToInt(
        Settings.PhysicalRadiusCm + ExternalAgent.PhysicalRadiusCm
        + FMath::Max(Settings.HardSafetyGapCm, ExternalAgent.HardSafetyGapCm)
        + Settings.SoftMarginCm + ExternalAgent.SoftMarginCm);
      if (PairSoftDistanceCm <= 0
        || OutDemand.ExternalCongestionCostByCellCm[OccupiedCell]
          > MAX_int32 - PairSoftDistanceCm)
      {
        return;
      }
      OutDemand.ExternalCongestionCostByCellCm[OccupiedCell] += PairSoftDistanceCm;
      ++OutDemand.ExternalPopulationAgentCount;
    }
    ExternalPopulationHash = Fold(ExternalPopulationHash, ExternalAgent.AgentId);
    ExternalPopulationHash = Fold(ExternalPopulationHash, OccupiedCell);
    ExternalPopulationHash = Fold(ExternalPopulationHash,
      FMath::RoundToInt(ExternalAgent.PhysicalRadiusCm));
    ExternalPopulationHash = Fold(ExternalPopulationHash,
      FMath::RoundToInt(ExternalAgent.HardSafetyGapCm));
    ExternalPopulationHash = Fold(ExternalPopulationHash,
      FMath::RoundToInt(ExternalAgent.SoftMarginCm));
  }
  for (const int32 Population : OutDemand.ExternalPopulationByCell)
    OutDemand.ExternalOccupiedCellCount += Population > 0 ? 1 : 0;
  OutDemand.ExternalPopulationHash = ExternalPopulationHash;
  int32 PreviousId = INDEX_NONE;
  OutDemand.Regions.SetNum(Settings.DemandRegionCount);
  for (int32 Region = 0; Region < Settings.DemandRegionCount; ++Region)
    OutDemand.Regions[Region].StableRegionKey = Region;
  for (const auto& Link : Topology.RegionLinks)
    if (Link.bTerminal && Topology.Cells[Link.CellKey].bFeasible)
      ++OutDemand.Regions[Link.RegionKey].AvailableCapacity;
  for (auto& Region : OutDemand.Regions)
  {
    Region.bFeasible = Region.AvailableCapacity > 0;
    OutDemand.FeasibleRegionCount += Region.bFeasible ? 1 : 0;
  }
  if (OutDemand.FeasibleRegionCount <= 0) return;
  const int32 NormalizedPhase =
    ((Settings.DemandRegionPhaseOffset % Settings.DemandRegionCount)
      + Settings.DemandRegionCount) % Settings.DemandRegionCount;
  TArray<int32> FeasibleRegionIndices;
  FeasibleRegionIndices.Reserve(OutDemand.FeasibleRegionCount);
  for (int32 RegionIndex = 0; RegionIndex < OutDemand.Regions.Num(); ++RegionIndex)
    if (OutDemand.Regions[RegionIndex].bFeasible)
      FeasibleRegionIndices.Add(RegionIndex);
  const int32 FeasiblePhaseOffset = FeasibleRegionIndices.IsEmpty() ? 0
    : NormalizedPhase * FeasibleRegionIndices.Num() / Settings.DemandRegionCount;
  int32 Remaining = Agents.Num();
  while (Remaining > 0)
  {
    bool bProgress = false;
    for (int32 RegionOrder = 0;
      RegionOrder < FeasibleRegionIndices.Num() && Remaining > 0; ++RegionOrder)
    {
      const int32 RegionIndex = FeasibleRegionIndices[
        (FeasiblePhaseOffset + RegionOrder) % FeasibleRegionIndices.Num()];
      auto& Region = OutDemand.Regions[RegionIndex];
      if (Region.DesiredPopulation < Region.AvailableCapacity
        && Remaining > 0)
      {
        ++Region.DesiredPopulation;
        --Remaining;
        bProgress = true;
      }
    }
    if (!bProgress) break;
  }
  OutDemand.DesiredPopulationTotal = Agents.Num() - Remaining;
  uint32 MembershipHash = Fold(FnvOffset, 1);
  for (const auto& Agent : Agents)
  {
    if (Agent.AgentId <= PreviousId) return;
    PreviousId = Agent.AgentId;
    auto& State = OutDemand.AgentStates.AddDefaulted_GetRef();
    State.AgentId = Agent.AgentId;
    State.CurrentCellKey = AttachSource(
      Agent.Location, Settings, FlowConfig, SharedFlowField, Topology);
    State.bSourceAttached = State.CurrentCellKey != INDEX_NONE;
    OutDemand.SourceAttachmentFailureCount += State.bSourceAttached ? 0 : 1;
    const FVector2f Offset = Agent.Location - Settings.TargetLocation;
    State.CurrentRegionKey = RegionForOffset(Offset, Settings.DemandRegionCount);
    const auto* Cell = FindCell(Topology, State.CurrentCellKey);
    State.bTerminal = Cell && Cell->bTerminal
      && Offset.Size() + 0.01f >= Settings.MinimumCenterDistanceCm
      && Offset.Size() <= Settings.MaximumCenterDistanceCm + 0.01f;
    if (State.bTerminal)
    {
      ++OutDemand.Regions[State.CurrentRegionKey].CurrentPopulation;
      ++OutDemand.CurrentTerminalPopulation;
    }
    MembershipHash = Fold(MembershipHash, Agent.AgentId);
  }
  for (auto& Region : OutDemand.Regions)
  {
    Region.Deficit = FMath::Max(0, Region.DesiredPopulation - Region.CurrentPopulation);
    Region.Surplus = FMath::Max(0, Region.CurrentPopulation - Region.DesiredPopulation);
    OutDemand.TotalDeficit += Region.Deficit;
    OutDemand.TotalSurplus += Region.Surplus;
    int32 Keep = FMath::Min(Region.CurrentPopulation, Region.DesiredPopulation);
    for (auto& State : OutDemand.AgentStates)
      if (State.bTerminal && State.CurrentRegionKey == Region.StableRegionKey)
      {
        State.bTerminalStay = Keep-- > 0;
        State.bSupply = !State.bTerminalStay;
      }
  }
  for (auto& State : OutDemand.AgentStates)
    if (!State.bTerminal) State.bSupply = true;
  for (const auto& State : OutDemand.AgentStates)
    OutDemand.SupplyAgentCount += State.bSupply ? 1 : 0;

  uint32 Hash = Fold(FnvOffset, 2);
  Hash = Fold(Hash, Topology.TopologyHash);
  Hash = Fold(Hash, NormalizedPhase);
  Hash = Fold(Hash, MembershipHash);
  Hash = Fold(Hash, OutDemand.ExternalPopulationHash);
  Hash = Fold(Hash, OutDemand.ExternalPopulationAgentCount);
  Hash = Fold(Hash, OutDemand.ExternalOccupiedCellCount);
  for (int32 CellKey = 0; CellKey < OutDemand.ExternalPopulationByCell.Num(); ++CellKey)
    if (OutDemand.ExternalPopulationByCell[CellKey] > 0)
    {
      Hash = Fold(Hash, CellKey);
      Hash = Fold(Hash, OutDemand.ExternalPopulationByCell[CellKey]);
      Hash = Fold(Hash, OutDemand.ExternalCongestionCostByCellCm[CellKey]);
    }
  for (const auto& Region : OutDemand.Regions)
  {
    Hash = Fold(Hash, Region.StableRegionKey);
    Hash = Fold(Hash, Region.AvailableCapacity);
    Hash = Fold(Hash, Region.CurrentPopulation);
    Hash = Fold(Hash, Region.DesiredPopulation);
    Hash = Fold(Hash, Region.Deficit);
    Hash = Fold(Hash, Region.Surplus);
  }
  for (const auto& State : OutDemand.AgentStates)
  {
    Hash = Fold(Hash, State.AgentId);
    Hash = Fold(Hash, State.CurrentCellKey);
    Hash = Fold(Hash, State.CurrentRegionKey);
    Hash = Fold(Hash, State.bTerminalStay ? 1 : 0);
    Hash = Fold(Hash, State.bSupply ? 1 : 0);
  }
  OutDemand.MembershipHash = MembershipHash;
  OutDemand.DemandHash = Hash;
  OutDemand.bValid = Remaining == 0 && OutDemand.SourceAttachmentFailureCount == 0
    && OutDemand.DesiredPopulationTotal == Agents.Num();
}

void FCrowdDemoTargetRegionTransportKernel::SolveTransport(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan* PreviousPlan,
  const int32 PlanEpoch,
  const int32 FixedStepIndex,
  const int32 TargetRevision,
  FCrowdDemoTargetRegionFlowPlan& OutPlan)
{
  OutPlan = {};
  OutPlan.PlanEpoch = PlanEpoch;
  OutPlan.BuildFixedStepIndex = FixedStepIndex;
  OutPlan.TargetRevision = TargetRevision;
  OutPlan.FeasibleGraphHash = Topology.FeasibleGraphHash;
  OutPlan.EnvironmentHash = Topology.EnvironmentHash;
  OutPlan.MembershipHash = Demand.MembershipHash;
  OutPlan.ExternalPopulationHash = Demand.ExternalPopulationHash;
  if (!Topology.bValid || !Demand.bValid) return;
  const int32 CellCount = Topology.Cells.Num();
  const int32 RegionCount = Demand.Regions.Num();
  const int32 GateBase = CellCount + RegionCount;
  const int32 Source = GateBase + CellCount;
  const int32 Sink = Source + 1;
  TArray<TArray<FResidualEdge>> Graph;
  Graph.SetNum(Sink + 1);
  TArray<int32> SupplyByCell;
  SupplyByCell.Init(0, CellCount);
  TArray<int32> PopulationByCell;
  PopulationByCell.Init(0, CellCount);
  for (const auto& State : Demand.AgentStates)
  {
    if (PopulationByCell.IsValidIndex(State.CurrentCellKey))
      ++PopulationByCell[State.CurrentCellKey];
    if (State.bSupply && SupplyByCell.IsValidIndex(State.CurrentCellKey))
      ++SupplyByCell[State.CurrentCellKey];
  }
  auto PreviousQuota = [&](const int32 From, const int32 To)
  {
    if (!PreviousPlan) return 0;
    for (const auto& Flow : PreviousPlan->EdgeFlows)
      if (Flow.FromCellKey == From && Flow.ToCellKey == To) return Flow.AgentQuota;
    return 0;
  };
  int32 StableOrder = 0;
  for (int32 Cell = 0; Cell < CellCount; ++Cell)
  {
    if (SupplyByCell[Cell] <= 0) continue;
    if (!Topology.Cells[Cell].bTerminal)
    {
      AddResidualEdge(Graph, Source, Cell, SupplyByCell[Cell], 0, 0, StableOrder++);
      continue;
    }
    const int32 Gate = GateBase + Cell;
    AddResidualEdge(Graph, Source, Gate, SupplyByCell[Cell], 0, 0, StableOrder++);
    for (const auto& Edge : Topology.Edges)
    {
      if (Edge.FromCellKey != Cell) continue;
      const int32 Physical = Edge.GeometryCostCm + Edge.SoftClearancePenaltyCm
        + Edge.RadialDeviationPenaltyCm + PopulationByCell[Edge.ToCellKey] * 94
        + (Demand.ExternalCongestionCostByCellCm.IsValidIndex(Edge.ToCellKey)
          ? Demand.ExternalCongestionCostByCellCm[Edge.ToCellKey] : 0);
      const int32 Reuse = FMath::Min(SupplyByCell[Cell],
        PreviousQuota(Edge.FromCellKey, Edge.ToCellKey));
      if (Reuse > 0)
        AddResidualEdge(Graph, Gate, Edge.ToCellKey, Reuse, Physical, 0,
          StableOrder++, Edge.FromCellKey, Edge.ToCellKey, true, true);
      AddResidualEdge(Graph, Gate, Edge.ToCellKey, SupplyByCell[Cell], Physical, 1,
        StableOrder++, Edge.FromCellKey, Edge.ToCellKey, true, false);
    }
  }
  for (const auto& Edge : Topology.Edges)
  {
    const int32 Physical = Edge.GeometryCostCm + Edge.SoftClearancePenaltyCm
      + Edge.RadialDeviationPenaltyCm + PopulationByCell[Edge.ToCellKey] * 94
      + (Demand.ExternalCongestionCostByCellCm.IsValidIndex(Edge.ToCellKey)
        ? Demand.ExternalCongestionCostByCellCm[Edge.ToCellKey] : 0);
    const int32 Reuse = FMath::Min(Demand.SupplyAgentCount,
      PreviousQuota(Edge.FromCellKey, Edge.ToCellKey));
    if (Reuse > 0)
      AddResidualEdge(Graph, Edge.FromCellKey, Edge.ToCellKey, Reuse,
        Physical, 0, StableOrder++, Edge.FromCellKey, Edge.ToCellKey, true, true);
    AddResidualEdge(Graph, Edge.FromCellKey, Edge.ToCellKey,
      Demand.SupplyAgentCount, Physical, 1, StableOrder++,
      Edge.FromCellKey, Edge.ToCellKey, true, false);
  }
  for (const auto& Link : Topology.RegionLinks)
    if (Link.bTerminal && Demand.Regions[Link.RegionKey].Deficit > 0)
      AddResidualEdge(Graph, Link.CellKey, CellCount + Link.RegionKey,
        Demand.Regions[Link.RegionKey].Deficit, 0, 0, StableOrder++);
  for (const auto& Region : Demand.Regions)
    if (Region.Deficit > 0)
      AddResidualEdge(Graph, CellCount + Region.StableRegionKey, Sink,
        Region.Deficit, 0, 0, StableOrder++);

  int32 Flow = 0;
  int64 TotalPhysical = 0;
  int64 TotalChange = 0;
  while (true)
  {
    TArray<FCost> Distance;
    Distance.Init(FCost(), Graph.Num());
    Distance[Source] = {0, 0};
    TArray<int32> PreviousNode;
    TArray<int32> PreviousEdge;
    PreviousNode.Init(INDEX_NONE, Graph.Num());
    PreviousEdge.Init(INDEX_NONE, Graph.Num());
    for (int32 Pass = 0; Pass < Graph.Num() - 1; ++Pass)
    {
      bool bChanged = false;
      for (int32 From = 0; From < Graph.Num(); ++From)
      {
        if (From == Sink) continue;
        for (int32 EdgeIndex = 0; EdgeIndex < Graph[From].Num(); ++EdgeIndex)
        {
          const auto& Edge = Graph[From][EdgeIndex];
          if (Edge.To == Source || Edge.Capacity <= 0
            || Distance[From].Physical >= MAX_int64 / 8) continue;
          const FCost Candidate = Add(Distance[From], Edge.PhysicalCost, Edge.ChangeCost);
          // Stable node/edge scan order selects the first equal-cost predecessor. Replacing an
          // equal-cost predecessor with a residual reverse edge can create a zero-cost parent
          // cycle after the first augmentation.
          if (Less(Candidate, Distance[Edge.To]))
          {
            Distance[Edge.To] = Candidate;
            PreviousNode[Edge.To] = From;
            PreviousEdge[Edge.To] = EdgeIndex;
            bChanged = true;
          }
        }
      }
      if (!bChanged) break;
    }
    if (PreviousNode[Sink] == INDEX_NONE) break;
    int32 Node = Sink;
    bool bPathValid = true;
    int32 HopCount = 0;
    while (Node != Source)
    {
      if (++HopCount > Graph.Num()) { bPathValid = false; break; }
      const int32 From = PreviousNode[Node];
      const int32 EdgeIndex = PreviousEdge[Node];
      if (From == INDEX_NONE || EdgeIndex == INDEX_NONE) { bPathValid = false; break; }
      auto& Edge = Graph[From][EdgeIndex];
      --Edge.Capacity;
      ++Graph[Node][Edge.Reverse].Capacity;
      Node = From;
    }
    if (!bPathValid) break;
    ++Flow;
    TotalPhysical += Distance[Sink].Physical;
    TotalChange += Distance[Sink].Change;
  }

  TMap<int64, FCrowdDemoTargetPolarEdgeFlow> FlowByKey;
  for (int32 From = 0; From < Graph.Num(); ++From)
    for (const auto& Edge : Graph[From])
      if (Edge.bTopologyForward && Edge.InitialCapacity > 0)
      {
        const int32 Used = Edge.InitialCapacity - Edge.Capacity;
        if (Used <= 0) continue;
        const int64 Key = (static_cast<int64>(Edge.FromCellKey) << 32)
          | static_cast<uint32>(Edge.ToCellKey);
        auto& Result = FlowByKey.FindOrAdd(Key);
        Result.FromCellKey = Edge.FromCellKey;
        Result.ToCellKey = Edge.ToCellKey;
        Result.AgentQuota += Used;
        Result.ReusedQuota += Edge.bReuseArc ? Used : 0;
      }
  FlowByKey.GenerateValueArray(OutPlan.EdgeFlows);
  OutPlan.EdgeFlows.Sort([](const auto& A, const auto& B)
  {
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  OutPlan.RoutedAgentCount = Flow;
  OutPlan.UnroutedAgentCount = FMath::Max(0, Demand.TotalDeficit - Flow);
  OutPlan.TotalPhysicalCost = TotalPhysical;
  OutPlan.ChangedQuotaUnitCount = TotalChange;
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, PlanEpoch);
  Hash = Fold(Hash, TargetRevision);
  Hash = Fold(Hash, Topology.FeasibleGraphHash);
  Hash = Fold(Hash, Demand.MembershipHash);
  Hash = Fold(Hash, Demand.ExternalPopulationHash);
  Hash = Fold(Hash, Flow);
  Hash = Fold(Hash, OutPlan.UnroutedAgentCount);
  Hash = Fold(Hash, TotalPhysical);
  Hash = Fold(Hash, TotalChange);
  for (const auto& Edge : OutPlan.EdgeFlows)
  {
    Hash = Fold(Hash, Edge.FromCellKey);
    Hash = Fold(Hash, Edge.ToCellKey);
    Hash = Fold(Hash, Edge.AgentQuota);
    Hash = Fold(Hash, Edge.ReusedQuota);
  }
  OutPlan.TransportHash = Hash;
  OutPlan.bValid = OutPlan.UnroutedAgentCount == 0;
}

void FCrowdDemoTargetRegionTransportKernel::ValidatePlanForDemand(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  const int32 TargetRevision,
  FCrowdDemoTargetRegionPlanValidationResult& OutValidation)
{
  OutValidation = {};
  const int32 CellCount = Topology.Cells.Num();
  TArray<int32> SupplyByCell, InflowByCell, OutflowByCell;
  SupplyByCell.Init(0, CellCount);
  InflowByCell.Init(0, CellCount);
  OutflowByCell.Init(0, CellCount);
  auto PinCell = [&](const int32 CellKey)
  {
    if (OutValidation.FirstFailureCellKey == INDEX_NONE
      || (CellKey != INDEX_NONE && CellKey < OutValidation.FirstFailureCellKey))
      OutValidation.FirstFailureCellKey = CellKey;
  };
  auto PinAgentForCell = [&](const int32 CellKey)
  {
    for (const auto& State : Demand.AgentStates)
      if (State.CurrentCellKey == CellKey
        && (OutValidation.FirstFailureAgentId == INDEX_NONE
          || State.AgentId < OutValidation.FirstFailureAgentId))
        OutValidation.FirstFailureAgentId = State.AgentId;
  };

  if (!Topology.bValid || !Demand.bValid || !Plan.bValid
    || Plan.TargetRevision != TargetRevision
    || Plan.FeasibleGraphHash != Topology.FeasibleGraphHash
    || Plan.MembershipHash != Demand.MembershipHash
    || Plan.ExternalPopulationHash != Demand.ExternalPopulationHash)
  {
    ++OutValidation.InvalidCellCount;
    PinCell(INDEX_NONE);
  }

  for (const auto& State : Demand.AgentStates)
  {
    if (!Topology.Cells.IsValidIndex(State.CurrentCellKey)
      || !Topology.Cells[State.CurrentCellKey].bFeasible)
    {
      ++OutValidation.InvalidCellCount;
      PinCell(State.CurrentCellKey);
      PinAgentForCell(State.CurrentCellKey);
      continue;
    }
    if (State.bSupply) ++SupplyByCell[State.CurrentCellKey];
  }

  int32 PreviousFrom = INDEX_NONE;
  int32 PreviousTo = INDEX_NONE;
  TArray<TArray<int32>> PositiveAdjacency;
  PositiveAdjacency.SetNum(CellCount);
  for (const auto& Flow : Plan.EdgeFlows)
  {
    const bool bStrictlySorted = PreviousFrom == INDEX_NONE
      || Flow.FromCellKey > PreviousFrom
      || (Flow.FromCellKey == PreviousFrom && Flow.ToCellKey > PreviousTo);
    if (!bStrictlySorted || Flow.AgentQuota <= 0)
    {
      ++OutValidation.MissingEdgeCount;
      PinCell(Flow.FromCellKey);
    }
    PreviousFrom = Flow.FromCellKey;
    PreviousTo = Flow.ToCellKey;
    const FCrowdDemoTargetPolarEdge* Found = nullptr;
    for (const auto& Edge : Topology.Edges)
      if (Edge.FromCellKey == Flow.FromCellKey && Edge.ToCellKey == Flow.ToCellKey)
      { Found = &Edge; break; }
    if (!Found)
    {
      ++OutValidation.MissingEdgeCount;
      PinCell(Flow.FromCellKey);
      PinAgentForCell(Flow.FromCellKey);
      continue;
    }
    if (!Topology.Cells.IsValidIndex(Flow.FromCellKey)
      || !Topology.Cells.IsValidIndex(Flow.ToCellKey)
      || !Topology.Cells[Flow.FromCellKey].bFeasible
      || !Topology.Cells[Flow.ToCellKey].bFeasible)
    {
      ++OutValidation.InfeasibleEdgeCount;
      PinCell(Flow.FromCellKey);
      PinAgentForCell(Flow.FromCellKey);
      continue;
    }
    OutflowByCell[Flow.FromCellKey] += Flow.AgentQuota;
    InflowByCell[Flow.ToCellKey] += Flow.AgentQuota;
    PositiveAdjacency[Flow.FromCellKey].Add(Flow.ToCellKey);
  }

  TArray<int32> TerminalDeficitCapacity;
  TerminalDeficitCapacity.Init(0, CellCount);
  for (const auto& Link : Topology.RegionLinks)
    if (Link.bTerminal && Demand.Regions.IsValidIndex(Link.RegionKey)
      && Topology.Cells.IsValidIndex(Link.CellKey))
      TerminalDeficitCapacity[Link.CellKey] += Demand.Regions[Link.RegionKey].Deficit;

  for (int32 Cell = 0; Cell < CellCount; ++Cell)
  {
    if (SupplyByCell[Cell] > 0 && OutflowByCell[Cell] < SupplyByCell[Cell])
    {
      ++OutValidation.InsufficientOutgoingQuotaCellCount;
      PinCell(Cell);
      PinAgentForCell(Cell);
    }
    const int32 Available = SupplyByCell[Cell] + InflowByCell[Cell];
    const int32 Remainder = Available - OutflowByCell[Cell];
    if (Remainder < 0 || Remainder > TerminalDeficitCapacity[Cell])
    {
      ++OutValidation.FlowConservationFailureCount;
      PinCell(Cell);
      PinAgentForCell(Cell);
    }
  }

  for (int32 StartCell = 0; StartCell < CellCount; ++StartCell)
  {
    if (SupplyByCell[StartCell] <= 0) continue;
    TBitArray<> Visited(false, CellCount);
    TArray<int32> Queue;
    Queue.Add(StartCell);
    Visited[StartCell] = true;
    bool bReachesDeficit = false;
    for (int32 Head = 0; Head < Queue.Num() && !bReachesDeficit; ++Head)
    {
      const int32 Cell = Queue[Head];
      if (TerminalDeficitCapacity[Cell] > 0) { bReachesDeficit = true; break; }
      for (const int32 Next : PositiveAdjacency[Cell])
        if (Visited.IsValidIndex(Next) && !Visited[Next])
        { Visited[Next] = true; Queue.Add(Next); }
    }
    if (!bReachesDeficit && Demand.TotalDeficit > 0)
    {
      OutValidation.UnreachableDeficitCount += SupplyByCell[StartCell];
      PinCell(StartCell);
      PinAgentForCell(StartCell);
    }
  }

  OutValidation.bValid = OutValidation.MissingEdgeCount == 0
    && OutValidation.InfeasibleEdgeCount == 0
    && OutValidation.InvalidCellCount == 0
    && OutValidation.InsufficientOutgoingQuotaCellCount == 0
    && OutValidation.FlowConservationFailureCount == 0
    && OutValidation.UnreachableDeficitCount == 0;
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, OutValidation.bValid ? 1 : 0);
  Hash = Fold(Hash, OutValidation.MissingEdgeCount);
  Hash = Fold(Hash, OutValidation.InfeasibleEdgeCount);
  Hash = Fold(Hash, OutValidation.InvalidCellCount);
  Hash = Fold(Hash, OutValidation.InsufficientOutgoingQuotaCellCount);
  Hash = Fold(Hash, OutValidation.FlowConservationFailureCount);
  Hash = Fold(Hash, OutValidation.UnreachableDeficitCount);
  Hash = Fold(Hash, OutValidation.FirstFailureCellKey);
  Hash = Fold(Hash, OutValidation.FirstFailureAgentId);
  Hash = Fold(Hash, TargetRevision);
  Hash = Fold(Hash, Topology.FeasibleGraphHash);
  Hash = Fold(Hash, Demand.DemandHash);
  Hash = Fold(Hash, Plan.TransportHash);
  OutValidation.ValidationHash = Hash;
}

void FCrowdDemoTargetRegionTransportKernel::BuildGuidance(
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputAgents,
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  TArray<FCrowdDemoTargetRegionGuidanceResult>& OutResults,
  FCrowdDemoTargetRegionGuidanceSummary& OutSummary)
{
  OutResults.Reset();
  OutSummary = {};
  TArray<FCrowdDemoTargetRegionTransportAgent> Agents(InputAgents);
  Agents.Sort([](const auto& A, const auto& B) { return A.AgentId < B.AgentId; });
  TMap<int32, const FCrowdDemoTargetRegionAgentDemandState*> StateByAgent;
  for (const auto& State : Demand.AgentStates) StateByAgent.Add(State.AgentId, &State);
  TArray<FCrowdDemoTargetPolarEdgeFlow> StableFlows(Plan.EdgeFlows);
  StableFlows.Sort([](const FCrowdDemoTargetPolarEdgeFlow& A,
    const FCrowdDemoTargetPolarEdgeFlow& B)
  {
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  TMap<int32, TArray<const FCrowdDemoTargetPolarEdgeFlow*>> Outgoing;
  for (const auto& Flow : StableFlows)
    Outgoing.FindOrAdd(Flow.FromCellKey).Add(&Flow);
  for (auto& Pair : Outgoing)
    Pair.Value.Sort([](const FCrowdDemoTargetPolarEdgeFlow& A,
      const FCrowdDemoTargetPolarEdgeFlow& B) { return A.ToCellKey < B.ToCellKey; });
  TMap<int64, int32> UsedByEdge;
  uint32 Hash = Fold(FnvOffset, 1);
  for (const auto& Agent : Agents)
  {
    auto& Result = OutResults.AddDefaulted_GetRef();
    Result.AgentId = Agent.AgentId;
    const auto* const* StatePtr = StateByAgent.Find(Agent.AgentId);
    const auto* State = StatePtr ? *StatePtr : nullptr;
    if (!State)
    {
      Result.Mode = ECrowdDemoTargetRegionGuidanceMode::Unrouted;
      ++OutSummary.UnroutedAgentCount;
      if (OutSummary.FirstUnroutedAgentId == INDEX_NONE)
      {
        OutSummary.FirstUnroutedAgentId = Agent.AgentId;
        OutSummary.FirstUnroutedCellKey = INDEX_NONE;
      }
    }
    else
    {
      Result.CurrentCellKey = State->CurrentCellKey;
      Result.DemandRegionKey = State->CurrentRegionKey;
      const float Distance = (Agent.Location - Settings.TargetLocation).Size();
      if (Distance > Settings.MaximumCenterDistanceCm + Settings.InfluenceBlendWidthCm)
      {
        Result.Mode = ECrowdDemoTargetRegionGuidanceMode::FarFlow;
        Result.DesiredVelocity = Agent.FarFlowPreferredVelocity;
        ++OutSummary.FarFlowAgentCount;
      }
      else if (State->bTerminalStay)
      {
        Result.Mode = ECrowdDemoTargetRegionGuidanceMode::TerminalSettle;
        const FVector2f Offset = Agent.Location - Settings.TargetLocation;
        const FVector2f Normal = Offset.SizeSquared() > UE_SMALL_NUMBER
          ? Offset.GetSafeNormal() : FVector2f(1.0f, 0.0f);
        FVector2f Relative = FVector2f::ZeroVector;
        if (Distance < Settings.MinimumCenterDistanceCm)
          Relative = Normal * FMath::Min(Settings.TransportSpeedCmps,
            (Settings.MinimumCenterDistanceCm - Distance) * Settings.RadialGainPerSecond);
        else if (Distance > Settings.MaximumCenterDistanceCm)
          Relative = -Normal * FMath::Min(Settings.TransportSpeedCmps,
            (Distance - Settings.MaximumCenterDistanceCm) * Settings.RadialGainPerSecond);
        Result.DesiredVelocity = Settings.TargetVelocity + Relative;
        ++OutSummary.TerminalSettleAgentCount;
      }
      else
      {
        const TArray<const FCrowdDemoTargetPolarEdgeFlow*>* Flows = Outgoing.Find(State->CurrentCellKey);
        const FCrowdDemoTargetPolarEdgeFlow* Chosen = nullptr;
        if (Flows)
          for (const auto* Flow : *Flows)
          {
            const int64 Key = (static_cast<int64>(Flow->FromCellKey) << 32)
              | static_cast<uint32>(Flow->ToCellKey);
            int32& Used = UsedByEdge.FindOrAdd(Key);
            if (Used < Flow->AgentQuota) { ++Used; Chosen = Flow; break; }
          }
        if (Chosen && Topology.Cells.IsValidIndex(Chosen->ToCellKey))
        {
          Result.Mode = ECrowdDemoTargetRegionGuidanceMode::Transport;
          Result.NextCellKey = Chosen->ToCellKey;
          const FVector2f Direction = (Topology.Cells[Chosen->ToCellKey].WorldAnchorCm
            - Agent.Location).GetSafeNormal();
          const FVector2f Relative = Direction
            * FMath::Min(Settings.TransportSpeedCmps, Agent.MaxSpeedCmps);
          Result.DesiredVelocity = Settings.TargetVelocity + Relative;
          ++OutSummary.TransportAgentCount;
        }
        else
        {
          Result.Mode = ECrowdDemoTargetRegionGuidanceMode::Unrouted;
          Result.DesiredVelocity = Settings.TargetVelocity;
          ++OutSummary.UnroutedAgentCount;
          if (OutSummary.FirstUnroutedAgentId == INDEX_NONE)
          {
            OutSummary.FirstUnroutedAgentId = Agent.AgentId;
            OutSummary.FirstUnroutedCellKey = State->CurrentCellKey;
          }
        }
      }
    }
    const float Speed = Result.DesiredVelocity.Size();
    if (Speed > Agent.MaxSpeedCmps && Agent.MaxSpeedCmps > 0.0f)
      Result.DesiredVelocity = Result.DesiredVelocity.GetSafeNormal() * Agent.MaxSpeedCmps;
    Result.DesiredVelocity = Quantize(Result.DesiredVelocity, Settings.VelocityQuantumCmps);
  }
  OutSummary.FarFlowAgentCount = 0;
  OutSummary.TransportAgentCount = 0;
  OutSummary.TerminalSettleAgentCount = 0;
  OutSummary.UnroutedAgentCount = 0;
  OutSummary.FirstUnroutedAgentId = INDEX_NONE;
  OutSummary.FirstUnroutedCellKey = INDEX_NONE;
  for (const FCrowdDemoTargetRegionGuidanceResult& Result : OutResults)
  {
    switch (Result.Mode)
    {
    case ECrowdDemoTargetRegionGuidanceMode::FarFlow: ++OutSummary.FarFlowAgentCount; break;
    case ECrowdDemoTargetRegionGuidanceMode::Transport: ++OutSummary.TransportAgentCount; break;
    case ECrowdDemoTargetRegionGuidanceMode::TerminalSettle:
      ++OutSummary.TerminalSettleAgentCount; break;
    default:
      ++OutSummary.UnroutedAgentCount;
      if (OutSummary.FirstUnroutedAgentId == INDEX_NONE)
      {
        OutSummary.FirstUnroutedAgentId = Result.AgentId;
        OutSummary.FirstUnroutedCellKey = Result.CurrentCellKey;
      }
      break;
    }
    Hash = Fold(Hash, Result.AgentId);
    Hash = Fold(Hash, Result.CurrentCellKey);
    Hash = Fold(Hash, Result.NextCellKey);
    Hash = Fold(Hash, Result.DemandRegionKey);
    Hash = Fold(Hash, static_cast<int32>(Result.Mode));
    Hash = Fold(Hash, Q(Result.DesiredVelocity.X, Settings.VelocityQuantumCmps));
    Hash = Fold(Hash, Q(Result.DesiredVelocity.Y, Settings.VelocityQuantumCmps));
  }
  for (const auto& Flow : StableFlows)
  {
    FCrowdDemoTargetRegionGuidanceConsumption Consumption;
    Consumption.FromCellKey = Flow.FromCellKey;
    Consumption.ToCellKey = Flow.ToCellKey;
    Consumption.AgentQuota = Flow.AgentQuota;
    const int64 Key = (static_cast<int64>(Flow.FromCellKey) << 32)
      | static_cast<uint32>(Flow.ToCellKey);
    Consumption.ConsumedQuota = UsedByEdge.FindRef(Key);
    OutSummary.Consumption.Add(Consumption);
    Hash = Fold(Hash, Consumption.FromCellKey);
    Hash = Fold(Hash, Consumption.ToCellKey);
    Hash = Fold(Hash, Consumption.AgentQuota);
    Hash = Fold(Hash, Consumption.ConsumedQuota);
  }
  Hash = Fold(Hash, OutSummary.FirstUnroutedAgentId);
  Hash = Fold(Hash, OutSummary.FirstUnroutedCellKey);
  OutSummary.GuidanceHash = Hash;
  OutSummary.bValid = Plan.bValid && OutSummary.UnroutedAgentCount == 0;
}
