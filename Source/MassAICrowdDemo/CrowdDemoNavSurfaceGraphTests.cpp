#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FCrowdDemoNavSurfaceGraphArchitectureTest,
  "CrowdDemo.NavSurfaceGraph.Architecture",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrowdDemoNavSurfaceGraphArchitectureTest::RunTest(const FString& Parameters)
{
  FString GameModeSource;
  FString ProbeSource;
  FString RunScript;
  TestTrue(TEXT("GameMode source is readable"), FFileHelper::LoadFileToString(
    GameModeSource,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/MassAICrowdDemo/CrowdDemoGameMode.cpp"))));
  TestTrue(TEXT("surface graph probe source is readable"), FFileHelper::LoadFileToString(
    ProbeSource,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/MassAICrowdDemo/CrowdDemoNavSurfaceGraphProbe.cpp"))));
  TestTrue(TEXT("demo runner is readable"), FFileHelper::LoadFileToString(
    RunScript,
    *FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/RunCrowdDemo.ps1"))));

  const int32 NavGraphBranch = GameModeSource.Find(TEXT("CrowdDemoNavSurfaceGraph"));
  const int32 FixedSpawn = GameModeSource.Find(TEXT("MassSubsystem->SpawnAgents"));
  TestTrue(TEXT("surface graph path branches before fixed Agent spawn"),
    NavGraphBranch != INDEX_NONE && FixedSpawn != INDEX_NONE
      && NavGraphBranch < FixedSpawn);
  TestTrue(TEXT("probe extracts the production static Recast graph"),
    ProbeSource.Contains(TEXT("ExtractStaticRecast")));
  TestTrue(TEXT("probe builds shared flow twice for dynamic target rebind"),
    ProbeSource.Contains(TEXT("GoalFlow"))
      && ProbeSource.Contains(TEXT("ReboundFlow")));
  TestTrue(TEXT("probe verifies the unreachable drop"),
    ProbeSource.Contains(TEXT("bDropTopUnreachable")));
  TestTrue(TEXT("runner exposes the real map surface graph switch"),
    RunScript.Contains(TEXT("[switch]$NavSurfaceGraph"))
      && RunScript.Contains(TEXT("-CrowdDemoNavSurfaceGraph")));
  TestTrue(TEXT("product-small path keeps Round spawn and validates Runtime flow resources"),
    GameModeSource.Contains(TEXT("CrowdDemoNavFlowProductSmall"))
      && ProbeSource.Contains(TEXT("GetFlowCacheMetrics"))
      && ProbeSource.Contains(TEXT("GetTrackedAgentCount() == 20")));
  TestTrue(TEXT("runner exposes and gates NavFlowProductSmall"),
    RunScript.Contains(TEXT("[switch]$NavFlowProductSmall"))
      && RunScript.Contains(TEXT("-CrowdDemoNavFlowProductSmall"))
      && RunScript.Contains(TEXT("NavFlowProductSmall gate passed")));
  return true;
}

#endif
