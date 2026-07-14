#pragma once

#include "CoreMinimal.h"
#include "CrowdDemoTypes.h"

class MASSAICROWDDEMO_API FCrowdDemoRoundCheckpointTransport
{
public:
  static void BuildChunks(
    const FCrowdDemoCorrectionFrame& Frame,
    int32 ChunkSize,
    FCrowdDemoCorrectionFrameHeader& OutHeader,
    TArray<FCrowdDemoCorrectionFrameChunk>& OutChunks);

  static bool TryAssemble(
    const FCrowdDemoCorrectionFrameHeader& Header,
    TConstArrayView<FCrowdDemoCorrectionFrameChunk> Chunks,
    TArray<FCrowdDemoRoundAgentState>& OutAgents);
};
