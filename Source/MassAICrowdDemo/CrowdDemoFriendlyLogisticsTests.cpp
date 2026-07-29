#include "Misc/AutomationTest.h"
#include "CrowdDemoFriendlyLogisticsTestDirector.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoFriendlyLogisticsArchitectureTest,
  "CrowdDemo.FriendlyLogistics.Architecture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoFriendlyLogisticsArchitectureTest::RunTest(
  const FString& Parameters)
{
  FString Coordinator;
  FString GameMode;
  FString MassSubsystem;
  FString RunScript;
  TestTrue(TEXT("friendly coordinator source is readable"),
    FFileHelper::LoadFileToString(Coordinator,
      *FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Source/MassAICrowdDemo/CrowdDemoFriendlyLogisticsCoordinator.cpp"))));
  TestTrue(TEXT("GameMode source is readable"),
    FFileHelper::LoadFileToString(GameMode,
      *FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Source/MassAICrowdDemo/CrowdDemoGameMode.cpp"))));
  TestTrue(TEXT("Mass subsystem source is readable"),
    FFileHelper::LoadFileToString(MassSubsystem,
      *FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Source/MassAICrowdDemo/Mass/CrowdDemoMassSubsystem.cpp"))));
  TestTrue(TEXT("runner source is readable"),
    FFileHelper::LoadFileToString(RunScript,
      *FPaths::Combine(FPaths::ProjectDir(),
        TEXT("Scripts/RunCrowdDemo.ps1"))));
  TestTrue(TEXT("scene consumes public transaction store"),
    Coordinator.Contains(TEXT("FCrowdLogisticsTransactionStore"))
      && Coordinator.Contains(TEXT("FCrowdLogisticsKernel::ChooseCarrier")));
  TestTrue(TEXT("faults are selected by the test director"),
    Coordinator.Contains(
        TEXT("FCrowdDemoFriendlyLogisticsTestDirector::Evaluate"))
      && Coordinator.Contains(TEXT("death_requeue"))
      && Coordinator.Contains(TEXT("RetargetSink"))
      && Coordinator.Contains(TEXT("UnreachableBackoffCount")));
  TestTrue(TEXT("scene uses public late-join channel"),
    Coordinator.Contains(TEXT("PublishBaseline"))
      && Coordinator.Contains(TEXT("ECrowdReliableStateKind::Task"))
      && Coordinator.Contains(TEXT("DrainClientApplyFrames")));
  TestTrue(TEXT("scene movement uses product boundary and Nav flow"),
    Coordinator.Contains(TEXT("FCrowdMassBoundaryRunner Runner"))
      && Coordinator.Contains(TEXT("BuildOrRefreshNavGraph"))
      && Coordinator.Contains(TEXT("AcquireFlow"))
      && Coordinator.Contains(TEXT("ApplyProductBoundaryCommit")));
  TestTrue(TEXT("scene movement goal comes from resolved sources"),
    Coordinator.Contains(TEXT(
      "ResolvedChannels.MovementGoal.Location"))
      && Coordinator.Contains(TEXT(
        "EvaluateFriendlyLogistics"))
      && Coordinator.Contains(TEXT(
        "FCrowdDemoPlanningRuntimeHost::Stage")));
  TestFalse(TEXT("friendly coordinator no longer owns source diff wiring"),
    Coordinator.Contains(TEXT(
      "FCrowdDemoSourceSetDiff::BuildDesiredSourceDiff"))
      || Coordinator.Contains(TEXT("SetEvaluationContext(")));
  TestTrue(TEXT("client presentation consumes replicated corrections"),
    Coordinator.Contains(TEXT("ClientLocations.Find(EntityRef)"))
      && Coordinator.Contains(TEXT(
        "Frame.Corrections")));
  TestTrue(TEXT("entry fixes population at 20 and skips Round coordinator"),
    GameMode.Contains(TEXT("bFriendlyLogisticsSmall"))
      && GameMode.Contains(TEXT("fixed_mass_agents=20 round_coordinator=0")));
  TestTrue(TEXT("runner exposes and gates friendly logistics"),
    RunScript.Contains(TEXT("[switch]$FriendlyLogisticsSmall"))
      && RunScript.Contains(TEXT("FriendlyLogisticsSmall gate passed")));
  TestTrue(TEXT("dedicated product map exists"),
    FPaths::FileExists(*FPaths::Combine(
      FPaths::ProjectContentDir(),
      TEXT("Maps/CrowdDemo_FriendlyLogisticsSmall.umap"))));
  TestTrue(TEXT("cargo facts drive public presentation"),
    Coordinator.Contains(TEXT("UMassCrowdPresentationSubsystem"))
      && Coordinator.Contains(TEXT("State.CargoRef"))
      && Coordinator.Contains(TEXT("cargo_attach=%d"))
      && Coordinator.Contains(TEXT("presentation_instances=20")));
  TestTrue(TEXT("presentation is committed as one prepared frame"),
    Coordinator.Contains(TEXT("PrepareFrame("))
      && Coordinator.Contains(TEXT("ApplyPreparedFrame(")));
  TestTrue(TEXT("real lifecycle recycles a Mass entity"),
    Coordinator.Contains(TEXT("RecycleTrackedAgent("))
      && MassSubsystem.Contains(TEXT("EntityManager.DestroyEntity"))
      && MassSubsystem.Contains(TEXT("LifecycleSerial + 1")));
  TestTrue(TEXT("dedicated path has one presentation owner"),
    MassSubsystem.Contains(
      TEXT("bPublicPresentationOwnsClient"))
      && MassSubsystem.Contains(
        TEXT("CrowdDemoFriendlyLogisticsSmall")));
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoFriendlyLogisticsDirectorTest,
  "CrowdDemo.FriendlyLogistics.TestDirector",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoFriendlyLogisticsDirectorTest::RunTest(
  const FString& Parameters)
{
  FCrowdDemoFriendlyDirectorInput Input;
  Input.TaskState = ECrowdLogisticsTaskState::Created;
  Input.bTransitionDelayElapsed = true;
  TestEqual(TEXT("first miss backs off"),
    FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(Input).Action,
    ECrowdDemoFriendlyDirectorAction::IncrementBackoff);
  Input.UnreachableBackoffCount = 2;
  TestEqual(TEXT("competition follows backoff"),
    FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(Input).Action,
    ECrowdDemoFriendlyDirectorAction::ClaimCompetitionWinner);
  Input.TaskState = ECrowdLogisticsTaskState::Picked;
  Input.bDeathInjected = false;
  Input.PickupObservedFixedStep = 10;
  Input.FixedStepIndex = 55;
  TestEqual(TEXT("death requeue is deterministic"),
    FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(Input).Action,
    ECrowdDemoFriendlyDirectorAction::RequeueDeadCarrier);
  Input.TaskState = ECrowdLogisticsTaskState::Requeued;
  Input.bFallbackApplied = false;
  TestEqual(TEXT("fallback precedes recovery"),
    FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(Input).Action,
    ECrowdDemoFriendlyDirectorAction::ApplyFallbackSink);
  Input.bFallbackApplied = true;
  TestEqual(TEXT("recovery claim follows fallback"),
    FCrowdDemoFriendlyLogisticsTestDirector::Evaluate(Input).Action,
    ECrowdDemoFriendlyDirectorAction::ClaimRecoveryCarrier);
  return true;
}

#endif
