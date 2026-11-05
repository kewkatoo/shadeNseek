"""Stage C2 - after sns_props.py merged every model, drop the raw Interchange per-node meshes/materials/textures
(/Game/Shade/Props/<id>/Mesh). Only done for models whose merged SM_<id>* exists. Halves the size of Content/.
"""
import unreal

eal = unreal.EditorAssetLibrary
ROOT = "/Game/Shade/Props"


def log(*a):
    unreal.log("SNS_CLEAN " + " ".join(str(x) for x in a))


mids = sorted({a.split("/")[4] for a in eal.list_assets(ROOT, recursive=True, include_folder=False)})
removed = 0
for mid in mids:
    folder = f"{ROOT}/{mid}"
    merged = [a for a in eal.list_assets(folder, recursive=False, include_folder=False) if a.split("/")[-1].startswith("SM_")]
    raw = f"{folder}/Mesh"
    if merged and eal.does_directory_exist(raw):
        if eal.delete_directory(raw):
            removed += 1
        else:
            log("could not delete", raw)
log("removed raw imports for", removed, "models")
log("ALL DONE")
