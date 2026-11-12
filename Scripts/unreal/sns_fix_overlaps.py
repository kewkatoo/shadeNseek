"""Nothing clips into anything (L_Vellara, in place). SNS_OVL_INSPECT=1 only reports.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_overlaps.py

1. The vault bridge over the lane (SM_BridgeHouse) lines the passage with walls 2 cm in front of N0's and S0's fronts.
   Those houses still had windows there: shutters, sills, doorsteps and the window boxes on the sills poked through the
   bridge's plaster (shutters and a sill on a blank wall, a box half in the wall). Their geometry in front of the facade
   along the bridge's length is cut away (hidden behind the bridge anyway), and the boxes / flowers that stood on those
   sills are removed.
2. Every movable prop near the route is tested against everything around it (houses, other props, gameplay pieces) with
   an exact triangle-intersection test, the prop shrunk 3% about its centre so resting on the ground or touching a wall
   doesn't count but a shutter through a window box, a chair through a pot or two pots merged does. A clipping prop is
   slid to the nearest spot within 45 cm where it touches nothing and still stands on the same surface (window boxes
   only along their sill); if there is none it is removed (window boxes with their flowers).
sns_fit's sampled check (used while dressing) misses thin intersections; this is the exact one.
"""
import math
import os
import sys

import unreal

sys.path.append(os.path.dirname(__file__))

INSPECT = os.environ.get("SNS_OVL_INSPECT") == "1"
MAP = "/Game/Shade/Maps/L_Vellara"
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
AU = unreal.GeometryScript_AssetUtils
GQ = unreal.GeometryScript_MeshQueries
GS = unreal.GeometryScript_MeshSpatial
GT = unreal.GeometryScript_MeshTransforms
GC = unreal.GeometryScript_MeshComparison
SEL = unreal.GeometryScript_MeshSelection
ED = unreal.GeometryScript_MeshEdits
ML = unreal.MathLibrary
OPTS = unreal.GeometryScriptSpatialQueryOptions()
lvl.load_level(MAP)

AREA = (-1500.0, 5000.0, -1000.0, 900.0)
SKIP = ("Sphere", "SM_Mountains", "mountainside", "coastal_cliff", "coast_rocks", "dutch_ship", "ship_pinnace", "modular_wooden_pier",
        "SM_Sea", "SM_Seabed", "SM_Block", "SM_Rope_100", "SM_Bulb", "SM_Sheet", "SM_Towel", "SM_Bougain", "SM_Leafy", "SM_Card",
        "MatineeCam", "flower_", "shrub_", "periwinkle", "fern_", "iceplant", "weed_plant", "grass")
GROUND = ("SM_Slab_", "SM_Stairs")
STRUCT = ("SM_H_", "SM_BridgeHouse", "SM_ChurchFront", "SM_PotWall", "SM_Campanile", "SM_Dome", "SM_Bench_Stone", "SM_Railing",
          "SM_Post_100", "SM_Beam_100", "SM_Bollard", "SM_Bell", "SM_DoorSill", "SM_ShopSill", "SM_Planter_Long", "large_iron_gate",
          "island_tree")
FLOWERS = ("flower_", "shrub_", "periwinkle")
# fixed into a wall by design (brackets, hose reels, the lion-head fountain): not judged against the houses, but props
# keep clear of them
MOUNTED = ("street_lamp", "aircon", "garden_hose", "lion_head", "lifebuoy")


def has(n, toks):
    return any(t in n for t in toks)


def log(*a):
    unreal.log("SNS_OVL " + " ".join(str(x) for x in a))


def mesh_name(sm):
    return sm.get_name() if sm else ""


# ---------------------------------------------------------------------------------------------------------------------
# meshes
# ---------------------------------------------------------------------------------------------------------------------
_dm, _shrunk = {}, {}


def dmesh(sm):
    k = sm.get_path_name()
    if k not in _dm:
        m = unreal.DynamicMesh()
        AU.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        _dm[k] = m
    return _dm[k]


def shrunk(sm, f=0.97):
    k = sm.get_path_name()
    if k not in _shrunk:
        m = unreal.DynamicMesh()
        AU.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        bb = GQ.get_mesh_bounding_box(m)
        c = unreal.Vector((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2)
        GT.translate_mesh(m, unreal.Vector(-c.x, -c.y, -c.z))
        GT.scale_mesh(m, unreal.Vector(f, f, f), unreal.Vector(0, 0, 0))
        GT.translate_mesh(m, c)
        _shrunk[k] = m
    return _shrunk[k]


def intersects(a_mesh, a_xf, b_mesh, b_xf):
    _, hit = GC.is_intersecting_mesh(a_mesh, a_xf, b_mesh, b_xf)
    return bool(hit)


# ---------------------------------------------------------------------------------------------------------------------
# 1. the bridge
# ---------------------------------------------------------------------------------------------------------------------
def all_smas():
    return [a for a in eas.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.get_editor_property("static_mesh")]


def by_mesh(name):
    return [a for a in all_smas() if mesh_name(a.static_mesh_component.get_editor_property("static_mesh")) == name]


bridge = (by_mesh("SM_BridgeHouse") or [None])[0]
if bridge:
    bo, be = bridge.get_actor_bounds(False)
    bx0, bx1, btop = bo.x - be.x + 10.0, bo.x + be.x - 10.0, bo.z + be.z   # the block, not the cap's overhang
    log("bridge x", round(bx0), "..", round(bx1), "top", round(btop))
    for hname, (y0, y1) in (("SM_H_N0", (-159.8, -60.0)), ("SM_H_S0", (60.0, 159.8))):
        for h in by_mesh(hname):
            sm = h.static_mesh_component.get_editor_property("static_mesh")
            m = unreal.DynamicMesh()
            AU.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
            xf = h.get_actor_transform()
            a = ML.inverse_transform_location(xf, unreal.Vector(bx0, y0, -1.0))
            b = ML.inverse_transform_location(xf, unreal.Vector(bx1, y1, btop - 1.0))
            box = unreal.Box(unreal.Vector(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)), unreal.Vector(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)))
            _, sel = SEL.select_mesh_elements_in_box(m, box, unreal.GeometryScriptMeshSelectionType.TRIANGLES, False, 1)
            n_before = GQ.get_num_triangle_i_ds(m)
            _, n_sel = SEL.get_mesh_selection_info(sel)
            log("  selected", n_sel, "triangles in front of", hname, "along the bridge")
            if not INSPECT:
                ED.delete_selected_triangles_from_mesh(m, sel)
                co = unreal.GeometryScriptCopyMeshToAssetOptions()
                co.set_editor_property("enable_recompute_tangents", True)
                co.set_editor_property("enable_recompute_normals", False)
                AU.copy_mesh_to_static_mesh(m, sm, co, unreal.GeometryScriptMeshWriteLOD())
                unreal.EditorAssetLibrary.save_loaded_asset(sm)
            log("trim", hname, "in front of the facade under the bridge: triangles", n_before, "->", GQ.get_num_triangle_i_ds(m))
    # dressing that stood on the hidden sills (above the ground, against the fronts inside the passage)
    for a in all_smas():
        n = mesh_name(a.static_mesh_component.get_editor_property("static_mesh"))
        if n.startswith(GROUND) or has(n, STRUCT) or has(n, MOUNTED):
            continue
        o, e = a.get_actor_bounds(False)
        if bx0 - 5 <= o.x <= bx1 + 5 and 110.0 <= abs(o.y) < 165.0 and o.z - e.z > 50.0 and o.z < btop:
            log("remove (stood on a sill hidden by the bridge)", a.get_actor_label(), n, [round(o.x), round(o.y), round(o.z)])
            if not INSPECT:
                a.destroy_actor()

# ---------------------------------------------------------------------------------------------------------------------
# 2. clipping props
# ---------------------------------------------------------------------------------------------------------------------
recs = []  # dict(actor, comp, sm, kind, movable)
for a in eas.get_all_level_actors():
    comps = a.get_components_by_class(unreal.StaticMeshComponent)
    for c in comps:
        sm = c.get_editor_property("static_mesh")
        n = mesh_name(sm)
        if not n or not c.is_visible() or a.is_hidden_ed() and False:
            continue
        if has(n, SKIP) or n.startswith(GROUND):
            continue
        o, e, _ = unreal.SystemLibrary.get_component_bounds(c)
        if o.x + e.x < AREA[0] or o.x - e.x > AREA[1] or o.y + e.y < AREA[2] or o.y - e.y > AREA[3]:
            continue
        if a.get_editor_property("hidden"):
            continue
        sma = isinstance(a, unreal.StaticMeshActor)
        kind = ("struct" if has(n, STRUCT) else "windowbox" if n == "SM_WindowBox" else "mounted" if has(n, MOUNTED)
                else "prop" if sma else "fixed")
        recs.append({"a": a, "c": c, "sm": sm, "n": n, "kind": kind, "lo": o - e, "hi": o + e})
log("objects", len(recs), "movable", sum(1 for r in recs if r["kind"] in ("prop", "windowbox")))


def overlap(r, lo, hi, pad=1.0):
    return not (hi.x + pad < r["lo"].x or lo.x - pad > r["hi"].x or hi.y + pad < r["lo"].y or lo.y - pad > r["hi"].y or
                hi.z + pad < r["lo"].z or lo.z - pad > r["hi"].z)


def clashes(r, xf, lo, hi):
    """Others this record clips into at transform xf (world bounds lo..hi)."""
    out = []
    for o in recs:
        if o is r or o.get("gone") or not overlap(o, lo, hi):
            continue
        b_mesh = shrunk(o["sm"]) if o["kind"] in ("prop", "windowbox") else dmesh(o["sm"])
        if intersects(shrunk(r["sm"]), xf, b_mesh, o["c"].get_world_transform()):
            out.append(o)
    return out


ground_like = [r for r in recs if r["kind"] in ("struct", "fixed")]
grounds = []
for a in all_smas():
    n = mesh_name(a.static_mesh_component.get_editor_property("static_mesh"))
    if n.startswith(GROUND):
        grounds.append(a)


def support_z(x, y, ztop, zmin):
    """Highest surface under (x, y) between ztop and zmin among the ground, structures and fixed pieces."""
    best = None
    cands = [(g.static_mesh_component.get_editor_property("static_mesh"), g.get_actor_transform(), g.get_actor_bounds(False)) for g in grounds]
    cands += [(r["sm"], r["c"].get_world_transform(), (None, None)) for r in ground_like if r["lo"].x <= x <= r["hi"].x and r["lo"].y <= y <= r["hi"].y]
    for sm, xf, bnd in cands:
        if bnd[0] is not None:
            o, e = bnd
            if not (o.x - e.x <= x <= o.x + e.x and o.y - e.y <= y <= o.y + e.y):
                continue
        m = dmesh(sm)
        bvh = _bvh(sm)
        lo = ML.inverse_transform_location(xf, unreal.Vector(x, y, ztop))
        d = ML.inverse_transform_direction(xf, unreal.Vector(0, 0, -1))
        L = (d.x ** 2 + d.y ** 2 + d.z ** 2) ** 0.5 or 1.0
        _, hit, _ = GS.find_nearest_ray_intersection_with_mesh(m, bvh, lo, unreal.Vector(d.x / L, d.y / L, d.z / L), OPTS)
        if hit.hit:
            z = ML.transform_location(xf, hit.hit_position).z
            if zmin <= z <= ztop and (best is None or z > best):
                best = z
    return best


_bvhs = {}


def _bvh(sm):
    k = sm.get_path_name()
    if k not in _bvhs:
        _, b = GS.build_bvh_for_mesh(dmesh(sm))
        _bvhs[k] = b
    return _bvhs[k]


def moved_xf(r, dx, dy):
    t = r["c"].get_world_transform()
    loc = t.translation
    return unreal.Transform(unreal.Vector(loc.x + dx, loc.y + dy, loc.z), t.rotation.rotator(), t.scale3d)


def footprint_support(r, dx, dy):
    lo, hi = r["lo"], r["hi"]
    cx, cy = (lo.x + hi.x) / 2 + dx, (lo.y + hi.y) / 2 + dy
    return support_z(cx, cy, lo.z + 25.0, lo.z - 30.0)


bad = []
for r in recs:
    if r["kind"] not in ("prop", "windowbox"):
        continue
    cl = clashes(r, r["c"].get_world_transform(), r["lo"], r["hi"])
    if cl:
        bad.append((r, cl))
log("clipping props", len(bad))
for r, cl in bad:
    o = r["a"].get_actor_location()
    log("  clip", r["a"].get_actor_label(), r["n"], [round(o.x), round(o.y), round(o.z)], "->", ", ".join(f"{c['a'].get_actor_label()} ({c['n']})" for c in cl))

moved = removed = 0
for r, _ in bad:
    if r.get("gone"):
        continue
    xf0 = r["c"].get_world_transform()
    cl = clashes(r, xf0, r["lo"], r["hi"])
    if not cl:
        continue  # an earlier move cleared it
    base = footprint_support(r, 0.0, 0.0)
    # directions: away from the first clash's centre, then sideways, then the rest of the compass
    c0 = cl[0]
    ox, oy = (r["lo"].x + r["hi"].x) / 2 - (c0["lo"].x + c0["hi"].x) / 2, (r["lo"].y + r["hi"].y) / 2 - (c0["lo"].y + c0["hi"].y) / 2
    ang0 = math.atan2(oy, ox) if (abs(ox) + abs(oy)) > 1e-3 else 0.0
    if r["kind"] == "windowbox":
        # out from the wall first (most were set 1-2 cm into it), then along the sill, then both
        yaw = math.radians(r["a"].get_actor_rotation().yaw)
        ax, ay = math.cos(yaw), math.sin(yaw)              # along the sill
        nx, ny = -ay, ax                                   # across it (one of the two is out of the wall)
        dirs = [(nx, ny), (-nx, -ny), (ax, ay), (-ax, -ay)]
        dirs += [(sn * nx + sa * ax * 1.5, sn * ny + sa * ay * 1.5) for sn in (1, -1) for sa in (1, -1)]
        dists = [1.5, 3.0, 4.5, 6.0, 9.0, 12.0]
    else:
        dirs = [(math.cos(ang0 + k * math.pi / 4), math.sin(ang0 + k * math.pi / 4)) for k in (0, 1, -1, 2, -2, 3, -3, 4)]
        dists = [4.0, 8.0, 12.0, 18.0, 24.0, 32.0, 45.0]
    done = False
    for d in dists:
        for (ux, uy) in dirs:
            dx, dy = ux * d, uy * d
            lo = unreal.Vector(r["lo"].x + dx, r["lo"].y + dy, r["lo"].z)
            hi = unreal.Vector(r["hi"].x + dx, r["hi"].y + dy, r["hi"].z)
            xf = moved_xf(r, dx, dy)
            if clashes(r, xf, lo, hi):
                continue
            s = footprint_support(r, dx, dy)
            if base is None or s is None or abs(s - base) > 2.0:
                continue
            log("  move", r["a"].get_actor_label(), r["n"], "by", [round(dx, 1), round(dy, 1)])
            if not INSPECT:
                loc = r["a"].get_actor_location()
                r["a"].set_actor_location(unreal.Vector(loc.x + dx, loc.y + dy, loc.z), False, False)
                bo2, be2, _ = unreal.SystemLibrary.get_component_bounds(r["c"])
                r["lo"], r["hi"] = bo2 - be2, bo2 + be2
            moved += 1
            done = True
            break
        if done:
            break
    if done:
        continue
    log("  remove", r["a"].get_actor_label(), r["n"], "(nowhere clear nearby)")
    removed += 1
    r["gone"] = True
    if r["kind"] == "windowbox":
        # its flowers go with it
        for f in all_smas():
            fn = mesh_name(f.static_mesh_component.get_editor_property("static_mesh"))
            if not has(fn, FLOWERS):
                continue
            fl = f.get_actor_location()
            if r["lo"].x - 2 <= fl.x <= r["hi"].x + 2 and r["lo"].y - 2 <= fl.y <= r["hi"].y + 2 and r["lo"].z <= fl.z <= r["hi"].z + 10:
                log("    with its flower", f.get_actor_label())
                if not INSPECT:
                    f.destroy_actor()
    if not INSPECT and isinstance(r["a"], unreal.StaticMeshActor):
        r["a"].destroy_actor()

log("moved", moved, "removed", removed)
if not INSPECT:
    lvl.save_current_level()
    log("saved")
