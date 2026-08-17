#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoContinuousLifecycleArchitectureTest,
  "CrowdDemo.ContinuousLifecycle.Architecture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoContinuousLifecycleArchitectureTest::RunTest(const FString& Parameters)
{
  FString GameModeSource;
  FString CoordinatorHeader;
  FString CoordinatorSource;
  TestTrue(TEXT("GameMode source is readable"), FFileHelper::LoadFileToString(
    GameModeSource,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/MassAICrowdDemo/CrowdDemoGameMode.cpp"))));
  TestTrue(TEXT("continuous coordinator header is readable"), FFileHelper::LoadFileToString(
    CoordinatorHeader,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/MassAICrowdDemo/CrowdDemoContinuousLifecycleCoordinator.h"))));
  TestTrue(TEXT("continuous coordinator source is readable"), FFileHelper::LoadFileToString(
    CoordinatorSource,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/MassAICrowdDemo/CrowdDemoContinuousLifecycleCoordinator.cpp"))));

  const int32 ContinuousBranch = GameModeSource.Find(TEXT("CrowdDemoContinuousLifecycle"));
  const int32 FixedSpawn = GameModeSource.Find(TEXT("MassSubsystem->SpawnAgents"));
  TestTrue(TEXT("continuous path branches before fixed Agent spawn"),
    ContinuousBranch != INDEX_NONE && FixedSpawn != INDEX_NONE && ContinuousBranch < FixedSpawn);
  TestFalse(TEXT("continuous path has no host lifecycle multicast"),
    CoordinatorHeader.Contains(TEXT("UFUNCTION(NetMulticast, Reliable)")));
  TestTrue(TEXT("continuous path uses the product replication channel"),
    CoordinatorSource.Contains(TEXT("AMassCrowdReplicationActor"))
      && CoordinatorSource.Contains(TEXT("PublishBaseline"))
      && CoordinatorSource.Contains(TEXT("PublishReliable")));
  TestTrue(TEXT("continuous path applies production Spawn batch"),
    CoordinatorSource.Contains(TEXT("FCrowdSpawnBatch")));
  TestTrue(TEXT("continuous path applies production Despawn batch"),
    CoordinatorSource.Contains(TEXT("FCrowdDespawnBatch")));
  TestTrue(TEXT("continuous path applies production Membership batch"),
    CoordinatorSource.Contains(TEXT("FCrowdMembershipBatch")));
  TestTrue(TEXT("continuous path delegates instance lifecycle to Presentation"),
    CoordinatorSource.Contains(TEXT("UMassCrowdPresentationSubsystem"))
      && CoordinatorSource.Contains(TEXT("ApplySpawn"))
      && CoordinatorSource.Contains(TEXT("ApplyDespawn"))
      && CoordinatorSource.Contains(TEXT("ApplyUpdate")));
  TestFalse(TEXT("continuous path does not directly mutate ISM slots"),
    CoordinatorSource.Contains(TEXT("->AddInstance("))
      || CoordinatorSource.Contains(TEXT("->RemoveInstance("))
      || CoordinatorSource.Contains(TEXT("->UpdateInstanceTransform(")));
  TestFalse(TEXT("lifecycle operations do not clear and rebuild all visuals"),
    CoordinatorSource.Contains(TEXT("void ACrowdDemoContinuousLifecycleCoordinator::RebuildClientVisuals")));
  TestFalse(TEXT("T1 particle-active flag is not a lifecycle implementation"),
    CoordinatorSource.Contains(TEXT("bParticleActive")));
  return true;
}

#endif
