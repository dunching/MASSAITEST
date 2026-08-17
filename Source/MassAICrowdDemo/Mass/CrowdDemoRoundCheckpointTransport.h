#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

class MASSAICROWDDEMO_API FCrowdDemoRoundCheckpointTransport
{
public:
  static bool BuildChunks(
    const FCrowdDemoRoundCheckpointFrame& Frame,
    int32 ChunkSize,
    FCrowdDemoRoundCheckpointHeader& OutHeader,
    TArray<FCrowdDemoRoundCheckpointChunk>& OutChunks);

  static bool TryAssemble(
    const FCrowdDemoRoundCheckpointHeader& Header,
    TConstArrayView<FCrowdDemoRoundCheckpointChunk> Chunks,
    TArray<FCrowdDemoRoundAgentState>& OutAgents);
};
