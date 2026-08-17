using UnrealBuildTool;

public class MassCrowdSpatial : ModuleRules
{
  public MassCrowdSpatial(ReadOnlyTargetRules Target) : base(Target)
  {
    bUseUnity = false;
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdCore"
    });
  }
}
