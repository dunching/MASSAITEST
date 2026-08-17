"""Generate a new, self-contained insect rig and five animation clips in Blender.

This script deliberately does not read assets from SuperInvincibleTank_BugFix.
Run with:
  blender.exe --background --python Scripts/BuildCrowdDemoVatSource.py -- --output-dir <dir>
"""

import argparse
import json
import math
import os
import sys

import bpy
from mathutils import Vector


FPS = 30
FIRST_FRAME = 0
LAST_FRAME = 24
CLIPS = ("Idle", "Move", "Attack", "HitReact", "Death")


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    return parser.parse_args(argv)


def clear_scene():
    bpy.ops.object.mode_set(mode="OBJECT") if bpy.context.object and bpy.context.object.mode != "OBJECT" else None
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in (bpy.data.actions, bpy.data.armatures, bpy.data.meshes, bpy.data.materials):
        for item in list(collection):
            collection.remove(item)


def create_armature():
    armature_data = bpy.data.armatures.new("CrowdDemoBug_Skeleton")
    armature = bpy.data.objects.new("Armature", armature_data)
    bpy.context.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")

    bones = {
        "root": ((0.0, 0.0, 0.0), (0.0, 0.0, 0.35), None),
        "thorax": ((0.0, 0.0, 0.25), (0.0, 0.8, 0.25), "root"),
        "head": ((0.0, 0.65, 0.28), (0.0, 1.25, 0.34), "thorax"),
        "abdomen": ((0.0, -0.2, 0.25), (0.0, -1.15, 0.22), "thorax"),
        "wing_l": ((0.15, 0.15, 0.45), (0.95, -0.25, 0.55), "thorax"),
        "wing_r": ((-0.15, 0.15, 0.45), (-0.95, -0.25, 0.55), "thorax"),
        "leg_fl": ((0.2, 0.45, 0.18), (0.85, 0.75, -0.15), "thorax"),
        "leg_fr": ((-0.2, 0.45, 0.18), (-0.85, 0.75, -0.15), "thorax"),
        "leg_ml": ((0.22, 0.05, 0.15), (0.95, 0.0, -0.2), "thorax"),
        "leg_mr": ((-0.22, 0.05, 0.15), (-0.95, 0.0, -0.2), "thorax"),
        "leg_bl": ((0.2, -0.35, 0.15), (0.85, -0.75, -0.15), "thorax"),
        "leg_br": ((-0.2, -0.35, 0.15), (-0.85, -0.75, -0.15), "thorax"),
    }
    for name, (head, tail, parent_name) in bones.items():
        bone = armature_data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        if parent_name:
            bone.parent = armature_data.edit_bones[parent_name]
    bpy.ops.object.mode_set(mode="OBJECT")
    armature.show_in_front = True
    return armature


def assign_all_vertices(obj, bone_name):
    group = obj.vertex_groups.new(name=bone_name)
    group.add(range(len(obj.data.vertices)), 1.0, "REPLACE")


def add_ellipsoid(name, location, scale, bone_name, subdivisions=1):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    assign_all_vertices(obj, bone_name)
    return obj


def add_limb(name, start, end, radius, bone_name):
    start_v = Vector(start)
    end_v = Vector(end)
    direction = end_v - start_v
    midpoint = (start_v + end_v) * 0.5
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=radius, depth=direction.length, location=midpoint)
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = Vector((0.0, 0.0, 1.0)).rotation_difference(direction.normalized())
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    assign_all_vertices(obj, bone_name)
    return obj


def add_wing(name, side, bone_name):
    x0 = 0.12 * side
    vertices = [
        (x0, 0.35, 0.46),
        (1.15 * side, 0.05, 0.55),
        (1.35 * side, -0.75, 0.45),
        (0.35 * side, -0.35, 0.44),
    ]
    mesh = bpy.data.meshes.new(name + "_Mesh")
    mesh.from_pydata(vertices, [], [(0, 1, 2), (0, 2, 3)])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    assign_all_vertices(obj, bone_name)
    return obj


def create_mesh(armature):
    parts = [
        add_ellipsoid("Thorax", (0.0, 0.1, 0.32), (0.48, 0.7, 0.4), "thorax", 2),
        add_ellipsoid("Head", (0.0, 0.92, 0.36), (0.38, 0.42, 0.34), "head", 2),
        add_ellipsoid("Abdomen", (0.0, -0.72, 0.3), (0.52, 0.85, 0.42), "abdomen", 2),
        add_ellipsoid("EyeL", (0.25, 1.18, 0.45), (0.12, 0.13, 0.14), "head", 1),
        add_ellipsoid("EyeR", (-0.25, 1.18, 0.45), (0.12, 0.13, 0.14), "head", 1),
        add_wing("WingL", 1.0, "wing_l"),
        add_wing("WingR", -1.0, "wing_r"),
    ]
    limbs = {
        "leg_fl": ((0.18, 0.42, 0.18), (0.88, 0.82, -0.12)),
        "leg_fr": ((-0.18, 0.42, 0.18), (-0.88, 0.82, -0.12)),
        "leg_ml": ((0.2, 0.05, 0.15), (1.0, 0.0, -0.18)),
        "leg_mr": ((-0.2, 0.05, 0.15), (-1.0, 0.0, -0.18)),
        "leg_bl": ((0.18, -0.35, 0.15), (0.88, -0.82, -0.12)),
        "leg_br": ((-0.18, -0.35, 0.15), (-0.88, -0.82, -0.12)),
    }
    for bone_name, (start, end) in limbs.items():
        parts.append(add_limb(bone_name.title(), start, end, 0.055, bone_name))

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = "SK_CrowdDemoBug"
    modifier = mesh.modifiers.new(name="Armature", type="ARMATURE")
    modifier.object = armature
    mesh.parent = armature

    material = bpy.data.materials.new("M_CrowdDemoBug_Source")
    material.diffuse_color = (0.08, 0.38, 0.12, 1.0)
    mesh.data.materials.append(material)
    for polygon in mesh.data.polygons:
        polygon.use_smooth = True
    return mesh


def key_pose(armature, frame, rotations=None, locations=None):
    rotations = rotations or {}
    locations = locations or {}
    for pose_bone in armature.pose.bones:
        pose_bone.rotation_mode = "XYZ"
        pose_bone.rotation_euler = rotations.get(pose_bone.name, (0.0, 0.0, 0.0))
        pose_bone.location = locations.get(pose_bone.name, (0.0, 0.0, 0.0))
        pose_bone.keyframe_insert("rotation_euler", frame=frame, group=pose_bone.name)
        pose_bone.keyframe_insert("location", frame=frame, group=pose_bone.name)


def create_actions(armature):
    actions = {}
    armature.animation_data_create()

    def new_action(name):
        action = bpy.data.actions.new(name=f"A_CrowdDemoBug_{name}")
        action.use_frame_range = True
        action.frame_start = FIRST_FRAME
        action.frame_end = LAST_FRAME
        armature.animation_data.action = action
        actions[name] = action
        return action

    new_action("Idle")
    key_pose(armature, 0)
    key_pose(armature, 12, {"abdomen": (0.0, 0.0, math.radians(4)), "wing_l": (0.0, math.radians(4), 0.0), "wing_r": (0.0, math.radians(-4), 0.0)}, {"thorax": (0.0, 0.0, 0.05)})
    key_pose(armature, 24)

    new_action("Move")
    move_a = {"leg_fl": (math.radians(22), 0.0, 0.0), "leg_mr": (math.radians(22), 0.0, 0.0), "leg_bl": (math.radians(22), 0.0, 0.0), "leg_fr": (math.radians(-22), 0.0, 0.0), "leg_ml": (math.radians(-22), 0.0, 0.0), "leg_br": (math.radians(-22), 0.0, 0.0), "wing_l": (0.0, math.radians(18), 0.0), "wing_r": (0.0, math.radians(-18), 0.0)}
    move_b = {name: (-value[0], value[1], value[2]) for name, value in move_a.items()}
    key_pose(armature, 0, move_a)
    key_pose(armature, 6, move_b, {"thorax": (0.0, 0.0, 0.07)})
    key_pose(armature, 12, move_a)
    key_pose(armature, 18, move_b, {"thorax": (0.0, 0.0, 0.07)})
    key_pose(armature, 24, move_a)

    new_action("Attack")
    key_pose(armature, 0)
    key_pose(armature, 9, {"head": (math.radians(-18), 0.0, 0.0), "abdomen": (math.radians(10), 0.0, 0.0)}, {"thorax": (0.0, -0.08, 0.03)})
    key_pose(armature, 13, {"head": (math.radians(28), 0.0, 0.0), "wing_l": (0.0, math.radians(24), 0.0), "wing_r": (0.0, math.radians(-24), 0.0)}, {"thorax": (0.0, 0.16, 0.02)})
    key_pose(armature, 24)

    new_action("HitReact")
    key_pose(armature, 0)
    key_pose(armature, 5, {"thorax": (math.radians(-22), 0.0, math.radians(10)), "head": (math.radians(20), 0.0, 0.0), "wing_l": (0.0, math.radians(-30), 0.0), "wing_r": (0.0, math.radians(30), 0.0)}, {"root": (0.0, -0.18, 0.08)})
    key_pose(armature, 13, {"thorax": (math.radians(8), 0.0, math.radians(-5))}, {"root": (0.0, -0.05, 0.02)})
    key_pose(armature, 24)

    new_action("Death")
    key_pose(armature, 0)
    key_pose(armature, 10, {"root": (0.0, math.radians(45), math.radians(20)), "wing_l": (math.radians(35), 0.0, 0.0), "wing_r": (math.radians(-35), 0.0, 0.0)}, {"root": (0.0, 0.0, -0.1)})
    death_pose = {"root": (0.0, math.radians(90), math.radians(28)), "head": (math.radians(35), 0.0, 0.0), "abdomen": (math.radians(-18), 0.0, 0.0), "wing_l": (math.radians(58), 0.0, 0.0), "wing_r": (math.radians(-58), 0.0, 0.0), "leg_fl": (math.radians(35), 0.0, 0.0), "leg_fr": (math.radians(-35), 0.0, 0.0)}
    key_pose(armature, 18, death_pose, {"root": (0.0, 0.0, -0.22)})
    key_pose(armature, 24, death_pose, {"root": (0.0, 0.0, -0.22)})

    return actions


def select_only(objects):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def export_assets(output_dir, armature, mesh, actions):
    os.makedirs(output_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = FIRST_FRAME
    scene.frame_end = LAST_FRAME

    armature.animation_data.action = None
    select_only([armature, mesh])
    bpy.ops.export_scene.fbx(
        filepath=os.path.join(output_dir, "SK_CrowdDemoBug.fbx"),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        axis_forward="-Y",
        axis_up="Z",
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        bake_anim=False,
    )

    select_only([mesh])
    bpy.ops.export_scene.fbx(
        filepath=os.path.join(output_dir, "SM_CrowdDemoBug_Source.fbx"),
        use_selection=True,
        object_types={"MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        axis_forward="-Y",
        axis_up="Z",
        mesh_smooth_type="FACE",
        bake_anim=False,
    )

    for clip_name in CLIPS:
        armature.animation_data.action = actions[clip_name]
        # UE 5.7 Interchange does not create an AnimSequence from an
        # armature-only FBX reliably. Include the skinned driver mesh while the
        # Unreal import task explicitly disables mesh import for clip files.
        select_only([armature, mesh])
        bpy.ops.export_scene.fbx(
            filepath=os.path.join(output_dir, f"A_CrowdDemoBug_{clip_name}.fbx"),
            use_selection=True,
            object_types={"ARMATURE", "MESH"},
            apply_unit_scale=True,
            apply_scale_options="FBX_SCALE_ALL",
            axis_forward="-Y",
            axis_up="Z",
            mesh_smooth_type="FACE",
            add_leaf_bones=False,
            bake_anim=True,
            bake_anim_use_all_bones=True,
            bake_anim_use_nla_strips=False,
            bake_anim_use_all_actions=False,
            bake_anim_force_startend_keying=True,
            bake_anim_step=1.0,
            bake_anim_simplify_factor=0.0,
        )

    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(output_dir, "CrowdDemoBug_Source.blend"))
    manifest = {
        "schema_version": 1,
        "source_kind": "new_generated_asset",
        "reads_original_project_assets": False,
        "sample_rate": FPS,
        "clips": [
            {"visual_state": name, "sequence": f"A_CrowdDemoBug_{name}", "sample_count": 25, "loop": name in ("Idle", "Move")}
            for name in CLIPS
        ],
    }
    with open(os.path.join(output_dir, "CrowdDemoBugVatManifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)


def main():
    args = parse_args()
    clear_scene()
    armature = create_armature()
    mesh = create_mesh(armature)
    actions = create_actions(armature)
    export_assets(os.path.abspath(args.output_dir), armature, mesh, actions)
    print(f"CrowdDemoVatSourceGenerated output={os.path.abspath(args.output_dir)} clips={len(CLIPS)}")


if __name__ == "__main__":
    main()
