#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

class MASSAICROWDDEMO_API FCrowdDemoT7PresentationEventStream
{
public:
  bool Enqueue(const FCrowdDemoT7PresentationEvent& Event);
  bool Resolve(
    int32 ExpectedRoundId,
    int32 AgentId,
    int32 LifecycleSerial,
    double WorldSeconds,
    FCrowdDemoT7PresentationEvent& OutEvent);
  void Reset();

private:
  struct FTrack
  {
    FCrowdDemoT7PresentationEvent Current;
    TArray<FCrowdDemoT7PresentationEvent> Pending;
    double NextAdvanceWorldSeconds = 0.0;
    bool bHasCurrent = false;
  };

  static constexpr double MinimumPresentationSeconds = 0.10;
  int32 RoundId = INDEX_NONE;
  TMap<int32, FTrack> Tracks;
};
