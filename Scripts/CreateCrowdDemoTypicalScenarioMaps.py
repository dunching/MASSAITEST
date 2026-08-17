import os
import unreal


MAPS = [
    ("CrowdDemo_SimRoundSoftPressureT1OpenSpawnRelaxationSmall",
     unreal.CrowdDemoSoftPressureTestCase.OPEN_SPAWN_RELAXATION),
    ("CrowdDemo_SimRoundSoftPressureT2OpenCohortMovementSmall",
     unreal.CrowdDemoSoftPressureTestCase.OPEN_COHORT_MOVEMENT),
    ("CrowdDemo_SimRoundSoftPressureT3OpenBidirectionalSwapSmall",
     unreal.CrowdDemoSoftPressureTestCase.BIDIRECTIONAL_SWAP),
    ("CrowdDemo_SimRoundSoftPressureT4ValidCorridorTransitSmall",
     unreal.CrowdDemoSoftPressureTestCase.VALID_CORRIDOR_TRANSIT),
    ("CrowdDemo_SimRoundSoftPressureT6HeterogeneousTransitSmall",
     unreal.CrowdDemoSoftPressureTestCase.HETEROGENEOUS_TRANSIT),
    ("CrowdDemo_SimRoundSoftPressureT6HeterogeneousTargetStaticSmall",
     unreal.CrowdDemoSoftPressureTestCase.HETEROGENEOUS_TARGET_STATIC),
    ("CrowdDemo_SimRoundSoftPressureT6HeterogeneousTargetMovingSmall",
     unreal.CrowdDemoSoftPressureTestCase.HETEROGENEOUS_TARGET_MOVING),
    ("CrowdDemo_MultiStateVatHitResponseSmall",
     unreal.CrowdDemoSoftPressureTestCase.MULTI_STATE_VAT_HIT_RESPONSE),
    ("CrowdDemo_RangedProjectileCombatSmall",
     unreal.CrowdDemoSoftPressureTestCase.RANGED_PROJECTILE_COMBAT),
]


def _spawn_lighting_preview_and_camera():
    cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    floor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, -900.0, -8.0))
    floor.set_actor_label("PreviewFloor")
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

    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, -900.0, 2500.0))
    sky.set_actor_label("SkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("intensity", 1.5)

    atmosphere = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0))
    atmosphere.set_actor_label("SkyAtmosphere")
    fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 0.0))
    fog.set_actor_label("ExponentialHeightFog")
    cloud = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.VolumetricCloud, unreal.Vector(0.0, 0.0, 0.0))
    cloud.set_actor_label("VolumetricCloud")

    sky_sphere = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
    sky_sphere.set_actor_label("SM_SkySphere")
    sphere_mesh = unreal.load_object(
        None, "/Engine/MapTemplates/Sky/SM_SkySphere.SM_SkySphere")
    if sphere_mesh:
        sky_sphere.get_editor_property(
            "static_mesh_component").set_static_mesh(sphere_mesh)
        sky_sphere.set_actor_scale3d(unreal.Vector(100.0, 100.0, 100.0))

    camera_location = unreal.Vector(0.0, -3600.0, 4200.0)
    camera_rotation = unreal.Rotator(-58.0, 90.0, 0.0)
    start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, camera_location, camera_rotation)
    start.set_actor_label("PlayerStart")
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CameraActor, camera_location, camera_rotation)
    camera.set_actor_label("CrowdDemoFixedOverheadCamera")


def _create_map(map_name, test_case):
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
    settings.set_editor_property("default_game_mode", unreal.load_class(
        None, "/Script/MassAICrowdDemo.MassAICrowdDemoGameMode"))
    config_class = unreal.load_class(
        None, "/Script/MassAICrowdDemo.CrowdDemoScenarioConfigActor")
    config = unreal.EditorLevelLibrary.spawn_actor_from_class(
        config_class, unreal.Vector(0.0, 0.0, 120.0))
    config.set_actor_label("CrowdDemoScenarioConfig")
    config.set_editor_property("scenario_override_value", 1)
    config.set_editor_property("entity_count_override", 20)
    config.set_editor_property("soft_pressure_test_case", test_case)
    _spawn_lighting_preview_and_camera()

    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("failed to save {}".format(package_path))
    unreal.log("CrowdDemoTypicalMapCreated path={} agents=20 test_case={}".format(
        package_path, str(test_case)))


def main():
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")
    selected = os.environ.get("CROWD_DEMO_TYPICAL_MAP_INDEX")
    rows = [MAPS[int(selected)]] if selected is not None else MAPS
    for map_name, test_case in rows:
        _create_map(map_name, test_case)


main()
