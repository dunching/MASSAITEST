#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "MassCrowdWorkerAgentState.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"
#include "MassCrowdWorkerMovementControlResource.h"

namespace CrowdWorkerAgentFoundationTests
{
  constexpr uint64 Generation = 71;
  constexpr uint64 InitialStateHash = 0x123456789abcdef0ull;
  const FCrowdStableEntityRef EntityRef{7, 7101, 3};

  FCrowdWorkerPayload MakePayload(const uint32 Value)
  {
    FCrowdWorkerPayload Payload;
    Payload.SchemaId = 0x41474654u;
    Payload.SchemaVersion = 1;
    Payload.Bytes.SetNumUninitialized(sizeof(Value));
    FMemory::Memcpy(Payload.Bytes.GetData(), &Value, sizeof(Value));
    Payload.RecalculateStableHash();
    return Payload;
  }

  FCrowdBehaviorContributionKey MakeKey(
    const int16 Priority,
    const uint32 TypeId,
    const uint32 ControllerId,
    const uint32 Sequence)
  {
    FCrowdBehaviorContributionKey Key;
    Key.Priority = Priority;
    Key.SourceTypeId.Value = TypeId;
    Key.ControllerId.Value = ControllerId;
    Key.SourceSequence = Sequence;
    return Key;
  }

  bool BuildBehaviorState(
    const bool bReverseInsertion,
    FCrowdWorkerBehaviorState& OutState)
  {
    OutState = {};
    OutState.LastFixedStep = 4;
    OutState.LastAbsoluteSimulationTick = 5;
    OutState.BusinessCommitLedgerHash =
      14695981039346656037ull;
    OutState.EvaluationContext.EntityRef = EntityRef;
    OutState.EvaluationContext.FixedStepIndex = 4;
    OutState.EvaluationContext.Position = FVector(10.0, 20.0, 0.0);
    OutState.EvaluationContext.Velocity = FVector(5.0, 0.0, 0.0);
    OutState.EvaluationContext.Facing = FVector::ForwardVector;
    OutState.EvaluationContext.RecalculateStableHash();
    OutState.SourceSet.EntityRef = EntityRef;
    OutState.SourceSet.CapabilityBinding.ProfileKey.Value = 1;
    OutState.SourceSet.Revision = 1;
    OutState.SourceSet.RecalculateStableHash();

    FCrowdBehaviorContributions Contributions;
    FCrowdMovementContribution Move;
    Move.Key = MakeKey(10, 1001, 2001, 1);
    Move.DesiredVelocity = FVector(300.0, 0.0, 0.0);
    Contributions.Movement.Add(Move);

    FCrowdConstraintContribution Lock;
    Lock.Key = MakeKey(20, 1002, 2001, 2);
    Lock.BlendMode = ECrowdBehaviorBlendMode::MinLimit;
    Lock.SpeedLimitCmps = 150.0f;
    Lock.bLockMovement = true;
    FCrowdConstraintContribution NavLimit;
    NavLimit.Key = MakeKey(15, 1003, 2002, 1);
    NavLimit.BlendMode = ECrowdBehaviorBlendMode::MinLimit;
    NavLimit.SpeedLimitCmps = 200.0f;
    NavLimit.AllowedNavLayerMask = 3;
    Contributions.Constraints = {Lock, NavLimit};
    if (bReverseInsertion)
      Algo::Reverse(Contributions.Constraints);
    return FCrowdBehaviorResolver::Resolve(
        Contributions, OutState.ResolvedChannels)
      && OutState.IsValid();
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerBehaviorFoundationDeterminismTest,
  "MassCrowd.RuntimeV2.AgentFoundation.BehaviorDeterminism",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerBehaviorFoundationDeterminismTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerAgentFoundationTests;
  (void)Parameters;
  FCrowdWorkerBehaviorState Forward;
  FCrowdWorkerBehaviorState Reverse;
  TestTrue(TEXT("forward behavior resolves"),
    BuildBehaviorState(false, Forward));
  TestTrue(TEXT("reverse behavior resolves"),
    BuildBehaviorState(true, Reverse));
  TestEqual(TEXT("reverse insertion keeps resolved hash"),
    Reverse.ResolvedChannels.StableHash,
    Forward.ResolvedChannels.StableHash);
  TestTrue(TEXT("constraint locks movement"),
    Forward.ResolvedChannels.bMovementLocked);
  TestEqual(TEXT("resolved movement obeys the constraint"),
    Forward.ResolvedChannels.DesiredVelocity,
    FVector::ZeroVector);

  FCrowdWorkerPayload First;
  FCrowdWorkerPayload Second;
  TestTrue(TEXT("behavior state encodes"),
    FCrowdWorkerBehaviorStateCodec::Encode(Forward, First));
  TestTrue(TEXT("behavior state re-encodes"),
    FCrowdWorkerBehaviorStateCodec::Encode(Forward, Second));
  TestEqual(TEXT("behavior codec is deterministic"),
    First.StableHash, Second.StableHash);
  FCrowdWorkerBehaviorState Decoded;
  TestTrue(TEXT("behavior state decodes"),
    FCrowdWorkerBehaviorStateCodec::Decode(First, Decoded));
  TestEqual(TEXT("behavior state hash round trips"),
    Decoded.ResolvedChannels.StableHash,
    Forward.ResolvedChannels.StableHash);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerLifecycleFoundationTransitionTest,
  "MassCrowd.RuntimeV2.AgentFoundation.LifecycleTransitions",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerLifecycleFoundationTransitionTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerAgentFoundationTests;
  (void)Parameters;
  FCrowdWorkerLifecycleState Current;
  const auto Apply = [this, &Current](
    const uint64 ExpectedRevision,
    const uint64 Revision,
    const uint64 InputSequence,
    const ECrowdWorkerLifecyclePhase Phase)
  {
    FCrowdWorkerLifecycleTransition Transition;
    Transition.EntityRef = EntityRef;
    Transition.ExpectedRevision = ExpectedRevision;
    Transition.Revision = Revision;
    Transition.TargetPhase = Phase;
    FCrowdWorkerLifecycleState Next;
    const bool bApplied =
      FCrowdWorkerLifecycleStateMachine::Apply(
        ExpectedRevision == 0 ? nullptr : &Current,
        Transition, InputSequence, InitialStateHash, Next);
    if (bApplied) Current = Next;
    return bApplied;
  };
  TestTrue(TEXT("spawn pending applies"),
    Apply(0, 1, 10, ECrowdWorkerLifecyclePhase::SpawnPending));
  TestTrue(TEXT("activation applies"),
    Apply(1, 2, 11, ECrowdWorkerLifecyclePhase::Active));
  TestTrue(TEXT("suspend applies"),
    Apply(2, 3, 12, ECrowdWorkerLifecyclePhase::Suspended));
  TestTrue(TEXT("resume applies"),
    Apply(3, 4, 13, ECrowdWorkerLifecyclePhase::Active));

  FCrowdWorkerLifecycleTransition Stale;
  Stale.EntityRef = EntityRef;
  Stale.ExpectedRevision = 2;
  Stale.Revision = 5;
  Stale.TargetPhase = ECrowdWorkerLifecyclePhase::Suspended;
  FCrowdWorkerLifecycleState Rejected;
  TestFalse(TEXT("stale lifecycle revision is rejected"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      &Current, Stale, 14, InitialStateHash, Rejected));
  Stale.EntityRef.LifecycleSerial += 1;
  Stale.ExpectedRevision = Current.Revision;
  TestFalse(TEXT("different lifecycle serial is rejected"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      &Current, Stale, 14, InitialStateHash, Rejected));

  TestTrue(TEXT("remove applies"),
    Apply(4, 5, 15, ECrowdWorkerLifecyclePhase::Removed));
  FCrowdWorkerPayload Encoded;
  TestTrue(TEXT("removed lifecycle encodes"),
    FCrowdWorkerLifecycleStateCodec::Encode(Current, Encoded));
  FCrowdWorkerLifecycleState Decoded;
  TestTrue(TEXT("removed lifecycle decodes"),
    FCrowdWorkerLifecycleStateCodec::Decode(Encoded, Decoded));
  TestTrue(TEXT("removed lifecycle round trips"),
    Decoded == Current);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerConstraintFoundationTest,
  "MassCrowd.RuntimeV2.AgentFoundation.ConstraintIsNotVelocityAuthority",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerConstraintFoundationTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerAgentFoundationTests;
  (void)Parameters;
  FCrowdWorkerBehaviorState Behavior;
  TestTrue(TEXT("locked behavior builds"),
    BuildBehaviorState(false, Behavior));
  FCrowdWorkerMovementControlEntry Profile;
  Profile.EntityRef = EntityRef;
  Profile.AgentId = 1;
  Profile.MaximumSpeedCmps = 300.0f;
  Profile.AutonomousPreferredVelocity =
    FVector(300.0, 0.0, 0.0);
  TestTrue(TEXT("constraint is a resolved semantic"),
    Behavior.ResolvedChannels.bMovementLocked);
  TestEqual(TEXT("constraint does not rewrite preferred velocity"),
    Profile.AutonomousPreferredVelocity,
    FVector(300.0, 0.0, 0.0));
  TestFalse(TEXT("constraint does not create velocity authority"),
    Profile.bUseAuthoritativePreferredVelocity);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerParticipationFoundationTest,
  "MassCrowd.RuntimeV2.AgentFoundation.ParticipationIndependentLifecycle",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerParticipationFoundationTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerAgentFoundationTests;
  (void)Parameters;
  FCrowdWorkerLifecycleState Lifecycle;
  Lifecycle.EntityRef = EntityRef;
  Lifecycle.Revision = 2;
  Lifecycle.SourceInputSequence = 11;
  Lifecycle.InitialStateHash = InitialStateHash;
  Lifecycle.Phase = ECrowdWorkerLifecyclePhase::Active;
  TestTrue(TEXT("active lifecycle is valid"), Lifecycle.IsValid());

  FCrowdWorkerParticipationState Participation;
  Participation.EntityRef = EntityRef;
  Participation.Revision = 3;
  Participation.EnabledMask =
    CrowdWorkerParticipationBit(
      ECrowdWorkerParticipationChannel::Combat)
    | CrowdWorkerParticipationBit(
      ECrowdWorkerParticipationChannel::Presentation);
  FCrowdWorkerPayload Payload;
  TestTrue(TEXT("participation encodes"),
    FCrowdWorkerParticipationStateCodec::Encode(
      Participation, Payload));
  FCrowdWorkerParticipationState Decoded;
  TestTrue(TEXT("participation decodes"),
    FCrowdWorkerParticipationStateCodec::Decode(
      Payload, Decoded));
  TestFalse(TEXT("particle participation is disabled"),
    Decoded.IsEnabled(
      ECrowdWorkerParticipationChannel::Particle));
  TestTrue(TEXT("combat participation remains enabled"),
    Decoded.IsEnabled(
      ECrowdWorkerParticipationChannel::Combat));
  TestTrue(TEXT("presentation participation remains enabled"),
    Decoded.IsEnabled(
      ECrowdWorkerParticipationChannel::Presentation));
  TestEqual(TEXT("participation does not change lifecycle"),
    Lifecycle.Phase, ECrowdWorkerLifecyclePhase::Active);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdWorkerCorrectionFoundationOrderingTest,
  "MassCrowd.RuntimeV2.AgentFoundation.StaleCorrectionRejected",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdWorkerCorrectionFoundationOrderingTest::RunTest(
  const FString& Parameters)
{
  using namespace CrowdWorkerAgentFoundationTests;
  (void)Parameters;
  FCrowdWorkerEntityStateStore States;
  TestTrue(TEXT("state store resets"), States.Reset(4, 1024));
  TestEqual(TEXT("entity spawns"),
    States.Spawn(EntityRef, Generation, 1, MakePayload(1)),
    ECrowdWorkerQueueResult::Added);

  FCrowdWorkerParticipationState Participation;
  Participation.EntityRef = EntityRef;
  Participation.Revision = 2;
  FCrowdWorkerDirtyStateRecord Authority;
  Authority.EntityRef = EntityRef;
  Authority.Field = ECrowdWorkerField::Participation;
  Authority.Generation = Generation;
  Authority.WorkerEpoch = 2;
  Authority.StateRevision = 2;
  Authority.CorrectionRevision = 2;
  Authority.SourceInputSequence = 2;
  TestTrue(TEXT("participation payload encodes"),
    FCrowdWorkerParticipationStateCodec::Encode(
      Participation, Authority.Payload));
  TestTrue(TEXT("authoritative correction applies"),
    States.ApplyAuthoritativeDirty(Authority));

  FCrowdWorkerDirtyStateRecord Stale = Authority;
  Stale.WorkerEpoch = 3;
  Stale.StateRevision = 3;
  Stale.CorrectionRevision = 1;
  Stale.SourceInputSequence = 3;
  TestEqual(TEXT("stale correction revision is rejected"),
    States.ApplyDirty(Stale),
    ECrowdWorkerQueueResult::RejectedStale);
  const FCrowdWorkerDirtyStateRecord* Retained =
    States.Find(EntityRef, ECrowdWorkerField::Participation);
  TestNotNull(TEXT("authoritative participation remains"), Retained);
  if (Retained)
    TestEqual(TEXT("newest correction remains observable"),
      Retained->CorrectionRevision, uint64{2});
  return true;
}

#endif
