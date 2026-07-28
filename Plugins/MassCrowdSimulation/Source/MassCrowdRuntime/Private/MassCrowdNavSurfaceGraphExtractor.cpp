#include "MassCrowdNavSurfaceGraphExtractor.h"

#include "AI/NavigationSystemBase.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

bool FCrowdNavSurfaceGraphExtractor::ExtractStaticRecast(
  UWorld& World,
  const int32 MaxPolygons,
  TArray<FCrowdNavSurfacePolygonInput>& OutPolygons,
  FCrowdNavSurfaceExtractionDiagnostics& OutDiagnostics)
{
  OutPolygons.Reset();
  OutDiagnostics = {};
  if (MaxPolygons <= 0)
  {
    OutDiagnostics.FailureReason = TEXT("InvalidMaxPolygons");
    return false;
  }
#if WITH_RECAST
  UNavigationSystemV1* NavigationSystem =
    FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
  OutDiagnostics.bNavigationSystemFound = NavigationSystem != nullptr;
  const ARecastNavMesh* Recast = NavigationSystem
    ? Cast<ARecastNavMesh>(NavigationSystem->GetDefaultNavDataInstance(
      FNavigationSystem::DontCreate))
    : nullptr;
  OutDiagnostics.bRecastNavMeshFound = Recast != nullptr;
  if (!Recast)
  {
    OutDiagnostics.FailureReason = TEXT("MissingRecastNavMesh");
    return false;
  }

  TSet<uint32> Layers;
  TArray<FNavTileRef> TileRefs;
  Recast->GetAllNavMeshTiles(TileRefs);
  OutDiagnostics.TileRefCount = TileRefs.Num();
  for (const FNavTileRef TileRef : TileRefs)
  {
      if (!TileRef.IsValid()) continue;
      int32 RefX = 0;
      int32 RefY = 0;
      int32 Layer = 0;
      if (!Recast->GetNavMeshTileXY(TileRef, RefX, RefY, Layer))
      {
        ++OutDiagnostics.SkippedTileRefCount;
        continue;
      }
      Layers.Add(static_cast<uint32>(Layer + 1));
      TArray<FNavPoly> Polys;
      if (!Recast->GetPolysInTile(TileRef, Polys)) continue;
      ++OutDiagnostics.TileCount;
      for (const FNavPoly& Poly : Polys)
      {
        if (OutPolygons.Num() >= MaxPolygons)
        {
          OutDiagnostics.FailureReason = TEXT("PolygonLimitExceeded");
          return false;
        }
        TArray<FVector> Vertices;
        TArray<FNavigationPortalEdge> Neighbors;
        if (!Recast->GetPolyVerts(Poly.Ref, Vertices)
          || Vertices.Num() < 3
          || !Recast->GetPolyNeighbors(Poly.Ref, Neighbors)) continue;
        FCrowdNavSurfacePolygonInput& Input = OutPolygons.AddDefaulted_GetRef();
        Input.SourcePolygonKey = static_cast<uint64>(Poly.Ref);
        Input.NavLayerHint = static_cast<uint32>(Layer + 1);
        Input.Center = Poly.Center;
        Input.Vertices = MoveTemp(Vertices);
        FVector Normal = FVector::ZeroVector;
        for (int32 Index = 0; Index < Input.Vertices.Num(); ++Index)
        {
          const FVector& A = Input.Vertices[Index];
          const FVector& B = Input.Vertices[(Index + 1) % Input.Vertices.Num()];
          Normal.X += (A.Y - B.Y) * (A.Z + B.Z);
          Normal.Y += (A.Z - B.Z) * (A.X + B.X);
          Normal.Z += (A.X - B.X) * (A.Y + B.Y);
        }
        Input.SurfaceNormal = Normal.GetSafeNormal();
        if (Input.SurfaceNormal.Z < 0.0) Input.SurfaceNormal *= -1.0;
        Input.AreaCostQ = 100;
        for (const FNavigationPortalEdge& Neighbor : Neighbors)
        {
          if (Neighbor.ToRef == INVALID_NAVNODEREF) continue;
          Input.Portals.Add({
            static_cast<uint64>(Neighbor.ToRef), Neighbor.Left, Neighbor.Right});
          ++OutDiagnostics.PortalCount;
        }
      }
  }
  OutDiagnostics.PolygonCount = OutPolygons.Num();
  OutDiagnostics.LayerCount = Layers.Num();
  if (OutPolygons.IsEmpty())
  {
    OutDiagnostics.FailureReason = TEXT("NoPolygons");
    return false;
  }
  return true;
#else
  OutDiagnostics.FailureReason = TEXT("RecastDisabledAtBuild");
  return false;
#endif
}
