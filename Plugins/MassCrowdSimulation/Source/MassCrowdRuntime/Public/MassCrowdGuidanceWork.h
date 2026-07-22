#pragma once

#include "CoreMinimal.h"
#include "MassCrowdRuntimeBridge.h"

struct FCrowdMassGuidanceWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassGatherRecord> Records;
};

struct FCrowdMassGuidanceWorkOutput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdComposedGuidance> ComposedGuidance;
  uint32 StableHash = 2166136261u;
  bool bValid = false;
};

class MASSCROWDRUNTIME_API FCrowdMassGuidanceWork
{
public:
  static FCrowdMassGuidanceWorkOutput Compose(
    const FCrowdMassGuidanceWorkInput& Input);
};
