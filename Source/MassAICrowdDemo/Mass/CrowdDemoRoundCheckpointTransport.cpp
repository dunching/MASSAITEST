#include "Mass/CrowdDemoRoundCheckpointTransport.h"

void FCrowdDemoRoundCheckpointTransport::BuildChunks(
  const FCrowdDemoCorrectionFrame& Frame,
  const int32 ChunkSize,
  FCrowdDemoCorrectionFrameHeader& OutHeader,
  TArray<FCrowdDemoCorrectionFrameChunk>& OutChunks)
{
  const int32 SafeChunkSize = FMath::Max(1, ChunkSize);
  const int32 ChunkCount = FMath::DivideAndRoundUp(Frame.AgentStates.Num(), SafeChunkSize);
  OutHeader = FCrowdDemoCorrectionFrameHeader();
  OutHeader.bValid = Frame.bValid;
  OutHeader.FrameKind = Frame.FrameKind;
  OutHeader.CorrectionRevision = Frame.CorrectionRevision;
  OutHeader.RoundId = Frame.RoundId;
  OutHeader.RoundRevision = Frame.RoundRevision;
  OutHeader.SourceCheckpointRevision = Frame.SourceCheckpointRevision;
  OutHeader.ServerTimeSeconds = Frame.ServerTimeSeconds;
  OutHeader.AgentCount = Frame.AgentStates.Num();
  OutHeader.ChunkCount = ChunkCount;
  OutHeader.ChunkSize = SafeChunkSize;
  OutHeader.CrowdState = Frame.CrowdState;

  OutChunks.Reset(ChunkCount);
  for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
  {
    const int32 StartAgentIndex = ChunkIndex * SafeChunkSize;
    const int32 AgentCountInChunk = FMath::Min(SafeChunkSize, Frame.AgentStates.Num() - StartAgentIndex);
    FCrowdDemoCorrectionFrameChunk& Chunk = OutChunks.AddDefaulted_GetRef();
    Chunk.bValid = 1;
    Chunk.StableKey = Frame.CorrectionRevision * 1000 + ChunkIndex;
    Chunk.CorrectionRevision = Frame.CorrectionRevision;
    Chunk.RoundId = Frame.RoundId;
    Chunk.RoundRevision = Frame.RoundRevision;
    Chunk.ChunkIndex = ChunkIndex;
    Chunk.StartAgentIndex = StartAgentIndex;
    Chunk.AgentCountInChunk = AgentCountInChunk;
    Chunk.Header = OutHeader;
    Chunk.Agents.Append(Frame.AgentStates.GetData() + StartAgentIndex, AgentCountInChunk);
  }
}

bool FCrowdDemoRoundCheckpointTransport::TryAssemble(
  const FCrowdDemoCorrectionFrameHeader& Header,
  const TConstArrayView<FCrowdDemoCorrectionFrameChunk> Chunks,
  TArray<FCrowdDemoRoundAgentState>& OutAgents)
{
  if (Header.bValid == 0 || Header.AgentCount < 0 || Header.ChunkCount < 0)
  {
    return false;
  }
  TArray<const FCrowdDemoCorrectionFrameChunk*> ByIndex;
  ByIndex.SetNumZeroed(Header.ChunkCount);
  for (const FCrowdDemoCorrectionFrameChunk& Chunk : Chunks)
  {
    if (Chunk.bValid == 0
      || Chunk.CorrectionRevision != Header.CorrectionRevision
      || Chunk.RoundId != Header.RoundId
      || Chunk.RoundRevision != Header.RoundRevision
      || Chunk.Header.FrameKind != Header.FrameKind
      || !ByIndex.IsValidIndex(Chunk.ChunkIndex)
      || Chunk.Agents.Num() != Chunk.AgentCountInChunk)
    {
      return false;
    }
    if (!ByIndex[Chunk.ChunkIndex])
    {
      ByIndex[Chunk.ChunkIndex] = &Chunk;
    }
  }
  OutAgents.SetNum(Header.AgentCount);
  int32 ReceivedAgents = 0;
  for (int32 ChunkIndex = 0; ChunkIndex < Header.ChunkCount; ++ChunkIndex)
  {
    const FCrowdDemoCorrectionFrameChunk* Chunk = ByIndex[ChunkIndex];
    if (!Chunk || Chunk->StartAgentIndex != ReceivedAgents
      || ReceivedAgents + Chunk->AgentCountInChunk > Header.AgentCount)
    {
      OutAgents.Reset();
      return false;
    }
    for (int32 Offset = 0; Offset < Chunk->AgentCountInChunk; ++Offset)
    {
      OutAgents[ReceivedAgents + Offset] = Chunk->Agents[Offset];
    }
    ReceivedAgents += Chunk->AgentCountInChunk;
  }
  if (ReceivedAgents != Header.AgentCount)
  {
    OutAgents.Reset();
    return false;
  }
  return true;
}
