using UnrealBuildTool;

public class MassCrowdRuntime : ModuleRules
{
  public MassCrowdRuntime(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCrowdCore",
      "MassEntity",
      "MassSpawner",
      "NavigationSystem"
    });
  }
}
