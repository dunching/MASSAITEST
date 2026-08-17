#include "Mass/CrowdDemoVatPlaybackKernel.h"

namespace
{
  bool IsLoopingState(const ECrowdDemoVisualState State)
  {
    return State == ECrowdDemoVisualState::Idle || State == ECrowdDemoVisualState::Move;
  }
}

FCrowdDemoVatPlaybackResult FCrowdDemoVatPlaybackKernel::Evaluate(
  const FCrowdDemoVatPlaybackInput& Input)
{
  FCrowdDemoVatPlaybackResult Result;
  const int32 ClipIndex = static_cast<int32>(Input.VisualState);
  if (ClipIndex < 0 || ClipIndex >= ClipCount
    || !FMath::IsFinite(Input.ServerTimeSeconds)
    || !FMath::IsFinite(Input.StateStartServerTimeSeconds)
    || !FMath::IsFinite(Input.PlayRate))
  {
    return Result;
  }

  Result.ClipIndex = ClipIndex;
  Result.bLooping = IsLoopingState(Input.VisualState);
  const float StartFrame = static_cast<float>(ClipIndex * FramesPerClip);
  const float EndFrame = StartFrame + static_cast<float>(FramesPerClip - 1);
  const float ElapsedSeconds = FMath::Max(
    0.0f,
    Input.ServerTimeSeconds - Input.StateStartServerTimeSeconds);
  const float PlayRate = FMath::Max(0.0f, Input.PlayRate);
  const float SeedFrames = Result.bLooping
    ? static_cast<float>(Input.PhaseSeed % FramesPerClip)
    : 0.0f;
  const float LocalFrame = ElapsedSeconds * SampleRate * PlayRate + SeedFrames;

  if (Result.bLooping)
  {
    const float Wrapped = FMath::Fmod(LocalFrame, static_cast<float>(FramesPerClip));
    Result.Frame = StartFrame + FMath::Max(Wrapped, 0.0f);
    const float PreviousLocal = FMath::Fmod(
      FMath::Max(Wrapped, 0.0f) + static_cast<float>(FramesPerClip - 1),
      static_cast<float>(FramesPerClip));
    Result.PreviousFrame = StartFrame + PreviousLocal;
  }
  else
  {
    Result.Frame = FMath::Clamp(StartFrame + LocalFrame, StartFrame, EndFrame);
    Result.PreviousFrame = FMath::Clamp(Result.Frame - 1.0f, StartFrame, EndFrame);
  }

  if (Input.HitFlashRevision > 0
    && Input.HitFlashDurationSeconds > KINDA_SMALL_NUMBER
    && FMath::IsFinite(Input.HitFlashStartServerTimeSeconds)
    && FMath::IsFinite(Input.HitFlashDurationSeconds)
    && FMath::IsFinite(Input.HitFlashPeakIntensity))
  {
    const float FlashAge = Input.ServerTimeSeconds - Input.HitFlashStartServerTimeSeconds;
    if (FlashAge >= 0.0f && FlashAge < Input.HitFlashDurationSeconds)
    {
      const float NormalizedAge = FlashAge / Input.HitFlashDurationSeconds;
      Result.HitFlashIntensity = FMath::Max(
        0.0f,
        Input.HitFlashPeakIntensity * (1.0f - NormalizedAge));
    }
  }

  Result.bValid = true;
  return Result;
}
