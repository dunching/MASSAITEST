#include "CrowdNavSurfaceGraph.h"
#include "Mass/CrowdDemoBehaviorAdapters.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
  FCrowdAgentFacts MakeMixedFacts(
    const uint64 StableId,
    const ECrowdActiveBehavior Initial,
    std::initializer_list<ECrowdCapability> Capabilities)
  {
    FCrowdAgentFacts Facts;
    Facts.StableEntityRef = {1, StableId, 1};
    Facts.FactionKey = static_cast<uint32>((StableId % 3) + 1);
    Facts.ActiveBehavior = Initial;
    Facts.MovementProfileKey = 1;
    for (const ECrowdCapability Capability : Capabilities)
      Facts.CapabilitySet.Add(Capability);
    return Facts;
  }

  FCrowdRuntimeBehaviorContext MakeMixedContext(
    const FCrowdAgentFacts& Facts,
    const ECrowdActiveBehavior Behavior,
    const int64 FixedStep,
    const bool bReady)
  {
    FCrowdRuntimeBehaviorContext Context;
    Context.AgentFacts = Facts;
    Context.RequestedBehavior = Behavior;
    Context.FixedStepIndex = FixedStep;
    Context.TransitionRevision = 1;
    Context.TaskRef = {2, Facts.StableEntityRef.StableEntityId, 1};
    Context.TargetRef = {1, 20, 1};
    Context.TargetLocation = FVector(200, 0, 100);
    Context.ObjectiveKey = 1;
    Context.MovementProfileKey = 1;
    Context.InteractionPayloadKey = 1;
    Context.InteractionQuantity = Behavior == ECrowdActiveBehavior::Attack ? 25 : 1;
    Context.bInteractionReady = bReady;
    return Context;
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

  FCrowdDemoBehaviorProviderSet Providers;
  FCrowdDemoBusinessCommitLedger Ledger;
  FCrowdAgentFacts Hauler = MakeMixedFacts(
    1, ECrowdActiveBehavior::HaulPickup,
    {ECrowdCapability::Move, ECrowdCapability::Haul, ECrowdCapability::UseNavLayer});
  FCrowdRuntimeBehaviorOutput Pickup;
  TestTrue(TEXT("pickup evaluates"), FCrowdRuntimeBehaviorTransition::Evaluate(
    Providers, MakeMixedContext(Hauler, ECrowdActiveBehavior::HaulPickup, 10, true), Pickup));
  TestEqual(TEXT("pickup applies"), Ledger.Apply(Pickup.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("pickup replay is idempotent"), Ledger.Apply(Pickup.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);

  FCrowdRuntimeBehaviorOutput Deliver;
  TestTrue(TEXT("deliver evaluates"), FCrowdRuntimeBehaviorTransition::Evaluate(
    Providers, MakeMixedContext(Hauler, ECrowdActiveBehavior::HaulDeliver, 20, true), Deliver));
  TestEqual(TEXT("delivery applies"), Ledger.Apply(Deliver.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);

  FCrowdAgentFacts Attacker = MakeMixedFacts(
    7, ECrowdActiveBehavior::Pursue,
    {ECrowdCapability::Move, ECrowdCapability::Pursue,
      ECrowdCapability::Attack, ECrowdCapability::UseNavLayer});
  FCrowdRuntimeBehaviorOutput Attack;
  TestTrue(TEXT("attack evaluates through same provider set"),
    FCrowdRuntimeBehaviorTransition::Evaluate(
      Providers, MakeMixedContext(Attacker, ECrowdActiveBehavior::Attack, 30, true), Attack));
  TestEqual(TEXT("combat applies"), Ledger.Apply(Attack.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Applied);
  TestEqual(TEXT("combat replay is idempotent"), Ledger.Apply(Attack.BusinessCommitRequest),
    ECrowdDemoBusinessCommitAcceptResult::Duplicate);
  TestEqual(TEXT("cargo pickup/delivery both observed"),
    Ledger.GetPickupCount() + Ledger.GetDeliveryCount(), 2);
  TestEqual(TEXT("combat quantity observed"), Ledger.GetCombatHitQuantity(20), 25);
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
  TestTrue(TEXT("mixed path evaluates unified behavior"),
    Coordinator.Contains(TEXT("FCrowdRuntimeBehaviorTransition::Evaluate")));
  TestTrue(TEXT("mixed path consumes Runtime-owned Nav resources"),
    Coordinator.Contains(TEXT("UMassCrowdRuntimeSubsystem"))
      && Coordinator.Contains(TEXT("GetNavGraphResource"))
      && Coordinator.Contains(TEXT("AcquireFlow")));
  TestFalse(TEXT("mixed path does not build a private Recast graph"),
    Coordinator.Contains(TEXT("ExtractStaticRecast"))
      || Coordinator.Contains(TEXT("FCrowdNavSurfaceGraphExtractor::BuildFlow")));
  TestTrue(TEXT("mixed path uses the product boundary runner"),
    Coordinator.Contains(TEXT("FCrowdMassBoundaryRunner Runner"))
      && Coordinator.Contains(TEXT("RunProductMovementBoundary"))
      && Coordinator.Contains(TEXT("BuildAndSealCommit")));
  TestFalse(TEXT("mixed path has no direct shared-flow movement owner"),
    Coordinator.Contains(TEXT("MoveAlongSharedFlow")));
  TestTrue(TEXT("mixed path drains public apply frames"),
    Coordinator.Contains(TEXT("DrainClientApplyFrames"))
      && Coordinator.Contains(TEXT("Frame.Corrections")));
  TestTrue(TEXT("mixed path delegates atomic presentation lifecycle"),
    Coordinator.Contains(TEXT("UMassCrowdPresentationSubsystem"))
      && Coordinator.Contains(TEXT("PrepareFrame"))
      && Coordinator.Contains(TEXT("ApplyPreparedFrame")));
  TestFalse(TEXT("mixed coordinator does not mutate ISM slots directly"),
    Coordinator.Contains(TEXT("->AddInstance("))
      || Coordinator.Contains(TEXT("->RemoveInstance("))
      || Coordinator.Contains(TEXT("->UpdateInstanceTransform(")));
  TestFalse(TEXT("mixed coordinator does not consume RoundPlan"),
    Coordinator.Contains(TEXT("RoundPlan")));
  TestTrue(TEXT("runner exposes and gates mixed path"),
    Runner.Contains(TEXT("[switch]$MixedSandbox"))
      && Runner.Contains(TEXT("PASS CrowdDemoMixedSandbox role=server"))
      && Runner.Contains(TEXT("PASS CrowdDemoMixedSandbox role=client")));
  return true;
}

#endif
