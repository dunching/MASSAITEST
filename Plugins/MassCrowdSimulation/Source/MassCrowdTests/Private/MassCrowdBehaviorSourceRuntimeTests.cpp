#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdTestBehaviorProvider.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerResultApply.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdCapabilityBinding MakeLegacyBinding()
  {
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey =
      CrowdBuiltinBehaviorSchemas::LegacyFullProfile;
    return Binding;
  }

  FCrowdBehaviorSourceCommand MakeBuiltinCommand(
    const FCrowdStableEntityRef EntityRef,
    const uint32 CommandSequence,
    const uint32 SourceSequence,
    const FCrowdBehaviorSourceTypeId TypeId,
    const FVector Vector,
    const uint32 PrimaryId = 0,
    const uint32 SecondaryId = 0,
    const int32 Lifetime = 0,
    const uint32 Flags = 0)
  {
    FCrowdBuiltinBehaviorSourcePayload Payload;
    Payload.Vector = Vector;
    Payload.PrimaryId = PrimaryId;
    Payload.SecondaryId = SecondaryId;
    Payload.Flags = Flags;
    FCrowdBehaviorSourceCommand Command;
    Command.EffectiveFixedStep = 10;
    Command.Handle = {EntityRef, {1}, SourceSequence};
    Command.CommandSequence = CommandSequence;
    Command.Kind = ECrowdBehaviorSourceCommandKind::Start;
    Command.SourceTypeId = TypeId;
    Command.LifetimeSteps = Lifetime;
    Command.Payload.Set(
      CrowdBuiltinBehaviorSchemas::Standard, Payload);
    return Command;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorSourceRuntimeAtomicTest,
  "MassCrowd.BehaviorSource.RuntimeAtomicBoundary",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorSourceRuntimeAtomicTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("providers initialize"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 100, 1};
  TestTrue(TEXT("entity registers"),
    Runtime.RegisterEntity(EntityRef, MakeLegacyBinding()));
  TestEqual(TEXT("registration stages initial worker binding"),
    Runtime.GetPendingBindingUpdates().Num(), 1);

  const FCrowdBehaviorSourceCommand Commands[] = {
    MakeBuiltinCommand(EntityRef, 1, 1,
      CrowdBuiltinSourceTypeIds::MoveToSink,
      FVector(100.0, 0.0, 0.0)),
    MakeBuiltinCommand(EntityRef, 2, 2,
      CrowdBuiltinSourceTypeIds::SharedFlow,
      FVector(0.0, 100.0, 0.0),
      CrowdBehavior::FullQ15Weight),
    MakeBuiltinCommand(EntityRef, 3, 3,
      CrowdBuiltinSourceTypeIds::FaceMovement,
      FVector(1.0, 0.0, 0.0)),
    MakeBuiltinCommand(EntityRef, 4, 4,
      CrowdBuiltinSourceTypeIds::Formation,
      FVector(0.0, 10.0, 0.0)),
    MakeBuiltinCommand(EntityRef, 5, 5,
      CrowdBuiltinSourceTypeIds::CarryCargo,
      FVector::ZeroVector, 1, 9),
    MakeBuiltinCommand(EntityRef, 6, 6,
      CrowdBuiltinSourceTypeIds::HitReaction,
      FVector::ZeroVector, 0, 0, 2, 1)};
  for (const FCrowdBehaviorSourceCommand& Command : Commands)
    TestTrue(TEXT("command queues"), Runtime.QueueCommand(Command));
  FCrowdBehaviorEntityEvaluationContext Context;
  Context.EntityRef = EntityRef;
  Context.FixedStepIndex = 10;
  Context.Position = FVector(10.0, 20.0, 30.0);
  Context.Velocity = FVector(1.0, 2.0, 3.0);
  Context.Facing = FVector::ForwardVector;
  Context.RecalculateStableHash();
  TestTrue(TEXT("evaluation context stages"),
    Runtime.SetEvaluationContext(Context));

  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("six-source boundary prepares"),
    Runtime.PrepareBoundary(10, Prepared));
  TestEqual(TEXT("all sources coexist"),
    Prepared.Entities[0].StagedSourceSet.Instances.Num(), 6);
  TestTrue(TEXT("hit reaction locks only movement"),
    Prepared.Entities[0].ResolvedChannels.bMovementLocked);
  TestEqual(TEXT("cargo presentation remains"),
    Prepared.Entities[0].ResolvedChannels.Presentation.Num(), 1);
  TestEqual(TEXT("uncommitted commands are not journaled"),
    Runtime.GetWorkerInputCommandJournal().Num(), 0);
  TestEqual(TEXT("uncommitted contexts are not journaled"),
    Runtime.GetWorkerInputContextJournal().Num(), 0);
  TestTrue(TEXT("prepared boundary commits"),
    Runtime.CommitPrepared(Prepared));
  TestEqual(TEXT("committed commands enter worker input journal"),
    Runtime.GetWorkerInputCommandJournal().Num(), 6);
  TestEqual(TEXT("matching committed context enters worker journal"),
    Runtime.GetWorkerInputContextJournal().Num(), 1);
  TestEqual(TEXT("initial binding enters worker journal"),
    Runtime.GetWorkerInputBindingJournal().Num(), 1);
  TestEqual(TEXT("journal keeps exact committed context"),
    Runtime.GetWorkerInputContextJournal()[0].StableHash,
    Context.StableHash);
  TestTrue(TEXT("worker input journal acknowledges atomically"),
    Runtime.AcknowledgeWorkerInputCommands(6));
  TestTrue(TEXT("worker context journal acknowledges atomically"),
    Runtime.AcknowledgeWorkerInputContexts(1));
  TestTrue(TEXT("worker binding journal acknowledges atomically"),
    Runtime.AcknowledgeWorkerInputBindings(1));
  TestEqual(TEXT("acknowledged journal drains"),
    Runtime.GetWorkerInputCommandJournal().Num(), 0);
  TestEqual(TEXT("acknowledged context journal drains"),
    Runtime.GetWorkerInputContextJournal().Num(), 0);
  TestEqual(TEXT("acknowledged binding journal drains"),
    Runtime.GetWorkerInputBindingJournal().Num(), 0);

  const FCrowdBehaviorSourceSet* Committed =
    Runtime.FindSourceSet(EntityRef);
  TestNotNull(TEXT("committed set is queryable"), Committed);
  TestEqual(TEXT("source-set revision increments once"),
    Committed ? Committed->Revision : 0u, 2u);

  Context.FixedStepIndex = 12;
  Context.RecalculateStableHash();
  TestTrue(TEXT("resumed context stages"),
    Runtime.SetEvaluationContext(Context));
  FCrowdBehaviorPreparedBoundary Resumed;
  TestTrue(TEXT("expiry boundary prepares"),
    Runtime.PrepareBoundary(12, Resumed));
  TestEqual(TEXT("only hit source expires"),
    Resumed.Entities[0].StagedSourceSet.Instances.Num(), 5);
  TestFalse(TEXT("movement lock expires"),
    Resumed.Entities[0].ResolvedChannels.bMovementLocked);
  TestTrue(TEXT("lower-priority movement resumes"),
    !Resumed.Entities[0].ResolvedChannels.DesiredVelocity.IsNearlyZero());
  TestEqual(TEXT("cargo presentation still remains"),
    Resumed.Entities[0].ResolvedChannels.Presentation.Num(), 1);
  TestTrue(TEXT("resumed boundary commits"),
    Runtime.CommitPrepared(Resumed));

  FCrowdBehaviorSourceCommand Gap = MakeBuiltinCommand(
    EntityRef, 8, 7, CrowdBuiltinSourceTypeIds::SharedFlow,
    FVector(5.0, 0.0, 0.0));
  Gap.EffectiveFixedStep = 13;
  TestTrue(TEXT("well-formed gap reaches transaction"),
    Runtime.QueueCommand(Gap));
  Context.FixedStepIndex = 13;
  Context.RecalculateStableHash();
  TestTrue(TEXT("gap context stages"),
    Runtime.SetEvaluationContext(Context));
  const uint64 BeforeRejectedHash =
    Runtime.FindSourceSet(EntityRef)->StableHash;
  FCrowdBehaviorPreparedBoundary Rejected;
  TestFalse(TEXT("sequence gap rejects whole boundary"),
    Runtime.PrepareBoundary(13, Rejected));
  TestEqual(TEXT("rejected boundary performs zero writes"),
    Runtime.FindSourceSet(EntityRef)->StableHash,
    BeforeRejectedHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorSourcePreparedValidationTest,
  "MassCrowd.BehaviorSource.PreparedValidation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorSourcePreparedValidationTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("runtime initializes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 300, 1};
  const FCrowdCapabilityBinding Binding = MakeLegacyBinding();
  TestTrue(TEXT("entity registers"),
    Runtime.RegisterEntity(EntityRef, Binding));

  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("empty boundary prepares"),
    Runtime.PrepareBoundary(5, Prepared));
  FCrowdBehaviorPreparedBoundary Tampered = Prepared;
  ++Tampered.SourceSetHash;
  TestFalse(TEXT("aggregate hash tamper is rejected"),
    Runtime.ValidatePrepared(Tampered));
  TestTrue(TEXT("untampered boundary commits"),
    Runtime.CommitPrepared(Prepared));

  FCrowdCapabilityBinding First = Binding;
  First.ModifierRevision = 1;
  First.ModifierCount = 1;
  First.Modifiers[0] = {
    CrowdBuiltinCapabilityIds::Attack,
    ECrowdCapabilityModifierOperation::Remove};
  FCrowdCapabilityBinding Conflicting = First;
  Conflicting.Modifiers[0] = {
    CrowdBuiltinCapabilityIds::Haul,
    ECrowdCapabilityModifierOperation::Remove};
  TestTrue(TEXT("first binding update queues"),
    Runtime.QueueCapabilityBinding(6, EntityRef, First));
  TestTrue(TEXT("conflicting binding update queues for transaction"),
    Runtime.QueueCapabilityBinding(6, EntityRef, Conflicting));
  FCrowdBehaviorPreparedBoundary Rejected;
  TestFalse(TEXT("same-step conflicting binding rejects boundary"),
    Runtime.PrepareBoundary(6, Rejected));
  TestEqual(TEXT("conflict performs zero source writes"),
    Runtime.FindSourceSet(EntityRef)->Revision, 1u);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorSourceWorkerCommitTest,
  "MassCrowd.BehaviorSource.WorkerPreparedCommit",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorSourceWorkerCommitTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("runtime initializes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 400, 1};
  TestTrue(TEXT("entity registers"),
    Runtime.RegisterEntity(EntityRef, MakeLegacyBinding()));
  const FCrowdBehaviorSourceCommand Command = MakeBuiltinCommand(
    EntityRef, 1, 1, CrowdBuiltinSourceTypeIds::MoveToSink,
    FVector(300.0, 20.0, 0.0));
  TestTrue(TEXT("production command stages"),
    Runtime.QueueCommand(Command));
  FCrowdBehaviorEntityEvaluationContext Context;
  Context.EntityRef = EntityRef;
  Context.FixedStepIndex = 10;
  Context.Position = FVector(10.0, 0.0, 0.0);
  Context.Facing = FVector::ForwardVector;
  Context.RecalculateStableHash();
  TestTrue(TEXT("production context stages"),
    Runtime.SetEvaluationContext(Context));

  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("production boundary prepares"),
    Runtime.PrepareBoundary(10, Prepared));
  TestEqual(TEXT("one expected entity"), Prepared.Entities.Num(), 1);
  FCrowdBehaviorWorkerCommitEntity Worker;
  Worker.EntityRef = EntityRef;
  Worker.SourceSet = Prepared.Entities[0].StagedSourceSet;
  Worker.ResolvedChannels = Prepared.Entities[0].ResolvedChannels;
  Worker.EvaluationContextHash = Context.StableHash;

  FCrowdBehaviorWorkerCommitEntity Tampered = Worker;
  ++Tampered.EvaluationContextHash;
  TestFalse(TEXT("mismatched worker context rejects atomically"),
    Runtime.CommitWorkerPrepared(
      Prepared, MakeArrayView(&Tampered, 1), {}));
  TestEqual(TEXT("rejection retains pending command"),
    Runtime.GetPendingCommandCount(), 1);
  TestEqual(TEXT("rejection writes no worker journal"),
    Runtime.GetWorkerInputCommandJournal().Num(), 0);

  FCrowdBehaviorWorkerCommitEntity RevisionAdvanced = Worker;
  ++RevisionAdvanced.SourceSet.Revision;
  RevisionAdvanced.SourceSet.RecalculateStableHash();
  TestTrue(TEXT("worker-owned monotonic revision may advance autonomously"),
    Runtime.CommitWorkerPrepared(
      Prepared, MakeArrayView(&RevisionAdvanced, 1), {}));
  TestEqual(TEXT("autonomous revision is retained exactly"),
    Runtime.FindSourceSet(EntityRef)->Revision,
    RevisionAdvanced.SourceSet.Revision);
  TestFalse(TEXT("same transaction cannot commit twice"),
    Runtime.CommitWorkerPrepared(
      Prepared, MakeArrayView(&RevisionAdvanced, 1), {}));

  Runtime.Reset();
  TestTrue(TEXT("runtime reinitializes"),
    Runtime.InitializeFromRegisteredProviders());
  TestTrue(TEXT("entity re-registers"),
    Runtime.RegisterEntity(EntityRef, MakeLegacyBinding()));
  TestTrue(TEXT("production command restages"),
    Runtime.QueueCommand(Command));
  TestTrue(TEXT("production context restages"),
    Runtime.SetEvaluationContext(Context));
  TestTrue(TEXT("production boundary reprepares"),
    Runtime.PrepareBoundary(10, Prepared));
  Worker.EntityRef = EntityRef;
  Worker.SourceSet = Prepared.Entities[0].StagedSourceSet;
  Worker.ResolvedChannels = Prepared.Entities[0].ResolvedChannels;
  Worker.EvaluationContextHash = Context.StableHash;

  TestTrue(TEXT("validated worker state commits"),
    Runtime.CommitWorkerPrepared(
      Prepared, MakeArrayView(&Worker, 1),
      Prepared.Entities[0].Events));
  TestEqual(TEXT("worker commit consumes staged command"),
    Runtime.GetPendingCommandCount(), 0);
  TestEqual(TEXT("production path does not echo command to worker"),
    Runtime.GetWorkerInputCommandJournal().Num(), 0);
  TestEqual(TEXT("production path does not echo context to worker"),
    Runtime.GetWorkerInputContextJournal().Num(), 0);
  const FCrowdBehaviorSourceSet* Committed =
    Runtime.FindSourceSet(EntityRef);
  TestNotNull(TEXT("worker-owned source set is queryable"), Committed);
  TestEqual(TEXT("worker-owned source hash is exact"),
    Committed ? Committed->StableHash : 0ull,
    Worker.SourceSet.StableHash);
  TestFalse(TEXT("exact transaction cannot commit twice"),
    Runtime.CommitWorkerPrepared(
      Prepared, MakeArrayView(&Worker, 1), {}));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorSourceWorkerAuthoritativeSparseTest,
  "MassCrowd.BehaviorSource.WorkerAuthoritativeSparse",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorSourceWorkerAuthoritativeSparseTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("runtime initializes"),
    Runtime.InitializeFromRegisteredProviders());
  TArray<FCrowdStableEntityRef> EntityRefs{
    {1, 500, 1}, {1, 501, 1}};
  for (const FCrowdStableEntityRef& EntityRef : EntityRefs)
    TestTrue(TEXT("entity registers"),
      Runtime.RegisterEntity(EntityRef, MakeLegacyBinding()));

  TArray<FCrowdBehaviorEntityEvaluationContext> Contexts;
  for (int32 Index = 0; Index < EntityRefs.Num(); ++Index)
  {
    FCrowdBehaviorEntityEvaluationContext& Context =
      Contexts.AddDefaulted_GetRef();
    Context.EntityRef = EntityRefs[Index];
    Context.FixedStepIndex = 10;
    Context.Position = FVector(Index * 100.0, 0.0, 0.0);
    Context.Facing = FVector::ForwardVector;
    Context.RecalculateStableHash();
    TestTrue(TEXT("context stages"),
      Runtime.SetEvaluationContext(Context));
  }
  const FCrowdBehaviorSourceCommand Command = MakeBuiltinCommand(
    EntityRefs[0], 1, 1, CrowdBuiltinSourceTypeIds::MoveToSink,
    FVector(300.0, 20.0, 0.0));
  TestTrue(TEXT("command stages"), Runtime.QueueCommand(Command));
  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("boundary prepares"),
    Runtime.PrepareBoundary(10, Prepared));

  FCrowdWorkerBehaviorAuthority Authority;
  TestTrue(TEXT("authority initializes"),
    Authority.ResetQuiescent(
      1, ECrowdWorkerBehaviorAuthorityMode::Production));
  TestTrue(TEXT("authority receives membership"),
    Authority.UpdateCurrentEntities(1, EntityRefs));
  TestTrue(TEXT("one sparse context queues one content expectation"),
    Authority.QueuePreparedExpectation(
      1, 1, Prepared, MakeArrayView(&Contexts[0], 1), false));

  FCrowdWorkerBehaviorState Behavior;
  Behavior.LastFixedStep = 10;
  Behavior.LastAbsoluteSimulationTick = 11;
  Behavior.BusinessCommitLedgerHash = 14695981039346656037ull;
  Behavior.EvaluationContext = Contexts[0];
  Behavior.EvaluationContext.Position.X += 37.0;
  Behavior.EvaluationContext.RecalculateStableHash();
  Behavior.SourceSet = Prepared.Entities[0].StagedSourceSet;
  Behavior.ResolvedChannels = Prepared.Entities[0].ResolvedChannels;
  FCrowdWorkerStatePatch Patch;
  Patch.EntityRef = EntityRefs[0];
  Patch.StateFieldId = static_cast<uint16>(
    ECrowdWorkerField::Behavior) + 1;
  Patch.Generation = 1;
  Patch.WorkerEpoch = 1;
  Patch.SourceInputSequence = 1;
  Patch.DirtyMask = CrowdWorkerRuntimeV2FieldMask(
    ECrowdWorkerField::Behavior);
  Patch.State.StateRevision = 1;
  TestTrue(TEXT("behavior encodes"),
    FCrowdWorkerBehaviorStateCodec::Encode(
      Behavior, Patch.State.Payload));
  Patch.RecalculateStableHash();
  FCrowdWorkerPublishedBatch Batch;
  Batch.Generation = 1;
  Batch.PublishSequence = 1;
  Batch.MinWorkerEpoch = 1;
  Batch.MaxWorkerEpoch = 1;
  Batch.LastAppliedInputSequence = 1;
  Batch.StatePatches.Add(Patch);
  Batch.RecalculateStableHash();
  FCrowdWorkerResultApplyProxy Proxy;
  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 1024 * 1024;
  Limits.MaxInputRecordsPerBatch = 16;
  Limits.MaxStatePatchesPerSlot = 16;
  Limits.MaxPendingOrderedEvents = 16;
  TestTrue(TEXT("proxy initializes"),
    Proxy.ResetFromCheckpoint(1, Limits, EntityRefs, 0, 0));
  TestEqual(TEXT("single sparse behavior patch applies"),
    Proxy.Apply(Batch), ECrowdWorkerResultApplyResult::Applied);
  TestEqual(TEXT("missing unchanged entity does not block sparse match"),
    Authority.ValidateAvailable(Proxy),
    ECrowdWorkerBehaviorValidationResult::Matched);

  TArray<FCrowdBehaviorWorkerCommitEntity> WorkerEntities;
  for (const FCrowdBehaviorPreparedEntity& Entity : Prepared.Entities)
  {
    FCrowdBehaviorWorkerCommitEntity& Worker =
      WorkerEntities.AddDefaulted_GetRef();
    Worker.EntityRef = Entity.EntityRef;
    Worker.SourceSet = Entity.StagedSourceSet;
    Worker.ResolvedChannels = Entity.ResolvedChannels;
    Worker.EvaluationContextHash = Entity.EvaluationContextHash + 1;
  }
  TestTrue(TEXT("authoritative commit accepts Worker kinematic hashes"),
    Runtime.CommitWorkerAuthoritative(
      Prepared, WorkerEntities, {}));
  TestEqual(TEXT("authoritative commit consumes staged command"),
    Runtime.GetPendingCommandCount(), 0);
  TestFalse(TEXT("authoritative transaction cannot commit twice"),
    Runtime.CommitWorkerAuthoritative(
      Prepared, WorkerEntities, {}));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorAuthorityCommittedPredictedKinematicsTest,
  "MassCrowd.BehaviorSource.CommittedPredictedKinematics",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorAuthorityCommittedPredictedKinematicsTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("runtime initializes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 650, 1};
  TestTrue(TEXT("entity registers"),
    Runtime.RegisterEntity(EntityRef, MakeLegacyBinding()));

  FCrowdBehaviorEntityEvaluationContext CommittedContext;
  CommittedContext.EntityRef = EntityRef;
  CommittedContext.FixedStepIndex = 10;
  CommittedContext.Position = FVector(100.0, 20.0, 0.0);
  CommittedContext.Velocity = FVector(30.0, 0.0, 0.0);
  CommittedContext.Facing = FVector::ForwardVector;
  CommittedContext.RecalculateStableHash();
  TestTrue(TEXT("context stages"),
    Runtime.SetEvaluationContext(CommittedContext));
  const FCrowdBehaviorSourceCommand Command = MakeBuiltinCommand(
    EntityRef, 1, 1, CrowdBuiltinSourceTypeIds::MoveToSink,
    FVector(300.0, 20.0, 0.0));
  TestTrue(TEXT("source command stages"),
    Runtime.QueueCommand(Command));
  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("boundary prepares"),
    Runtime.PrepareBoundary(10, Prepared));
  TestTrue(TEXT("boundary commits"),
    Runtime.CommitPrepared(Prepared));

  FCrowdWorkerBehaviorAuthority Authority;
  TestTrue(TEXT("authority initializes"),
    Authority.ResetQuiescent(
      1, ECrowdWorkerBehaviorAuthorityMode::Shadow));
  const TArray<FCrowdStableEntityRef> EntityRefs{EntityRef};
  TestTrue(TEXT("authority receives membership"),
    Authority.UpdateCurrentEntities(1, EntityRefs));
  TestTrue(TEXT("ordinary prediction expectation queues"),
    Authority.QueueCommittedExpectation(
      1, 1, Runtime,
      MakeArrayView(&CommittedContext, 1), false, false));

  const FCrowdBehaviorSourceSet* SourceSet =
    Runtime.FindSourceSet(EntityRef);
  const FCrowdResolvedBehaviorChannels* Resolved =
    Runtime.FindResolvedChannels(EntityRef);
  TestNotNull(TEXT("committed source set exists"), SourceSet);
  TestNotNull(TEXT("committed resolved channels exist"), Resolved);
  if (!SourceSet || !Resolved) return false;

  FCrowdWorkerBehaviorState Behavior;
  Behavior.LastFixedStep = 10;
  Behavior.LastAbsoluteSimulationTick = 11;
  Behavior.BusinessCommitLedgerHash = 14695981039346656037ull;
  Behavior.EvaluationContext = CommittedContext;
  Behavior.EvaluationContext.Position.X += 37.0;
  Behavior.EvaluationContext.Velocity.Y += 12.0;
  Behavior.EvaluationContext.RecalculateStableHash();
  Behavior.SourceSet = *SourceSet;
  TestTrue(TEXT("committed source has an instance"),
    !Behavior.SourceSet.Instances.IsEmpty());
  if (Behavior.SourceSet.Instances.IsEmpty()) return false;
  const uint64 PredictedEvaluatorState = 1;
  TestTrue(TEXT("predicted evaluator state stages"),
    Behavior.SourceSet.Instances[0].State.Set(
      0x50524544u, PredictedEvaluatorState));
  Behavior.SourceSet.RecalculateStableHash();
  Behavior.ResolvedChannels = *Resolved;

  FCrowdWorkerStatePatch Patch;
  Patch.EntityRef = EntityRef;
  Patch.StateFieldId = static_cast<uint16>(
    ECrowdWorkerField::Behavior) + 1;
  Patch.Generation = 1;
  Patch.WorkerEpoch = 1;
  Patch.SourceInputSequence = 1;
  Patch.DirtyMask = CrowdWorkerRuntimeV2FieldMask(
    ECrowdWorkerField::Behavior);
  Patch.State.StateRevision = 1;
  TestTrue(TEXT("predicted behavior encodes"),
    FCrowdWorkerBehaviorStateCodec::Encode(
      Behavior, Patch.State.Payload));
  Patch.RecalculateStableHash();

  FCrowdWorkerPublishedBatch Batch;
  Batch.Generation = 1;
  Batch.PublishSequence = 1;
  Batch.MinWorkerEpoch = 1;
  Batch.MaxWorkerEpoch = 1;
  Batch.LastAppliedInputSequence = 1;
  Batch.StatePatches.Add(Patch);
  Batch.RecalculateStableHash();
  FCrowdWorkerResultApplyProxy Proxy;
  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 1024 * 1024;
  Limits.MaxInputRecordsPerBatch = 16;
  Limits.MaxStatePatchesPerSlot = 16;
  Limits.MaxPendingOrderedEvents = 16;
  TestTrue(TEXT("proxy initializes"),
    Proxy.ResetFromCheckpoint(1, Limits, EntityRefs, 0, 0));
  TestEqual(TEXT("predicted behavior patch applies"),
    Proxy.Apply(Batch), ECrowdWorkerResultApplyResult::Applied);
  TestEqual(TEXT("control parity ignores predicted state hashes"),
    Authority.ValidateAvailable(Proxy),
    ECrowdWorkerBehaviorValidationResult::Matched);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorAuthorityAutonomousNoEventCaptureTest,
  "MassCrowd.BehaviorSource.AutonomousNoEventCapture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorAuthorityAutonomousNoEventCaptureTest::RunTest(
  const FString& Parameters)
{
  const FCrowdStableEntityRef EntityRef{1, 700, 1};
  const TArray<FCrowdStableEntityRef> EntityRefs{EntityRef};
  FCrowdWorkerBehaviorAuthority Authority;
  TestTrue(TEXT("authority initializes with a two-batch event capacity"),
    Authority.ResetQuiescent(
      1, ECrowdWorkerBehaviorAuthorityMode::Production, {}, 2));
  TestTrue(TEXT("authority receives membership"),
    Authority.UpdateCurrentEntities(1, EntityRefs));

  FCrowdWorkerContractLimits Limits;
  Limits.MaxPayloadBytes = 1024 * 1024;
  Limits.MaxInputRecordsPerBatch = 16;
  Limits.MaxStatePatchesPerSlot = 16;
  Limits.MaxPendingOrderedEvents = 16;
  FCrowdWorkerResultApplyProxy Proxy;
  TestTrue(TEXT("proxy initializes"),
    Proxy.ResetFromCheckpoint(1, Limits, EntityRefs, 0, 0));

  FCrowdWorkerBehaviorState Behavior;
  Behavior.LastFixedStep = 1;
  Behavior.LastAbsoluteSimulationTick = 1;
  Behavior.BusinessCommitLedgerHash = 14695981039346656037ull;
  Behavior.EvaluationContext.EntityRef = EntityRef;
  Behavior.EvaluationContext.FixedStepIndex = 1;
  Behavior.EvaluationContext.Facing = FVector::ForwardVector;
  Behavior.EvaluationContext.RecalculateStableHash();
  Behavior.SourceSet.EntityRef = EntityRef;
  Behavior.SourceSet.CapabilityBinding = MakeLegacyBinding();
  Behavior.SourceSet.Revision = 1;
  Behavior.SourceSet.RecalculateStableHash();
  Behavior.ResolvedChannels.bValid = true;
  Behavior.ResolvedChannels.StableHash = 1;

  for (uint64 InputSequence = 1; InputSequence <= 4; ++InputSequence)
  {
    TestTrue(TEXT("non-consuming owner queues autonomous expectation"),
      Authority.QueueAutonomousExpectation(
        1, InputSequence, EntityRefs, false));
    FCrowdWorkerStatePatch Patch;
    Patch.EntityRef = EntityRef;
    Patch.StateFieldId = static_cast<uint16>(
      ECrowdWorkerField::Behavior) + 1;
    Patch.Generation = 1;
    Patch.WorkerEpoch = InputSequence;
    Patch.SourceInputSequence = InputSequence;
    Patch.DirtyMask = CrowdWorkerRuntimeV2FieldMask(
      ECrowdWorkerField::Behavior);
    Patch.State.StateRevision = InputSequence;
    TestTrue(TEXT("behavior encodes"),
      FCrowdWorkerBehaviorStateCodec::Encode(
        Behavior, Patch.State.Payload));
    Patch.RecalculateStableHash();
    FCrowdWorkerPublishedBatch Batch;
    Batch.Generation = 1;
    Batch.PublishSequence = InputSequence;
    Batch.MinWorkerEpoch = InputSequence;
    Batch.MaxWorkerEpoch = InputSequence;
    Batch.LastAppliedInputSequence = InputSequence;
    Batch.StatePatches.Add(MoveTemp(Patch));
    Batch.RecalculateStableHash();
    TestEqual(TEXT("batch applies"), Proxy.Apply(Batch),
      ECrowdWorkerResultApplyResult::Applied);
    TestEqual(TEXT("autonomous expectation matches"),
      Authority.ValidateAvailable(Proxy),
      ECrowdWorkerBehaviorValidationResult::Matched);
  }
  TArray<FCrowdBehaviorSourceEvent> MatchedEvents;
  TArray<FCrowdBusinessContribution> MatchedBusinessCommits;
  TestFalse(TEXT("non-consuming owner does not retain event batches"),
    Authority.PeekMatchedEvents(
      1, MatchedEvents, MatchedBusinessCommits));
  TestFalse(TEXT("event capacity does not latch a violation"),
    Authority.GetMetrics().bViolation);
  return true;
}

#endif
