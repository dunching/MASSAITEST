using UnrealBuildTool;

public class MassCrowdStateTreeAdapter : ModuleRules
{
  public MassCrowdStateTreeAdapter(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCrowdCore",
      "MassCrowdRuntime",
      "StateTreeModule",
      "GameplayStateTreeModule"
    });
  }
}
