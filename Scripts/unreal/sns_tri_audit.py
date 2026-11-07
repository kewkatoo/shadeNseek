"""QA: list the meshes placed in L_Vellara by the triangles they push through the classic (non-Nanite) pipeline.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_tri_audit.py     (output in Saved/Logs/py_sns_tri_audit.log)

Non-Nanite triangles are drawn again in every pass (depth, base, velocity, each shadow cascade), so a few dense
props dominate frame time on a laptop GPU. Prints SNS_TRI lines: total tris, instances, per-mesh tris, nanite, casts
shadow, mesh path.
"""
import collections

import unreal

MAP = "/Game/Shade/Maps/L_Vellara"
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
lvl.load_level(MAP)

agg = collections.defaultdict(lambda: [0, 0, False, False, ""])
for a in actors.get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        sm = c.get_editor_property("static_mesh")
        if not sm:
            continue
        n = 1
        if isinstance(c, unreal.InstancedStaticMeshComponent):
            n = c.get_instance_count()
        try:
            tris = sm.get_num_triangles(0)
        except Exception:  # noqa: BLE001
            tris = -1
        nanite = bool(sm.get_editor_property("nanite_settings").get_editor_property("enabled"))
        e = agg[sm.get_path_name()]
        e[0] += n
        e[1] = tris
        e[2] = nanite
        e[3] = e[3] or bool(c.get_editor_property("cast_shadow"))
        e[4] = str(a.get_folder_path())

rows = sorted(agg.items(), key=lambda kv: (not kv[1][2]) * kv[1][0] * kv[1][1], reverse=True)
total = 0
for path, (n, tris, nanite, shadow, folder) in rows:
    if not nanite:
        total += n * tris
for path, (n, tris, nanite, shadow, folder) in rows[:60]:
    unreal.log(f"SNS_TRI {n * tris:9d} x{n:<4d} {tris:8d} nanite={nanite} shadow={shadow} {folder} {path.split('.')[0]}")
unreal.log(f"SNS_TRI total non-nanite tris {total}")
