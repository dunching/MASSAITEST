#pragma once

#include "CoreMinimal.h"

struct FCrowdSharedFlowObstacleSpec
{
  int32 ObstacleId = 0;
  FVector Center = FVector::ZeroVector;
  FVector Extent = FVector::ZeroVector;
};

struct FCrowdSharedFlowFieldConfig
{
  int32 Revision = 0;
  FVector BoundsMin = FVector(-3000.0f, -3400.0f, 0.0f);
  FVector BoundsMax = FVector(3000.0f, 2200.0f, 0.0f);
  float CellSizeCm = 100.0f;
  float AgentInflateCm = 48.0f;
  int32 ConnectivityContractVersion = 0;
  FVector GoalLocation = FVector(2200.0f, 1600.0f, 60.0f);
  TArray<FCrowdSharedFlowObstacleSpec> ObstacleSpecs;
};

enum class ECrowdFlowLocationStatus : uint8
{
  Reachable,
  OutOfBounds,
  BlockedRasterCell,
  UnreachableFreeCell
};

enum class ECrowdNavigationNodeKind : uint8
{
  CenterAnchor,
  VerticalEdgeConnection,
  HorizontalEdgeConnection
};

struct FCrowdNavigationSafeInterval
{
  ECrowdNavigationNodeKind Kind = ECrowdNavigationNodeKind::CenterAnchor;
  int32 PrimaryCellKey = INDEX_NONE;
  int32 SecondaryCellKey = INDEX_NONE;
  int32 IntervalOrdinal = INDEX_NONE;
  int32 QuantizedMinCm = 0;
  int32 QuantizedMaxCm = 0;
};

struct FCrowdNavigationNode
{
  uint64 StableNodeKey = 0;
  ECrowdNavigationNodeKind Kind = ECrowdNavigationNodeKind::CenterAnchor;
  int32 PrimaryCellKey = INDEX_NONE;
  int32 SecondaryCellKey = INDEX_NONE;
  int32 IntervalOrdinal = INDEX_NONE;
  FIntPoint QuantizedLocationCm = FIntPoint::ZeroValue;
};

struct FCrowdNavigationEdge
{
  uint64 MinNodeKey = 0;
  uint64 MaxNodeKey = 0;
  int32 QuantizedCost = MAX_int32;
};

struct FCrowdSharedFlowSample
{
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  ECrowdFlowLocationStatus Status = ECrowdFlowLocationStatus::OutOfBounds;
  FVector FlowDirection = FVector::ZeroVector;
  int32 IntegrationCost = MAX_int32;
  uint64 NavigationNodeKey = 0;
  uint64 NextNavigationNodeKey = 0;
  float GuidanceDistanceCm = 0.0f;
  bool bBlocked = false;
  bool bUnreachable = true;
  bool bRecoveredFromRasterMismatch = false;
  bool bSourceAttached = false;
};

struct FCrowdReachableFlowCellSearchResult
{
  bool bFound = false;
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  int32 RingDistance = MAX_int32;
  float WorldDistanceCm = MAX_flt;
  int32 IntegrationCost = MAX_int32;
  FVector CellCenter = FVector::ZeroVector;
  FVector FlowDirection = FVector::ZeroVector;
};

struct FCrowdSharedFlowConstraintResult
{
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bHitObstacle = false;
  bool bPenetrating = false;
  bool bHitFlowBounds = false;
  float FlowBoundsReprojectDeltaCm = 0.0f;
};

struct FCrowdSharedFlowConstraintDiagnostic
{
  bool bValid = false;
  FVector Start = FVector::ZeroVector;
  FVector Proposed = FVector::ZeroVector;
  FVector DomainProposed = FVector::ZeroVector;
  FVector SlideX = FVector::ZeroVector;
  FVector SlideY = FVector::ZeroVector;
  float FlowBoundsReprojectDeltaCm = 0.0f;
  bool bHitFlowBounds = false;
  bool bStartInsideAnyObstacle = false;
  bool bEndInsideAnyObstacle = false;
  bool bDirectSegmentClear = false;
  bool bSlideXClear = false;
  bool bSlideYClear = false;
  int32 SelectedObstacleId = INDEX_NONE;
  FVector SelectedInflatedMin = FVector::ZeroVector;
  FVector SelectedInflatedMax = FVector::ZeroVector;
  float SelectedSegmentEntryT = -1.0f;
  float SelectedSegmentExitT = -1.0f;
  bool bStartInsideSelectedObstacle = false;
  bool bEndInsideSelectedObstacle = false;
  TArray<int32> IntersectedObstacleIds;
  uint32 StableHash = 2166136261u;
};

struct MASSCROWDCORE_API FCrowdSharedFlowField
{
  FCrowdSharedFlowFieldConfig Config;
  TArray<int32> IntegrationCost;
  TArray<FVector> FlowDirection;
  TArray<int32> NextCellIndex;
  TBitArray<> Blocked;
  TBitArray<> Unreachable;
  TArray<FCrowdNavigationSafeInterval> NavigationSafeIntervals;
  TArray<FCrowdNavigationNode> NavigationNodes;
  // Stable topology cache used by runtime attachment sampling. Each entry is
  // sorted by NavigationNodes index (and therefore StableNodeKey).
  TArray<TArray<int32>> NavigationCellNodes;
  TArray<FCrowdNavigationEdge> NavigationEdges;
  TArray<int32> NavigationIntegrationCost;
  TArray<int32> NavigationNextNodeIndex;
  TArray<int32> GoalAttachmentNodeIndices;
  int32 Width = 0;
  int32 Height = 0;
  int32 GoalCellIndex = INDEX_NONE;
  int32 BlockedCellCount = 0;
  int32 ValidDirectedEdgeCount = 0;
  int32 NavigationCenterAnchorCount = 0;
  int32 NavigationConnectionPointCount = 0;
  int32 NavigationSafeIntervalCount = 0;
  int32 NavigationInternalEdgeCount = 0;
  int32 CenterInvalidButConnectedCellCount = 0;
  int32 GoalAttachmentCount = 0;
  uint32 TopologyHash = 0;
  uint32 IntegrationHash = 0;
  uint32 BuildHash = 0;

  void Reset();
  bool IsValid() const;
  int32 LocationToCellIndex(const FVector& Location) const;
  FVector CellCenter(int32 CellIndex) const;
};

namespace UE::MassCrowd::Private
{
  inline bool SharedFlowSegmentIntersectsBox2D(
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
        if (Origin >= AxisMin && Origin <= AxisMax) continue;
        return false;
      }
      const float InvDirection = 1.0f / Direction;
      float Enter = (AxisMin - Origin) * InvDirection;
      float Exit = (AxisMax - Origin) * InvDirection;
      if (Enter > Exit) Swap(Enter, Exit);
      TMin = FMath::Max(TMin, Enter);
      TMax = FMath::Min(TMax, Exit);
      if (TMin > TMax) return false;
    }
    return TMax >= 0.0f && TMin <= 1.0f;
  }

  template <typename ConfigType>
  bool SharedFlowIsInsideInflatedObstacle(
    const ConfigType& Config,
    const FVector& Location)
  {
    for (const auto& Obstacle : Config.ObstacleSpecs)
    {
      const FVector Center = FVector(Obstacle.Center);
      const FVector Extent = FVector(Obstacle.Extent);
      if (FMath::Abs(Location.X - Center.X) <= Extent.X + Config.AgentInflateCm
        && FMath::Abs(Location.Y - Center.Y) <= Extent.Y + Config.AgentInflateCm)
        return true;
    }
    return false;
  }

  template <typename ConfigType>
  bool SharedFlowIsSegmentClear(
    const ConfigType& Config,
    const FVector& Start,
    const FVector& End)
  {
    for (const auto& Obstacle : Config.ObstacleSpecs)
    {
      const FVector Inflate(Config.AgentInflateCm, Config.AgentInflateCm, 0.0f);
      const FVector Min = FVector(Obstacle.Center) - FVector(Obstacle.Extent) - Inflate;
      const FVector Max = FVector(Obstacle.Center) + FVector(Obstacle.Extent) + Inflate;
      if (SharedFlowSegmentIntersectsBox2D(Start, End, Min, Max)) return false;
    }
    return true;
  }

  template <typename ConfigType>
  bool SharedFlowIsInsideContractBounds(
    const ConfigType& Config,
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

class MASSCROWDCORE_API FCrowdSharedFlowFieldKernel
{
public:
  static FCrowdSharedFlowFieldConfig MakeSf1Config(int32 Revision = 1);
  static bool BuildTopology(
    const FCrowdSharedFlowFieldConfig& Config,
    FCrowdSharedFlowField& OutField);
  static bool Build(const FCrowdSharedFlowFieldConfig& Config, FCrowdSharedFlowField& OutField);
  static bool ResolveGoalAnchor(
    const FCrowdSharedFlowField& Field,
    const FVector& TargetLocation,
    int32& OutAnchorCellKey,
    FVector& OutAnchorLocation);
  static bool BuildIntegrationForAnchor(
    int32 AnchorCellKey,
    const FVector& AnchorLocation,
    FCrowdSharedFlowField& InOutField);
  static FCrowdSharedFlowSample Sample(const FCrowdSharedFlowField& Field, const FVector& Location);
  static FCrowdReachableFlowCellSearchResult FindNearestReachableCell(
    const FCrowdSharedFlowField& Field,
    const FVector& Location,
    int32 MaximumRingDistance = 8);
  static bool CanTraverseCellEdge(
    const FCrowdSharedFlowField& Field,
    int32 FromCellIndex,
    int32 ToCellIndex);
  static bool CanTraverseWorldSegment(
    const FCrowdSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& End);
  template <typename ConfigType>
  static bool CanTraverseWorldSegmentCompatible(
    const ConfigType& Config,
    const FVector& Start,
    const FVector& End)
  {
    return UE::MassCrowd::Private::SharedFlowIsInsideContractBounds(Config, Start)
      && UE::MassCrowd::Private::SharedFlowIsInsideContractBounds(Config, End)
      && UE::MassCrowd::Private::SharedFlowIsSegmentClear(Config, Start, End);
  }
  static FCrowdSharedFlowConstraintResult ConstrainMovement(
    const FCrowdSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    float FixedStepSeconds,
    bool bConstrainToFlowBounds = false);
  template <typename ConfigType>
  static FCrowdSharedFlowConstraintResult ConstrainMovementCompatible(
    const ConfigType& Config,
    const FVector& Start,
    const FVector& Proposed,
    const float FixedStepSeconds,
    const bool bConstrainToFlowBounds = false)
  {
    FCrowdSharedFlowConstraintResult Result;
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
    Result.Velocity = FixedStepSeconds > SMALL_NUMBER
      ? (DomainProposed - Start) / FixedStepSeconds : FVector::ZeroVector;
    Result.bPenetrating =
      UE::MassCrowd::Private::SharedFlowIsInsideInflatedObstacle(Config, Start);
    if (UE::MassCrowd::Private::SharedFlowIsSegmentClear(Config, Start, DomainProposed)
      && !UE::MassCrowd::Private::SharedFlowIsInsideInflatedObstacle(Config, DomainProposed))
      return Result;

    Result.bHitObstacle = true;
    const FVector SlideX(DomainProposed.X, Start.Y, Start.Z);
    const FVector SlideY(Start.X, DomainProposed.Y, Start.Z);
    const bool bSlideX = UE::MassCrowd::Private::SharedFlowIsSegmentClear(
        Config, Start, SlideX)
      && !UE::MassCrowd::Private::SharedFlowIsInsideInflatedObstacle(Config, SlideX);
    const bool bSlideY = UE::MassCrowd::Private::SharedFlowIsSegmentClear(
        Config, Start, SlideY)
      && !UE::MassCrowd::Private::SharedFlowIsInsideInflatedObstacle(Config, SlideY);
    if (bSlideX || bSlideY)
    {
      const float XProgressSq = bSlideX ? FVector::DistSquared2D(Start, SlideX) : -1.0f;
      const float YProgressSq = bSlideY ? FVector::DistSquared2D(Start, SlideY) : -1.0f;
      Result.Location = XProgressSq >= YProgressSq ? SlideX : SlideY;
      Result.Velocity = FixedStepSeconds > SMALL_NUMBER
        ? (Result.Location - Start) / FixedStepSeconds : FVector::ZeroVector;
      return Result;
    }
    Result.Location = Start;
    Result.Velocity = FVector::ZeroVector;
    return Result;
  }
  static FCrowdSharedFlowConstraintDiagnostic DiagnoseMovementConstraint(
    const FCrowdSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    bool bConstrainToFlowBounds = false);
  static bool IsInsideInflatedObstacle(const FCrowdSharedFlowFieldConfig& Config, const FVector& Location);
  template <typename ConfigType>
  static bool IsInsideInflatedObstacleCompatible(
    const ConfigType& Config,
    const FVector& Location)
  {
    return UE::MassCrowd::Private::SharedFlowIsInsideInflatedObstacle(Config, Location);
  }
};
