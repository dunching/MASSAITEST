import unreal


MAPS = [
    ("CrowdDemo_SimRoundCrowdTrafficSmall", 20),
    ("CrowdDemo_SimRoundCrowdTrafficMedium", 100),
    ("CrowdDemo_SimRoundCrowdTrafficCohort", 500),
    ("CrowdDemo_SimRoundCrowdTrafficCrossing", 200),
]


def _spawn_preview_scene():
    cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    floor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, -900.0, -8.0))
    floor.set_actor_label("CrowdDemoPreviewFloor")
    floor.set_actor_scale3d(unreal.Vector(100.0, 100.0, 0.08))
    floor.get_editor_property("static_mesh_component").set_static_mesh(cube)

    directional = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(-1200.0, -2200.0, 3200.0),
        unreal.Rotator(-55.0, 35.0, 0.0))
    directional.set_actor_label("CrowdDemoPreviewDirectionalLight")
    directional_component = directional.get_component_by_class(unreal.DirectionalLightComponent)
    directional_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    directional_component.set_editor_property("intensity", 1000.0)

    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, -900.0, 2500.0))
    sky.set_actor_label("CrowdDemoPreviewSkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("intensity", 1.5)

    start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0.0, -3600.0, 4200.0),
        unreal.Rotator(-58.0, 90.0, 0.0))
    start.set_actor_label("CrowdDemoPreviewPlayerStart")


def _create_map(map_name, entity_count):
    package_path = "/Game/Maps/{}".format(map_name)
    if unreal.EditorAssetLibrary.does_asset_exist(package_path):
        if not unreal.EditorAssetLibrary.delete_asset(package_path):
            raise RuntimeError("failed to delete {}".format(package_path))

    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not level_editor.new_level(package_path):
        raise RuntimeError("failed to create {}".format(package_path))

    world = editor.get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property(
        "default_game_mode",
        unreal.load_class(None, "/Script/MassAICrowdDemo.MassAICrowdDemoGameMode"))
    settings.set_editor_property("force_no_precomputed_lighting", True)

    config_class = unreal.load_class(
        None, "/Script/MassAICrowdDemo.CrowdDemoScenarioConfigActor")
    config = unreal.EditorLevelLibrary.spawn_actor_from_class(
        config_class, unreal.Vector(0.0, 0.0, 120.0))
    config.set_actor_label("CrowdDemoScenarioConfig")
    config.set_editor_property("scenario_override_value", 2)
    config.set_editor_property("entity_count_override", entity_count)
    _spawn_preview_scene()

    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("failed to save {}".format(package_path))
    unreal.log("CrowdDemoSf3MapCreated path={} agents={}".format(
        package_path, entity_count))


def main():
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")
    for map_name, entity_count in MAPS:
        _create_map(map_name, entity_count)


main()
