#pragma once

#include "CoreMinimal.h"
#include "MassCrowdFacingWork.h"
#include "MassCrowdMovementFinalizeWork.h"

struct FCrowdMassFacingFinalizeWorkInput
{
  FCrowdMassFacingWorkInput Facing;
  FCrowdMassBoundarySnapshot Snapshot;
  TArray<FCrowdMassFinalKinematicState> Kinematics;
};

struct FCrowdMassFacingFinalizeWorkOutput
{
  FCrowdMassFacingWorkOutput Facing;
  FCrowdMassMovementFinalizeWorkOutput Finalize;
  TArray<FCrowdMassCommitTarget> CommitTargets;
  uint32 StableHash = 2166136261u;
  bool bCompleted = false;
};

// Executes the strictly ordered Facing -> MovementFinalize dependency chain
// against one immutable boundary snapshot. The caller publishes no partial
// result unless the complete output and target set have been validated.
class MASSCROWDRUNTIME_API FCrowdMassFacingFinalizeWork
{
public:
  static FCrowdMassFacingFinalizeWorkOutput Run(
    const FCrowdMassFacingFinalizeWorkInput& Input);
};
