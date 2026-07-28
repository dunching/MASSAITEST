#include "CrowdNavSurfaceGraph.h"
#include "Mass/CrowdDemoBehaviorAdapters.h"
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
    Binding.ProfileKey = CrowdBuiltinBehaviorSchemas::LegacyFullProfile;
    return Binding;
  }

  bool QueueRecipe(
    FCrowdBehaviorSourceRuntime& Runtime,
    const FCrowdStableEntityRef EntityRef,
    const ECrowdActiveBehavior Behavior,
    const int64 FixedStep,
    uint32& CommandSequence,
    uint32& SourceSequence)
  {
    FCrowdRuntimeBehaviorContext Context;
    Context.AgentFacts.StableEntityRef = EntityRef;
    Context.RequestedBehavior = Behavior;
    Context.FixedStepIndex = FixedStep;
    Context.TransitionRevision = 1;
    Context.TaskRef = {2, EntityRef.StableEntityId, 1};
    Context.TargetRef = {1, 20, 1};
    Context.TargetLocation = FVector(200, 0, 100);
    Context.ObjectiveKey = 1;
    Context.MovementProfileKey = 1;
    Context.InteractionPayloadKey = 1;
    Context.InteractionQuantity = Behavior == ECrowdActiveBehavior::Attack ? 25 : 1;
    Context.bInteractionReady = true;
    const ECrowdBusinessCommitKind CommitKind =
      Behavior == ECrowdActiveBehavior::Attack
        ? ECrowdBusinessCommitKind::CombatHit
        : Behavior == ECrowdActiveBehavior::HaulPickup
          ? ECrowdBusinessCommitKind::CargoPickup
          : ECrowdBusinessCommitKind::CargoDeliver;
    Context.ExternalCommitId =
      FCrowdBehaviorCommitId::Make(CommitKind, Context);
    TArray<FCrowdBehaviorSourceCommand> Commands;
    const FCrowdBehaviorSourceSet* Set = Runtime.FindSourceSet(EntityRef);
    if (!Set || !FCrowdLegacyBehaviorRecipe::BuildTransitionCommands(
      Context, *Set, {1}, CommandSequence, SourceSequence, Commands))
      return false;
    for (const FCrowdBehaviorSourceCommand& Command : Commands)
    {
      if (!Runtime.QueueCommand(Command)) return false;
    }
    return true;
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
  TestTrue(TEXT("source runtime initializes"), Runtime.InitializeBuiltins());
  const FCrowdStableEntityRef Hauler{1, 1, 1};
  const FCrowdStableEntityRef Attacker{1, 7, 1};
  TestTrue(TEXT("hauler registers"),
    Runtime.RegisterEntity(Hauler, MakeMixedBinding()));
  TestTrue(TEXT("attacker registers"),
    Runtime.RegisterEntity(Attacker, MakeMixedBinding()));
  uint32 HaulerCommandSequence = 1;
  uint32 HaulerSourceSequence = 1;
  uint32 AttackerCommandSequence = 1;
  uint32 AttackerSourceSequence = 1;
  TestTrue(TEXT("pickup expands to sources"), QueueRecipe(
    Runtime, Hauler, ECrowdActiveBehavior::HaulPickup, 10,
    HaulerCommandSequence, HaulerSourceSequence));
  TestTrue(TEXT("attack expands to sources"), QueueRecipe(
    Runtime, Attacker, ECrowdActiveBehavior::Attack, 10,
    AttackerCommandSequence, AttackerSourceSequence));
  FCrowdBehaviorPreparedBoundary Prepared;
  TestTrue(TEXT("composed boundary prepares"),
    Runtime.PrepareBoundary(10, Prepared));
  TestEqual(TEXT("both entities resolve"), Prepared.Entities.Num(), 2);
  TestTrue(TEXT("pickup produces business output"),
    Prepared.Entities[0].ResolvedChannels.Business.Num() == 1);
  TestTrue(TEXT("attack produces business output"),
    Prepared.Entities[1].ResolvedChannels.Business.Num() == 1);
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
    Coordinator.Contains(TEXT("FCrowdLegacyBehaviorRecipe::BuildTransitionCommands"))
      && Coordinator.Contains(TEXT("BehaviorSourceRuntime->PrepareBoundary"))
      && Coordinator.Contains(TEXT("ApplyPreparedBehaviorBusiness")));
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
