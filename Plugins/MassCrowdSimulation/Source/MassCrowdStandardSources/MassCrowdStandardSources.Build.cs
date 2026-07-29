using UnrealBuildTool;

public class MassCrowdStandardSources : ModuleRules
{
  public MassCrowdStandardSources(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdCore",
      "MassCrowdRuntime"
    });
  }
}
