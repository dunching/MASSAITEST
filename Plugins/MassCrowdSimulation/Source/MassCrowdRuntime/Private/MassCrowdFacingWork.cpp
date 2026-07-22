#include "MassCrowdFacingWork.h"

namespace
{
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

FCrowdMassFacingWorkOutput FCrowdMassFacingWork::Resolve(
  const FCrowdMassFacingWorkInput& Input)
{
  FCrowdMassFacingWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0
    || Input.Agents.IsEmpty())
    return Output;
  FCrowdFacingKernel::Resolve(Input.Agents, Input.Settings, Output.Summary);
  if (!Output.Summary.bValid
    || Output.Summary.Results.Num() != Input.Agents.Num())
    return Output;
  uint32 Hash = Fold(2166136261u, 1u);
  Hash = Fold(Hash, static_cast<uint32>(Input.FixedStepIndex));
  Hash = Fold(Hash, static_cast<uint32>(Input.PlanRevision));
  Hash = Fold(Hash, Output.Summary.StableHash);
  Output.StableHash = Hash;
  Output.bCompleted = true;
  return Output;
}
