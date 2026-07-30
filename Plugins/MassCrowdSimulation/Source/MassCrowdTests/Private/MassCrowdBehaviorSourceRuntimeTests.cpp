#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdTestBehaviorProvider.h"
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
  TestTrue(TEXT("prepared boundary commits"),
    Runtime.CommitPrepared(Prepared));
  TestEqual(TEXT("committed commands enter worker input journal"),
    Runtime.GetWorkerInputCommandJournal().Num(), 6);
  TestTrue(TEXT("worker input journal acknowledges atomically"),
    Runtime.AcknowledgeWorkerInputCommands(6));
  TestEqual(TEXT("acknowledged journal drains"),
    Runtime.GetWorkerInputCommandJournal().Num(), 0);

  const FCrowdBehaviorSourceSet* Committed =
    Runtime.FindSourceSet(EntityRef);
  TestNotNull(TEXT("committed set is queryable"), Committed);
  TestEqual(TEXT("source-set revision increments once"),
    Committed ? Committed->Revision : 0u, 2u);

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

#endif
