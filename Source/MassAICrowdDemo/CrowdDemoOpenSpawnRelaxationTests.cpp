#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"
#include "Mass/CrowdDemoOpenSpawnStateTranslation.h"
#include "CrowdDemoBusinessSourceProvider.h"
#include "MassCrowdBehaviorSourceRuntime.h"
#include "MassCrowdWorkerLifecycleBehaviorDomain.h"

namespace
{
  TArray<FCrowdDemoOpenSpawnRelaxationLayoutInput> MakeInputs(const bool bReverse = false)
  {
    TArray<FCrowdDemoOpenSpawnRelaxationLayoutInput> Inputs;
    for (int32 Index = 0; Index < 20; ++Index)
    {
      FCrowdDemoOpenSpawnRelaxationLayoutInput Input;
      Input.AgentId = 100 + Index;
      Input.FormationIndex = Index;
      Inputs.Add(Input);
    }
    if (bReverse) Algo::Reverse(Inputs);
    return Inputs;
  }

  void AdvanceToInsertion(
    const FCrowdDemoOpenSpawnRelaxationLayout& Layout,
    FCrowdDemoOpenSpawnRelaxationRuntime& Runtime)
  {
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(0, Layout, Runtime);
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(15, Layout, Runtime);
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(30, Layout, Runtime);
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(45, Layout, Runtime);
    FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(60, Layout, Runtime);
  }

  FCrowdBehaviorSourceCommand MakeSemanticStateCommand(
    const FCrowdStableEntityRef EntityRef,
    const int64 FixedStepIndex,
    const uint32 CommandSequence,
    const ECrowdBehaviorSourceCommandKind Kind,
    const ECrowdSemanticBehaviorState State)
  {
    FCrowdSemanticBehaviorStatePayload StatePayload;
    StatePayload.State = State;
    FCrowdBehaviorSourceCommand Command;
    Command.EffectiveFixedStep = FixedStepIndex;
    Command.Handle = {
      EntityRef,
      CrowdDemoBehaviorControllerIds::SemanticState,
      1};
    Command.CommandSequence = CommandSequence;
    Command.Kind = Kind;
    Command.SourceTypeId = CrowdStandardSources::SemanticState;
    Command.Payload.Set(
      CrowdStandardSources::PayloadSchema(
        CrowdStandardSources::SemanticState),
      StatePayload);
    return Command;
  }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnRelaxationLayoutTest,
  "CrowdDemo.SoftPressure.T1.Layout",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnRelaxationLayoutTest::RunTest(const FString& Parameters)
{
  const auto Inputs = MakeInputs();
  const auto ReverseInputs = MakeInputs(true);
  const auto Layout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(Inputs);
  const auto ReverseLayout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(ReverseInputs);
  TestTrue(TEXT("layout is valid"), Layout.bValid);
  TestEqual(TEXT("layout has twenty stable instances"), Layout.Agents.Num(), 20);
  TestEqual(TEXT("input reversal preserves layout hash"), Layout.LayoutHash, ReverseLayout.LayoutHash);
  TestEqual(TEXT("source is formation index nineteen"), Layout.SourceAgentId, 119);
  TestEqual(TEXT("removal target is fixed inner agent"), Layout.RemovedAgentId, 110);

  const auto OpenConfig = FCrowdDemoOpenSpawnRelaxationKernel::MakeOpenFlowConfig();
  TestEqual(TEXT("T1 open config has no obstacles"), OpenConfig.ObstacleSpecs.Num(), 0);
  TestTrue(TEXT("T1 bounds cover staging"), OpenConfig.BoundsMin.X <= -4000.0f &&
    OpenConfig.BoundsMax.X >= 4000.0f);

  TArray<FCrowdDemoOpenSpawnRelaxationLayoutInput> PermutedIdInputs;
  for (int32 Index = 0; Index < 20; ++Index)
  {
    FCrowdDemoOpenSpawnRelaxationLayoutInput Input;
    Input.AgentId = 500 + ((Index * 7) % 20);
    Input.FormationIndex = Index;
    PermutedIdInputs.Add(Input);
  }
  const auto PermutedLayout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(PermutedIdInputs);
  auto PermutedRuntime = FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(PermutedLayout);
  TArray<int32> FinalIds;
  TArray<FVector> FinalLocations;
  for (auto& Agent : PermutedRuntime.Agents)
  {
    Agent.bParticleActive = true;
    FinalIds.Add(Agent.AgentId);
    const auto* LayoutAgent = PermutedLayout.Agents.FindByPredicate([&](const auto& Candidate)
      { return Candidate.AgentId == Agent.AgentId; });
    FinalLocations.Add(LayoutAgent ? LayoutAgent->ActiveLocation : FVector::ZeroVector);
  }
  FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
    FinalIds, FinalLocations, PermutedRuntime);
  TestTrue(TEXT("permuted AgentIds preserve pre-insert location association"),
    PermutedRuntime.bValid);
  TestEqual(TEXT("permuted AgentIds do not manufacture displacement"),
    PermutedRuntime.NewEquilibriumDisplacedAgentCount, 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnRelaxationLifecycleTest,
  "CrowdDemo.SoftPressure.T1.Lifecycle",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnRelaxationLifecycleTest::RunTest(const FString& Parameters)
{
  const auto Layout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(MakeInputs());
  auto Runtime = FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(Layout);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(0, Layout, Runtime);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(15, Layout, Runtime);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(30, Layout, Runtime);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(45, Layout, Runtime);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(60, Layout, Runtime);

  const TArray<int32> ExpectedBeforeRemoval{0, 5, 10, 15, 19, 20};
  TestEqual(TEXT("activation schedule has stable transition count"),
    Runtime.ActiveCountTransitions.Num(), ExpectedBeforeRemoval.Num());
  for (int32 Index = 0; Index < ExpectedBeforeRemoval.Num(); ++Index)
    TestEqual(FString::Printf(TEXT("active transition %d"), Index),
      Runtime.ActiveCountTransitions[Index], ExpectedBeforeRemoval[Index]);
  TestEqual(TEXT("source insertion enters propagation phase"),
    static_cast<int32>(Runtime.Phase),
    static_cast<int32>(ECrowdDemoOpenSpawnRelaxationPhase::PropagationAndInsertSettle));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnRelaxationPropagationTest,
  "CrowdDemo.SoftPressure.T1.PropagationAndSettling",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnRelaxationPropagationTest::RunTest(const FString& Parameters)
{
  const auto Layout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(MakeInputs());
  auto Runtime = FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(Layout);
  AdvanceToInsertion(Layout, Runtime);

  auto MakeInfluence = [](const int32 A, const int32 B, const bool bRealized)
  {
    FCrowdDemoParticleSoftPairInfluence Influence;
    Influence.MinAgentId = FMath::Min(A, B);
    Influence.MaxAgentId = FMath::Max(A, B);
    Influence.RequestedCorrectionA = FVector(1.0f, 0.0f, 0.0f);
    Influence.RequestedCorrectionB = FVector(-1.0f, 0.0f, 0.0f);
    if (bRealized)
    {
      Influence.RealizedCorrectionA = Influence.RequestedCorrectionA;
      Influence.RealizedCorrectionB = Influence.RequestedCorrectionB;
    }
    return Influence;
  };

  TArray<FCrowdDemoParticleSoftPairInfluence> Influences;
  Influences.Add(MakeInfluence(119, 110, true));
  Influences.Add(MakeInfluence(110, 105, true));
  Influences.Add(MakeInfluence(105, 100, true));
  Influences.Add(MakeInfluence(101, 107, false));
  TArray<FCrowdDemoParticleSoftPairInfluence> ReverseInfluences = Influences;
  Algo::Reverse(ReverseInfluences);

  auto ReverseRuntime = Runtime;
  FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
    60, Influences, 0.0f, 4.0f, Runtime);
  FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
    60, ReverseInfluences, 0.0f, 4.0f, ReverseRuntime);
  TestEqual(TEXT("three-hop realized pressure reaches layer three"),
    Runtime.PressurePropagationLayerMax, 3);
  TestEqual(TEXT("requested-only pair is not a propagation edge"),
    Runtime.CumulativeInfluenceEdges.Num(), 3);
  TestEqual(TEXT("pair input reversal preserves propagation hash"),
    Runtime.PropagationHash, ReverseRuntime.PropagationHash);

  for (int32 Step = 61; Step <= 75; ++Step)
    FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
      Step, Influences, 0.0f, 4.0f, Runtime);
  TestEqual(TEXT("insert settling schedules removal only after layer three"),
    static_cast<int32>(Runtime.Phase),
    static_cast<int32>(ECrowdDemoOpenSpawnRelaxationPhase::Removal));
  TestTrue(TEXT("insert settling step recorded"), Runtime.InsertSettlingStep != INDEX_NONE);

  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(76, Layout, Runtime);
  TestEqual(TEXT("removal changes active count to nineteen"),
    Runtime.ActiveCountTransitions.Last(), 19);
  TestEqual(TEXT("removal begins independent post-removal tracker"),
    Runtime.PostRemovalSettling.StepCount, 0);
  for (int32 Step = 76; Step <= 91; ++Step)
    FCrowdDemoOpenSpawnRelaxationKernel::RecordParticleStep(
      Step, {}, 0.0f, 2.0f, Runtime);
  TestEqual(TEXT("post-removal settling completes independently"),
    static_cast<int32>(Runtime.Phase),
    static_cast<int32>(ECrowdDemoOpenSpawnRelaxationPhase::Completed));
  TestTrue(TEXT("post-removal settling step recorded"),
    Runtime.PostRemovalSettlingStep != INDEX_NONE);
  TArray<int32> FinalIds;
  TArray<FVector> FinalLocations;
  for (int32 Index = 0; Index < Runtime.Agents.Num(); ++Index)
  {
    FinalIds.Add(Runtime.Agents[Index].AgentId);
    FVector Location = Runtime.PreInsertLocationsByAgent[Index];
    if (Runtime.Agents[Index].bParticleActive && Runtime.Agents[Index].AgentId != Runtime.SourceAgentId)
      Location.X += 2.0f;
    FinalLocations.Add(Location);
  }
  FCrowdDemoOpenSpawnRelaxationKernel::RecordFinalLocations(
    FinalIds, FinalLocations, Runtime);
  TestEqual(TEXT("no old-layout restoration guidance is reported"),
    Runtime.OldLayoutReturnedAgentCount, 0);
  TestTrue(TEXT("post-removal state proves a new quantized equilibrium"),
    Runtime.NewEquilibriumDisplacedAgentCount > 0);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnPreparedBoundaryFactsTest,
  "CrowdDemo.SoftPressure.T1.PreparedBoundaryFacts",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnPreparedBoundaryFactsTest::RunTest(const FString& Parameters)
{
  const auto Layout = FCrowdDemoOpenSpawnRelaxationKernel::BuildLayout(MakeInputs());
  auto Runtime = FCrowdDemoOpenSpawnRelaxationKernel::InitializeRuntime(Layout);
  FCrowdDemoOpenSpawnRelaxationKernel::PrepareBoundary(0, Layout, Runtime);

  TArray<int32> AgentIds;
  for (const auto& Agent : Runtime.Agents) AgentIds.Add(Agent.AgentId);
  TArray<int32> ReversedIds = AgentIds;
  Algo::Reverse(ReversedIds);
  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> Facts;
  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> ReversedFacts;
  TestTrue(TEXT("prepared facts build"),
    FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      0, AgentIds, Runtime, Facts));
  TestTrue(TEXT("reversed expected input builds"),
    FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      0, ReversedIds, Runtime, ReversedFacts));
  TestEqual(TEXT("stable fact count"), Facts.Num(), ReversedFacts.Num());
  for (int32 Index = 0; Index < Facts.Num(); ++Index)
  {
    TestEqual(FString::Printf(TEXT("stable AgentId %d"), Index),
      Facts[Index].AgentId, ReversedFacts[Index].AgentId);
    TestEqual(FString::Printf(TEXT("stable participation %d"), Index),
      Facts[Index].bParticleActive, ReversedFacts[Index].bParticleActive);
    TestEqual(FString::Printf(TEXT("stable pending reset %d"), Index),
      Facts[Index].bPendingBoundaryReset,
      ReversedFacts[Index].bPendingBoundaryReset);
  }

  TArray<int32> DuplicateIds = AgentIds;
  DuplicateIds.Last() = DuplicateIds[0];
  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> InvalidFacts;
  TestFalse(TEXT("duplicate expected AgentId rejected"),
    FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      0, DuplicateIds, Runtime, InvalidFacts));
  TArray<int32> MissingIds = AgentIds;
  MissingIds.Pop();
  TestFalse(TEXT("missing expected Agent rejected"),
    FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      0, MissingIds, Runtime, InvalidFacts));
  TestFalse(TEXT("stale boundary facts rejected"),
    FCrowdDemoOpenSpawnRelaxationKernel::ValidatePreparedBoundaryFacts(
      1, AgentIds, Facts));

  TArray<int32> PendingResetIds;
  for (const auto& Fact : Facts)
    if (Fact.bPendingBoundaryReset) PendingResetIds.Add(Fact.AgentId);
  TestTrue(TEXT("first pending reset consume succeeds"),
    FCrowdDemoOpenSpawnRelaxationKernel::ConsumePendingBoundaryResets(
      PendingResetIds, Runtime));
  TestFalse(TEXT("pending reset cannot be consumed twice"),
    FCrowdDemoOpenSpawnRelaxationKernel::ConsumePendingBoundaryResets(
      PendingResetIds, Runtime));

  TArray<FCrowdDemoPreparedOpenSpawnBoundaryFact> RestoredFacts;
  TestTrue(TEXT("restored runtime deterministically rebuilds facts"),
    FCrowdDemoOpenSpawnRelaxationKernel::BuildPreparedBoundaryFacts(
      0, AgentIds, Runtime, RestoredFacts));
  for (const auto& Fact : RestoredFacts)
    TestFalse(TEXT("consumed reset remains cleared"), Fact.bPendingBoundaryReset);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnStateTranslationTest,
  "CrowdDemo.SoftPressure.T1.GenericStateTranslation",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnStateTranslationTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdDemoOpenSpawnRelaxationAgentState Agent;
  Agent.AgentId = 100;
  Agent.FormationIndex = 0;
  ECrowdWorkerLifecyclePhase Lifecycle;
  ECrowdSemanticBehaviorState Behavior;
  const auto Translate = [&]()
  {
    return FCrowdDemoOpenSpawnStateTranslation::Translate(
      Agent.bParticleActive
        ? ECrowdDemoOpenSpawnRelaxationPhase::BatchActivation
        : ECrowdDemoOpenSpawnRelaxationPhase::Staging,
      Agent, 110, Lifecycle, Behavior);
  };
  TestTrue(TEXT("staged state translates"), Translate());
  TestEqual(TEXT("staged lifecycle is spawn pending"),
    static_cast<uint8>(Lifecycle),
    static_cast<uint8>(ECrowdWorkerLifecyclePhase::SpawnPending));
  TestEqual(TEXT("staged behavior is waiting"),
    static_cast<uint8>(Behavior),
    static_cast<uint8>(ECrowdSemanticBehaviorState::Waiting));

  const ECrowdWorkerLifecyclePhase PreviouslyActive =
    ECrowdWorkerLifecyclePhase::Active;
  TestTrue(TEXT("reused staged state translates"),
    FCrowdDemoOpenSpawnStateTranslation::Translate(
      ECrowdDemoOpenSpawnRelaxationPhase::Staging,
      Agent, 110, Lifecycle, Behavior, &PreviouslyActive));
  TestEqual(TEXT("reused staged lifecycle is suspended"),
    static_cast<uint8>(Lifecycle),
    static_cast<uint8>(ECrowdWorkerLifecyclePhase::Suspended));
  TestEqual(TEXT("reused staged behavior remains waiting"),
    static_cast<uint8>(Behavior),
    static_cast<uint8>(ECrowdSemanticBehaviorState::Waiting));

  Agent.bParticleActive = true;
  TestTrue(TEXT("active relaxation translates"), Translate());
  TestEqual(TEXT("active relaxation lifecycle is active"),
    static_cast<uint8>(Lifecycle),
    static_cast<uint8>(ECrowdWorkerLifecyclePhase::Active));
  TestEqual(TEXT("active relaxation behavior is relaxing"),
    static_cast<uint8>(Behavior),
    static_cast<uint8>(ECrowdSemanticBehaviorState::Relaxing));

  Agent.AgentId = 101;
  TestTrue(TEXT("post-removal settler translates"),
    FCrowdDemoOpenSpawnStateTranslation::Translate(
      ECrowdDemoOpenSpawnRelaxationPhase::PostRemovalSettle,
      Agent, 110, Lifecycle, Behavior));
  TestEqual(TEXT("post-removal settler behavior is settling"),
    static_cast<uint8>(Behavior),
    static_cast<uint8>(ECrowdSemanticBehaviorState::Settling));

  Agent.AgentId = 110;
  Agent.bParticleActive = false;
  TestTrue(TEXT("particle-removed agent translates"),
    FCrowdDemoOpenSpawnStateTranslation::Translate(
      ECrowdDemoOpenSpawnRelaxationPhase::PostRemovalSettle,
      Agent, 110, Lifecycle, Behavior));
  TestEqual(TEXT("particle removal does not destroy lifecycle"),
    static_cast<uint8>(Lifecycle),
    static_cast<uint8>(ECrowdWorkerLifecyclePhase::Active));
  TestEqual(TEXT("particle-removed behavior is waiting"),
    static_cast<uint8>(Behavior),
    static_cast<uint8>(ECrowdSemanticBehaviorState::Waiting));

  const FCrowdStableEntityRef EntityRef{1, 100, 1};
  FCrowdWorkerLifecycleTransition Transition;
  Transition.EntityRef = EntityRef;
  Transition.Revision = 1;
  Transition.TargetPhase = ECrowdWorkerLifecyclePhase::SpawnPending;
  FCrowdWorkerLifecycleState First;
  FCrowdWorkerLifecycleState Replay;
  TestTrue(TEXT("translated lifecycle applies"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      nullptr, Transition, 7, 99, First));
  TestTrue(TEXT("same translated lifecycle deterministically applies"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      nullptr, Transition, 7, 99, Replay));
  FCrowdWorkerPayload FirstPayload;
  FCrowdWorkerPayload ReplayPayload;
  TestTrue(TEXT("translated lifecycle encodes"),
    FCrowdWorkerLifecycleStateCodec::Encode(First, FirstPayload));
  TestTrue(TEXT("translated lifecycle replay encodes"),
    FCrowdWorkerLifecycleStateCodec::Encode(Replay, ReplayPayload));
  TestEqual(TEXT("same input has same lifecycle state hash"),
    FirstPayload.StableHash, ReplayPayload.StableHash);

  FCrowdWorkerLifecycleTransition Activate;
  Activate.EntityRef = EntityRef;
  Activate.ExpectedRevision = 1;
  Activate.Revision = 2;
  Activate.TargetPhase = ECrowdWorkerLifecyclePhase::Active;
  FCrowdWorkerLifecycleState Active;
  TestTrue(TEXT("fresh activation applies"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      &First, Activate, 8, 99, Active));
  FCrowdWorkerLifecycleState Rejected;
  TestFalse(TEXT("stale translated activation is rejected"),
    FCrowdWorkerLifecycleStateMachine::Apply(
      &Active, Activate, 9, 99, Rejected));

  FCrowdWorkerLifecycleState Rebased;
  TestTrue(TEXT("new authoritative snapshot rebases lifecycle"),
    FCrowdWorkerLifecycleStateMachine::RebaseInitialState(
      Active, 10, 100, Rebased));
  TestEqual(TEXT("rebase preserves lifecycle revision"),
    Rebased.Revision, Active.Revision);
  TestEqual(TEXT("rebase preserves lifecycle phase"),
    static_cast<uint8>(Rebased.Phase),
    static_cast<uint8>(Active.Phase));
  TestEqual(TEXT("rebase advances lifecycle source input"),
    Rebased.SourceInputSequence, uint64{10});
  TestEqual(TEXT("rebase adopts authoritative state hash"),
    Rebased.InitialStateHash, uint64{100});
  TestFalse(TEXT("stale lifecycle rebase is rejected"),
    FCrowdWorkerLifecycleStateMachine::RebaseInitialState(
      Rebased, 9, 101, Rejected));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoOpenSpawnBehaviorOrderingTest,
  "CrowdDemo.SoftPressure.T1.GenericBehaviorOrdering",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoOpenSpawnBehaviorOrderingTest::RunTest(
  const FString& Parameters)
{
  (void)Parameters;
  FCrowdBehaviorSourceRuntime Runtime;
  TestTrue(TEXT("behavior providers initialize"),
    Runtime.InitializeFromRegisteredProviders());
  const FCrowdStableEntityRef EntityRef{1, 100, 1};
  FCrowdCapabilityBinding Binding;
  Binding.ProfileKey = CrowdDemoBehaviorSchemas::FullProfile;
  TestTrue(TEXT("T1 behavior entity registers"),
    Runtime.RegisterEntity(EntityRef, Binding));
  FCrowdBehaviorEntityEvaluationContext Context;
  Context.EntityRef = EntityRef;
  Context.FixedStepIndex = 5;
  Context.Facing = FVector::ForwardVector;
  Context.RecalculateStableHash();
  TestTrue(TEXT("waiting context stages"),
    Runtime.SetEvaluationContext(Context));
  TestTrue(TEXT("waiting command queues"),
    Runtime.QueueCommand(MakeSemanticStateCommand(
      EntityRef, 5, 1, ECrowdBehaviorSourceCommandKind::Start,
      ECrowdSemanticBehaviorState::Waiting)));
  FCrowdBehaviorPreparedBoundary Waiting;
  TestTrue(TEXT("waiting behavior prepares"),
    Runtime.PrepareBoundary(5, Waiting));
  TestTrue(TEXT("waiting behavior commits"),
    Runtime.CommitPrepared(Waiting));
  TestTrue(TEXT("waiting command journal acknowledges"),
    Runtime.AcknowledgeWorkerInputCommands(1));
  TestTrue(TEXT("waiting context journal acknowledges"),
    Runtime.AcknowledgeWorkerInputContexts(1));
  TestTrue(TEXT("waiting binding journal acknowledges"),
    Runtime.AcknowledgeWorkerInputBindings(1));

  Context.FixedStepIndex = 6;
  Context.RecalculateStableHash();
  TestTrue(TEXT("relaxing context stages"),
    Runtime.SetEvaluationContext(Context));
  TestTrue(TEXT("ordered relaxing update queues"),
    Runtime.QueueCommand(MakeSemanticStateCommand(
      EntityRef, 6, 2, ECrowdBehaviorSourceCommandKind::Update,
      ECrowdSemanticBehaviorState::Relaxing)));
  FCrowdBehaviorPreparedBoundary Relaxing;
  TestTrue(TEXT("ordered relaxing update prepares"),
    Runtime.PrepareBoundary(6, Relaxing));
  TestTrue(TEXT("semantic marker produces no movement"),
    Relaxing.Entities[0].ResolvedChannels.DesiredVelocity.IsZero());
  TestEqual(TEXT("semantic marker produces no presentation output"),
    Relaxing.Entities[0].ResolvedChannels.Presentation.Num(), 0);
  TestTrue(TEXT("ordered relaxing update commits"),
    Runtime.CommitPrepared(Relaxing));

  Context.FixedStepIndex = 7;
  Context.RecalculateStableHash();
  TestTrue(TEXT("stale context stages"),
    Runtime.SetEvaluationContext(Context));
  TestTrue(TEXT("well-formed stale behavior update queues"),
    Runtime.QueueCommand(MakeSemanticStateCommand(
      EntityRef, 7, 2, ECrowdBehaviorSourceCommandKind::Update,
      ECrowdSemanticBehaviorState::Settling)));
  FCrowdBehaviorPreparedBoundary Stale;
  TestFalse(TEXT("stale behavior ordering is rejected"),
    Runtime.PrepareBoundary(7, Stale));
  return true;
}

#endif
