#include "Misc/AutomationTest.h"
#include "MassCrowdSpatialSafety.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdSpatialSafetyIndexTest,
  "MassCrowd.Runtime.SpatialSafety.IndexAndUpdate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdSpatialSafetyIndexTest::RunTest(const FString& Parameters)
{
  FCrowdSpatialSafetyIndex Index;
  const TArray<FCrowdSpatialSafetyAgent> Agents = {
    {{1, 2, 1}, FVector(200.0, 0.0, 0.0), 50.0f, 0},
    {{1, 1, 1}, FVector(0.0, 0.0, 0.0), 50.0f, 0},
    {{1, 3, 1}, FVector(0.0, 0.0, 300.0), 50.0f, 0},
    {{1, 4, 1}, FVector(20.0, 0.0, 0.0), 50.0f, 1}};
  TestTrue(TEXT("index builds from reverse stable order"),
    Index.Build(Agents, 120.0f, 150.0f));
  TestFalse(TEXT("same-layer overlap is rejected"),
    Index.IsCandidateSafe({1, 1, 1}, FVector(120.0, 0.0, 0.0), 50.0f));
  TestTrue(TEXT("different nav layer does not conflict"),
    Index.IsCandidateSafe({1, 1, 1}, FVector(0.0, 0.0, 160.0), 50.0f));
  TestEqual(TEXT("minimum separation ignores different nav layers"),
    Index.CalculateMinimumSeparationCm(), 200.0f);
  TestTrue(TEXT("moving entity updates its spatial cell"),
    Index.Update({1, 2, 1}, FVector(400.0, 0.0, 0.0)));
  TestTrue(TEXT("vacated cell becomes safe"),
    Index.IsCandidateSafe({1, 1, 1}, FVector(120.0, 0.0, 0.0), 50.0f));
  return true;
}

#endif
