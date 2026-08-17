#include "MassCrowdFacingFinalizeWork.h"

namespace
{
  constexpr uint32 FnvOffset = 2166136261u;
  constexpr uint32 FnvPrime = 16777619u;

  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
      Hash ^= static_cast<uint8>((Value >> (Byte * 8)) & 0xffu);
      Hash *= FnvPrime;
    }
    return Hash;
  }
}

FCrowdMassFacingFinalizeWorkOutput FCrowdMassFacingFinalizeWork::Run(
  const FCrowdMassFacingFinalizeWorkInput& Input)
{
  FCrowdMassFacingFinalizeWorkOutput Output;
  if (!Input.Snapshot.bValid
    || Input.Facing.FixedStepIndex != Input.Snapshot.FixedStepIndex
    || Input.Facing.PlanRevision != Input.Snapshot.PlanRevision
    || Input.Facing.Agents.Num() != Input.Snapshot.Agents.Num()
    || Input.Kinematics.Num() != Input.Snapshot.Agents.Num())
    return Output;

  Output.Facing = FCrowdMassFacingWork::Resolve(Input.Facing);
  if (!Output.Facing.bCompleted)
    return Output;

  FCrowdMassMovementFinalizeWorkInput FinalizeInput;
  if (!FCrowdMassMovementFinalizeWork::BuildInputFromPrepared(
      Input.Snapshot, Input.Kinematics, Output.Facing.Summary.Results,
      FinalizeInput, Output.CommitTargets))
    return Output;

  Output.Finalize =
    FCrowdMassMovementFinalizeWork::BuildCommitPlan(FinalizeInput);
  if (!Output.Finalize.bCompleted
    || !FCrowdMassRuntimeBridge::ValidateCommitTargets(
      Output.Finalize.CommitPlan, Output.CommitTargets))
    return Output;

  uint32 Hash = Fold(FnvOffset, 1u);
  Hash = Fold(Hash, static_cast<uint32>(Input.Snapshot.FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint32>(Input.Snapshot.PlanRevision));
  Hash = Fold(Hash, static_cast<uint32>(Input.Snapshot.StableHash));
  Hash = Fold(Hash, static_cast<uint32>(Input.Snapshot.StableHash >> 32));
  Hash = Fold(Hash, Output.Facing.StableHash);
  Hash = Fold(Hash, Output.Finalize.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}
