"""Collision-aware placement for set dressing (mesh-accurate, no physics needed).

A candidate prop's mesh vertices are sampled in world space and tested against the meshes already registered nearby
(fast winding number = point-in-mesh; depth = distance from an inside sample to that mesh's surface). The prop rests on
the highest surface of the allowed support kinds under its footprint (rays), and slides along a given direction (away
from a wall) or sideways until nothing interpenetrates; if no spot fits it is left out. Used by sns_level.py; the
independent check is sns_qa_clipping.py.
"""
import math

import unreal

GS = unreal.GeometryScript_MeshSpatial
GQ = unreal.GeometryScript_MeshQueries
AU = unreal.GeometryScript_AssetUtils
ML = unreal.MathLibrary
OPTS = unreal.GeometryScriptSpatialQueryOptions()
_cache = {}

GROUND = ("SM_Slab_", "SM_Stairs")
HOUSE = ("SM_H_", "SM_ChurchFront", "SM_Bridge", "SM_Campanile", "SM_Dome", "SM_PotWall", "SM_Bench_Stone", "SM_Bollard",
         "SM_Post_100", "SM_Beam_100", "SM_Railing", "SM_Bell", "SM_BigShutter")
SKIP = ("SM_Block", "SM_Sea", "mountainside", "coastal_cliff", "coast_rocks", "dutch_ship", "ship_pinnace", "modular_wooden_pier",
        "island_tree", "Sphere", "SM_Rope_100", "SM_Bulb", "SM_Bougain", "SM_Leafy", "MatineeCam", "SM_Mountains",
        "SM_Seabed")


def log(*a):
    unreal.log("SNS_FIT " + " ".join(str(x) for x in a))


def mesh_data(sm, samples=480):
    key = sm.get_path_name()
    d = _cache.get(key)
    if d:
        return d
    dm = unreal.DynamicMesh()
    AU.copy_mesh_from_static_mesh(sm, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    _, bvh = GS.build_bvh_for_mesh(dm)
    n = GQ.get_vertex_count(dm)
    stride = max(1, n // samples)
    pts = []
    vid = 0
    while len(pts) < samples and vid < n * 2 + 1:
        p, ok = GQ.get_vertex_position(dm, vid)
        if ok:
            pts.append((p.x, p.y, p.z))
        vid += stride
    bb = GQ.get_mesh_bounding_box(dm)
    d = (dm, bvh, pts, (bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z))
    _cache[key] = d
    return d


def xform(x, y, z, yaw=0.0, scale=1.0):
    s = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
    return unreal.Transform(location=unreal.Vector(float(x), float(y), float(z)), rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(yaw)),
                            scale=unreal.Vector(float(s[0]), float(s[1]), float(s[2])))


def world_aabb(xf, bb):
    xs, ys, zs = [], [], []
    for cx in (bb[0], bb[3]):
        for cy in (bb[1], bb[4]):
            for cz in (bb[2], bb[5]):
                w = ML.transform_location(xf, unreal.Vector(cx, cy, cz))
                xs.append(w.x)
                ys.append(w.y)
                zs.append(w.z)
    return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))


def overlap(a, b, pad=0.5):
    return not (a[3] < b[0] - pad or b[3] < a[0] - pad or a[4] < b[1] - pad or b[4] < a[1] - pad or a[5] < b[2] - pad or b[5] < a[2] - pad)


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def kind_of(name):
    if any(k in name for k in GROUND):
        return "ground"
    if any(k in name for k in HOUSE):
        return "house"
    return "prop"


class Fitter:
    def __init__(self, corridor=(), clear=()):
        self.items = []
        self.corridor = list(corridor)        # [(x, y), ...] polyline props must not be pushed towards
        self.clear = list(clear)              # [(x, y, r)] keep-free discs
        self.placed = 0
        self.skipped = 0
        self.moved = 0

    # registry -------------------------------------------------------------------------------------------------------
    def add(self, sm, xf, kind):
        if sm is None:
            return
        _, _, _, bb = mesh_data(sm)
        self.items.append(dict(sm=sm, xf=xf, kind=kind, box=world_aabb(xf, bb)))

    def register_level(self, level_actors, x0=-1600.0, x1=5100.0, y0=-1100.0, y1=900.0):
        n = 0
        for a in level_actors:
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                sm = c.get_editor_property("static_mesh")
                if not sm or not c.get_editor_property("visible"):
                    continue
                name = sm.get_name()
                if any(k in name for k in SKIP):
                    continue
                xf = c.get_world_transform()
                t = xf.translation
                if not (x0 < t.x < x1 and y0 < t.y < y1):
                    continue
                kind = kind_of(name)
                if kind == "prop" and "outdoor_table" in name:
                    kind = "table"
                self.add(sm, xf, kind)
                n += 1
        log("registered", n, "meshes", len(_cache))

    def near(self, box, kinds=None, skip=()):
        return [it for it in self.items if (kinds is None or it["kind"] in kinds) and it["kind"] not in skip and overlap(box, it["box"])]

    # queries --------------------------------------------------------------------------------------------------------
    def depth(self, sm, xf, skip=()):
        """Deepest interpenetration (cm) between the candidate and registered meshes, tested both ways for props (a
        neighbour's spout or rim inside the candidate counts as much as the candidate inside the neighbour)."""
        cdm, cbvh, pts, bb = mesh_data(sm)
        cbox = world_aabb(xf, bb)
        cands = self.near(cbox, skip=skip)
        if not cands:
            return 0.0, None
        wpts = [ML.transform_location(xf, unreal.Vector(*p)) for p in pts]
        worst, who = 0.0, None
        for it in cands:
            if it["kind"] in ("ground", "house"):
                continue
            _, _, opts_, _ = mesh_data(it["sm"])
            for p in opts_:
                w = ML.transform_location(it["xf"], unreal.Vector(*p))
                if not (cbox[0] <= w.x <= cbox[3] and cbox[1] <= w.y <= cbox[4] and cbox[2] <= w.z <= cbox[5]):
                    continue
                lp = ML.inverse_transform_location(xf, w)
                _, ins, _ = GS.is_point_inside_mesh(cdm, cbvh, lp, OPTS)
                if not ins:
                    continue
                _, near, _ = GS.find_nearest_point_on_mesh(cdm, cbvh, lp, OPTS)
                if near.valid:
                    ws = ML.transform_location(xf, near.position)
                    d = math.dist((w.x, w.y, w.z), (ws.x, ws.y, ws.z))
                    if d > worst:
                        worst, who = d, it
        for it in cands:
            bdm, bbvh, _, _ = mesh_data(it["sm"])
            b = it["box"]
            for w in wpts:
                if not (b[0] <= w.x <= b[3] and b[1] <= w.y <= b[4] and b[2] <= w.z <= b[5]):
                    continue
                lp = ML.inverse_transform_location(it["xf"], w)
                _, ins, _ = GS.is_point_inside_mesh(bdm, bbvh, lp, OPTS)
                if not ins:
                    continue
                _, near, _ = GS.find_nearest_point_on_mesh(bdm, bbvh, lp, OPTS)
                if near.valid:
                    ws = ML.transform_location(it["xf"], near.position)
                    d = math.dist((w.x, w.y, w.z), (ws.x, ws.y, ws.z))
                    if d > worst:
                        worst, who = d, it
        return worst, who

    def surface_z(self, x, y, z_top, z_bottom, kinds):
        """Highest surface of the given kinds under (x, y) between z_top and z_bottom, or None."""
        best = None
        for it in self.near((x - 1, y - 1, z_bottom, x + 1, y + 1, z_top), kinds):
            bdm, bbvh, _, _ = mesh_data(it["sm"])
            o = ML.inverse_transform_location(it["xf"], unreal.Vector(x, y, z_top))
            d = ML.inverse_transform_direction(it["xf"], unreal.Vector(0, 0, -1))
            L = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z) or 1.0
            _, hit, _ = GS.find_nearest_ray_intersection_with_mesh(bdm, bbvh, o, unreal.Vector(d.x / L, d.y / L, d.z / L), OPTS)
            if hit.hit:
                hz = ML.transform_location(it["xf"], hit.hit_position).z
                if z_bottom <= hz <= z_top and (best is None or hz > best):
                    best = hz
        return best

    def corridor_dist(self, x, y):
        pts = self.corridor
        if len(pts) < 2:
            return 1e9
        return min(seg_dist(x, y, a[0], a[1], b[0], b[1]) for a, b in zip(pts[:-1], pts[1:]))

    # placement ------------------------------------------------------------------------------------------------------
    def fit(self, sm, x, y, z, yaw=0.0, scale=1.0, support=("ground",), slide=(0.0, 0.0), max_slide=40.0, lat=(0.0, 0.0),
            lat_steps=(), tol=1.2, rest=True, lift=0.3, skip=()):
        """Returns a Transform that fits, or None. `slide` is a unit direction (usually the wall normal), `lat` the
        direction along the wall for `lat_steps` (cm). `skip` kinds are ignored by the interpenetration test."""
        _, _, _, bb = mesh_data(sm)
        tries = [(0.0, 0.0)]
        k = 4.0
        while k <= max_slide + 0.1:
            tries.append((k, 0.0))
            k += 4.0
        for ls in lat_steps:
            tries.append((0.0, ls))
            k = 4.0
            while k <= max_slide + 0.1:
                tries.append((k, ls))
                k += 8.0
        d0 = self.corridor_dist(x, y)
        for (sd, ls) in tries:
            cx = x + slide[0] * sd + lat[0] * ls
            cy = y + slide[1] * sd + lat[1] * ls
            if any(math.hypot(cx - ax, cy - ay) < ar for (ax, ay, ar) in self.clear):
                continue
            if (sd or ls) and self.corridor_dist(cx, cy) < min(d0, 70.0):
                continue
            xf = xform(cx, cy, z, yaw, scale)
            if rest:
                hits = []
                for (u, v) in ((0.5, 0.5), (0.22, 0.22), (0.78, 0.22), (0.22, 0.78), (0.78, 0.78)):
                    w = ML.transform_location(xf, unreal.Vector(bb[0] + (bb[3] - bb[0]) * u, bb[1] + (bb[4] - bb[1]) * v, bb[2]))
                    hits.append(self.surface_z(w.x, w.y, z + 150.0, z - 250.0, support))
                ok = [h for h in hits if h is not None]
                if not ok:
                    continue
                # Rest on the highest surface near the centre's (a step up counts; a balcony's top rail caught by one
                # corner ray does not).
                ref = hits[0] if hits[0] is not None else sorted(ok)[len(ok) // 2]
                fz = max(h for h in ok if h <= ref + 25.0)
                low = ML.transform_location(xf, unreal.Vector(0, 0, bb[2])).z - z     # pivot -> lowest point offset
                xf = xform(cx, cy, fz - low + lift, yaw, scale)
            dep, _ = self.depth(sm, xf, skip=skip)
            if dep <= tol:
                if sd or ls:
                    self.moved += 1
                self.placed += 1
                return xf
        self.skipped += 1
        return None
