"""Read-only validation for the generated T7 five-state Bone VAT assets."""

import unreal


ROOT = "/Game/CrowdDemo/VAT/T7"
CLIPS = ("Idle", "Move", "Attack", "HitReact", "Death")


def require(path, expected_type):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset or not isinstance(asset, expected_type):
        raise RuntimeError(f"Missing or wrong asset type: {path}")
    return asset


def main():
    skeletal = require(ROOT + "/Meshes/SK_CrowdDemoBug", unreal.SkeletalMesh)
    static = require(ROOT + "/Meshes/SM_CrowdDemoBug_Source", unreal.StaticMesh)
    data = require(ROOT + "/Data/DA_CrowdDemoBug_FiveState_BoneVAT", unreal.AnimToTextureDataAsset)
    sequences = [
        require(ROOT + f"/Animations/A_CrowdDemoBug_{clip}", unreal.AnimSequence)
        for clip in CLIPS
    ]
    textures = [
        require(ROOT + "/Textures/T_CrowdDemoBug_BonePosition", unreal.Texture2D),
        require(ROOT + "/Textures/T_CrowdDemoBug_BoneRotation", unreal.Texture2D),
        require(ROOT + "/Textures/T_CrowdDemoBug_BoneWeight", unreal.Texture2D),
    ]
    materials = [
        require(ROOT + f"/Materials/MI_CrowdDemoBug_{clip}_VAT", unreal.MaterialInstanceConstant)
        for clip in CLIPS
    ]
    runtime_material = require(
        ROOT + "/Materials/MI_CrowdDemoBug_Runtime_VAT", unreal.MaterialInstanceConstant
    )
    materials.append(runtime_material)
    hit_flash_material = require(
        ROOT + "/Materials/MI_CrowdDemoBug_Runtime_HitFlash_VAT", unreal.MaterialInstanceConstant
    )
    materials.append(hit_flash_material)

    if data.get_editor_property("skeletal_mesh").get_path_name() != skeletal.get_path_name():
        raise RuntimeError("VAT data asset skeletal mesh reference mismatch")
    if data.get_editor_property("static_mesh").get_path_name() != static.get_path_name():
        raise RuntimeError("VAT data asset static mesh reference mismatch")
    if data.get_editor_property("sample_rate") != 30.0:
        raise RuntimeError("VAT sample rate must be 30fps")
    if data.get_editor_property("num_frames") != 125:
        raise RuntimeError("VAT must contain exactly 125 sampled frames")
    if data.get_editor_property("uv_channel") != 1:
        raise RuntimeError("VAT lookup UV channel must be UV1")

    baked_ranges = [
        (entry.get_editor_property("start_frame"), entry.get_editor_property("end_frame"))
        for entry in data.get_editor_property("animations")
    ]
    expected_ranges = [(index * 25, index * 25 + 24) for index in range(5)]
    if baked_ranges != expected_ranges:
        raise RuntimeError(f"VAT clip ranges mismatch: {baked_ranges}")

    static_mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    uv_count = static_mesh_subsystem.get_num_uv_channels(static, 0)
    if uv_count < 2:
        raise RuntimeError(f"VAT static mesh is missing UV1: uv_count={uv_count}")

    if any(texture.blueprint_get_size_x() <= 0 or texture.blueprint_get_size_y() <= 0 for texture in textures):
        raise RuntimeError("VAT texture has invalid dimensions")
    if any(material.get_editor_property("parent") is None for material in materials):
        raise RuntimeError("VAT material instance is missing its official AnimToTexture parent")
    auto_play = unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(
        runtime_material, "AutoPlay"
    )
    if auto_play:
        raise RuntimeError("Runtime VAT material must use per-instance manual frame playback")
    hit_flash_auto_play = unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(
        hit_flash_material, "AutoPlay"
    )
    if hit_flash_auto_play:
        raise RuntimeError("Hit-flash VAT material must use per-instance manual frame playback")
    runtime_body_color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        runtime_material, "BodyColor"
    )
    hit_flash_body_color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        hit_flash_material, "BodyColor"
    )
    if runtime_body_color == hit_flash_body_color:
        raise RuntimeError("Runtime and hit-flash VAT materials must have distinct BodyColor values")
    bounds = static.get_bounding_box()
    bounds_size = bounds.max - bounds.min

    unreal.log(
        "CrowdDemoVATValidationSuccess "
        f"skeletal={skeletal.get_path_name()} static={static.get_path_name()} "
        f"sequences={len(sequences)} ranges={baked_ranges} uv_count={uv_count} "
        f"textures={[(item.blueprint_get_size_x(), item.blueprint_get_size_y()) for item in textures]} "
        f"materials={len(materials)} runtime_manual=2 "
        f"bounds_cm=({bounds_size.x:.3f},{bounds_size.y:.3f},{bounds_size.z:.3f}) "
        f"runtime_color={runtime_body_color} hit_flash_color={hit_flash_body_color}"
    )


main()
