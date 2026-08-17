#include "CrowdDemoNavSurfaceGraphProbe.h"

#include "CrowdNavSurfaceGraph.h"
#include "Camera/CameraActor.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MassCrowdNavSurfaceGraphExtractor.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "Mass/CrowdDemoMassSubsystem.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ACrowdDemoNavSurfaceGraphProbe::ACrowdDemoNavSurfaceGraphProbe()
{
  PrimaryActorTick.bCanEverTick = true;
}

void ACrowdDemoNavSurfaceGraphProbe::BeginPlay()
{
  Super::BeginPlay();
  const UWorld* World = GetWorld();
  FirstAttemptWorldSeconds = World ? World->GetTimeSeconds() + 2.0 : 2.0;
  NextAttemptWorldSeconds = FirstAttemptWorldSeconds;
  bCaptureRequested = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoCaptureNavSurfaceGraph"));
  bProductSmall = FParse::Param(
    FCommandLine::Get(), TEXT("CrowdDemoNavFlowProductSmall"));
  if (bCaptureRequested) PrepareCaptureView();
}

void ACrowdDemoNavSurfaceGraphProbe::PrepareCaptureView()
{
  UWorld* World = GetWorld();
  APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
  if (!Controller) return;
  for (TActorIterator<ACameraActor> It(World); It; ++It)
  {
    if (It->ActorHasTag(TEXT("CrowdNavAcceptanceCamera")))
    {
      Controller->SetViewTarget(*It);
      return;
    }
  }
}

void ACrowdDemoNavSurfaceGraphProbe::Tick(const float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UWorld* World = GetWorld();
  if (!World) return;
  if (bFinished)
  {
    if (bCaptureRequested && !bCaptureCompleted && CaptureAtWorldSeconds > 0.0)
    {
      PrepareCaptureView();
      if (World->GetTimeSeconds() >= CaptureAtWorldSeconds)
      {
        const FString CapturePath = FPaths::Combine(
          FPaths::ProjectSavedDir(), TEXT("StageI_NavSurfaceGraph_Visual.png"));
        FScreenshotRequest::RequestScreenshot(CapturePath, false, false);
        bCaptureCompleted = true;
        UE_LOG(LogTemp, Display,
          TEXT("CrowdDemoNavSurfaceGraph stage=visual_capture_requested path=%s"),
          *CapturePath);
      }
    }
    return;
  }
  if (World->GetTimeSeconds() < NextAttemptWorldSeconds) return;
  if (TryValidate())
  {
    bFinished = true;
    return;
  }
  NextAttemptWorldSeconds = World->GetTimeSeconds() + 1.0;
  if (World->GetTimeSeconds() - FirstAttemptWorldSeconds > 15.0)
  {
    UE_LOG(LogTemp, Error,
      TEXT("VIOLATION CrowdDemoNavSurfaceGraph stage=timeout source=RecastSurfaceGraph"));
    bFinished = true;
  }
}

bool ACrowdDemoNavSurfaceGraphProbe::TryValidate()
{
  ++AttemptCount;
  UWorld* World = GetWorld();
  if (!World) return false;
  TArray<FCrowdNavSurfacePolygonInput> Polygons;
  FCrowdNavSurfaceExtractionDiagnostics Extraction;
  if (!FCrowdNavSurfaceGraphExtractor::ExtractStaticRecast(
    *World, 20000, Polygons, Extraction))
  {
    if (AttemptCount == 1 || AttemptCount % 5 == 0)
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoNavSurfaceGraph stage=extract_wait attempt=%d reason=%s nav_system=%d recast=%d tile_refs=%d skipped_tile_refs=%d tiles=%d polygons=%d source=RecastSurfaceGraph"),
        AttemptCount,
        *Extraction.FailureReason,
        Extraction.bNavigationSystemFound ? 1 : 0,
        Extraction.bRecastNavMeshFound ? 1 : 0,
        Extraction.TileRefCount,
        Extraction.SkippedTileRefCount,
        Extraction.TileCount,
        Extraction.PolygonCount);
    }
    return false;
  }

  FCrowdNavSurfaceGraphBuildConfig Config;
  Config.MinPortalWidthCm = 70;
  FCrowdNavSurfaceGraph Graph;
  if (!FCrowdNavSurfaceGraphKernel::Build(Polygons, Config, Graph))
  {
    if (AttemptCount == 1 || AttemptCount % 5 == 0)
    {
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoNavSurfaceGraph stage=graph_build_wait attempt=%d polygons=%d portals=%d tile_refs=%d skipped_tile_refs=%d source=RecastSurfaceGraph"),
        AttemptCount, Extraction.PolygonCount, Extraction.PortalCount,
        Extraction.TileRefCount, Extraction.SkippedTileRefCount);
    }
    return false;
  }
  if (AttemptCount == 1)
  {
    FBox GraphBounds(EForceInit::ForceInit);
    TSet<uint32> ExtractedGraphLayers;
    for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
    {
      GraphBounds += Node.Center;
      ExtractedGraphLayers.Add(Node.NavLayer);
    }
    UE_LOG(LogTemp, Display,
      TEXT("CrowdDemoNavSurfaceGraph stage=graph_ready nodes=%d polygons=%d portals=%d graph_layers=%d bounds_min=%s bounds_max=%s rejected_portals=%d rejected_missing=%d rejected_narrow=%d rejected_narrow_min_cm=%u rejected_narrow_max_cm=%u rejected_step=%d rejected_slope=%d source=RecastSurfaceGraph"),
      Graph.Nodes.Num(), Extraction.PolygonCount, Extraction.PortalCount,
      ExtractedGraphLayers.Num(), *GraphBounds.Min.ToCompactString(),
      *GraphBounds.Max.ToCompactString(), Graph.RejectedPortalCount,
      Graph.RejectedMissingNeighborCount, Graph.RejectedNarrowPortalCount,
      Graph.MinRejectedPortalWidthCm, Graph.MaxRejectedPortalWidthCm,
      Graph.RejectedStepPortalCount, Graph.RejectedSlopePortalCount);
  }

  TMap<FName, FVector> MarkerLocations;
  for (TActorIterator<ATargetPoint> It(World); It; ++It)
  {
    for (const FName Tag : It->Tags)
    {
      if (Tag.ToString().StartsWith(TEXT("CrowdNav")))
        MarkerLocations.Add(Tag, It->GetActorLocation());
    }
  }
  const FName RequiredMarkers[] = {
    TEXT("CrowdNavLower"), TEXT("CrowdNavUpper"), TEXT("CrowdNavRamp"),
    TEXT("CrowdNavHigh"), TEXT("CrowdNavNarrow"), TEXT("CrowdNavDropTop"),
    TEXT("CrowdNavDropBottom"), TEXT("CrowdNavRouteA"), TEXT("CrowdNavRouteB"),
    TEXT("CrowdNavGoal")};
  TMap<FName, int32> MarkerNodes;
  for (const FName Marker : RequiredMarkers)
  {
    const FVector* Location = MarkerLocations.Find(Marker);
    if (!Location)
    {
      if (AttemptCount == 1 || AttemptCount % 5 == 0)
        UE_LOG(LogTemp, Warning,
          TEXT("CrowdDemoNavSurfaceGraph stage=marker_wait attempt=%d marker=%s reason=missing source=RecastSurfaceGraph"),
          AttemptCount, *Marker.ToString());
      return false;
    }
    uint64 AttachedNodeId = 0;
    uint32 AttachedLayer = 0;
    const int32 NodeIndex = FCrowdNavSurfaceGraphKernel::AttachClosest(
      Graph, *Location, 350.0f, AttachedNodeId, AttachedLayer)
        ? Graph.FindNodeIndex(AttachedNodeId) : INDEX_NONE;
    if (NodeIndex == INDEX_NONE)
    {
      if (AttemptCount == 1 || AttemptCount % 5 == 0)
      {
        int32 ClosestIndex = INDEX_NONE;
        double ClosestDistance = TNumericLimits<double>::Max();
        for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
        {
          const double Distance = FVector::Distance(Graph.Nodes[Index].Center, *Location);
          if (Distance < ClosestDistance)
          {
            ClosestDistance = Distance;
            ClosestIndex = Index;
          }
        }
        UE_LOG(LogTemp, Warning,
          TEXT("CrowdDemoNavSurfaceGraph stage=marker_wait attempt=%d marker=%s reason=no_near_node location=%s nodes=%d closest_distance=%.1f closest_location=%s source=RecastSurfaceGraph"),
          AttemptCount, *Marker.ToString(), *Location->ToCompactString(), Graph.Nodes.Num(),
          ClosestDistance, ClosestIndex == INDEX_NONE
            ? TEXT("none") : *Graph.Nodes[ClosestIndex].Center.ToCompactString());
      }
      return false;
    }
    MarkerNodes.Add(Marker, NodeIndex);
  }

  TSet<uint32> GraphLayers;
  int32 DirectedEdgeCount = 0;
  int32 SlopedEdgeCount = 0;
  uint32 MinimumWidthCm = MAX_uint32;
  for (const FCrowdNavSurfaceNode& Node : Graph.Nodes)
  {
    GraphLayers.Add(Node.NavLayer);
    for (const FCrowdNavSurfaceEdge& Edge : Node.Edges)
    {
      ++DirectedEdgeCount;
      if (Edge.SlopeMilliDegrees >= 3000) ++SlopedEdgeCount;
      MinimumWidthCm = FMath::Min(MinimumWidthCm, Edge.WidthCm);
    }
  }

  int32 LayeredOverlapCount = 0;
  for (int32 A = 0; A < Graph.Nodes.Num(); ++A)
  {
    for (int32 B = A + 1; B < Graph.Nodes.Num(); ++B)
    {
      const FVector Delta = Graph.Nodes[A].Center - Graph.Nodes[B].Center;
      if (FVector2D(Delta.X, Delta.Y).Size() <= 120.0
        && FMath::Abs(Delta.Z) >= 200.0
        && Graph.Nodes[A].NavLayer != Graph.Nodes[B].NavLayer)
        ++LayeredOverlapCount;
    }
  }

  const int32 GoalIndex = MarkerNodes.FindChecked(TEXT("CrowdNavGoal"));
  FCrowdNavSurfaceFlow GoalFlow;
  if (!FCrowdNavSurfaceGraphKernel::BuildFlow(
    Graph, Graph.Nodes[GoalIndex].StableNodeId, 1, GoalFlow))
  {
    if (AttemptCount == 1 || AttemptCount % 5 == 0)
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoNavSurfaceGraph stage=goal_flow_wait attempt=%d nodes=%d topology_hash=%llu source=RecastSurfaceGraph"),
        AttemptCount, Graph.Nodes.Num(), Graph.TopologyHash);
    return false;
  }
  const FName ReachableMarkers[] = {
    TEXT("CrowdNavLower"), TEXT("CrowdNavUpper"), TEXT("CrowdNavRamp"),
    TEXT("CrowdNavHigh"), TEXT("CrowdNavNarrow"),
    TEXT("CrowdNavDropBottom"), TEXT("CrowdNavRouteA"), TEXT("CrowdNavRouteB")};
  int32 ReachableMarkerCount = 0;
  FString UnreachableMarkers;
  for (const FName Marker : ReachableMarkers)
  {
    if (GoalFlow.Nodes[MarkerNodes.FindChecked(Marker)].IntegrationCostQ < MAX_uint32)
      ++ReachableMarkerCount;
    else
      UnreachableMarkers += (UnreachableMarkers.IsEmpty() ? TEXT("") : TEXT(","))
        + Marker.ToString();
  }
  const bool bDropTopUnreachable =
    GoalFlow.Nodes[MarkerNodes.FindChecked(TEXT("CrowdNavDropTop"))]
      .IntegrationCostQ == MAX_uint32;
  int32 ReachableSlopedEdgeCount = 0;
  for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
  {
    if (GoalFlow.Nodes[NodeIndex].IntegrationCostQ == MAX_uint32) continue;
    for (const FCrowdNavSurfaceEdge& Edge : Graph.Nodes[NodeIndex].Edges)
    {
      const int32 ToIndex = Graph.FindNodeIndex(Edge.ToStableNodeId);
      if (Edge.SlopeMilliDegrees >= 3000 && ToIndex != INDEX_NONE
        && GoalFlow.Nodes[ToIndex].IntegrationCostQ < MAX_uint32)
        ++ReachableSlopedEdgeCount;
    }
  }

  FCrowdNavSurfaceFlow ReboundFlow;
  const int32 RouteAIndex = MarkerNodes.FindChecked(TEXT("CrowdNavRouteA"));
  if (!FCrowdNavSurfaceGraphKernel::BuildFlow(
    Graph, Graph.Nodes[RouteAIndex].StableNodeId, 2, ReboundFlow))
  {
    if (AttemptCount == 1 || AttemptCount % 5 == 0)
      UE_LOG(LogTemp, Warning,
        TEXT("CrowdDemoNavSurfaceGraph stage=rebound_flow_wait attempt=%d nodes=%d topology_hash=%llu source=RecastSurfaceGraph"),
        AttemptCount, Graph.Nodes.Num(), Graph.TopologyHash);
    return false;
  }
  bool bProductRuntimeValid = !bProductSmall;
  FCrowdNavFlowCacheMetrics ProductMetrics;
  if (bProductSmall)
  {
    UMassCrowdRuntimeSubsystem* Runtime =
      World->GetSubsystem<UMassCrowdRuntimeSubsystem>();
    UCrowdDemoMassSubsystem* Mass =
      World->GetSubsystem<UCrowdDemoMassSubsystem>();
    if (!Runtime || !Mass)
      return false;
    if (!Runtime->GetNavGraphResource().IsReady())
    {
      Runtime->SetGraphBuildConfig(Config);
      if (!Runtime->BuildOrRefreshNavGraph())
        return false;
    }
    const FCrowdNavGraphResource& Resource =
      Runtime->GetNavGraphResource();
    FCrowdNavFlowKey GoalKey;
    GoalKey.TopologyRevision = Resource.TopologyRevision;
    GoalKey.ObjectiveAttachment =
      Graph.Nodes[GoalIndex].StableNodeId;
    GoalKey.MovementProfileKey = 1;
    GoalKey.NavLayer = Graph.Nodes[GoalIndex].NavLayer;
    FCrowdNavFlowKey ReboundKey = GoalKey;
    ReboundKey.ObjectiveAttachment =
      Graph.Nodes[RouteAIndex].StableNodeId;
    FCrowdNavFlowHandle GoalHandle;
    FCrowdNavFlowHandle GoalSharedHandle;
    FCrowdNavFlowHandle ReboundHandle;
    const bool bAcquired =
      Runtime->AcquireFlow(GoalKey, GoalHandle)
      && Runtime->AcquireFlow(GoalKey, GoalSharedHandle)
      && Runtime->AcquireFlow(ReboundKey, ReboundHandle);
    const auto ProductGoal = Runtime->ResolveFlow(GoalHandle);
    const auto ProductRebound = Runtime->ResolveFlow(ReboundHandle);
    ProductMetrics = Runtime->GetFlowCacheMetrics();
    bProductRuntimeValid = bAcquired
      && GoalHandle == GoalSharedHandle
      && ProductGoal.IsValid() && ProductRebound.IsValid()
      && Resource.TopologyHash == Graph.TopologyHash
      && ProductGoal->IntegrationHash == GoalFlow.IntegrationHash
      && ProductRebound->IntegrationHash == ReboundFlow.IntegrationHash
      && ProductMetrics.ResourceCount == 2
      && ProductMetrics.ReferencedResourceCount == 2
      && ProductMetrics.HitCount >= 1
      && Mass->GetTrackedAgentCount() == 20
      && Mass->GetAliveAgentCount() == 20;
    if (GoalHandle.IsValid()) Runtime->ReleaseFlow(GoalHandle);
    if (GoalSharedHandle.IsValid())
      Runtime->ReleaseFlow(GoalSharedHandle);
    if (ReboundHandle.IsValid())
      Runtime->ReleaseFlow(ReboundHandle);
  }
  const bool bPassed = Graph.Nodes.Num() >= 20
    && DirectedEdgeCount >= Graph.Nodes.Num()
    && GraphLayers.Num() >= 2
    && LayeredOverlapCount > 0
    && SlopedEdgeCount > 0
    && MinimumWidthCm < 500
    && MarkerNodes.FindChecked(TEXT("CrowdNavLower"))
      != MarkerNodes.FindChecked(TEXT("CrowdNavUpper"))
    && ReachableMarkerCount == UE_ARRAY_COUNT(ReachableMarkers)
    && ReachableSlopedEdgeCount > 0
    && bDropTopUnreachable
    && ReboundFlow.TopologyHash == GoalFlow.TopologyHash
    && ReboundFlow.IntegrationHash != GoalFlow.IntegrationHash
    && bProductRuntimeValid;
  const FString Evidence = FString::Printf(
    TEXT("%s CrowdDemoNavSurfaceGraph stage=validation nodes=%d directed_edges=%d tiles=%d extracted_layers=%d graph_layers=%d overlap=%d sloped_edges=%d reachable_sloped_edges=%d min_width_cm=%u rejected_portals=%d reachable_markers=%d unreachable_markers=%s drop_unreachable=%d topology_hash=%llu goal_hash=%llu rebound_hash=%llu product_small=%d flow_resources=%d referenced=%d cache_hits=%llu cache_misses=%llu cache_bytes=%llu source=RecastSurfaceGraph"),
    bPassed ? TEXT("PASS") : TEXT("VIOLATION"),
    Graph.Nodes.Num(),
    DirectedEdgeCount,
    Extraction.TileCount,
    Extraction.LayerCount,
    GraphLayers.Num(),
    LayeredOverlapCount,
    SlopedEdgeCount,
    ReachableSlopedEdgeCount,
    MinimumWidthCm,
    Graph.RejectedPortalCount,
    ReachableMarkerCount,
    UnreachableMarkers.IsEmpty() ? TEXT("none") : *UnreachableMarkers,
    bDropTopUnreachable ? 1 : 0,
    Graph.TopologyHash,
    GoalFlow.IntegrationHash,
    ReboundFlow.IntegrationHash,
    bProductSmall ? 1 : 0,
    ProductMetrics.ResourceCount,
    ProductMetrics.ReferencedResourceCount,
    static_cast<unsigned long long>(ProductMetrics.HitCount),
    static_cast<unsigned long long>(ProductMetrics.MissCount),
    static_cast<unsigned long long>(ProductMetrics.Bytes));
  if (bPassed)
  {
    UE_LOG(LogTemp, Display, TEXT("%s"), *Evidence);
    if (bCaptureRequested && GetNetMode() != NM_DedicatedServer)
    {
      PrepareCaptureView();
      CaptureAtWorldSeconds = GetWorld()->GetTimeSeconds() + 1.0;
    }
  }
  else
  {
    UE_LOG(LogTemp, Error, TEXT("%s"), *Evidence);
  }
  return bPassed;
}
