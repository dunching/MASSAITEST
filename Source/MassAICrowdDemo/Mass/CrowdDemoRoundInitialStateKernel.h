#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

struct FCrowdDemoRoundInitialStateAgent
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 FormationIndex = INDEX_NONE;
  int32 CapabilityProfileKey = 0;
  float RadiusCm = 42.0f;
};

struct FCrowdDemoRoundInitialStateResult
{
  int32 AgentId = INDEX_NONE;
  int32 LifecycleSerial = 0;
  int32 FormationIndex = INDEX_NONE;
  int32 CapabilityProfileKey = 0;
  FVector Location = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 90.0f;
  float RadiusCm = 42.0f;
};

struct FCrowdDemoRoundInitialStateSummary
{
  uint32 InputHash = 2166136261u;
  uint32 InitialStateHash = 2166136261u;
  bool bValid = false;
};

class FCrowdDemoRoundInitialStateKernel
{
public:
  static bool BuildGeneric(
    TConstArrayView<FCrowdDemoRoundInitialStateAgent> Agents,
    const FCrowdDemoRoundRules& Rules,
    TArray<FCrowdDemoRoundInitialStateResult>& OutStates,
    FCrowdDemoRoundInitialStateSummary& OutSummary);

  static uint32 HashStates(
    TConstArrayView<FCrowdDemoRoundInitialStateResult> States);
};
