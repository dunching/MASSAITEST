using UnrealBuildTool;

public class MassCrowdBehaviorFixtureTests : ModuleRules
{
  public MassCrowdBehaviorFixtureTests(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PrivateDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdBehaviorFixture",
      "MassCrowdCore",
      "MassCrowdRuntime",
      "MassCrowdNetworking"
    });
  }
}
