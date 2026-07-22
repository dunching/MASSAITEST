#pragma once

#include "CoreMinimal.h"
#include "CrowdFacingKernel.h"

struct FCrowdMassFacingWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdFacingSettings Settings;
  TArray<FCrowdFacingInput> Agents;
};

struct FCrowdMassFacingWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  FCrowdFacingSummary Summary;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassFacingWork
{
public:
  static FCrowdMassFacingWorkOutput Resolve(
    const FCrowdMassFacingWorkInput& Input);
};
