"""Window-box flowers rendered as dark rectangles: their Poly Haven alpha masks were never downloaded, so sns_props.py gave
them the opaque M_Prop master. Run after `sns_import.py` (SNS_ONLY=models imports the now-downloaded *_alpha maps) and
`sns_fix_masks.py`: every prop material that has a matching alpha map is re-parented to the alpha-tested M_Foliage (with
a light breeze) and the props' Nanite meshes keep their leaf area (PRESERVE_AREA). Works in place: the raw glTF pieces
that sns_props.py merges are gone, so the props are not re-merged.
"""
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
ROOT = "/Game/Shade/Props"
FOLIAGE = eal.load_asset("/Game/Shade/Materials/M_Foliage")


def log(*a):
    unreal.log("SNS_FLOWERS " + " ".join(str(x) for x in a))


fixed_mi, fixed_sm = 0, 0
mids = sorted({p.split("/")[4] for p in eal.list_assets(ROOT, recursive=True, include_folder=False)})
for mid in mids:
    folder = f"{ROOT}/{mid}"
    assets = [p.split(".")[0] for p in eal.list_assets(folder, recursive=True, include_folder=False)]
    alphas = {p.split("/")[-1]: p for p in assets if "/Maps/" in p and p.lower().endswith("_alpha")}
    if not alphas:
        continue
    any_fixed = False
    for p in assets:
        if "/Materials/" not in p:
            continue
        mi = eal.load_asset(p)
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        diff = mel.get_material_instance_texture_parameter_value(mi, "BaseColorTex")
        if not diff:
            continue
        dname = diff.get_name()                     # T_<base>_diff
        if not dname.endswith("_diff"):
            continue
        aname = dname[:-5] + "_alpha"
        if aname not in alphas:
            continue
        nor = mel.get_material_instance_texture_parameter_value(mi, "NormalTex")
        arm = mel.get_material_instance_texture_parameter_value(mi, "ARMTex")
        if mi.get_editor_property("parent") != FOLIAGE:
            mel.set_material_instance_parent(mi, FOLIAGE)
        mel.set_material_instance_texture_parameter_value(mi, "BaseColorTex", diff)
        if nor:
            mel.set_material_instance_texture_parameter_value(mi, "NormalTex", nor)
        if arm:
            mel.set_material_instance_texture_parameter_value(mi, "ARMTex", arm)
        mel.set_material_instance_texture_parameter_value(mi, "AlphaTex", eal.load_asset(alphas[aname]))
        mel.set_material_instance_scalar_parameter_value(mi, "WindAmp", 0.0 if "tree" in mid else 1.8)
        mel.update_material_instance(mi)
        eal.save_loaded_asset(mi)
        fixed_mi += 1
        any_fixed = True
        log("material", p, "->", aname)
    if not any_fixed:
        continue
    for p in assets:
        if "/Mesh/" in p or "/Maps/" in p or "/Materials/" in p:
            continue
        sm = eal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        ns = sm.get_editor_property("nanite_settings")
        if ns.get_editor_property("enabled"):
            ns.set_editor_property("shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)
            sm.set_editor_property("nanite_settings", ns)
            eal.save_loaded_asset(sm)
            fixed_sm += 1
log("done", fixed_mi, "materials", fixed_sm, "nanite meshes")
log("ALL DONE")
