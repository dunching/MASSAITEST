#pragma once

#include "CoreMinimal.h"
#include "CrowdNavSurfaceGraph.h"

class UWorld;

struct FCrowdNavSurfaceExtractionDiagnostics
{
  FString FailureReason;
  bool bNavigationSystemFound = false;
  bool bRecastNavMeshFound = false;
  int32 TileRefCount = 0;
  int32 SkippedTileRefCount = 0;
  int32 TileCount = 0;
  int32 PolygonCount = 0;
  int32 PortalCount = 0;
  int32 LayerCount = 0;
};

class MASSCROWDRUNTIME_API FCrowdNavSurfaceGraphExtractor
{
public:
  static bool ExtractStaticRecast(
    UWorld& World,
    int32 MaxPolygons,
    TArray<FCrowdNavSurfacePolygonInput>& OutPolygons,
    FCrowdNavSurfaceExtractionDiagnostics& OutDiagnostics);
};
