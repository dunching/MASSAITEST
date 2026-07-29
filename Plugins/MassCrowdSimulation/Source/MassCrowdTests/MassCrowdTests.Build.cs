using UnrealBuildTool;

public class MassCrowdTests : ModuleRules
{
  public MassCrowdTests(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PrivateDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCrowdCore",
      "MassCrowdSpatial",
      "MassCrowdCombat",
      "MassCrowdProjectiles",
      "MassCrowdRuntime",
      "MassCrowdStandardSources",
      "MassCrowdNetworking",
      "MassCrowdPresentation",
      "MassEntity",
      "Projects"
    });
  }
}
