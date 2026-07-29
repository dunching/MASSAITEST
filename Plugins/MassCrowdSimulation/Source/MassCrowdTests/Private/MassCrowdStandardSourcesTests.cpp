#if WITH_DEV_AUTOMATION_TESTS

#include "MassCrowdStandardSources.h"
#include "MassCrowdReplicationChannel.h"

#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
  struct FEvaluationResult
  {
    FCrowdBehaviorContributions Contributions;
    FCrowdBehaviorSourceState NextState;
    bool bHasNextState = false;
  };

  template <typename PayloadType>
  bool EvaluateStandardSource(
    FAutomationTestBase& Test,
    FCrowdBehaviorSourceRuntime& Runtime,
    const FCrowdBehaviorSourceTypeId TypeId,
    const PayloadType& Payload,
    const TConstArrayView<FCrowdBehaviorContextRecord> Records,
    FEvaluationResult& Out,
    const FVector Position = FVector::ZeroVector,
    const FVector Velocity = FVector::ZeroVector,
    const int64 FixedStep = 10,
    const FCrowdBehaviorSourceState* State = nullptr,
    const int64 StartFixedStep = 1,
    const int64 ExpireFixedStep = 31)
  {
    const FCrowdBehaviorSourceSpec* Spec =
      Runtime.GetEvaluators().FindSpec(TypeId);
    const auto Evaluator =
      Runtime.GetEvaluators().FindEvaluator(TypeId);
    if (!Test.TestNotNull(TEXT("standard spec"), Spec)
      || !Test.TestTrue(TEXT("standard evaluator"), Evaluator.IsValid()))
      return false;
    FCrowdBehaviorSourceInstance Instance;
    Instance.Handle = {{1, 10, 1}, {1}, 1};
    Instance.SourceTypeId = TypeId;
    Instance.SourceVersion = 1;
    Instance.Priority = Spec->DefaultPriority;
    Instance.StartFixedStep = StartFixedStep;
    Instance.LastUpdateFixedStep = StartFixedStep;
    Instance.ExpireFixedStep = ExpireFixedStep;
    Instance.ReplicationPolicy = Spec->ReplicationPolicy;
    if (!Instance.Payload.Set(
        CrowdStandardSources::PayloadSchema(TypeId), Payload))
      return false;
    Instance.State.SchemaId = Spec->StateSchemaId;
    if (State) Instance.State = *State;
    FCrowdBehaviorSourceEvaluationContext Context;
    Context.FixedStepIndex = FixedStep;
    Context.Position = Position;
    Context.Velocity = Velocity;
    Context.Facing = FVector::ForwardVector;
    Context.Instance = Instance;
    Context.ContextRecords = Records;
    FCrowdBehaviorContributionWriter Writer(
      *Spec, Instance, Out.Contributions);
    if (!Evaluator->Evaluate(Context, Writer)
      || !Writer.Succeeded())
      return false;
    Out.bHasNextState = Writer.HasNextState();
    if (Out.bHasNextState) Out.NextState = Writer.GetNextState();
    return true;
  }

  FCrowdBehaviorContextRecord MakeTargetRecord(
    const FCrowdStableEntityRef TargetRef,
    const FVector3f Position,
    const FVector3f Velocity = FVector3f::ZeroVector,
    const uint64 Revision = 7)
  {
    FCrowdTargetKinematicsV1 Target;
    Target.TargetRef = TargetRef;
    Target.Position = Position;
    Target.Velocity = Velocity;
    Target.Facing = FVector3f::ForwardVector;
    Target.NavLayer = 3;
    Target.FactRevision = Revision;
    FCrowdBehaviorContextRecord Record;
    Record.Set(
      CrowdStandardSources::TargetKinematicsContextType,
      CrowdStandardSources::ContextSchemaVersion,
      Target);
    return Record;
  }

  FCrowdBehaviorContextRecord MakeFormationRecord(
    const FCrowdStableEntityRef AnchorRef,
    const FVector3f Position,
    const FVector3f Facing,
    const FVector3f LocalSlotOffset,
    const uint64 Revision = 7)
  {
    FCrowdFormationAnchorV1 Anchor;
    Anchor.AnchorRef = AnchorRef;
    Anchor.Position = Position;
    Anchor.Facing = Facing;
    Anchor.LocalSlotOffset = LocalSlotOffset;
    Anchor.NavLayer = 3;
    Anchor.FactRevision = Revision;
    FCrowdBehaviorContextRecord Record;
    Record.Set(
      CrowdStandardSources::FormationAnchorContextType,
      CrowdStandardSources::ContextSchemaVersion,
      Anchor);
    return Record;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesRegistryTest,
  "MassCrowd.StandardSources.S2.RegistryAndIds",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesRegistryTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("provider registry freezes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdBehaviorSourceTypeId Types[] = {
    CrowdStandardSources::MoveToLocation,
    CrowdStandardSources::ArriveAtLocation,
    CrowdStandardSources::FollowEntity,
    CrowdStandardSources::PursueEntity,
    CrowdStandardSources::FleeFromEntity,
    CrowdStandardSources::MaintainDistance,
    CrowdStandardSources::FaceMovement,
    CrowdStandardSources::FaceEntity,
    CrowdStandardSources::MovementLock,
    CrowdStandardSources::SpeedLimit,
    CrowdStandardSources::WanderSteering,
    CrowdStandardSources::FormationOffset,
    CrowdStandardSources::TimedImpulse};
  TSet<uint32> Seen;
  for (const FCrowdBehaviorSourceTypeId Type : Types)
  {
    TestTrue(TEXT("type id is in reserved range"),
      Type.Value >= 10000 && Type.Value < 20000);
    TestFalse(TEXT("type id unique"), Seen.Contains(Type.Value));
    Seen.Add(Type.Value);
    TestNotNull(TEXT("registered standard spec"),
      Runtime.GetEvaluators().FindSpec(Type));
    TestTrue(TEXT("registered standard evaluator"),
      Runtime.GetEvaluators().FindEvaluator(Type).IsValid());
  }
  TestTrue(TEXT("registry hash includes standard provider"),
    Runtime.GetRegistryHash() != 0);
  TestTrue(TEXT("context schema hash includes v1 PODs"),
    Runtime.GetContextSchemaHash() != 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesLocationTest,
  "MassCrowd.StandardSources.S3.LocationGoal",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesLocationTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  FCrowdMoveToLocationPayload Move;
  Move.TargetLocation = FVector3f(100.0f, 0.0f, 0.0f);
  Move.MaximumSpeedCmps = 50.0f;
  Move.AcceptanceRadiusCm = 5.0f;
  FEvaluationResult MoveResult;
  TestTrue(TEXT("move evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MoveToLocation,
      Move, {}, MoveResult));
  TestEqual(TEXT("move velocity"),
    MoveResult.Contributions.Movement[0].DesiredVelocity,
    FVector(50.0, 0.0, 0.0));
  TestTrue(TEXT("move publishes generic goal"),
    MoveResult.Contributions.Movement[0].Goal.bHasGoal);
  TestEqual(TEXT("move goal"),
    MoveResult.Contributions.Movement[0].Goal.Location,
    FVector(100.0, 0.0, 0.0));

  FCrowdArriveAtLocationPayload Arrive;
  Arrive.TargetLocation = FVector3f(50.0f, 0.0f, 0.0f);
  Arrive.MaximumSpeedCmps = 100.0f;
  Arrive.AcceptanceRadiusCm = 10.0f;
  Arrive.SlowdownRadiusCm = 90.0f;
  FEvaluationResult ArriveResult;
  TestTrue(TEXT("arrive evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::ArriveAtLocation,
      Arrive, {}, ArriveResult));
  TestEqual(TEXT("arrive slows linearly"),
    ArriveResult.Contributions.Movement[0].DesiredVelocity,
    FVector(50.0, 0.0, 0.0));
  FCrowdResolvedBehaviorChannels Resolved;
  TestTrue(TEXT("goal resolves"),
    FCrowdBehaviorResolver::Resolve(
      MoveResult.Contributions, Resolved));
  TestTrue(TEXT("resolved goal present"),
    Resolved.MovementGoal.bHasGoal);
  TestEqual(TEXT("resolved goal stable"),
    Resolved.MovementGoal.Location, FVector(100.0, 0.0, 0.0));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesTargetTest,
  "MassCrowd.StandardSources.S3.TargetAndDistance",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesTargetTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  const FCrowdStableEntityRef TargetRef{1, 20, 1};
  const FCrowdBehaviorContextRecord TargetRecord =
    MakeTargetRecord(
      TargetRef,
      FVector3f(100.0f, 0.0f, 0.0f),
      FVector3f(10.0f, 0.0f, 0.0f));
  const TArray<FCrowdBehaviorContextRecord> Records{TargetRecord};

  FCrowdPursueEntityPayload Pursue;
  Pursue.TargetRef = TargetRef;
  Pursue.MaximumSpeedCmps = 100.0f;
  Pursue.AcceptanceRadiusCm = 5.0f;
  Pursue.MaximumPredictionSeconds = 0.5f;
  FEvaluationResult PursueResult;
  TestTrue(TEXT("pursue evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::PursueEntity,
      Pursue, Records, PursueResult));
  TestEqual(TEXT("pursue bounded prediction"),
    PursueResult.Contributions.Movement[0].Goal.Location,
    FVector(105.0, 0.0, 0.0));

  FCrowdMaintainDistancePayload Distance;
  Distance.TargetRef = TargetRef;
  Distance.MinimumDistanceCm = 30.0f;
  Distance.MaximumDistanceCm = 60.0f;
  Distance.HysteresisCm = 5.0f;
  Distance.MaximumCorrectionSpeedCmps = 25.0f;
  FEvaluationResult DistanceResult;
  TestTrue(TEXT("distance evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MaintainDistance,
      Distance, Records, DistanceResult));
  TestTrue(TEXT("distance writes state"),
    DistanceResult.bHasNextState);
  TestEqual(TEXT("distance approaches"),
    DistanceResult.Contributions.Movement[0].DesiredVelocity,
    FVector(25.0, 0.0, 0.0));

  const TArray<FCrowdBehaviorContextRecord> HoldRecords{
    MakeTargetRecord(
      TargetRef, FVector3f(60.0f, 0.0f, 0.0f),
      FVector3f::ZeroVector, 8)};
  FEvaluationResult HoldResult;
  TestTrue(TEXT("approach enters hold at maximum boundary"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MaintainDistance,
      Distance, HoldRecords, HoldResult,
      FVector::ZeroVector, FVector::ZeroVector, 11,
      &DistanceResult.NextState));
  TestTrue(TEXT("hold produces no additive correction"),
    HoldResult.Contributions.Movement[0]
      .DesiredVelocity.IsNearlyZero());
  const TArray<FCrowdBehaviorContextRecord> HysteresisRecords{
    MakeTargetRecord(
      TargetRef, FVector3f(65.0f, 0.0f, 0.0f),
      FVector3f::ZeroVector, 9)};
  FEvaluationResult HysteresisResult;
  TestTrue(TEXT("hysteresis boundary evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MaintainDistance,
      Distance, HysteresisRecords, HysteresisResult,
      FVector::ZeroVector, FVector::ZeroVector, 12,
      &HoldResult.NextState));
  TestTrue(TEXT("hysteresis prevents boundary chatter"),
    HysteresisResult.Contributions.Movement[0]
      .DesiredVelocity.IsNearlyZero());
  const TArray<FCrowdBehaviorContextRecord> OutsideRecords{
    MakeTargetRecord(
      TargetRef, FVector3f(66.0f, 0.0f, 0.0f),
      FVector3f::ZeroVector, 10)};
  FEvaluationResult OutsideResult;
  TestTrue(TEXT("distance outside hysteresis evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MaintainDistance,
      Distance, OutsideRecords, OutsideResult,
      FVector::ZeroVector, FVector::ZeroVector, 13,
      &HysteresisResult.NextState));
  TestEqual(TEXT("distance resumes approach outside hysteresis"),
    OutsideResult.Contributions.Movement[0].DesiredVelocity,
    FVector(25.0, 0.0, 0.0));
  TestFalse(TEXT("missing target context rejects"),
    [&]()
    {
      FEvaluationResult Missing;
      return EvaluateStandardSource(
        *this, Runtime, CrowdStandardSources::PursueEntity,
        Pursue, {}, Missing);
    }());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesStateTest,
  "MassCrowd.StandardSources.S5.StateImpulseFormation",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesStateTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  FCrowdWanderSteeringPayload Wander;
  Wander.SpeedCmps = 80.0f;
  Wander.ReselectIntervalSteps = 5;
  FEvaluationResult First;
  TestTrue(TEXT("wander initializes"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::WanderSteering,
      Wander, {}, First));
  FEvaluationResult Replay;
  TestTrue(TEXT("wander replays"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::WanderSteering,
      Wander, {}, Replay));
  TestEqual(TEXT("wander deterministic"),
    First.Contributions.Movement[0].DesiredVelocity,
    Replay.Contributions.Movement[0].DesiredVelocity);
  TestTrue(TEXT("wander persists state"), First.bHasNextState);
  FEvaluationResult Continued;
  TestTrue(TEXT("wander consumes persistent PRNG state"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::WanderSteering,
      Wander, {}, Continued,
      FVector::ZeroVector, FVector::ZeroVector, 11,
      &First.NextState));
  TestEqual(TEXT("wander direction remains stable before reselection"),
    Continued.Contributions.Movement[0].DesiredVelocity,
    First.Contributions.Movement[0].DesiredVelocity);

  const FCrowdStableEntityRef AnchorRef{1, 21, 1};
  const TArray<FCrowdBehaviorContextRecord> FormationRecords{
    MakeFormationRecord(
      AnchorRef,
      FVector3f(100.0f, 0.0f, 0.0f),
      FVector3f(0.0f, 1.0f, 0.0f),
      FVector3f(10.0f, 20.0f, 0.0f))};
  FCrowdFormationOffsetPayload Formation;
  Formation.PositionGain = 1.0f;
  Formation.MaximumCorrectionSpeedCmps = 200.0f;
  FEvaluationResult FormationResult;
  TestTrue(TEXT("formation evaluates anchor-local offset"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::FormationOffset,
      Formation, FormationRecords, FormationResult));
  TestEqual(TEXT("formation rotates local slot by anchor facing"),
    FormationResult.Contributions.Movement[0].DesiredVelocity,
    FVector(80.0, 10.0, 0.0));
  TestEqual(TEXT("formation is an additive correction"),
    FormationResult.Contributions.Movement[0].BlendMode,
    ECrowdBehaviorBlendMode::Additive);

  FCrowdTimedImpulsePayload Impulse;
  Impulse.InitialVelocity = FVector3f(100.0f, 0.0f, 0.0f);
  Impulse.DecayMode = ECrowdImpulseDecayMode::Linear;
  FEvaluationResult ImpulseResult;
  TestTrue(TEXT("impulse evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::TimedImpulse,
      Impulse, {}, ImpulseResult,
      FVector::ZeroVector, FVector::ZeroVector, 6,
      nullptr, 1, 11));
  TestEqual(TEXT("impulse linear half life"),
    ImpulseResult.Contributions.Movement[0].DesiredVelocity,
    FVector(50.0, 0.0, 0.0));
  FEvaluationResult ExpiredImpulse;
  TestTrue(TEXT("impulse endpoint evaluates deterministically"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::TimedImpulse,
      Impulse, {}, ExpiredImpulse,
      FVector::ZeroVector, FVector::ZeroVector, 11,
      nullptr, 1, 11));
  TestTrue(TEXT("impulse endpoint is zero before lifecycle removal"),
    ExpiredImpulse.Contributions.Movement[0]
      .DesiredVelocity.IsNearlyZero());
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesVectorAndConstraintTest,
  "MassCrowd.StandardSources.S3.VectorFacingConstraint",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesVectorAndConstraintTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  const FCrowdStableEntityRef TargetRef{1, 30, 1};
  const TArray<FCrowdBehaviorContextRecord> Records{
    MakeTargetRecord(
      TargetRef, FVector3f(100.0f, 0.0f, 0.0f),
      FVector3f(10.0f, 0.0f, 0.0f), 9)};

  FCrowdFollowEntityPayload Follow;
  Follow.TargetRef = TargetRef;
  Follow.LocalOffset = FVector3f(-20.0f, 0.0f, 0.0f);
  Follow.MaximumSpeedCmps = 200.0f;
  Follow.AcceptanceRadiusCm = 1.0f;
  Follow.PositionGain = 1.0f;
  FEvaluationResult FollowResult;
  TestTrue(TEXT("follow evaluates target velocity and local offset"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::FollowEntity,
      Follow, Records, FollowResult));
  TestEqual(TEXT("follow goal preserves target ref"),
    FollowResult.Contributions.Movement[0].Goal.TargetRef,
    TargetRef);
  TestEqual(TEXT("follow target-relative velocity"),
    FollowResult.Contributions.Movement[0].DesiredVelocity,
    FVector(90.0, 0.0, 0.0));

  FCrowdFleeFromEntityPayload Flee;
  Flee.TargetRef = TargetRef;
  Flee.MaximumSpeedCmps = 70.0f;
  Flee.SafeDistanceCm = 200.0f;
  FEvaluationResult FleeResult;
  TestTrue(TEXT("flee evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::FleeFromEntity,
      Flee, Records, FleeResult));
  TestEqual(TEXT("flee points away"),
    FleeResult.Contributions.Movement[0].DesiredVelocity,
    FVector(-70.0, 0.0, 0.0));

  FCrowdFaceEntityPayload Face;
  Face.TargetRef = TargetRef;
  FEvaluationResult FaceResult;
  TestTrue(TEXT("face entity evaluates independently"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::FaceEntity,
      Face, Records, FaceResult));
  TestEqual(TEXT("face entity points at target"),
    FaceResult.Contributions.Facing[0].DesiredDirection,
    FVector::ForwardVector);

  FCrowdMovementLockPayload Lock;
  FEvaluationResult LockResult;
  TestTrue(TEXT("movement lock evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MovementLock,
      Lock, {}, LockResult));
  TestTrue(TEXT("movement lock is explicit constraint"),
    LockResult.Contributions.Constraints[0].bLockMovement);

  FCrowdSpeedLimitPayload Limit;
  Limit.MaximumSpeedCmps = 123.0f;
  Limit.AllowedNavLayerMask = 4;
  FEvaluationResult LimitResult;
  TestTrue(TEXT("speed limit evaluates"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::SpeedLimit,
      Limit, {}, LimitResult));
  TestEqual(TEXT("speed limit remains min-limit"),
    LimitResult.Contributions.Constraints[0].BlendMode,
    ECrowdBehaviorBlendMode::MinLimit);
  TestEqual(TEXT("speed limit preserves layer mask"),
    LimitResult.Contributions.Constraints[0].AllowedNavLayerMask,
    uint64{4});
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesContextValidationTest,
  "MassCrowd.StandardSources.S6.ContextValidation",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesContextValidationTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  const FCrowdStableEntityRef TargetRef{1, 31, 1};
  FCrowdPursueEntityPayload Pursue;
  Pursue.TargetRef = TargetRef;
  Pursue.MaximumSpeedCmps = 100.0f;
  Pursue.AcceptanceRadiusCm = 5.0f;
  Pursue.MaximumPredictionSeconds = 0.5f;
  const auto Rejects = [&](const FCrowdBehaviorContextRecord& Record)
  {
    FEvaluationResult Result;
    const TArray<FCrowdBehaviorContextRecord> Records{Record};
    return !EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::PursueEntity,
      Pursue, Records, Result);
  };
  TestTrue(TEXT("wrong target ref rejects"),
    Rejects(MakeTargetRecord(
      {1, 32, 1}, FVector3f(100.0f, 0.0f, 0.0f))));
  TestTrue(TEXT("zero fact revision rejects"),
    Rejects(MakeTargetRecord(
      TargetRef, FVector3f(100.0f, 0.0f, 0.0f),
      FVector3f::ZeroVector, 0)));
  FCrowdTargetKinematicsV1 Target;
  Target.TargetRef = TargetRef;
  Target.Position = FVector3f(100.0f, 0.0f, 0.0f);
  Target.Facing = FVector3f::ForwardVector;
  Target.FactRevision = 1;
  FCrowdBehaviorContextRecord WrongVersion;
  WrongVersion.Set(
    CrowdStandardSources::TargetKinematicsContextType, 2, Target);
  TestTrue(TEXT("wrong context version rejects"),
    Rejects(WrongVersion));
  FCrowdMoveToLocationPayload InvalidMove;
  InvalidMove.MaximumSpeedCmps =
    std::numeric_limits<float>::quiet_NaN();
  FEvaluationResult InvalidResult;
  TestFalse(TEXT("non-finite payload rejects"),
    EvaluateStandardSource(
      *this, Runtime, CrowdStandardSources::MoveToLocation,
      InvalidMove, {}, InvalidResult));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdStandardSourcesNetworkCodecTest,
  "MassCrowd.StandardSources.S6.NetworkCodecV3",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdStandardSourcesNetworkCodecTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  FCrowdBehaviorSourceInstance Instance;
  Instance.Handle = {{1, 40, 1}, {1}, 1};
  Instance.SourceTypeId = CrowdStandardSources::WanderSteering;
  Instance.SourceVersion = 1;
  Instance.Priority = 100;
  Instance.StartFixedStep = 1;
  Instance.LastUpdateFixedStep = 2;
  Instance.ExpireFixedStep = INDEX_NONE;
  Instance.ReplicationPolicy =
    ECrowdBehaviorSourceReplicationPolicy::Predictable;
  FCrowdWanderSteeringPayload Payload;
  Payload.SpeedCmps = 80.0f;
  Payload.ReselectIntervalSteps = 5;
  FCrowdWanderSteeringState State;
  State.RandomState = 0x12345678;
  State.NextReselectFixedStep = 7;
  State.DirectionIndex = 3;
  TestTrue(TEXT("standard payload serializes"),
    Instance.Payload.Set(
      CrowdStandardSources::PayloadSchema(
        CrowdStandardSources::WanderSteering),
      Payload));
  TestTrue(TEXT("standard state serializes"),
    Instance.State.Set(
      CrowdStandardSources::WanderStateSchema, State));
  FCrowdBehaviorSourceSetReplicationRecord Record;
  Record.RegistryHash = Runtime.GetRegistryHash();
  Record.ContextSchemaHash = Runtime.GetContextSchemaHash();
  Record.ResolvedBehaviorHash = 0x1234;
  Record.DerivedDiagnosticLabel = 9;
  Record.SourceSet.EntityRef = Instance.Handle.EntityRef;
  Record.SourceSet.CapabilityBinding.ProfileKey = {1};
  Record.SourceSet.Revision = 2;
  Record.SourceSet.Instances.Add(Instance);
  Record.SourceSet.ControllerCursors.Add({{1}, 2, 0x5678});
  Record.SourceSet.RecalculateStableHash();
  TArray<uint8> Bytes;
  TestTrue(TEXT("v3 encodes standard persistent state"),
    FCrowdReplicationCodec::EncodeBehaviorSourceSet(
      Record, Bytes));
  FCrowdBehaviorSourceSetReplicationRecord Decoded;
  TestTrue(TEXT("v3 decodes standard persistent state"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      Bytes, Record.RegistryHash,
      Record.ContextSchemaHash, Decoded));
  TestEqual(TEXT("standard source set round trips"),
    Decoded.SourceSet.StableHash,
    Record.SourceSet.StableHash);
  TestFalse(TEXT("registry mismatch requests rejection"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      Bytes, Record.RegistryHash + 1,
      Record.ContextSchemaHash, Decoded));
  TArray<uint8> OldVersion = Bytes;
  OldVersion[0] = 2;
  OldVersion[1] = 0;
  TestFalse(TEXT("v2 standard source set rejects"),
    FCrowdReplicationCodec::DecodeBehaviorSourceSet(
      OldVersion, Record.RegistryHash,
      Record.ContextSchemaHash, Decoded));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdPresentationAdditiveTest,
  "MassCrowd.StandardSources.S2.PresentationAdditive",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdPresentationAdditiveTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorContributions Contributions;
  auto Add = [&](const int16 Priority,
    const ECrowdBehaviorBlendMode Mode, const uint32 Value)
  {
    FCrowdPresentationContribution& Entry =
      Contributions.Presentation.AddDefaulted_GetRef();
    Entry.Key = {Priority, {50000u + Value}, {1}, Value + 1};
    Entry.BlendMode = Mode;
    Entry.PropertyId = 7;
    Entry.Value = Value;
  };
  Add(20, ECrowdBehaviorBlendMode::Override, 1);
  Add(10, ECrowdBehaviorBlendMode::Override, 2);
  Add(5, ECrowdBehaviorBlendMode::Additive, 3);
  Add(4, ECrowdBehaviorBlendMode::Additive, 4);
  FCrowdResolvedBehaviorChannels Resolved;
  TestTrue(TEXT("presentation resolves"),
    FCrowdBehaviorResolver::Resolve(Contributions, Resolved));
  TestEqual(TEXT("one override plus all additive"),
    Resolved.Presentation.Num(), 3);
  TestEqual(TEXT("highest override retained"),
    Resolved.Presentation[0].Value, 1u);
  return true;
}

#endif
