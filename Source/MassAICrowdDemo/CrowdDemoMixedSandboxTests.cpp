#include "CrowdNavSurfaceGraph.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "CrowdDemoBusinessAdapters.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdCapabilityBinding MakeMixedBinding()
  {
    FCrowdCapabilityBinding Binding;
    Binding.ProfileKey = CrowdDemoBehaviorSchemas::FullProfile;
    return Binding;
  }

  template <typename PayloadType>
  FCrowdDemoDesiredSource MakeDesiredSource(
    const FCrowdBehaviorControllerId ControllerId,
    const uint32 SourceSequence,
    const FCrowdBehaviorSourceTypeId SourceTypeId,
    const PayloadType& Payload,
    const int32 LifetimeSteps = 0)
  {
    FCrowdDemoDesiredSource Desired;
    Desired.ControllerId = ControllerId;
    Desired.SourceSequence = SourceSequence;
    Desired.SourceTypeId = SourceTypeId;
    Desired.LifetimeSteps = LifetimeSteps;
    Desired.Payload.Set(
      CrowdStandardSources::PayloadSchema(SourceTypeId), Payload);
    return Desired;
  }

  FCrowdDemoDesiredSource MakeDesiredSource(
    const FCrowdBehaviorControllerId ControllerId,
    const uint32 SourceSequence,
    const FCrowdBehaviorSourceTypeId SourceTypeId,
    const FCrowdDemoBehaviorSourcePayload& Payload,
    const int32 LifetimeSteps = 0)
  {
    FCrowdDemoDesiredSource Desired;
    Desired.ControllerId = ControllerId;
    Desired.SourceSequence = SourceSequence;
    Desired.SourceTypeId = SourceTypeId;
    Desired.LifetimeSteps = LifetimeSteps;
    Desired.Payload.Set(CrowdDemoBehaviorSchemas::Standard, Payload);
    return Desired;
  }

  bool QueueDesiredSources(
    FCrowdBehaviorSourceRuntime& Runtime,
    const FCrowdStableEntityRef EntityRef,
    const int64 FixedStep,
    const TConstArrayView<FCrowdDemoDesiredSource> Desired,
    TConstArrayView<FCrowdBehaviorContextRecord> Records = {})
  {
    TArray<FCrowdBehaviorSourceCommand> Commands;
    const FCrowdBehaviorSourceSet* Set = Runtime.FindSourceSet(EntityRef);
    if (!Set || !FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      FixedStep, *Set, Desired, Commands))
      return false;
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
    {
      if (!Runtime.QueueCommand(Command)) return false;
    }
    FCrowdBehaviorEntityEvaluationContext EvaluationContext;
    EvaluationContext.EntityRef = EntityRef;
    EvaluationContext.FixedStepIndex = FixedStep;
    EvaluationContext.Position = FVector::ZeroVector;
    EvaluationContext.Facing = FVector::ForwardVector;
    EvaluationContext.Records = Records;
    EvaluationContext.RecalculateStableHash();
    return Runtime.SetEvaluationContext(EvaluationContext);
  }

  FCrowdBehaviorContextRecord MakeTargetRecord(
    const FCrowdStableEntityRef TargetRef,
    const FVector3f Position)
  {
    FCrowdTargetKinematicsV1 Target;
    Target.TargetRef = TargetRef;
    Target.Position = Position;
    Target.Facing = FVector3f::ForwardVector;
    Target.NavLayer = 1;
    Target.FactRevision = 1;
    FCrowdBehaviorContextRecord Record;
    Record.Set(
      CrowdStandardSources::TargetKinematicsContextType,
      CrowdStandardSources::ContextSchemaVersion,
      Target);
    return Record;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoMixedSandboxCompositionTest,
  "CrowdDemo.MixedSandbox.J.Composition",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoMixedSandboxCompositionTest::RunTest(const FString& Parameters)
{
  TArray<FCrowdNavSurfacePolygonInput> Polygons;
  Polygons.Add({1, 0, FVector(0, 0, 0), FVector::UpVector, 100,
    {FVector(-50,-50,0), FVector(50,-50,0), FVector(50,50,0), FVector(-50,50,0)},
    {{2, FVector(50,-40,0), FVector(50,40,0)}}});
  Polygons.Add({2, 1, FVector(100, 0, 100), FVector(-1,0,1).GetSafeNormal(), 100,
    {FVector(50,-50,0), FVector(150,-50,100), FVector(150,50,100), FVector(50,50,0)},
    {{1, FVector(50,40,0), FVector(50,-40,0)}, {3, FVector(150,-40,100), FVector(150,40,100)}}});
  Polygons.Add({3, 1, FVector(200, 0, 100), FVector::UpVector, 100,
    {FVector(150,-50,100), FVector(250,-50,100), FVector(250,50,100), FVector(150,50,100)},
    {{2, FVector(150,40,100), FVector(150,-40,100)}}});
  FCrowdNavSurfaceGraph Graph;
  FCrowdNavSurfaceGraphBuildConfig GraphConfig;
  GraphConfig.MinPortalWidthCm = 60;
  TestTrue(TEXT("mixed sandbox graph builds"),
    FCrowdNavSurfaceGraphKernel::Build(Polygons, GraphConfig, Graph));
  FCrowdNavSurfaceFlow Flow;
  TestTrue(TEXT("mixed sandbox shared flow crosses ramp"),
    FCrowdNavSurfaceGraphKernel::BuildFlow(
      Graph, Graph.Nodes.Last().StableNodeId, 1, Flow));
  TestTrue(TEXT("lower node reaches elevated goal"),
    Flow.Nodes[0].IntegrationCostQ < MAX_uint32);

  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("source runtime initializes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef Hauler{1, 1, 1};
  const FCrowdStableEntityRef Attacker{1, 7, 1};
  TestTrue(TEXT("hauler registers"),
    Runtime.RegisterEntity(Hauler, MakeMixedBinding()));
  TestTrue(TEXT("attacker registers"),
    Runtime.RegisterEntity(Attacker, MakeMixedBinding()));
  FCrowdArriveAtLocationPayload Arrive;
  Arrive.TargetLocation = FVector3f(200.0f, 0.0f, 100.0f);
  Arrive.MaximumSpeedCmps = 250.0f;
  Arrive.AcceptanceRadiusCm = 20.0f;
  Arrive.SlowdownRadiusCm = 150.0f;
  FCrowdFaceMovementPayload FaceMovement;
  FaceMovement.MinimumSpeedCmps = 1.0f;
  FCrowdDemoBehaviorSourcePayload Pickup;
  Pickup.TargetRef = {2, Hauler.StableEntityId, 1};
  Pickup.CommitId = 0x1001;
  Pickup.PrimaryId = CrowdDemoBehaviorAdapterIds::CargoPickup;
  Pickup.SecondaryId = 1;
  Pickup.Quantity = 1;
  const TArray<FCrowdDemoDesiredSource> HaulerDesired = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 1,
      CrowdStandardSources::ArriveAtLocation, Arrive),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceMovement, FaceMovement),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Interaction, 1,
      CrowdDemoSourceTypeIds::PickupInteraction, Pickup)};
  TestTrue(TEXT("pickup composes standard and product sources"),
    QueueDesiredSources(Runtime, Hauler, 10, HaulerDesired));

  const FCrowdStableEntityRef Target{1, 20, 1};
  FCrowdPursueEntityPayload Pursue;
  Pursue.TargetRef = Target;
  Pursue.MaximumSpeedCmps = 300.0f;
  Pursue.AcceptanceRadiusCm = 25.0f;
  Pursue.MaximumPredictionSeconds = 0.5f;
  FCrowdMaintainDistancePayload Distance;
  Distance.TargetRef = Target;
  Distance.MinimumDistanceCm = 100.0f;
  Distance.MaximumDistanceCm = 200.0f;
  Distance.HysteresisCm = 10.0f;
  Distance.MaximumCorrectionSpeedCmps = 80.0f;
  FCrowdFaceEntityPayload FaceTarget;
  FaceTarget.TargetRef = Target;
  FCrowdMovementLockPayload Lock;
  const TArray<FCrowdDemoDesiredSource> AttackerDesired = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 1,
      CrowdStandardSources::PursueEntity, Pursue),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 2,
      CrowdStandardSources::MaintainDistance, Distance),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceEntity, FaceTarget),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Reaction, 1,
      CrowdStandardSources::MovementLock, Lock, 1)};
  const TArray<FCrowdBehaviorContextRecord> TargetRecords = {
    MakeTargetRecord(Target, FVector3f(200.0f, 0.0f, 100.0f))};
  TestTrue(TEXT("attack composes source responsibilities"),
    QueueDesiredSources(
      Runtime, Attacker, 10, AttackerDesired, TargetRecords));
  FCrowdBehaviorPreparedBoundary Prepared;
  if (!TestTrue(TEXT("composed boundary prepares"),
      Runtime.PrepareBoundary(10, Prepared)))
    return false;
  if (!TestEqual(TEXT("both entities resolve"),
      Prepared.Entities.Num(), 2))
    return false;
  TestTrue(TEXT("pickup produces business output"),
    Prepared.Entities[0].ResolvedChannels.Business.Num() == 1);
  TestFalse(TEXT("pickup business does not lock movement"),
    Prepared.Entities[0].ResolvedChannels.bMovementLocked);
  TestTrue(TEXT("pickup business keeps resolved movement"),
    !Prepared.Entities[0].ResolvedChannels.DesiredVelocity.IsNearlyZero());
  TestTrue(TEXT("attack business is no longer a behavior source"),
    Prepared.Entities[1].ResolvedChannels.Business.IsEmpty());
  TestTrue(TEXT("attack stops through explicit constraint"),
    Prepared.Entities[1].ResolvedChannels.bMovementLocked);
  TestTrue(TEXT("source boundary commits"), Runtime.CommitPrepared(Prepared));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoMixedSandboxArchitectureTest,
  "CrowdDemo.MixedSandbox.J.Architecture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoMixedSandboxArchitectureTest::RunTest(const FString& Parameters)
{
  FString GameMode;
  FString Coordinator;
  FString Runner;
  TestTrue(TEXT("GameMode readable"), FFileHelper::LoadFileToString(
    GameMode, *FPaths::Combine(FPaths::ProjectDir(),
      TEXT("Source/MassAICrowdDemo/CrowdDemoGameMode.cpp"))));
  TestTrue(TEXT("mixed coordinator readable"), FFileHelper::LoadFileToString(
    Coordinator, *FPaths::Combine(FPaths::ProjectDir(),
      TEXT("Source/MassAICrowdDemo/CrowdDemoMixedSandboxCoordinator.cpp"))));
  TestTrue(TEXT("runner readable"), FFileHelper::LoadFileToString(
    Runner, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/RunCrowdDemo.ps1"))));

  const int32 MixedBranch = GameMode.Find(TEXT("CrowdDemoMixedSandbox"));
  const int32 FixedSpawn = GameMode.Find(TEXT("MassSubsystem->SpawnAgents"));
  TestTrue(TEXT("mixed path bypasses fixed Round agents"),
    MixedBranch != INDEX_NONE && FixedSpawn != INDEX_NONE && MixedBranch < FixedSpawn);
  TestTrue(TEXT("mixed path owns real lifecycle"),
    Coordinator.Contains(TEXT("LifecycleWorld.ApplyAtBoundary")));
  TestTrue(TEXT("mixed path evaluates composable behavior sources"),
    Coordinator.Contains(TEXT("FCrowdDemoPlanningRuntimeHost::Stage"))
      && Coordinator.Contains(TEXT("BehaviorSourceRuntime->PrepareBoundary"))
      && Coordinator.Contains(TEXT(
        "FCrowdDemoBusinessPatchAdapter::Prepare")));
  TestFalse(TEXT("mixed coordinator no longer owns source diff wiring"),
    Coordinator.Contains(TEXT(
      "FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff"))
      || Coordinator.Contains(TEXT("ApplyPlannerDecision(")));
  TestFalse(TEXT("legacy behavior recipe is no longer authoritative"),
    Coordinator.Contains(TEXT("FCrowdDemoBehaviorRecipe")));
  const int32 MovementStart =
    Coordinator.Find(TEXT(
      "bool ACrowdDemoMixedSandboxCoordinator::BeginProductMovementBoundary("));
  const int32 MovementEnd =
    Coordinator.Find(TEXT(
      "bool ACrowdDemoMixedSandboxCoordinator::GetOrBuildFlow("),
      ESearchCase::CaseSensitive, ESearchDir::FromStart,
      MovementStart + 1);
  const FString MovementBody =
    MovementStart != INDEX_NONE && MovementEnd > MovementStart
      ? Coordinator.Mid(MovementStart, MovementEnd - MovementStart)
      : FString();
  TestFalse(TEXT("production movement does not inspect source type ids"),
    MovementBody.Contains(TEXT("SourceTypeId"))
      || MovementBody.Contains(TEXT("StagedSourceSet.Instances")));
  TestTrue(TEXT("production movement consumes resolved channels"),
    Coordinator.Contains(TEXT("Resolved.DesiredVelocity"))
      && Coordinator.Contains(TEXT("Resolved.DesiredFacing"))
      && Coordinator.Contains(TEXT("Resolved.MovementGoal")));
  TestTrue(TEXT("mixed movement executes the complete runtime work chain"),
    Coordinator.Contains(
      TEXT("FCrowdMassMovementPipelineWork::Run("))
      && Coordinator.Contains(
        TEXT("FCrowdMassParticlePipelineWork::Run("))
      && Coordinator.Contains(
        TEXT("FCrowdMassFacingFinalizeWork::Run(")));
  TestFalse(TEXT("business output does not implicitly suppress movement"),
    Coordinator.Contains(
      TEXT("ResolvedChannels.Business.IsEmpty()")));
  const int32 MarkValidated =
    Coordinator.Find(TEXT("Runner.MarkValidated("));
  const int32 FinalTransformWrite =
    Coordinator.Find(TEXT("SetLocation("),
      ESearchCase::CaseSensitive,
      ESearchDir::FromStart,
      MarkValidated);
  TestTrue(TEXT("mixed final movement write follows validation"),
    MarkValidated != INDEX_NONE
      && FinalTransformWrite > MarkValidated);
  TestTrue(TEXT("source authority is the Runtime world store"),
    Coordinator.Contains(
      TEXT("&RuntimeSubsystem->GetBehaviorSourceRuntime()")));
  TestFalse(TEXT("mixed production path no longer selects a behavior provider"),
    Coordinator.Contains(TEXT("BehaviorProviders"))
      || Coordinator.Contains(TEXT(
        "FCrowdRuntimeBehaviorTransition::Evaluate")));
  TestTrue(TEXT("mixed path consumes Runtime-owned Nav resources"),
    Coordinator.Contains(TEXT("UMassCrowdRuntimeSubsystem"))
      && Coordinator.Contains(TEXT("GetNavGraphResource"))
      && Coordinator.Contains(TEXT("AcquireFlow")));
  TestFalse(TEXT("mixed path does not build a private Recast graph"),
    Coordinator.Contains(TEXT("ExtractStaticRecast"))
      || Coordinator.Contains(TEXT("FCrowdNavSurfaceGraphExtractor::BuildFlow")));
  TestTrue(TEXT("mixed path uses the product boundary runner"),
    Coordinator.Contains(
      TEXT("MakeUnique<FCrowdMassBoundaryRunner>()"))
      && Coordinator.Contains(TEXT("BeginProductMovementBoundary"))
      && Coordinator.Contains(TEXT("PollProductMovementBoundary"))
      && Coordinator.Contains(TEXT("BuildAndSealCommit")));
  TestFalse(TEXT("mixed path has no direct shared-flow movement owner"),
    Coordinator.Contains(TEXT("MoveAlongSharedFlow")));
  TestTrue(TEXT("mixed path drains public apply frames"),
    Coordinator.Contains(TEXT("DrainClientApplyFrames"))
      && Coordinator.Contains(TEXT("Frame.Corrections")));
  TestTrue(TEXT("small-population baseline clamps chunk capacity to snapshot limits"),
    Coordinator.Contains(TEXT("FMath::Min(128, FMath::Max(1, Config.PopulationLimit))")));
  TestTrue(TEXT("baseline publication failure retries with a bounded cooldown"),
    Coordinator.Contains(TEXT("*EligibleSeconds = World->GetTimeSeconds() + 1.0"))
      && Coordinator.Contains(TEXT("stage=baseline_retry")));
  TestTrue(TEXT("mixed attack snapshot has an explicit v2 gate"),
    Coordinator.Contains(TEXT("MixedAgentPayloadVersion = 2"))
      && Coordinator.Contains(TEXT(
        "PayloadVersion != MixedAgentPayloadVersion")));
  TestTrue(TEXT("late join carries complete cooldown state"),
    Coordinator.Contains(TEXT("AttackCooldownEndFixedStep"))
      && Coordinator.Contains(TEXT(
        "Slot.AttackState.CooldownEndFixedStep")));
  TestTrue(TEXT("mixed path delegates atomic presentation lifecycle"),
    Coordinator.Contains(TEXT("UMassCrowdPresentationSubsystem"))
      && Coordinator.Contains(TEXT("PrepareFrame"))
      && Coordinator.Contains(TEXT("ApplyPreparedFrame")));
  TestTrue(TEXT("mixed scale gate uses public projectile boundary and Mass store"),
    Coordinator.Contains(
      TEXT("FCrowdProjectileBoundaryPipeline::Prepare"))
      && Coordinator.Contains(
        TEXT("ProjectileStore.ApplyValidated"))
      && Coordinator.Contains(
        TEXT("Config.PopulationLimit / 5")));
  TestTrue(TEXT("mixed projectile hits use the generic combat resolver"),
    Coordinator.Contains(
      TEXT("FCrowdCombatResolver::Resolve"))
      && Coordinator.Contains(
        TEXT("FCrowdPreparedHostHitCommit")));
  TestFalse(TEXT("mixed coordinator does not mutate ISM slots directly"),
    Coordinator.Contains(TEXT("->AddInstance("))
      || Coordinator.Contains(TEXT("->RemoveInstance("))
      || Coordinator.Contains(TEXT("->UpdateInstanceTransform(")));
  TestFalse(TEXT("mixed coordinator does not consume RoundPlan"),
    Coordinator.Contains(TEXT("RoundPlan")));
  TestTrue(TEXT("runner exposes and gates mixed path"),
    Runner.Contains(TEXT("[switch]$MixedSandbox"))
      && Runner.Contains(TEXT("PASS CrowdDemoMixedSandbox role=server"))
      && Runner.Contains(TEXT("PASS CrowdDemoMixedSandbox role=client"))
      && Runner.Contains(TEXT("projectile_spawned=$ExpectedProjectiles"))
      && Runner.Contains(TEXT("projectile_hash=$ProjectileHash")));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoBehaviorDesiredDiffTest,
  "CrowdDemo.MixedSandbox.J.DesiredSourceDiff",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoBehaviorDesiredDiffTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("provider registry initializes"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 19, 1};
  TestTrue(TEXT("entity registers"),
    Runtime.RegisterEntity(EntityRef, MakeMixedBinding()));

  FCrowdMoveToLocationPayload Move;
  Move.TargetLocation = FVector3f(500.0f, 0.0f, 0.0f);
  Move.MaximumSpeedCmps = 200.0f;
  Move.AcceptanceRadiusCm = 10.0f;
  FCrowdFaceMovementPayload Face;
  Face.MinimumSpeedCmps = 1.0f;
  const TArray<FCrowdDemoDesiredSource> InitialDesired = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 1,
      CrowdStandardSources::MoveToLocation, Move),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceMovement, Face)};
  TArray<FCrowdBehaviorSourceCommand> Commands;
  TestTrue(TEXT("initial desired set builds"),
    FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      1, *Runtime.FindSourceSet(EntityRef),
      InitialDesired, Commands));
  TestEqual(TEXT("initial movement and facing start"),
    Commands.Num(), 2);
  if (Commands.Num() == 2)
  {
    TestEqual(TEXT("navigation command has independent sequence"),
      Commands[0].CommandSequence, 1u);
    TestEqual(TEXT("facing command has independent sequence"),
      Commands[1].CommandSequence, 1u);
  }
  for (const FCrowdBehaviorSourceCommand& Command : Commands)
    TestTrue(TEXT("initial command queues"),
      Runtime.QueueCommand(Command));
  FCrowdBehaviorEntityEvaluationContext Evaluation;
  Evaluation.EntityRef = EntityRef;
  Evaluation.FixedStepIndex = 1;
  Evaluation.Facing = FVector::ForwardVector;
  Evaluation.RecalculateStableHash();
  TestTrue(TEXT("initial context accepted"),
    Runtime.SetEvaluationContext(Evaluation));
  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("initial desired set prepares"),
    Runtime.PrepareBoundary(1, Prepared));
  TestTrue(TEXT("initial desired set commits"),
    Runtime.CommitPrepared(Prepared));

  TestTrue(TEXT("unchanged desired set diffs"),
    FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      2, *Runtime.FindSourceSet(EntityRef),
      InitialDesired, Commands));
  TestEqual(TEXT("unchanged desired set emits no churn"),
    Commands.Num(), 0);

  FCrowdDemoBehaviorSourcePayload Pickup;
  Pickup.TargetRef = {2, 99, 1};
  Pickup.CommitId = 0x2001;
  Pickup.PrimaryId = CrowdDemoBehaviorAdapterIds::CargoPickup;
  Pickup.SecondaryId = 1;
  Pickup.Quantity = 1;
  TArray<FCrowdDemoDesiredSource> WithBusiness = InitialDesired;
  WithBusiness.Add(MakeDesiredSource(
    CrowdDemoBehaviorControllerIds::Interaction, 1,
    CrowdDemoSourceTypeIds::PickupInteraction, Pickup));
  TestTrue(TEXT("business source diffs"),
    FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      2, *Runtime.FindSourceSet(EntityRef),
      WithBusiness, Commands));
  TestEqual(TEXT("transition only starts missing interaction"),
    Commands.Num(), 1);
  if (Commands.Num() == 1)
  {
    TestTrue(TEXT("movement sources remain untouched"),
      Commands[0].Kind == ECrowdBehaviorSourceCommandKind::Start);
    TestTrue(TEXT("new source is pickup adapter"),
      Commands[0].SourceTypeId
        == CrowdDemoSourceTypeIds::PickupInteraction);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoStandardSourceLifecycleCompositionTest,
  "CrowdDemo.MixedSandbox.J.StandardSourceLifecycleComposition",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoStandardSourceLifecycleCompositionTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!TestTrue(TEXT("provider registry initializes"),
      Runtime.InitializeFromRegisteredProviders()))
    return false;
  const FCrowdStableEntityRef EntityRef{1, 41, 1};
  if (!TestTrue(TEXT("entity registers"),
      Runtime.RegisterEntity(EntityRef, MakeMixedBinding())))
    return false;

  FCrowdArriveAtLocationPayload Arrive;
  Arrive.TargetLocation = FVector3f(500.0f, 0.0f, 0.0f);
  Arrive.MaximumSpeedCmps = 200.0f;
  Arrive.AcceptanceRadiusCm = 10.0f;
  Arrive.SlowdownRadiusCm = 100.0f;
  FCrowdFaceMovementPayload Face;
  Face.MinimumSpeedCmps = 1.0f;
  FCrowdSpeedLimitPayload Limit;
  Limit.MaximumSpeedCmps = 150.0f;
  Limit.AllowedNavLayerMask = MAX_uint64;
  FCrowdDemoBehaviorSourcePayload Carry;
  Carry.PrimaryId = 1;
  Carry.SecondaryId = 1;
  const TArray<FCrowdDemoDesiredSource> Persistent = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 1,
      CrowdStandardSources::ArriveAtLocation, Arrive),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 3,
      CrowdStandardSources::SpeedLimit, Limit),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceMovement, Face),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Presentation, 1,
      CrowdDemoSourceTypeIds::CarryCargo, Carry)};

  auto PrepareAndCommit =
    [&](const int64 Step,
      const TConstArrayView<FCrowdDemoDesiredSource> Desired,
      FCrowdBehaviorPreparedBoundary* OutPrepared = nullptr)
  {
    if (!QueueDesiredSources(Runtime, EntityRef, Step, Desired))
      return false;
    FCrowdBehaviorPreparedBoundary Prepared;
    if (!Runtime.PrepareBoundary(Step, Prepared))
      return false;
    if (OutPrepared) *OutPrepared = Prepared;
    return Runtime.CommitPrepared(Prepared);
  };
  if (!TestTrue(TEXT("persistent composition commits"),
      PrepareAndCommit(1, Persistent)))
    return false;

  TMap<FCrowdBehaviorSourceHandle, uint64> PersistentHashes;
  for (const FCrowdBehaviorSourceInstance& Instance
    : Runtime.FindSourceSet(EntityRef)->Instances)
  {
    PersistentHashes.Add(
      Instance.Handle, Instance.CalculateStableHash());
  }

  FCrowdTimedImpulsePayload Impulse;
  Impulse.InitialVelocity = FVector3f(-300.0f, 0.0f, 0.0f);
  Impulse.DecayMode = ECrowdImpulseDecayMode::Linear;
  TArray<FCrowdDemoDesiredSource> WithHitReaction = Persistent;
  WithHitReaction.Add(MakeDesiredSource(
    CrowdDemoBehaviorControllerIds::Reaction, 3,
    CrowdStandardSources::TimedImpulse, Impulse, 2));
  FCrowdBehaviorPreparedBoundary HitPrepared;
  if (!TestTrue(TEXT("hit reaction commits beside persistent sources"),
      PrepareAndCommit(2, WithHitReaction, &HitPrepared)))
    return false;
  TestTrue(TEXT("hit reaction temporarily overrides movement"),
    HitPrepared.Entities.Num() == 1
      && HitPrepared.Entities[0].ResolvedChannels.DesiredVelocity.X < 0.0);
  for (const FCrowdBehaviorSourceInstance& Instance
    : Runtime.FindSourceSet(EntityRef)->Instances)
  {
    if (Instance.Handle.ControllerId
      == CrowdDemoBehaviorControllerIds::Reaction)
      continue;
    const uint64* Before = PersistentHashes.Find(Instance.Handle);
    TestTrue(TEXT("hit reaction preserves persistent handle"),
      Before != nullptr);
    if (Before)
      TestEqual(TEXT("hit reaction preserves payload and state"),
        Instance.CalculateStableHash(), *Before);
  }
  if (!TestTrue(TEXT("hit reaction stops cleanly"),
      PrepareAndCommit(3, Persistent)))
    return false;
  TestEqual(TEXT("only persistent sources remain after hit reaction"),
    Runtime.FindSourceSet(EntityRef)->Instances.Num(),
    Persistent.Num());

  FCrowdMovementLockPayload Lock;
  TArray<FCrowdDemoDesiredSource> WithAttack = Persistent;
  WithAttack.Add(MakeDesiredSource(
    CrowdDemoBehaviorControllerIds::Reaction, 1,
    CrowdStandardSources::MovementLock, Lock, 1));
  FCrowdBehaviorPreparedBoundary AttackPrepared;
  if (!TestTrue(TEXT("attack and explicit one-frame lock commit"),
      PrepareAndCommit(4, WithAttack, &AttackPrepared)))
    return false;
  TestTrue(TEXT("attack lock is explicit"),
    AttackPrepared.Entities.Num() == 1
      && AttackPrepared.Entities[0]
        .ResolvedChannels.bMovementLocked);
  TestTrue(TEXT("attack intent stays outside behavior channels"),
    AttackPrepared.Entities[0].ResolvedChannels.Business.IsEmpty());
  FCrowdBehaviorPreparedBoundary Recovered;
  if (!TestTrue(TEXT("one-frame attack lock is removed"),
      PrepareAndCommit(5, Persistent, &Recovered)))
    return false;
  TestTrue(TEXT("navigation resumes after one-frame attack lock"),
    Recovered.Entities.Num() == 1
      && !Recovered.Entities[0].ResolvedChannels.bMovementLocked
      && Recovered.Entities[0]
        .ResolvedChannels.Business.IsEmpty()
      && !Recovered.Entities[0]
        .ResolvedChannels.DesiredVelocity.IsNearlyZero());
  for (const FCrowdBehaviorSourceInstance& Instance
    : Runtime.FindSourceSet(EntityRef)->Instances)
  {
    const uint64* Before = PersistentHashes.Find(Instance.Handle);
    TestTrue(TEXT("attack recovery preserves persistent handle"),
      Before != nullptr);
    if (Before)
      TestEqual(TEXT("attack recovery preserves payload and state"),
        Instance.CalculateStableHash(), *Before);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoTargetLossStopsDependentSourcesTest,
  "CrowdDemo.MixedSandbox.J.TargetLossStopsDependentSources",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoTargetLossStopsDependentSourcesTest::RunTest(
  const FString& Parameters)
{
  FCrowdBehaviorSourceRuntime Runtime;
  if (!Runtime.InitializeFromRegisteredProviders()) return false;
  const FCrowdStableEntityRef EntityRef{1, 51, 1};
  const FCrowdStableEntityRef TargetRef{1, 52, 1};
  if (!Runtime.RegisterEntity(EntityRef, MakeMixedBinding()))
    return false;
  FCrowdPursueEntityPayload Pursue;
  Pursue.TargetRef = TargetRef;
  Pursue.MaximumSpeedCmps = 200.0f;
  Pursue.AcceptanceRadiusCm = 10.0f;
  Pursue.MaximumPredictionSeconds = 0.25f;
  FCrowdMaintainDistancePayload Distance;
  Distance.TargetRef = TargetRef;
  Distance.MinimumDistanceCm = 100.0f;
  Distance.MaximumDistanceCm = 200.0f;
  Distance.HysteresisCm = 10.0f;
  Distance.MaximumCorrectionSpeedCmps = 50.0f;
  FCrowdFaceEntityPayload Face;
  Face.TargetRef = TargetRef;
  FCrowdSpeedLimitPayload Limit;
  Limit.MaximumSpeedCmps = 150.0f;
  Limit.AllowedNavLayerMask = MAX_uint64;
  const TArray<FCrowdDemoDesiredSource> Targeted = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 1,
      CrowdStandardSources::PursueEntity, Pursue),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 2,
      CrowdStandardSources::MaintainDistance, Distance),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 3,
      CrowdStandardSources::SpeedLimit, Limit),
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Facing, 1,
      CrowdStandardSources::FaceEntity, Face)};
  const TArray<FCrowdBehaviorContextRecord> TargetRecords = {
    MakeTargetRecord(TargetRef, FVector3f(500.0f, 0.0f, 0.0f))};
  if (!QueueDesiredSources(
      Runtime, EntityRef, 1, Targeted, TargetRecords))
    return false;
  FCrowdBehaviorPreparedBoundary Prepared;
  if (!Runtime.PrepareBoundary(1, Prepared)
    || !Runtime.CommitPrepared(Prepared))
    return false;

  const TArray<FCrowdDemoDesiredSource> LostTargetDesired = {
    MakeDesiredSource(
      CrowdDemoBehaviorControllerIds::Navigation, 3,
      CrowdStandardSources::SpeedLimit, Limit)};
  TArray<FCrowdBehaviorSourceCommand> Commands;
  TestTrue(TEXT("lost target builds a valid source diff"),
    FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff(
      2, *Runtime.FindSourceSet(EntityRef),
      LostTargetDesired, Commands));
  TestEqual(TEXT("all three target-dependent sources stop"),
    Commands.Num(), 3);
  for (const FCrowdBehaviorSourceCommand& Command : Commands)
  {
    TestEqual(TEXT("target-loss command is stop"),
      Command.Kind, ECrowdBehaviorSourceCommandKind::Stop);
    TestTrue(TEXT("target-loss stop queues"),
      Runtime.QueueCommand(Command));
  }
  FCrowdBehaviorEntityEvaluationContext NoTargetContext;
  NoTargetContext.EntityRef = EntityRef;
  NoTargetContext.FixedStepIndex = 2;
  NoTargetContext.Facing = FVector::ForwardVector;
  NoTargetContext.RecalculateStableHash();
  TestTrue(TEXT("target-loss context has no stale target record"),
    Runtime.SetEvaluationContext(NoTargetContext));
  TestTrue(TEXT("target-loss boundary prepares atomically"),
    Runtime.PrepareBoundary(2, Prepared));
  TestTrue(TEXT("target-loss boundary commits"),
    Runtime.CommitPrepared(Prepared));
  const FCrowdBehaviorSourceSet* Final =
    Runtime.FindSourceSet(EntityRef);
  TestTrue(TEXT("source set remains valid"), Final != nullptr);
  if (Final)
  {
    TestEqual(TEXT("only independent speed limit survives"),
      Final->Instances.Num(), 1);
    TestTrue(TEXT("surviving source is independent"),
      Final->Instances[0].SourceTypeId
        == CrowdStandardSources::SpeedLimit);
  }
  return true;
}

#endif
