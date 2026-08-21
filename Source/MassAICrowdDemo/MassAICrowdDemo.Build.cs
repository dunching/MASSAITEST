using UnrealBuildTool;

public class MassAICrowdDemo : ModuleRules
{
  public MassAICrowdDemo(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    CppStandard = CppStandardVersion.Cpp20;
    // This module contains many legacy translation-unit-local helpers with the
    // same names. Unity amalgamation merges their anonymous namespaces and can
    // produce false redefinition/overload errors on an otherwise valid clean
    // build, so keep each source file as its intended translation unit.
    bUseUnity = false;

    PublicIncludePaths.Add(ModuleDirectory);

    PublicDependencyModuleNames.AddRange(new[]
    {
      "Core",
      "CoreUObject",
      "Engine",
      "MassCore",
      "MassCommon",
      "MassEntity",
      "MassLOD",
      "MassMovement",
      "MassCrowdCore",
      "MassCrowdSpatial",
      "MassCrowdCombat",
      "MassCrowdProjectiles",
      "MassCrowdNetworking",
      "MassCrowdPresentation",
      "MassCrowdStandardSources",
      "MassCrowdDemoBusiness",
      "MassReplication",
      "MassSpawner",
      "NetCore"
    });

    PrivateDependencyModuleNames.AddRange(new[]
    {
      "MassAIBehavior",
      "MassCrowdRuntime",
      "MassNavigation",
      "MassReplication",
      "MassSimulation",
      "NavigationSystem",
      "RenderCore",
      "RHI"
    });

    if (Target.bBuildEditor)
    {
      PrivateDependencyModuleNames.AddRange(new[]
      {
        "LevelEditor",
        "UnrealEd"
      });
    }
  }
}
