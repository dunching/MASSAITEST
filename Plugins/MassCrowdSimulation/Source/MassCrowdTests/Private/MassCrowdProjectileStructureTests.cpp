#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdProjectileModuleStructureTest,
  "MassCrowd.Projectiles.ModuleStructure",
  EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::EngineFilter)

bool FMassCrowdProjectileModuleStructureTest::RunTest(
  const FString& Parameters)
{
  const TSharedPtr<IPlugin> Plugin =
    IPluginManager::Get().FindPlugin(TEXT("MassCrowdSimulation"));
  if (!TestTrue(TEXT("plugin found"), Plugin.IsValid()))
    return false;
  const FString SourceRoot = Plugin->GetBaseDir() / TEXT("Source");
  FString RuntimeBuild;
  FString ProjectilesBuild;
  TestTrue(TEXT("runtime build read"),
    FFileHelper::LoadFileToString(
      RuntimeBuild,
      *(SourceRoot / TEXT(
        "MassCrowdRuntime/MassCrowdRuntime.Build.cs"))));
  TestTrue(TEXT("projectiles build read"),
    FFileHelper::LoadFileToString(
      ProjectilesBuild,
      *(SourceRoot / TEXT(
        "MassCrowdProjectiles/MassCrowdProjectiles.Build.cs"))));
  TestFalse(TEXT("Runtime never depends on Projectiles"),
    RuntimeBuild.Contains(TEXT("\"MassCrowdProjectiles\"")));
  TestTrue(TEXT("Projectiles depends on Spatial"),
    ProjectilesBuild.Contains(TEXT("\"MassCrowdSpatial\"")));
  TestTrue(TEXT("Projectiles depends on Combat"),
    ProjectilesBuild.Contains(TEXT("\"MassCrowdCombat\"")));

  TArray<FString> PluginFiles;
  static const TCHAR* PublicModuleNames[] = {
    TEXT("MassCrowdSpatial"),
    TEXT("MassCrowdCombat"),
    TEXT("MassCrowdProjectiles")
  };
  for (const TCHAR* ModuleName : PublicModuleNames)
  {
    IFileManager::Get().FindFilesRecursive(
      PluginFiles,
      *(SourceRoot / ModuleName),
      TEXT("*.*"),
      true,
      false);
  }
  bool bNoDemoDependency = true;
  for (const FString& File : PluginFiles)
  {
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *File)
      || Text.Contains(TEXT("Crowd") TEXT("Demo")))
    {
      bNoDemoDependency = false;
      AddError(FString::Printf(
        TEXT("public projectile module has host dependency: %s"),
        *File));
    }
  }
  TestTrue(TEXT("public modules contain no Demo dependency"),
    bNoDemoDependency);
  return bNoDemoDependency;
}

#endif
