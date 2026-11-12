"""Door and shop openings along the lane get stone thresholds (L_Vellara, in place).

    SNS_MESHES=door_sills Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_meshes.py      (SM_DoorSill, SM_ShopSill)
    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_doorways.py                       (SNS_DOOR_INSPECT=1: only print)

House.door_at / shopfront cut the opening 25 cm into the wall, but the lane's paving stops at the facade (y = +-160,
the west house's face at x = -1000), so every doorway was a pit down to the bedrock 40 cm below: a dark trench between
the doorstep and the door, the house front looking set back and sunk. This finds each ground-level opening by casting
from the lane into the facades at knee height (edges refined by bisection), and sets a threshold in it sized to the
opening (top level with the doorstep). sns_level.py builds the same thresholds into new houses. Idempotent.
"""
import os
import sys

import unreal

sys.path.append(os.path.dirname(__file__))
from sns_fit import mesh_data  # noqa: E402

MAP = "/Game/Shade/Maps/L_Vellara"
INSPECT = os.environ.get("SNS_DOOR_INSPECT") == "1"
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
eal = unreal.EditorAssetLibrary
GS = unreal.GeometryScript_MeshSpatial
ML = unreal.MathLibrary
OPTS = unreal.GeometryScriptSpatialQueryOptions()
lvl.load_level(MAP)
DOOR = eal.load_asset("/Game/Shade/Meshes/SM_DoorSill")
SHOP = eal.load_asset("/Game/Shade/Meshes/SM_ShopSill")
DOOR_W, SHOP_W = 111.6, 229.6


def log(*a):
    unreal.log("SNS_DOOR " + " ".join(str(x) for x in a))


def name(a):
    sm = a.static_mesh_component.get_editor_property("static_mesh") if isinstance(a, unreal.StaticMeshActor) else None
    return sm.get_name() if sm else ""


def cast(a, start, d):
    bdm, bbvh, _, _ = mesh_data(a.static_mesh_component.get_editor_property("static_mesh"))
    xf = a.get_actor_transform()
    o = ML.inverse_transform_location(xf, start)
    dd = ML.inverse_transform_direction(xf, d)
    L = (dd.x ** 2 + dd.y ** 2 + dd.z ** 2) ** 0.5 or 1.0
    _, hit, _ = GS.find_nearest_ray_intersection_with_mesh(bdm, bbvh, o, unreal.Vector(dd.x / L, dd.y / L, dd.z / L), OPTS)
    return ML.transform_location(xf, hit.hit_position) if hit.hit else None


acts = eas.get_all_level_actors()
walls = [a for a in acts if name(a).startswith("SM_H_") or name(a) == "SM_BridgeHouse"]
existing = [a for a in acts if name(a) in ("SM_DoorSill", "SM_ShopSill")]


def wall_depth(p, d, axis):
    """Distance along `axis` ('x' or 'y') of the first wall hit from p in direction d (or None)."""
    best = None
    for a in walls:
        o, e = a.get_actor_bounds(False)
        if axis == "y" and not (o.x - e.x <= p.x <= o.x + e.x):
            continue
        if axis == "x" and not (o.y - e.y <= p.y <= o.y + e.y):
            continue
        h = cast(a, p, d)
        if h is None:
            continue
        v = h.y if axis == "y" else h.x
        dist = abs(v - (p.y if axis == "y" else p.x))
        if best is None or dist < best[0]:
            best = (dist, v)
    return None if best is None else best[1]


# (side, facade coordinate, sign of "into the house", run axis, run range, ray start offset, yaw)
SIDES = [("north", -160.0, -1.0, "x", (-1000.0, 2650.0), "y", 0.0),
         ("south", 160.0, 1.0, "x", (-1000.0, 2650.0), "y", 180.0),
         ("west", -1000.0, -1.0, "y", (-160.0, 160.0), "x", -90.0)]
placed = 0
for (side, face, into, run_axis, (r0, r1), ray_axis, yaw) in SIDES:
    def recessed(t):
        if ray_axis == "y":
            p, d = unreal.Vector(t, 0.0, 45.0), unreal.Vector(0, into, 0)
        else:
            p, d = unreal.Vector(-900.0, t, 45.0), unreal.Vector(into, 0, 0)
        v = wall_depth(p, d, ray_axis)
        return v is not None and 5.0 < (v - face) * into < 40.0

    samples = []
    t = r0
    while t <= r1:
        samples.append((t, recessed(t)))
        t += 2.5
    runs, start = [], None
    for i, (t, r) in enumerate(samples):
        if r and start is None:
            start = i
        if (not r or i == len(samples) - 1) and start is not None:
            end = i - 1 if not r else i
            runs.append((start, end))
            start = None
    for (i0, i1) in runs:
        def edge(a, b):  # a outside, b inside: bisect to the jamb
            for _ in range(10):
                m = (a + b) / 2.0
                if recessed(m):
                    b = m
                else:
                    a = m
            return b
        lo = edge(samples[i0 - 1][0], samples[i0][0]) if i0 > 0 else samples[i0][0]
        hi = edge(samples[i1 + 1][0], samples[i1][0]) if i1 + 1 < len(samples) else samples[i1][0]
        width = hi - lo
        if width < 15.0:
            log(side, "skip sliver", round(lo, 1), round(hi, 1))
            continue
        sm, mw = (SHOP, SHOP_W) if width > 170.0 else (DOOR, DOOR_W)
        # a narrow slot that is not a door (no doorstep in front, e.g. where the bridge meets a house): filled flush
        # with the paving instead of at doorstep height
        z = -13.8 if width < 90.0 else 0.0
        c = (lo + hi) / 2.0
        loc = unreal.Vector(c, face, z) if run_axis == "x" else unreal.Vector(face, c, z)
        sx = (width - 0.4) / mw
        dup = any(abs(e.get_actor_location().x - loc.x) < 10 and abs(e.get_actor_location().y - loc.y) < 10 for e in existing)
        log(side, sm.get_name(), "opening", round(lo, 1), "..", round(hi, 1), "width", round(width, 1), "at", [round(loc.x, 1), round(loc.y, 1)],
            "scale x", round(sx, 3), "(already there)" if dup else "")
        if INSPECT or dup:
            continue
        a = eas.spawn_actor_from_object(sm, loc, unreal.Rotator(0.0, 0.0, yaw))
        a.set_actor_scale3d(unreal.Vector(sx, 1.0, 1.0))
        a.set_folder_path("Houses/Sills")
        comp = a.static_mesh_component
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_SUNLIGHT, unreal.CollisionResponseType.ECR_IGNORE)
        placed += 1
log("placed", placed)
if not INSPECT and placed:
    lvl.save_current_level()
    log("saved")
