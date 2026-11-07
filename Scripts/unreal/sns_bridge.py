"""The start passage: the "bridge house" block over the lane, with the vault cut through it.

sns_level.py builds it as part of the village; run this on its own to regenerate just the mesh in place:
    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_bridge.py

The lane is 320 cm wide (house fronts at y = -160 / +160). The vault used to be cut exactly 320 wide, so its inner
walls lay in the same plane as both house fronts for the whole passage and the two surfaces z-fought - big triangles
of aged plaster flickering over the facades whenever the camera moved (TSR hides it only while the view is still).
The cut is now 2 cm narrower each side, so the vault's own walls line the passage just in front of the houses.
"""
import os
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402
from sngeo import GT, T, append, box, subtract, save, mat, new, log  # noqa: E402

HOUSES = "/Game/Shade/Meshes/Houses"
LANE_HALF = 160.0
INSET = 2.0


def build_bridge_mesh():
    half = LANE_HALF - INSET
    bridge = box(400, 340, 520, 0, (0, 0, 260), bev=3)
    subtract(bridge, box(440, 2 * half, 180, 0, (0, 0, 80)))
    vc = new()
    unreal.GeometryScript_Primitives.append_cylinder(vc, unreal.GeometryScriptPrimitiveOptions(), T(), half, 440.0, 32, 0, True,
                                                     unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    GT.transform_mesh(vc, T(rot=(90, 0, 0)))
    GT.transform_mesh(vc, T((0, 0, 170)))
    subtract(bridge, vc)
    append(bridge, box(420, 360, 14, 1, (0, 0, 520)))
    return save(bridge, "SM_BridgeHouse", [mat("MI_Wall_Aged"), mat("MI_Trim_White")], folder=HOUSES, nanite=True)


if __name__ == "__main__":
    sm = build_bridge_mesh()
    log("bridge rebuilt", sm.get_path_name() if sm else None, sm.get_bounds().box_extent if sm else None)
