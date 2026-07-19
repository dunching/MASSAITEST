#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoGuidanceComposePriorityTest,
  "CrowdDemo.SF.GuidanceCompose.PriorityAndOrder",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoGuidanceComposePriorityTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoGuidanceCandidate> Candidates;
  Candidates.Add(FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    7, ECrowdDemoGuidanceProvider::SharedFlow, 12,
    FVector(300, 0, 0), FVector(1000, 0, 0), 0.0f, true));
  Candidates.Add(FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    7, ECrowdDemoGuidanceProvider::TargetRegion, 12,
    FVector(0, 200, 0), FVector(0, 1000, 0), 90.0f, true));
  Candidates.Add(FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    7, ECrowdDemoGuidanceProvider::BusinessOverride, 12,
    FVector(-100, 0, 0), FVector(-1000, 0, 0), 180.0f, true));
  const auto Forward = FCrowdDemoGuidanceComposeKernel::Compose(
    7, 12, Candidates, FVector::ZeroVector, 15.0f);
  Algo::Reverse(Candidates);
  const auto Reversed = FCrowdDemoGuidanceComposeKernel::Compose(
    7, 12, Candidates, FVector::ZeroVector, 15.0f);
  TestTrue(TEXT("compose is valid"), Forward.bValid);
  TestEqual(TEXT("business has highest priority"),
    static_cast<uint8>(Forward.SelectedProvider),
    static_cast<uint8>(ECrowdDemoGuidanceProvider::BusinessOverride));
  TestTrue(TEXT("business velocity selected"),
    Forward.AutonomousPreferredVelocity.Equals(FVector(-100, 0, 0)));
  TestEqual(TEXT("input reversal preserves candidate hash"),
    Reversed.CandidateSetHash, Forward.CandidateSetHash);
  TestEqual(TEXT("input reversal preserves composed hash"),
    Reversed.StableHash, Forward.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoGuidanceComposeFallbackTest,
  "CrowdDemo.SF.GuidanceCompose.FallbackAndContract",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoGuidanceComposeFallbackTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdDemoGuidanceCandidate> Candidates;
  Candidates.Add(FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    8, ECrowdDemoGuidanceProvider::TargetRegion, 3,
    FVector(0, 200, 0), FVector(0, 1000, 0), 90.0f, false));
  Candidates.Add(FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    8, ECrowdDemoGuidanceProvider::SharedFlow, 3,
    FVector(150, 0, 0), FVector(1000, 0, 0), 0.0f, true));
  const auto Flow = FCrowdDemoGuidanceComposeKernel::Compose(
    8, 3, Candidates, FVector(5, 6, 7), 25.0f);
  TestEqual(TEXT("invalid target falls back to flow"),
    static_cast<uint8>(Flow.SelectedProvider),
    static_cast<uint8>(ECrowdDemoGuidanceProvider::SharedFlow));

  const auto Stop = FCrowdDemoGuidanceComposeKernel::Compose(
    8, 4, Candidates, FVector(5, 6, 7), 25.0f);
  TestEqual(TEXT("stale revision falls back to stop"),
    static_cast<uint8>(Stop.SelectedProvider),
    static_cast<uint8>(ECrowdDemoGuidanceProvider::Stop));
  TestTrue(TEXT("stop velocity is zero"),
    Stop.AutonomousPreferredVelocity.IsNearlyZero());
  TestTrue(TEXT("stop preserves current location"),
    Stop.DesiredLocation.Equals(FVector(5, 6, 7)));

  const auto Changed = FCrowdDemoGuidanceComposeKernel::BuildCandidate(
    8, ECrowdDemoGuidanceProvider::SharedFlow, 3,
    FVector(151, 0, 0), FVector(1000, 0, 0), 0.0f, true);
  TestNotEqual(TEXT("quantized velocity changes candidate hash"),
    Changed.StableHash, Candidates[1].StableHash);
  return true;
}

#endif
