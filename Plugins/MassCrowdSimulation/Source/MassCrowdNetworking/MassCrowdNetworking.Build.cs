using UnrealBuildTool;

public class MassCrowdNetworking : ModuleRules
{
  public MassCrowdNetworking(ReadOnlyTargetRules Target) : base(Target)
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
      "NetCore"
    });
  }
}
