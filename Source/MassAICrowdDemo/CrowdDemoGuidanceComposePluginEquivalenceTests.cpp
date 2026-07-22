#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdGuidanceComposeKernel.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"
#include "Misc/AutomationTest.h"

namespace
{
  ECrowdGuidanceProvider ToCoreProvider(
    const ECrowdDemoGuidanceProvider Provider)
  {
    return static_cast<ECrowdGuidanceProvider>(static_cast<uint8>(Provider));
  }

  void CompareCandidate(
    FAutomationTestBase& Test,
    const TCHAR* Label,
    const FCrowdDemoGuidanceCandidate& Legacy,
    const FCrowdGuidanceCandidate& Core)
  {
    Test.TestEqual(FString::Printf(TEXT("%s agent"), Label),
      Core.AgentId, Legacy.AgentId);
    Test.TestEqual(FString::Printf(TEXT("%s provider"), Label),
      static_cast<uint8>(Core.Provider),
      static_cast<uint8>(Legacy.Provider));
    Test.TestEqual(FString::Printf(TEXT("%s revision"), Label),
      Core.PlanRevision, Legacy.PlanRevision);
    Test.TestTrue(FString::Printf(TEXT("%s velocity"), Label),
      Core.PreferredVelocity.Equals(Legacy.PreferredVelocity));
    Test.TestTrue(FString::Printf(TEXT("%s location"), Label),
      Core.DesiredLocation.Equals(Legacy.DesiredLocation));
    Test.TestEqual(FString::Printf(TEXT("%s yaw"), Label),
      Core.DesiredYawDegrees, Legacy.DesiredYawDegrees);
    Test.TestEqual(FString::Printf(TEXT("%s valid"), Label),
      Core.bValid, Legacy.bValid);
    Test.TestEqual(FString::Printf(TEXT("%s hash"), Label),
      Core.StableHash, Legacy.StableHash);
  }

  void CompareComposed(
    FAutomationTestBase& Test,
    const TCHAR* Label,
    const FCrowdDemoComposedGuidance& Legacy,
    const FCrowdComposedGuidance& Core)
  {
    Test.TestEqual(FString::Printf(TEXT("%s agent"), Label),
      Core.AgentId, Legacy.AgentId);
    Test.TestEqual(FString::Printf(TEXT("%s provider"), Label),
      static_cast<uint8>(Core.SelectedProvider),
      static_cast<uint8>(Legacy.SelectedProvider));
    Test.TestEqual(FString::Printf(TEXT("%s revision"), Label),
      Core.PlanRevision, Legacy.PlanRevision);
    Test.TestTrue(FString::Printf(TEXT("%s velocity"), Label),
      Core.AutonomousPreferredVelocity.Equals(
        Legacy.AutonomousPreferredVelocity));
    Test.TestTrue(FString::Printf(TEXT("%s location"), Label),
      Core.DesiredLocation.Equals(Legacy.DesiredLocation));
    Test.TestEqual(FString::Printf(TEXT("%s yaw"), Label),
      Core.DesiredYawDegrees, Legacy.DesiredYawDegrees);
    Test.TestEqual(FString::Printf(TEXT("%s candidate hash"), Label),
      Core.CandidateSetHash, Legacy.CandidateSetHash);
    Test.TestEqual(FString::Printf(TEXT("%s valid"), Label),
      Core.bValid, Legacy.bValid);
    Test.TestEqual(FString::Printf(TEXT("%s hash"), Label),
      Core.StableHash, Legacy.StableHash);
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoGuidanceComposePluginEquivalenceTest,
  "CrowdDemo.SF.GuidanceCompose.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoGuidanceComposePluginEquivalenceTest::RunTest(
  const FString& Parameters)
{
  struct FInput
  {
    ECrowdDemoGuidanceProvider Provider;
    FVector Velocity;
    FVector Location;
    float Yaw;
    bool bValid;
  };
  const FInput Inputs[] = {
    {ECrowdDemoGuidanceProvider::SharedFlow,
      FVector(300.49, -20.51, 0.49), FVector(1000.4, 10.6, 60.0),
      5.125f, true},
    {ECrowdDemoGuidanceProvider::TargetRegion,
      FVector(40.0, 200.0, 0.0), FVector(50.0, 1000.0, 60.0),
      89.995f, true},
    {ECrowdDemoGuidanceProvider::BusinessOverride,
      FVector(-100.0, 25.0, 0.0), FVector(-1000.0, 15.0, 60.0),
      179.995f, true},
  };

  TArray<FCrowdDemoGuidanceCandidate> LegacyCandidates;
  TArray<FCrowdGuidanceCandidate> CoreCandidates;
  for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
  {
    const FInput& Input = Inputs[Index];
    const FCrowdDemoGuidanceCandidate Legacy =
      FCrowdDemoGuidanceComposeKernel::BuildCandidate(
        42, Input.Provider, 17, Input.Velocity, Input.Location,
        Input.Yaw, Input.bValid);
    const FCrowdGuidanceCandidate Core =
      FCrowdGuidanceComposeKernel::BuildCandidate(
        42, ToCoreProvider(Input.Provider), 17, Input.Velocity,
        Input.Location, Input.Yaw, Input.bValid);
    CompareCandidate(*this, *FString::Printf(TEXT("candidate %d"), Index),
      Legacy, Core);
    LegacyCandidates.Add(Legacy);
    CoreCandidates.Add(Core);
  }

  const FVector StopLocation(7.4, 8.6, 60.0);
  const FCrowdDemoComposedGuidance LegacyForward =
    FCrowdDemoGuidanceComposeKernel::Compose(
      42, 17, LegacyCandidates, StopLocation, 12.345f);
  const FCrowdComposedGuidance CoreForward =
    FCrowdGuidanceComposeKernel::Compose(
      42, 17, CoreCandidates, StopLocation, 12.345f);
  CompareComposed(*this, TEXT("forward"), LegacyForward, CoreForward);

  Algo::Reverse(LegacyCandidates);
  Algo::Reverse(CoreCandidates);
  const FCrowdDemoComposedGuidance LegacyReverse =
    FCrowdDemoGuidanceComposeKernel::Compose(
      42, 17, LegacyCandidates, StopLocation, 12.345f);
  const FCrowdComposedGuidance CoreReverse =
    FCrowdGuidanceComposeKernel::Compose(
      42, 17, CoreCandidates, StopLocation, 12.345f);
  CompareComposed(*this, TEXT("reverse"), LegacyReverse, CoreReverse);

  const FCrowdDemoComposedGuidance LegacyStale =
    FCrowdDemoGuidanceComposeKernel::Compose(
      42, 18, LegacyCandidates, StopLocation, 12.345f);
  const FCrowdComposedGuidance CoreStale =
    FCrowdGuidanceComposeKernel::Compose(
      42, 18, CoreCandidates, StopLocation, 12.345f);
  CompareComposed(*this, TEXT("stale revision"), LegacyStale, CoreStale);

  const FCrowdDemoGuidanceCandidate LegacyInvalid =
    FCrowdDemoGuidanceComposeKernel::BuildCandidate(
      INDEX_NONE, ECrowdDemoGuidanceProvider::SharedFlow, 17,
      FVector::ZeroVector, FVector::ZeroVector, 0.0f, true);
  const FCrowdGuidanceCandidate CoreInvalid =
    FCrowdGuidanceComposeKernel::BuildCandidate(
      INDEX_NONE, ECrowdGuidanceProvider::SharedFlow, 17,
      FVector::ZeroVector, FVector::ZeroVector, 0.0f, true);
  CompareCandidate(*this, TEXT("invalid identity"),
    LegacyInvalid, CoreInvalid);
  return true;
}

#endif
