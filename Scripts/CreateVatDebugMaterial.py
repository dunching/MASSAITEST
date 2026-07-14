import unreal


ASSET_DIR = "/Game/VAT"
MATERIAL_NAME = "M_CrowdDemoVATDebug"
MATERIAL_PATH = f"{ASSET_DIR}/{MATERIAL_NAME}"


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_or_load_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL_PATH):
        existing = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
        if existing:
            return existing

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialFactoryNew()
    return asset_tools.create_asset(MATERIAL_NAME, ASSET_DIR, unreal.Material, factory)


def main():
    ensure_directory(ASSET_DIR)
    material = create_or_load_material()
    if not material:
        raise RuntimeError(f"Failed to create {MATERIAL_PATH}")

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)

    custom_data = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionPerInstanceCustomData3Vector,
        -360,
        0,
    )
    custom_data.set_editor_property("data_index", 0)
    custom_data.set_editor_property("const_default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    unreal.MaterialEditingLibrary.connect_material_property(
        custom_data,
        "",
        unreal.MaterialProperty.MP_BASE_COLOR,
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        custom_data,
        "",
        unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log(f"CrowdDemoVATDebugMaterial path={MATERIAL_PATH}")


if __name__ == "__main__":
    main()
