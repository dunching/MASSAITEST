import os
import unreal


MAPS = [
    ("CrowdDemo_SimRoundSoftPressureTargetStaticSmall", 20,
     unreal.CrowdDemoSoftPressureTestCase.PURSUIT_AND_SETTLE),
    ("CrowdDemo_SimRoundSoftPressureTargetMovingSmall", 20,
     unreal.CrowdDemoSoftPressureTestCase.PURSUIT_AND_SETTLE_MOVING),
]


def _spawn_lighting_and_preview():
    cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    floor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, -900.0, -8.0))
    floor.set_actor_label("CrowdDemoPreviewFloor")
    floor.set_actor_scale3d(unreal.Vector(100.0, 100.0, 0.08))
    floor.get_editor_property("static_mesh_component").set_static_mesh(cube)

    directional = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(-1200.0, -2200.0, 3200.0),
        unreal.Rotator(-55.0, 35.0, 0.0))
    directional.set_actor_label("DirectionalLight")
    directional_component = directional.get_component_by_class(
        unreal.DirectionalLightComponent)
    directional_component.set_editor_property(
        "mobility", unreal.ComponentMobility.MOVABLE)
    directional_component.set_editor_property("intensity", 1000.0)

    fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 0.0))
    fog.set_actor_label("ExponentialHeightFog")
    atmosphere = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    atmosphere.set_actor_label("SkyAtmosphere")

    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, -900.0, 2500.0))
    sky.set_actor_label("SkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("intensity", 1.5)

    sky_sphere = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
    sky_sphere.set_actor_label("SM_SkySphere")
    sphere_mesh = unreal.load_object(
        None, "/Engine/MapTemplates/Sky/SM_SkySphere.SM_SkySphere")
    if sphere_mesh:
        sky_sphere.get_editor_property(
            "static_mesh_component").set_static_mesh(sphere_mesh)
        sky_sphere.set_actor_scale3d(unreal.Vector(100.0, 100.0, 100.0))

    cloud = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.VolumetricCloud, unreal.Vector(0.0, 0.0, 0.0))
    cloud.set_actor_label("VolumetricCloud")

    start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(0.0, -3600.0, 4200.0),
        unreal.Rotator(-58.0, 90.0, 0.0))
    start.set_actor_label("CrowdDemoPreviewPlayerStart")


def _create_map(map_name, entity_count, test_case):
    package_path = "/Game/Maps/{}".format(map_name)
    if unreal.EditorAssetLibrary.does_asset_exist(package_path):
        if not unreal.EditorAssetLibrary.delete_asset(package_path):
            raise RuntimeError("failed to delete {}".format(package_path))
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("failed to create blank world for {}".format(package_path))
    level_editor.get_current_level().set_editor_property(
        "use_external_actors", False)
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(
        None, "/Script/MassAICrowdDemo.MassAICrowdDemoGameMode"))
    config_class = unreal.load_class(
        None, "/Script/MassAICrowdDemo.CrowdDemoScenarioConfigActor")
    config = unreal.EditorLevelLibrary.spawn_actor_from_class(
        config_class, unreal.Vector(0.0, 0.0, 120.0))
    config.set_actor_label("CrowdDemoScenarioConfig")
    config.set_editor_property("scenario_override_value", 1)
    config.set_editor_property("entity_count_override", entity_count)
    config.set_editor_property("soft_pressure_test_case", test_case)
    _spawn_lighting_and_preview()
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, package_path):
        raise RuntimeError("failed to save {}".format(package_path))
    unreal.log("CrowdDemoTargetApproachMapCreated path={} agents={} test_case={}".format(
        package_path, entity_count, str(test_case)))


def main():
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")
    selected = os.environ.get("CROWD_DEMO_TARGET_APPROACH_MAP_INDEX")
    rows = [MAPS[int(selected)]] if selected is not None else MAPS
    for map_name, entity_count, test_case in rows:
        _create_map(map_name, entity_count, test_case)


main()
