#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdGuidanceComposeKernel.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdGuidanceComposeDeterminismTest,
  "MassCrowd.Core.GuidanceCompose.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdGuidanceComposeDeterminismTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdGuidanceCandidate> Candidates;
  Candidates.Add(FCrowdGuidanceComposeKernel::BuildCandidate(
    7, ECrowdGuidanceProvider::SharedFlow, 12,
    FVector(300, 0, 0), FVector(1000, 0, 0), 0.0f, true));
  Candidates.Add(FCrowdGuidanceComposeKernel::BuildCandidate(
    7, ECrowdGuidanceProvider::TargetRegion, 12,
    FVector(0, 200, 0), FVector(0, 1000, 0), 90.0f, true));
  Candidates.Add(FCrowdGuidanceComposeKernel::BuildCandidate(
    7, ECrowdGuidanceProvider::BusinessOverride, 12,
    FVector(-100, 0, 0), FVector(-1000, 0, 0), 180.0f, true));

  const FCrowdComposedGuidance Forward = FCrowdGuidanceComposeKernel::Compose(
    7, 12, Candidates, FVector(5, 6, 7), 15.0f);
  Algo::Reverse(Candidates);
  const FCrowdComposedGuidance Reversed = FCrowdGuidanceComposeKernel::Compose(
    7, 12, Candidates, FVector(5, 6, 7), 15.0f);

  TestTrue(TEXT("compose is valid"), Forward.bValid);
  TestEqual(TEXT("business override has highest priority"),
    static_cast<uint8>(Forward.SelectedProvider),
    static_cast<uint8>(ECrowdGuidanceProvider::BusinessOverride));
  TestTrue(TEXT("business velocity selected"),
    Forward.AutonomousPreferredVelocity.Equals(FVector(-100, 0, 0)));
  TestEqual(TEXT("input reversal preserves candidate hash"),
    Reversed.CandidateSetHash, Forward.CandidateSetHash);
  TestEqual(TEXT("input reversal preserves composed hash"),
    Reversed.StableHash, Forward.StableHash);

  const FCrowdComposedGuidance Stale = FCrowdGuidanceComposeKernel::Compose(
    7, 13, Candidates, FVector(5, 6, 7), 15.0f);
  TestEqual(TEXT("stale revision selects stop"),
    static_cast<uint8>(Stale.SelectedProvider),
    static_cast<uint8>(ECrowdGuidanceProvider::Stop));
  TestTrue(TEXT("stop velocity is zero"),
    Stale.AutonomousPreferredVelocity.IsNearlyZero());
  TestTrue(TEXT("stop location is preserved"),
    Stale.DesiredLocation.Equals(FVector(5, 6, 7)));

  const FCrowdGuidanceCandidate Invalid =
    FCrowdGuidanceComposeKernel::BuildCandidate(
      INDEX_NONE, ECrowdGuidanceProvider::SharedFlow, 12,
      FVector::ZeroVector, FVector::ZeroVector, 0.0f, true);
  TestFalse(TEXT("invalid identity rejects candidate"), Invalid.bValid);
  return true;
}

#endif
