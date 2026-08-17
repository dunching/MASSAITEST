#pragma once

#include "CoreMinimal.h"

enum class ECrowdDemoGuidanceProvider : uint8
{
  SharedFlow = 0,
  TargetRegion = 1,
  BusinessOverride = 2,
  Stop = 3
};

struct FCrowdDemoGuidanceCandidate
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoGuidanceProvider Provider = ECrowdDemoGuidanceProvider::SharedFlow;
  int32 PlanRevision = 0;
  FVector PreferredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  float DesiredYawDegrees = 0.0f;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

struct FCrowdDemoComposedGuidance
{
  int32 AgentId = INDEX_NONE;
  ECrowdDemoGuidanceProvider SelectedProvider = ECrowdDemoGuidanceProvider::Stop;
  int32 PlanRevision = 0;
  FVector AutonomousPreferredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  float DesiredYawDegrees = 0.0f;
  uint32 CandidateSetHash = 2166136261u;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSAICROWDDEMO_API FCrowdDemoGuidanceComposeKernel
{
public:
  static FCrowdDemoGuidanceCandidate BuildCandidate(
    int32 AgentId,
    ECrowdDemoGuidanceProvider Provider,
    int32 PlanRevision,
    const FVector& PreferredVelocity,
    const FVector& DesiredLocation,
    float DesiredYawDegrees,
    bool bValid);

  static FCrowdDemoComposedGuidance Compose(
    int32 AgentId,
    int32 PlanRevision,
    TConstArrayView<FCrowdDemoGuidanceCandidate> Candidates,
    const FVector& StopLocation,
    float StopYawDegrees);
};
