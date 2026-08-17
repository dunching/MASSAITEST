using UnrealBuildTool;

public class MassCrowdCore : ModuleRules
{
  public MassCrowdCore(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    PublicDependencyModuleNames.Add("Core");
  }
}
