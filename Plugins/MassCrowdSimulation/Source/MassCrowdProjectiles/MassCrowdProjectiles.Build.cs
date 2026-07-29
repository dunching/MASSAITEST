using UnrealBuildTool;

public class MassCrowdProjectiles : ModuleRules
{
  public MassCrowdProjectiles(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCrowdCore",
      "MassCrowdSpatial",
      "MassCrowdCombat",
      "MassCrowdRuntime",
      "MassCommon",
      "MassEntity",
      "MassSpawner"
    });
  }
}
