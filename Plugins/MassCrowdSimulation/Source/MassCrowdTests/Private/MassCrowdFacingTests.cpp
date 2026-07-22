#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "CrowdFacingKernel.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdFacingDeterminismTest,
  "MassCrowd.Core.Facing.Determinism",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdFacingDeterminismTest::RunTest(const FString& Parameters)
{
  FCrowdFacingSettings Settings;
  FCrowdFacingInput Autonomous;
  Autonomous.AgentId = 4;
  Autonomous.CurrentYawDegrees = 175.0f;
  Autonomous.AutonomousPreferredVelocity = FVector2f(-100.0f, -10.0f);
  Autonomous.bHasTarget = true;
  Autonomous.TargetLocation = FVector2f(1000.0f, 0.0f);

  FCrowdFacingInput Settled = Autonomous;
  Settled.AgentId = 2;
  Settled.CurrentYawDegrees = 0.0f;
  Settled.Location = FVector2f::ZeroVector;
  Settled.TargetLocation = FVector2f(0.0f, 100.0f);
  Settled.bFinalPositionSettled = true;

  FCrowdFacingInput Held = Autonomous;
  Held.AgentId = 3;
  Held.CurrentYawDegrees = -179.995f;
  Held.AutonomousPreferredVelocity = FVector2f::ZeroVector;
  Held.bHasTarget = false;

  TArray<FCrowdFacingInput> Inputs = {Autonomous, Settled, Held};
  FCrowdFacingSummary Forward;
  FCrowdFacingKernel::Resolve(Inputs, Settings, Forward);
  TestTrue(TEXT("facing solve valid"), Forward.bValid);
  TestEqual(TEXT("results sorted by AgentId"), Forward.Results[0].AgentId, 2);
  TestTrue(TEXT("settled entity faces target"),
    Forward.Results[0].bFacingTarget);
  TestEqual(TEXT("turn rate limits one 30Hz step"),
    Forward.Results[0].ResolvedYawDegrees, 12.0f);
  TestTrue(TEXT("zero autonomous direction holds yaw"),
    Forward.Results[1].bHeldCurrentYaw);
  TestTrue(TEXT("held yaw changes by no more than one angle quantum"),
    FMath::Abs(FMath::FindDeltaAngleDegrees(
      Held.CurrentYawDegrees, Forward.Results[1].ResolvedYawDegrees))
      <= Settings.AngleQuantumDegrees);
  TestFalse(TEXT("moving entity does not face target"),
    Forward.Results[2].bFacingTarget);
  TestTrue(TEXT("moving entity follows autonomous direction"),
    Forward.Results[2].AppliedYawDeltaDegrees > 0.0f);

  Algo::Reverse(Inputs);
  FCrowdFacingSummary Reversed;
  FCrowdFacingKernel::Resolve(Inputs, Settings, Reversed);
  TestTrue(TEXT("reversed solve valid"), Reversed.bValid);
  TestEqual(TEXT("input reversal preserves hash"),
    Reversed.StableHash, Forward.StableHash);

  FCrowdFacingSettings InvalidSettings = Settings;
  InvalidSettings.FixedStepSeconds = 0.0f;
  FCrowdFacingSummary Invalid;
  FCrowdFacingKernel::Resolve(Inputs, InvalidSettings, Invalid);
  TestFalse(TEXT("invalid timing is rejected"), Invalid.bValid);
  return true;
}

#endif
