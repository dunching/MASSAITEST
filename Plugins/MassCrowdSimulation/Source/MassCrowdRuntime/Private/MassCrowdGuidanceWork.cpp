#include "MassCrowdGuidanceWork.h"

#include "CrowdGuidanceComposeKernel.h"

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }
}

FCrowdMassGuidanceWorkOutput FCrowdMassGuidanceWork::Compose(
  const FCrowdMassGuidanceWorkInput& Input)
{
  FCrowdMassGuidanceWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  Output.PlanRevision = Input.PlanRevision;
  if (Input.FixedStepIndex < 0 || Input.PlanRevision < 0) return Output;
  TArray<FCrowdMassGatherRecord> Records = Input.Records;
  Records.Sort([](const auto& A, const auto& B)
  {
    return A.Identity.AgentId < B.Identity.AgentId;
  });
  int32 PreviousAgentId = INDEX_NONE;
  uint32 Hash = Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex));
  for (const FCrowdMassGatherRecord& Record : Records)
  {
    if (Record.Identity.AgentId == INDEX_NONE
      || Record.Identity.AgentId <= PreviousAgentId
      || Record.Identity.LifecycleSerial <= 0
      || !Record.State.bInitialized)
      return Output;
    PreviousAgentId = Record.Identity.AgentId;
    const FCrowdGuidanceCandidate Candidates[] = {
      Record.Guidance.SharedFlow,
      Record.Guidance.TargetRegion,
      Record.Guidance.BusinessOverride};
    FCrowdComposedGuidance Result = FCrowdGuidanceComposeKernel::Compose(
      Record.Identity.AgentId,
      Input.PlanRevision,
      Candidates,
      Record.State.Position,
      Record.State.YawDegrees);
    if (!Result.bValid) return Output;
    Hash = Fold(Hash, Result.StableHash);
    Output.ComposedGuidance.Add(Result);
  }
  Output.StableHash = Hash;
  Output.bValid = !Records.IsEmpty()
    && Output.ComposedGuidance.Num() == Records.Num();
  return Output;
}
