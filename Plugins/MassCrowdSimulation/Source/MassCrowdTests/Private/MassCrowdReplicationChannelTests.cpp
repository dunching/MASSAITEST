#include "Misc/AutomationTest.h"

#include "MassCrowdReplicationChannel.h"

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
  FCrowdBehaviorSourceCommand DecodedSourceCommand;
  TestTrue(TEXT("source command codec encodes"),
    FCrowdReplicationCodec::EncodeBehaviorSourceCommand(
      SourceCommand, Bytes));
  TestTrue(TEXT("source command codec round trips"),
    FCrowdReplicationCodec::DecodeBehaviorSourceCommand(
      Bytes, DecodedSourceCommand));
  TestEqual(TEXT("source handle preserves controller identity"),
    DecodedSourceCommand.Handle.ControllerId.Value, 7u);
  TestEqual(TEXT("source command sequence round trips"),
    DecodedSourceCommand.CommandSequence, 9u);

  FCrowdBehaviorSourceSetReplicationRecord SourceSetRecord;
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
      Bytes, DecodedSourceSet));
  TestEqual(TEXT("source set hash round trips"),
    DecodedSourceSet.SourceSet.StableHash,
    SourceSetRecord.SourceSet.StableHash);
  TestEqual(TEXT("resolved behavior hash round trips"),
    DecodedSourceSet.ResolvedBehaviorHash,
    SourceSetRecord.ResolvedBehaviorHash);

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
