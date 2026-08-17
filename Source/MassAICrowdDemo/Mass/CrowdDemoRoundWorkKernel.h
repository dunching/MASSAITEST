#pragma once

#include "CoreMinimal.h"
#include "Mass/CrowdDemoGuidanceComposeKernel.h"

// Immutable POD copied from Mass on GT. WORK code must not retain UObject,
// EntityManager, fragment view, or mutable Pipeline references.
struct FCrowdDemoRoundWorkAgentInput
{
  int32 AgentId = INDEX_NONE;
  int32 PlanRevision = 0;
  FVector StopLocation = FVector::ZeroVector;
  float StopYawDegrees = 0.0f;
  FCrowdDemoGuidanceCandidate SharedFlow;
  FCrowdDemoGuidanceCandidate TargetRegion;
  FCrowdDemoGuidanceCandidate BusinessOverride;
};

struct FCrowdDemoRoundWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoRoundWorkAgentInput> Agents;
};

struct FCrowdDemoRoundWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  TArray<FCrowdDemoComposedGuidance> ComposedGuidance;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoRoundWorkKernel
{
public:
  static FCrowdDemoRoundWorkOutput ComposeGuidance(
    const FCrowdDemoRoundWorkInput& Input);
};
