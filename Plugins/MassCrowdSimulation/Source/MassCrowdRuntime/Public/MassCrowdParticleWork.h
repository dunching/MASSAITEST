#pragma once

#include "CoreMinimal.h"
#include "CrowdParticleConstraintKernel.h"

struct FCrowdMassParticleWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdParticleConstraintEnvironment Environment;
  FCrowdParticleConstraintSettings Settings;
  TArray<FCrowdParticleConstraintAgent> Agents;
  bool bCaptureTrace = false;
};

struct FCrowdMassParticleWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdParticleConstraintPair> Pairs;
  TArray<FCrowdParticleConstraintResult> Results;
  FCrowdParticleConstraintSummary Summary;
  FCrowdParticleConstraintSummary AppliedSummary;
  FCrowdParticleConstraintTrace Trace;
  uint32 AppliedStateHash = 2166136261u;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassParticleWork
{
public:
  static FCrowdMassParticleWorkOutput Solve(
    const FCrowdMassParticleWorkInput& Input);
};
