#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdFacingKernel.h"
#include "Mass/CrowdDemoFacingKernel.h"
#include "MassCrowdFacingWork.h"
#include "Misc/AutomationTest.h"

namespace
{
  FCrowdFacingInput ToCoreInput(const FCrowdDemoFacingInput& Legacy)
  {
    FCrowdFacingInput Core;
    Core.AgentId = Legacy.AgentId;
    Core.CurrentYawDegrees = Legacy.CurrentYawDegrees;
    Core.AutonomousPreferredVelocity = Legacy.AutonomousPreferredVelocity;
    Core.Location = Legacy.Location;
    Core.TargetLocation = Legacy.TargetLocation;
    Core.bHasTarget = Legacy.bHasTarget;
    Core.bFinalPositionSettled = Legacy.bFinalPositionSettled;
    return Core;
  }

  void CompareSummaries(
    FAutomationTestBase& Test,
    const TCHAR* Label,
    const FCrowdDemoFacingSummary& Legacy,
    const FCrowdFacingSummary& Core)
  {
    Test.TestEqual(FString::Printf(TEXT("%s valid"), Label),
      Core.bValid, Legacy.bValid);
    Test.TestEqual(FString::Printf(TEXT("%s result count"), Label),
      Core.Results.Num(), Legacy.Results.Num());
    Test.TestEqual(FString::Printf(TEXT("%s target count"), Label),
      Core.TargetFacingAgentCount, Legacy.TargetFacingAgentCount);
    Test.TestEqual(FString::Printf(TEXT("%s autonomous count"), Label),
      Core.AutonomousFacingAgentCount, Legacy.AutonomousFacingAgentCount);
    Test.TestEqual(FString::Printf(TEXT("%s held count"), Label),
      Core.HeldYawAgentCount, Legacy.HeldYawAgentCount);
    Test.TestEqual(FString::Printf(TEXT("%s maximum delta"), Label),
      Core.MaximumAppliedYawDeltaDegrees,
      Legacy.MaximumAppliedYawDeltaDegrees);
    Test.TestEqual(FString::Printf(TEXT("%s hash"), Label),
      Core.StableHash, Legacy.StableHash);
    for (int32 Index = 0;
      Index < FMath::Min(Core.Results.Num(), Legacy.Results.Num()); ++Index)
    {
      const FCrowdFacingResult& NewResult = Core.Results[Index];
      const FCrowdDemoFacingResult& OldResult = Legacy.Results[Index];
      const FString Prefix = FString::Printf(TEXT("%s result %d"), Label, Index);
      Test.TestEqual(Prefix + TEXT(" agent"),
        NewResult.AgentId, OldResult.AgentId);
      Test.TestEqual(Prefix + TEXT(" desired"),
        NewResult.DesiredYawDegrees, OldResult.DesiredYawDegrees);
      Test.TestEqual(Prefix + TEXT(" resolved"),
        NewResult.ResolvedYawDegrees, OldResult.ResolvedYawDegrees);
      Test.TestEqual(Prefix + TEXT(" delta"),
        NewResult.AppliedYawDeltaDegrees, OldResult.AppliedYawDeltaDegrees);
      Test.TestEqual(Prefix + TEXT(" target"),
        NewResult.bFacingTarget, OldResult.bFacingTarget);
      Test.TestEqual(Prefix + TEXT(" held"),
        NewResult.bHeldCurrentYaw, OldResult.bHeldCurrentYaw);
    }
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoFacingPluginEquivalenceTest,
  "CrowdDemo.SF.Facing.PluginCoreEquivalence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoFacingPluginEquivalenceTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoFacingSettings LegacySettings;
  LegacySettings.FixedStepSeconds = 1.0f / 30.0f;
  LegacySettings.MaximumTurnRateDegreesPerSecond = 333.0f;
  LegacySettings.AutonomousSpeedEpsilonCmps = 1.25f;
  LegacySettings.AngleQuantumDegrees = 0.01f;
  FCrowdFacingSettings CoreSettings;
  CoreSettings.FixedStepSeconds = LegacySettings.FixedStepSeconds;
  CoreSettings.MaximumTurnRateDegreesPerSecond =
    LegacySettings.MaximumTurnRateDegreesPerSecond;
  CoreSettings.AutonomousSpeedEpsilonCmps =
    LegacySettings.AutonomousSpeedEpsilonCmps;
  CoreSettings.AngleQuantumDegrees = LegacySettings.AngleQuantumDegrees;

  TArray<FCrowdDemoFacingInput> LegacyInputs;
  auto AddInput = [&LegacyInputs](
    int32 AgentId, float CurrentYaw, FVector2f Autonomous,
    FVector2f Location, FVector2f Target, bool bHasTarget, bool bSettled)
  {
    FCrowdDemoFacingInput& Input = LegacyInputs.AddDefaulted_GetRef();
    Input.AgentId = AgentId;
    Input.CurrentYawDegrees = CurrentYaw;
    Input.AutonomousPreferredVelocity = Autonomous;
    Input.Location = Location;
    Input.TargetLocation = Target;
    Input.bHasTarget = bHasTarget;
    Input.bFinalPositionSettled = bSettled;
  };
  AddInput(9, 179.0f, FVector2f(-200.0f, -4.0f),
    FVector2f::ZeroVector, FVector2f(100.0f, 0.0f), true, false);
  AddInput(3, -170.0f, FVector2f(300.0f, 0.0f),
    FVector2f(10.0f, 20.0f), FVector2f(-90.0f, 20.0f), true, true);
  AddInput(7, 45.006f, FVector2f(1.0f, 0.0f),
    FVector2f::ZeroVector, FVector2f::ZeroVector, false, false);
  AddInput(5, 90.0f, FVector2f(0.0f, -300.0f),
    FVector2f(50.0f, 50.0f), FVector2f(50.0f, 50.0f), true, true);

  TArray<FCrowdFacingInput> CoreInputs;
  for (const FCrowdDemoFacingInput& Input : LegacyInputs)
    CoreInputs.Add(ToCoreInput(Input));
  FCrowdDemoFacingSummary LegacyForward;
  FCrowdFacingSummary CoreForward;
  FCrowdDemoFacingKernel::Resolve(LegacyInputs, LegacySettings, LegacyForward);
  FCrowdFacingKernel::Resolve(CoreInputs, CoreSettings, CoreForward);
  CompareSummaries(*this, TEXT("forward"), LegacyForward, CoreForward);
  FCrowdMassFacingWorkInput RuntimeInput;
  RuntimeInput.FixedStepIndex = 23;
  RuntimeInput.PlanRevision = 7;
  RuntimeInput.Settings = CoreSettings;
  RuntimeInput.Agents = CoreInputs;
  const FCrowdMassFacingWorkOutput RuntimeForward =
    FCrowdMassFacingWork::Resolve(RuntimeInput);
  TestTrue(TEXT("Runtime forward completed"), RuntimeForward.bCompleted);
  CompareSummaries(
    *this, TEXT("runtime forward"), LegacyForward, RuntimeForward.Summary);

  Algo::Reverse(LegacyInputs);
  Algo::Reverse(CoreInputs);
  FCrowdDemoFacingSummary LegacyReversed;
  FCrowdFacingSummary CoreReversed;
  FCrowdDemoFacingKernel::Resolve(
    LegacyInputs, LegacySettings, LegacyReversed);
  FCrowdFacingKernel::Resolve(CoreInputs, CoreSettings, CoreReversed);
  CompareSummaries(*this, TEXT("reversed"), LegacyReversed, CoreReversed);
  RuntimeInput.Agents = CoreInputs;
  const FCrowdMassFacingWorkOutput RuntimeReversed =
    FCrowdMassFacingWork::Resolve(RuntimeInput);
  TestTrue(TEXT("Runtime reversed completed"), RuntimeReversed.bCompleted);
  CompareSummaries(
    *this, TEXT("runtime reversed"), LegacyReversed,
    RuntimeReversed.Summary);
  TestEqual(TEXT("legacy reversal remains stable"),
    LegacyReversed.StableHash, LegacyForward.StableHash);
  TestEqual(TEXT("Core reversal remains stable"),
    CoreReversed.StableHash, CoreForward.StableHash);
  TestEqual(TEXT("Runtime reversal remains stable"),
    RuntimeReversed.StableHash, RuntimeForward.StableHash);
  return true;
}

#endif
