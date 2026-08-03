#include "Misc/AutomationTest.h"

#include "MassCrowdReplicationChannel.h"
#include "MassCrowdWorkerPacketTransport.h"
#include "MassCrowdWorkerReplicationAdapter.h"
#include "MassCrowdWorkerReplicationCodec.h"

namespace
{
  FCrowdReplicationChannelLimits MakeReplicationLimits()
  {
    FCrowdReplicationChannelLimits Limits;
    Limits.MaxReliableRecordsPerBatch = 8;
    Limits.MaxReliablePayloadBytesPerRecord = 64;
    Limits.MaxBufferedReliableRecords = 2;
    Limits.MaxCorrectionRecords = 8;
    Limits.SnapshotLimits.MaxEntityCount = 16;
    Limits.SnapshotLimits.MaxChunkCount = 16;
    Limits.SnapshotLimits.MaxEntitiesPerChunk = 1;
    Limits.SnapshotLimits.MaxChunkPayloadBytes = 32;
    Limits.SnapshotLimits.MaxTotalPayloadBytes = 256;
    Limits.SnapshotLimits.AssemblyTimeoutSeconds = 2.0;
    return Limits;
  }

  FCrowdReliableStateRecord MakeReliable(const uint64 Sequence)
  {
    FCrowdReliableStateRecord Record;
    Record.Sequence = Sequence;
    Record.Kind = ECrowdReliableStateKind::BehaviorSourceSet;
    Record.EntityRef = {1, Sequence, 1};
    Record.Revision = 3;
    Record.Payload = {static_cast<uint8>(Sequence), 7};
    Record.StableHash =
      FCrowdReplicationTransport::CalculateReliableRecordHash(Record);
    return Record;
  }

  FCrowdWorkerPayload MakeWorkerPayload(const uint32 Value)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = 0x57413701u;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(
      Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdReplicationBaselineSequenceTest,
  "MassCrowd.Networking.Replication.BaselineSequence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdReplicationBaselineSequenceTest::RunTest(
  const FString& Parameters)
{
  const FCrowdReplicationChannelLimits Limits = MakeReplicationLimits();
  TArray<FCrowdRelevantSnapshotEntityPayload> Entities;
  Entities.Add({{1, 2, 3}});
  Entities.Add({{4, 5}});
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  TestTrue(TEXT("baseline snapshot builds"),
    FCrowdRelevantSnapshotTransport::Build(
      7, 20, 4, Entities, Limits.SnapshotLimits, Header, Chunks));
  FCrowdBaselineBegin Begin{7, 11, 1234};
  FCrowdBaselineEnd End{7, 11, Header.SnapshotHash};

  FCrowdReplicationClientState Client;
  TestTrue(TEXT("client initializes"), Client.Initialize(Limits));
  TestTrue(TEXT("baseline begins"),
    Client.AcceptBaselineBegin(Begin, 1.0)
      == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("baseline begin duplicate is idempotent"),
    Client.AcceptBaselineBegin(Begin, 1.1)
      == ECrowdReplicationAcceptResult::Duplicate);
  for (const FCrowdRelevantSnapshotChunk& Chunk : Chunks)
    TestTrue(TEXT("snapshot chunks accepted"),
      Client.AcceptSnapshotChunk(Chunk, 1.2)
        == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("header accepted after chunks"),
    Client.AcceptSnapshotHeader(Header, 1.3)
      == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("baseline ends only after complete snapshot"),
    Client.AcceptBaselineEnd(End, 1.4)
      == ECrowdReplicationAcceptResult::BaselineComplete);

  TArray<FCrowdRelevantSnapshotEntityPayload> Restored;
  uint32 Revision = 0;
  uint64 Resume = 0;
  TestTrue(TEXT("baseline can be consumed"),
    Client.ConsumeCompletedBaseline(Restored, Revision, Resume));
  TestTrue(TEXT("baseline restores exact entities"),
    Restored == Entities && Revision == 7 && Resume == 11);

  FCrowdReliableStateBatch Batch;
  Batch.FirstSequence = 11;
  Batch.Records = {MakeReliable(11), MakeReliable(12)};
  Batch.StableHash =
    FCrowdReplicationTransport::CalculateReliableBatchHash(Batch);
  TestTrue(TEXT("contiguous reliable batch applies"),
    Client.AcceptReliableBatch(Batch)
      == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("identical reliable batch is idempotent"),
    Client.AcceptReliableBatch(Batch)
      == ECrowdReplicationAcceptResult::Duplicate);

  FCrowdMovementCorrectionRecord Correction;
  Correction.EntityRef = {1, 1, 1};
  Correction.Sequence = 5;
  Correction.FixedStepIndex = 20;
  Correction.Position = FVector(10.0, 20.0, 30.0);
  Correction.Velocity = FVector(1.0, 2.0, 0.0);
  Correction.YawDegrees = 45.0f;
  Correction.StableHash =
    FCrowdReplicationTransport::CalculateMovementCorrectionHash(Correction);
  FCrowdMovementCorrectionRecord SecondCorrection = Correction;
  SecondCorrection.EntityRef = {1, 2, 1};
  SecondCorrection.Position.X += 25.0;
  SecondCorrection.StableHash =
    FCrowdReplicationTransport::CalculateMovementCorrectionHash(
      SecondCorrection);
  const FCrowdMovementCorrectionRecord CorrectionBatch[] = {
    Correction, SecondCorrection};
  TestTrue(TEXT("movement correction batch applies atomically"),
    Client.AcceptMovementCorrections(CorrectionBatch)
      == ECrowdReplicationAcceptResult::Accepted);
  FCrowdMovementCorrectionRecord Stale = Correction;
  Stale.Sequence = 4;
  Stale.StableHash =
    FCrowdReplicationTransport::CalculateMovementCorrectionHash(Stale);
  TestTrue(TEXT("older correction is latest-wins ignored"),
    Client.AcceptMovementCorrection(Stale)
      == ECrowdReplicationAcceptResult::IgnoredStaleCorrection);
  TArray<FCrowdReplicationApplyFrame> Frames;
  TestTrue(TEXT("baseline state and correction drain once"),
    Client.DrainApplyFrames(Frames));
  TestEqual(TEXT("three atomic apply frames drained"), Frames.Num(), 3);
  TestTrue(TEXT("runtime baseline is ordered before state and presentation"),
    Frames[0].Kind == ECrowdReplicationApplyFrameKind::Baseline
      && Frames[1].Kind
        == ECrowdReplicationApplyFrameKind::ReliableState
      && Frames[2].Kind
        == ECrowdReplicationApplyFrameKind::MovementCorrection);
  TestEqual(TEXT("one apply frame retains the whole correction batch"),
    Frames[2].Corrections.Num(), 2);
  TestFalse(TEXT("drained history is not scanned again"),
    Client.DrainApplyFrames(Frames));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdReplicationResyncBufferTest,
  "MassCrowd.Networking.Replication.ResyncAndBoundedBuffer",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdReplicationResyncBufferTest::RunTest(
  const FString& Parameters)
{
  const FCrowdReplicationChannelLimits Limits = MakeReplicationLimits();
  FCrowdRelevantSnapshotHeader Header;
  TArray<FCrowdRelevantSnapshotChunk> Chunks;
  const TArray<FCrowdRelevantSnapshotEntityPayload> Entities = {{{1}}};
  TestTrue(TEXT("server fixture snapshot builds"),
    FCrowdRelevantSnapshotTransport::Build(
      8, 1, 1, Entities, Limits.SnapshotLimits, Header, Chunks));
  const FCrowdBaselineBegin Begin{8, 20, 222};
  FCrowdReplicationServerState Server;
  TestTrue(TEXT("server initializes"), Server.Initialize(Limits));
  TestTrue(TEXT("server begins awaiting ack"),
    Server.BeginBaseline(Begin, Header) && Server.IsAwaitingAck());
  TestTrue(TEXT("server buffers first expected sequence"),
    Server.BufferReliable(MakeReliable(20)));
  TestFalse(TEXT("sequence gap forces a new baseline"),
    Server.BufferReliable(MakeReliable(22)));
  TestTrue(TEXT("gap state is fail closed"), Server.RequiresNewBaseline());

  FCrowdReplicationClientState Client;
  TestTrue(TEXT("resync client initializes"), Client.Initialize(Limits));
  TestTrue(TEXT("resync baseline begins"),
    Client.AcceptBaselineBegin(Begin, 1.0)
      == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("resync header accepted"),
    Client.AcceptSnapshotHeader(Header, 1.1)
      == ECrowdReplicationAcceptResult::Accepted);
  TestTrue(TEXT("conflicting baseline end requests resync"),
    Client.AcceptBaselineEnd({8, 20, Header.SnapshotHash + 1}, 1.2)
      == ECrowdReplicationAcceptResult::ResyncRequired);
  TestTrue(TEXT("client remains fail closed"), Client.RequiresResync());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdReplicationCodecTest,
  "MassCrowd.Networking.Replication.VersionedCodecs",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdReplicationCodecTest::RunTest(
  const FString& Parameters)
{
  TArray<uint8> Bytes;

  FCrowdAgentReplicationRecord Agent;
  Agent.EntityRef = {1, 42, 3};
  Agent.Position = FVector(10.0, 20.0, 30.0);
  Agent.Velocity = FVector(1.0, 2.0, 3.0);
  Agent.YawDegrees = 45.0f;
  Agent.MovementProfileKey = 7;
  Agent.CapabilityProfileKey = {1};
  Agent.CapabilityModifierRevision = 2;
  Agent.SourceSetRevision = 4;
  Agent.SourceSetHash = 0x1234;
  Agent.ResolvedBehaviorHash = 0x5678;
  Agent.DerivedDiagnosticLabel = 4;
  Agent.Revision = 9;
  FCrowdAgentReplicationRecord DecodedAgent;
  TestTrue(TEXT("agent codec encodes"),
    FCrowdReplicationCodec::EncodeAgent(Agent, Bytes));
  TestTrue(TEXT("agent codec round trips"),
    FCrowdReplicationCodec::DecodeAgent(Bytes, DecodedAgent));
  TestTrue(TEXT("agent fields remain exact"),
    DecodedAgent.EntityRef == Agent.EntityRef
      && DecodedAgent.Position == Agent.Position
      && DecodedAgent.Velocity == Agent.Velocity
      && DecodedAgent.YawDegrees == Agent.YawDegrees
      && DecodedAgent.MovementProfileKey == Agent.MovementProfileKey
      && DecodedAgent.CapabilityProfileKey
        == Agent.CapabilityProfileKey
      && DecodedAgent.CapabilityModifierRevision
        == Agent.CapabilityModifierRevision
      && DecodedAgent.SourceSetRevision == Agent.SourceSetRevision
      && DecodedAgent.SourceSetHash == Agent.SourceSetHash
      && DecodedAgent.ResolvedBehaviorHash
        == Agent.ResolvedBehaviorHash
      && DecodedAgent.DerivedDiagnosticLabel
        == Agent.DerivedDiagnosticLabel
      && DecodedAgent.Revision == Agent.Revision);
  TArray<uint8> V1Agent = Bytes;
  V1Agent[0] = 1;
  V1Agent[1] = 0;
  TestFalse(TEXT("v1 agent payload is explicitly rejected"),
    FCrowdReplicationCodec::DecodeAgent(V1Agent, DecodedAgent));

  FCrowdBehaviorSourceCommand SourceCommand;
  SourceCommand.EffectiveFixedStep = 20;
  SourceCommand.Handle = {{1, 42, 3}, {7}, 8};
  SourceCommand.CommandSequence = 9;
  SourceCommand.Kind = ECrowdBehaviorSourceCommandKind::Start;
  SourceCommand.SourceTypeId = {1001};
  SourceCommand.Priority = 30;
  SourceCommand.LifetimeSteps = 12;
  const uint32 SourcePayloadValue = 77;
  SourceCommand.Payload.Set(5, SourcePayloadValue);
  constexpr uint64 RegistryHash = 0x1122334455667788ull;
  constexpr uint64 ContextSchemaHash = 0x8877665544332211ull;
  FCrowdBehaviorSourceCommandReplicationRecord SourceCommandRecord;
  SourceCommandRecord.RegistryHash = RegistryHash;
  SourceCommandRecord.ContextSchemaHash = ContextSchemaHash;
  SourceCommandRecord.StateSchemaId = 7001;
  SourceCommandRecord.Command = SourceCommand;
  FCrowdBehaviorSourceCommandReplicationRecord DecodedSourceCommand;
  TestTrue(TEXT("source command codec encodes"),
    FCrowdReplicationCodec::EncodeBehaviorSourceCommand(
      SourceCommandRecord, Bytes));
  TestTrue(TEXT("source command codec round trips"),
    FCrowdReplicationCodec::DecodeBehaviorSourceCommand(
      Bytes, RegistryHash, ContextSchemaHash, DecodedSourceCommand));
  TestEqual(TEXT("source handle preserves controller identity"),
    DecodedSourceCommand.Command.Handle.ControllerId.Value, 7u);
  TestEqual(TEXT("source command sequence round trips"),
    DecodedSourceCommand.Command.CommandSequence, 9u);
  TestEqual(TEXT("state schema accompanies source command"),
    DecodedSourceCommand.StateSchemaId, 7001u);
  TestFalse(TEXT("registry mismatch requests command resync"),
    FCrowdReplicationCodec::DecodeBehaviorSourceCommand(
      Bytes, RegistryHash + 1, ContextSchemaHash, DecodedSourceCommand));
  TArray<uint8> V2SourceCommand = Bytes;
  V2SourceCommand[0] = 2;
  V2SourceCommand[1] = 0;
  TestFalse(TEXT("v2 behavior command is explicitly rejected"),
    FCrowdReplicationCodec::DecodeBehaviorSourceCommand(
      V2SourceCommand, RegistryHash, ContextSchemaHash,
      DecodedSourceCommand));

  FCrowdBehaviorSourceSetReplicationRecord SourceSetRecord;
  SourceSetRecord.RegistryHash = RegistryHash;
  SourceSetRecord.ContextSchemaHash = ContextSchemaHash;
  SourceSetRecord.SourceSet.EntityRef = {1, 42, 3};
  SourceSetRecord.SourceSet.CapabilityBinding.ProfileKey = {1};
  SourceSetRecord.SourceSet.Revision = 2;
  FCrowdBehaviorSourceInstance& Instance =
    SourceSetRecord.SourceSet.Instances.AddDefaulted_GetRef();
  Instance.Handle = SourceCommand.Handle;
  Instance.SourceTypeId = SourceCommand.SourceTypeId;
  Instance.SourceVersion = 1;
  Instance.Priority = 30;
  Instance.StartFixedStep = 20;
  Instance.LastUpdateFixedStep = 20;
  Instance.ExpireFixedStep = 32;
  Instance.ReplicationPolicy =
    ECrowdBehaviorSourceReplicationPolicy::Predictable;
  Instance.Payload = SourceCommand.Payload;
  const uint32 PersistentStateValue = 1234;
  Instance.State.Set(7001, PersistentStateValue);
  SourceSetRecord.SourceSet.ControllerCursors.Add(
    {{7}, 9, SourceCommand.CalculateStableHash()});
  SourceSetRecord.SourceSet.RecalculateStableHash();
  SourceSetRecord.ResolvedBehaviorHash = 0x9999;
  SourceSetRecord.DerivedDiagnosticLabel = 4;
  FCrowdBehaviorSourceSetReplicationRecord DecodedSourceSet;
  TestTrue(TEXT("source set baseline codec encodes"),
    FCrowdReplicationCodec::EncodeBehaviorSourceSet(
      SourceSetRecord, Bytes));
  TestTrue(TEXT("source set baseline codec round trips"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      Bytes, RegistryHash, ContextSchemaHash, DecodedSourceSet));
  TestEqual(TEXT("source set hash round trips"),
    DecodedSourceSet.SourceSet.StableHash,
    SourceSetRecord.SourceSet.StableHash);
  TestEqual(TEXT("resolved behavior hash round trips"),
    DecodedSourceSet.ResolvedBehaviorHash,
    SourceSetRecord.ResolvedBehaviorHash);
  uint32 DecodedPersistentState = 0;
  TestTrue(TEXT("persistent source state round trips"),
    DecodedSourceSet.SourceSet.Instances[0].State.Get(
      7001, DecodedPersistentState));
  TestEqual(TEXT("persistent state remains exact"),
    DecodedPersistentState, PersistentStateValue);
  TestFalse(TEXT("context schema mismatch requests source-set resync"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      Bytes, RegistryHash, ContextSchemaHash + 1, DecodedSourceSet));
  TArray<uint8> V2SourceSet = Bytes;
  V2SourceSet[0] = 2;
  V2SourceSet[1] = 0;
  TestFalse(TEXT("v2 behavior source set is explicitly rejected"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      V2SourceSet, RegistryHash, ContextSchemaHash, DecodedSourceSet));

  FCrowdTaskReplicationRecord Task{
    {2, 1, 1}, {2, 2, 1}, {2, 3, 1}, {2, 4, 1},
    5, 2, 11};
  FCrowdTaskReplicationRecord DecodedTask;
  TestTrue(TEXT("task codec encodes"),
    FCrowdReplicationCodec::EncodeTask(Task, Bytes));
  TestTrue(TEXT("task codec decodes"),
    FCrowdReplicationCodec::DecodeTask(Bytes, DecodedTask));
  TestTrue(TEXT("task fields remain exact"),
    DecodedTask.TaskRef == Task.TaskRef
      && DecodedTask.CarrierRef == Task.CarrierRef
      && DecodedTask.Quantity == Task.Quantity
      && DecodedTask.Revision == Task.Revision);

  FCrowdInventoryReplicationRecord Inventory{
    {3, 1, 1}, 10, 2, 3, 4, 20, 6};
  FCrowdInventoryReplicationRecord DecodedInventory;
  TestTrue(TEXT("inventory codec encodes"),
    FCrowdReplicationCodec::EncodeInventory(Inventory, Bytes));
  TestTrue(TEXT("inventory codec decodes"),
    FCrowdReplicationCodec::DecodeInventory(Bytes, DecodedInventory));
  TestTrue(TEXT("inventory fields remain exact"),
    DecodedInventory.OwnerRef == Inventory.OwnerRef
      && DecodedInventory.InTransit == Inventory.InTransit
      && DecodedInventory.Capacity == Inventory.Capacity);

  FCrowdCargoReplicationRecord Cargo{
    {4, 1, 1}, {4, 2, 1}, {4, 3, 1}, {4, 4, 1},
    8, 3, 12};
  FCrowdCargoReplicationRecord DecodedCargo;
  TestTrue(TEXT("cargo codec encodes"),
    FCrowdReplicationCodec::EncodeCargo(Cargo, Bytes));
  TestTrue(TEXT("cargo codec decodes"),
    FCrowdReplicationCodec::DecodeCargo(Bytes, DecodedCargo));
  TestTrue(TEXT("cargo fields remain exact"),
    DecodedCargo.CargoRef == Cargo.CargoRef
      && DecodedCargo.CarrierRef == Cargo.CarrierRef
      && DecodedCargo.State == Cargo.State);

  FCrowdPresentationReplicationRecord Presentation;
  Presentation.EntityRef = {5, 1, 1};
  Presentation.CargoRef = Cargo.CargoRef;
  Presentation.Location = FVector(4.0, 5.0, 6.0);
  Presentation.YawDegrees = 90.0f;
  Presentation.ProfileKey = 17;
  Presentation.VisualState = 3;
  Presentation.Revision = 21;
  FCrowdPresentationReplicationRecord DecodedPresentation;
  TestTrue(TEXT("presentation codec encodes"),
    FCrowdReplicationCodec::EncodePresentation(Presentation, Bytes));
  TestTrue(TEXT("presentation codec decodes"),
    FCrowdReplicationCodec::DecodePresentation(
      Bytes, DecodedPresentation));
  TestTrue(TEXT("presentation fields remain exact"),
    DecodedPresentation.EntityRef == Presentation.EntityRef
      && DecodedPresentation.CargoRef == Presentation.CargoRef
      && DecodedPresentation.Location == Presentation.Location
      && DecodedPresentation.VisualState == Presentation.VisualState);

  Bytes[0] = 1;
  TestFalse(TEXT("v1 codec version fails closed"),
    FCrowdReplicationCodec::DecodePresentation(
      Bytes, DecodedPresentation));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerPacketTransportTest,
  "MassCrowd.Networking.Replication.WorkerPacketTransport",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerPacketTransportTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdWorkerPacketTransportConfig Config;
  Config.MaxChunkBytes = 3;
  Config.MaxPacketBytes = 12;
  Config.MaxChunkCount = 4;
  Config.AssemblyTimeoutSeconds = 2.0;
  const TArray<uint8> Payload{1, 2, 3, 4, 5, 6, 7, 8};
  FCrowdWorkerPacketHeader Header;
  TArray<FCrowdWorkerPacketChunk> Chunks;
  FCrowdWorkerPacketEnd End;
  TestTrue(TEXT("worker packet builds"),
    FCrowdWorkerPacketTransport::Build(
      ECrowdWorkerPacketKind::Checkpoint,
      9, 4, 0x12345678ull, Payload, Config,
      Header, Chunks, End));
  TestEqual(TEXT("worker packet chunk count"), Chunks.Num(), 3);

  FCrowdWorkerPacketAssembler Assembler;
  TestTrue(TEXT("worker assembler initializes"),
    Assembler.Initialize(Config));
  TestEqual(TEXT("worker packet header accepted"),
    Assembler.AcceptHeader(Header, 10.0),
    ECrowdWorkerPacketAcceptResult::Accepted);
  TestEqual(TEXT("worker packet header duplicate idempotent"),
    Assembler.AcceptHeader(Header, 10.1),
    ECrowdWorkerPacketAcceptResult::Duplicate);
  TestEqual(TEXT("worker packet first chunk accepted"),
    Assembler.AcceptChunk(Chunks[0], 10.2),
    ECrowdWorkerPacketAcceptResult::Accepted);
  TestEqual(TEXT("worker packet chunk duplicate idempotent"),
    Assembler.AcceptChunk(Chunks[0], 10.3),
    ECrowdWorkerPacketAcceptResult::Duplicate);
  TestEqual(TEXT("worker packet out of order fails closed"),
    Assembler.AcceptChunk(Chunks[2], 10.4),
    ECrowdWorkerPacketAcceptResult::RequiresResync);

  TestTrue(TEXT("worker assembler resets after order failure"),
    Assembler.Initialize(Config));
  TestEqual(TEXT("worker packet header reaccepted"),
    Assembler.AcceptHeader(Header, 20.0),
    ECrowdWorkerPacketAcceptResult::Accepted);
  FCrowdWorkerPacketChunk Tampered = Chunks[0];
  Tampered.Bytes[0] ^= 0xff;
  TestEqual(TEXT("worker packet chunk hash fails closed"),
    Assembler.AcceptChunk(Tampered, 20.1),
    ECrowdWorkerPacketAcceptResult::Rejected);
  for (int32 Index = 0; Index < Chunks.Num(); ++Index)
    TestEqual(TEXT("worker packet ordered chunk accepted"),
      Assembler.AcceptChunk(Chunks[Index], 20.2 + Index * 0.1),
      ECrowdWorkerPacketAcceptResult::Accepted);
  TestEqual(TEXT("worker packet completes"),
    Assembler.AcceptEnd(End, 20.6),
    ECrowdWorkerPacketAcceptResult::Complete);
  FCrowdWorkerAssembledPacket Assembled;
  TestTrue(TEXT("worker packet consumes"),
    Assembler.ConsumeCompleted(Assembled));
  TestTrue(TEXT("worker packet bytes remain exact"),
    Assembled.Bytes == Payload);
  TestEqual(TEXT("worker packet identity remains exact"),
    Assembled.Header.ObjectStableHash, Header.ObjectStableHash);
  TestFalse(TEXT("worker packet consumes once"),
    Assembler.ConsumeCompleted(Assembled));

  TestTrue(TEXT("worker timeout assembler initializes"),
    Assembler.Initialize(Config));
  TestEqual(TEXT("worker timeout header accepted"),
    Assembler.AcceptHeader(Header, 30.0),
    ECrowdWorkerPacketAcceptResult::Accepted);
  TestEqual(TEXT("worker assembly timeout requires resync"),
    Assembler.AcceptChunk(Chunks[0], 32.1),
    ECrowdWorkerPacketAcceptResult::RequiresResync);

  FCrowdWorkerPacketTransportConfig TightConfig = Config;
  TightConfig.MaxPacketBytes = Payload.Num() - 1;
  TestFalse(TEXT("worker packet capacity fails closed"),
    FCrowdWorkerPacketTransport::Build(
      ECrowdWorkerPacketKind::Checkpoint,
      9, 4, 0x12345678ull, Payload, TightConfig,
      Header, Chunks, End));

  FCrowdWorkerPacketTransportConfig NetworkConfig;
  NetworkConfig.MaxPacketBytes = 64 * 1024;
  NetworkConfig.MaxChunkCount = 16;
  TArray<uint8> LargeCheckpoint;
  LargeCheckpoint.SetNumUninitialized(49 * 1024);
  for (int32 Index = 0; Index < LargeCheckpoint.Num(); ++Index)
    LargeCheckpoint[Index] = static_cast<uint8>(Index & 0xff);
  TestTrue(TEXT("49 KiB checkpoint builds as bounded reliable RPC chunks"),
    FCrowdWorkerPacketTransport::Build(
      ECrowdWorkerPacketKind::Checkpoint,
      10, 5, 0x87654321ull, LargeCheckpoint, NetworkConfig,
      Header, Chunks, End));
  TestEqual(TEXT("49 KiB checkpoint uses thirteen 4 KiB chunks"),
    Chunks.Num(), 13);
  for (const FCrowdWorkerPacketChunk& Chunk : Chunks)
    TestTrue(TEXT("worker network chunk stays within reliable RPC cap"),
      Chunk.Bytes.Num()
        <= FCrowdWorkerPacketTransportConfig::ReliableRpcSafeChunkBytes);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerReplicationCodecTest,
  "MassCrowd.Networking.Replication.WorkerCodec",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerReplicationCodecTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 83;
  const FCrowdStableEntityRef EntityRef{4, 900, 3};
  FCrowdWorkerNetworkStateConfig Config;
  Config.MaxStateRecordsPerCheckpoint = 8;
  Config.MaxResourceRecordsPerCheckpoint = 8;
  Config.MaxWorkItemsPerCheckpoint = 8;
  Config.MaxWakeupsPerCheckpoint = 8;
  Config.MaxDependencyEdgesPerCheckpoint = 8;
  Config.MaxCommandsPerCheckpoint = 8;
  Config.MaxLifecycleWatermarksPerCheckpoint = 8;
  Config.MaxPayloadBytes = 1024;
  Config.MaxEncodedCheckpointBytes = 64 * 1024;

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("codec state store resets"), States.Reset(8, 1024));
  TestEqual(TEXT("codec entity spawns"),
    States.Spawn(EntityRef, Generation, 1, MakeWorkerPayload(41)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("codec resource store resets"), Resources.Reset(1024));
  TestEqual(TEXT("codec resource stages"),
    Resources.StageBuilding({77, 5, MakeWorkerPayload(42)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("codec resource commits"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));
  TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
  TArray<FCrowdWorkerResourceRecord> CompleteResources;
  States.GetStateRecords(CompleteStates);
  Resources.GetCurrentRecords(CompleteResources);

  auto MakeHeader = [&States, &Resources](
    const uint64 Epoch,
    const uint64 InputSequence,
    const uint64 LastOrderedEventSequence)
  {
    FCrowdWorkerCheckpoint Header;
    Header.Generation = Generation;
    Header.WorkerEpoch = Epoch;
    Header.AbsoluteSimulationTick = Epoch;
    Header.FixedSimulationQuantumSeconds = 1.0 / 30.0;
    Header.LastAppliedInputSequence = InputSequence;
    Header.LastOrderedEventSequence = LastOrderedEventSequence;
    Header.EntityStateHash = States.CalculateStableHash();
    Header.ResourceRevisionHash =
      Resources.CalculateCurrentStableHash();
    Header.RecalculateStableHash();
    return Header;
  };
  FCrowdWorkerNetworkContinuationState Continuation;
  Continuation.WorkRing.Epoch = 2;
  Continuation.LifecycleWatermarks.Add({
    EntityRef.ProviderId,
    EntityRef.StableEntityId,
    EntityRef.LifecycleSerial});
  FCrowdWorkerNetworkStatePublisher Publisher;
  TestTrue(TEXT("codec publisher resets"),
    Publisher.Reset(Config, Generation));
  TestTrue(TEXT("codec baseline commits"),
    Publisher.CommitEpoch(
      MakeHeader(1, 1, 0),
      CompleteStates,
      CompleteResources,
      Continuation));
  FCrowdWorkerNetworkCheckpoint Checkpoint;
  TestEqual(TEXT("codec checkpoint reads"),
    Publisher.ReadCheckpoint(Generation, Checkpoint),
    ECrowdWorkerNetworkReadResult::Ready);

  TArray<uint8> CheckpointBytes;
  TestTrue(TEXT("checkpoint encodes"),
    FCrowdWorkerReplicationCodec::EncodeCheckpoint(
      Checkpoint, Config, CheckpointBytes));
  FCrowdWorkerNetworkCheckpoint DecodedCheckpoint;
  TestTrue(TEXT("checkpoint decodes"),
    FCrowdWorkerReplicationCodec::DecodeCheckpoint(
      CheckpointBytes, Config, DecodedCheckpoint));
  TestEqual(TEXT("checkpoint stable hash round trips"),
    DecodedCheckpoint.StableHash, Checkpoint.StableHash);
  TArray<uint8> ReencodedCheckpoint;
  TestTrue(TEXT("decoded checkpoint re-encodes"),
    FCrowdWorkerReplicationCodec::EncodeCheckpoint(
      DecodedCheckpoint, Config, ReencodedCheckpoint));
  TestTrue(TEXT("checkpoint wire form is deterministic"),
    ReencodedCheckpoint == CheckpointBytes);

  TArray<uint8> InvalidBytes = CheckpointBytes;
  InvalidBytes[0] ^= 0xff;
  TestFalse(TEXT("bad checkpoint magic fails closed"),
    FCrowdWorkerReplicationCodec::DecodeCheckpoint(
      InvalidBytes, Config, DecodedCheckpoint));
  InvalidBytes = CheckpointBytes;
  InvalidBytes.Last() ^= 0x01;
  TestFalse(TEXT("checkpoint hash tamper fails closed"),
    FCrowdWorkerReplicationCodec::DecodeCheckpoint(
      InvalidBytes, Config, DecodedCheckpoint));

  FCrowdWorkerNetworkStateConfig TightConfig = Config;
  TightConfig.MaxEncodedCheckpointBytes = CheckpointBytes.Num() - 1;
  TestFalse(TEXT("checkpoint byte capacity fails closed"),
    FCrowdWorkerReplicationCodec::DecodeCheckpoint(
      CheckpointBytes, TightConfig, DecodedCheckpoint));
  FCrowdWorkerIntentBatch Intent;
  Intent.Generation = Generation;
  Intent.FirstInputSequence = 10;
  Intent.LastInputSequence = 11;
  Intent.TargetSimulationTimeSeconds = 10.0 / 30.0;
  FCrowdWorkerExternalGameplayInput External;
  External.InputSequence = 10;
  External.EntityRef = EntityRef;
  External.InputTypeId = static_cast<uint16>(
    ECrowdWorkerExternalGameplayInputType::GameplayFact);
  External.DirtyMask = 1;
  External.FullState = MakeWorkerPayload(91);
  Intent.ExternalGameplayInputs.Add(External);
  Intent.Clock.InputSequence = 11;
  Intent.Clock.SimulationTick = 10;
  Intent.RecalculateStableHash();
  TArray<uint8> IntentBytes;
  TestTrue(TEXT("intent encodes without entity authority state"),
    FCrowdWorkerReplicationCodec::EncodeIntent(
      Intent, Config, IntentBytes));
  FCrowdWorkerIntentBatch DecodedIntent;
  TestTrue(TEXT("intent decodes"),
    FCrowdWorkerReplicationCodec::DecodeIntent(
      IntentBytes, Config, DecodedIntent));
  TestEqual(TEXT("intent stable hash round trips"),
    DecodedIntent.StableHash, Intent.StableHash);
  TestEqual(TEXT("clock tick round trips"),
    DecodedIntent.Clock.SimulationTick, uint64{10});
  TArray<uint8> InvalidIntent = IntentBytes;
  InvalidIntent.Last() ^= 1;
  TestFalse(TEXT("intent hash tamper fails closed"),
    FCrowdWorkerReplicationCodec::DecodeIntent(
      InvalidIntent, Config, DecodedIntent));
  FCrowdWorkerIntentBatch UnsupportedIntent = Intent;
  ++UnsupportedIntent.Version;
  UnsupportedIntent.RecalculateStableHash();
  TestFalse(TEXT("unsupported intent version is rejected"),
    FCrowdWorkerReplicationCodec::EncodeIntent(
      UnsupportedIntent, Config, InvalidIntent));

  FCrowdWorkerIntentBatch ClockOnly;
  ClockOnly.Generation = Generation;
  ClockOnly.FirstInputSequence = 12;
  ClockOnly.LastInputSequence = 12;
  ClockOnly.TargetSimulationTimeSeconds = 11.0 / 30.0;
  ClockOnly.Clock.InputSequence = 12;
  ClockOnly.Clock.SimulationTick = 11;
  ClockOnly.RecalculateStableHash();
  TArray<uint8> OneThousandEntityIntentBytes;
  TArray<uint8> TenThousandEntityIntentBytes;
  TestTrue(TEXT("1k unchanged world clock intent encodes"),
    FCrowdWorkerReplicationCodec::EncodeIntent(
      ClockOnly, Config, OneThousandEntityIntentBytes));
  TestTrue(TEXT("10k unchanged world clock intent encodes"),
    FCrowdWorkerReplicationCodec::EncodeIntent(
      ClockOnly, Config, TenThousandEntityIntentBytes));
  TestTrue(TEXT("unchanged intent cost is entity-count invariant"),
    TenThousandEntityIntentBytes.Num()
      <= 1.10 * OneThousandEntityIntentBytes.Num() + 1024.0);

  FCrowdWorkerAuthorityDigestBatch Digest;
  Digest.Generation = Generation;
  Digest.DigestSequence = 1;
  Digest.SimulationTick = 30;
  Digest.ThroughInputSequence = 11;
  Digest.Entries.Add({
    {ECrowdWorkerField::InputSnapshot,
      ECrowdWorkerAuthorityScopeKind::Global, 0},
    30, 11, 1, 0x1234ull});
  Digest.RecalculateStableHash();
  TestTrue(TEXT("authority digest contract validates"),
    Digest.IsValid(Config));

  FCrowdWorkerAuthorityDigestInbox DigestInbox;
  TestTrue(TEXT("first digest is accepted"),
    DigestInbox.Offer(FCrowdWorkerAuthorityDigestBatch{Digest}));
  TestEqual(TEXT("first digest is pending"),
    DigestInbox.Peek()->DigestSequence, uint64{1});
  DigestInbox.Consume();

  FCrowdWorkerAuthorityDigestBatch DigestThree = Digest;
  DigestThree.DigestSequence = 3;
  DigestThree.SimulationTick = 90;
  DigestThree.ThroughInputSequence = 13;
  DigestThree.Entries[0].SimulationTick = 90;
  DigestThree.Entries[0].ThroughInputSequence = 13;
  DigestThree.RecalculateStableHash();
  TestTrue(TEXT("digest loss does not create a sequence gap"),
    DigestInbox.Offer(MoveTemp(DigestThree)));

  FCrowdWorkerAuthorityDigestBatch LateDigest = Digest;
  LateDigest.DigestSequence = 2;
  LateDigest.SimulationTick = 60;
  LateDigest.ThroughInputSequence = 12;
  LateDigest.Entries[0].SimulationTick = 60;
  LateDigest.Entries[0].ThroughInputSequence = 12;
  LateDigest.RecalculateStableHash();
  TestFalse(TEXT("late out-of-order digest is rejected"),
    DigestInbox.Offer(MoveTemp(LateDigest)));
  TestEqual(TEXT("late digest does not replace newer pending digest"),
    DigestInbox.Peek()->DigestSequence, uint64{3});

  FCrowdWorkerAuthorityDigestBatch DigestFour = Digest;
  DigestFour.DigestSequence = 4;
  DigestFour.SimulationTick = 120;
  DigestFour.ThroughInputSequence = 14;
  DigestFour.Entries[0].SimulationTick = 120;
  DigestFour.Entries[0].ThroughInputSequence = 14;
  DigestFour.RecalculateStableHash();
  TestTrue(TEXT("newer digest supersedes pending digest"),
    DigestInbox.Offer(MoveTemp(DigestFour)));
  TestEqual(TEXT("latest digest remains pending"),
    DigestInbox.Peek()->DigestSequence, uint64{4});
  DigestInbox.Consume();

  FCrowdWorkerAuthorityDigestBatch DuplicateFour = Digest;
  DuplicateFour.DigestSequence = 4;
  DuplicateFour.SimulationTick = 120;
  DuplicateFour.ThroughInputSequence = 14;
  DuplicateFour.Entries[0].SimulationTick = 120;
  DuplicateFour.Entries[0].ThroughInputSequence = 14;
  DuplicateFour.RecalculateStableHash();
  TestFalse(TEXT("consumed digest sequence cannot arrive again"),
    DigestInbox.Offer(MoveTemp(DuplicateFour)));
  DigestInbox.Reset();
  TestTrue(TEXT("resync reset accepts a new digest sequence baseline"),
    DigestInbox.Offer(FCrowdWorkerAuthorityDigestBatch{Digest}));

  FCrowdWorkerAuthorityCorrectionBatch Correction;
  Correction.Generation = Generation;
  Correction.CorrectionSequence = 1;
  Correction.ApplySimulationTick = 30;
  Correction.ThroughInputSequence = 11;
  Correction.Scopes.Add(Digest.Entries[0].Scope);
  Correction.AuthoritativeMembers.Add(EntityRef);
  FCrowdWorkerDirtyStateRecord Corrected = CompleteStates[0];
  Corrected.WorkerEpoch = 30;
  Corrected.StateRevision = 30;
  Corrected.CorrectionRevision = 1;
  Corrected.SourceInputSequence = 11;
  Corrected.Payload = MakeWorkerPayload(92);
  Correction.Records.Add(Corrected);
  Correction.Tombstones.Add({
    FCrowdStableEntityRef{4, 901, 1},
    ECrowdWorkerField::Presentation});
  Correction.RecalculateStableHash();
  TArray<uint8> CorrectionBytes;
  TestTrue(TEXT("sparse correction encodes"),
    FCrowdWorkerReplicationCodec::EncodeCorrection(
      Correction, Config, CorrectionBytes));
  FCrowdWorkerAuthorityCorrectionBatch DecodedCorrection;
  TestTrue(TEXT("sparse correction decodes"),
    FCrowdWorkerReplicationCodec::DecodeCorrection(
      CorrectionBytes, Config, DecodedCorrection));
  TestEqual(TEXT("correction has no continuation payload"),
    DecodedCorrection.Records.Num(), 1);
  TestEqual(TEXT("correction tombstone round trips"),
    DecodedCorrection.Tombstones.Num(), 1);
  TestEqual(TEXT("correction stable hash round trips"),
    DecodedCorrection.StableHash, Correction.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerReplicationOrderTest,
  "MassCrowd.Networking.Replication.WorkerCheckpointOrder",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerReplicationOrderTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  constexpr uint64 Generation = 71;
  const FCrowdStableEntityRef EntityRef{3, 10, 1};
  FCrowdWorkerNetworkStateConfig Config;
  Config.MaxStateRecordsPerCheckpoint = 16;
  Config.MaxResourceRecordsPerCheckpoint = 4;

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("late join state store resets"),
    States.Reset(8, 1024));
  TestEqual(TEXT("late join entity spawns"),
    States.Spawn(EntityRef, Generation, 1, MakeWorkerPayload(1)),
    ECrowdWorkerQueueResult::Added);
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("late join resource store resets"),
    Resources.Reset(1024));
  TestEqual(TEXT("late join resource stages"),
    Resources.StageBuilding({90, 1, MakeWorkerPayload(2)}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("late join resource commits"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));

  TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
  TArray<FCrowdWorkerResourceRecord> CompleteResources;
  States.GetStateRecords(CompleteStates);
  Resources.GetCurrentRecords(CompleteResources);
  FCrowdWorkerCheckpoint Header;
  Header.Generation = Generation;
  Header.WorkerEpoch = 1;
  Header.AbsoluteSimulationTick = 1;
  Header.FixedSimulationQuantumSeconds = 1.0 / 30.0;
  Header.LastAppliedInputSequence = 1;
  Header.EntityStateHash = States.CalculateStableHash();
  Header.ResourceRevisionHash =
    Resources.CalculateCurrentStableHash();
  Header.RecalculateStableHash();
  FCrowdWorkerNetworkContinuationState Continuation;
  Continuation.WorkRing.Epoch = 2;
  Continuation.LifecycleWatermarks.Add({
    EntityRef.ProviderId,
    EntityRef.StableEntityId,
    EntityRef.LifecycleSerial});

  FCrowdWorkerNetworkStatePublisher Publisher;
  TestTrue(TEXT("late join publisher resets"),
    Publisher.Reset(Config, Generation));
  TestTrue(TEXT("late join checkpoint commits"),
    Publisher.CommitEpoch(
      Header, CompleteStates, CompleteResources, Continuation));
  FCrowdWorkerNetworkCheckpoint Baseline;
  TestEqual(TEXT("late join checkpoint reads"),
    Publisher.ReadCheckpoint(Generation, Baseline),
    ECrowdWorkerNetworkReadResult::Ready);
  TestEqual(TEXT("checkpoint input waterline is explicit"),
    Baseline.InputBaselineSequence,
    Header.LastAppliedInputSequence);

  FCrowdWorkerReplicationClientGate Client;
  TestTrue(TEXT("late join gate initializes"),
    Client.Initialize(Config));
  TestEqual(TEXT("resources before checkpoint are rejected"),
    Client.AcceptResourceRevisions(
      Baseline.StableHash, Baseline.ResourceRecords),
    ECrowdWorkerReplicationAcceptResult::RejectedOrder);
  TestEqual(TEXT("checkpoint accepted"),
    Client.AcceptCheckpoint(Baseline),
    ECrowdWorkerReplicationAcceptResult::Accepted);
  TestEqual(TEXT("event baseline before resources is rejected"),
    Client.AcceptEventBaseline(
      Baseline.StableHash, Baseline.EventBaselineSequence),
    ECrowdWorkerReplicationAcceptResult::RejectedOrder);
  TestEqual(TEXT("resource baseline accepted"),
    Client.AcceptResourceRevisions(
      Baseline.StableHash, Baseline.ResourceRecords),
    ECrowdWorkerReplicationAcceptResult::Accepted);
  TestEqual(TEXT("event baseline makes client live"),
    Client.AcceptEventBaseline(
      Baseline.StableHash, Baseline.EventBaselineSequence),
    ECrowdWorkerReplicationAcceptResult::BaselineReady);
  FCrowdWorkerNetworkCheckpoint Consumed;
  TestTrue(TEXT("completed checkpoint consumes once"),
    Client.ConsumeReadyCheckpoint(Consumed));
  TestFalse(TEXT("checkpoint cannot consume twice"),
    Client.ConsumeReadyCheckpoint(Consumed));

  FCrowdWorkerIntentBatch Intent;
  Intent.Generation = Generation;
  Intent.FirstInputSequence = 2;
  Intent.LastInputSequence = 3;
  Intent.TargetSimulationTimeSeconds = 2.0 / 30.0;
  FCrowdWorkerExternalGameplayInput External;
  External.InputSequence = 2;
  External.EntityRef = EntityRef;
  External.InputTypeId = static_cast<uint16>(
    ECrowdWorkerExternalGameplayInputType::GameplayFact);
  External.DirtyMask = 1;
  External.FullState = MakeWorkerPayload(3);
  Intent.ExternalGameplayInputs.Add(External);
  Intent.Clock.InputSequence = 3;
  Intent.Clock.SimulationTick = 2;
  Intent.RecalculateStableHash();

  FCrowdWorkerInputSequenceGate SequenceGate;
  TestTrue(TEXT("post-checkpoint intent gate resets"),
    SequenceGate.ResetForResnapshot(Generation, 2));
  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 1024;
  Limits.MaxInputRecordsPerBatch = 16;
  Limits.MaxStatePatchesPerSlot = 16;
  Limits.MaxPendingOrderedEvents = 16;
  TestEqual(TEXT("first live intent accepted"),
    SequenceGate.Accept(Intent, Limits),
    ECrowdWorkerInputAcceptResult::Accepted);
  TestEqual(TEXT("duplicate live intent is idempotent"),
    SequenceGate.Accept(Intent, Limits),
    ECrowdWorkerInputAcceptResult::AcceptedDuplicate);

  FCrowdWorkerIntentBatch Gap = Intent;
  Gap.FirstInputSequence = 5;
  Gap.LastInputSequence = 6;
  Gap.ExternalGameplayInputs[0].InputSequence = 5;
  Gap.Clock.InputSequence = 6;
  Gap.Clock.SimulationTick = 3;
  Gap.TargetSimulationTimeSeconds = 3.0 / 30.0;
  Gap.RecalculateStableHash();
  TestEqual(TEXT("intent gap requires checkpoint"),
    SequenceGate.Accept(Gap, Limits),
    ECrowdWorkerInputAcceptResult::RequiresResnapshot);
  TestTrue(TEXT("intent gap latches resnapshot"),
    SequenceGate.RequiresResnapshot());
  return true;
}
