#include "Misc/AutomationTest.h"

#include "Mass/CrowdDemoVatPlaybackKernel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoVatPlaybackRangesTest,
  "CrowdDemo.Combat.T7.VatPlaybackRanges",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoVatPlaybackRangesTest::RunTest(const FString& Parameters)
{
  for (int32 ClipIndex = 0; ClipIndex < FCrowdDemoVatPlaybackKernel::ClipCount; ++ClipIndex)
  {
    FCrowdDemoVatPlaybackInput Input;
    Input.VisualState = static_cast<ECrowdDemoVisualState>(ClipIndex);
    Input.ServerTimeSeconds = 10.5f;
    Input.StateStartServerTimeSeconds = 10.0f;
    Input.PhaseSeed = 0;
    const FCrowdDemoVatPlaybackResult Result = FCrowdDemoVatPlaybackKernel::Evaluate(Input);
    const float StartFrame = static_cast<float>(ClipIndex * FCrowdDemoVatPlaybackKernel::FramesPerClip);
    const float EndFrame = StartFrame + 24.0f;
    TestTrue(TEXT("playback input is valid"), Result.bValid);
    TestTrue(TEXT("frame remains inside clip"), Result.Frame >= StartFrame && Result.Frame <= EndFrame);
    TestTrue(TEXT("previous frame remains inside clip"), Result.PreviousFrame >= StartFrame && Result.PreviousFrame <= EndFrame);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoVatPlaybackLoopAndClampTest,
  "CrowdDemo.Combat.T7.VatPlaybackLoopAndClamp",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoVatPlaybackLoopAndClampTest::RunTest(const FString& Parameters)
{
  FCrowdDemoVatPlaybackInput Move;
  Move.VisualState = ECrowdDemoVisualState::Move;
  Move.ServerTimeSeconds = 100.0f;
  Move.StateStartServerTimeSeconds = 0.0f;
  Move.PhaseSeed = 3;
  const FCrowdDemoVatPlaybackResult MoveResult = FCrowdDemoVatPlaybackKernel::Evaluate(Move);
  TestTrue(TEXT("move loops"), MoveResult.bLooping);
  TestTrue(TEXT("move remains in range"), MoveResult.Frame >= 25.0f && MoveResult.Frame <= 49.0f);

  FCrowdDemoVatPlaybackInput Death = Move;
  Death.VisualState = ECrowdDemoVisualState::Death;
  const FCrowdDemoVatPlaybackResult DeathResult = FCrowdDemoVatPlaybackKernel::Evaluate(Death);
  TestFalse(TEXT("death does not loop"), DeathResult.bLooping);
  TestEqual(TEXT("death clamps to final baked frame"), DeathResult.Frame, 124.0f);
  TestEqual(TEXT("death previous frame clamps inside clip"), DeathResult.PreviousFrame, 123.0f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoVatPlaybackFlashTest,
  "CrowdDemo.Combat.T7.HitFlashCurve",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoVatPlaybackFlashTest::RunTest(const FString& Parameters)
{
  FCrowdDemoVatPlaybackInput Input;
  Input.HitFlashRevision = 1;
  Input.HitFlashStartServerTimeSeconds = 2.0f;
  Input.HitFlashDurationSeconds = 0.4f;
  Input.HitFlashPeakIntensity = 1.0f;
  Input.ServerTimeSeconds = 2.2f;
  const FCrowdDemoVatPlaybackResult Mid = FCrowdDemoVatPlaybackKernel::Evaluate(Input);
  TestTrue(TEXT("flash active at midpoint"), FMath::IsNearlyEqual(Mid.HitFlashIntensity, 0.5f, 0.001f));

  Input.ServerTimeSeconds = 2.4f;
  const FCrowdDemoVatPlaybackResult Expired = FCrowdDemoVatPlaybackKernel::Evaluate(Input);
  TestEqual(TEXT("flash expires deterministically"), Expired.HitFlashIntensity, 0.0f);
  return true;
}

#endif
