#pragma once

#include "CoreMinimal.h"

struct FCrowdNavSurfacePortalInput
{
  uint64 ToSourcePolygonKey = 0;
  FVector Left = FVector::ZeroVector;
  FVector Right = FVector::ZeroVector;
};

struct FCrowdNavSurfacePolygonInput
{
  uint64 SourcePolygonKey = 0;
  uint32 NavLayerHint = 0;
  FVector Center = FVector::ZeroVector;
  FVector SurfaceNormal = FVector::UpVector;
  uint32 AreaCostQ = 100;
  TArray<FVector> Vertices;
  TArray<FCrowdNavSurfacePortalInput> Portals;
};

struct FCrowdNavSurfaceGraphBuildConfig
{
  int32 MaxNodes = 65536;
  int32 MaxVerticesPerPolygon = 12;
  int32 MaxPortalsPerPolygon = 32;
  uint32 MinPortalWidthCm = 80;
  uint32 MaxPortalStepHeightCm = 200;
  uint32 MaxSlopeMilliDegrees = 45000;
  uint32 SlopeCostPerDegreeQ = 5;
  uint32 MaxEdgeCostQ = 1000000;
};

struct FCrowdNavSurfaceEdge
{
  uint64 ToStableNodeId = 0;
  uint32 WidthCm = 0;
  uint32 SlopeMilliDegrees = 0;
  uint32 CostQ = 0;
};

struct FCrowdNavSurfaceNode
{
  uint64 StableNodeId = 0;
  uint32 NavLayer = 0;
  FVector Center = FVector::ZeroVector;
  FVector SurfaceNormal = FVector::UpVector;
  TArray<FVector> Vertices;
  TArray<FCrowdNavSurfaceEdge> Edges;
};

struct MASSCROWDCORE_API FCrowdNavSurfaceGraph
{
  TArray<FCrowdNavSurfaceNode> Nodes;
  uint64 TopologyHash = 0;
  int32 RejectedPortalCount = 0;
  int32 RejectedMissingNeighborCount = 0;
  int32 RejectedNarrowPortalCount = 0;
  uint32 MinRejectedPortalWidthCm = MAX_uint32;
  uint32 MaxRejectedPortalWidthCm = 0;
  int32 RejectedStepPortalCount = 0;
  int32 RejectedSlopePortalCount = 0;

  void Reset();
  bool IsValid() const;
  int32 FindNodeIndex(uint64 StableNodeId) const;
};

struct FCrowdNavSurfaceFlowNode
{
  uint64 StableNodeId = 0;
  uint32 IntegrationCostQ = MAX_uint32;
  uint64 NextStableNodeId = 0;
  FVector Direction = FVector::ZeroVector;
};

struct MASSCROWDCORE_API FCrowdNavSurfaceFlow
{
  uint64 TopologyHash = 0;
  uint64 GoalStableNodeId = 0;
  uint32 Revision = 0;
  uint64 IntegrationHash = 0;
  TArray<FCrowdNavSurfaceFlowNode> Nodes;

  bool IsValid() const;
};

class MASSCROWDCORE_API FCrowdNavSurfaceGraphKernel
{
public:
  static bool Build(
    TConstArrayView<FCrowdNavSurfacePolygonInput> Polygons,
    const FCrowdNavSurfaceGraphBuildConfig& Config,
    FCrowdNavSurfaceGraph& OutGraph);

  static bool BuildFlow(
    const FCrowdNavSurfaceGraph& Graph,
    uint64 GoalStableNodeId,
    uint32 Revision,
    FCrowdNavSurfaceFlow& OutFlow);

  static bool Attach(
    const FCrowdNavSurfaceGraph& Graph,
    const FVector& Location,
    uint32 NavLayer,
    float MaxDistanceCm,
    uint64& OutStableNodeId);

  static bool AttachClosest(
    const FCrowdNavSurfaceGraph& Graph,
    const FVector& Location,
    float MaxDistanceCm,
    uint64& OutStableNodeId,
    uint32& OutNavLayer);
};
