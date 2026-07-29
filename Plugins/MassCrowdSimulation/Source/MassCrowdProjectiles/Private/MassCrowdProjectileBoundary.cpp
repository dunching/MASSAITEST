#include "MassCrowdProjectileBoundary.h"

namespace
{
  constexpr uint64 ProjectileBoundaryFnvOffset =
    14695981039346656037ull;
  constexpr uint64 ProjectileBoundaryFnvPrime = 1099511628211ull;

  void FoldProjectileBoundaryHash(
    uint64& Hash, const uint64 Value)
  {
    for (int32 Byte = 0; Byte < 8; ++Byte)
    {
      Hash ^= static_cast<uint8>(Value >> (Byte * 8));
      Hash *= ProjectileBoundaryFnvPrime;
    }
  }
}

bool FCrowdPreparedProjectileBoundary::IsValid() const
{
  if (FixedStepIndex < 0 || BaseStateHash == 0
    || !Summary.bValid || StableHash == 0)
    return false;
  FCrowdPreparedProjectileBoundary Copy = *this;
  Copy.RecalculateStableHash();
  return Copy.StableHash == StableHash;
}

void FCrowdPreparedProjectileBoundary::RecalculateStableHash()
{
  uint64 Hash = ProjectileBoundaryFnvOffset;
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(FixedStepIndex));
  FoldProjectileBoundaryHash(Hash, BaseStateHash);
  FoldProjectileBoundaryHash(
    Hash, FCrowdProjectileKernel::HashStates(States));
  FoldProjectileBoundaryHash(
    Hash, FCrowdProjectileKernel::HashEvents(Events));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Impacts.Num()));
  for (const FCrowdImpactFact& Impact : Impacts)
    FoldProjectileBoundaryHash(Hash, Impact.StableHash);
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.SpawnedCount));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.ActiveCount));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.ImpactedCount));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.ExpiredCount));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.BroadphaseCandidateCount));
  FoldProjectileBoundaryHash(
    Hash, static_cast<uint64>(Summary.SweepTestCount));
  StableHash = Hash == 0 ? 1 : Hash;
}

bool FCrowdProjectileBoundaryPipeline::Prepare(
  const FCrowdProjectileBoundaryInput& Input,
  FCrowdPreparedProjectileBoundary& OutPrepared)
{
  OutPrepared = {};
  if (Input.FixedStepIndex < 0
    || !FMath::IsFinite(Input.ServerTimeSeconds)
    || !FMath::IsFinite(Input.FixedStepSeconds)
    || Input.FixedStepSeconds <= 0.0f)
    return false;
  OutPrepared.FixedStepIndex = Input.FixedStepIndex;
  OutPrepared.BaseStateHash =
    FCrowdProjectileKernel::HashStates(Input.CurrentStates);
  OutPrepared.States = Input.CurrentStates;
  if (!FCrowdProjectileKernel::Spawn(
      Input.FixedStepIndex, Input.ServerTimeSeconds,
      Input.Profiles, Input.SpawnRequests,
      OutPrepared.States, OutPrepared.Events,
      OutPrepared.Summary)
    || !FCrowdProjectileKernel::Advance(
      Input.FixedStepIndex, Input.ServerTimeSeconds,
      Input.FixedStepSeconds, Input.Profiles, Input.Targets,
      Input.EnvironmentBodies, OutPrepared.States,
      OutPrepared.Impacts, OutPrepared.Events,
      OutPrepared.Summary))
  {
    OutPrepared = {};
    return false;
  }
  OutPrepared.RecalculateStableHash();
  return OutPrepared.IsValid();
}

bool FCrowdProjectileBoundaryPipeline::ValidatePrepared(
  const FCrowdProjectileBoundaryInput& Input,
  const FCrowdPreparedProjectileBoundary& Prepared)
{
  return Prepared.IsValid()
    && Prepared.FixedStepIndex == Input.FixedStepIndex
    && Prepared.BaseStateHash
      == FCrowdProjectileKernel::HashStates(Input.CurrentStates);
}
