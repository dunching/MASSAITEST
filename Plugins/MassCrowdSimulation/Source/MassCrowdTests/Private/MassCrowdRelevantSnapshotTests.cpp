#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "MassCrowdRelevantSnapshot.h"

namespace
{
  FCrowdRelevantSnapshotLimits MakeSnapshotLimits()
  {
    FCrowdRelevantSnapshotLimits Limits;
    Limits.MaxEntityCount = 64;
    Limits.MaxChunkCount = 32;
    Limits.MaxEntitiesPerChunk = 3;
    Limits.MaxChunkPayloadBytes = 24;
    Limits.MaxTotalPayloadBytes = 512;
    Limits.AssemblyTimeoutSeconds = 2.0;
    return Limits;
  }

  TArray<FCrowdRelevantSnapshotEntityPayload> MakeSnapshotEntities(
    const int32 Count)
  {
    TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
    for (int32 EntityIndex = 0; EntityIndex < Count; ++EntityIndex)
    {
      FCrowdRelevantSnapshotEntityPayload& Entity =
        Entities.AddDefaulted_GetRef();
      Entity.Bytes = {
        static_cast<uint8>(EntityIndex),
        static_cast<uint8>(EntityIndex * 3 + 1),
        static_cast<uint8>(255 - EntityIndex)};
    }
    return Entities;
  }

  bool PayloadsEqual(
    const TArray<FCrowdRelevantSnapshotEntityPayload>& A,
    const TArray<FCrowdRelevantSnapshotEntityPayload>& B)
  {
    return A == B;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRelevantSnapshotBuildAssembleTest,
  "MassCrowd.Networking.RelevantSnapshot.BuildAssemble",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRelevantSnapshotBuildAssembleTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdRelevantSnapshotEntityPayload> Entities =
    MakeSnapshotEntities(7);
  FCrowdRelevantSnapshotLimits Limits = MakeSnapshotLimits();
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  TestTrue(TEXT("bounded snapshot builds"),
    FCrowdRelevantSnapshotTransport::Build(
      5, 120, 9, Entities, Limits, Header, Chunks));
  TestTrue(TEXT("snapshot uses multiple chunks"), Chunks.Num() > 1);
  TestEqual(TEXT("header entity count"), Header.EntityCount, Entities.Num());

  FCrowdRelevantSnapshotLimits RechunkLimits = Limits;
  RechunkLimits.MaxEntitiesPerChunk = 1;
  FCrowdRelevantSnapshotHeader RechunkedHeader;
  TArray<FCrowdRelevantSnapshotChunk> RechunkedChunks;
  TestTrue(TEXT("same snapshot can be rechunked"),
    FCrowdRelevantSnapshotTransport::Build(
      5, 120, 9, Entities, RechunkLimits,
      RechunkedHeader, RechunkedChunks));
  TestTrue(TEXT("snapshot hash ignores chunk layout"),
    Header.SnapshotHash == RechunkedHeader.SnapshotHash
      && Header.ChunkCount != RechunkedHeader.ChunkCount);

  FCrowdRelevantSnapshotAssembly Assembly;
  TestTrue(TEXT("assembly accepts expected revision"), Assembly.Begin(5, Limits));
  Algo::Reverse(Chunks);
  TestTrue(TEXT("chunk may arrive before header"),
    Assembly.AcceptChunk(Chunks[0], 10.0)
      == ECrowdRelevantSnapshotAcceptResult::Accepted);
  TestTrue(TEXT("identical chunk is idempotent"),
    Assembly.AcceptChunk(Chunks[0], 10.1)
      == ECrowdRelevantSnapshotAcceptResult::Duplicate);
  TestTrue(TEXT("header may arrive after a chunk"),
    Assembly.AcceptHeader(Header, 10.2)
      == ECrowdRelevantSnapshotAcceptResult::Accepted);
  for (int32 ChunkIndex = 1; ChunkIndex < Chunks.Num(); ++ChunkIndex)
  {
    const ECrowdRelevantSnapshotAcceptResult Result =
      Assembly.AcceptChunk(Chunks[ChunkIndex], 10.2 + ChunkIndex * 0.1);
    TestTrue(TEXT("remaining reversed chunk accepted"),
      Result == ECrowdRelevantSnapshotAcceptResult::Accepted
        || Result == ECrowdRelevantSnapshotAcceptResult::Complete);
  }
  TestTrue(TEXT("assembly reports complete"), Assembly.IsComplete());
  TArray<FCrowdRelevantSnapshotEntityPayload> Assembled;
  TestTrue(TEXT("assembly finalizes"), Assembly.TryFinalize(11.0, Assembled));
  TestTrue(TEXT("entity payload order is restored"),
    PayloadsEqual(Assembled, Entities));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRelevantSnapshotValidationTest,
  "MassCrowd.Networking.RelevantSnapshot.Validation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRelevantSnapshotValidationTest::RunTest(
  const FString& Parameters)
{
  const TArray<FCrowdRelevantSnapshotEntityPayload> Entities =
    MakeSnapshotEntities(4);
  const FCrowdRelevantSnapshotLimits Limits = MakeSnapshotLimits();
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  TestTrue(TEXT("validation fixture builds"),
    FCrowdRelevantSnapshotTransport::Build(
      6, 200, 12, Entities, Limits, Header, Chunks));

  FCrowdRelevantSnapshotAssembly Assembly;
  TestTrue(TEXT("validation assembly begins"), Assembly.Begin(6, Limits));
  FCrowdRelevantSnapshotChunk Stale = Chunks[0];
  Stale.SnapshotRevision = 5;
  Stale.ChunkHash = FCrowdRelevantSnapshotTransport::CalculateChunkHash(Stale);
  TestTrue(TEXT("stale revision rejected without poisoning expected revision"),
    Assembly.AcceptChunk(Stale, 1.0)
      == ECrowdRelevantSnapshotAcceptResult::Rejected
      && !Assembly.HasFailed());
  TestTrue(TEXT("valid chunk accepted after stale packet"),
    Assembly.AcceptChunk(Chunks[0], 1.1)
      == ECrowdRelevantSnapshotAcceptResult::Accepted);

  FCrowdRelevantSnapshotChunk Conflict = Chunks[0];
  Conflict.Payload.Last() ^= 1;
  Conflict.ChunkHash = FCrowdRelevantSnapshotTransport::CalculateChunkHash(Conflict);
  TestTrue(TEXT("conflicting duplicate rejects and poisons revision"),
    Assembly.AcceptChunk(Conflict, 1.2)
      == ECrowdRelevantSnapshotAcceptResult::Rejected
      && Assembly.HasFailed());

  FCrowdRelevantSnapshotAssembly CorruptAssembly;
  TestTrue(TEXT("corrupt assembly begins"), CorruptAssembly.Begin(6, Limits));
  TestTrue(TEXT("header accepted"),
    CorruptAssembly.AcceptHeader(Header, 2.0)
      == ECrowdRelevantSnapshotAcceptResult::Accepted);
  TArray<FCrowdRelevantSnapshotChunk> CorruptChunks = Chunks;
  CorruptChunks.Last().Payload.Last() ^= 1;
  CorruptChunks.Last().ChunkHash =
    FCrowdRelevantSnapshotTransport::CalculateChunkHash(CorruptChunks.Last());
  for (const FCrowdRelevantSnapshotChunk& Chunk : CorruptChunks)
  {
    CorruptAssembly.AcceptChunk(Chunk, 2.1);
  }
  TArray<FCrowdRelevantSnapshotEntityPayload> Output;
  TestFalse(TEXT("snapshot hash rejects coherently rehashed corrupt chunk"),
    CorruptAssembly.TryFinalize(2.2, Output));
  TestTrue(TEXT("snapshot hash failure poisons revision"),
    CorruptAssembly.HasFailed());

  FCrowdRelevantSnapshotEntityPayload Oversized;
  Oversized.Bytes.SetNumZeroed(Limits.MaxChunkPayloadBytes);
  FCrowdRelevantSnapshotHeader InvalidHeader;
  TArray<FCrowdRelevantSnapshotChunk> InvalidChunks;
  TestFalse(TEXT("entity larger than bounded chunk is rejected"),
    FCrowdRelevantSnapshotTransport::Build(
      7, 1, 1, MakeArrayView(&Oversized, 1), Limits,
      InvalidHeader, InvalidChunks));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdRelevantSnapshotTimeoutEmptyTest,
  "MassCrowd.Networking.RelevantSnapshot.TimeoutEmptyBounds",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdRelevantSnapshotTimeoutEmptyTest::RunTest(
  const FString& Parameters)
{
  const FCrowdRelevantSnapshotLimits Limits = MakeSnapshotLimits();
  const TArray<FCrowdRelevantSnapshotEntityPayload> Entities =
    MakeSnapshotEntities(2);
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  TestTrue(TEXT("timeout fixture builds"),
    FCrowdRelevantSnapshotTransport::Build(
      8, 20, 3, Entities, Limits, Header, Chunks));

  FCrowdRelevantSnapshotAssembly TimedOut;
  TestTrue(TEXT("timeout assembly begins"), TimedOut.Begin(8, Limits));
  TestTrue(TEXT("first chunk starts timeout"),
    TimedOut.AcceptChunk(Chunks[0], 10.0)
      == ECrowdRelevantSnapshotAcceptResult::Accepted);
  TestTrue(TEXT("late header reports timeout"),
    TimedOut.AcceptHeader(Header, 12.1)
      == ECrowdRelevantSnapshotAcceptResult::TimedOut);
  TArray<FCrowdRelevantSnapshotEntityPayload> Output;
  TestFalse(TEXT("timed-out assembly cannot finalize"),
    TimedOut.TryFinalize(12.1, Output));

  FCrowdRelevantSnapshotHeader EmptyHeader;
  TArray<FCrowdRelevantSnapshotChunk> EmptyChunks;
  TestTrue(TEXT("empty relevant set builds"),
    FCrowdRelevantSnapshotTransport::Build(
      9, 21, 4, {}, Limits, EmptyHeader, EmptyChunks));
  TestTrue(TEXT("empty relevant set has no chunks"), EmptyChunks.IsEmpty());
  FCrowdRelevantSnapshotAssembly EmptyAssembly;
  TestTrue(TEXT("empty assembly begins"), EmptyAssembly.Begin(9, Limits));
  TestTrue(TEXT("empty header completes assembly"),
    EmptyAssembly.AcceptHeader(EmptyHeader, 20.0)
      == ECrowdRelevantSnapshotAcceptResult::Complete);
  TestTrue(TEXT("complete empty set does not time out"),
    EmptyAssembly.TryFinalize(100.0, Output) && Output.IsEmpty());

  FCrowdRelevantSnapshotLimits InvalidLimits = Limits;
  InvalidLimits.MaxChunkPayloadBytes = 0;
  TestFalse(TEXT("invalid caller limits rejected"),
    EmptyAssembly.Begin(10, InvalidLimits));
  return true;
}
