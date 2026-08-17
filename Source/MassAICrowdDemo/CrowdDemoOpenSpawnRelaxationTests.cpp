#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Mass/CrowdDemoOpenSpawnRelaxationKernel.h"

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

#endif
