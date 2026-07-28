import math
import unreal


MAP_PATH = "/Game/Maps/CrowdDemo_NavSurfaceGraphVerticalSmall"


def spawn_cube(label, location, scale, rotation=unreal.Rotator()):
    mesh = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, location, rotation)
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    return actor


def spawn_marker(tag, location):
    marker = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.TargetPoint, location)
    marker.set_actor_label(tag)
    marker.set_editor_property("tags", [unreal.Name(tag)])
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
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(
        None, "/Script/MassAICrowdDemo.MassAICrowdDemoGameMode"))

    spawn_cube("Ground", unreal.Vector(0.0, 0.0, -50.0),
               unreal.Vector(50.0, 30.0, 1.0))
    spawn_cube("UpperBridge", unreal.Vector(0.0, 0.0, 550.0),
               unreal.Vector(24.0, 4.0, 1.0))
    ramp_run = 1400.0
    ramp_angle = math.degrees(math.atan2(600.0, ramp_run))
    ramp_scale_x = math.sqrt(600.0 * 600.0 + ramp_run * ramp_run) / 100.0
    ramp_center_z = 300.0 - math.cos(math.radians(ramp_angle)) * 25.0
    spawn_cube("RampWest", unreal.Vector(-1750.0, 0.0, ramp_center_z),
               unreal.Vector(ramp_scale_x, 4.0, 0.5),
               unreal.Rotator(roll=0.0, pitch=ramp_angle, yaw=0.0))
    spawn_cube("RampEast", unreal.Vector(1750.0, 0.0, ramp_center_z),
               unreal.Vector(ramp_scale_x, 4.0, 0.5),
               unreal.Rotator(roll=0.0, pitch=-ramp_angle, yaw=0.0))
    spawn_cube("NarrowBridge", unreal.Vector(0.0, -700.0, 350.0),
               unreal.Vector(14.0, 1.4, 0.6))
    narrow_run = 1200.0
    narrow_angle = math.degrees(math.atan2(400.0, narrow_run))
    narrow_scale_x = math.sqrt(400.0 * 400.0 + narrow_run * narrow_run) / 100.0
    narrow_center_z = 200.0 - math.cos(math.radians(narrow_angle)) * 22.5
    spawn_cube("NarrowRampWest", unreal.Vector(-1150.0, -700.0, narrow_center_z),
               unreal.Vector(narrow_scale_x, 1.4, 0.45),
               unreal.Rotator(roll=0.0, pitch=narrow_angle, yaw=0.0))
    spawn_cube("NarrowRampEast", unreal.Vector(1150.0, -700.0, narrow_center_z),
               unreal.Vector(narrow_scale_x, 1.4, 0.45),
               unreal.Rotator(roll=0.0, pitch=-narrow_angle, yaw=0.0))
    # A deterministic service stair keeps the high surface connected even where
    # Recast separates overlapping ramp/platform surfaces into distinct layers.
    for step in range(1, 21):
        height = step * 30.0
        y = 230.0 + (20 - step) * 60.0
        spawn_cube("ServiceStep{:02d}".format(step),
                   unreal.Vector(0.0, y, height * 0.5),
                   unreal.Vector(3.0, 0.7, height / 100.0))
    spawn_cube("UpperToNarrowConnector", unreal.Vector(0.0, -415.0, 550.0),
               unreal.Vector(6.0, 5.0, 1.0))
    spawn_cube("UnreachableDropTop", unreal.Vector(0.0, 1000.0, 850.0),
               unreal.Vector(5.0, 5.0, 1.0))
    spawn_cube("RouteDivider", unreal.Vector(500.0, 0.0, 100.0),
               unreal.Vector(4.0, 2.0, 2.0))

    nav_bounds = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 700.0))
    nav_bounds.set_actor_label("CrowdDemoNavBounds")
    nav_bounds.set_actor_scale3d(unreal.Vector(32.0, 22.0, 10.0))

    spawn_marker("CrowdNavLower", unreal.Vector(0.0, 0.0, 10.0))
    spawn_marker("CrowdNavUpper", unreal.Vector(0.0, 0.0, 610.0))
    spawn_marker("CrowdNavRamp", unreal.Vector(-1650.0, 0.0, 340.0))
    spawn_marker("CrowdNavHigh", unreal.Vector(700.0, 0.0, 610.0))
    spawn_marker("CrowdNavNarrow", unreal.Vector(0.0, -700.0, 420.0))
    spawn_marker("CrowdNavDropTop", unreal.Vector(0.0, 1000.0, 910.0))
    spawn_marker("CrowdNavDropBottom", unreal.Vector(0.0, 1000.0, 10.0))
    spawn_marker("CrowdNavRouteA", unreal.Vector(800.0, 650.0, 10.0))
    spawn_marker("CrowdNavRouteB", unreal.Vector(800.0, -1050.0, 10.0))
    spawn_marker("CrowdNavGoal", unreal.Vector(2200.0, 0.0, 10.0))

    light = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 2000.0),
        unreal.Rotator(roll=0.0, pitch=-55.0, yaw=35.0))
    light.set_actor_label("DirectionalLight")
    light.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property(
        "mobility", unreal.ComponentMobility.MOVABLE)
    sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0.0, 0.0, 1800.0))
    sky.set_actor_label("SkyLight")
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CameraActor, unreal.Vector(0.0, -5200.0, 3800.0),
        unreal.Rotator(roll=0.0, pitch=-35.0, yaw=90.0))
    camera.set_actor_label("CrowdDemoNavAcceptanceCamera")
    camera.set_editor_property(
        "tags", [unreal.Name("CrowdNavAcceptanceCamera")])
    start = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, camera.get_actor_location(), camera.get_actor_rotation())
    start.set_actor_label("PlayerStart")

    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    if not unreal.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError("failed to save {}".format(MAP_PATH))
    unreal.log("CrowdDemoNavSurfaceGraphMapCreated path={}".format(MAP_PATH))


main()
