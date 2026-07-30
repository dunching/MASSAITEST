"""Import newly generated FBX assets and bake a five-state Bone VAT in UE 5.7.

The script only consumes Intermediate/CrowdDemoVatSource. It never loads or
copies assets from SuperInvincibleTank_BugFix.
"""

import json
import os
import struct
import sys
import zlib

import unreal


SOURCE_DIR = os.path.abspath(os.path.join(unreal.Paths.project_dir(), "Intermediate", "CrowdDemoVatSource"))
ROOT = "/Game/CrowdDemo/VAT/T7"
MESH_DIR = ROOT + "/Meshes"
ANIM_DIR = ROOT + "/Animations"
TEXTURE_DIR = ROOT + "/Textures"
DATA_DIR = ROOT + "/Data"
MATERIAL_DIR = ROOT + "/Materials"
CLIPS = ("Idle", "Move", "Attack", "HitReact", "Death")
OFFICIAL_VAT_PARENT_PATH = (
    "/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/"
    "M_Body_BoneAnimation"
)
RUNTIME_PARENT_NAME = "M_CrowdDemoBug_Runtime_VAT"
RUNTIME_PARENT_PATH = MATERIAL_DIR + "/" + RUNTIME_PARENT_NAME
RUNTIME_PARENT_BUILD_PATH = RUNTIME_PARENT_PATH + "__Building"
LEGACY_HIT_FLASH_PATH = MATERIAL_DIR + "/MI_CrowdDemoBug_Runtime_HitFlash_VAT"
MATERIAL_ATTRIBUTE_PASSTHROUGH = (
    ("Metallic", "Metallic"),
    ("Specular", "Specular"),
    ("Roughness", "Roughness"),
    ("Anisotropy", "Anisotropy"),
    ("Opacity", "Opacity"),
    ("OpacityMask", "OpacityMask"),
    ("Normal", "Normal"),
    ("Tangent", "Tangent"),
    ("WorldPositionOffset", "WorldPositionOffset"),
    ("SubsurfaceColor", "SubsurfaceColor"),
    ("ClearCoat", "ClearCoat"),
    ("ClearCoatRoughness", "ClearCoatRoughness"),
    ("AmbientOcclusion", "AmbientOcclusion"),
    ("Refraction", "Refraction"),
    ("CustomizedUV0", "CustomizedUVs_0"),
    ("CustomizedUV1", "CustomizedUVs_1"),
    ("CustomizedUV2", "CustomizedUVs_2"),
    ("CustomizedUV3", "CustomizedUVs_3"),
    ("CustomizedUV4", "CustomizedUVs_4"),
    ("CustomizedUV5", "CustomizedUVs_5"),
    ("CustomizedUV6", "CustomizedUVs_6"),
    ("CustomizedUV7", "CustomizedUVs_7"),
    ("PixelDepthOffset", "PixelDepthOffset"),
    ("ShadingModel", "ShadingModel"),
    ("Displacement", "Displacement"),
)


def require_file(name):
    path = os.path.join(SOURCE_DIR, name)
    if not os.path.isfile(path):
        raise RuntimeError(f"Missing generated source file: {path}")
    return path


def save_asset(asset):
    if not asset or not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save asset: {asset}")


def delete_exact(asset_path):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        if not unreal.EditorAssetLibrary.delete_asset(asset_path):
            raise RuntimeError(f"Failed to replace generated asset: {asset_path}")


def run_import(filename, destination, options):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths"))
    if not paths:
        raise RuntimeError(f"Import produced no assets: {filename}")
    return paths


def make_skeletal_import_ui(import_animations, skeleton=None):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property(
        "mesh_type_to_import",
        unreal.FBXImportType.FBXIT_ANIMATION if import_animations else unreal.FBXImportType.FBXIT_SKELETAL_MESH,
    )
    ui.set_editor_property("import_mesh", not import_animations)
    ui.set_editor_property("import_animations", import_animations)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    if skeleton:
        ui.set_editor_property("skeleton", skeleton)
    mesh_data = ui.get_editor_property("skeletal_mesh_import_data")
    mesh_data.set_editor_property("import_morph_targets", False)
    if import_animations:
        anim_data = ui.get_editor_property("anim_sequence_import_data")
        anim_data.set_editor_property("import_bone_tracks", True)
        anim_data.set_editor_property("remove_redundant_keys", False)
    return ui


def make_static_import_ui():
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    return ui


def write_placeholder_png(path):
    width = height = 1
    raw = b"\x00\x80\x80\x80\xff"

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def import_texture(name):
    png_path = os.path.join(SOURCE_DIR, name + ".png")
    write_placeholder_png(png_path)
    paths = run_import(png_path, TEXTURE_DIR, None)
    texture = unreal.EditorAssetLibrary.load_asset(paths[0])
    if not texture:
        raise RuntimeError(f"Failed to load imported texture {paths[0]}")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    save_asset(texture)
    return texture


def create_data_asset(name):
    asset_path = DATA_DIR + "/" + name
    delete_exact(asset_path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AnimToTextureDataAsset)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DATA_DIR, unreal.AnimToTextureDataAsset, factory
    )
    if not asset:
        raise RuntimeError(f"Failed to create {asset_path}")
    return asset


def connect_expression(source, source_output, target, target_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, target, target_input
    ):
        raise RuntimeError(
            "Failed to connect material expression "
            f"{source.get_name()}:{source_output} -> "
            f"{target.get_name()}:{target_input}"
        )


def create_runtime_vat_parent():
    official_parent = unreal.EditorAssetLibrary.load_asset(OFFICIAL_VAT_PARENT_PATH)
    if not official_parent or not isinstance(official_parent, unreal.Material):
        raise RuntimeError(
            f"Missing official AnimToTexture parent material: {OFFICIAL_VAT_PARENT_PATH}"
        )

    delete_exact(RUNTIME_PARENT_BUILD_PATH)
    runtime_parent = unreal.EditorAssetLibrary.duplicate_asset(
        OFFICIAL_VAT_PARENT_PATH, RUNTIME_PARENT_BUILD_PATH
    )
    if not runtime_parent or not isinstance(runtime_parent, unreal.Material):
        raise RuntimeError(
            f"Failed to create temporary runtime VAT parent: {RUNTIME_PARENT_BUILD_PATH}"
        )

    original_attributes = (
        unreal.MaterialEditingLibrary.get_material_property_input_node(
            runtime_parent, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
        )
    )
    original_output = (
        unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(
            runtime_parent, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
        )
    )
    if not original_attributes or not original_output:
        raise RuntimeError(
            "Official AnimToTexture parent does not expose a Material Attributes result"
        )

    create = unreal.MaterialEditingLibrary.create_material_expression
    break_attributes = create(
        runtime_parent, unreal.MaterialExpressionBreakMaterialAttributes, 300, -80
    )
    make_attributes = create(
        runtime_parent, unreal.MaterialExpressionMakeMaterialAttributes, 1200, -80
    )
    per_instance_flash = create(
        runtime_parent, unreal.MaterialExpressionPerInstanceCustomData, 300, 520
    )
    saturate_flash = create(
        runtime_parent, unreal.MaterialExpressionSaturate, 500, 520
    )
    hit_flash_color = create(
        runtime_parent, unreal.MaterialExpressionVectorParameter, 500, 680
    )
    hit_flash_emissive_strength = create(
        runtime_parent, unreal.MaterialExpressionScalarParameter, 500, 840
    )
    base_color_lerp = create(
        runtime_parent, unreal.MaterialExpressionLinearInterpolate, 760, 40
    )
    flash_color_times_intensity = create(
        runtime_parent, unreal.MaterialExpressionMultiply, 760, 300
    )
    flash_emissive = create(
        runtime_parent, unreal.MaterialExpressionMultiply, 940, 300
    )
    emissive_add = create(
        runtime_parent, unreal.MaterialExpressionAdd, 1100, 300
    )
    expressions = (
        break_attributes,
        make_attributes,
        per_instance_flash,
        saturate_flash,
        hit_flash_color,
        hit_flash_emissive_strength,
        base_color_lerp,
        flash_color_times_intensity,
        flash_emissive,
        emissive_add,
    )
    if any(expression is None for expression in expressions):
        raise RuntimeError("Failed to create one or more runtime VAT material expressions")

    per_instance_flash.set_editor_property("data_index", 2)
    per_instance_flash.set_editor_property("const_default_value", 0.0)
    per_instance_flash.set_editor_property(
        "desc", "Per-instance HitFlashIntensity (custom data slot 2)"
    )
    hit_flash_color.set_editor_property("parameter_name", "HitFlashColor")
    hit_flash_color.set_editor_property(
        "default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)
    )
    hit_flash_emissive_strength.set_editor_property(
        "parameter_name", "HitFlashEmissiveStrength"
    )
    hit_flash_emissive_strength.set_editor_property("default_value", 1.0)

    connect_expression(
        original_attributes, original_output, break_attributes, ""
    )
    for source_attribute, target_attribute in MATERIAL_ATTRIBUTE_PASSTHROUGH:
        connect_expression(
            break_attributes,
            source_attribute,
            make_attributes,
            target_attribute,
        )

    connect_expression(break_attributes, "BaseColor", base_color_lerp, "A")
    connect_expression(hit_flash_color, "", base_color_lerp, "B")
    connect_expression(per_instance_flash, "", saturate_flash, "")
    connect_expression(saturate_flash, "", base_color_lerp, "Alpha")
    connect_expression(base_color_lerp, "", make_attributes, "BaseColor")

    connect_expression(
        hit_flash_color, "", flash_color_times_intensity, "A"
    )
    connect_expression(
        saturate_flash, "", flash_color_times_intensity, "B"
    )
    connect_expression(
        flash_color_times_intensity, "", flash_emissive, "A"
    )
    connect_expression(
        hit_flash_emissive_strength, "", flash_emissive, "B"
    )
    connect_expression(break_attributes, "EmissiveColor", emissive_add, "A")
    connect_expression(flash_emissive, "", emissive_add, "B")
    connect_expression(emissive_add, "", make_attributes, "EmissiveColor")

    if not unreal.MaterialEditingLibrary.connect_material_property(
        make_attributes, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
    ):
        raise RuntimeError("Failed to connect rebuilt runtime VAT material attributes")

    unreal.MaterialEditingLibrary.recompile_material(runtime_parent)
    save_asset(runtime_parent)

    delete_exact(RUNTIME_PARENT_PATH)
    if not unreal.EditorAssetLibrary.rename_asset(
        RUNTIME_PARENT_BUILD_PATH, RUNTIME_PARENT_PATH
    ):
        raise RuntimeError(
            f"Failed to publish runtime VAT parent: {RUNTIME_PARENT_PATH}"
        )
    published = unreal.EditorAssetLibrary.load_asset(RUNTIME_PARENT_PATH)
    if not published or not isinstance(published, unreal.Material):
        raise RuntimeError(
            f"Published runtime VAT parent is invalid: {RUNTIME_PARENT_PATH}"
        )
    save_asset(published)
    return published


def create_material_instances(
    data_asset, static_mesh, rebuild_clip_instances=True
):
    parent = unreal.EditorAssetLibrary.load_asset(OFFICIAL_VAT_PARENT_PATH)
    if not parent:
        raise RuntimeError(
            f"Missing official AnimToTexture parent material: {OFFICIAL_VAT_PARENT_PATH}"
        )
    runtime_parent = create_runtime_vat_parent()
    created = []
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if rebuild_clip_instances:
        for index, clip in enumerate(CLIPS):
            name = f"MI_CrowdDemoBug_{clip}_VAT"
            delete_exact(MATERIAL_DIR + "/" + name)
            factory = unreal.MaterialInstanceConstantFactoryNew()
            instance = asset_tools.create_asset(
                name, MATERIAL_DIR, unreal.MaterialInstanceConstant, factory
            )
            if not instance:
                raise RuntimeError(f"Failed to create material instance {name}")
            instance.set_editor_property("parent", parent)
            data_asset.set_editor_property("auto_play", True)
            data_asset.set_editor_property("animation_index", index)
            unreal.AnimToTextureBPLibrary.update_material_instance_from_data_asset(
                data_asset, instance
            )
            save_asset(instance)
            created.append(instance)

    runtime_name = "MI_CrowdDemoBug_Runtime_VAT"
    runtime_path = MATERIAL_DIR + "/" + runtime_name
    runtime = unreal.EditorAssetLibrary.load_asset(runtime_path)
    if runtime and not isinstance(runtime, unreal.MaterialInstanceConstant):
        raise RuntimeError(f"Runtime VAT material has the wrong type: {runtime_path}")
    if not runtime:
        runtime_factory = unreal.MaterialInstanceConstantFactoryNew()
        runtime = asset_tools.create_asset(
            runtime_name,
            MATERIAL_DIR,
            unreal.MaterialInstanceConstant,
            runtime_factory,
        )
    if not runtime:
        raise RuntimeError(f"Failed to create material instance {runtime_name}")
    unreal.MaterialEditingLibrary.set_material_instance_parent(
        runtime, runtime_parent
    )
    unreal.MaterialEditingLibrary.clear_all_material_instance_parameters(runtime)
    data_asset.set_editor_property("auto_play", False)
    data_asset.set_editor_property("frame", 0.0)
    unreal.AnimToTextureBPLibrary.update_material_instance_from_data_asset(data_asset, runtime)
    unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(
        runtime, "PlasticOverride", True
    )
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        runtime, "BodyColor", unreal.LinearColor(0.08, 0.38, 0.12, 1.0)
    )
    save_asset(runtime)
    created.append(runtime)

    delete_exact(LEGACY_HIT_FLASH_PATH)
    current_material = static_mesh.get_material(0)
    if not current_material or current_material.get_path_name() != runtime.get_path_name():
        static_mesh.set_material(0, runtime)
        save_asset(static_mesh)
    return created


def rebuild_materials_only():
    data_asset = unreal.EditorAssetLibrary.load_asset(
        DATA_DIR + "/DA_CrowdDemoBug_FiveState_BoneVAT"
    )
    static_mesh = unreal.EditorAssetLibrary.load_asset(
        MESH_DIR + "/SM_CrowdDemoBug_Source"
    )
    if not data_asset or not isinstance(data_asset, unreal.AnimToTextureDataAsset):
        raise RuntimeError("Materials-only rebuild requires the existing VAT data asset")
    if not static_mesh or not isinstance(static_mesh, unreal.StaticMesh):
        raise RuntimeError("Materials-only rebuild requires the existing VAT static mesh")
    materials = create_material_instances(
        data_asset, static_mesh, rebuild_clip_instances=False
    )
    unreal.log(
        "CrowdDemoVATMaterialsOnlySuccess "
        f"parent={RUNTIME_PARENT_PATH} materials={len(materials)} "
        "custom_data_slot=2 legacy_overlay=removed"
    )


def main():
    if "--materials-only" in sys.argv:
        rebuild_materials_only()
        return

    editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(
        editor_world, "Interchange.FeatureFlags.Import.FBX 0"
    )
    manifest_path = require_file("CrowdDemoBugVatManifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("reads_original_project_assets") is not False:
        raise RuntimeError("Generated-source manifest does not prove original-project isolation")
    if [item["visual_state"] for item in manifest.get("clips", [])] != list(CLIPS):
        raise RuntimeError("Generated-source clip order does not match the five-state contract")

    mesh_paths = run_import(
        require_file("SK_CrowdDemoBug.fbx"), MESH_DIR, make_skeletal_import_ui(False)
    )
    skeletal_mesh = next(
        (unreal.EditorAssetLibrary.load_asset(path) for path in mesh_paths if path.rsplit(".", 1)[-1].startswith("SK_CrowdDemoBug")),
        None,
    )
    if not skeletal_mesh:
        skeletal_mesh = unreal.EditorAssetLibrary.load_asset(MESH_DIR + "/SK_CrowdDemoBug")
    if not skeletal_mesh:
        raise RuntimeError(f"Skeletal mesh import failed: {mesh_paths}")
    skeleton = skeletal_mesh.get_editor_property("skeleton")
    if not skeleton:
        raise RuntimeError("Imported skeletal mesh has no skeleton")

    animations = []
    for clip in CLIPS:
        delete_exact(ANIM_DIR + f"/A_CrowdDemoBug_{clip}")
        paths = run_import(
            require_file(f"A_CrowdDemoBug_{clip}.fbx"),
            ANIM_DIR,
            make_skeletal_import_ui(True, skeleton),
        )
        animation = unreal.EditorAssetLibrary.load_asset(ANIM_DIR + f"/A_CrowdDemoBug_{clip}")
        if not animation or not isinstance(animation, unreal.AnimSequence):
            animation = next((unreal.EditorAssetLibrary.load_asset(path) for path in paths), None)
        if not animation or not isinstance(animation, unreal.AnimSequence):
            raise RuntimeError(f"Animation import failed for {clip}: {paths}")
        animations.append(animation)

    delete_exact(MESH_DIR + "/SM_CrowdDemoBug_Source")
    static_paths = run_import(
        require_file("SM_CrowdDemoBug_Source.fbx"), MESH_DIR, make_static_import_ui()
    )
    static_mesh = unreal.EditorAssetLibrary.load_asset(MESH_DIR + "/SM_CrowdDemoBug_Source")
    if not static_mesh or not isinstance(static_mesh, unreal.StaticMesh):
        raise RuntimeError(f"Static mesh import failed: {static_paths}")
    if not unreal.AnimToTextureBPLibrary.set_light_map_index(static_mesh, 0, 0, False):
        raise RuntimeError("Failed to reserve UV1 for VAT lookup data")

    position_texture = import_texture("T_CrowdDemoBug_BonePosition")
    rotation_texture = import_texture("T_CrowdDemoBug_BoneRotation")
    weight_texture = import_texture("T_CrowdDemoBug_BoneWeight")

    data_asset = create_data_asset("DA_CrowdDemoBug_FiveState_BoneVAT")
    data_asset.set_editor_property("skeletal_mesh", skeletal_mesh)
    data_asset.set_editor_property("static_mesh", static_mesh)
    data_asset.set_editor_property("skeletal_lod_index", 0)
    data_asset.set_editor_property("static_lod_index", 0)
    # Disable generated lightmap UVs above and reserve UV1 for VAT lookup data.
    data_asset.set_editor_property("uv_channel", 1)
    data_asset.set_editor_property("num_driver_triangles", 1)
    data_asset.set_editor_property("sample_rate", 30.0)
    data_asset.set_editor_property("mode", unreal.AnimToTextureMode.BONE)
    data_asset.set_editor_property("precision", unreal.AnimToTexturePrecision.SIXTEEN_BITS)
    data_asset.set_editor_property("num_bone_influences", unreal.AnimToTextureNumBoneInfluences.ONE)
    data_asset.set_editor_property("bone_position_texture", position_texture)
    data_asset.set_editor_property("bone_rotation_texture", rotation_texture)
    data_asset.set_editor_property("bone_weight_texture", weight_texture)
    sequence_infos = []
    for animation in animations:
        info = unreal.AnimToTextureAnimSequenceInfo()
        info.set_editor_property("enabled", True)
        info.set_editor_property("anim_sequence", animation)
        info.set_editor_property("use_custom_range", True)
        info.set_editor_property("start_frame", 0)
        info.set_editor_property("end_frame", 24)
        sequence_infos.append(info)
    data_asset.set_editor_property("anim_sequences", sequence_infos)
    data_asset.set_editor_property("auto_play", False)
    save_asset(data_asset)

    if not unreal.AnimToTextureBPLibrary.animation_to_texture(data_asset):
        raise RuntimeError("AnimToTexture bake returned false")
    save_asset(data_asset)
    save_asset(static_mesh)
    save_asset(position_texture)
    save_asset(rotation_texture)
    save_asset(weight_texture)
    materials = create_material_instances(data_asset, static_mesh)

    baked = list(data_asset.get_editor_property("animations"))
    ranges = [(item.get_editor_property("start_frame"), item.get_editor_property("end_frame")) for item in baked]
    expected = [(index * 25, index * 25 + 24) for index in range(len(CLIPS))]
    if ranges != expected or data_asset.get_editor_property("num_frames") != 125:
        raise RuntimeError(f"VAT frame contract mismatch ranges={ranges} num_frames={data_asset.get_editor_property('num_frames')}")
    unreal.log(
        "CrowdDemoVATBuildSuccess "
        f"skeletal={skeletal_mesh.get_path_name()} static={static_mesh.get_path_name()} "
        f"animations={len(animations)} vat_frames=125 ranges={ranges} "
        f"materials={len(materials)} runtime_manual=1 custom_data_slot=2"
    )


if __name__ == "__main__":
    main()
    unreal.SystemLibrary.quit_editor()
