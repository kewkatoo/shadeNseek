"""Set WindAmp to 0 on the Nanite tree foliage instances (see sns_props.py)."""
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
n = 0
for mid in ("island_tree_01", "island_tree_02"):
    for p in eal.list_assets(f"/Game/Shade/Props/{mid}/Materials", recursive=True, include_folder=False):
        mi = eal.load_asset(p)
        if isinstance(mi, unreal.MaterialInstanceConstant):
            mel.set_material_instance_scalar_parameter_value(mi, "WindAmp", 0.0)
            mel.update_material_instance(mi)
            eal.save_loaded_asset(mi)
            n += 1
unreal.log(f"SNS_TREES {n} instances stilled")
