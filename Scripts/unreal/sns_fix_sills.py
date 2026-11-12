"""Window sills stop flickering, and the sliver of N0's door beside the bridge is walled up.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_sills.py        (then sns_fix_plinth.py, sns_fix_overlaps.py)

1. House.window() puts each sill's top exactly at the height of the opening's floor, and the sill reaches 6 cm into the
   opening: two coplanar faces z-fought (the stone and the wall colour flickering in the window recess). Every upward
   stone (slot 7) face of every house mesh is raised 4 mm, so the sill sits just proud of the recess floor.
2. N0's door is behind the vault bridge except a 35 cm sliver between the bridge's end and N0's front, where a strip of
   the door, the stub of its doorstep, a threshold and the plinth's ends all met. The sliver is walled up flush with
   N0's front (true-size block in N0's plaster) and the threshold there is removed.
"""
import os
import sys

import unreal

sys.path.append(os.path.dirname(__file__))
from sngeo import box, save, mat  # noqa: E402

eal = unreal.EditorAssetLibrary
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
AU = unreal.GeometryScript_AssetUtils
SEL = unreal.GeometryScript_MeshSelection
GT = unreal.GeometryScript_MeshTransforms
GQ = unreal.GeometryScript_MeshQueries


def log(*a):
    unreal.log("SNS_SILL " + " ".join(str(x) for x in a))


# 1. sills
folder = "/Game/Shade/Meshes/Houses"
for p in sorted(eal.list_assets(folder, recursive=False, include_folder=False)):
    n = p.split("/")[-1].split(".")[0]
    if not n.startswith("SM_H_"):
        continue
    sm = eal.load_asset(p.split(".")[0])
    m = unreal.DynamicMesh()
    AU.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    _, s_mat = SEL.select_mesh_elements_by_material_id(m, 7, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
    _, s_up = SEL.select_mesh_elements_by_normal_angle(m, unreal.Vector(0, 0, 1), 3.0, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
    sel = SEL.combine_mesh_selections(s_mat, s_up, unreal.GeometryScriptCombineSelectionMode.INTERSECTION)
    _, cnt = SEL.get_mesh_selection_info(sel)
    if cnt == 0:
        continue
    GT.translate_mesh_selection(m, sel, unreal.Vector(0, 0, 0.4))
    co = unreal.GeometryScriptCopyMeshToAssetOptions()
    co.set_editor_property("enable_recompute_tangents", True)
    co.set_editor_property("enable_recompute_normals", False)
    AU.copy_mesh_to_static_mesh(m, sm, co, unreal.GeometryScriptMeshWriteLOD())
    eal.save_loaded_asset(sm)
    log(n, "raised", cnt, "stone top faces by 4 mm")

# 2. the sliver beside the bridge
lvl.load_level("/Game/Shade/Maps/L_Vellara")
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor):
        smn = a.static_mesh_component.get_editor_property("static_mesh")
        nm = smn.get_name() if smn else ""
        l = a.get_actor_location()
        if nm == "SM_DoorSill" and abs(l.x + 230.0) < 6 and abs(l.y + 160.0) < 2:
            log("remove threshold", a.get_actor_label())
            a.destroy_actor()
        if nm == "SM_N0Infill":
            a.destroy_actor()
x0, x1, yf, yb, z0, z1 = -251.0, -209.0, -160.2, -188.0, -2.0, 238.0
inf = save(box(x1 - x0, yf - yb, z1 - z0, 0, ((x0 + x1) / 2, (yf + yb) / 2, (z0 + z1) / 2)), "SM_N0Infill", [mat("MI_Wall_Cream")],
           folder=folder, collision="complex", uv_tile=200.0)
a = eas.spawn_actor_from_object(inf, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
a.set_folder_path("Houses")
a.static_mesh_component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_SUNLIGHT, unreal.CollisionResponseType.ECR_IGNORE)
lvl.save_current_level()
log("sliver walled up, saved")
