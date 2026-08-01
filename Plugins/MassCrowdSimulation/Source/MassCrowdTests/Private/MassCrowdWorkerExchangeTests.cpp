#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Async/Async.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "MassCrowdAsyncSimulationRuntime.h"
#include "MassCrowdRuntimeSubsystem.h"
#include "MassCrowdWorkerContracts.h"
#include "MassCrowdWorkerConsistencyDomains.h"
#include "MassCrowdWorkerExchange.h"
#include "MassCrowdWorkerMovementAuthority.h"
#include "MassCrowdWorkerResultApply.h"
#include "MassCrowdWorkerShadowSync.h"

namespace CrowdWorkerExchangeTests
{
  FCrowdWorkerContractLimits MakeLimits(
    const int32 MaxPatches = 10000,
    const int32 MaxEvents = 128)
  {
    FCrowdWorkerContractLimits Limits;
    Limits.MaxPayloadBytes = 64;
    Limits.MaxInputRecordsPerBatch = 10000;
    Limits.MaxStatePatchesPerSlot = MaxPatches;
    Limits.MaxPendingOrderedEvents = MaxEvents;
    return Limits;
  }

  FCrowdWorkerPayload MakePayload(
    const uint32 Value,
    const uint32 SchemaId = 1001)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = SchemaId;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(
      Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }

  FCrowdWorkerSpawnDelta MakeSpawn(
    const uint64 Sequence,
    const uint64 EntityId)
  {
    FCrowdWorkerSpawnDelta Delta;
    Delta.InputSequence = Sequence;
    Delta.EntityRef = {1, EntityId, 1};
    Delta.InitialState = MakePayload(
      static_cast<uint32>(EntityId), 2001);
    return Delta;
  }

  FCrowdWorkerCommandDelta MakeCommand(
    const uint64 Sequence,
    const uint64 EntityId)
  {
    FCrowdWorkerCommandDelta Delta;
    Delta.InputSequence = Sequence;
    Delta.EntityRef = {1, EntityId, 1};
    Delta.CommandId = 3001;
    Delta.EffectiveSimulationTimeSeconds =
      static_cast<double>(Sequence) / 30.0;
    Delta.Payload = MakePayload(
      static_cast<uint32>(Sequence), 3002);
    return Delta;
  }

  FCrowdWorkerIntentBatch MakeInputBatch(
    const uint64 Generation,
    const uint64 FirstSequence,
    const int32 Count)
  {
    FCrowdWorkerIntentBatch Batch;
    Batch.Generation = Generation;
    Batch.TargetSimulationTimeSeconds =
      static_cast<double>(FirstSequence + Count) / 30.0;
    if (Count <= 0)
      return Batch;
    Batch.FirstInputSequence = FirstSequence;
    Batch.LastInputSequence = FirstSequence + Count - 1;
    for (int32 Index = 0; Index < Count - 1; ++Index)
    {
      const uint64 Sequence = FirstSequence + Index;
      if ((Index & 1) == 0)
        Batch.Spawns.Add(MakeSpawn(Sequence, 100 + Sequence));
      else
        Batch.Commands.Add(MakeCommand(Sequence, 100 + Sequence));
    }
    Batch.Clock.InputSequence = Batch.LastInputSequence;
    Batch.Clock.SimulationTick = FirstSequence + Count;
    Batch.RecalculateStableHash();
    return Batch;
  }

  FCrowdWorkerStatePatch MakePatch(
    const uint64 EntityId,
    const uint64 WorkerEpoch,
    const uint64 SourceInputSequence,
    const uint64 DirtyMask,
    const uint32 StateValue,
    const uint64 Generation = 7)
  {
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = {1, EntityId, 1};
    Patch.Generation = Generation;
    Patch.WorkerEpoch = WorkerEpoch;
    Patch.SourceInputSequence = SourceInputSequence;
    Patch.DirtyMask = DirtyMask;
    Patch.State.StateRevision = WorkerEpoch;
    Patch.State.Payload = MakePayload(StateValue, 4001);
    Patch.RecalculateStableHash();
    return Patch;
  }

  FCrowdWorkerGameplayEvent MakeEvent(
    const uint64 EventSequence,
    const uint64 WorkerEpoch = 1,
    const uint64 SourceInputSequence = 0,
    const uint64 Generation = 7)
  {
    FCrowdWorkerGameplayEvent Event;
    Event.EntityRef = {1, 10 + EventSequence, 1};
    Event.Generation = Generation;
    Event.WorkerEpoch = WorkerEpoch;
    Event.SourceInputSequence = SourceInputSequence;
    Event.EventSequence = EventSequence;
    Event.EventId = 5000 + EventSequence;
    Event.Payload = MakePayload(
      static_cast<uint32>(EventSequence), 5001);
    Event.RecalculateStableHash();
    return Event;
  }

  FCrowdWorkerPublishMetadata MakeMetadata(
    const uint64 PublishSequence,
    const uint64 WorkerEpoch = 1,
    const uint64 LastAppliedInputSequence = 0,
    const uint64 Generation = 7)
  {
    FCrowdWorkerPublishMetadata Metadata;
    Metadata.Generation = Generation;
    Metadata.PublishSequence = PublishSequence;
    Metadata.MinWorkerEpoch = WorkerEpoch;
    Metadata.MaxWorkerEpoch = WorkerEpoch;
    Metadata.LastAppliedInputSequence =
      LastAppliedInputSequence;
    Metadata.PublishedSimulationTimeSeconds =
      static_cast<double>(WorkerEpoch) / 30.0;
    return Metadata;
  }

  bool ReadPayloadValue(
    const FCrowdWorkerPayload& Payload,
    uint32& OutValue)
  {
    if (Payload.Bytes.Num() != sizeof(OutValue))
      return false;
    FMemory::Memcpy(
      &OutValue, Payload.Bytes.GetData(), sizeof(OutValue));
    return true;
  }

  FCrowdWorkerMovementState MakeMovementState(
    const double TimeSeconds,
    const double PositionX,
    const float YawDegrees,
    const uint64 CorrectionRevision = 0)
  {
    FCrowdWorkerMovementState State;
    State.Position = FVector(PositionX, 2.0, 3.0);
    State.Velocity = FVector(30.0, 0.0, 0.0);
    State.YawDegrees = YawDegrees;
    State.SimulationTimeSeconds = TimeSeconds;
    State.CorrectionRevision = CorrectionRevision;
    return State;
  }

  FCrowdWorkerStatePatch MakeMovementPatch(
    const FCrowdStableEntityRef& EntityRef,
    const uint64 Epoch,
    const uint64 InputSequence,
    const uint64 StateRevision,
    const FCrowdWorkerMovementState& Movement,
    const uint64 Generation = 7)
  {
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = EntityRef;
    Patch.Generation = Generation;
    Patch.WorkerEpoch = Epoch;
    Patch.SourceInputSequence = InputSequence;
    Patch.DirtyMask = CrowdWorkerMovementFields::Movement;
    Patch.State.StateRevision = StateRevision;
    FCrowdWorkerMovementStateCodec::Encode(
      Movement, Patch.State.Payload);
    Patch.RecalculateStableHash();
    return Patch;
  }

  FCrowdAsyncSimulationRuntimeConfig MakeRuntimeConfig()
  {
    FCrowdAsyncSimulationRuntimeConfig Config;
    Config.ContractLimits = MakeLimits(10000, 128);
    Config.FixedSimulationQuantumSeconds = 1.0 / 30.0;
    Config.MaxQueuedInputBatches = 16;
    Config.MaxInputBatchesPerPump = 4;
    Config.MaxSimulationStepsPerPump = 8;
    Config.MaxPendingCommands = 128;
    Config.MaxInFlightShadowWorks = 16;
    return Config;
  }

  FCrowdMassBoundaryAgentRecord MakeBoundaryAgent(
    const int32 AgentId,
    const uint64 StableEntityId,
    const uint32 LifecycleSerial,
    const FVector& Position)
  {
    FCrowdMassBoundaryAgentRecord Record;
    Record.Identity.AgentId = AgentId;
    Record.Identity.SetStableEntityRef(
      {1, StableEntityId, LifecycleSerial});
    Record.AgentFacts.StableEntityRef =
      Record.Identity.GetStableEntityRef();
    Record.AgentFacts.CapabilitySet.Bits = 1;
    Record.AgentFacts.MovementProfileKey = 1;
    Record.AgentFacts.PresentationProfileKey = 1;
    Record.State.Position = Position;
    Record.State.Velocity = FVector(10.0, 0.0, 0.0);
    Record.State.YawDegrees = 0.0f;
    Record.State.PlanRevision = 1;
    Record.State.bInitialized = true;
    Record.Properties.CapabilityProfileKey = 1;
    return Record;
  }

  FCrowdMassBoundarySnapshot MakeBoundarySnapshot(
    const int32 FixedStepIndex,
    TConstArrayView<FCrowdMassBoundaryAgentRecord> Records)
  {
    FCrowdMassBoundarySnapshot Snapshot;
    FCrowdMassRuntimeBridge::BuildBoundarySnapshot(
      FixedStepIndex, 1, Records, Snapshot);
    return Snapshot;
  }

  FCrowdWorkerShadowSyncConfig MakeShadowConfig()
  {
    FCrowdWorkerShadowSyncConfig Config;
    Config.RuntimeConfig = MakeRuntimeConfig();
    Config.RuntimeConfig.ContractLimits.MaxPayloadBytes = 256;
    Config.RuntimeConfig.ContractLimits.MaxInputRecordsPerBatch = 64;
    Config.MaxPendingExpectations = 8;
    return Config;
  }

  bool DriveShadowUntilCompared(
    FCrowdWorkerBoundaryShadowSync& Shadow,
    FCrowdAsyncSimulationRuntime& Runtime,
    const uint64 ExpectedComparedCount)
  {
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdWorkerShadowCompareResult Result =
        Shadow.PollAndCompare(Runtime);
      if (Result == ECrowdWorkerShadowCompareResult::Violation)
        return false;
      if (Shadow.GetMetrics().ComparedSnapshotCount
          >= ExpectedComparedCount)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  }

  FCrowdWorkerIntentBatch MakeResnapshotBatch(
    const uint64 Generation,
    const TConstArrayView<FCrowdStableEntityRef> EntityRefs,
    const double TargetSimulationTimeSeconds)
  {
    FCrowdWorkerIntentBatch Batch;
    Batch.Generation = Generation;
    Batch.TargetSimulationTimeSeconds =
      TargetSimulationTimeSeconds;
    uint64 Sequence = 1;
    for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
    {
      FCrowdWorkerSpawnDelta Spawn;
      Spawn.InputSequence = Sequence++;
      Spawn.EntityRef = EntityRef;
      Spawn.InitialState = MakePayload(
        static_cast<uint32>(EntityRef.StableEntityId), 6001);
      Batch.Spawns.Add(MoveTemp(Spawn));
    }
    Batch.Clock.InputSequence = Sequence++;
    Batch.Clock.SimulationTick = FMath::Max<uint64>(
      1,
      static_cast<uint64>(FMath::RoundToInt64(
        TargetSimulationTimeSeconds * 30.0)));
    Batch.FirstInputSequence = 1;
    Batch.LastInputSequence = Sequence - 1;
    Batch.RecalculateStableHash();
    return Batch;
  }

  bool PollRuntimeUntilIdle(
    FCrowdAsyncSimulationRuntime& Runtime,
    const double TimeoutSeconds = 5.0)
  {
    const double Deadline =
      FPlatformTime::Seconds() + TimeoutSeconds;
    while (FPlatformTime::Seconds() < Deadline)
    {
      const ECrowdAsyncSimulationPollResult Result = Runtime.Poll();
      if (Result == ECrowdAsyncSimulationPollResult::Failed)
        return false;
      const FCrowdAsyncSimulationRuntimeMetrics Metrics =
        Runtime.GetMetrics();
      if (Result == ECrowdAsyncSimulationPollResult::Idle
        && Metrics.InputQueueDepth == 0
        && Metrics.SimulationTimeSeconds
          + UE_DOUBLE_SMALL_NUMBER
          >= Metrics.TargetSimulationTimeSeconds)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  }

  bool WaitForOwnerPumpCount(
    const FCrowdAsyncSimulationRuntime& Runtime,
    const uint64 ExpectedCount,
    const double TimeoutSeconds = 5.0)
  {
    const double Deadline =
      FPlatformTime::Seconds() + TimeoutSeconds;
    while (FPlatformTime::Seconds() < Deadline)
    {
      if (Runtime.GetMetrics().OwnerPumpCount >= ExpectedCount)
        return true;
      FPlatformProcess::SleepNoStats(0.0f);
    }
    return false;
  }
}

using namespace CrowdWorkerExchangeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerContractsPayloadAndHashTest,
  "MassCrowd.Runtime.WorkerContracts.PayloadAndCanonicalHash",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerContractsPayloadAndHashTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits();
  FCrowdWorkerPayload Payload = MakePayload(17);
  TestTrue(TEXT("valid schema payload accepted"),
    Payload.IsValid(Limits.MaxPayloadBytes));
  Payload.Bytes[0] ^= 1;
  TestFalse(TEXT("payload mutation invalidates hash"),
    Payload.IsValid(Limits.MaxPayloadBytes));

  FCrowdWorkerIntentBatch ClockOnly = MakeInputBatch(7, 1, 1);
  TestTrue(TEXT("clock-only input heartbeat is valid"),
    ClockOnly.IsValid(Limits));

  FCrowdWorkerIntentBatch Forward = MakeInputBatch(7, 1, 4);
  TestTrue(TEXT("mixed input batch is valid"),
    Forward.IsValid(Limits));
  FCrowdWorkerIntentBatch Reordered = Forward;
  Algo::Reverse(Reordered.Spawns);
  Algo::Reverse(Reordered.Commands);
  Reordered.RecalculateStableHash();
  TestEqual(TEXT("input hash ignores per-kind array order"),
    Reordered.StableHash, Forward.StableHash);
  TestTrue(TEXT("reordered input remains valid"),
    Reordered.IsValid(Limits));

  Reordered.Commands[0].InputSequence =
    Reordered.Spawns[0].InputSequence;
  Reordered.RecalculateStableHash();
  TestFalse(TEXT("duplicate global input sequence rejected"),
    Reordered.IsValid(Limits));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerContractsSequenceGateTest,
  "MassCrowd.Runtime.WorkerContracts.SequenceGate",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerContractsSequenceGateTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits();
  FCrowdWorkerInputSequenceGate Gate;
  TestTrue(TEXT("gate initializes"), Gate.ResetForResnapshot(7));

  const FCrowdWorkerIntentBatch First = MakeInputBatch(7, 1, 3);
  TestEqual(TEXT("first contiguous batch accepted"),
    Gate.Accept(First, Limits),
    ECrowdWorkerInputAcceptResult::Accepted);
  TestEqual(TEXT("next sequence advances"),
    Gate.GetNextExpectedInputSequence(), uint64{4});
  TestEqual(TEXT("exact duplicate is idempotent"),
    Gate.Accept(First, Limits),
    ECrowdWorkerInputAcceptResult::AcceptedDuplicate);

  FCrowdWorkerIntentBatch WrongGeneration = MakeInputBatch(8, 4, 1);
  TestEqual(TEXT("wrong generation rejected without poisoning"),
    Gate.Accept(WrongGeneration, Limits),
    ECrowdWorkerInputAcceptResult::RejectedGeneration);
  TestEqual(TEXT("wrong generation reason is explicit"),
    Gate.GetLastFailure(),
    ECrowdWorkerInputFailure::GenerationMismatch);
  TestFalse(TEXT("wrong generation does not require resnapshot"),
    Gate.RequiresResnapshot());

  const FCrowdWorkerIntentBatch Second = MakeInputBatch(7, 4, 1);
  TestEqual(TEXT("next contiguous batch accepted"),
    Gate.Accept(Second, Limits),
    ECrowdWorkerInputAcceptResult::Accepted);
  TestEqual(TEXT("older complete batch is stale"),
    Gate.Accept(First, Limits),
    ECrowdWorkerInputAcceptResult::RejectedStale);
  TestEqual(TEXT("stale reason is explicit"),
    Gate.GetLastFailure(),
    ECrowdWorkerInputFailure::StaleSequence);

  const FCrowdWorkerIntentBatch Gap = MakeInputBatch(7, 6, 1);
  TestEqual(TEXT("gap requires resnapshot"),
    Gate.Accept(Gap, Limits),
    ECrowdWorkerInputAcceptResult::RequiresResnapshot);
  TestTrue(TEXT("gap latches resnapshot"),
    Gate.RequiresResnapshot());
  TestEqual(TEXT("gap reason is explicit"),
    Gate.GetLastFailure(),
    ECrowdWorkerInputFailure::SequenceGap);
  TestEqual(TEXT("latched gate rejects later valid batch"),
    Gate.Accept(MakeInputBatch(7, 5, 1), Limits),
    ECrowdWorkerInputAcceptResult::RequiresResnapshot);

  TestTrue(TEXT("explicit resnapshot resets sequence"),
    Gate.ResetForResnapshot(9, 11));
  TestEqual(TEXT("resnapshot accepts configured first sequence"),
    Gate.Accept(MakeInputBatch(9, 11, 2), Limits),
    ECrowdWorkerInputAcceptResult::Accepted);

  FCrowdWorkerInputSequenceGate ConflictGate;
  TestTrue(TEXT("conflict gate initializes"),
    ConflictGate.ResetForResnapshot(7));
  TestEqual(TEXT("conflict fixture accepted"),
    ConflictGate.Accept(First, Limits),
    ECrowdWorkerInputAcceptResult::Accepted);
  FCrowdWorkerIntentBatch Conflict = First;
  Conflict.TargetSimulationTimeSeconds += 1.0;
  Conflict.RecalculateStableHash();
  TestEqual(TEXT("same range with different hash requires resnapshot"),
    ConflictGate.Accept(Conflict, Limits),
    ECrowdWorkerInputAcceptResult::RequiresResnapshot);
  TestEqual(TEXT("conflicting duplicate reason is explicit"),
    ConflictGate.GetLastFailure(),
    ECrowdWorkerInputFailure::ConflictingDuplicate);

  FCrowdWorkerInputSequenceGate OverlapGate;
  TestTrue(TEXT("overlap gate initializes"),
    OverlapGate.ResetForResnapshot(7));
  TestEqual(TEXT("overlap fixture accepted"),
    OverlapGate.Accept(First, Limits),
    ECrowdWorkerInputAcceptResult::Accepted);
  TestEqual(TEXT("partial overlap requires resnapshot"),
    OverlapGate.Accept(MakeInputBatch(7, 3, 2), Limits),
    ECrowdWorkerInputAcceptResult::RequiresResnapshot);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerContractsPublishedValidationTest,
  "MassCrowd.Runtime.WorkerContracts.PublishedValidation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerContractsPublishedValidationTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits();
  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("validation exchange initializes"),
    Exchange.ResetQuiescent(7, Limits));
  TestEqual(TEXT("validation patch appends"),
    Exchange.AppendStatePatch(MakePatch(1, 2, 3, 1, 10)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("validation event appends"),
    Exchange.AppendOrderedEvent(MakeEvent(1, 2, 3)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("validation batch publishes"),
    Exchange.TryPublishBuildingBatch(MakeMetadata(1, 2, 3)),
    ECrowdWorkerPublishResult::Published);
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  TestEqual(TEXT("validation batch exchanges"),
    Exchange.TryExchangePublishedBatch(7, 1, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  if (!TestNotNull(TEXT("validation batch available"), Batch))
    return false;
  TestEqual(TEXT("valid published batch accepted"),
    FCrowdWorkerPublishedBatchValidator::Validate(
      *Batch, Limits, 7, 0),
    ECrowdWorkerPublishedValidationResult::Valid);
  TestEqual(TEXT("consumer generation mismatch rejected"),
    FCrowdWorkerPublishedBatchValidator::Validate(
      *Batch, Limits, 8, 0),
    ECrowdWorkerPublishedValidationResult::RejectedGeneration);
  TestEqual(TEXT("already consumed publish sequence rejected"),
    FCrowdWorkerPublishedBatchValidator::Validate(
      *Batch, Limits, 7, 1),
    ECrowdWorkerPublishedValidationResult::RejectedPublishSequence);

  FCrowdWorkerPublishedBatch Mutated = *Batch;
  Mutated.StableHash ^= 1;
  TestEqual(TEXT("mutated published hash rejected"),
    FCrowdWorkerPublishedBatchValidator::Validate(
      Mutated, Limits, 7, 0),
    ECrowdWorkerPublishedValidationResult::RejectedHash);
  Mutated = *Batch;
  Mutated.StatePatches[0].EntityRef.LifecycleSerial = 0;
  Mutated.StatePatches[0].RecalculateStableHash();
  Mutated.RecalculateStableHash();
  TestEqual(TEXT("invalid result lifecycle rejected"),
    FCrowdWorkerPublishedBatchValidator::Validate(
      Mutated, Limits, 7, 0),
    ECrowdWorkerPublishedValidationResult::RejectedStructure);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerExchangeVariableBatchTest,
  "MassCrowd.Runtime.WorkerExchange.VariableBatchAndSingleFrame",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerExchangeVariableBatchTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits();
  for (const int32 PatchCount : {0, 1, 10, 9999})
  {
    FCrowdWorkerPublishedExchange Exchange;
    TestTrue(TEXT("exchange initializes"),
      Exchange.ResetQuiescent(7, Limits));
    for (int32 Index = PatchCount - 1; Index >= 0; --Index)
      TestEqual(TEXT("patch appends"),
        Exchange.AppendStatePatch(
          MakePatch(Index + 1, 1, 0, 1, Index + 100)),
        ECrowdWorkerAppendResult::Appended);
    TestEqual(TEXT("building batch publishes"),
      Exchange.TryPublishBuildingBatch(MakeMetadata(1)),
      ECrowdWorkerPublishResult::Published);

    const FCrowdWorkerPublishedBatch* Batch = nullptr;
    TestEqual(TEXT("published batch exchanges"),
      Exchange.TryExchangePublishedBatch(7, 1, Batch),
      ECrowdWorkerExchangeResult::Exchanged);
    if (!TestNotNull(TEXT("consuming batch available"), Batch))
      continue;
    TestEqual(TEXT("all variable patches consumed"),
      Batch->StatePatches.Num(), PatchCount);
    TestEqual(TEXT("published batch validates"),
      FCrowdWorkerPublishedBatchValidator::Validate(
        *Batch, Limits, 7, 0),
      ECrowdWorkerPublishedValidationResult::Valid);
    TestEqual(TEXT("patches sorted by stable entity"),
      PatchCount > 0
        ? Batch->StatePatches[0].EntityRef.StableEntityId
        : uint64{0},
      PatchCount > 0 ? uint64{1} : uint64{0});

    const FCrowdWorkerPublishedBatch* Duplicate = nullptr;
    TestEqual(TEXT("same consumer frame cannot chase tail"),
      Exchange.TryExchangePublishedBatch(7, 1, Duplicate),
      ECrowdWorkerExchangeResult::RejectedConsumerFrame);
    TestNull(TEXT("rejected exchange exposes no batch"), Duplicate);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerExchangeMergeAndDeferredTest,
  "MassCrowd.Runtime.WorkerExchange.MergeDeferredAndImmutability",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerExchangeMergeAndDeferredTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits();
  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("exchange initializes"),
    Exchange.ResetQuiescent(7, Limits));
  TestEqual(TEXT("initial state appends"),
    Exchange.AppendStatePatch(MakePatch(1, 1, 1, 1, 10)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("newer state replaces old state"),
    Exchange.AppendStatePatch(MakePatch(1, 2, 2, 2, 20)),
    ECrowdWorkerAppendResult::ReplacedNewer);
  TestEqual(TEXT("equivalent state merges dirty fields"),
    Exchange.AppendStatePatch(MakePatch(1, 2, 2, 4, 20)),
    ECrowdWorkerAppendResult::MergedEquivalent);
  TestEqual(TEXT("stale completion is ignored"),
    Exchange.AppendStatePatch(MakePatch(1, 1, 1, 8, 10)),
    ECrowdWorkerAppendResult::IgnoredStale);
  TestEqual(TEXT("first state publishes"),
    Exchange.TryPublishBuildingBatch(MakeMetadata(1, 2, 2)),
    ECrowdWorkerPublishResult::Published);

  TestEqual(TEXT("next building accepts state while published waits"),
    Exchange.AppendStatePatch(MakePatch(2, 3, 2, 1, 30)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("second publish defers while first is pending"),
    Exchange.TryPublishBuildingBatch(MakeMetadata(2, 3, 2)),
    ECrowdWorkerPublishResult::DeferredPublishedOccupied);

  const FCrowdWorkerPublishedBatch* First = nullptr;
  TestEqual(TEXT("first pending batch exchanges"),
    Exchange.TryExchangePublishedBatch(7, 1, First),
    ECrowdWorkerExchangeResult::Exchanged);
  if (!TestNotNull(TEXT("first consuming batch exists"), First))
    return false;
  TestEqual(TEXT("latest state coalesced to one patch"),
    First->StatePatches.Num(), 1);
  TestEqual(TEXT("dirty fields are unioned"),
    First->StatePatches[0].DirtyMask, uint64{7});
  uint32 StateValue = 0;
  TestTrue(TEXT("latest state payload readable"),
    ReadPayloadValue(
      First->StatePatches[0].State.Payload, StateValue));
  TestEqual(TEXT("latest state payload retained"), StateValue, 20u);
  const uint64 FirstHash = First->StableHash;

  TestEqual(TEXT("deferred building publishes after consume"),
    Exchange.TryPublishBuildingBatch(MakeMetadata(2, 3, 2)),
    ECrowdWorkerPublishResult::Published);
  TestEqual(TEXT("old consuming remains immutable before next exchange"),
    First->StableHash, FirstHash);
  TestEqual(TEXT("old consuming record remains immutable"),
    First->StatePatches[0].EntityRef.StableEntityId, uint64{1});

  const FCrowdWorkerPublishedBatch* Second = nullptr;
  TestEqual(TEXT("second batch exchanges on later frame"),
    Exchange.TryExchangePublishedBatch(7, 2, Second),
    ECrowdWorkerExchangeResult::Exchanged);
  if (!TestNotNull(TEXT("second consuming batch exists"), Second))
    return false;
  TestEqual(TEXT("second batch contains deferred state"),
    Second->StatePatches.Num(), 1);
  TestEqual(TEXT("second batch identity preserved"),
    Second->StatePatches[0].EntityRef.StableEntityId, uint64{2});

  FCrowdWorkerPublishedExchange Conflict;
  TestTrue(TEXT("conflict exchange initializes"),
    Conflict.ResetQuiescent(7, Limits));
  TestEqual(TEXT("conflict base appends"),
    Conflict.AppendStatePatch(MakePatch(1, 1, 1, 1, 10)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("same version different state fails closed"),
    Conflict.AppendStatePatch(MakePatch(1, 1, 1, 2, 11)),
    ECrowdWorkerAppendResult::Violation);
  TestTrue(TEXT("state conflict latches violation"),
    Conflict.HasViolation());
  TestEqual(TEXT("violated exchange refuses publish"),
    Conflict.TryPublishBuildingBatch(MakeMetadata(1)),
    ECrowdWorkerPublishResult::Violation);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerExchangeEventBackpressureTest,
  "MassCrowd.Runtime.WorkerExchange.OrderedEventBackpressure",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerExchangeEventBackpressureTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits(16, 2);
  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("event exchange initializes"),
    Exchange.ResetQuiescent(7, Limits));
  TestEqual(TEXT("first event appends"),
    Exchange.AppendOrderedEvent(MakeEvent(1)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("second event appends"),
    Exchange.AppendOrderedEvent(MakeEvent(2)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("event capacity fails closed"),
    Exchange.AppendOrderedEvent(MakeEvent(3)),
    ECrowdWorkerAppendResult::Violation);
  TestTrue(TEXT("capacity violation latches"),
    Exchange.HasViolation());

  FCrowdWorkerPublishedExchange Gap;
  TestTrue(TEXT("gap exchange initializes"),
    Gap.ResetQuiescent(7, Limits));
  TestEqual(TEXT("event sequence must start at one"),
    Gap.AppendOrderedEvent(MakeEvent(2)),
    ECrowdWorkerAppendResult::Violation);

  FCrowdWorkerPublishedExchange Ordered;
  TestTrue(TEXT("ordered exchange initializes"),
    Ordered.ResetQuiescent(7, Limits));
  TestEqual(TEXT("ordered first event appends"),
    Ordered.AppendOrderedEvent(MakeEvent(1)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("ordered second event appends"),
    Ordered.AppendOrderedEvent(MakeEvent(2)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("ordered events publish"),
    Ordered.TryPublishBuildingBatch(MakeMetadata(1)),
    ECrowdWorkerPublishResult::Published);
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  TestEqual(TEXT("ordered events exchange"),
    Ordered.TryExchangePublishedBatch(7, 1, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  if (!TestNotNull(TEXT("ordered consuming batch exists"), Batch))
    return false;
  TestEqual(TEXT("all ordered events preserved"),
    Batch->OrderedEvents.Num(), 2);
  TestEqual(TEXT("first event order preserved"),
    Batch->OrderedEvents[0].EventSequence, uint64{1});
  TestEqual(TEXT("second event order preserved"),
    Batch->OrderedEvents[1].EventSequence, uint64{2});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerExchangeConcurrentTest,
  "MassCrowd.Runtime.WorkerExchange.SingleProducerConsumer",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerExchangeConcurrentTest::RunTest(
  const FString& Parameters)
{
  constexpr int32 RoundCount = 256;
  const FCrowdWorkerContractLimits Limits = MakeLimits(4, 4);
  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("concurrent exchange initializes"),
    Exchange.ResetQuiescent(7, Limits));

  TAtomic<bool> Stop{false};
  TFuture<bool> Producer = Async(
    EAsyncExecution::ThreadPool,
    [&Exchange, &Stop]
    {
      for (int32 Round = 1;
        Round <= RoundCount && !Stop.Load(); ++Round)
      {
        if (Exchange.AppendStatePatch(
            MakePatch(
              Round, Round, Round, 1,
              static_cast<uint32>(Round)))
          != ECrowdWorkerAppendResult::Appended)
          return false;
        const FCrowdWorkerPublishMetadata Metadata =
          MakeMetadata(Round, Round, Round);
        const double Deadline = FPlatformTime::Seconds() + 5.0;
        for (;;)
        {
          const ECrowdWorkerPublishResult Result =
            Exchange.TryPublishBuildingBatch(Metadata);
          if (Result == ECrowdWorkerPublishResult::Published)
            break;
          if (Result
              != ECrowdWorkerPublishResult::DeferredPublishedOccupied
            || FPlatformTime::Seconds() >= Deadline)
            return false;
          FPlatformProcess::SleepNoStats(0.0f);
        }
      }
      return !Stop.Load();
    });

  uint64 LastPublishSequence = 0;
  int32 ConsumedCount = 0;
  uint64 ConsumerFrame = 1;
  const double Deadline = FPlatformTime::Seconds() + 10.0;
  while (ConsumedCount < RoundCount
    && FPlatformTime::Seconds() < Deadline)
  {
    const FCrowdWorkerPublishedBatch* Batch = nullptr;
    const ECrowdWorkerExchangeResult Result =
      Exchange.TryExchangePublishedBatch(
        7, ConsumerFrame++, Batch);
    if (Result == ECrowdWorkerExchangeResult::NoPublishedBatch)
    {
      FPlatformProcess::SleepNoStats(0.0f);
      continue;
    }
    if (Result != ECrowdWorkerExchangeResult::Exchanged
      || Batch == nullptr
      || FCrowdWorkerPublishedBatchValidator::Validate(
        *Batch, Limits, 7, LastPublishSequence)
        != ECrowdWorkerPublishedValidationResult::Valid
      || Batch->StatePatches.Num() != 1)
    {
      Stop.Store(true);
      break;
    }
    LastPublishSequence = Batch->PublishSequence;
    ++ConsumedCount;
  }
  if (ConsumedCount != RoundCount)
    Stop.Store(true);
  const bool bProducerSucceeded = Producer.Get();
  TestTrue(TEXT("producer completed without slot conflict"),
    bProducerSucceeded);
  TestEqual(TEXT("every published batch consumed"),
    ConsumedCount, RoundCount);
  TestEqual(TEXT("publish sequence remains contiguous"),
    LastPublishSequence, static_cast<uint64>(RoundCount));
  TestFalse(TEXT("concurrent exchange has no violation"),
    Exchange.HasViolation());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerResultApplyProxyTest,
  "MassCrowd.Runtime.WorkerResultApply.LifecycleOwnerAndEvents",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerResultApplyProxyTest::RunTest(
  const FString& Parameters)
{
  const FCrowdWorkerContractLimits Limits = MakeLimits(10, 4);
  FCrowdWorkerResultApplyProxy Proxy;
  TestTrue(TEXT("result proxy initializes"),
    Proxy.ResetQuiescent(7, Limits));
  const FCrowdStableEntityRef CurrentRefs[] = {
    {1, 10, 1}, {1, 20, 2}};
  TestTrue(TEXT("current GT lifecycle set accepted"),
    Proxy.UpdateCurrentEntities(7, CurrentRefs));

  FCrowdWorkerPublishedExchange Exchange;
  TestTrue(TEXT("result fixture exchange initializes"),
    Exchange.ResetQuiescent(7, Limits));
  TestEqual(TEXT("current lifecycle patch appended"),
    Exchange.AppendStatePatch(
      MakePatch(10, 1, 1,
        CrowdWorkerResultFields::PresentationDiagnosticProxy, 101)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("stale lifecycle patch appended structurally"),
    Exchange.AppendStatePatch(
      MakePatch(20, 1, 1,
        CrowdWorkerResultFields::PresentationDiagnosticProxy,
        202)),
    ECrowdWorkerAppendResult::Appended);
  FCrowdWorkerStatePatch DomainPatch =
    MakePatch(
      10, 1, 1,
      CrowdWorkerRuntimeV2FieldMask(
        ECrowdWorkerField::Movement),
      303);
  DomainPatch.StateFieldId =
    1 + static_cast<uint16>(ECrowdWorkerField::Movement);
  DomainPatch.RecalculateStableHash();
  TestEqual(TEXT("v2 movement proxy patch appended"),
    Exchange.AppendStatePatch(DomainPatch),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("ordered event appended"),
    Exchange.AppendOrderedEvent(MakeEvent(1, 1, 1)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("result fixture publishes"),
    Exchange.TryPublishBuildingBatch(MakeMetadata(1, 1, 1)),
    ECrowdWorkerPublishResult::Published);
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  TestEqual(TEXT("result fixture exchanges"),
    Exchange.TryExchangePublishedBatch(7, 1, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  TestNotNull(TEXT("result fixture batch exists"), Batch);
  if (!Batch) return false;
  TestEqual(TEXT("result proxy applies batch"),
    Proxy.Apply(*Batch),
    ECrowdWorkerResultApplyResult::Applied);
  TestNotNull(TEXT("current lifecycle proxy state applied"),
    Proxy.Find({1, 10, 1}));
  TestNotNull(TEXT("v2 movement domain state applied read-only"),
    Proxy.FindDomain(
      {1, 10, 1}, ECrowdWorkerField::Movement));
  TestNull(TEXT("stale lifecycle proxy state rejected"),
    Proxy.Find({1, 20, 1}));
  TestEqual(TEXT("stale lifecycle counted"),
    Proxy.GetMetrics().StaleLifecyclePatchCount, uint64{1});
  TestEqual(TEXT("ordered event counted"),
    Proxy.GetMetrics().LastAppliedEventSequence, uint64{1});
  TestEqual(TEXT("domain patch counted"),
    Proxy.GetMetrics().AppliedDomainPatchCount, uint64{1});

  FCrowdWorkerPublishedExchange RestoredExchange;
  TestTrue(TEXT("checkpoint event baseline restores"),
    RestoredExchange.ResetQuiescent(7, Limits, 0, 10, 0));
  TestEqual(TEXT("first post-checkpoint event is contiguous"),
    RestoredExchange.AppendOrderedEvent(MakeEvent(11, 2, 101)),
    ECrowdWorkerAppendResult::Appended);
  TestEqual(TEXT("post-checkpoint batch publishes from local sequence one"),
    RestoredExchange.TryPublishBuildingBatch(
      MakeMetadata(1, 2, 101)),
    ECrowdWorkerPublishResult::Published);
  const FCrowdWorkerPublishedBatch* RestoredBatch = nullptr;
  TestEqual(TEXT("post-checkpoint batch exchanges"),
    RestoredExchange.TryExchangePublishedBatch(
      7, 1, RestoredBatch),
    ECrowdWorkerExchangeResult::Exchanged);
  FCrowdWorkerResultApplyProxy RestoredProxy;
  TestTrue(TEXT("result proxy restores checkpoint baselines"),
    RestoredProxy.ResetFromCheckpoint(
      7, Limits, CurrentRefs, 100, 10));
  TestNotNull(TEXT("post-checkpoint batch exists"), RestoredBatch);
  if (RestoredBatch)
  {
    TestEqual(TEXT("result proxy accepts post-checkpoint event"),
      RestoredProxy.Apply(*RestoredBatch),
      ECrowdWorkerResultApplyResult::Applied);
    TestEqual(TEXT("result proxy advances event baseline"),
      RestoredProxy.GetMetrics().LastAppliedEventSequence,
      uint64{11});
  }

  FCrowdWorkerResultApplyProxy OwnerViolation;
  TestTrue(TEXT("owner violation proxy initializes"),
    OwnerViolation.ResetQuiescent(7, Limits));
  TestTrue(TEXT("owner violation lifecycle accepted"),
    OwnerViolation.UpdateCurrentEntities(7, CurrentRefs));
  FCrowdWorkerPublishedBatch WrongOwner = *Batch;
  WrongOwner.StatePatches[0].DirtyMask = 1ull << 8;
  WrongOwner.StatePatches[0].RecalculateStableHash();
  WrongOwner.RecalculateStableHash();
  TestEqual(TEXT("non-PW5 owner mask rejected"),
    OwnerViolation.Apply(WrongOwner),
    ECrowdWorkerResultApplyResult::RejectedOwnerMask);
  TestTrue(TEXT("owner violation latches"),
    OwnerViolation.GetMetrics().bViolation);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationPublishedResultsTest,
  "MassCrowd.Runtime.WorkerResultApply.RuntimeVariablePublishedBatch",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationPublishedResultsTest::RunTest(
  const FString& Parameters)
{
  FCrowdAsyncSimulationRuntime Runtime;
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeRuntimeConfig();
  TestTrue(TEXT("published runtime starts"),
    Runtime.Start(Config, 7));
  const FCrowdStableEntityRef Refs[] = {
    {1, 10, 1}, {1, 20, 1}};
  TestEqual(TEXT("published resnapshot queues"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(7, Refs, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("published resnapshot settles"),
    PollRuntimeUntilIdle(Runtime));
  const FCrowdWorkerPublishedBatch* Batch = nullptr;
  TestEqual(TEXT("two-patch batch exchanges"),
    Runtime.TryExchangePublishedBatch(7, 1, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  TestNotNull(TEXT("two-patch batch exists"), Batch);
  if (Batch)
    TestEqual(TEXT("resnapshot publishes both proxies"),
      Batch->StatePatches.Num(), 2);

  FCrowdWorkerIntentBatch Empty = MakeInputBatch(7, 4, 1);
  Empty.TargetSimulationTimeSeconds = 2.0 / 30.0;
  Empty.Clock.SimulationTick = 2;
  Empty.RecalculateStableHash();
  TestEqual(TEXT("empty progress batch queues"),
    Runtime.SubmitIntentBatch(Empty),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("empty progress batch settles"),
    PollRuntimeUntilIdle(Runtime));
  TestEqual(TEXT("empty published batch exchanges"),
    Runtime.TryExchangePublishedBatch(7, 2, Batch),
    ECrowdWorkerExchangeResult::Exchanged);
  if (Batch)
  {
    TestEqual(TEXT("empty batch has zero patches"),
      Batch->StatePatches.Num(), 0);
    TestEqual(TEXT("empty batch has zero events"),
      Batch->OrderedEvents.Num(), 0);
  }
  TestTrue(TEXT("published runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationRuntimeLifecycleTest,
  "MassCrowd.Runtime.WorkerRuntime.LifecycleMirrorAndClock",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationRuntimeLifecycleTest::RunTest(
  const FString& Parameters)
{
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeRuntimeConfig();
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("runtime starts"), Runtime.Start(Config, 7));
  TestEqual(TEXT("runtime waits for initial resnapshot"),
    Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Starting);
  TestTrue(TEXT("starting runtime requires resnapshot"),
    Runtime.RequiresResnapshot());
  TestEqual(TEXT("incremental input rejected before snapshot"),
    Runtime.SubmitIntentBatch(MakeInputBatch(7, 1, 1)),
    ECrowdAsyncSimulationSubmitResult::RejectedState);

  const FCrowdStableEntityRef InitialRefs[] = {
    {1, 20, 1},
    {1, 10, 1}};
  FCrowdWorkerIntentBatch Resnapshot =
    MakeResnapshotBatch(7, InitialRefs, 4.0 / 30.0);
  TestEqual(TEXT("initial resnapshot queues"),
    Runtime.SubmitResnapshot(Resnapshot),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("initial resnapshot and clock settle"),
    PollRuntimeUntilIdle(Runtime));
  TestEqual(TEXT("runtime enters running state"),
    Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Running);
  TestFalse(TEXT("resnapshot requirement clears"),
    Runtime.RequiresResnapshot());

  FCrowdWorkerMirrorSnapshot Snapshot;
  TestTrue(TEXT("running mirror snapshot is readable"),
    Runtime.ReadMirrorSnapshot(Snapshot));
  TestEqual(TEXT("initial mirror has two entities"),
    Snapshot.EntityRefs.Num(), 2);
  TestEqual(TEXT("mirror sorts stable refs"),
    Snapshot.EntityRefs[0].StableEntityId, uint64{10});
  TestEqual(TEXT("bootstrap executes one worker epoch at absolute tick four"),
    Snapshot.WorkerEpoch, uint64{1});
  TestTrue(TEXT("clock reaches target time"),
    FMath::IsNearlyEqual(
      Snapshot.SimulationTimeSeconds, 4.0 / 30.0));

  FCrowdWorkerIntentBatch Incremental;
  Incremental.Generation = 7;
  Incremental.FirstInputSequence = 4;
  Incremental.LastInputSequence = 8;
  Incremental.TargetSimulationTimeSeconds = 7.0 / 30.0;
  FCrowdWorkerExternalGameplayInput& State =
    Incremental.ExternalGameplayInputs.AddDefaulted_GetRef();
  State.InputSequence = 4;
  State.EntityRef = {1, 10, 1};
  State.DirtyMask = 1;
  State.FullState = MakePayload(30, 6001);
  FCrowdWorkerDespawnDelta& Despawn =
    Incremental.Despawns.AddDefaulted_GetRef();
  Despawn.InputSequence = 5;
  Despawn.EntityRef = {1, 20, 1};
  Despawn.ReasonId = 1;
  FCrowdWorkerSpawnDelta& Spawn =
    Incremental.Spawns.AddDefaulted_GetRef();
  Spawn.InputSequence = 6;
  Spawn.EntityRef = {1, 30, 1};
  Spawn.InitialState = MakePayload(30, 6001);
  FCrowdWorkerResourceDelta& Resource =
    Incremental.ResourceDeltas.AddDefaulted_GetRef();
  Resource.InputSequence = 7;
  Resource.ResourceId = 9001;
  Resource.Revision = 1;
  Resource.Payload = MakePayload(77, 6002);
  Incremental.Clock.InputSequence = 8;
  Incremental.Clock.SimulationTick = 7;
  Incremental.RecalculateStableHash();
  TestTrue(TEXT("incremental fixture valid"),
    Incremental.IsValid(Config.ContractLimits));
  TestEqual(TEXT("incremental input queues"),
    Runtime.SubmitIntentBatch(Incremental),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("incremental input settles"),
    PollRuntimeUntilIdle(Runtime));
  TestTrue(TEXT("updated mirror readable"),
    Runtime.ReadMirrorSnapshot(Snapshot));
  TestEqual(TEXT("despawn and spawn preserve count"),
    Snapshot.EntityRefs.Num(), 2);
  TestEqual(TEXT("new entity present"),
    Snapshot.EntityRefs[1], FCrowdStableEntityRef({1, 30, 1}));
  TestEqual(TEXT("last input sequence advances"),
    Snapshot.LastAppliedInputSequence, uint64{8});
  TestNotEqual(TEXT("resource contributes to mirror hash"),
    Snapshot.ResourceHash, uint64{0});

  FCrowdWorkerIntentBatch Reuse;
  Reuse.Generation = 7;
  Reuse.FirstInputSequence = 9;
  Reuse.LastInputSequence = 11;
  Reuse.TargetSimulationTimeSeconds = 9.0 / 30.0;
  FCrowdWorkerDespawnDelta& ReuseDespawn =
    Reuse.Despawns.AddDefaulted_GetRef();
  ReuseDespawn.InputSequence = 9;
  ReuseDespawn.EntityRef = {1, 30, 1};
  ReuseDespawn.ReasonId = 2;
  FCrowdWorkerSpawnDelta& ReuseSpawn =
    Reuse.Spawns.AddDefaulted_GetRef();
  ReuseSpawn.InputSequence = 10;
  ReuseSpawn.EntityRef = {1, 30, 2};
  ReuseSpawn.InitialState = MakePayload(31, 6001);
  Reuse.Clock.InputSequence = 11;
  Reuse.Clock.SimulationTick = 9;
  Reuse.RecalculateStableHash();
  TestEqual(TEXT("higher lifecycle reuse queues"),
    Runtime.SubmitIntentBatch(Reuse),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("higher lifecycle reuse settles"),
    PollRuntimeUntilIdle(Runtime));
  TestTrue(TEXT("reuse mirror readable"),
    Runtime.ReadMirrorSnapshot(Snapshot));
  TestEqual(TEXT("higher lifecycle replaces slot"),
    Snapshot.EntityRefs[1], FCrowdStableEntityRef({1, 30, 2}));
  TestTrue(TEXT("runtime stops cleanly"),
    Runtime.StopAndDrain(5.0));
  TestEqual(TEXT("runtime reaches stopped state"),
    Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Stopped);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationAdmissionSequenceTest,
  "MassCrowd.Runtime.WorkerRuntime.AdmissionApplySequenceAndCapacity",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationAdmissionSequenceTest::RunTest(
  const FString& Parameters)
{
  FCrowdAsyncSimulationRuntimeConfig Config =
    MakeRuntimeConfig();
  Config.MaxQueuedInputBatches = 2;
  Config.MaxInputBatchesPerPump = 1;

  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("admission runtime starts"),
    Runtime.Start(Config, 31));
  const FCrowdStableEntityRef InitialRef[] = {{3, 1, 1}};
  TestEqual(TEXT("admission resnapshot queues"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(31, InitialRef, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("admission resnapshot settles"),
    PollRuntimeUntilIdle(Runtime));

  const FCrowdWorkerIntentBatch SequenceTwo =
    MakeInputBatch(31, 3, 1);
  const FCrowdWorkerIntentBatch SequenceThree =
    MakeInputBatch(31, 4, 1);
  const FCrowdWorkerIntentBatch SequenceFour =
    MakeInputBatch(31, 5, 1);
  TestEqual(TEXT("first batch reserves sequence two"),
    Runtime.SubmitIntentBatch(SequenceTwo),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestEqual(TEXT("second batch reserves sequence three before poll"),
    Runtime.SubmitIntentBatch(SequenceThree),
    ECrowdAsyncSimulationSubmitResult::Accepted);

  FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(TEXT("accepted sequence advances before apply"),
    Metrics.LastAcceptedInputSequence, uint64{4});
  TestEqual(TEXT("applied sequence remains at snapshot"),
    Metrics.LastAppliedInputSequence, uint64{2});
  TestEqual(TEXT("queued watermark records reservation"),
    Metrics.QueuedInputSequenceWatermark, uint64{4});
  TestEqual(TEXT("both accepted batches are queued"),
    Metrics.InputQueueDepth, 2);

  TestEqual(TEXT("capacity rejection does not reserve sequence four"),
    Runtime.SubmitIntentBatch(SequenceFour),
    ECrowdAsyncSimulationSubmitResult::RejectedCapacity);
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("capacity preserves last accepted sequence"),
    Metrics.LastAcceptedInputSequence, uint64{4});
  TestEqual(TEXT("capacity failure reason is explicit"),
    Metrics.LastInputFailure,
    ECrowdAsyncSimulationInputFailure::Capacity);

  const uint64 PumpBefore = Metrics.OwnerPumpCount;
  TestEqual(TEXT("one-batch owner pump launches"),
    Runtime.Poll(), ECrowdAsyncSimulationPollResult::Working);
  TestTrue(TEXT("one-batch owner pump completes"),
    WaitForOwnerPumpCount(Runtime, PumpBefore + 1));
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("first pump applies only sequence two"),
    Metrics.LastAppliedInputSequence, uint64{3});
  TestEqual(TEXT("one accepted batch remains queued"),
    Metrics.InputQueueDepth, 1);

  TestEqual(TEXT("capacity retry reserves sequence four"),
    Runtime.SubmitIntentBatch(SequenceFour),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("retry advances accepted sequence"),
    Metrics.LastAcceptedInputSequence, uint64{5});
  TestEqual(TEXT("retry does not fabricate applied progress"),
    Metrics.LastAppliedInputSequence, uint64{3});
  TestTrue(TEXT("all admitted batches settle without false gap"),
    PollRuntimeUntilIdle(Runtime));
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("apply gate catches up to admission gate"),
    Metrics.LastAppliedInputSequence, uint64{5});
  TestFalse(TEXT("accepted-not-applied never requests resnapshot"),
    Runtime.RequiresResnapshot());

  TestEqual(TEXT("exact duplicate is idempotently accepted"),
    Runtime.SubmitIntentBatch(SequenceFour),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("duplicate is not enqueued twice"),
    Metrics.InputQueueDepth, 0);
  TestEqual(TEXT("duplicate preserves applied sequence"),
    Metrics.LastAppliedInputSequence, uint64{5});

  FCrowdWorkerIntentBatch Conflict = SequenceFour;
  Conflict.TargetSimulationTimeSeconds += 1.0;
  Conflict.RecalculateStableHash();
  TestEqual(TEXT("conflicting duplicate requires resnapshot"),
    Runtime.SubmitIntentBatch(Conflict),
    ECrowdAsyncSimulationSubmitResult::RequiresResnapshot);
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("conflicting duplicate reason is explicit"),
    Metrics.LastInputFailure,
    ECrowdAsyncSimulationInputFailure::ConflictingDuplicate);
  TestTrue(TEXT("conflicting duplicate latches resnapshot"),
    Runtime.RequiresResnapshot());

  TestTrue(TEXT("admission runtime invalidates"),
    Runtime.Invalidate(32));
  const double InvalidateDeadline =
    FPlatformTime::Seconds() + 5.0;
  while (Runtime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Invalidating
    && FPlatformTime::Seconds() < InvalidateDeadline)
  {
    Runtime.Poll();
    FPlatformProcess::SleepNoStats(0.0f);
  }
  Metrics = Runtime.GetMetrics();
  TestEqual(TEXT("invalidation resets accepted sequence"),
    Metrics.LastAcceptedInputSequence, uint64{0});
  TestEqual(TEXT("invalidation resets applied sequence"),
    Metrics.LastAppliedInputSequence, uint64{0});
  TestEqual(TEXT("invalidation resets queued watermark"),
    Metrics.QueuedInputSequenceWatermark, uint64{0});
  TestEqual(TEXT("invalidation clears failure reason"),
    Metrics.LastInputFailure,
    ECrowdAsyncSimulationInputFailure::None);
  TestTrue(TEXT("admission runtime stops"),
    Runtime.StopAndDrain(5.0));

  FCrowdAsyncSimulationRuntimeConfig CommandConfig =
    MakeRuntimeConfig();
  CommandConfig.MaxPendingCommands = 1;
  FCrowdAsyncSimulationRuntime CommandRuntime;
  TestTrue(TEXT("command runtime starts"),
    CommandRuntime.Start(CommandConfig, 33));
  TestEqual(TEXT("command runtime snapshot queues"),
    CommandRuntime.SubmitResnapshot(
      MakeResnapshotBatch(33, InitialRef, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("command runtime snapshot settles"),
    PollRuntimeUntilIdle(CommandRuntime));
  TestEqual(TEXT("first bounded command batch queues"),
    CommandRuntime.SubmitIntentBatch(MakeInputBatch(33, 3, 2)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("first bounded command batch settles"),
    PollRuntimeUntilIdle(CommandRuntime));
  TestEqual(TEXT("consumed command releases bounded capacity"),
    CommandRuntime.SubmitIntentBatch(MakeInputBatch(33, 5, 2)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("second bounded command batch settles"),
    PollRuntimeUntilIdle(CommandRuntime));
  TestFalse(TEXT("command consumption avoids false resnapshot"),
    CommandRuntime.RequiresResnapshot());
  TestTrue(TEXT("command runtime stops"),
    CommandRuntime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationRuntimeResnapshotTest,
  "MassCrowd.Runtime.WorkerRuntime.InvalidateResnapshotAndTeardown",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationRuntimeResnapshotTest::RunTest(
  const FString& Parameters)
{
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeRuntimeConfig();
  FCrowdAsyncSimulationRuntime Runtime;
  TestTrue(TEXT("resnapshot runtime starts"),
    Runtime.Start(Config, 11));
  const FCrowdStableEntityRef InitialRef[] = {{2, 1, 1}};
  TestEqual(TEXT("resnapshot fixture queues"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(11, InitialRef, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("resnapshot fixture settles"),
    PollRuntimeUntilIdle(Runtime));

  FCrowdWorkerIntentBatch Gap = MakeInputBatch(11, 4, 1);
  TestEqual(TEXT("admission gate rejects real gap"),
    Runtime.SubmitIntentBatch(Gap),
    ECrowdAsyncSimulationSubmitResult::RequiresResnapshot);
  TestTrue(TEXT("gap requests resnapshot"),
    Runtime.RequiresResnapshot());
  TestEqual(TEXT("runtime reports sequence gap reason"),
    Runtime.GetMetrics().LastInputFailure,
    ECrowdAsyncSimulationInputFailure::SequenceGap);
  TestEqual(TEXT("further input rejected while resnapshot required"),
    Runtime.SubmitIntentBatch(MakeInputBatch(11, 2, 1)),
    ECrowdAsyncSimulationSubmitResult::RequiresResnapshot);

  TestTrue(TEXT("generation invalidation begins"),
    Runtime.Invalidate(12));
  const double InvalidateDeadline =
    FPlatformTime::Seconds() + 5.0;
  while (Runtime.GetState()
      == ECrowdAsyncSimulationRuntimeState::Invalidating
    && FPlatformTime::Seconds() < InvalidateDeadline)
  {
    Runtime.Poll();
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestEqual(TEXT("invalidation waits for new snapshot"),
    Runtime.GetState(),
    ECrowdAsyncSimulationRuntimeState::Starting);
  TestEqual(TEXT("generation advances after invalidation"),
    Runtime.GetGeneration(), uint64{12});
  TestEqual(TEXT("old generation snapshot rejected"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(11, InitialRef, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::RejectedGeneration);

  const FCrowdStableEntityRef NewRef[] = {{2, 1, 2}};
  TestEqual(TEXT("new generation snapshot queues"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(12, NewRef, 2.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("new generation snapshot settles"),
    PollRuntimeUntilIdle(Runtime));
  FCrowdWorkerMirrorSnapshot Snapshot;
  TestTrue(TEXT("new generation mirror readable"),
    Runtime.ReadMirrorSnapshot(Snapshot));
  TestEqual(TEXT("new generation mirror identity"),
    Snapshot.EntityRefs[0], FCrowdStableEntityRef({2, 1, 2}));
  TestEqual(TEXT("resnapshot metric increments twice"),
    Runtime.GetMetrics().ResnapshotCount, uint64{2});

  FCrowdAsyncSimulationRuntime Busy;
  FCrowdAsyncSimulationRuntimeConfig BusyConfig = Config;
  BusyConfig.MaxSimulationStepsPerPump = 1;
  TestTrue(TEXT("busy runtime starts"),
    Busy.Start(BusyConfig, 21));
  TestEqual(TEXT("busy snapshot queues"),
    Busy.SubmitResnapshot(
      MakeResnapshotBatch(21, InitialRef, 10.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestEqual(TEXT("busy runtime launches short owner task"),
    Busy.Poll(), ECrowdAsyncSimulationPollResult::Working);
  TestTrue(TEXT("busy runtime drains during teardown"),
    Busy.StopAndDrain(5.0));
  TestEqual(TEXT("busy runtime stops"),
    Busy.GetState(),
    ECrowdAsyncSimulationRuntimeState::Stopped);

  TestTrue(TEXT("main runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationShadowSchedulerTest,
  "MassCrowd.Runtime.WorkerRuntime.ShortTaskShadowScheduler",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationShadowSchedulerTest::RunTest(
  const FString& Parameters)
{
  FCrowdAsyncSimulationRuntime Runtime;
  const FCrowdAsyncSimulationRuntimeConfig Config =
    MakeRuntimeConfig();
  TestTrue(TEXT("shadow scheduler runtime starts"),
    Runtime.Start(Config, 41));
  const FCrowdStableEntityRef InitialRef[] = {{4, 1, 1}};
  TestEqual(TEXT("shadow scheduler snapshot queues"),
    Runtime.SubmitResnapshot(
      MakeResnapshotBatch(41, InitialRef, 1.0 / 30.0)),
    ECrowdAsyncSimulationSubmitResult::Accepted);
  TestTrue(TEXT("shadow scheduler runtime reaches running"),
    PollRuntimeUntilIdle(Runtime));

  constexpr uint32 KernelId = 7001;
  constexpr int32 WorkCount = 12;
  FEvent* FirstWorkGate =
    FPlatformProcess::GetSynchEventFromPool(true);
  for (int32 Index = 0; Index < WorkCount; ++Index)
  {
    const uint64 WorkSequence = static_cast<uint64>(Index + 1);
    const uint64 ExpectedHash = 10000 + WorkSequence;
    FCrowdAsyncShadowWorkSubmission Submission;
    Submission.Generation = 41;
    Submission.WorkSequence = WorkSequence;
    Submission.KernelId = KernelId;
    Submission.ExpectedStableHash = ExpectedHash;
    Submission.Execute = [
      WorkSequence, ExpectedHash, FirstWorkGate]
    {
      if (WorkSequence == 1)
        FirstWorkGate->Wait();
      FPlatformProcess::SleepNoStats(
        static_cast<float>((WorkCount - WorkSequence) % 4)
          * 0.00025f);
      return ExpectedHash;
    };
    TestEqual(TEXT("short shadow task accepted"),
      Runtime.SubmitShadowWork(MoveTemp(Submission)),
      ECrowdAsyncShadowWorkSubmitResult::Accepted);
  }

  TArray<FCrowdAsyncShadowWorkResult> Completed;
  FPlatformProcess::SleepNoStats(0.002f);
  TestEqual(TEXT("later completion cannot pass unfinished prefix"),
    Runtime.CollectCompletedShadowWork(Completed), 0);
  FirstWorkGate->Trigger();

  FCrowdAsyncShadowWorkSubmission Duplicate;
  Duplicate.Generation = 41;
  Duplicate.WorkSequence = WorkCount;
  Duplicate.KernelId = KernelId;
  Duplicate.ExpectedStableHash = 1;
  Duplicate.Execute = [] { return uint64{1}; };
  TestEqual(TEXT("same kernel sequence cannot be resubmitted"),
    Runtime.SubmitShadowWork(MoveTemp(Duplicate)),
    ECrowdAsyncShadowWorkSubmitResult::RejectedSequence);

  TArray<FCrowdAsyncShadowWorkResult> AllCompleted;
  const double Deadline = FPlatformTime::Seconds() + 5.0;
  while (AllCompleted.Num() < WorkCount
    && FPlatformTime::Seconds() < Deadline)
  {
    Runtime.CollectCompletedShadowWork(Completed);
    AllCompleted.Append(Completed);
    FPlatformProcess::SleepNoStats(0.0f);
  }
  TestEqual(TEXT("all short tasks complete"),
    AllCompleted.Num(), WorkCount);
  for (int32 Index = 0; Index < AllCompleted.Num(); ++Index)
  {
    TestEqual(TEXT("completed work is returned in submission order"),
      AllCompleted[Index].WorkSequence,
      static_cast<uint64>(Index + 1));
    TestTrue(TEXT("completed shadow hash matches"),
      AllCompleted[Index].bHashMatch);
  }

  FCrowdAsyncShadowWorkSubmission Mismatch;
  Mismatch.Generation = 41;
  Mismatch.WorkSequence = WorkCount + 1;
  Mismatch.KernelId = KernelId;
  Mismatch.ExpectedStableHash = 20001;
  Mismatch.Execute = [] { return uint64{20002}; };
  TestEqual(TEXT("mismatch probe task accepted"),
    Runtime.SubmitShadowWork(MoveTemp(Mismatch)),
    ECrowdAsyncShadowWorkSubmitResult::Accepted);
  const double MismatchDeadline = FPlatformTime::Seconds() + 5.0;
  do
  {
    Runtime.CollectCompletedShadowWork(Completed);
    if (!Completed.IsEmpty()) break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  while (FPlatformTime::Seconds() < MismatchDeadline);
  TestEqual(TEXT("mismatch probe completes"), Completed.Num(), 1);
  if (!Completed.IsEmpty())
    TestFalse(TEXT("hash mismatch is reported"),
      Completed[0].bHashMatch);
  FCrowdAsyncShadowWorkSubmission Production;
  Production.Generation = 41;
  Production.WorkSequence = WorkCount + 2;
  Production.KernelId = KernelId;
  Production.ExpectedStableHash = 0;
  Production.bRequireExpectedStableHash = false;
  Production.Execute = [] { return uint64{30001}; };
  TestEqual(TEXT("production task without golden hash accepted"),
    Runtime.SubmitShadowWork(MoveTemp(Production)),
    ECrowdAsyncShadowWorkSubmitResult::Accepted);
  const double ProductionDeadline =
    FPlatformTime::Seconds() + 5.0;
  do
  {
    Runtime.CollectCompletedShadowWork(Completed);
    if (!Completed.IsEmpty()) break;
    FPlatformProcess::SleepNoStats(0.0f);
  }
  while (FPlatformTime::Seconds() < ProductionDeadline);
  TestEqual(TEXT("production task completes"), Completed.Num(), 1);
  if (!Completed.IsEmpty())
  {
    TestFalse(TEXT("production task does not require golden hash"),
      Completed[0].bRequiredExpectedStableHash);
    TestTrue(TEXT("production task success is accepted"),
      Completed[0].bHashMatch);
  }
  const FCrowdAsyncSimulationRuntimeMetrics Metrics =
    Runtime.GetMetrics();
  TestEqual(TEXT("submitted task metric"),
    Metrics.SubmittedShadowWorkCount,
    static_cast<uint64>(WorkCount + 1));
  TestEqual(TEXT("completed task metric"),
    Metrics.CompletedShadowWorkCount,
      static_cast<uint64>(WorkCount + 1));
  TestEqual(TEXT("submitted production task metric"),
    Metrics.SubmittedProductionWorkCount, uint64{1});
  TestEqual(TEXT("completed production task metric"),
    Metrics.CompletedProductionWorkCount, uint64{1});
  TestEqual(TEXT("mismatch metric"),
    Metrics.ShadowHashMismatchCount, uint64{1});
  TestEqual(TEXT("no task remains in flight"),
    Metrics.InFlightShadowWorkCount, 0);
  FPlatformProcess::ReturnSynchEventToPool(FirstWorkGate);
  TestTrue(TEXT("shadow scheduler runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdAsyncSimulationRuntimePerWorldHostTest,
  "MassCrowd.Runtime.WorkerRuntime.PerWorldHost",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdAsyncSimulationRuntimePerWorldHostTest::RunTest(
  const FString& Parameters)
{
  UWorld* TestWorld = nullptr;
  if (GEngine)
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
      if (Context.World()
        && (Context.WorldType == EWorldType::Editor
          || Context.WorldType == EWorldType::Game
          || Context.WorldType == EWorldType::PIE))
      {
        TestWorld = Context.World();
        break;
      }
  if (!TestNotNull(TEXT("test world available"), TestWorld))
    return false;
  UMassCrowdRuntimeSubsystem* Subsystem =
    TestWorld->GetSubsystem<UMassCrowdRuntimeSubsystem>();
  if (!TestNotNull(TEXT("runtime subsystem available"), Subsystem))
    return false;
  FCrowdAsyncSimulationRuntime* First =
    &Subsystem->GetAsyncSimulationRuntime();
  FCrowdAsyncSimulationRuntime* Second =
    &Subsystem->GetAsyncSimulationRuntime();
  TestEqual(TEXT("world owns one persistent runtime facade"),
    First, Second);
  TestEqual(TEXT("per-world runtime is stopped before product start"),
    First->GetState(),
    ECrowdAsyncSimulationRuntimeState::Stopped);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerShadowCodecTest,
  "MassCrowd.Runtime.WorkerShadow.CanonicalCodec",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerShadowCodecTest::RunTest(
  const FString& Parameters)
{
  FCrowdMassBoundaryAgentRecord Agent =
    MakeBoundaryAgent(10, 100, 1, FVector(1.231, 5.678, 9.101));
  FCrowdWorkerPayload First;
  FCrowdWorkerPayload Second;
  TestTrue(TEXT("boundary state encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeState(Agent, First));
  Agent.State.Position.X += 0.001;
  TestTrue(TEXT("equivalent quantized state encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeState(Agent, Second));
  TestEqual(TEXT("sub-centimeter source noise is canonical"),
    First, Second);
  Agent.State.Position.X += 0.02;
  TestTrue(TEXT("changed state encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeState(Agent, Second));
  TestNotEqual(TEXT("centimeter state change changes payload hash"),
    First.StableHash, Second.StableHash);
  TestTrue(TEXT("schema payload remains bounded"),
    First.Bytes.Num() < 256);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerShadowLifecycleTest,
  "MassCrowd.Runtime.WorkerShadow.LifecycleDirtyAndHashCompare",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerShadowLifecycleTest::RunTest(
  const FString& Parameters)
{
  FCrowdAsyncSimulationRuntime Runtime;
  FCrowdWorkerBoundaryShadowSync Shadow;
  const FCrowdWorkerShadowSyncConfig Config = MakeShadowConfig();
  TestTrue(TEXT("shadow runtime starts"),
    Shadow.Start(Runtime, Config, 31));

  TArray<FCrowdMassBoundaryAgentRecord> Records;
  Records.Add(MakeBoundaryAgent(
    20, 200, 1, FVector(200.0, 0.0, 0.0)));
  Records.Add(MakeBoundaryAgent(
    10, 100, 1, FVector(100.0, 0.0, 0.0)));
  const FCrowdMassBoundarySnapshot Initial =
    MakeBoundarySnapshot(0, Records);
  TestTrue(TEXT("initial source snapshot valid"), Initial.bValid);
  TestEqual(TEXT("initial shadow resnapshot queues"),
    Shadow.SubmitSnapshot(Runtime, Initial, 1.0 / 30.0),
    ECrowdWorkerShadowSubmitResult::Accepted);
  TestTrue(TEXT("initial mirror comparison completes"),
    DriveShadowUntilCompared(Shadow, Runtime, 1));
  TestFalse(TEXT("initial mirror has no violation"),
    Shadow.HasViolation());

  Records[0] = MakeBoundaryAgent(
    20, 200, 2, FVector(205.0, 0.0, 0.0));
  Records[1].State.Position.X += 25.0;
  const FCrowdMassBoundarySnapshot Incremental =
    MakeBoundarySnapshot(1, Records);
  TestTrue(TEXT("incremental source snapshot valid"),
    Incremental.bValid);
  FCrowdBehaviorSourceCommand BehaviorCommand;
  BehaviorCommand.EffectiveFixedStep = 1;
  BehaviorCommand.Handle.EntityRef = {1, 100, 1};
  BehaviorCommand.Handle.ControllerId.Value = 10;
  BehaviorCommand.Handle.SourceSequence = 1;
  BehaviorCommand.CommandSequence = 1;
  BehaviorCommand.Kind = ECrowdBehaviorSourceCommandKind::Start;
  BehaviorCommand.SourceTypeId.Value = 20;
  BehaviorCommand.LifetimeSteps = 10;
  const uint32 CommandValue = 77;
  TestTrue(TEXT("behavior command payload encodes"),
    BehaviorCommand.Payload.Set(30, CommandValue));
  TestTrue(TEXT("behavior command fixture valid"),
    BehaviorCommand.IsValid());
  const FCrowdBehaviorSourceCommand BehaviorCommands[] = {
    BehaviorCommand};
  TestEqual(TEXT("dirty lifecycle snapshot queues"),
    Shadow.SubmitSnapshot(
      Runtime, Incremental, 2.0 / 30.0, BehaviorCommands),
    ECrowdWorkerShadowSubmitResult::Accepted);
  TestEqual(
    TEXT("pending snapshot resolves its exact input sequence"),
    Shadow.ResolveInputSequenceForSnapshotHash(
      Incremental.StableHash),
    Shadow.GetMetrics().LastSubmittedInputSequence);
  TestTrue(TEXT("dirty mirror comparison completes"),
    DriveShadowUntilCompared(Shadow, Runtime, 2));

  const FCrowdWorkerShadowSyncMetrics& Metrics =
    Shadow.GetMetrics();
  TestEqual(TEXT("two source snapshots submitted"),
    Metrics.SubmittedSnapshotCount, uint64{2});
  TestEqual(TEXT("two source snapshots compared"),
    Metrics.ComparedSnapshotCount, uint64{2});
  TestEqual(TEXT("initial plus dirty records only"),
    Metrics.SubmittedInputRecordCount, uint64{10});
  TestEqual(TEXT("command journal enters worker input"),
    Metrics.SubmittedCommandRecordCount, uint64{1});
  TestEqual(TEXT("latest Mass snapshot hash acknowledged"),
    Metrics.LastComparedSourceSnapshotHash,
    Incremental.StableHash);
  TestEqual(
    TEXT("compared snapshot keeps exact input sequence mapping"),
    Shadow.ResolveInputSequenceForSnapshotHash(
      Incremental.StableHash),
    Metrics.LastComparedInputSequence);
  TestEqual(TEXT("entity set hash matches"),
    Metrics.LastExpectedEntitySetHash,
    Metrics.LastObservedEntitySetHash);
  TestEqual(TEXT("state hash matches"),
    Metrics.LastExpectedStateHash,
    Metrics.LastObservedStateHash);
  TestFalse(TEXT("lifecycle dirty compare has no violation"),
    Metrics.bViolation);

  FCrowdWorkerMirrorSnapshot Mirror;
  TestTrue(TEXT("shadow mirror readable"),
    Runtime.ReadMirrorSnapshot(Mirror));
  TestEqual(TEXT("shadow mirror count"), Mirror.EntityRefs.Num(), 2);
  TestEqual(TEXT("slot reuse carries higher lifecycle"),
    Mirror.EntityRefs[1], FCrowdStableEntityRef({1, 200, 2}));
  TestEqual(TEXT("snapshot metadata resource mirrored"),
    Mirror.ResourceIds.Num(), 1);
  TestTrue(TEXT("shadow runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerShadowAutonomousFrameTest,
  "MassCrowd.Runtime.WorkerShadow.AutonomousFrameNoStateEcho",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerShadowAutonomousFrameTest::RunTest(
  const FString& Parameters)
{
  FCrowdAsyncSimulationRuntime Runtime;
  FCrowdWorkerBoundaryShadowSync Shadow;
  const FCrowdWorkerShadowSyncConfig Config = MakeShadowConfig();
  TestTrue(TEXT("autonomous runtime starts"),
    Shadow.Start(Runtime, Config, 32));
  TArray<FCrowdMassBoundaryAgentRecord> Records;
  Records.Add(MakeBoundaryAgent(
    10, 100, 1, FVector(100.0, 0.0, 0.0)));
  Records.Add(MakeBoundaryAgent(
    20, 200, 1, FVector(200.0, 0.0, 0.0)));
  const FCrowdMassBoundarySnapshot Initial =
    MakeBoundarySnapshot(0, Records);
  TestEqual(TEXT("autonomous bootstrap queues"),
    Shadow.SubmitSnapshot(Runtime, Initial, 1.0 / 30.0),
    ECrowdWorkerShadowSubmitResult::Accepted);
  TestTrue(TEXT("autonomous bootstrap compares"),
    DriveShadowUntilCompared(Shadow, Runtime, 1));
  const uint64 BootstrapRecordCount =
    Shadow.GetMetrics().SubmittedInputRecordCount;
  const uint64 BootstrapStateHash =
    Shadow.GetMetrics().LastObservedStateHash;

  TestEqual(TEXT("autonomous frame queues"),
    Shadow.SubmitAutonomousFrame(
      Runtime, 1, 1, 2.0 / 30.0),
    ECrowdWorkerShadowSubmitResult::Accepted);
  TestTrue(TEXT("autonomous frame compares"),
    DriveShadowUntilCompared(Shadow, Runtime, 2));
  const FCrowdWorkerShadowSyncMetrics& Metrics =
    Shadow.GetMetrics();
  TestEqual(TEXT("autonomous frame submits only clock intent"),
    Metrics.SubmittedInputRecordCount,
    BootstrapRecordCount + 1);
  TestEqual(TEXT("autonomous frame does not serialize full state"),
    Metrics.FullStateSerializationCount, uint64{2});
  TestEqual(TEXT("autonomous frame does not serialize snapshot resource"),
    Metrics.SnapshotResourceSerializationCount, uint64{1});
  TestEqual(TEXT("autonomous frame preserves input-state hash"),
    Metrics.LastObservedStateHash, BootstrapStateHash);
  TestFalse(TEXT("autonomous frame has no violation"),
    Metrics.bViolation);
  TestTrue(TEXT("autonomous runtime stops"),
    Runtime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerShadowNetworkCheckpointStartTest,
  "MassCrowd.Runtime.WorkerShadow.NetworkCheckpointStart",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerShadowNetworkCheckpointStartTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  const FCrowdWorkerShadowSyncConfig Config = MakeShadowConfig();
  TArray<FCrowdMassBoundaryAgentRecord> Records;
  Records.Add(MakeBoundaryAgent(
    10, 100, 1, FVector(100.0, 0.0, 0.0)));
  Records.Add(MakeBoundaryAgent(
    20, 200, 1, FVector(200.0, 0.0, 0.0)));
  const FCrowdMassBoundarySnapshot Initial =
    MakeBoundarySnapshot(0, Records);

  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("checkpoint state store resets"),
    States.Reset(8, 256));
  uint64 InputSequence = 1;
  for (const FCrowdMassBoundaryAgentRecord& Record : Initial.Agents)
  {
    FCrowdWorkerPayload Payload;
    TestTrue(TEXT("checkpoint state encodes"),
      FCrowdWorkerBoundaryStateCodec::EncodeState(Record, Payload));
    TestEqual(TEXT("checkpoint entity spawns"),
      States.Spawn(
        Record.AgentFacts.StableEntityRef,
        41,
        InputSequence++,
        Payload),
      ECrowdWorkerQueueResult::Added);
  }
  FCrowdWorkerPayload SnapshotPayload;
  TestTrue(TEXT("checkpoint snapshot resource encodes"),
    FCrowdWorkerBoundaryStateCodec::EncodeSnapshotResource(
      Initial, SnapshotPayload));
  FCrowdWorkerResourceStore Resources;
  TestTrue(TEXT("checkpoint resource store resets"),
    Resources.Reset(256));
  TestEqual(TEXT("checkpoint snapshot resource stages"),
    Resources.StageBuilding({
      FCrowdWorkerBoundaryStateCodec::SnapshotResourceId,
      1,
      SnapshotPayload}),
    ECrowdWorkerQueueResult::Added);
  TArray<FCrowdWorkerResourceRevisionEvent> ResourceEvents;
  TestTrue(TEXT("checkpoint snapshot resource commits"),
    Resources.CommitBuildingAtEpoch(1, ResourceEvents));
  TArray<FCrowdWorkerDirtyStateRecord> CompleteStates;
  TArray<FCrowdWorkerResourceRecord> CompleteResources;
  States.GetStateRecords(CompleteStates);
  Resources.GetCurrentRecords(CompleteResources);
  FCrowdWorkerCheckpoint Header;
  Header.Generation = 41;
  Header.WorkerEpoch = 1;
  Header.AbsoluteSimulationTick = 1;
  Header.LastAppliedInputSequence = InputSequence;
  Header.EntityStateHash = States.CalculateStableHash();
  Header.ResourceRevisionHash = Resources.CalculateCurrentStableHash();
  Header.RecalculateStableHash();
  FCrowdWorkerNetworkContinuationState Continuation;
  Continuation.WorkRing.Epoch = 2;
  States.GetLifecycleWatermarks(Continuation.LifecycleWatermarks);
  FCrowdWorkerNetworkStatePublisher Publisher;
  TestTrue(TEXT("checkpoint publisher resets"),
    Publisher.Reset(Config.RuntimeConfig.NetworkState, 41));
  TestTrue(TEXT("checkpoint epoch commits"),
    Publisher.CommitEpoch(
      Header,
      CompleteStates,
      CompleteResources,
      Continuation));
  FCrowdWorkerNetworkCheckpoint Checkpoint;
  TestEqual(TEXT("authority checkpoint reads"),
    Publisher.ReadCheckpoint(41, Checkpoint),
    ECrowdWorkerNetworkReadResult::Ready);

  FCrowdAsyncSimulationRuntime ClientRuntime;
  FCrowdWorkerBoundaryShadowSync ClientShadow;
  TestTrue(TEXT("client starts from network checkpoint"),
    ClientShadow.StartFromNetworkCheckpoint(
      ClientRuntime, Config, Checkpoint));
  TestEqual(TEXT("client adopts checkpoint generation"),
    ClientShadow.GetGeneration(), uint64{41});
  TestEqual(TEXT("unchanged client snapshot submits as intent"),
    ClientShadow.SubmitSnapshot(
      ClientRuntime, Initial, 2.0 / 30.0),
    ECrowdWorkerShadowSubmitResult::Accepted);
  TestTrue(TEXT("checkpoint-based intent completes"),
    DriveShadowUntilCompared(ClientShadow, ClientRuntime, 1));
  TestEqual(TEXT("checkpoint baseline prevents duplicate spawns"),
    ClientShadow.GetMetrics().SubmittedInputRecordCount,
    uint64{2});
  TestTrue(TEXT("client runtime stops"),
    ClientRuntime.StopAndDrain(5.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerMovementAuthorityCutoverTest,
  "MassCrowd.Runtime.WorkerMovement.AuthorityCutoverAndEcho",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerMovementAuthorityCutoverTest::RunTest(
  const FString& Parameters)
{
  const FCrowdStableEntityRef First = {1, 100, 1};
  const FCrowdStableEntityRef Second = {1, 200, 1};
  const FCrowdStableEntityRef Entities[] = {First, Second};
  FCrowdWorkerMovementAuthority Authority;
  TestTrue(TEXT("shadow authority resets"),
    Authority.ResetQuiescent(
      7, ECrowdWorkerMovementAuthorityMode::Shadow));
  TestTrue(TEXT("shadow lifecycle set accepted"),
    Authority.UpdateCurrentEntities(7, Entities));
  TestFalse(TEXT("shadow mode retains Mass ownership"),
    Authority.IsWorkerOwner(First));
  TestTrue(TEXT("shadow normal movement input is legal"),
    Authority.ValidateNormalInput(
      First, CrowdWorkerMovementFields::Movement));
  const FCrowdWorkerMovementState ShadowState =
    MakeMovementState(1.0, 10.0, 20.0f);
  TestEqual(TEXT("shadow worker result accepted for compare"),
    Authority.AcceptPatch(MakeMovementPatch(
      First, 1, 10, 1, ShadowState)),
    ECrowdWorkerMovementAcceptResult::Accepted);
  TestTrue(TEXT("shadow exact result matches"),
    Authority.CompareShadow(
      First, ShadowState, 0.001, 0.001, 0.001));

  const FCrowdStableEntityRef Canaries[] = {First};
  TestTrue(TEXT("canary authority resets"),
    Authority.ResetQuiescent(
      8, ECrowdWorkerMovementAuthorityMode::Canary,
      Canaries));
  TestTrue(TEXT("canary lifecycle set accepted"),
    Authority.UpdateCurrentEntities(8, Entities));
  TestTrue(TEXT("selected entity is worker owned"),
    Authority.IsWorkerOwner(First));
  TestFalse(TEXT("unselected entity stays Mass owned"),
    Authority.IsWorkerOwner(Second));
  TestFalse(TEXT("worker-owned movement echo is rejected"),
    Authority.ValidateNormalInput(
      First, CrowdWorkerMovementFields::Movement));
  TestTrue(TEXT("external-only canary input remains legal"),
    Authority.ValidateNormalInput(First, 1ull << 20));
  TestTrue(TEXT("Mass-owned movement input remains legal"),
    Authority.ValidateNormalInput(
      Second, CrowdWorkerMovementFields::Movement));
  TestEqual(TEXT("worker result for Mass owner is rejected"),
    Authority.AcceptPatch(MakeMovementPatch(
      Second, 1, 10, 1, ShadowState, 8)),
    ECrowdWorkerMovementAcceptResult::RejectedOwner);

  TestTrue(TEXT("production authority resets"),
    Authority.ResetQuiescent(
      9, ECrowdWorkerMovementAuthorityMode::Production));
  TestTrue(TEXT("production lifecycle set accepted"),
    Authority.UpdateCurrentEntities(9, Entities));
  TestTrue(TEXT("all production entities worker owned"),
    Authority.IsWorkerOwner(Second));
  TestFalse(TEXT("production movement echo is rejected"),
    Authority.ValidateNormalInput(
      Second, CrowdWorkerMovementFields::Velocity));
  const FCrowdWorkerMovementAuthorityMetrics& Metrics =
    Authority.GetMetrics();
  TestEqual(TEXT("production echo rejection counted"),
    Metrics.RejectedEchoCount, uint64{1});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerMovementCorrectionInterpolationTest,
  "MassCrowd.Runtime.WorkerMovement.CorrectionAndInterpolation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerMovementCorrectionInterpolationTest::RunTest(
  const FString& Parameters)
{
  const FCrowdStableEntityRef Ref = {1, 100, 1};
  const FCrowdStableEntityRef Entities[] = {Ref};
  FCrowdWorkerMovementAuthority Authority;
  TestTrue(TEXT("production authority resets"),
    Authority.ResetQuiescent(
      7, ECrowdWorkerMovementAuthorityMode::Production));
  TestTrue(TEXT("production lifecycle set accepted"),
    Authority.UpdateCurrentEntities(7, Entities));
  const FCrowdWorkerMovementState First =
    MakeMovementState(1.0, 0.0, 170.0f);
  const FCrowdWorkerMovementState Second =
    MakeMovementState(2.0, 100.0, -170.0f);
  TestEqual(TEXT("first movement patch accepted"),
    Authority.AcceptPatch(
      MakeMovementPatch(Ref, 1, 10, 1, First)),
    ECrowdWorkerMovementAcceptResult::Accepted);
  TestEqual(TEXT("second movement patch accepted"),
    Authority.AcceptPatch(
      MakeMovementPatch(Ref, 2, 20, 2, Second)),
    ECrowdWorkerMovementAcceptResult::Accepted);
  TestEqual(TEXT("old epoch rejected"),
    Authority.AcceptPatch(
      MakeMovementPatch(Ref, 1, 30, 3, Second)),
    ECrowdWorkerMovementAcceptResult::RejectedSequence);

  FCrowdWorkerMovementSample Sample;
  TestTrue(TEXT("interpolation sample available"),
    Authority.Sample(Ref, 1.5, Sample));
  TestTrue(TEXT("sample is interpolated"), Sample.bInterpolated);
  TestTrue(TEXT("position interpolates midpoint"),
    Sample.Position.Equals(FVector(50.0, 2.0, 3.0), 0.001));
  TestTrue(TEXT("yaw follows shortest arc"),
    FMath::IsNearlyEqual(
      FMath::Abs(Sample.YawDegrees), 180.0f, 0.001f));

  FCrowdWorkerCorrectionDelta Correction;
  Correction.InputSequence = 30;
  Correction.EntityRef = Ref;
  Correction.CorrectionRevision = 5;
  Correction.DirtyMask = CrowdWorkerMovementFields::Movement;
  const FCrowdWorkerMovementState Corrected =
    MakeMovementState(1.75, 75.0, 90.0f, 5);
  TestTrue(TEXT("correction payload encodes"),
    FCrowdWorkerMovementStateCodec::Encode(
      Corrected, Correction.FullState));
  TestEqual(TEXT("new correction overwrites history"),
    Authority.ApplyCorrection(Correction, 7, 3),
    ECrowdWorkerMovementAcceptResult::AcceptedCorrection);
  TestEqual(TEXT("duplicate correction rejected"),
    Authority.ApplyCorrection(Correction, 7, 4),
    ECrowdWorkerMovementAcceptResult::
      RejectedCorrectionRevision);
  TestTrue(TEXT("corrected sample available"),
    Authority.Sample(Ref, 1.5, Sample));
  TestFalse(TEXT("correction clears interpolation history"),
    Sample.bInterpolated);
  TestTrue(TEXT("correction position is authoritative"),
    Sample.Position.Equals(Corrected.Position, 0.001));
  TestEqual(TEXT("correction revision preserved"),
    Sample.CorrectionRevision, uint64{5});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdWorkerConsistencyDomainsTest,
  "MassCrowd.Runtime.WorkerDomains.FailClosedEvidence",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdWorkerConsistencyDomainsTest::RunTest(
  const FString& Parameters)
{
  FCrowdWorkerConsistencyEvidence Particle;
  Particle.Domain = ECrowdWorkerConsistencyDomain::
    ParticleInteractionIsland;
  Particle.Generation = 7;
  Particle.DomainKey = 100;
  Particle.InputEpoch = 10;
  Particle.EnvironmentRevision = 3;
  Particle.EntityCount = 20;
  Particle.bStableMembership = true;
  Particle.bNetworkSemanticsFrozen = true;
  FCrowdWorkerConsistencyEvaluation Result =
    FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Particle);
  TestEqual(TEXT("open particle island stays Boundary"),
    Result.Decision,
    ECrowdWorkerConsistencyDecision::KeepBoundary);
  TestEqual(TEXT("particle reason is explicit"),
    Result.Failure,
    ECrowdWorkerConsistencyFailure::OpenInteractionBoundary);
  Particle.bClosedInteractionBoundary = true;
  TestEqual(TEXT("closed particle island is eligible"),
    FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Particle).
      Decision,
    ECrowdWorkerConsistencyDecision::EligibleForWorkerPatch);

  FCrowdWorkerConsistencyEvidence Target;
  Target.Domain =
    ECrowdWorkerConsistencyDomain::TargetCohort;
  Target.Generation = 7;
  Target.DomainKey = 200;
  Target.InputEpoch = 10;
  Target.EnvironmentRevision = 3;
  Target.EntityCount = 20;
  Target.bStableMembership = true;
  Target.bNetworkSemanticsFrozen = true;
  Result = FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Target);
  TestEqual(TEXT("cohort without atomic plan stays Boundary"),
    Result.Failure,
    ECrowdWorkerConsistencyFailure::MissingAtomicPlan);
  Target.bAtomicPlan = true;
  TestEqual(TEXT("complete cohort plan is eligible"),
    FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Target).
      Decision,
    ECrowdWorkerConsistencyDecision::EligibleForWorkerPatch);

  FCrowdWorkerConsistencyEvidence Combat;
  Combat.Domain =
    ECrowdWorkerConsistencyDomain::CombatEventBoundary;
  Combat.Generation = 7;
  Combat.DomainKey = 300;
  Combat.InputEpoch = 10;
  Combat.EntityCount = 20;
  Combat.bStableMembership = true;
  Combat.bNetworkSemanticsFrozen = true;
  Combat.FirstEventSequence = 10;
  Combat.LastEventSequence = 12;
  Combat.EventCount = 3;
  Combat.bOrderedEvents = true;
  Result = FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Combat);
  TestEqual(TEXT("combat without idempotency stays Boundary"),
    Result.Failure,
    ECrowdWorkerConsistencyFailure::MissingIdempotency);
  Combat.bIdempotencyProven = true;
  Result = FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Combat);
  TestEqual(TEXT("combat without rollback stays Boundary"),
    Result.Failure,
    ECrowdWorkerConsistencyFailure::MissingRollbackProof);
  Combat.bRollbackProven = true;
  TestEqual(TEXT("complete combat event proof is eligible"),
    FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Combat).
      Decision,
    ECrowdWorkerConsistencyDecision::EligibleForWorkerPatch);

  Target.bAtomicPlan = true;
  Target.ExternalDependencyCount = 1;
  TestEqual(TEXT("cross-domain dependency always stays Boundary"),
    FCrowdWorkerConsistencyDomainEvaluator::Evaluate(Target).
      Failure,
    ECrowdWorkerConsistencyFailure::CrossDomainDependency);
  return true;
}

#endif
