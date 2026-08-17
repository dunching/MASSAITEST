#pragma once

#include "CoreMinimal.h"
#include "MassCrowdSimulationTypes.h"

struct FCrowdComposedGuidance
{
  int32 AgentId = INDEX_NONE;
  ECrowdGuidanceProvider SelectedProvider = ECrowdGuidanceProvider::Stop;
  int32 PlanRevision = 0;
  FVector AutonomousPreferredVelocity = FVector::ZeroVector;
  FVector DesiredLocation = FVector::ZeroVector;
  float DesiredYawDegrees = 0.0f;
  uint32 CandidateSetHash = 2166136261u;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSCROWDCORE_API FCrowdGuidanceComposeKernel
{
public:
  static FCrowdGuidanceCandidate BuildCandidate(
    int32 AgentId,
    ECrowdGuidanceProvider Provider,
    int32 PlanRevision,
    const FVector& PreferredVelocity,
    const FVector& DesiredLocation,
    float DesiredYawDegrees,
    bool bValid);

  static FCrowdComposedGuidance Compose(
    int32 AgentId,
    int32 PlanRevision,
    TConstArrayView<FCrowdGuidanceCandidate> Candidates,
    const FVector& StopLocation,
    float StopYawDegrees);
};
