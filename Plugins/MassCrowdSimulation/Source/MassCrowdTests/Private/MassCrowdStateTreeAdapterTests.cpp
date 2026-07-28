#include "MassCrowdStateTreeSourceTasks.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdBehaviorSourceCommand BuildAdapterCommand(
    const FCrowdStableEntityRef EntityRef,
    const int64 FixedStep,
    const uint32 CommandSequence,
    const uint32 SourceSequence,
    const ECrowdBehaviorSourceCommandKind Kind,
    const FCrowdBehaviorSourceTypeId TypeId,
    const FCrowdBuiltinBehaviorSourcePayload& Payload,
    const int32 Lifetime = 0)
  {
    FCrowdStateTreeSourceCommandRequest Request;
    Request.EffectiveFixedStep = FixedStep;
    Request.Handle = {EntityRef, {50}, SourceSequence};
    Request.CommandSequence = CommandSequence;
    Request.Kind = Kind;
    Request.SourceTypeId = TypeId;
    Request.LifetimeSteps = Lifetime;
    Request.Payload = Payload;
    FCrowdBehaviorSourceCommand Command;
    FCrowdStateTreeCommandBuilder::Build(Request, Command);
    return Command;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdStateTreeAdapterLogisticsChainTest,
  "MassCrowd.StateTreeAdapter.LogisticsChain",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdStateTreeAdapterLogisticsChainTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("runtime initializes"), Runtime.InitializeBuiltins());
  const FCrowdStableEntityRef EntityRef{1, 500, 1};
  FCrowdCapabilityBinding Binding;
  Binding.ProfileKey =
    CrowdBuiltinBehaviorSchemas::LegacyFullProfile;
  TestTrue(TEXT("carrier registers"),
    Runtime.RegisterEntity(EntityRef, Binding));

  FCrowdBuiltinBehaviorSourcePayload MovePayload;
  MovePayload.Vector = FVector(100.0, 0.0, 0.0);
  MovePayload.PrimaryId = CrowdBehavior::FullQ15Weight;
  FCrowdBuiltinBehaviorSourcePayload CarryPayload;
  CarryPayload.PrimaryId = 1;
  CarryPayload.SecondaryId = 1;
  FCrowdBuiltinBehaviorSourcePayload PickupPayload;
  PickupPayload.TargetRef = {2, 700, 1};
  PickupPayload.CommitId = 1001;
  PickupPayload.PrimaryId = 1;
  PickupPayload.SecondaryId = 1;
  PickupPayload.Quantity = 1;

  const FCrowdBehaviorSourceCommand Initial[] = {
    BuildAdapterCommand(EntityRef, 1, 1, 1,
      ECrowdBehaviorSourceCommandKind::Start,
      CrowdBuiltinSourceTypeIds::MoveToSink, MovePayload),
    BuildAdapterCommand(EntityRef, 1, 2, 2,
      ECrowdBehaviorSourceCommandKind::Start,
      CrowdBuiltinSourceTypeIds::CarryCargo, CarryPayload),
    BuildAdapterCommand(EntityRef, 1, 3, 3,
      ECrowdBehaviorSourceCommandKind::Start,
      CrowdBuiltinSourceTypeIds::PickupInteraction, PickupPayload)};
  for (const FCrowdBehaviorSourceCommand& Command : Initial)
    TestTrue(TEXT("StateTree command queues"),
      Runtime.QueueCommand(Command));
  FCrowdBehaviorPreparedBoundary Pickup;
  TestTrue(TEXT("pickup boundary prepares"),
    Runtime.PrepareBoundary(1, Pickup));
  TestEqual(TEXT("pickup emits business request"),
    Pickup.Entities[0].ResolvedChannels.Business.Num(), 1);
  TestEqual(TEXT("cargo presentation is independent"),
    Pickup.Entities[0].ResolvedChannels.Presentation.Num(), 1);
  TestTrue(TEXT("pickup boundary commits"),
    Runtime.CommitPrepared(Pickup));

  FCrowdBuiltinBehaviorSourcePayload HitPayload;
  HitPayload.Flags = 1;
  const FCrowdBehaviorSourceCommand Hit =
    BuildAdapterCommand(EntityRef, 2, 4, 4,
      ECrowdBehaviorSourceCommandKind::Start,
      CrowdBuiltinSourceTypeIds::HitReaction,
      HitPayload, 2);
  TestTrue(TEXT("hit command queues"), Runtime.QueueCommand(Hit));
  FCrowdBehaviorPreparedBoundary Interrupted;
  TestTrue(TEXT("hit boundary prepares"),
    Runtime.PrepareBoundary(2, Interrupted));
  TestTrue(TEXT("hit temporarily locks movement"),
    Interrupted.Entities[0].ResolvedChannels.bMovementLocked);
  TestEqual(TEXT("cargo survives hit"),
    Interrupted.Entities[0].ResolvedChannels.Presentation.Num(), 1);
  TestTrue(TEXT("hit boundary commits"),
    Runtime.CommitPrepared(Interrupted));

  FCrowdBehaviorPreparedBoundary Resumed;
  TestTrue(TEXT("post-hit boundary prepares"),
    Runtime.PrepareBoundary(4, Resumed));
  TestFalse(TEXT("movement resumes after hit expires"),
    Resumed.Entities[0].ResolvedChannels.bMovementLocked);
  TestEqual(TEXT("task and cargo sources remain"),
    Resumed.Entities[0].StagedSourceSet.Instances.Num(), 3);
  TestTrue(TEXT("post-hit boundary commits"),
    Runtime.CommitPrepared(Resumed));

  FCrowdBuiltinBehaviorSourcePayload DeliverPayload = PickupPayload;
  DeliverPayload.CommitId = 1002;
  const FCrowdBehaviorSourceCommand Delivery[] = {
    BuildAdapterCommand(EntityRef, 5, 5, 3,
      ECrowdBehaviorSourceCommandKind::Stop,
      CrowdBuiltinSourceTypeIds::PickupInteraction, PickupPayload),
    BuildAdapterCommand(EntityRef, 5, 6, 5,
      ECrowdBehaviorSourceCommandKind::Start,
      CrowdBuiltinSourceTypeIds::DeliverInteraction, DeliverPayload)};
  for (const FCrowdBehaviorSourceCommand& Command : Delivery)
    TestTrue(TEXT("delivery transition queues"),
      Runtime.QueueCommand(Command));
  FCrowdBehaviorPreparedBoundary Delivered;
  TestTrue(TEXT("delivery boundary prepares"),
    Runtime.PrepareBoundary(5, Delivered));
  TestEqual(TEXT("delivery emits one business request"),
    Delivered.Entities[0].ResolvedChannels.Business.Num(), 1);
  TestTrue(TEXT("resolved request belongs to delivery source"),
    Delivered.Entities[0].ResolvedChannels.Business[0].Key.SourceTypeId
      == CrowdBuiltinSourceTypeIds::DeliverInteraction);
  TestEqual(TEXT("cargo remains through delivery transition"),
    Delivered.Entities[0].ResolvedChannels.Presentation.Num(), 1);
  return true;
}

#endif
