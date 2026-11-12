"""A stone plinth (base course) along the foot of every house front on the lane: the joint between walls and street.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_plinth.py      (then sns_fix_overlaps.py)

Even with the paving's edge squared, the bare line where a smooth plaster wall meets the cobbles read as a crack / light
seam all along both sides of the lane. Old houses here stand on a stone base course; this builds one: 18 cm tall, 4 cm
proud, 3 cm into the wall and 2 cm into the ground so no joint is left. Where each facade is bare at the ground is
found by casting from the lane into the houses every 5 cm (door / shop openings and doorsteps are left free, so are
the open alley, piazza and stairs). One true-size mesh, SM_LanePlinth, placed at the origin. Idempotent.
"""
import os
import sys

import unreal

sys.path.append(os.path.dirname(__file__))
from sns_fit import mesh_data  # noqa: E402
from sngeo import append, box, new, save, mat  # noqa: E402

lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
GS = unreal.GeometryScript_MeshSpatial
ML = unreal.MathLibrary
OPTS = unreal.GeometryScriptSpatialQueryOptions()
lvl.load_level("/Game/Shade/Maps/L_Vellara")


def log(*a):
    unreal.log("SNS_PLINTH " + " ".join(str(x) for x in a))


def name(a):
    sm = a.static_mesh_component.get_editor_property("static_mesh") if isinstance(a, unreal.StaticMeshActor) else None
    return sm.get_name() if sm else ""


acts = eas.get_all_level_actors()
walls = [a for a in acts if name(a).startswith("SM_H_") or name(a) == "SM_BridgeHouse"]
for a in acts:
    if name(a) == "SM_LanePlinth":
        a.destroy_actor()


def wall_y(x, z, dy):
    best = None
    for a in walls:
        o, e = a.get_actor_bounds(False)
        if not (o.x - e.x <= x <= o.x + e.x):
            continue
        bdm, bbvh, _, _ = mesh_data(a.static_mesh_component.get_editor_property("static_mesh"))
        xf = a.get_actor_transform()
        lo = ML.inverse_transform_location(xf, unreal.Vector(x, 0.0, z))
        d = ML.inverse_transform_direction(xf, unreal.Vector(0, dy, 0))
        _, hit, _ = GS.find_nearest_ray_intersection_with_mesh(bdm, bbvh, lo, d, OPTS)
        if hit.hit:
            y = ML.transform_location(xf, hit.hit_position).y
            if best is None or abs(y) < abs(best):
                best = y
    return best


STEP = 5.0
m = new()
runs = 0
for side, dy in (("north", -1.0), ("south", 1.0)):
    found = []   # [x0, x1, face_y]
    x = -997.5
    while x <= 2652.5:
        ys = [wall_y(x, z, dy) for z in (8.0, 30.0)]
        ok = all(y is not None and 150.0 <= abs(y) <= 162.0 for y in ys) and abs(ys[0] - ys[1]) < 0.6
        if ok:
            if found and abs(ys[0] - found[-1][2]) < 0.6 and x - found[-1][1] <= STEP + 0.1:
                found[-1][1] = x
            else:
                found.append([x, x, ys[0]])
        x += STEP
    # bridge the short breaks where two houses meet (corner pipes, seams); doors and open gaps are 160 cm and more
    merged = []
    for r in [f for f in found if f[1] - f[0] >= 10.0]:   # (a corner downpipe reads as a 5 cm sliver of "wall")
        if merged and r[0] - merged[-1][1] < 40.0 and abs(r[2] - merged[-1][2]) < 3.0:
            merged[-1][1] = r[1]
            merged[-1][2] = max(merged[-1][2], r[2]) if dy > 0 else min(merged[-1][2], r[2])
        else:
            merged.append(list(r))
    for (a0, a1, f) in merged:
        if a1 - a0 < 20.0:
            continue
        x0, x1 = a0 - STEP / 2, a1 + STEP / 2
        append(m, box(x1 - x0, 7.0, 18.0, 0, ((x0 + x1) / 2, f + (-dy) * 0.5, 7.0), bev=0.8))
        runs += 1
        log(side, "plinth", round(x0), "..", round(x1), "face y", round(f, 1))
sm = save(m, "SM_LanePlinth", [mat("MI_StepStone")], folder="/Game/Shade/Meshes/Houses", nanite=False, collision=None, uv_tile=100.0)
a = eas.spawn_actor_from_object(sm, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
a.set_folder_path("Houses")
c = a.static_mesh_component
c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
lvl.save_current_level()
log("runs", runs, "saved")
