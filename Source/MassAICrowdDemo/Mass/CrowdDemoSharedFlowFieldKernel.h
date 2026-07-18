#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

enum class ECrowdDemoFlowLocationStatus : uint8
{
  Reachable,
  OutOfBounds,
  BlockedRasterCell,
  UnreachableFreeCell
};

enum class ECrowdDemoNavigationNodeKind : uint8
{
  CenterAnchor,
  VerticalEdgeConnection,
  HorizontalEdgeConnection
};

struct FCrowdDemoNavigationSafeInterval
{
  ECrowdDemoNavigationNodeKind Kind = ECrowdDemoNavigationNodeKind::CenterAnchor;
  int32 PrimaryCellKey = INDEX_NONE;
  int32 SecondaryCellKey = INDEX_NONE;
  int32 IntervalOrdinal = INDEX_NONE;
  int32 QuantizedMinCm = 0;
  int32 QuantizedMaxCm = 0;
};

struct FCrowdDemoNavigationNode
{
  uint64 StableNodeKey = 0;
  ECrowdDemoNavigationNodeKind Kind = ECrowdDemoNavigationNodeKind::CenterAnchor;
  int32 PrimaryCellKey = INDEX_NONE;
  int32 SecondaryCellKey = INDEX_NONE;
  int32 IntervalOrdinal = INDEX_NONE;
  FIntPoint QuantizedLocationCm = FIntPoint::ZeroValue;
};

struct FCrowdDemoNavigationEdge
{
  uint64 MinNodeKey = 0;
  uint64 MaxNodeKey = 0;
  int32 QuantizedCost = MAX_int32;
};

struct FCrowdDemoSharedFlowSample
{
  int32 CellIndex = INDEX_NONE;
  int32 StableCellKey = INDEX_NONE;
  ECrowdDemoFlowLocationStatus Status = ECrowdDemoFlowLocationStatus::OutOfBounds;
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

struct FCrowdDemoReachableFlowCellSearchResult
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

struct FCrowdDemoSharedFlowConstraintResult
{
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bHitObstacle = false;
  bool bPenetrating = false;
  bool bHitFlowBounds = false;
  float FlowBoundsReprojectDeltaCm = 0.0f;
};

struct FCrowdDemoSharedFlowConstraintDiagnostic
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

struct FCrowdDemoSharedFlowField
{
  FCrowdDemoSharedFlowFieldConfig Config;
  TArray<int32> IntegrationCost;
  TArray<FVector> FlowDirection;
  TArray<int32> NextCellIndex;
  TBitArray<> Blocked;
  TBitArray<> Unreachable;
  TArray<FCrowdDemoNavigationSafeInterval> NavigationSafeIntervals;
  TArray<FCrowdDemoNavigationNode> NavigationNodes;
  // Stable topology cache used by runtime attachment sampling. Each entry is
  // sorted by NavigationNodes index (and therefore StableNodeKey).
  TArray<TArray<int32>> NavigationCellNodes;
  TArray<FCrowdDemoNavigationEdge> NavigationEdges;
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

class MASSAICROWDDEMO_API FCrowdDemoSharedFlowFieldKernel
{
public:
  static FCrowdDemoSharedFlowFieldConfig MakeSf1Config(int32 Revision = 1);
  static bool BuildTopology(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    FCrowdDemoSharedFlowField& OutField);
  static bool Build(const FCrowdDemoSharedFlowFieldConfig& Config, FCrowdDemoSharedFlowField& OutField);
  static bool ResolveGoalAnchor(
    const FCrowdDemoSharedFlowField& Field,
    const FVector& TargetLocation,
    int32& OutAnchorCellKey,
    FVector& OutAnchorLocation);
  static bool BuildIntegrationForAnchor(
    int32 AnchorCellKey,
    const FVector& AnchorLocation,
    FCrowdDemoSharedFlowField& InOutField);
  static FCrowdDemoSharedFlowSample Sample(const FCrowdDemoSharedFlowField& Field, const FVector& Location);
  static FCrowdDemoReachableFlowCellSearchResult FindNearestReachableCell(
    const FCrowdDemoSharedFlowField& Field,
    const FVector& Location,
    int32 MaximumRingDistance = 8);
  static bool CanTraverseCellEdge(
    const FCrowdDemoSharedFlowField& Field,
    int32 FromCellIndex,
    int32 ToCellIndex);
  static bool CanTraverseWorldSegment(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& End);
  static FCrowdDemoSharedFlowConstraintResult ConstrainMovement(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    float FixedStepSeconds,
    bool bConstrainToFlowBounds = false);
  static FCrowdDemoSharedFlowConstraintDiagnostic DiagnoseMovementConstraint(
    const FCrowdDemoSharedFlowFieldConfig& Config,
    const FVector& Start,
    const FVector& Proposed,
    bool bConstrainToFlowBounds = false);
  static bool IsInsideInflatedObstacle(const FCrowdDemoSharedFlowFieldConfig& Config, const FVector& Location);
};
