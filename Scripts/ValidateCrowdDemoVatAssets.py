"""Read-only validation for the generated T7 five-state Bone VAT assets."""

import unreal


ROOT = "/Game/CrowdDemo/VAT/T7"
CLIPS = ("Idle", "Move", "Attack", "HitReact", "Death")
RUNTIME_PARENT_PATH = ROOT + "/Materials/M_CrowdDemoBug_Runtime_VAT"
LEGACY_HIT_FLASH_PATH = ROOT + "/Materials/MI_CrowdDemoBug_Runtime_HitFlash_VAT"


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
    runtime_parent = require(RUNTIME_PARENT_PATH, unreal.Material)
    if unreal.EditorAssetLibrary.does_asset_exist(LEGACY_HIT_FLASH_PATH):
        raise RuntimeError("Legacy duplicate-ISM hit-flash material still exists")

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
    if runtime_material.get_editor_property("parent").get_path_name() != runtime_parent.get_path_name():
        raise RuntimeError("Runtime VAT material does not use the project hit-flash parent")
    auto_play = unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(
        runtime_material, "AutoPlay"
    )
    if auto_play:
        raise RuntimeError("Runtime VAT material must use per-instance manual frame playback")
    hit_flash_color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        runtime_material, "HitFlashColor"
    )
    hit_flash_emissive = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
        runtime_material, "HitFlashEmissiveStrength"
    )
    if hit_flash_color != unreal.LinearColor(1.0, 1.0, 1.0, 1.0):
        raise RuntimeError(f"Runtime VAT hit-flash color must default to white: {hit_flash_color}")
    if abs(hit_flash_emissive - 1.0) > 0.0001:
        raise RuntimeError(
            f"Runtime VAT hit-flash emissive strength must default to 1: {hit_flash_emissive}"
        )
    runtime_body_color = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
        runtime_material, "BodyColor"
    )
    root_attributes = unreal.MaterialEditingLibrary.get_material_property_input_node(
        runtime_parent, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
    )
    if not isinstance(root_attributes, unreal.MaterialExpressionMakeMaterialAttributes):
        raise RuntimeError("Runtime VAT parent does not publish rebuilt Material Attributes")
    connected_inputs = unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
        runtime_parent, root_attributes
    )
    connected_inputs = [node for node in connected_inputs if node]
    if len(connected_inputs) < 27:
        raise RuntimeError(
            "Runtime VAT parent does not preserve every material attribute input: "
            f"connected={len(connected_inputs)}"
        )

    graph_nodes = []
    pending_nodes = [root_attributes]
    visited_paths = set()
    while pending_nodes:
        node = pending_nodes.pop()
        node_path = node.get_path_name()
        if node_path in visited_paths:
            continue
        visited_paths.add(node_path)
        graph_nodes.append(node)
        pending_nodes.extend(
            input_node
            for input_node in unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
                runtime_parent, node
            )
            if input_node
        )
    custom_data_nodes = [
        expression
        for expression in graph_nodes
        if isinstance(expression, unreal.MaterialExpressionPerInstanceCustomData)
    ]
    if not any(
        node.get_editor_property("data_index") == 2
        and abs(node.get_editor_property("const_default_value")) <= 0.0001
        for node in custom_data_nodes
    ):
        raise RuntimeError("Runtime VAT parent is missing per-instance custom data slot 2")
    if not any(
        isinstance(expression, unreal.MaterialExpressionBreakMaterialAttributes)
        for expression in graph_nodes
    ):
        raise RuntimeError("Runtime VAT parent is missing the original attribute passthrough")
    bounds = static.get_bounding_box()
    bounds_size = bounds.max - bounds.min

    unreal.log(
        "CrowdDemoVATValidationSuccess "
        f"skeletal={skeletal.get_path_name()} static={static.get_path_name()} "
        f"sequences={len(sequences)} ranges={baked_ranges} uv_count={uv_count} "
        f"textures={[(item.blueprint_get_size_x(), item.blueprint_get_size_y()) for item in textures]} "
        f"materials={len(materials)} runtime_manual=1 custom_data_slot=2 "
        f"bounds_cm=({bounds_size.x:.3f},{bounds_size.y:.3f},{bounds_size.z:.3f}) "
        f"runtime_color={runtime_body_color} hit_flash_color={hit_flash_color} "
        f"hit_flash_emissive={hit_flash_emissive}"
    )


main()
