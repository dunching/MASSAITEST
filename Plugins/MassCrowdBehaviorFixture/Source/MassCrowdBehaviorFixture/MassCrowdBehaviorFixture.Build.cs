using UnrealBuildTool;

public class MassCrowdBehaviorFixture : ModuleRules
{
  public MassCrowdBehaviorFixture(ReadOnlyTargetRules Target) : base(Target)
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
