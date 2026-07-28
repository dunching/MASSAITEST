#include "Misc/AutomationTest.h"
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
  TestTrue(TEXT("faults are driven by observed task state"),
    Coordinator.Contains(TEXT("Store.GetTask().State"))
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

#endif
