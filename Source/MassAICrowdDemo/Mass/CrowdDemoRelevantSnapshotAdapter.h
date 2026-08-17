#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"
#include "MassCrowdRelevantSnapshot.h"

class MASSAICROWDDEMO_API FCrowdDemoRelevantSnapshotAdapter
{
public:
  static constexpr uint16 AgentPayloadVersion = 1;

  static FCrowdRelevantSnapshotLimits MakeLimits();

  static bool EncodeAgents(
    TConstArrayView<FCrowdDemoRoundAgentState> Agents,
    TArray<FCrowdRelevantSnapshotEntityPayload>& OutPayloads);

  static bool DecodeAgents(
    TConstArrayView<FCrowdRelevantSnapshotEntityPayload> Payloads,
    TArray<FCrowdDemoRoundAgentState>& OutAgents);
};
