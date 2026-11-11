"""Stage E2 - the backdrop around Vellara: the mountain behind the town and the seabed in front of it.

  * Mountain: a procedural terrain - low foothills right behind the upper rows, the main ridge ~190 m up and ~300 m
    back, gullies running down it, wings that come down to the sea on both sides of the bay, M_Terrain (limestone cliff
    on steep faces, rock with green scrub on the slopes, grassy shelves) and trees scattered on the gentler lower
    slopes. Replaces the scaled-up rock scans that hung over the town like a cave roof.
  * Seabed: sand shelving away from the beach, so the single-layer water reads turquoise in the shallows and deep blue
    where the bottom falls away.
Run on its own on the saved map (sns_level.py also runs it): Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_backdrop.py
"""
import math
import os
import random
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402
from sngeo import T, V, save, mat, new, log, eal, opts  # noqa: E402

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
MAP = "/Game/Shade/Maps/L_Vellara"
STANDALONE = __name__ == "__main__"
BE = unreal.GeometryScript_MeshEdits  # set_vertex_position lives here
GQ = unreal.GeometryScript_MeshQueries
RB = random.Random(4242)

if STANDALONE:
    lvl.load_level(MAP)
for a in list(actors.get_all_level_actors()):
    if str(a.get_folder_path()) in ("Mountains", "Backdrop"):
        actors.destroy_actor(a)


def grid(nx, ny, fn):
    """A (nx+1) x (ny+1) vertex grid; fn(u, v) -> (x, y, z) with u, v in [0, 1]."""
    m = new()
    unreal.GeometryScript_Primitives.append_rectangle_xy(m, opts(0), T(), 1.0, 1.0, nx, ny)
    n = GQ.get_vertex_count(m)
    for vid in range(n):
        p, ok = GQ.get_vertex_position(m, vid)
        if not ok:
            continue
        x, y, z = fn(min(1.0, max(0.0, p.x + 0.5)), min(1.0, max(0.0, p.y + 0.5)))
        BE.set_vertex_position(m, vid, V(x, y, z), True)
    return m


# --------------------------------------------------------------------------------------------------------- mountain
def front(x):
    """Where the slope starts (y) and how high (z): right behind the upper rows, coming forward and down to the sea
    beyond both ends of the town."""
    # the wings stay north of the terrace line, so the sea views from the piazza and the terrace stay open
    k = max(0.0, abs(x - 1500.0) - 9500.0)
    return -3400.0 + k * 0.25, max(-1300.0, 1700.0 - k * 0.3)


def ripple(x, d):
    return (0.5 * math.sin(x / 2300.0 + 1.7 * math.sin(d / 3100.0)) + 0.3 * math.sin(d / 1700.0 - x / 2900.0 + 0.6) +
            0.2 * math.sin((x + d) / 900.0))


def height(x, d):
    """Foothills right behind the town (a ~25 m shoulder, mostly hidden by the upper rows), then the main ridge rising
    from ~60 m back to ~190 m high ~260-330 m away - from the harbour it sits just above the rooftops, with sky above."""
    fy, fz = front(x)
    t = min(1.0, max(0.0, (d - 6000.0) / 20000.0))
    ridge = 15000.0 * (1.0 + 0.18 * math.sin(x / 8000.0 + 0.7) + 0.1 * math.sin(x / 3300.0 + 2.1))
    z = fz + 2500.0 * (1.0 - math.exp(-d / 3000.0)) + ridge * t * t * (3 - 2 * t)
    gully = (0.5 + 0.5 * math.sin(x / 1300.0 + 0.8 * math.sin(d / 2600.0))) ** 3
    z -= 700.0 * gully * min(1.0, d / 2500.0)
    z += 600.0 * ripple(x, d) * min(1.0, d / 1500.0)
    return z


X0, X1, DMAX = -17000.0, 21000.0, 30000.0


def mountain_pt(u, v):
    # the grid's +y row is the front: y must grow with the grid's v, or the mirrored triangles face away and are culled
    v = 1.0 - v
    x = X0 + (X1 - X0) * u
    fy, fz = front(x)
    if v <= 0.0:
        # Behind the town the edge drops straight down (hidden by the upper rows); beyond the town it runs down into the
        # sea as a sloping apron, like a headland, instead of showing a bare vertical wall.
        if -3500.0 < x < 6500.0:
            return (x, fy + 40.0, fz - 4000.0)
        return (x, fy + max(400.0, (fz + 1400.0) * 1.1), -1400.0)
    d = DMAX * ((v - 1.0 / 72.0) / (1.0 - 1.0 / 72.0)) ** 1.7 if v > 1.0 / 72.0 else 0.0
    return (x, fy - d, height(x, d))


mt = grid(140, 72, mountain_pt)
mt_sm = save(mt, "SM_Mountains", [mat("M_Terrain")], folder="/Game/Shade/Meshes/Backdrop", nanite=True, collision=None, uv_tile=4000.0, angle=180.0)
if mt_sm:
    a = actors.spawn_actor_from_object(mt_sm, V(0, 0, 0), unreal.Rotator(0, 0, 0))
    a.set_folder_path("Mountains")
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    log("mountain vertices", GQ.get_vertex_count(mt))

# trees on the gentler lower slopes (dark green dots at this distance; no shadows, no wind)
trees = [eal.load_asset(p) for p in ("/Game/Shade/Props/island_tree_01/SM_island_tree_01", "/Game/Shade/Props/island_tree_02/SM_island_tree_02")
         if eal.does_asset_exist(p)]
placed = 0
for _ in range(900):
    if placed >= 50 or not trees:  # masked Nanite foliage is costly even far away; 50 read the same at this distance
        break
    x = RB.uniform(-9000.0, 13000.0)
    d = RB.uniform(300.0, 9000.0) ** 1.0
    fy, fz = front(x)
    z0, z1, z2 = height(x, d), height(x, d + 150.0), height(x + 150.0, d)
    slope = math.hypot(z1 - z0, z2 - z0) / 150.0
    if slope > 1.1 or RB.random() > 0.55:
        continue
    t = actors.spawn_actor_from_object(RB.choice(trees), V(x, fy - d, z0 - 40.0), unreal.Rotator(0, RB.uniform(0, 360), 0))
    s = RB.uniform(0.6, 1.1)
    t.set_actor_scale3d(V(s, s, s))
    t.set_folder_path("Mountains")
    c = t.static_mesh_component
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_cast_shadow(False)
    try:
        c.set_editor_property("evaluate_world_position_offset", False)
    except Exception:
        pass
    placed += 1
log("mountain trees", placed)


# ----------------------------------------------------------------------------------------------------------- seabed
def seabed_pt(u, v):
    x = -14000.0 + 30000.0 * u
    y = 3250.0 + 16000.0 * v
    t = min(1.0, max(0.0, (y - 3300.0) / 5200.0))
    z = -1262.0 - 2200.0 * (t * t * (3 - 2 * t)) - 0.25 * max(0.0, y - 8500.0)
    z -= 0.5 * max(0.0, abs(x - 1500.0) - 9000.0)
    z += 60.0 * math.sin(x / 700.0 + y / 1100.0) * t
    return (x, y, z)


sb = grid(90, 60, seabed_pt)
sb_sm = save(sb, "SM_Seabed", [mat("MI_Sand")], folder="/Game/Shade/Meshes/Backdrop", nanite=True, collision=None, uv_tile=600.0, angle=180.0)
if sb_sm:
    a = actors.spawn_actor_from_object(sb_sm, V(0, 0, 0), unreal.Rotator(0, 0, 0))
    a.set_folder_path("Backdrop")
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.static_mesh_component.set_cast_shadow(False)

if STANDALONE:
    lvl.save_current_level()
log("BACKDROP DONE")
