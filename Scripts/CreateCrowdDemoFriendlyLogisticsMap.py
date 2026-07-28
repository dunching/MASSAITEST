import unreal


MAP_PATH = "/Game/Maps/CrowdDemo_FriendlyLogisticsSmall"


def spawn_cube(label, location, scale):
    mesh = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, location)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    return actor


def spawn_marker(label, location):
    marker = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.TargetPoint, location)
    marker.set_actor_label(label)
    marker.set_editor_property("tags", [unreal.Name(label)])
    return marker


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not unreal.EditorAssetLibrary.delete_asset(MAP_PATH):
            raise RuntimeError("failed to replace {}".format(MAP_PATH))
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if not level_editor.new_level(MAP_PATH):
        raise RuntimeError("failed to create {}".format(MAP_PATH))
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode",
        unreal.load_class(
            None, "/Script/MassAICrowdDemo.MassAICrowdDemoGameMode"))

    # The product Mass fixture spawns its initial formation near Y=-3050.
    # Keep the full formation, source, and both sinks on one cooked surface.
    spawn_cube("LogisticsFloor", unreal.Vector(0.0, 0.0, -50.0),
               unreal.Vector(24.0, 80.0, 1.0))
    spawn_cube("SourceDepot", unreal.Vector(-650.0, 0.0, 125.0),
               unreal.Vector(2.2, 3.2, 2.5))
    spawn_cube("PrimarySink", unreal.Vector(650.0, 0.0, 125.0),
               unreal.Vector(2.2, 3.2, 2.5))
    spawn_cube("FallbackSink", unreal.Vector(650.0, -650.0, 100.0),
               unreal.Vector(1.6, 2.0, 2.0))
    for index in range(5):
        spawn_cube(
            "SourceCargo{:02d}".format(index + 1),
            unreal.Vector(-500.0, -260.0 + index * 125.0, 35.0),
            unreal.Vector(0.35, 0.35, 0.35))

    spawn_marker("FriendlyLogisticsSource",
                 unreal.Vector(-300.0, 0.0, 60.0))
    spawn_marker("FriendlyLogisticsSink",
                 unreal.Vector(300.0, 0.0, 60.0))
    spawn_marker("FriendlyLogisticsFallback",
                 unreal.Vector(300.0, -450.0, 60.0))

    nav_bounds = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 300.0))
    nav_bounds.set_actor_label("FriendlyLogisticsNavBounds")
    nav_bounds.set_actor_scale3d(unreal.Vector(20.0, 45.0, 5.0))

    light = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 1800.0),
        unreal.Rotator(roll=0.0, pitch=-55.0, yaw=25.0))
    light.set_actor_label("FriendlyLogisticsDirectionalLight")
    light.get_component_by_class(
        unreal.DirectionalLightComponent).set_editor_property(
            "mobility", unreal.ComponentMobility.MOVABLE)
    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1600.0))
    sky.set_actor_label("FriendlyLogisticsSkyLight")
    sky.get_component_by_class(
        unreal.SkyLightComponent).set_editor_property(
            "mobility", unreal.ComponentMobility.MOVABLE)

    camera_location = unreal.Vector(0.0, -2200.0, 1050.0)
    camera_rotation = unreal.Rotator(roll=0.0, pitch=-23.0, yaw=90.0)
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CameraActor, camera_location, camera_rotation)
    camera.set_actor_label("FriendlyLogisticsAcceptanceCamera")
    camera.set_editor_property(
        "tags", [unreal.Name("FriendlyLogisticsAcceptanceCamera")])
    start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, camera_location, camera_rotation)
    start.set_actor_label("PlayerStart")

    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("failed to save {}".format(MAP_PATH))
    unreal.log("CrowdDemoFriendlyLogisticsMapCreated path={}".format(MAP_PATH))


main()
