"""Ground slabs with square edges (in place).

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_slabs.py

slab() in sns_level.py bevelled every ground slab's edges by 2 cm. The lane slab's long edges are exactly the facade line
on both sides, so along the whole lane the paving rolled down 2 cm into a groove right at the foot of every wall, which
caught the light as a bright seam: the houses looked lifted off the street. The same groove ran wherever two slabs meet
(lane / piazza / alley / stairs / terrace). This rebuilds each bevelled SM_Slab_NN as the same box without the bevel:
same size, pivot (top centre), material and world-scale UVs, so the actors and the texture layout stay as they are.
"""
import os
import sys

import unreal

sys.path.append(os.path.dirname(__file__))
from sngeo import box, save  # noqa: E402

eal = unreal.EditorAssetLibrary
FOLDER = "/Game/Shade/Meshes/Houses"


def log(*a):
    unreal.log("SNS_SLAB " + " ".join(str(x) for x in a))


n = 0
for p in sorted(eal.list_assets(FOLDER, recursive=False, include_folder=False)):
    name = p.split("/")[-1].split(".")[0]
    if not name.startswith("SM_Slab_"):
        continue
    sm = eal.load_asset(p.split(".")[0])
    bb = sm.get_bounding_box()
    dx, dy, thick = bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z
    cx, cy = (bb.max.x + bb.min.x) / 2, (bb.max.y + bb.min.y) / 2
    if thick >= 200 or abs(bb.max.z) > 0.5 or abs(cx) > 0.5 or abs(cy) > 0.5:
        log("skip", name, "size", round(dx), round(dy), round(thick), "top", round(bb.max.z, 1), "centre", round(cx, 1), round(cy, 1))
        continue
    mats = [m.material_interface for m in sm.get_editor_property("static_materials")]
    m = box(dx, dy, thick, 0, (0, 0, -thick / 2))
    save(m, name, mats, folder=FOLDER, nanite=True, uv_tile=200.0)
    n += 1
    log("square", name, "size", round(dx, 1), round(dy, 1), round(thick, 1), "material", mats[0].get_name() if mats and mats[0] else None)
log("rebuilt", n)
