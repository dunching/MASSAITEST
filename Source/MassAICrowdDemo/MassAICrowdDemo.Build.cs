using UnrealBuildTool;

public class MassAICrowdDemo : ModuleRules
{
  public MassAICrowdDemo(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;

    PublicIncludePaths.Add(ModuleDirectory);

    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCommon",
      "MassEntity",
      "MassLOD",
      "MassMovement",
      "MassReplication",
      "MassSpawner",
      "NetCore"
    });

    PrivateDependencyModuleNames.AddRange(new[]
    {
      "MassAIBehavior",
      "MassNavigation",
      "MassReplication",
      "MassSimulation",
      "NavigationSystem"
    });
  }
}
