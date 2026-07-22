#pragma once

#include "CoreMinimal.h"
#include "MassCrowdRuntimeBridge.h"

struct FCrowdMassFinalKinematicState
{
  int32 AgentId = INDEX_NONE;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  bool bValid = false;
};

struct FCrowdMassMovementFinalizeRecord
{
  int32 AgentId = INDEX_NONE;
  uint32 LifecycleSerial = 0;
  uint32 CapabilityProfileKey = 0;
  FVector Position = FVector::ZeroVector;
  FVector Velocity = FVector::ZeroVector;
  float YawDegrees = 0.0f;
};

struct FCrowdMassMovementFinalizeWorkInput
{
  int32 FixedStepIndex = INDEX_NONE;
  int32 PlanRevision = INDEX_NONE;
  TArray<FCrowdMassMovementFinalizeRecord> Records;
};

struct FCrowdMassMovementFinalizeWorkOutput
{
  FCrowdMassCommitPlan CommitPlan;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

class MASSCROWDRUNTIME_API FCrowdMassMovementFinalizeWork
{
public:
  static bool BuildInputFromPrepared(
    const FCrowdMassBoundarySnapshot& Snapshot,
    TConstArrayView<FCrowdMassFinalKinematicState> Kinematics,
    TConstArrayView<FCrowdFacingResult> Facings,
    FCrowdMassMovementFinalizeWorkInput& OutInput,
    TArray<FCrowdMassCommitTarget>& OutTargets);

  static FCrowdMassMovementFinalizeWorkOutput BuildCommitPlan(
    const FCrowdMassMovementFinalizeWorkInput& Input);
};
