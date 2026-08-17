using UnrealBuildTool;

public class MassCrowdDemoBusiness : ModuleRules
{
  public MassCrowdDemoBusiness(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;

    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdCore",
      "MassCrowdRuntime",
      "MassCrowdStandardSources"
    });
  }
}
