#include "Mass/CrowdDemoRoundCheckpointTransport.h"

bool FCrowdDemoRoundCheckpointTransport::BuildChunks(
  const FCrowdDemoRoundCheckpointFrame& Frame,
  const int32 ChunkSize,
  FCrowdDemoRoundCheckpointHeader& OutHeader,
  TArray<FCrowdDemoRoundCheckpointChunk>& OutChunks)
{
  OutHeader = FCrowdDemoRoundCheckpointHeader();
  OutChunks.Reset();
  if (Frame.bValid == 0
    || Frame.StateFrameRevision <= 0
    || Frame.CheckpointRevision <= 0
    || Frame.AgentCount != Frame.AgentStates.Num())
  {
    return false;
  }

  const int32 SafeChunkSize = FMath::Max(1, ChunkSize);
  const int32 ChunkCount = FMath::DivideAndRoundUp(Frame.AgentStates.Num(), SafeChunkSize);
  OutHeader.bValid = Frame.bValid;
  OutHeader.StateFrameRevision = Frame.StateFrameRevision;
  OutHeader.RoundId = Frame.RoundId;
  OutHeader.RoundRevision = Frame.RoundRevision;
  OutHeader.CheckpointRevision = Frame.CheckpointRevision;
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
    FCrowdDemoRoundCheckpointChunk& Chunk = OutChunks.AddDefaulted_GetRef();
    Chunk.bValid = 1;
    Chunk.StableKey = Frame.StateFrameRevision * 1000 + ChunkIndex;
    Chunk.StateFrameRevision = Frame.StateFrameRevision;
    Chunk.RoundId = Frame.RoundId;
    Chunk.RoundRevision = Frame.RoundRevision;
    Chunk.ChunkIndex = ChunkIndex;
    Chunk.StartAgentIndex = StartAgentIndex;
    Chunk.AgentCountInChunk = AgentCountInChunk;
    Chunk.Header = OutHeader;
    Chunk.Agents.Append(Frame.AgentStates.GetData() + StartAgentIndex, AgentCountInChunk);
  }
  return true;
}

bool FCrowdDemoRoundCheckpointTransport::TryAssemble(
  const FCrowdDemoRoundCheckpointHeader& Header,
  const TConstArrayView<FCrowdDemoRoundCheckpointChunk> Chunks,
  TArray<FCrowdDemoRoundAgentState>& OutAgents)
{
  if (Header.bValid == 0 || Header.AgentCount < 0 || Header.ChunkCount < 0)
  {
    return false;
  }
  TArray<const FCrowdDemoRoundCheckpointChunk*> ByIndex;
  ByIndex.SetNumZeroed(Header.ChunkCount);
  for (const FCrowdDemoRoundCheckpointChunk& Chunk : Chunks)
  {
    if (Chunk.bValid == 0
      || Chunk.StateFrameRevision != Header.StateFrameRevision
      || Chunk.RoundId != Header.RoundId
      || Chunk.RoundRevision != Header.RoundRevision
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
    const FCrowdDemoRoundCheckpointChunk* Chunk = ByIndex[ChunkIndex];
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
