#include "Misc/AutomationTest.h"
#include "MassCrowdSpatialQuery.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdSpatialBodySnapshot MakeSpatialBody(
    const uint64 EntityId,
    const FVector& Start,
    const FVector& End,
    const uint32 NavLayer = 0)
  {
    FCrowdSpatialBodySnapshot Body;
    Body.EntityRef = {1, EntityId, 1};
    Body.StartPosition = Start;
    Body.EndPosition = End;
    Body.RadiusCm = 40.0f;
    Body.NavLayer = NavLayer;
    Body.RecalculateStableHash();
    return Body;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdSpatialStableGridAndSweepTest,
  "MassCrowd.Spatial.StableGridAndRelativeSweep",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdSpatialStableGridAndSweepTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdSpatialBodySnapshot> ReverseBodies = {
    MakeSpatialBody(
      3, FVector(300.0, -100.0, 0.0),
      FVector(300.0, 100.0, 0.0)),
    MakeSpatialBody(
      2, FVector(300.0, 0.0, 0.0),
      FVector(300.0, 0.0, 0.0)),
    MakeSpatialBody(
      1, FVector(100.0, 0.0, 0.0),
      FVector(100.0, 0.0, 0.0), 1)};
  FCrowdSpatialQueryIndex Index;
  TestTrue(TEXT("reverse bodies build"),
    Index.Build(ReverseBodies, 128.0f));
  TArray<int32> Candidates;
  TestTrue(TEXT("query succeeds"),
    Index.GatherCandidates(
      FVector::ZeroVector, FVector(600.0, 0.0, 0.0),
      12.0f, 0, MAX_uint32, Candidates));
  TestEqual(TEXT("other nav layer is excluded"),
    Candidates.Num(), 2);
  TestTrue(TEXT("candidate order is stable"),
    Index.GetBodyChecked(Candidates[0]).EntityRef.StableEntityId == 2
      && Index.GetBodyChecked(Candidates[1]).EntityRef.StableEntityId == 3);

  FCrowdSpatialSweepHit StaticHit;
  FCrowdSpatialSweepHit MovingHit;
  TestTrue(TEXT("static target swept"),
    FCrowdSpatialSweep::MovingSphere(
      FVector::ZeroVector, FVector(600.0, 0.0, 0.0), 12.0f,
      Index.GetBodyChecked(Candidates[0]), StaticHit));
  TestTrue(TEXT("moving target relative swept"),
    FCrowdSpatialSweep::MovingSphere(
      FVector::ZeroVector, FVector(600.0, 0.0, 0.0), 12.0f,
      Index.GetBodyChecked(Candidates[1]), MovingHit));
  TestTrue(TEXT("TOI is quantized"),
    StaticHit.TimeOfImpactQ <= 1000000
      && MovingHit.TimeOfImpactQ <= 1000000);

  FCrowdSpatialEnvironmentBody Wall;
  Wall.StableSurfaceId = 9;
  Wall.BoundsMin = FVector(248.0, -100.0, -100.0);
  Wall.BoundsMax = FVector(252.0, 100.0, 100.0);
  Wall.CollisionProfileId = 2;
  Wall.EffectProfileId = 3;
  Wall.RecalculateStableHash();
  FCrowdSpatialSweepHit WallHit;
  TestTrue(TEXT("environment AABB swept"),
    FCrowdSpatialSweep::EnvironmentAabb(
      FVector::ZeroVector, FVector(600.0, 0.0, 0.0),
      12.0f, Wall, WallHit));
  TestTrue(TEXT("wall wins when earlier"),
    FCrowdSpatialSweep::IsEarlierStableHit(
      WallHit, StaticHit));
  return true;
}

#endif
