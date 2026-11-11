"""QA (read-only, never saves the map): dressing props that clip into other geometry, float or sink.

For every prop near the route it samples mesh vertices in world space and tests them against each mesh whose bounds
overlap (fast winding number = point-in-mesh); the deepest inside sample, measured to the other mesh's surface, is the
penetration depth. Floating / sunk: rays straight down from the prop's lowest points against the meshes below it.
Run: Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_qa_clipping.py -> Saved/QA/clipping.tsv + SNS_CLIP log lines.
"""
import math
import os

import unreal

GS = unreal.GeometryScript_MeshSpatial
GQ = unreal.GeometryScript_MeshQueries
AU = unreal.GeometryScript_AssetUtils
ML = unreal.MathLibrary
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lvl.load_level("/Game/Shade/Maps/L_Vellara")

X0, X1, Y0, Y1 = -1500.0, 5000.0, -1000.0, 800.0      # the route and the facades seen from it
SAMPLES = 700
SKIP = ("SM_Block", "SM_Sea", "mountainside", "coastal_cliff", "coast_rocks", "dutch_ship", "ship_pinnace", "modular_wooden_pier",
        "island_tree", "Sphere", "SM_Rope_100", "SM_Bulb", "MatineeCam", "SM_Mountains", "SM_Seabed")
GROUND = ("SM_Slab_", "SM_Stairs")
STRUCT = ("SM_H_", "SM_ChurchFront", "SM_Bridge", "SM_Campanile", "SM_Dome", "SM_PotWall", "SM_Railing", "SM_Post_100", "SM_Beam_100",
          "SM_Bench_Stone", "SM_Bollard", "SM_Bell")
WALL_MOUNTED = ("street_lamp", "aircon", "garden_hose", "lion_head", "lifebuoy", "SM_Card", "SM_Sheet", "SM_Towel", "BougainDrape",
                "BougainClump", "LeafyClump")
FOLIAGE = ("flower_", "shrub_", "periwinkle", "fern_", "iceplant", "weed_plant", "Bougain", "Leafy", "grass")
log = lambda *a: unreal.log("SNS_CLIP " + " ".join(str(x) for x in a))


def has(name, keys):
    return any(k in name for k in keys)


cache = {}


def mesh_data(sm):
    key = sm.get_path_name()
    if key in cache:
        return cache[key]
    dm = unreal.DynamicMesh()
    AU.copy_mesh_from_static_mesh(sm, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    _, bvh = GS.build_bvh_for_mesh(dm)
    n = GQ.get_vertex_count(dm)
    pts = []
    vid, stride = 0, max(1, n // SAMPLES)
    guard = 0
    while len(pts) < SAMPLES and guard < n * 2 + 10:
        p, ok = GQ.get_vertex_position(dm, vid)
        if ok:
            pts.append((p.x, p.y, p.z))
        vid += stride
        guard += stride
        if vid > n * 2:
            break
    bb = GQ.get_mesh_bounding_box(dm)
    cache[key] = (dm, bvh, pts, bb)
    return cache[key]


def world_aabb(xf, bb):
    xs, ys, zs = [], [], []
    for cx in (bb.min.x, bb.max.x):
        for cy in (bb.min.y, bb.max.y):
            for cz in (bb.min.z, bb.max.z):
                w = ML.transform_location(xf, unreal.Vector(cx, cy, cz))
                xs.append(w.x); ys.append(w.y); zs.append(w.z)
    return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))


items = []
for a in eas.get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        sm = c.get_editor_property("static_mesh")
        if not sm or not c.get_editor_property("visible"):
            continue
        name = sm.get_name()
        if has(name, SKIP):
            continue
        xf = c.get_world_transform()
        loc = xf.translation
        if not (X0 < loc.x < X1 and Y0 < loc.y < Y1):
            continue
        try:
            dm, bvh, pts, bb = mesh_data(sm)
        except Exception as e:
            log("mesh fail", name, e)
            continue
        kind = "ground" if has(name, GROUND) else "struct" if has(name, STRUCT) else "foliage" if has(name, FOLIAGE) else \
            "mounted" if has(name, WALL_MOUNTED) else "window" if "SM_WindowBox" in name else "prop"
        items.append(dict(label=a.get_actor_label(), folder=str(a.get_folder_path()), name=name, xf=xf, sm=sm, kind=kind,
                          box=world_aabb(xf, bb), comp=c))
log("items", len(items), "meshes", len(cache))

opts = unreal.GeometryScriptSpatialQueryOptions()


def overlap(b1, b2, pad=0.5):
    return not (b1[3] < b2[0] - pad or b2[3] < b1[0] - pad or b1[4] < b2[1] - pad or b2[4] < b1[1] - pad or
                b1[5] < b2[2] - pad or b2[5] < b1[2] - pad)


def penetration(A, B):
    """Deepest A sample inside B (cm, world) and the inside fraction."""
    _, _, apts, _ = mesh_data(A["sm"])
    bdm, bbvh, _, _ = mesh_data(B["sm"])
    bx = B["box"]
    deepest, inside, tested = 0.0, 0, 0
    for p in apts:
        w = ML.transform_location(A["xf"], unreal.Vector(*p))
        if not (bx[0] <= w.x <= bx[3] and bx[1] <= w.y <= bx[4] and bx[2] <= w.z <= bx[5]):
            continue
        tested += 1
        lp = ML.inverse_transform_location(B["xf"], w)
        _, ins, _ = GS.is_point_inside_mesh(bdm, bbvh, lp, opts)
        if not ins:
            continue
        inside += 1
        _, near, _ = GS.find_nearest_point_on_mesh(bdm, bbvh, lp, opts)
        if near.valid:
            ws = ML.transform_location(B["xf"], near.position)
            d = math.dist((w.x, w.y, w.z), (ws.x, ws.y, ws.z))
            deepest = max(deepest, d)
    return deepest, inside, tested


def threshold(A, B):
    ka, kb = A["kind"], B["kind"]
    if kb == "foliage" or (ka == "foliage" and kb in ("foliage", "window")):
        return None                       # leaves interpenetrate naturally; flowers sit in their box
    if ka == "foliage":
        return 4.0
    if ka == "mounted":
        return 8.0 if kb == "struct" else 3.0
    if kb == "ground":
        return 4.0
    return 1.5


rows = []
props = [i for i in items if i["kind"] in ("prop", "foliage", "mounted", "window") or i["name"] in ("SM_Bench_Stone", "SM_Bollard", "SM_PotWall")]
for A in props:
    for B in items:
        if B is A or not overlap(A["box"], B["box"]):
            continue
        th = threshold(A, B)
        if th is None:
            continue
        d, ins, tested = penetration(A, B)
        if d > th:
            rows.append(("clip", A, B, d, ins, tested))
# floating: rays down from just above the lowest samples (wall-mounted pieces and hanging foliage are meant to hang)
for A in props:
    if A["kind"] == "mounted" or has(A["name"], ("Bougain", "Leafy")):
        continue
    _, _, apts, _ = mesh_data(A["sm"])
    ws = sorted((ML.transform_location(A["xf"], unreal.Vector(*p)) for p in apts), key=lambda w: w.z)[:6]
    if not ws:
        continue
    col = (A["box"][0], A["box"][1], A["box"][2] - 400, A["box"][3], A["box"][4], A["box"][2] + 5)
    best = None
    for w in ws:
        o = unreal.Vector(w.x, w.y, w.z + 3.0)
        for B in items:
            if B is A or not overlap(col, B["box"]):
                continue
            bdm, bbvh, _, _ = mesh_data(B["sm"])
            lo = ML.inverse_transform_location(B["xf"], o)
            ld = ML.inverse_transform_direction(B["xf"], unreal.Vector(0, 0, -1))
            L = math.sqrt(ld.x ** 2 + ld.y ** 2 + ld.z ** 2) or 1.0
            ld = unreal.Vector(ld.x / L, ld.y / L, ld.z / L)
            _, hit, _ = GS.find_nearest_ray_intersection_with_mesh(bdm, bbvh, lo, ld, opts)
            if hit.hit:
                hw = ML.transform_location(B["xf"], hit.hit_position)
                gap = w.z - hw.z
                if gap > -3.5 and (best is None or gap < best[0]):
                    best = (gap, B)
    if best is None:
        rows.append(("float", A, None, 999.0, 0, 0))
    elif best[0] > 2.0:
        rows.append(("float", A, best[1], best[0], 0, 0))

out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "QA")
os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "clipping.tsv"), "w", encoding="utf-8") as fh:
    fh.write("type\tdepth_cm\tA_label\tA_mesh\tA_kind\tA_x\tA_y\tA_z\tB_label\tB_mesh\tB_kind\tinside\ttested\n")
    for (t, A, B, d, ins, tested) in sorted(rows, key=lambda r: -r[3]):
        l = A["xf"].translation
        fh.write(f"{t}\t{d:.1f}\t{A['label']}\t{A['name']}\t{A['kind']}\t{l.x:.0f}\t{l.y:.0f}\t{l.z:.0f}\t"
                 f"{B['label'] if B else '-'}\t{B['name'] if B else '-'}\t{B['kind'] if B else '-'}\t{ins}\t{tested}\n")
n_clip = sum(1 for r in rows if r[0] == "clip")
n_float = sum(1 for r in rows if r[0] == "float")
solid = [r for r in rows if r[1]["kind"] in ("prop", "window", "struct")]
log("RESULT clips", n_clip, "floating", n_float, "props checked", len(props), "| solid props: clips",
    sum(1 for r in solid if r[0] == "clip"), "floating", sum(1 for r in solid if r[0] == "float"),
    "| plants/flowers: clips", sum(1 for r in rows if r[0] == "clip" and r[1]["kind"] == "foliage"),
    "| wall-mounted: clips", sum(1 for r in rows if r[0] == "clip" and r[1]["kind"] == "mounted"))
for (t, A, B, d, ins, tested) in sorted(rows, key=lambda r: (r[1]["kind"] not in ("prop", "window", "struct"), -r[3]))[:60]:
    l = A["xf"].translation
    log(f"{t} {d:6.1f} cm  {A['name']} ({l.x:.0f},{l.y:.0f},{l.z:.0f}) -> {B['name'] if B else 'nothing below'}")
log("ALL DONE")
