#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MassCrowdNavRuntime.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdNavSurfacePolygonInput MakeRuntimePolygon(
    const uint64 Key, const FVector& Center)
  {
    FCrowdNavSurfacePolygonInput Polygon;
    Polygon.SourcePolygonKey = Key;
    Polygon.NavLayerHint = 1;
    Polygon.Center = Center;
    Polygon.Vertices = {
      Center + FVector(-40.0, -40.0, 0.0),
      Center + FVector(40.0, -40.0, 0.0),
      Center + FVector(40.0, 40.0, 0.0),
      Center + FVector(-40.0, 40.0, 0.0)};
    return Polygon;
  }

  FCrowdNavSurfaceGraph MakeRuntimeGraph()
  {
    TArray<FCrowdNavSurfacePolygonInput> Polygons;
    Polygons.Add(MakeRuntimePolygon(10, FVector(0.0, 0.0, 0.0)));
    Polygons.Add(MakeRuntimePolygon(20, FVector(200.0, 0.0, 0.0)));
    Polygons.Add(MakeRuntimePolygon(30, FVector(400.0, 0.0, 0.0)));
    Polygons[0].Portals.Add({
      20, FVector(100.0, -60.0, 0.0), FVector(100.0, 60.0, 0.0)});
    Polygons[1].Portals.Add({
      10, FVector(100.0, 60.0, 0.0), FVector(100.0, -60.0, 0.0)});
    Polygons[1].Portals.Add({
      30, FVector(300.0, -60.0, 0.0), FVector(300.0, 60.0, 0.0)});
    Polygons[2].Portals.Add({
      20, FVector(300.0, 60.0, 0.0), FVector(300.0, -60.0, 0.0)});
    FCrowdNavSurfaceGraph Graph;
    FCrowdNavSurfaceGraphKernel::Build(Polygons, {}, Graph);
    return Graph;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdNavRuntimeFlowCacheTest,
  "MassCrowd.Runtime.NavRuntime.FlowCache",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdNavRuntimeFlowCacheTest::RunTest(
  const FString& Parameters)
{
  const FCrowdNavSurfaceGraph Graph = MakeRuntimeGraph();
  TestTrue(TEXT("fixture graph valid"), Graph.IsValid());
  FCrowdNavFlowCache Cache({1, 1024 * 1024});
  const FCrowdNavFlowKey FirstKey{
    1, Graph.Nodes[0].StableNodeId, 7, 1};
  const FCrowdNavFlowKey SecondKey{
    1, Graph.Nodes[2].StableNodeId, 7, 1};

  FCrowdNavFlowHandle First;
  TestTrue(TEXT("first flow acquired"), Cache.Acquire(FirstKey, Graph, First));
  FCrowdNavFlowHandle Shared;
  TestTrue(TEXT("same key shares flow"), Cache.Acquire(FirstKey, Graph, Shared));
  TestEqual(TEXT("shared acquire returns same handle"), Shared, First);
  TestEqual(TEXT("cache hit counted"), Cache.GetMetrics().HitCount, 1ull);
  TestTrue(TEXT("first release succeeds"), Cache.Release(First));
  TestTrue(TEXT("second release succeeds"), Cache.Release(Shared));

  FCrowdNavFlowHandle Second;
  TestTrue(TEXT("unreferenced LRU can be evicted"),
    Cache.Acquire(SecondKey, Graph, Second));
  TestEqual(TEXT("cache remains bounded"), Cache.GetMetrics().ResourceCount, 1);
  TestEqual(TEXT("eviction counted"), Cache.GetMetrics().EvictionCount, 1ull);
  TestFalse(TEXT("evicted handle no longer resolves"),
    Cache.Resolve(First).IsValid());
  TestTrue(TEXT("current handle resolves"), Cache.Resolve(Second).IsValid());

  FCrowdNavFlowHandle Blocked;
  TestFalse(TEXT("referenced resource is never evicted"),
    Cache.Acquire(FirstKey, Graph, Blocked));
  TestEqual(TEXT("budget rejection counted"),
    Cache.GetMetrics().RejectedAcquireCount, 1ull);
  TestTrue(TEXT("release current handle"), Cache.Release(Second));
  return true;
}

#endif
