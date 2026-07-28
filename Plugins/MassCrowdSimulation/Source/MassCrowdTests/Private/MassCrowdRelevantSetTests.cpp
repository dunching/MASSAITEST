#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "MassCrowdRelevantSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdSpatialRelevantSetTest,
  "MassCrowd.Networking.Relevancy.SpatialGridAndClosure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdSpatialRelevantSetTest::RunTest(
  const FString& Parameters)
{
  FCrowdRelevantSetLimits Limits;
  Limits.MaxRelevantEntities = 8;
  Limits.MaxRelationshipEdges = 8;
  FCrowdSpatialGridRelevantSetProvider Provider(Limits);
  const FCrowdStableEntityRef A{1, 1, 1};
  const FCrowdStableEntityRef B{1, 2, 1};
  const FCrowdStableEntityRef C{1, 3, 1};
  const FCrowdStableEntityRef D{1, 4, 1};
  TArray<FCrowdAuthorityLocationRecord> Locations = {
    {D, FVector(9000.0, 0.0, 0.0), 1},
    {C, FVector(8000.0, 0.0, 0.0), 1},
    {B, FVector(7000.0, 0.0, 0.0), 1},
    {A, FVector(100.0, 0.0, 0.0), 1}};
  TArray<FCrowdRelationshipEdge> Edges = {
    {B, C, 1}, {A, B, 1}, {C, D, 1}};
  FCrowdClientView View{7, FVector::ZeroVector, 500.0f, 1};
  FCrowdRelevantSetResult Forward;
  TestTrue(TEXT("persistent spatial index builds"),
    Provider.RebuildIndex(Locations, Edges));
  TestTrue(TEXT("spatial set and bounded closure build"),
    Provider.BuildRelevantSet(View, Forward));
  TestTrue(TEXT("two-hop closure includes A B C but not D"),
    Forward.EntityRefs == TArray<FCrowdStableEntityRef>({A, B, C}));
  Algo::Reverse(Locations);
  Algo::Reverse(Edges);
  FCrowdRelevantSetResult Reverse;
  TestTrue(TEXT("reversed input rebuilds the same index"),
    Provider.RebuildIndex(Locations, Edges));
  TestTrue(TEXT("input order does not change set"),
    Provider.BuildRelevantSet(View, Reverse));
  TestEqual(TEXT("relevancy hash is deterministic"),
    Reverse.StableHash, Forward.StableHash);
  Edges.Add({A, FCrowdStableEntityRef{9, 9, 1}, 1});
  TestFalse(TEXT("dangling relationship fails closed"),
    Provider.RebuildIndex(Locations, Edges));
  FCrowdRelevantSetIndexUpdate MoveUpdate;
  MoveUpdate.Upserts.Add(
    {A, FVector(5000.0, 0.0, 0.0), 1});
  TestTrue(TEXT("location incrementally migrates buckets"),
    Provider.ApplyIndexUpdate(MoveUpdate));
  FCrowdRelevantSetResult Moved;
  TestTrue(TEXT("query after bucket migration succeeds"),
    Provider.BuildRelevantSet(View, Moved));
  TestTrue(TEXT("moved root leaves the nearby set"),
    Moved.EntityRefs.IsEmpty());
  return true;
}
