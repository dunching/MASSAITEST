#include "Mass/CrowdDemoRoundWorkKernel.h"

namespace
{
  uint32 Fold(uint32 Hash, const uint32 Value)
  {
    Hash ^= Value;
    return Hash * 16777619u;
  }
}

FCrowdDemoRoundWorkOutput FCrowdDemoRoundWorkKernel::ComposeGuidance(
  const FCrowdDemoRoundWorkInput& Input)
{
  FCrowdDemoRoundWorkOutput Output;
  Output.FixedStepIndex = Input.FixedStepIndex;
  TArray<FCrowdDemoRoundWorkAgentInput> StableAgents = Input.Agents;
  StableAgents.Sort([](const auto& A, const auto& B)
  {
    return A.AgentId < B.AgentId;
  });
  uint32 Hash = Fold(2166136261u, static_cast<uint32>(Input.FixedStepIndex));
  int32 PreviousAgentId = INDEX_NONE;
  for (const FCrowdDemoRoundWorkAgentInput& Agent : StableAgents)
  {
    if (Agent.AgentId == INDEX_NONE || Agent.AgentId == PreviousAgentId)
      return Output;
    PreviousAgentId = Agent.AgentId;
    const FCrowdDemoGuidanceCandidate Candidates[] = {
      Agent.SharedFlow, Agent.TargetRegion, Agent.BusinessOverride};
    FCrowdDemoComposedGuidance Result = FCrowdDemoGuidanceComposeKernel::Compose(
      Agent.AgentId, Agent.PlanRevision, Candidates,
      Agent.StopLocation, Agent.StopYawDegrees);
    if (!Result.bValid) return Output;
    Hash = Fold(Hash, Result.StableHash);
    Output.ComposedGuidance.Add(Result);
  }
  Output.StableHash = Hash;
  Output.bValid = Output.ComposedGuidance.Num() == StableAgents.Num();
  return Output;
}
