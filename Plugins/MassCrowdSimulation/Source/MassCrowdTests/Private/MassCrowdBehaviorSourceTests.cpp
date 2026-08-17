#include "MassCrowdBehaviorSource.h"
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  constexpr FCrowdCapabilityId MoveCapability{100};
  constexpr FCrowdCapabilityId FaceCapability{200};
  constexpr FCrowdCapabilityProfileKey ProfileKey{10};
  constexpr FCrowdBehaviorSourceTypeId MoveSourceType{1000};
  constexpr FCrowdBehaviorSourceTypeId HitSourceType{2000};
  constexpr uint32 PayloadSchema = 77;

  struct FTestPayload
  {
    FVector Vector = FVector::ZeroVector;
    uint32 Value = 0;
  };

  FCrowdCapabilityProfileRegistry MakeProfiles()
  {
    FCrowdCapabilityProfileRegistry Profiles;
    FCrowdCapabilityProfile Profile;
    Profile.Key = ProfileKey;
    Profile.CapabilityIds = {MoveCapability};
    Profiles.Register(MoveTemp(Profile));
    Profiles.Freeze();
    return Profiles;
  }

  FCrowdBehaviorSourceSpecRegistry MakeSpecs()
  {
    FCrowdBehaviorSourceSpecRegistry Specs;
    FCrowdBehaviorSourceSpec Move;
    Move.TypeId = MoveSourceType;
    Move.ChannelMask =
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Movement);
    Move.DefaultPriority = 10;
    Move.PayloadSchemaId = PayloadSchema;
    Move.ReplicationPolicy =
      ECrowdBehaviorSourceReplicationPolicy::Predictable;
    Move.RequiredCapabilityCount = 1;
    Move.RequiredCapabilities[0] = MoveCapability;
    Specs.Register(Move);

    FCrowdBehaviorSourceSpec Hit;
    Hit.TypeId = HitSourceType;
    Hit.ChannelMask =
      CrowdBehaviorChannelBit(ECrowdBehaviorChannel::Constraint);
    Hit.DefaultPriority = 100;
    Hit.MaxLifetimeSteps = 3;
    Hit.PayloadSchemaId = PayloadSchema;
    Hit.ReplicationPolicy =
      ECrowdBehaviorSourceReplicationPolicy::ResolvedOnly;
    Specs.Register(Hit);
    Specs.Freeze();
    return Specs;
  }

  FCrowdBehaviorSourceSet MakeEmptySet()
  {
    FCrowdBehaviorSourceSet Set;
    Set.EntityRef = {1, 10, 1};
    Set.CapabilityBinding.ProfileKey = ProfileKey;
    Set.Revision = 1;
    Set.RecalculateStableHash();
    return Set;
  }

  FCrowdBehaviorSourceCommand MakeCommand(
    const ECrowdBehaviorSourceCommandKind Kind,
    const FCrowdBehaviorSourceTypeId TypeId,
    const uint32 CommandSequence,
    const uint32 SourceSequence,
    const int64 FixedStep,
    const int32 Lifetime = 0)
  {
    FCrowdBehaviorSourceCommand Command;
    Command.EffectiveFixedStep = FixedStep;
    Command.Handle = {{1, 10, 1}, {1}, SourceSequence};
    Command.CommandSequence = CommandSequence;
    Command.Kind = Kind;
    Command.SourceTypeId = TypeId;
    Command.LifetimeSteps = Lifetime;
    const FTestPayload Payload{FVector(100.0, 0.0, 0.0), 9};
    Command.Payload.Set(PayloadSchema, Payload);
    return Command;
  }

  FCrowdBehaviorContributionKey MakeKey(
    const int16 Priority,
    const uint32 Type,
    const uint32 SourceSequence)
  {
    return {Priority, {Type}, {1}, SourceSequence};
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorCapabilityProfileTest,
  "MassCrowd.BehaviorSource.CapabilityProfiles",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorCapabilityProfileTest::RunTest(
  const FString& Parameters)
{
  FCrowdCapabilityProfileRegistry Profiles = MakeProfiles();
  FCrowdCapabilityBinding Binding;
  Binding.ProfileKey = ProfileKey;
  Binding.ModifierRevision = 2;
  Binding.ModifierCount = 1;
  Binding.Modifiers[0] = {
    FaceCapability, ECrowdCapabilityModifierOperation::Add};
  FCrowdResolvedCapabilitySet Resolved;
  TestTrue(TEXT("profile and modifier resolve"),
    Profiles.Resolve(Binding, Resolved));
  TestTrue(TEXT("profile capability retained"),
    Resolved.Has(MoveCapability));
  TestTrue(TEXT("modifier capability added"),
    Resolved.Has(FaceCapability));
  TestTrue(TEXT("resolved set has stable hash"),
    Resolved.CalculateStableHash() != 0);

  Binding.Modifiers[0].Operation =
    ECrowdCapabilityModifierOperation::Remove;
  TestTrue(TEXT("remove modifier resolves"),
    Profiles.Resolve(Binding, Resolved));
  TestFalse(TEXT("missing profile capability is absent"),
    Resolved.Has(FaceCapability));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorSourceStateMachineTest,
  "MassCrowd.BehaviorSource.StateMachine",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorSourceStateMachineTest::RunTest(
  const FString& Parameters)
{
  FCrowdCapabilityProfileRegistry Profiles = MakeProfiles();
  FCrowdResolvedCapabilitySet Capabilities;
  TestTrue(TEXT("capabilities resolve"),
    Profiles.Resolve(MakeEmptySet().CapabilityBinding, Capabilities));
  const FCrowdBehaviorSourceSpecRegistry Specs = MakeSpecs();
  const FCrowdBehaviorSourceSet Empty = MakeEmptySet();
  const FCrowdBehaviorSourceCommand Start = MakeCommand(
    ECrowdBehaviorSourceCommandKind::Start,
    MoveSourceType, 1, 1, 10);

  FCrowdBehaviorSourceSet Started;
  TArray<FCrowdBehaviorSourceEvent> Events;
  uint64 BatchHash = 0;
  TestTrue(TEXT("start stages successfully"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Empty, MakeArrayView(&Start, 1), 10, Specs, Capabilities,
      Started, Events, BatchHash));
  TestEqual(TEXT("one source started"), Started.Instances.Num(), 1);
  TestEqual(TEXT("revision increments once"), Started.Revision, 2u);
  TestEqual(TEXT("start event emitted"), Events.Num(), 1);
  TestTrue(TEXT("batch has stable hash"), BatchHash != 0);

  FCrowdBehaviorSourceSet Duplicate;
  TArray<FCrowdBehaviorSourceEvent> DuplicateEvents;
  uint64 DuplicateHash = 0;
  TestTrue(TEXT("exact duplicate is idempotent"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Started, MakeArrayView(&Start, 1), 10, Specs, Capabilities,
      Duplicate, DuplicateEvents, DuplicateHash));
  TestEqual(TEXT("duplicate does not increment revision"),
    Duplicate.Revision, Started.Revision);
  TestTrue(TEXT("duplicate emits no lifecycle event"),
    DuplicateEvents.IsEmpty());

  const FCrowdBehaviorSourceCommand Gap = MakeCommand(
    ECrowdBehaviorSourceCommandKind::Update,
    MoveSourceType, 3, 1, 11);
  FCrowdBehaviorSourceSet Rejected;
  TArray<FCrowdBehaviorSourceEvent> RejectedEvents;
  uint64 RejectedHash = 0;
  TestFalse(TEXT("command sequence gap rejects whole batch"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Started, MakeArrayView(&Gap, 1), 11, Specs, Capabilities,
      Rejected, RejectedEvents, RejectedHash));
  TestTrue(TEXT("rejected output remains empty"),
    Rejected.Instances.IsEmpty());

  const FCrowdBehaviorSourceCommand Hit = MakeCommand(
    ECrowdBehaviorSourceCommandKind::Start,
    HitSourceType, 2, 2, 11, 2);
  FCrowdBehaviorSourceSet WithHit;
  TestTrue(TEXT("temporary hit source starts"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Started, MakeArrayView(&Hit, 1), 11, Specs, Capabilities,
      WithHit, Events, BatchHash));
  TestEqual(TEXT("move and hit coexist"), WithHit.Instances.Num(), 2);

  FCrowdBehaviorSourceSet Expired;
  TestTrue(TEXT("expiry sweep succeeds"),
    FCrowdBehaviorSourceStateMachine::Apply(
      WithHit, {}, 13, Specs, Capabilities,
      Expired, Events, BatchHash));
  TestEqual(TEXT("hit expires without deleting move"),
    Expired.Instances.Num(), 1);
  TestTrue(TEXT("original move source survives"),
    Expired.Instances[0].SourceTypeId == MoveSourceType);

  FCrowdBehaviorSourceCommand ConflictingDuplicate = Start;
  const FTestPayload DifferentPayload{
    FVector(200.0, 0.0, 0.0), 10};
  ConflictingDuplicate.Payload.Set(
    PayloadSchema, DifferentPayload);
  TestFalse(TEXT("same key with different content is rejected"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Started, MakeArrayView(&ConflictingDuplicate, 1), 11,
      Specs, Capabilities, Rejected, RejectedEvents, RejectedHash));

  TArray<FCrowdBehaviorSourceCommand> OverflowCommands;
  for (uint32 Index = 0;
    Index <= CrowdBehavior::MaxSourcesPerEntity; ++Index)
  {
    OverflowCommands.Add(MakeCommand(
      ECrowdBehaviorSourceCommandKind::Start,
      MoveSourceType, Index + 1, Index + 1, 20));
  }
  TestFalse(TEXT("seventeenth active source rejects whole batch"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Empty, OverflowCommands, 20, Specs, Capabilities,
      Rejected, RejectedEvents, RejectedHash));
  TestTrue(TEXT("overflow exposes no staged source"),
    Rejected.Instances.IsEmpty());

  FCrowdResolvedCapabilitySet NoCapabilities;
  FCrowdBehaviorSourceSet Revoked;
  TestTrue(TEXT("capability revocation stages"),
    FCrowdBehaviorSourceStateMachine::Apply(
      Expired, {}, 14, Specs, NoCapabilities,
      Revoked, Events, BatchHash));
  TestTrue(TEXT("dependent move source removed"),
    Revoked.Instances.IsEmpty());
  TestTrue(TEXT("capability revocation event emitted"),
    Events.ContainsByPredicate([](const auto& Event)
    {
      return Event.Kind
        == ECrowdBehaviorSourceEventKind::CapabilityRevoked;
    }));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdBehaviorResolverCompositionTest,
  "MassCrowd.BehaviorSource.ResolverComposition",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdBehaviorResolverCompositionTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorContributions Contributions;
  Contributions.Movement.Add({
    MakeKey(10, 1000, 1),
    ECrowdBehaviorBlendMode::WeightedAdd,
    CrowdBehavior::FullQ15Weight,
    FVector(100.0, 0.0, 0.0)});
  Contributions.Movement.Add({
    MakeKey(10, 1001, 2),
    ECrowdBehaviorBlendMode::WeightedAdd,
    CrowdBehavior::FullQ15Weight,
    FVector(0.0, 100.0, 0.0)});
  Contributions.Facing.Add({
    MakeKey(20, 1100, 3),
    ECrowdBehaviorBlendMode::Override,
    CrowdBehavior::FullQ15Weight,
    FVector(0.0, 1.0, 0.0)});
  Contributions.Constraints.Add({
    MakeKey(100, 2000, 4),
    ECrowdBehaviorBlendMode::Override,
    0.0f,
    MAX_uint64,
    true});
  Contributions.Presentation.Add({
    MakeKey(10, 3000, 5),
    ECrowdBehaviorBlendMode::Override,
    1,
    42});

  FCrowdResolvedBehaviorChannels Locked;
  TestTrue(TEXT("composed channels resolve"),
    FCrowdBehaviorResolver::Resolve(Contributions, Locked));
  TestTrue(TEXT("hit reaction locks movement"),
    Locked.bMovementLocked
      && Locked.DesiredVelocity.IsNearlyZero());
  TestTrue(TEXT("facing remains independently resolved"),
    Locked.DesiredFacing.Equals(FVector(0.0, 1.0, 0.0)));
  TestEqual(TEXT("presentation survives movement lock"),
    Locked.Presentation.Num(), 1);

  Contributions.Constraints.Reset();
  Algo::Reverse(Contributions.Movement);
  FCrowdResolvedBehaviorChannels Resumed;
  TestTrue(TEXT("movement resumes after constraint removal"),
    FCrowdBehaviorResolver::Resolve(Contributions, Resumed));
  TestTrue(TEXT("weighted movement is deterministic"),
    Resumed.DesiredVelocity.Equals(FVector(50.0, 50.0, 0.0)));

  Algo::Reverse(Contributions.Movement);
  FCrowdResolvedBehaviorChannels Reordered;
  TestTrue(TEXT("reordered input resolves"),
    FCrowdBehaviorResolver::Resolve(Contributions, Reordered));
  TestEqual(TEXT("reordered input keeps stable hash"),
    Reordered.StableHash, Resumed.StableHash);
  return true;
}

#endif
