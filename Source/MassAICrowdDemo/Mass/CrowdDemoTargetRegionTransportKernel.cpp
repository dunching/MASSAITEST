#include "Mass/CrowdDemoTargetRegionTransportKernel.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
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

  int64 StableEdgeKey(const int32 FromCellKey, const int32 ToCellKey)
  {
    return (static_cast<int64>(FromCellKey) << 32)
      | static_cast<uint32>(ToCellKey);
  }

  void RefreshExecutionHash(FCrowdDemoTargetRegionQuotaExecutionState& State)
  {
    State.Edges.Sort([](const auto& A, const auto& B)
    {
      return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
        : A.ToCellKey < B.ToCellKey;
    });
    State.ActiveClaims.Sort([](const auto& A, const auto& B)
    {
      return A.AgentId < B.AgentId;
    });
    uint32 Hash = Fold(FnvOffset, 2);
    Hash = Fold(Hash, State.PlanEpoch);
    Hash = Fold(Hash, State.PlanTransportHash);
    Hash = Fold(Hash, State.CompletedTransitionCount);
    for (const auto& Edge : State.Edges)
    {
      Hash = Fold(Hash, Edge.FromCellKey);
      Hash = Fold(Hash, Edge.ToCellKey);
      Hash = Fold(Hash, Edge.InitialQuota);
      Hash = Fold(Hash, Edge.ConsumedQuota);
    }
    for (const auto& Claim : State.ActiveClaims)
    {
      Hash = Fold(Hash, Claim.AgentId);
      Hash = Fold(Hash, Claim.FromCellKey);
      Hash = Fold(Hash, Claim.ToCellKey);
    }
    State.ExecutionHash = Hash;
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

  int32 ComputeTerminalCellCapacity(
    const FCrowdDemoTargetPolarCell& Cell,
    const FCrowdDemoTargetRegionTransportSettings& Settings)
  {
    if (!Cell.bFeasible || !Cell.bTerminal || Cell.SectorCount <= 0)
      return 0;
    const float MinimumSpacingCm =
      2.0f * Settings.PhysicalRadiusCm + Settings.HardSafetyGapCm
      + 2.0f * Settings.SoftMarginCm;
    if (!FMath::IsFinite(MinimumSpacingCm) || MinimumSpacingCm <= 0.0f)
      return 0;
    const float BandInnerCm = static_cast<float>(Cell.RadialBand)
      * Settings.RadialBandWidthCm;
    const float BandOuterCm = BandInnerCm + Settings.RadialBandWidthCm;
    const float UsableInnerCm = FMath::Max(
      BandInnerCm, Settings.MinimumCenterDistanceCm);
    const float UsableOuterCm = FMath::Min(
      BandOuterCm, Settings.MaximumCenterDistanceCm);
    const float UsableRadialExtentCm = UsableOuterCm - UsableInnerCm;
    if (UsableRadialExtentCm <= UE_SMALL_NUMBER)
      return 0;
    const float AngularSpanRadians = 2.0f * PI
      / static_cast<float>(Cell.SectorCount);
    const float ConservativeArcExtentCm = UsableInnerCm
      * AngularSpanRadians;
    const int32 RadialSlots = FMath::Max(
      1, FMath::FloorToInt(UsableRadialExtentCm / MinimumSpacingCm));
    const int32 AngularSlots = FMath::Max(
      1, FMath::FloorToInt(ConservativeArcExtentCm / MinimumSpacingCm));
    if (RadialSlots > MAX_int32 / AngularSlots)
      return 0;
    return RadialSlots * AngularSlots;
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
      int32 Lower = 0;
      int32 Upper = SharedFlowField->NavigationNodes.Num();
      while (Lower < Upper)
      {
        const int32 Middle = Lower + (Upper - Lower) / 2;
        if (SharedFlowField->NavigationNodes[Middle].StableNodeKey
          < Sample.NavigationNodeKey)
        {
          Lower = Middle + 1;
        }
        else
        {
          Upper = Middle;
        }
      }
      if (SharedFlowField->NavigationNodes.IsValidIndex(Lower)
        && SharedFlowField->NavigationNodes[Lower].StableNodeKey
          == Sample.NavigationNodeKey)
      {
        NodeIndex = Lower;
      }
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

FVector2f FCrowdDemoTargetRegionTransportKernel::ComposeEngagedHoldVelocity(
  const FVector2f& AgentLocation,
  const FVector2f& TargetLocation,
  const FVector2f& TargetVelocity,
  const float MaxSpeedCmps)
{
  if (!FMath::IsFinite(AgentLocation.X) || !FMath::IsFinite(AgentLocation.Y)
    || !FMath::IsFinite(TargetLocation.X) || !FMath::IsFinite(TargetLocation.Y)
    || !FMath::IsFinite(TargetVelocity.X) || !FMath::IsFinite(TargetVelocity.Y)
    || !FMath::IsFinite(MaxSpeedCmps) || MaxSpeedCmps <= 0.0f)
  {
    return FVector2f::ZeroVector;
  }

  const FVector2f TargetToAgent = AgentLocation - TargetLocation;
  const FVector2f Normal = TargetToAgent.GetSafeNormal();
  if (Normal.IsNearlyZero())
    return FVector2f::ZeroVector;

  // Positive radial target speed means the target is moving toward the agent.
  // Remove only that component, so Hold never generates proactive retreat.
  const float ApproachingRadialCmps = FMath::Max(
    0.0f, FVector2f::DotProduct(TargetVelocity, Normal));
  FVector2f Result = TargetVelocity - Normal * ApproachingRadialCmps;
  if (Result.SizeSquared() > FMath::Square(MaxSpeedCmps))
    Result = Result.GetSafeNormal() * MaxSpeedCmps;
  return Result;
}

FCrowdDemoTargetEngagementDecision
FCrowdDemoTargetRegionTransportKernel::ResolveTargetEngagement(
  const ECrowdDemoTargetDistanceResponsePolicy Policy,
  const bool bWasEngaged,
  const bool bPreviousTerminalStay,
  const bool bPreviousSupply,
  const float CurrentDistanceCm,
  const float MinimumDistanceCm,
  const float MaximumDistanceCm,
  const float ReleaseHysteresisCm)
{
  FCrowdDemoTargetEngagementDecision Result;
  const bool bAcquireThenHold = Policy
    == ECrowdDemoTargetDistanceResponsePolicy::AcquireThenHold;
  const bool bFinite = FMath::IsFinite(CurrentDistanceCm)
    && FMath::IsFinite(MinimumDistanceCm)
    && FMath::IsFinite(MaximumDistanceCm)
    && FMath::IsFinite(ReleaseHysteresisCm);
  const bool bValidBand = bFinite && MinimumDistanceCm >= 0.0f
    && MaximumDistanceCm + 0.01f >= MinimumDistanceCm
    && ReleaseHysteresisCm >= 0.0f;
  if (!bValidBand)
    return Result;

  const bool bInsideAcquisitionBand = CurrentDistanceCm + 0.01f
      >= MinimumDistanceCm
    && CurrentDistanceCm <= MaximumDistanceCm + 0.01f;
  Result.bAcquired = bAcquireThenHold && !bWasEngaged
    && bPreviousTerminalStay && !bPreviousSupply && bInsideAcquisitionBand;
  Result.bReleased = bWasEngaged && (!bAcquireThenHold
    || bPreviousSupply
    || CurrentDistanceCm > MaximumDistanceCm + ReleaseHysteresisCm + 0.01f);
  Result.bEngagedHold = (bWasEngaged || Result.bAcquired) && !Result.bReleased;
  Result.bSuppressedRetreat = Result.bEngagedHold
    && CurrentDistanceCm < MinimumDistanceCm;
  return Result;
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
    Hash = Fold(Hash, Cell.Capacity);
  }
  for (const auto& Edge : Topology.Edges)
  {
    Hash = Fold(Hash, Edge.FromCellKey);
    Hash = Fold(Hash, Edge.ToCellKey);
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
      Cell.Capacity = ComputeTerminalCellCapacity(Cell, Settings);
      OutSummary.BoundsBlockedCellCount += Cell.bBoundsBlocked ? 1 : 0;
      OutSummary.ObstacleBlockedCellCount += Cell.bObstacleBlocked ? 1 : 0;
      OutSummary.TargetBlockedCellCount += Cell.bTargetBlocked ? 1 : 0;
      OutSummary.FeasibleCellCount += Cell.bFeasible ? 1 : 0;
      OutSummary.TotalFeasibleCapacity += Cell.Capacity;
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.StableCellKey);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.bFeasible ? 1 : 0);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.bTerminal ? 1 : 0);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.PrimaryDemandRegionKey);
      FeasibleGraphHash = Fold(FeasibleGraphHash, Cell.Capacity);
      TopologyHash = Fold(TopologyHash, Cell.StableCellKey);
      TopologyHash = Fold(TopologyHash, Band);
      TopologyHash = Fold(TopologyHash, Sector);
      TopologyHash = Fold(TopologyHash, SectorCount);
      TopologyHash = Fold(TopologyHash, Q(Cell.RelativeAnchorCm.X));
      TopologyHash = Fold(TopologyHash, Q(Cell.RelativeAnchorCm.Y));
      TopologyHash = Fold(TopologyHash, Cell.Capacity);

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
  OutDemand.AvailableCapacityByCell.Init(0, Topology.Cells.Num());
  OutDemand.AdmittedPopulationByCell.Init(0, Topology.Cells.Num());
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
  for (const FCrowdDemoTargetPolarCell& Cell : Topology.Cells)
  {
    if (!Cell.bTerminal || !Cell.bFeasible || Cell.Capacity <= 0)
      continue;
    const int32 ExternalPopulation =
      OutDemand.ExternalPopulationByCell.IsValidIndex(Cell.StableCellKey)
      ? OutDemand.ExternalPopulationByCell[Cell.StableCellKey] : 0;
    const int32 AvailableCapacity = FMath::Max(
      0, Cell.Capacity - ExternalPopulation);
    OutDemand.AvailableCapacityByCell[Cell.StableCellKey] =
      AvailableCapacity;
    if (OutDemand.Regions.IsValidIndex(Cell.PrimaryDemandRegionKey))
      OutDemand.Regions[Cell.PrimaryDemandRegionKey].AvailableCapacity +=
        AvailableCapacity;
    OutDemand.TotalFeasibleCapacity += AvailableCapacity;
  }
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
  TArray<int32> PreferredDesiredByRegion;
  PreferredDesiredByRegion.Reserve(OutDemand.Regions.Num());
  for (const FCrowdDemoTargetDemandRegion& Region : OutDemand.Regions)
    PreferredDesiredByRegion.Add(Region.DesiredPopulation);
  OutDemand.DesiredPopulationTotal = Agents.Num();
  OutDemand.AssignablePopulation = Agents.Num() - Remaining;
  OutDemand.OverflowPopulation = Remaining;
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
    State.bEngagedHold = Agent.bEngagedHold;
    State.bTerminal = State.bEngagedHold || (Cell && Cell->bTerminal
      && Offset.Size() + 0.01f >= Settings.MinimumCenterDistanceCm
      && Offset.Size() <= Settings.MaximumCenterDistanceCm + 0.01f);
    if (State.bTerminal)
    {
      ++OutDemand.Regions[State.CurrentRegionKey].CurrentPopulation;
      ++OutDemand.CurrentTerminalPopulation;
    }
    MembershipHash = Fold(MembershipHash, Agent.AgentId);
  }
  TArray<int32> RemainingCapacityByCell = OutDemand.AvailableCapacityByCell;
  for (FCrowdDemoTargetDemandRegion& Region : OutDemand.Regions)
  {
    Region.CurrentPopulation = 0;
    Region.DesiredPopulation = 0;
    Region.Deficit = 0;
    Region.Surplus = 0;
  }
  int32 AdmittedTerminalPopulation = 0;
  for (FCrowdDemoTargetRegionAgentDemandState& State : OutDemand.AgentStates)
  {
    if (!State.bTerminal) continue;
    int32 AdmissionCellKey = State.CurrentCellKey;
    if (State.bEngagedHold)
    {
      AdmissionCellKey = INDEX_NONE;
      for (const FCrowdDemoTargetPolarCell& Cell : Topology.Cells)
      {
        if (Cell.bTerminal
          && Cell.PrimaryDemandRegionKey == State.CurrentRegionKey
          && RemainingCapacityByCell.IsValidIndex(Cell.StableCellKey)
          && RemainingCapacityByCell[Cell.StableCellKey] > 0)
        {
          AdmissionCellKey = Cell.StableCellKey;
          break;
        }
      }
    }
    if (!RemainingCapacityByCell.IsValidIndex(AdmissionCellKey)
      || RemainingCapacityByCell[AdmissionCellKey] <= 0)
      continue;
    const int32 AdmissionRegionKey =
      Topology.Cells[AdmissionCellKey].PrimaryDemandRegionKey;
    if (!OutDemand.Regions.IsValidIndex(AdmissionRegionKey)) continue;
    State.bCapacityAdmitted = true;
    State.bTerminalStay = true;
    State.AssignedRegionKey = AdmissionRegionKey;
    --RemainingCapacityByCell[AdmissionCellKey];
    ++OutDemand.AdmittedPopulationByCell[AdmissionCellKey];
    ++OutDemand.Regions[AdmissionRegionKey].CurrentPopulation;
    ++OutDemand.Regions[AdmissionRegionKey].DesiredPopulation;
    ++AdmittedTerminalPopulation;
  }

  // Admission is a deterministic maximum flow from the current attached
  // source cells to remaining terminal-cell capacity. Region round-robin is
  // retained as a preference, but cannot assign a source to an unreachable
  // clipped component.
  TArray<int32> SupplyCountByCell;
  SupplyCountByCell.Init(0, Topology.Cells.Num());
  TArray<int32> SupplySourceCells;
  for (FCrowdDemoTargetRegionAgentDemandState& State : OutDemand.AgentStates)
  {
    if (State.bCapacityAdmitted || !State.bSourceAttached
      || !SupplyCountByCell.IsValidIndex(State.CurrentCellKey))
      continue;
    if (SupplyCountByCell[State.CurrentCellKey]++ == 0)
      SupplySourceCells.Add(State.CurrentCellKey);
  }
  SupplySourceCells.Sort();
  TArray<int32> TerminalCells;
  for (const FCrowdDemoTargetPolarCell& Cell : Topology.Cells)
    if (Cell.bTerminal && Cell.bFeasible
      && RemainingCapacityByCell.IsValidIndex(Cell.StableCellKey)
      && RemainingCapacityByCell[Cell.StableCellKey] > 0)
      TerminalCells.Add(Cell.StableCellKey);
  TerminalCells.Sort([&](const int32 A, const int32 B)
  {
    const int32 RegionA = Topology.Cells[A].PrimaryDemandRegionKey;
    const int32 RegionB = Topology.Cells[B].PrimaryDemandRegionKey;
    const bool bPreferredA = PreferredDesiredByRegion.IsValidIndex(RegionA)
      && OutDemand.Regions[RegionA].CurrentPopulation
        < PreferredDesiredByRegion[RegionA];
    const bool bPreferredB = PreferredDesiredByRegion.IsValidIndex(RegionB)
      && OutDemand.Regions[RegionB].CurrentPopulation
        < PreferredDesiredByRegion[RegionB];
    return bPreferredA != bPreferredB ? bPreferredA : A < B;
  });

  const int32 SourceNode = 0;
  const int32 SourceBase = 1;
  const int32 TerminalBase = SourceBase + SupplySourceCells.Num();
  const int32 RegionBase = TerminalBase + TerminalCells.Num();
  const int32 SinkNode = RegionBase + OutDemand.Regions.Num();
  TArray<TArray<FResidualEdge>> AdmissionGraph;
  AdmissionGraph.SetNum(SinkNode + 1);
  TArray<TArray<int32>> TopologyAdjacency;
  TopologyAdjacency.SetNum(Topology.Cells.Num());
  for (const FCrowdDemoTargetPolarEdge& Edge : Topology.Edges)
    if (TopologyAdjacency.IsValidIndex(Edge.FromCellKey)
      && TopologyAdjacency.IsValidIndex(Edge.ToCellKey))
      TopologyAdjacency[Edge.FromCellKey].Add(Edge.ToCellKey);
  for (TArray<int32>& Neighbors : TopologyAdjacency)
  {
    Neighbors.Sort();
    Neighbors.SetNum(Algo::Unique(Neighbors));
  }
  int32 AdmissionStableOrder = 0;
  for (int32 SourceIndex = 0; SourceIndex < SupplySourceCells.Num(); ++SourceIndex)
  {
    const int32 SourceCellKey = SupplySourceCells[SourceIndex];
    AddResidualEdge(AdmissionGraph, SourceNode, SourceBase + SourceIndex,
      SupplyCountByCell[SourceCellKey], 0, 0, AdmissionStableOrder++);
    TBitArray<> Visited(false, Topology.Cells.Num());
    TArray<int32> Queue;
    Visited[SourceCellKey] = true;
    Queue.Add(SourceCellKey);
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
      const int32 CellKey = Queue[Head];
      for (const int32 ToCellKey : TopologyAdjacency[CellKey])
      {
        if (!Visited.IsValidIndex(ToCellKey) || Visited[ToCellKey])
          continue;
        Visited[ToCellKey] = true;
        Queue.Add(ToCellKey);
      }
    }
    for (int32 TerminalIndex = 0; TerminalIndex < TerminalCells.Num(); ++TerminalIndex)
    {
      const int32 TerminalCellKey = TerminalCells[TerminalIndex];
      if (!Visited[TerminalCellKey]) continue;
      AddResidualEdge(AdmissionGraph, SourceBase + SourceIndex,
        TerminalBase + TerminalIndex,
        FMath::Min(SupplyCountByCell[SourceCellKey],
          RemainingCapacityByCell[TerminalCellKey]),
        0, 0, AdmissionStableOrder++, SourceCellKey, TerminalCellKey,
        true, false);
    }
  }
  TArray<int32> RemainingCapacityByRegion;
  RemainingCapacityByRegion.Init(0, OutDemand.Regions.Num());
  for (int32 TerminalIndex = 0; TerminalIndex < TerminalCells.Num(); ++TerminalIndex)
  {
    const int32 TerminalCellKey = TerminalCells[TerminalIndex];
    const int32 RegionKey =
      Topology.Cells[TerminalCellKey].PrimaryDemandRegionKey;
    if (!RemainingCapacityByRegion.IsValidIndex(RegionKey)) continue;
    AddResidualEdge(AdmissionGraph, TerminalBase + TerminalIndex,
      RegionBase + RegionKey, RemainingCapacityByCell[TerminalCellKey],
      0, 0, AdmissionStableOrder++);
    RemainingCapacityByRegion[RegionKey] +=
      RemainingCapacityByCell[TerminalCellKey];
  }
  for (int32 RegionKey = 0; RegionKey < OutDemand.Regions.Num(); ++RegionKey)
  {
    const int32 PreferredRemaining = FMath::Clamp(
      PreferredDesiredByRegion[RegionKey]
        - OutDemand.Regions[RegionKey].CurrentPopulation,
      0, RemainingCapacityByRegion[RegionKey]);
    if (PreferredRemaining > 0)
      AddResidualEdge(AdmissionGraph, RegionBase + RegionKey, SinkNode,
        PreferredRemaining, 0, 0, AdmissionStableOrder++);
    const int32 Spare = RemainingCapacityByRegion[RegionKey]
      - PreferredRemaining;
    if (Spare > 0)
      AddResidualEdge(AdmissionGraph, RegionBase + RegionKey, SinkNode,
        Spare, 1, 0, AdmissionStableOrder++);
  }
  int32 MatchedSupplyPopulation = 0;
  while (true)
  {
    TArray<int32> ParentNode, ParentEdge;
    ParentNode.Init(INDEX_NONE, AdmissionGraph.Num());
    ParentEdge.Init(INDEX_NONE, AdmissionGraph.Num());
    TArray<int32> Queue;
    Queue.Add(SourceNode);
    ParentNode[SourceNode] = SourceNode;
    for (int32 Head = 0;
      Head < Queue.Num() && ParentNode[SinkNode] == INDEX_NONE; ++Head)
    {
      const int32 From = Queue[Head];
      for (int32 EdgeIndex = 0;
        EdgeIndex < AdmissionGraph[From].Num(); ++EdgeIndex)
      {
        const FResidualEdge& Edge = AdmissionGraph[From][EdgeIndex];
        if (Edge.Capacity <= 0 || ParentNode[Edge.To] != INDEX_NONE)
          continue;
        ParentNode[Edge.To] = From;
        ParentEdge[Edge.To] = EdgeIndex;
        Queue.Add(Edge.To);
        if (Edge.To == SinkNode) break;
      }
    }
    if (ParentNode[SinkNode] == INDEX_NONE) break;
    int32 Augment = MAX_int32;
    for (int32 Node = SinkNode; Node != SourceNode;
      Node = ParentNode[Node])
      Augment = FMath::Min(Augment,
        AdmissionGraph[ParentNode[Node]][ParentEdge[Node]].Capacity);
    for (int32 Node = SinkNode; Node != SourceNode;
      Node = ParentNode[Node])
    {
      FResidualEdge& Edge =
        AdmissionGraph[ParentNode[Node]][ParentEdge[Node]];
      Edge.Capacity -= Augment;
      AdmissionGraph[Node][Edge.Reverse].Capacity += Augment;
    }
    MatchedSupplyPopulation += Augment;
  }
  TMap<int32, TArray<int32>> DestinationCellsBySource;
  for (int32 SourceIndex = 0; SourceIndex < SupplySourceCells.Num(); ++SourceIndex)
    for (const FResidualEdge& Edge : AdmissionGraph[SourceBase + SourceIndex])
    {
      if (!Edge.bTopologyForward || Edge.InitialCapacity <= 0) continue;
      const int32 Used = Edge.InitialCapacity - Edge.Capacity;
      for (int32 Unit = 0; Unit < Used; ++Unit)
        DestinationCellsBySource.FindOrAdd(Edge.FromCellKey).Add(
          Edge.ToCellKey);
    }
  for (auto& Pair : DestinationCellsBySource)
    Pair.Value.Sort();
  TMap<int32, int32> NextDestinationBySource;
  int32 AssignedSupplyPopulation = 0;
  for (FCrowdDemoTargetRegionAgentDemandState& State : OutDemand.AgentStates)
  {
    if (State.bCapacityAdmitted) continue;
    TArray<int32>* Destinations =
      DestinationCellsBySource.Find(State.CurrentCellKey);
    int32& NextDestination =
      NextDestinationBySource.FindOrAdd(State.CurrentCellKey);
    if (!Destinations || !Destinations->IsValidIndex(NextDestination))
    {
      State.bCapacityHold = true;
      continue;
    }
    const int32 DestinationCellKey = (*Destinations)[NextDestination++];
    const int32 AssignedRegion =
      Topology.Cells[DestinationCellKey].PrimaryDemandRegionKey;
    State.bCapacityAdmitted = true;
    State.bSupply = true;
    State.AssignedRegionKey = AssignedRegion;
    ++OutDemand.Regions[AssignedRegion].DesiredPopulation;
    ++OutDemand.SupplyAgentCount;
    ++AssignedSupplyPopulation;
  }
  if (AssignedSupplyPopulation != MatchedSupplyPopulation) return;
  OutDemand.AssignablePopulation =
    AdmittedTerminalPopulation + AssignedSupplyPopulation;
  OutDemand.OverflowPopulation =
    OutDemand.DesiredPopulationTotal - OutDemand.AssignablePopulation;
  for (FCrowdDemoTargetDemandRegion& Region : OutDemand.Regions)
  {
    Region.Deficit = Region.DesiredPopulation - Region.CurrentPopulation;
    OutDemand.TotalDeficit += Region.Deficit;
  }

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
    Hash = Fold(Hash, State.AssignedRegionKey);
    Hash = Fold(Hash, State.bTerminalStay ? 1 : 0);
    Hash = Fold(Hash, State.bSupply ? 1 : 0);
    Hash = Fold(Hash, State.bEngagedHold ? 1 : 0);
    Hash = Fold(Hash, State.bCapacityAdmitted ? 1 : 0);
    Hash = Fold(Hash, State.bCapacityHold ? 1 : 0);
  }
  Hash = Fold(Hash, OutDemand.DesiredPopulationTotal);
  Hash = Fold(Hash, OutDemand.TotalFeasibleCapacity);
  Hash = Fold(Hash, OutDemand.AssignablePopulation);
  Hash = Fold(Hash, OutDemand.OverflowPopulation);
  OutDemand.MembershipHash = MembershipHash;
  OutDemand.DemandHash = Hash;
  OutDemand.bValid = OutDemand.SourceAttachmentFailureCount == 0
    && OutDemand.DesiredPopulationTotal == Agents.Num()
    && OutDemand.AssignablePopulation + OutDemand.OverflowPopulation
      == OutDemand.DesiredPopulationTotal
    && OutDemand.TotalDeficit == OutDemand.SupplyAgentCount;
}

void FCrowdDemoTargetRegionTransportKernel::UpdateStaticDemandPopulation(
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputAgents,
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoSharedFlowFieldConfig& FlowConfig,
  const FCrowdDemoSharedFlowField* SharedFlowField,
  const FCrowdDemoTargetPolarTopology& Topology,
  FCrowdDemoTargetRegionDemandResult& InOutDemand,
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputExternalAgents,
  const bool bRefreshSourceAttachments)
{
  BuildDemand(InputAgents, Settings, FlowConfig, SharedFlowField, Topology,
    InOutDemand, InputExternalAgents);
  return;
}

void FCrowdDemoTargetRegionTransportKernel::SolveTransport(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan* PreviousPlan,
  const int32 PlanEpoch,
  const int32 FixedStepIndex,
  const int32 TargetRevision,
  FCrowdDemoTargetRegionFlowPlan& OutPlan,
  const TConstArrayView<FCrowdDemoTargetRegionQuotaAgentClaim> ReservedClaims)
{
  OutPlan = {};
  OutPlan.PlanEpoch = PlanEpoch;
  OutPlan.BuildFixedStepIndex = FixedStepIndex;
  OutPlan.TargetRevision = TargetRevision;
  OutPlan.FeasibleGraphHash = Topology.FeasibleGraphHash;
  OutPlan.EnvironmentHash = Topology.EnvironmentHash;
  OutPlan.MembershipHash = Demand.MembershipHash;
  OutPlan.ExternalPopulationHash = Demand.ExternalPopulationHash;
  OutPlan.TotalFeasibleCapacity = Demand.TotalFeasibleCapacity;
  OutPlan.AssignablePopulation = Demand.AssignablePopulation;
  OutPlan.OverflowPopulation = Demand.OverflowPopulation;
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
  TMap<int64, int32> FixedQuotaByEdge;
  TArray<FCrowdDemoTargetRegionQuotaAgentClaim> StableReservedClaims(ReservedClaims);
  StableReservedClaims.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  int32 PreviousReservedAgentId = INDEX_NONE;
  for (const auto& Claim : StableReservedClaims)
  {
    const auto* AgentState = Demand.AgentStates.FindByPredicate(
      [&Claim](const auto& State) { return State.AgentId == Claim.AgentId; });
    const auto* Edge = Topology.Edges.FindByPredicate([&Claim](const auto& Candidate)
    {
      return Candidate.FromCellKey == Claim.FromCellKey
        && Candidate.ToCellKey == Claim.ToCellKey;
    });
    if (Claim.AgentId == INDEX_NONE || Claim.AgentId <= PreviousReservedAgentId
      || !AgentState || !AgentState->bSupply
      || AgentState->CurrentCellKey != Claim.FromCellKey || !Edge
      || !SupplyByCell.IsValidIndex(Claim.FromCellKey)
      || !SupplyByCell.IsValidIndex(Claim.ToCellKey)
      || SupplyByCell[Claim.FromCellKey] <= 0)
      return;
    --SupplyByCell[Claim.FromCellKey];
    ++SupplyByCell[Claim.ToCellKey];
    ++FixedQuotaByEdge.FindOrAdd(
      StableEdgeKey(Claim.FromCellKey, Claim.ToCellKey));
    PreviousReservedAgentId = Claim.AgentId;
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
  {
    if (!Link.bTerminal || !Topology.Cells.IsValidIndex(Link.CellKey)
      || !Demand.Regions.IsValidIndex(Link.RegionKey)
      || Topology.Cells[Link.CellKey].PrimaryDemandRegionKey != Link.RegionKey
      || Demand.Regions[Link.RegionKey].Deficit <= 0)
      continue;
    const int32 Occupied =
      Demand.AdmittedPopulationByCell.IsValidIndex(Link.CellKey)
      ? Demand.AdmittedPopulationByCell[Link.CellKey] : 0;
    const int32 Available =
      Demand.AvailableCapacityByCell.IsValidIndex(Link.CellKey)
      ? FMath::Max(0,
          Demand.AvailableCapacityByCell[Link.CellKey] - Occupied)
      : 0;
    if (Available > 0)
      AddResidualEdge(Graph, Link.CellKey, CellCount + Link.RegionKey,
        FMath::Min(Available, Demand.Regions[Link.RegionKey].Deficit),
        0, 0, StableOrder++);
  }
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
    // Deterministic queue relaxation computes the same lexicographic shortest
    // augmenting path without rescanning every node and edge for V-1 passes.
    // Residual reverse arcs can be negative, so plain Dijkstra is not valid.
    TArray<int32> Queue;
    TArray<uint8> bQueued;
    TArray<int32> EnqueueCount;
    bQueued.Init(0, Graph.Num());
    EnqueueCount.Init(0, Graph.Num());
    Queue.Add(Source);
    bQueued[Source] = 1;
    EnqueueCount[Source] = 1;
    int32 QueueHead = 0;
    bool bNegativeCycle = false;
    while (QueueHead < Queue.Num() && !bNegativeCycle)
    {
      const int32 From = Queue[QueueHead++];
      bQueued[From] = 0;
      if (From == Sink || Distance[From].Physical >= MAX_int64 / 8)
        continue;
      for (int32 EdgeIndex = 0; EdgeIndex < Graph[From].Num(); ++EdgeIndex)
      {
        const auto& Edge = Graph[From][EdgeIndex];
        if (Edge.To == Source || Edge.Capacity <= 0) continue;
        const FCost Candidate = Add(
          Distance[From], Edge.PhysicalCost, Edge.ChangeCost);
        if (Less(Candidate, Distance[Edge.To]))
        {
          Distance[Edge.To] = Candidate;
          PreviousNode[Edge.To] = From;
          PreviousEdge[Edge.To] = EdgeIndex;
          if (!bQueued[Edge.To])
          {
            Queue.Add(Edge.To);
            bQueued[Edge.To] = 1;
            if (++EnqueueCount[Edge.To] > Graph.Num())
            {
              bNegativeCycle = true;
              break;
            }
          }
        }
      }
    }
    if (bNegativeCycle) return;
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
  TArray<int64> FixedEdgeKeys;
  FixedQuotaByEdge.GenerateKeyArray(FixedEdgeKeys);
  FixedEdgeKeys.Sort();
  for (const int64 Key : FixedEdgeKeys)
  {
    const int32 FixedQuota = FixedQuotaByEdge.FindRef(Key);
    const int32 FromCellKey = static_cast<int32>(Key >> 32);
    const int32 ToCellKey = static_cast<int32>(static_cast<uint32>(Key));
    auto& Result = FlowByKey.FindOrAdd(Key);
    Result.FromCellKey = FromCellKey;
    Result.ToCellKey = ToCellKey;
    Result.AgentQuota += FixedQuota;
    Result.ReusedQuota += FixedQuota;
    if (const auto* Edge = Topology.Edges.FindByPredicate(
      [FromCellKey, ToCellKey](const auto& Candidate)
      {
        return Candidate.FromCellKey == FromCellKey
          && Candidate.ToCellKey == ToCellKey;
      }))
    {
      TotalPhysical += static_cast<int64>(FixedQuota)
        * (Edge->GeometryCostCm + Edge->SoftClearancePenaltyCm
          + Edge->RadialDeviationPenaltyCm);
    }
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
  Hash = Fold(Hash, OutPlan.TotalFeasibleCapacity);
  Hash = Fold(Hash, OutPlan.AssignablePopulation);
  Hash = Fold(Hash, OutPlan.OverflowPopulation);
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
    || Plan.ExternalPopulationHash != Demand.ExternalPopulationHash
    || Plan.TotalFeasibleCapacity != Demand.TotalFeasibleCapacity
    || Plan.AssignablePopulation != Demand.AssignablePopulation
    || Plan.OverflowPopulation != Demand.OverflowPopulation)
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
  {
    if (!Link.bTerminal || !Demand.Regions.IsValidIndex(Link.RegionKey)
      || !Topology.Cells.IsValidIndex(Link.CellKey)
      || Topology.Cells[Link.CellKey].PrimaryDemandRegionKey != Link.RegionKey)
      continue;
    const int32 Occupied =
      Demand.AdmittedPopulationByCell.IsValidIndex(Link.CellKey)
      ? Demand.AdmittedPopulationByCell[Link.CellKey] : 0;
    const int32 Available =
      Demand.AvailableCapacityByCell.IsValidIndex(Link.CellKey)
      ? FMath::Max(0,
          Demand.AvailableCapacityByCell[Link.CellKey] - Occupied)
      : 0;
    TerminalDeficitCapacity[Link.CellKey] = FMath::Min(
      Available, Demand.Regions[Link.RegionKey].Deficit);
  }

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

void FCrowdDemoTargetRegionTransportKernel::InitializeQuotaExecutionState(
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  FCrowdDemoTargetRegionQuotaExecutionState& OutState)
{
  OutState = {};
  OutState.PlanEpoch = Plan.PlanEpoch;
  OutState.PlanTransportHash = Plan.TransportHash;
  for (const FCrowdDemoTargetPolarEdgeFlow& Flow : Plan.EdgeFlows)
  {
    FCrowdDemoTargetRegionQuotaEdgeState& Edge = OutState.Edges.AddDefaulted_GetRef();
    Edge.FromCellKey = Flow.FromCellKey;
    Edge.ToCellKey = Flow.ToCellKey;
    Edge.InitialQuota = Flow.AgentQuota;
  }
  OutState.Edges.Sort([](const auto& A, const auto& B)
  {
    return A.FromCellKey != B.FromCellKey ? A.FromCellKey < B.FromCellKey
      : A.ToCellKey < B.ToCellKey;
  });
  OutState.bValid = Plan.bValid;
  RefreshExecutionHash(OutState);
}

void FCrowdDemoTargetRegionTransportKernel::ReplacePlanPreservingClaims(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& PreviousPlan,
  const FCrowdDemoTargetRegionQuotaExecutionState& PreviousExecution,
  const int32 PlanEpoch,
  const int32 FixedStepIndex,
  const int32 TargetRevision,
  FCrowdDemoTargetRegionFlowPlan& OutPlan,
  FCrowdDemoTargetRegionQuotaExecutionState& OutExecution,
  FCrowdDemoTargetRegionPlanReplacementSummary& OutSummary)
{
  OutPlan = {};
  OutExecution = {};
  OutSummary = {};
  TArray<FCrowdDemoTargetRegionQuotaAgentClaim> StableClaims =
    PreviousExecution.ActiveClaims;
  StableClaims.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  OutSummary.PreviousClaimCount = StableClaims.Num();
  TArray<FCrowdDemoTargetRegionQuotaAgentClaim> EligibleClaims;
  const bool bPreviousContractValid = PreviousPlan.bValid
    && PreviousExecution.bValid
    && PreviousExecution.PlanEpoch == PreviousPlan.PlanEpoch
    && PreviousExecution.PlanTransportHash == PreviousPlan.TransportHash
    && PreviousPlan.TargetRevision == TargetRevision;
  int32 PreviousAgentId = INDEX_NONE;
  if (bPreviousContractValid)
  {
    for (const auto& Claim : StableClaims)
    {
      const auto* AgentState = Demand.AgentStates.FindByPredicate(
        [&Claim](const auto& State) { return State.AgentId == Claim.AgentId; });
      const auto* Edge = Topology.Edges.FindByPredicate([&Claim](const auto& Candidate)
      {
        return Candidate.FromCellKey == Claim.FromCellKey
          && Candidate.ToCellKey == Claim.ToCellKey;
      });
      if (Claim.AgentId == INDEX_NONE || Claim.AgentId <= PreviousAgentId
        || !AgentState || !Edge)
      {
        PreviousAgentId = Claim.AgentId;
        continue;
      }
      if (AgentState->CurrentCellKey == Claim.ToCellKey)
      {
        ++OutSummary.CompletedAtReplacementCount;
      }
      else if (AgentState->bSupply
        && AgentState->CurrentCellKey == Claim.FromCellKey)
      {
        EligibleClaims.Add(Claim);
      }
      PreviousAgentId = Claim.AgentId;
    }
  }
  OutSummary.GeometryEligibleClaimCount = EligibleClaims.Num();

  FCrowdDemoTargetRegionPlanValidationResult Validation;
  FCrowdDemoTargetRegionPlanValidationResult ExecutionValidation;
  while (true)
  {
    SolveTransport(Topology, Demand, PreviousPlan.bValid ? &PreviousPlan : nullptr,
      PlanEpoch, FixedStepIndex, TargetRevision, OutPlan, EligibleClaims);
    ValidatePlanForDemand(Topology, Demand, OutPlan, TargetRevision, Validation);
    InitializeQuotaExecutionState(OutPlan, OutExecution);
    OutExecution.CompletedTransitionCount =
      PreviousExecution.CompletedTransitionCount
        + OutSummary.CompletedAtReplacementCount;
    OutExecution.ActiveClaims = EligibleClaims;
    OutExecution.bValid = OutPlan.bValid && Validation.bValid;
    RefreshExecutionHash(OutExecution);
    ValidateQuotaExecutionState(Topology, Demand, OutPlan, OutExecution,
      TargetRevision, ExecutionValidation);
    if (OutPlan.bValid && Validation.bValid && ExecutionValidation.bValid)
      break;
    if (EligibleClaims.IsEmpty()) break;
    // Lowest AgentId claims have stable precedence. If the full frozen set is
    // infeasible for either the new plan or its capacity/quota execution,
    // release the highest AgentId and retry the complete atomic replacement.
    EligibleClaims.Pop(EAllowShrinking::No);
  }

  OutExecution.bValid = OutPlan.bValid && Validation.bValid
    && ExecutionValidation.bValid;
  RefreshExecutionHash(OutExecution);
  OutSummary.MigratedClaimCount = EligibleClaims.Num();
  OutSummary.ReleasedClaimCount = FMath::Max(0,
    OutSummary.PreviousClaimCount - OutSummary.MigratedClaimCount
      - OutSummary.CompletedAtReplacementCount);
  uint32 Hash = Fold(FnvOffset, 1);
  Hash = Fold(Hash, PreviousPlan.TransportHash);
  Hash = Fold(Hash, PreviousExecution.ExecutionHash);
  Hash = Fold(Hash, OutPlan.TransportHash);
  Hash = Fold(Hash, OutExecution.ExecutionHash);
  Hash = Fold(Hash, OutSummary.PreviousClaimCount);
  Hash = Fold(Hash, OutSummary.GeometryEligibleClaimCount);
  Hash = Fold(Hash, OutSummary.MigratedClaimCount);
  Hash = Fold(Hash, OutSummary.ReleasedClaimCount);
  Hash = Fold(Hash, OutSummary.CompletedAtReplacementCount);
  for (const auto& Claim : EligibleClaims)
  {
    Hash = Fold(Hash, Claim.AgentId);
    Hash = Fold(Hash, Claim.FromCellKey);
    Hash = Fold(Hash, Claim.ToCellKey);
  }
  OutSummary.ReplacementHash = Hash;
  OutSummary.bValid = OutPlan.bValid && OutExecution.bValid
    && Validation.bValid && ExecutionValidation.bValid;
}

void FCrowdDemoTargetRegionTransportKernel::ValidateQuotaExecutionState(
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  const FCrowdDemoTargetRegionQuotaExecutionState& State,
  const int32 TargetRevision,
  FCrowdDemoTargetRegionPlanValidationResult& OutValidation)
{
  OutValidation = {};
  auto PinCell = [&OutValidation](const int32 CellKey)
  {
    if (OutValidation.FirstFailureCellKey == INDEX_NONE
      || (CellKey != INDEX_NONE && CellKey < OutValidation.FirstFailureCellKey))
      OutValidation.FirstFailureCellKey = CellKey;
  };
  auto PinAgent = [&OutValidation](const int32 AgentId)
  {
    if (OutValidation.FirstFailureAgentId == INDEX_NONE
      || (AgentId != INDEX_NONE && AgentId < OutValidation.FirstFailureAgentId))
      OutValidation.FirstFailureAgentId = AgentId;
  };
  if (!Topology.bValid || !Demand.bValid || !Plan.bValid || !State.bValid
    || Plan.TargetRevision != TargetRevision
    || Plan.FeasibleGraphHash != Topology.FeasibleGraphHash
    || Plan.MembershipHash != Demand.MembershipHash
    || State.PlanEpoch != Plan.PlanEpoch
    || State.PlanTransportHash != Plan.TransportHash
    || State.Edges.Num() != Plan.EdgeFlows.Num())
  {
    ++OutValidation.InvalidCellCount;
  }

  TMap<int64, int32> EdgeIndexByKey;
  int32 PreviousFrom = INDEX_NONE;
  int32 PreviousTo = INDEX_NONE;
  for (int32 Index = 0; Index < State.Edges.Num(); ++Index)
  {
    const auto& EdgeState = State.Edges[Index];
    const bool bSorted = PreviousFrom == INDEX_NONE
      || EdgeState.FromCellKey > PreviousFrom
      || (EdgeState.FromCellKey == PreviousFrom && EdgeState.ToCellKey > PreviousTo);
    PreviousFrom = EdgeState.FromCellKey;
    PreviousTo = EdgeState.ToCellKey;
    const int64 Key = (static_cast<int64>(EdgeState.FromCellKey) << 32)
      | static_cast<uint32>(EdgeState.ToCellKey);
    const FCrowdDemoTargetPolarEdgeFlow* PlanEdge = Plan.EdgeFlows.FindByPredicate(
      [&EdgeState](const auto& Flow)
      {
        return Flow.FromCellKey == EdgeState.FromCellKey
          && Flow.ToCellKey == EdgeState.ToCellKey;
      });
    const FCrowdDemoTargetPolarEdge* TopologyEdge = Topology.Edges.FindByPredicate(
      [&EdgeState](const auto& Edge)
      {
        return Edge.FromCellKey == EdgeState.FromCellKey
          && Edge.ToCellKey == EdgeState.ToCellKey;
      });
    if (!bSorted || EdgeIndexByKey.Contains(Key) || !PlanEdge || !TopologyEdge
      || EdgeState.InitialQuota <= 0 || EdgeState.ConsumedQuota < 0
      || EdgeState.ConsumedQuota > EdgeState.InitialQuota
      || (PlanEdge && PlanEdge->AgentQuota != EdgeState.InitialQuota))
    {
      ++OutValidation.MissingEdgeCount;
      PinCell(EdgeState.FromCellKey);
    }
    EdgeIndexByKey.Add(Key, Index);
  }

  TMap<int32, const FCrowdDemoTargetRegionAgentDemandState*> DemandByAgent;
  for (const auto& AgentState : Demand.AgentStates)
    DemandByAgent.Add(AgentState.AgentId, &AgentState);
  TArray<int32> ReservedByEdge;
  ReservedByEdge.Init(0, State.Edges.Num());
  TArray<int32> PendingConsumedByEdge;
  PendingConsumedByEdge.Init(0, State.Edges.Num());
  TSet<int32> ClaimedAtSourceAgentIds;
  int32 PreviousAgentId = INDEX_NONE;
  for (const auto& Claim : State.ActiveClaims)
  {
    const int64 Key = (static_cast<int64>(Claim.FromCellKey) << 32)
      | static_cast<uint32>(Claim.ToCellKey);
    const int32* EdgeIndex = EdgeIndexByKey.Find(Key);
    const auto* const* AgentStatePtr = DemandByAgent.Find(Claim.AgentId);
    const auto* AgentState = AgentStatePtr ? *AgentStatePtr : nullptr;
    if (Claim.AgentId == INDEX_NONE || Claim.AgentId <= PreviousAgentId || !EdgeIndex
      || !AgentState
      || (AgentState->CurrentCellKey != Claim.FromCellKey
        && AgentState->CurrentCellKey != Claim.ToCellKey))
    {
      ++OutValidation.FlowConservationFailureCount;
      PinCell(Claim.FromCellKey);
      PinAgent(Claim.AgentId);
    }
    if (EdgeIndex && AgentState)
    {
      if (AgentState->CurrentCellKey == Claim.FromCellKey)
      {
        ++ReservedByEdge[*EdgeIndex];
        ClaimedAtSourceAgentIds.Add(Claim.AgentId);
      }
      else if (AgentState->CurrentCellKey == Claim.ToCellKey)
      {
        ++PendingConsumedByEdge[*EdgeIndex];
      }
    }
    PreviousAgentId = Claim.AgentId;
  }
  TArray<int32> AvailableByEdge;
  AvailableByEdge.SetNum(State.Edges.Num());
  for (int32 Index = 0; Index < State.Edges.Num(); ++Index)
  {
    if (State.Edges[Index].ConsumedQuota + PendingConsumedByEdge[Index]
      + ReservedByEdge[Index]
      > State.Edges[Index].InitialQuota)
    {
      ++OutValidation.InsufficientOutgoingQuotaCellCount;
      PinCell(State.Edges[Index].FromCellKey);
    }
    AvailableByEdge[Index] = State.Edges[Index].InitialQuota
      - State.Edges[Index].ConsumedQuota - PendingConsumedByEdge[Index]
      - ReservedByEdge[Index];
  }

  // A current supply without a preserved claim must be able to reserve an
  // outgoing unit before Guidance runs. This turns a stale short plan into an
  // atomic boundary rebuild instead of exposing one or more unrouted frames.
  TArray<FCrowdDemoTargetRegionAgentDemandState> StableDemandStates(Demand.AgentStates);
  StableDemandStates.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (const auto& AgentState : StableDemandStates)
  {
    if (!AgentState.bSupply || AgentState.bTerminalStay
      || ClaimedAtSourceAgentIds.Contains(AgentState.AgentId))
      continue;
    int32 ChosenEdgeIndex = INDEX_NONE;
    for (int32 EdgeIndex = 0; EdgeIndex < State.Edges.Num(); ++EdgeIndex)
    {
      if (State.Edges[EdgeIndex].FromCellKey == AgentState.CurrentCellKey
        && AvailableByEdge[EdgeIndex] > 0)
      {
        ChosenEdgeIndex = EdgeIndex;
        break;
      }
    }
    if (ChosenEdgeIndex == INDEX_NONE)
    {
      ++OutValidation.InsufficientOutgoingQuotaCellCount;
      PinCell(AgentState.CurrentCellKey);
      PinAgent(AgentState.AgentId);
    }
    else
    {
      --AvailableByEdge[ChosenEdgeIndex];
    }
  }

  TArray<int32> ClaimsByDestinationCell;
  ClaimsByDestinationCell.Init(0, Topology.Cells.Num());
  TArray<int32> AdmissionQuotaByCell;
  AdmissionQuotaByCell.Init(0, Topology.Cells.Num());
  for (const FCrowdDemoTargetRegionAgentDemandState& AgentState :
    Demand.AgentStates)
    if (AgentState.bSupply
      && AdmissionQuotaByCell.IsValidIndex(AgentState.CurrentCellKey))
      ++AdmissionQuotaByCell[AgentState.CurrentCellKey];
  for (int32 EdgeIndex = 0; EdgeIndex < State.Edges.Num(); ++EdgeIndex)
  {
    const FCrowdDemoTargetRegionQuotaEdgeState& Edge = State.Edges[EdgeIndex];
    const int32 RemainingQuota = FMath::Max(0,
      Edge.InitialQuota - Edge.ConsumedQuota
        - PendingConsumedByEdge[EdgeIndex]);
    if (AdmissionQuotaByCell.IsValidIndex(Edge.FromCellKey))
      AdmissionQuotaByCell[Edge.FromCellKey] -= RemainingQuota;
    if (AdmissionQuotaByCell.IsValidIndex(Edge.ToCellKey))
      AdmissionQuotaByCell[Edge.ToCellKey] += RemainingQuota;
  }
  for (const FCrowdDemoTargetRegionQuotaAgentClaim& Claim :
    State.ActiveClaims)
  {
    const auto* const* AgentStatePtr = DemandByAgent.Find(Claim.AgentId);
    const FCrowdDemoTargetRegionAgentDemandState* AgentState =
      AgentStatePtr ? *AgentStatePtr : nullptr;
    if (AgentState
      && ClaimsByDestinationCell.IsValidIndex(Claim.ToCellKey)
      && Topology.Cells[Claim.ToCellKey].bTerminal
      && Topology.Cells[Claim.ToCellKey].PrimaryDemandRegionKey
        == AgentState->AssignedRegionKey
      && ClaimsByDestinationCell[Claim.ToCellKey]
        < FMath::Max(0, AdmissionQuotaByCell[Claim.ToCellKey]))
      ++ClaimsByDestinationCell[Claim.ToCellKey];
  }
  for (int32 CellKey = 0; CellKey < Topology.Cells.Num(); ++CellKey)
  {
    if (!Topology.Cells[CellKey].bTerminal)
      continue;
    const int32 Occupied =
      Demand.AdmittedPopulationByCell.IsValidIndex(CellKey)
      ? Demand.AdmittedPopulationByCell[CellKey] : 0;
    const int32 AvailableCapacity =
      Demand.AvailableCapacityByCell.IsValidIndex(CellKey)
      ? Demand.AvailableCapacityByCell[CellKey] : 0;
    const int32 PlannedAdmissions = FMath::Max(
      ClaimsByDestinationCell[CellKey],
      FMath::Max(0, AdmissionQuotaByCell[CellKey]));
    if (Occupied + PlannedAdmissions > AvailableCapacity)
    {
      ++OutValidation.OverbookedCellCount;
      PinCell(CellKey);
    }
  }

  OutValidation.bValid = OutValidation.MissingEdgeCount == 0
    && OutValidation.InfeasibleEdgeCount == 0
    && OutValidation.InvalidCellCount == 0
    && OutValidation.InsufficientOutgoingQuotaCellCount == 0
    && OutValidation.FlowConservationFailureCount == 0
    && OutValidation.UnreachableDeficitCount == 0
    && OutValidation.OverbookedCellCount == 0;
  uint32 Hash = Fold(FnvOffset, 2);
  Hash = Fold(Hash, OutValidation.bValid ? 1 : 0);
  Hash = Fold(Hash, OutValidation.MissingEdgeCount);
  Hash = Fold(Hash, OutValidation.InvalidCellCount);
  Hash = Fold(Hash, OutValidation.InsufficientOutgoingQuotaCellCount);
  Hash = Fold(Hash, OutValidation.FlowConservationFailureCount);
  Hash = Fold(Hash, OutValidation.OverbookedCellCount);
  Hash = Fold(Hash, OutValidation.FirstFailureCellKey);
  Hash = Fold(Hash, OutValidation.FirstFailureAgentId);
  Hash = Fold(Hash, TargetRevision);
  Hash = Fold(Hash, Topology.FeasibleGraphHash);
  Hash = Fold(Hash, Demand.MembershipHash);
  Hash = Fold(Hash, Plan.TransportHash);
  Hash = Fold(Hash, State.ExecutionHash);
  OutValidation.ValidationHash = Hash;
}

void FCrowdDemoTargetRegionTransportKernel::BuildGuidanceWithExecution(
  const TConstArrayView<FCrowdDemoTargetRegionTransportAgent> InputAgents,
  const FCrowdDemoTargetRegionTransportSettings& Settings,
  const FCrowdDemoTargetPolarTopology& Topology,
  const FCrowdDemoTargetRegionDemandResult& Demand,
  const FCrowdDemoTargetRegionFlowPlan& Plan,
  FCrowdDemoTargetRegionQuotaExecutionState& InOutExecutionState,
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
  FCrowdDemoTargetRegionQuotaExecutionState WorkState = InOutExecutionState;
  if (!WorkState.bValid || WorkState.PlanEpoch != Plan.PlanEpoch
    || WorkState.PlanTransportHash != Plan.TransportHash)
    InitializeQuotaExecutionState(Plan, WorkState);
  TMap<int64, int32> EdgeIndexByKey;
  for (int32 Index = 0; Index < WorkState.Edges.Num(); ++Index)
  {
    const auto& Edge = WorkState.Edges[Index];
    const int64 Key = (static_cast<int64>(Edge.FromCellKey) << 32)
      | static_cast<uint32>(Edge.ToCellKey);
    EdgeIndexByKey.Add(Key, Index);
  }

  // Reconcile last boundary's transient edge claims with the new cell snapshot.
  // Crossing ToCell consumes one unit; remaining in FromCell preserves the claim.
  TArray<FCrowdDemoTargetRegionQuotaAgentClaim> ReconciledClaims;
  bool bExecutionValid = WorkState.bValid;
  WorkState.ActiveClaims.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  for (const auto& Claim : WorkState.ActiveClaims)
  {
    const auto* const* StatePtr = StateByAgent.Find(Claim.AgentId);
    const auto* State = StatePtr ? *StatePtr : nullptr;
    const int64 Key = (static_cast<int64>(Claim.FromCellKey) << 32)
      | static_cast<uint32>(Claim.ToCellKey);
    const int32* EdgeIndex = EdgeIndexByKey.Find(Key);
    if (!State || !EdgeIndex)
    {
      bExecutionValid = false;
      continue;
    }
    if (State->CurrentCellKey == Claim.ToCellKey)
    {
      auto& Edge = WorkState.Edges[*EdgeIndex];
      if (Edge.ConsumedQuota >= Edge.InitialQuota)
      {
        bExecutionValid = false;
        continue;
      }
      ++Edge.ConsumedQuota;
      ++WorkState.CompletedTransitionCount;
    }
    else if (State->CurrentCellKey == Claim.FromCellKey)
    {
      ReconciledClaims.Add(Claim);
    }
    else
    {
      bExecutionValid = false;
    }
  }
  WorkState.ActiveClaims = MoveTemp(ReconciledClaims);
  TMap<int32, int32> ClaimIndexByAgent;
  TArray<int32> ReservedByEdge;
  ReservedByEdge.Init(0, WorkState.Edges.Num());
  for (int32 ClaimIndex = 0; ClaimIndex < WorkState.ActiveClaims.Num(); ++ClaimIndex)
  {
    const auto& Claim = WorkState.ActiveClaims[ClaimIndex];
    ClaimIndexByAgent.Add(Claim.AgentId, ClaimIndex);
    const int64 Key = (static_cast<int64>(Claim.FromCellKey) << 32)
      | static_cast<uint32>(Claim.ToCellKey);
    if (const int32* EdgeIndex = EdgeIndexByKey.Find(Key))
      ++ReservedByEdge[*EdgeIndex];
  }
  TArray<int32> AvailableByEdge;
  AvailableByEdge.SetNum(WorkState.Edges.Num());
  for (int32 Index = 0; Index < WorkState.Edges.Num(); ++Index)
  {
    AvailableByEdge[Index] = WorkState.Edges[Index].InitialQuota
      - WorkState.Edges[Index].ConsumedQuota - ReservedByEdge[Index];
    if (AvailableByEdge[Index] < 0) bExecutionValid = false;
  }
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
      Result.DemandRegionKey = State->AssignedRegionKey != INDEX_NONE
        ? State->AssignedRegionKey : State->CurrentRegionKey;
      const float Distance = (Agent.Location - Settings.TargetLocation).Size();
      if (State->bCapacityHold)
      {
        Result.Mode = ECrowdDemoTargetRegionGuidanceMode::CapacityHold;
        Result.DesiredVelocity = FVector2f::ZeroVector;
        ++OutSummary.CapacityHoldAgentCount;
      }
      else if (Distance > Settings.MaximumCenterDistanceCm + Settings.InfluenceBlendWidthCm)
      {
        Result.Mode = ECrowdDemoTargetRegionGuidanceMode::FarFlow;
        Result.DesiredVelocity = Agent.FarFlowPreferredVelocity;
        ++OutSummary.FarFlowAgentCount;
      }
      else if (State->bEngagedHold)
      {
        // Suppress only proactive radial retreat. Tangential target motion and
        // target motion that would otherwise open the range remain followable.
        Result.Mode = ECrowdDemoTargetRegionGuidanceMode::EngagedHold;
        Result.DesiredVelocity = ComposeEngagedHoldVelocity(
          Agent.Location, Settings.TargetLocation, Settings.TargetVelocity,
          Agent.MaxSpeedCmps);
        ++OutSummary.EngagedHoldAgentCount;
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
        // Region demand is a shared occupancy fact, not a permanent agent slot.
        // Preserve that occupancy with a soft wedge-boundary barrier instead of
        // attracting every terminal agent to the same region-center point.
        if (Settings.DemandRegionCount > 0 && Distance > UE_SMALL_NUMBER)
        {
          float Angle = FMath::Atan2(Offset.Y, Offset.X);
          if (Angle < 0.0f) Angle += 2.0f * PI;
          const float RegionWidth = 2.0f * PI
            / static_cast<float>(Settings.DemandRegionCount);
          const float RegionStart = static_cast<float>(State->CurrentRegionKey) * RegionWidth;
          const float LocalAngle = FMath::Fmod(
            Angle - RegionStart + 2.0f * PI, 2.0f * PI);
          const float HardClearanceCm = Agent.PhysicalRadiusCm + Agent.HardSafetyGapCm;
          const float MarginAngle = FMath::Min(
            RegionWidth * 0.25f, HardClearanceCm / Distance);
          const FVector2f CounterClockwiseTangent(-Normal.Y, Normal.X);
          float AngularErrorCm = 0.0f;
          if (LocalAngle < MarginAngle)
            AngularErrorCm = (MarginAngle - LocalAngle) * Distance;
          else if (LocalAngle > RegionWidth - MarginAngle
            && LocalAngle < RegionWidth + UE_SMALL_NUMBER)
            AngularErrorCm = -(LocalAngle - (RegionWidth - MarginAngle)) * Distance;
          Relative += CounterClockwiseTangent * FMath::Clamp(
            AngularErrorCm * Settings.RadialGainPerSecond,
            -Settings.TransportSpeedCmps, Settings.TransportSpeedCmps);
          if (Relative.SizeSquared() > FMath::Square(Settings.TransportSpeedCmps))
            Relative = Relative.GetSafeNormal() * Settings.TransportSpeedCmps;
        }
        Result.DesiredVelocity = Settings.TargetVelocity + Relative;
        ++OutSummary.TerminalSettleAgentCount;
      }
      else
      {
        const TArray<const FCrowdDemoTargetPolarEdgeFlow*>* Flows = Outgoing.Find(State->CurrentCellKey);
        const FCrowdDemoTargetPolarEdgeFlow* Chosen = nullptr;
        if (const int32* ClaimIndex = ClaimIndexByAgent.Find(Agent.AgentId))
        {
          const auto& Claim = WorkState.ActiveClaims[*ClaimIndex];
          Chosen = StableFlows.FindByPredicate([&Claim](const auto& Flow)
          {
            return Flow.FromCellKey == Claim.FromCellKey
              && Flow.ToCellKey == Claim.ToCellKey;
          });
        }
        else if (Flows)
          for (const auto* Flow : *Flows)
          {
            const int64 Key = (static_cast<int64>(Flow->FromCellKey) << 32)
              | static_cast<uint32>(Flow->ToCellKey);
            const int32* EdgeIndex = EdgeIndexByKey.Find(Key);
            if (EdgeIndex && AvailableByEdge[*EdgeIndex] > 0)
            {
              --AvailableByEdge[*EdgeIndex];
              Chosen = Flow;
              FCrowdDemoTargetRegionQuotaAgentClaim& NewClaim =
                WorkState.ActiveClaims.AddDefaulted_GetRef();
              NewClaim.AgentId = Agent.AgentId;
              NewClaim.FromCellKey = Flow->FromCellKey;
              NewClaim.ToCellKey = Flow->ToCellKey;
              ClaimIndexByAgent.Add(Agent.AgentId, WorkState.ActiveClaims.Num() - 1);
              break;
            }
          }
        if (Chosen && Topology.Cells.IsValidIndex(Chosen->ToCellKey))
        {
          Result.Mode = ECrowdDemoTargetRegionGuidanceMode::Transport;
          Result.NextCellKey = Chosen->ToCellKey;
          const FCrowdDemoTargetPolarCell& ToCell = Topology.Cells[Chosen->ToCellKey];
          FVector2f Direction = (ToCell.WorldAnchorCm - Agent.Location).GetSafeNormal();
          // If the route already reaches a terminal cell in the agent's current
          // demand region, enter the distance band radially first. Chasing the
          // angular cell center can otherwise leave only a few cm/s of radial
          // progress at the round boundary.
          if (ToCell.bTerminal
            && ToCell.PrimaryDemandRegionKey == State->AssignedRegionKey)
          {
            const FVector2f Offset = Agent.Location - Settings.TargetLocation;
            if (Distance > Settings.MaximumCenterDistanceCm)
              Direction = -Offset.GetSafeNormal();
            else if (Distance < Settings.MinimumCenterDistanceCm)
              Direction = Offset.GetSafeNormal();
          }
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
  OutSummary.EngagedHoldAgentCount = 0;
  OutSummary.CapacityHoldAgentCount = 0;
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
    case ECrowdDemoTargetRegionGuidanceMode::EngagedHold:
      ++OutSummary.EngagedHoldAgentCount; break;
    case ECrowdDemoTargetRegionGuidanceMode::CapacityHold:
      ++OutSummary.CapacityHoldAgentCount; break;
    case ECrowdDemoTargetRegionGuidanceMode::Unrouted:
      ++OutSummary.UnroutedAgentCount;
      if (OutSummary.FirstUnroutedAgentId == INDEX_NONE)
      {
        OutSummary.FirstUnroutedAgentId = Result.AgentId;
        OutSummary.FirstUnroutedCellKey = Result.CurrentCellKey;
      }
      break;
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
  WorkState.ActiveClaims.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  TArray<int32> TerminalClaimsByCell;
  TerminalClaimsByCell.Init(0, Topology.Cells.Num());
  TArray<int32> AdmissionQuotaByCell;
  AdmissionQuotaByCell.Init(0, Topology.Cells.Num());
  for (const FCrowdDemoTargetRegionAgentDemandState& AgentState :
    Demand.AgentStates)
    if (AgentState.bSupply
      && AdmissionQuotaByCell.IsValidIndex(AgentState.CurrentCellKey))
      ++AdmissionQuotaByCell[AgentState.CurrentCellKey];
  for (const FCrowdDemoTargetRegionQuotaEdgeState& Edge : WorkState.Edges)
  {
    const int32 RemainingQuota = FMath::Max(
      0, Edge.InitialQuota - Edge.ConsumedQuota);
    if (AdmissionQuotaByCell.IsValidIndex(Edge.FromCellKey))
      AdmissionQuotaByCell[Edge.FromCellKey] -= RemainingQuota;
    if (AdmissionQuotaByCell.IsValidIndex(Edge.ToCellKey))
      AdmissionQuotaByCell[Edge.ToCellKey] += RemainingQuota;
  }
  for (const FCrowdDemoTargetRegionQuotaAgentClaim& Claim :
    WorkState.ActiveClaims)
  {
    const auto* const* AgentStatePtr = StateByAgent.Find(Claim.AgentId);
    const FCrowdDemoTargetRegionAgentDemandState* AgentState =
      AgentStatePtr ? *AgentStatePtr : nullptr;
    if (AgentState
      && TerminalClaimsByCell.IsValidIndex(Claim.ToCellKey)
      && Topology.Cells[Claim.ToCellKey].bTerminal
      && Topology.Cells[Claim.ToCellKey].PrimaryDemandRegionKey
        == AgentState->AssignedRegionKey
      && TerminalClaimsByCell[Claim.ToCellKey]
        < FMath::Max(0, AdmissionQuotaByCell[Claim.ToCellKey]))
      ++TerminalClaimsByCell[Claim.ToCellKey];
  }
  for (int32 CellKey = 0; CellKey < Topology.Cells.Num(); ++CellKey)
  {
    if (!Topology.Cells[CellKey].bTerminal)
      continue;
    const int32 Occupied =
      Demand.AdmittedPopulationByCell.IsValidIndex(CellKey)
      ? Demand.AdmittedPopulationByCell[CellKey] : 0;
    const int32 AvailableCapacity =
      Demand.AvailableCapacityByCell.IsValidIndex(CellKey)
      ? Demand.AvailableCapacityByCell[CellKey] : 0;
    const int32 PlannedAdmissions = FMath::Max(
      TerminalClaimsByCell[CellKey],
      FMath::Max(0, AdmissionQuotaByCell[CellKey]));
    if (Occupied + PlannedAdmissions > AvailableCapacity)
      bExecutionValid = false;
  }
  RefreshExecutionHash(WorkState);
  WorkState.bValid = bExecutionValid;
  for (const auto& Flow : StableFlows)
  {
    FCrowdDemoTargetRegionGuidanceConsumption Consumption;
    Consumption.FromCellKey = Flow.FromCellKey;
    Consumption.ToCellKey = Flow.ToCellKey;
    Consumption.AgentQuota = Flow.AgentQuota;
    const auto* EdgeState = WorkState.Edges.FindByPredicate([&Flow](const auto& Edge)
    {
      return Edge.FromCellKey == Flow.FromCellKey
        && Edge.ToCellKey == Flow.ToCellKey;
    });
    Consumption.ConsumedQuota = EdgeState ? EdgeState->ConsumedQuota : 0;
    OutSummary.Consumption.Add(Consumption);
    Hash = Fold(Hash, Consumption.FromCellKey);
    Hash = Fold(Hash, Consumption.ToCellKey);
    Hash = Fold(Hash, Consumption.AgentQuota);
    Hash = Fold(Hash, Consumption.ConsumedQuota);
  }
  Hash = Fold(Hash, OutSummary.FirstUnroutedAgentId);
  Hash = Fold(Hash, OutSummary.FirstUnroutedCellKey);
  Hash = Fold(Hash, WorkState.ExecutionHash);
  OutSummary.ExecutionHash = WorkState.ExecutionHash;
  OutSummary.GuidanceHash = Hash;
  OutSummary.bValid = Plan.bValid && WorkState.bValid
    && OutSummary.UnroutedAgentCount == 0;
  if (OutSummary.bValid)
    InOutExecutionState = MoveTemp(WorkState);
  else
    InOutExecutionState.bValid = false;
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
  FCrowdDemoTargetRegionQuotaExecutionState ExecutionState;
  InitializeQuotaExecutionState(Plan, ExecutionState);
  BuildGuidanceWithExecution(InputAgents, Settings, Topology, Demand, Plan,
    ExecutionState, OutResults, OutSummary);
}
