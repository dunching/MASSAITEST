#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FMassCrowdPluginBoundaryTest,
  "MassCrowd.Plugin.Boundary",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassCrowdPluginBoundaryTest::RunTest(const FString& Parameters)
{
  const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(
    TEXT("MassCrowdSimulation"));
  if (!TestTrue(TEXT("plugin is discoverable"), Plugin.IsValid())) return false;

  const FString SourceRoot = Plugin->GetBaseDir() / TEXT("Source");
  TArray<FString> Files;
  IFileManager::Get().FindFilesRecursive(Files, *SourceRoot, TEXT("*.*"), true, false);
  Files.Sort();

  const TArray<FString> PluginForbidden = {
    FString(TEXT("Crowd")) + TEXT("Demo"),
    FString(TEXT("/Game/")) + TEXT("Maps/"),
    FString(TEXT("Sim")) + TEXT("Round"),
    FString(TEXT("-")) + TEXT("port=")
  };
  const TArray<FString> CoreForbidden = {
    FString(TEXT("Mass")) + TEXT("Entity"),
    FString(TEXT("U")) + TEXT("World"),
    FString(TEXT("A")) + TEXT("Actor"),
    FString(TEXT("Mass")) + TEXT("Replication"),
    FString(TEXT("MassCrowd")) + TEXT("Runtime")
  };

  bool bValid = true;
  for (const FString& File : Files)
  {
    if (!File.EndsWith(TEXT(".h")) && !File.EndsWith(TEXT(".cpp"))
      && !File.EndsWith(TEXT(".cs"))) continue;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *File))
    {
      AddError(FString::Printf(TEXT("cannot read plugin source: %s"), *File));
      bValid = false;
      continue;
    }
    for (const FString& Token : PluginForbidden)
      if (Text.Contains(Token, ESearchCase::IgnoreCase))
      {
        AddError(FString::Printf(TEXT("plugin source contains forbidden token '%s': %s"),
          *Token, *File));
        bValid = false;
      }
    if (File.Contains(TEXT("MassCrowdCore")))
      for (const FString& Token : CoreForbidden)
        if (Text.Contains(Token, ESearchCase::CaseSensitive))
        {
          AddError(FString::Printf(TEXT("core source contains forbidden dependency '%s': %s"),
            *Token, *File));
          bValid = false;
        }
  }
  return bValid;
}

#endif
