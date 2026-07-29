using UnrealBuildTool;

public class MassCrowdCombat : ModuleRules
{
  public MassCrowdCombat(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "MassCrowdCore"
    });
  }
}
