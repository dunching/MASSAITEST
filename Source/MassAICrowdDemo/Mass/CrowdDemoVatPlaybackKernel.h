#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoVatPlaybackInput
{
  ECrowdDemoVisualState VisualState = ECrowdDemoVisualState::Idle;
  float ServerTimeSeconds = 0.0f;
  float StateStartServerTimeSeconds = 0.0f;
  float PlayRate = 1.0f;
  uint32 PhaseSeed = 0;
  int32 HitFlashRevision = 0;
  float HitFlashStartServerTimeSeconds = 0.0f;
  float HitFlashDurationSeconds = 0.0f;
  float HitFlashPeakIntensity = 0.0f;
};

struct FCrowdDemoVatPlaybackResult
{
  int32 ClipIndex = 0;
  float Frame = 0.0f;
  float PreviousFrame = 0.0f;
  float HitFlashIntensity = 0.0f;
  bool bLooping = true;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoVatPlaybackKernel
{
public:
  static constexpr float SampleRate = 30.0f;
  static constexpr int32 FramesPerClip = 25;
  static constexpr int32 ClipCount = 5;

  static FCrowdDemoVatPlaybackResult Evaluate(const FCrowdDemoVatPlaybackInput& Input);
};
