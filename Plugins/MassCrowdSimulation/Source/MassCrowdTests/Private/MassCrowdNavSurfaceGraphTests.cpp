#include "CrowdNavSurfaceGraph.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdNavSurfacePolygonInput MakePolygon(
    const uint64 SourceKey,
    const uint32 Layer,
    const FVector& Center,
    const FVector& Normal = FVector::UpVector)
  {
    FCrowdNavSurfacePolygonInput Polygon;
    Polygon.SourcePolygonKey = SourceKey;
    Polygon.NavLayerHint = Layer;
    Polygon.Center = Center;
    Polygon.SurfaceNormal = Normal;
    Polygon.Vertices = {
      Center + FVector(-40.0, -40.0, 0.0),
      Center + FVector(40.0, -40.0, 0.0),
      Center + FVector(40.0, 40.0, 0.0),
      Center + FVector(-40.0, 40.0, 0.0)};
    return Polygon;
  }

  void AddPortal(
    FCrowdNavSurfacePolygonInput& From,
    const uint64 To,
    const float WidthCm)
  {
    From.Portals.Add({
      To,
      From.Center + FVector(0.0, -WidthCm * 0.5, 0.0),
      From.Center + FVector(0.0, WidthCm * 0.5, 0.0)});
  }

  TArray<FCrowdNavSurfacePolygonInput> MakeLayeredGraphInput()
  {
    TArray<FCrowdNavSurfacePolygonInput> Polygons;
    Polygons.Add(MakePolygon(1, 1, FVector(0.0, 0.0, 0.0)));
    Polygons.Add(MakePolygon(2, 1, FVector(200.0, 0.0, 0.0)));
    Polygons.Add(MakePolygon(3, 1, FVector(400.0, 0.0, 0.0)));
    Polygons.Add(MakePolygon(4, 2, FVector(0.0, 0.0, 300.0)));
    Polygons.Add(MakePolygon(
      5, 2, FVector(200.0, 0.0, 200.0), FVector(-0.4472136, 0.0, 0.8944272)));
    Polygons.Add(MakePolygon(6, 2, FVector(400.0, 0.0, 300.0)));
    Polygons.Add(MakePolygon(7, 2, FVector(200.0, 200.0, 300.0)));
    Polygons.Add(MakePolygon(8, 3, FVector(0.0, 400.0, 800.0)));

    AddPortal(Polygons[0], 2, 120.0f);
    AddPortal(Polygons[1], 1, 120.0f);
    AddPortal(Polygons[1], 3, 120.0f);
    AddPortal(Polygons[2], 2, 120.0f);
    AddPortal(Polygons[3], 5, 120.0f);
    AddPortal(Polygons[4], 4, 120.0f);
    AddPortal(Polygons[4], 6, 120.0f);
    AddPortal(Polygons[5], 5, 120.0f);
    AddPortal(Polygons[3], 7, 60.0f);
    AddPortal(Polygons[6], 4, 60.0f);
    AddPortal(Polygons[0], 8, 120.0f);
    AddPortal(Polygons[7], 1, 120.0f);
    return Polygons;
  }

  int32 FindByCenterAndLayer(
    const FCrowdNavSurfaceGraph& Graph,
    const FVector& Center,
    const uint32 Layer)
  {
    return Graph.Nodes.IndexOfByPredicate([&](const FCrowdNavSurfaceNode& Node)
    {
      return Node.NavLayer == Layer && Node.Center.Equals(Center, 0.1);
    });
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdNavSurfaceGraphDeterminismTest,
  "MassCrowd.Core.NavSurfaceGraph.DeterminismAndLayering",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdNavSurfaceGraphDeterminismTest::RunTest(const FString& Parameters)
{
  const FCrowdNavSurfaceGraphBuildConfig Config;
  TArray<FCrowdNavSurfacePolygonInput> Input = MakeLayeredGraphInput();
  FCrowdNavSurfaceGraph Forward;
  TestTrue(TEXT("layered surface graph builds"),
    FCrowdNavSurfaceGraphKernel::Build(Input, Config, Forward));
  TestEqual(TEXT("all polygons become stable nodes"), Forward.Nodes.Num(), 8);
  TestTrue(TEXT("narrow and drop portals are rejected"),
    Forward.RejectedPortalCount >= 4);

  Algo::Reverse(Input);
  for (FCrowdNavSurfacePolygonInput& Polygon : Input) Algo::Reverse(Polygon.Portals);
  FCrowdNavSurfaceGraph Reordered;
  TestTrue(TEXT("reordered graph builds"),
    FCrowdNavSurfaceGraphKernel::Build(Input, Config, Reordered));
  TestEqual(TEXT("topology hash ignores extraction order"),
    Reordered.TopologyHash, Forward.TopologyHash);

  uint64 LowerAttachment = 0;
  uint64 UpperAttachment = 0;
  TestTrue(TEXT("lower overlap attaches to lower layer"),
    FCrowdNavSurfaceGraphKernel::Attach(
      Forward, FVector(0.0, 0.0, 5.0), 1, 400.0f, LowerAttachment));
  TestTrue(TEXT("upper overlap attaches to upper layer"),
    FCrowdNavSurfaceGraphKernel::Attach(
      Forward, FVector(0.0, 0.0, 295.0), 2, 400.0f, UpperAttachment));
  TestNotEqual(TEXT("XY-overlap layers keep distinct nodes"),
    LowerAttachment, UpperAttachment);
  uint64 ClosestAttachment = 0;
  uint32 ClosestLayer = 0;
  TestTrue(TEXT("closest attachment selects the nearest 3D surface layer"),
    FCrowdNavSurfaceGraphKernel::AttachClosest(
      Forward, FVector(0.0, 0.0, 295.0), 400.0f,
      ClosestAttachment, ClosestLayer));
  TestEqual(TEXT("closest attachment returns upper layer"), ClosestLayer, 2u);
  TestEqual(TEXT("closest attachment returns upper node"),
    ClosestAttachment, UpperAttachment);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdNavSurfaceSharedFlowTest,
  "MassCrowd.Core.NavSurfaceGraph.SharedFlow",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdNavSurfaceSharedFlowTest::RunTest(const FString& Parameters)
{
  const TArray<FCrowdNavSurfacePolygonInput> Input = MakeLayeredGraphInput();
  FCrowdNavSurfaceGraph Graph;
  TestTrue(TEXT("surface graph builds"),
    FCrowdNavSurfaceGraphKernel::Build(Input, {}, Graph));
  const int32 UpperStart = FindByCenterAndLayer(
    Graph, FVector(0.0, 0.0, 300.0), 2);
  const int32 UpperRamp = FindByCenterAndLayer(
    Graph, FVector(200.0, 0.0, 200.0), 2);
  const int32 UpperGoal = FindByCenterAndLayer(
    Graph, FVector(400.0, 0.0, 300.0), 2);
  const int32 LowerStart = FindByCenterAndLayer(
    Graph, FVector(0.0, 0.0, 0.0), 1);
  TestTrue(TEXT("required nodes found"),
    UpperStart != INDEX_NONE && UpperRamp != INDEX_NONE
      && UpperGoal != INDEX_NONE && LowerStart != INDEX_NONE);

  FCrowdNavSurfaceFlow GoalAtHighEnd;
  TestTrue(TEXT("high-layer flow builds"),
    FCrowdNavSurfaceGraphKernel::BuildFlow(
      Graph, Graph.Nodes[UpperGoal].StableNodeId, 1, GoalAtHighEnd));
  TestTrue(TEXT("ramp path is reachable"),
    GoalAtHighEnd.Nodes[UpperStart].IntegrationCostQ < MAX_uint32
      && GoalAtHighEnd.Nodes[UpperRamp].IntegrationCostQ < MAX_uint32);
  TestEqual(TEXT("disconnected lower layer stays unreachable"),
    GoalAtHighEnd.Nodes[LowerStart].IntegrationCostQ, MAX_uint32);
  TestTrue(TEXT("ramp direction is projected to surface tangent"),
    FMath::Abs(FVector::DotProduct(
      GoalAtHighEnd.Nodes[UpperRamp].Direction,
      Graph.Nodes[UpperRamp].SurfaceNormal)) < 0.001);

  FCrowdNavSurfaceFlow Rebound;
  TestTrue(TEXT("dynamic goal attachment rebuilds integration only"),
    FCrowdNavSurfaceGraphKernel::BuildFlow(
      Graph, Graph.Nodes[UpperStart].StableNodeId, 2, Rebound));
  TestEqual(TEXT("dynamic goal keeps topology resource"),
    Rebound.TopologyHash, GoalAtHighEnd.TopologyHash);
  TestNotEqual(TEXT("dynamic goal changes integration"),
    Rebound.IntegrationHash, GoalAtHighEnd.IntegrationHash);
  return true;
}

#endif
