"""Small plants Shade walks through must sit on M_Foliage: that is where the brush WPO lives (leaves part round the cat,
MPC_Shade). periwinkle_plant models its leaves as geometry (no alpha map), so sns_fix_flowers.py left it on the opaque
M_Prop and it stood rigid while the cat walked through the window boxes. Re-parent such plant materials to M_Foliage
(texture parameters carry over by name; its AlphaTex defaults to flat white = fully opaque). Works in place.
"""
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
FOLIAGE = eal.load_asset("/Game/Shade/Materials/M_Foliage")
PLANTS = ["/Game/Shade/Props/periwinkle_plant/Materials/MI_periwinkle_plant"]


def log(*a):
    unreal.log("SNS_BRUSH " + " ".join(str(x) for x in a))


for p in PLANTS:
    mi = eal.load_asset(p)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        log("missing", p)
        continue
    parent = mi.get_editor_property("parent")
    texs = {n: mel.get_material_instance_texture_parameter_value(mi, n) for n in ("BaseColorTex", "NormalTex", "ARMTex")}
    if parent != FOLIAGE:
        mel.set_material_instance_parent(mi, FOLIAGE)
    for n, t in texs.items():
        if t:
            mel.set_material_instance_texture_parameter_value(mi, n, t)
    mel.set_material_instance_scalar_parameter_value(mi, "WindAmp", 1.2)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    log("re-parented", mi.get_name(), "from", parent.get_name() if parent else None, {n: (t.get_name() if t else None) for n, t in texs.items()})
log("DONE")
