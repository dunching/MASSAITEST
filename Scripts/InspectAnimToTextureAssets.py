import unreal


ASSET_PATHS = [
    "/Game/AI/Generated/Items/Meshes/VAT/FlyingAphid/DA_Enemy_FlyingAphid_MultiState_BoneVAT",
    "/Game/AI/Generated/Items/Meshes/VAT/MudMortarSnail/DA_Enemy_MudMortarSnail_MultiState_BoneVAT",
    "/Game/ItemsDefine/NPCs/ItemDefine_Enemy_FlyingAphid_PDA",
    "/Game/ItemsDefine/NPCs/ItemDefine_Enemy_MudMortarSnail_PDA",
    "/Game/AI/Generated/Items/Meshes/Animations/Anim_Enemy_FlyingAphid_MultiState",
    "/Game/AI/Generated/Items/Meshes/Animations/Anim_Enemy_MudMortarSnail_MultiState",
]


def stable_value(value):
    if isinstance(value, (list, tuple)):
        return [stable_value(item) for item in value]
    return str(value)


for asset_path in ASSET_PATHS:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        unreal.log_error(f"CROWDDEMO_VAT_ASSET_MISSING path={asset_path}")
        continue
    unreal.log(f"CROWDDEMO_VAT_ASSET path={asset_path} class={asset.get_class().get_name()}")
    for property_name in sorted(name for name in dir(asset) if not name.startswith("_")):
        try:
            value = asset.get_editor_property(property_name)
        except Exception:
            continue
        unreal.log(f"CROWDDEMO_VAT_PROPERTY asset={asset_path} name={property_name} value={stable_value(value)}")
        if property_name == "item_behavior_data" and value is not None:
            for nested_name in sorted(name for name in dir(value) if not name.startswith("_")):
                try:
                    nested_value = value.get_editor_property(nested_name)
                except Exception:
                    continue
                unreal.log(
                    f"CROWDDEMO_VAT_NESTED asset={asset_path} name={nested_name} value={stable_value(nested_value)}"
                )
