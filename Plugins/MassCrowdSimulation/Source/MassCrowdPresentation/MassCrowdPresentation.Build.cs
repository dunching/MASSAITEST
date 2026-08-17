using UnrealBuildTool;

public class MassCrowdPresentation : ModuleRules
{
  public MassCrowdPresentation(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdCore",
      "MassCrowdRuntime"
    });
    PrivateDependencyModuleNames.AddRange(new[]
    {
      "CoreUObject",
      "Engine",
      "RenderCore",
      "RHI"
    });
  }
}
