"""Builds the cat used for both Shade (the living shadow) and Biscotto (the real cat).

Original model, generated from scratch:
  1. Scripts/blender/cat_sdf.py (system Python) sculpts the body as a signed distance field and polygonises it.
  2. This script (Blender) cleans and decimates it, adds cupped ears with inner-ear colouring, real eyeballs set in
     almond sockets, whiskers and brow whiskers, paints a ginger mackerel-tabby coat into vertex colours, builds a
     37-bone digitigrade skeleton (shoulder blades, toes, 8-bone tail), skins it with heat-diffused weights and adds
     five combed fur shells (material slot 3; UV1.x = shell height) so the silhouette reads as soft fur.
Material slots: 0 M_CatBody, 1 M_CatEye, 2 M_CatWhisker, 3 M_CatShell.
Exports asset-library/Models/generated/SK_Cat.fbx plus preview renders in Saved/CatPreview.

Run:  python -I Scripts/blender/cat_sdf.py .
      blender -b --factory-startup --python Scripts/blender/make_cat.py -- <project_root>
Conventions: Blender metres, cat faces -Y, Z up, cat's left = +X.
"""
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector, noise
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(argv[0] if argv else ".")
OUT_DIR = os.path.join(ROOT, "asset-library", "Models", "generated")
PREVIEW_DIR = os.path.join(ROOT, "Saved", "CatPreview")
os.makedirs(PREVIEW_DIR, exist_ok=True)
NPZ = os.path.join(OUT_DIR, "cat_body_sdf.npz")

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
for m in list(bpy.data.meshes):
    bpy.data.meshes.remove(m)
scene = bpy.context.scene
RNG = np.random.default_rng(7)


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def rot(yaw=0.0, pitch=0.0, roll=0.0):
    """Same convention as cat_sdf.rot (local->world, degrees)."""
    return (Matrix.Rotation(math.radians(yaw), 3, 'Z') @ Matrix.Rotation(math.radians(pitch), 3, 'X')
            @ Matrix.Rotation(math.radians(roll), 3, 'Y'))


# =====================================================================================================================
# 1. Body from the SDF sculpt: smooth away marching-cubes terracing, decimate to a game budget.
# =====================================================================================================================
d = np.load(NPZ)
V, F = d["verts"], d["faces"]
me = bpy.data.meshes.new("CatBody")
me.vertices.add(len(V))
me.vertices.foreach_set("co", V.ravel())
me.loops.add(F.size)
me.loops.foreach_set("vertex_index", F.ravel())
me.polygons.add(len(F))
me.polygons.foreach_set("loop_start", np.arange(0, F.size, 3))
me.update(calc_edges=True)
body = bpy.data.objects.new("CatBody", me)
scene.collection.objects.link(body)
s1 = body.modifiers.new("smooth", 'SMOOTH')
s1.factor, s1.iterations = 0.5, 6
dec = body.modifiers.new("decimate", 'DECIMATE')
dec.ratio = 24000 / max(len(F), 1)
s2 = body.modifiers.new("smooth2", 'SMOOTH')
s2.factor, s2.iterations = 0.35, 2
dg = bpy.context.evaluated_depsgraph_get()
me2 = bpy.data.meshes.new_from_object(body.evaluated_get(dg))
body.modifiers.clear()
body.data = me2
bpy.data.meshes.remove(me)
me = me2
me.shade_smooth()
MATS = [bpy.data.materials.new(n) for n in ("M_CatBody", "M_CatEye", "M_CatWhisker", "M_CatShell")]
for m_ in MATS:
    me.materials.append(m_)
print("CAT body tris", sum(len(p.vertices) - 2 for p in me.polygons))

# =====================================================================================================================
# 2. Skeleton (digitigrade). Bones point from joint to joint so the UE rig can aim them.
# =====================================================================================================================
TAIL = [(0, 0.172, 0.222), (0, 0.215, 0.226), (0, 0.26, 0.226), (0, 0.305, 0.222), (0, 0.35, 0.216), (0, 0.393, 0.208),
        (0, 0.433, 0.199), (0, 0.468, 0.19), (0, 0.492, 0.184)]
BONES = []


def bone(name, head, tail, parent=None, connect=False):
    BONES.append((name, Vector(head), Vector(tail), parent, connect))


bone("root", (0, 0, 0), (0, 0, 0.05))
bone("pelvis", (0, 0.15, 0.212), (0, 0.07, 0.215), "root")
bone("spine_01", (0, 0.07, 0.215), (0, -0.01, 0.218), "pelvis", True)
bone("spine_02", (0, -0.01, 0.218), (0, -0.085, 0.222), "spine_01", True)
bone("spine_03", (0, -0.085, 0.222), (0, -0.125, 0.226), "spine_02", True)
bone("neck", (0, -0.125, 0.226), (0, -0.19, 0.279), "spine_03", True)
bone("head", (0, -0.19, 0.279), (0, -0.268, 0.296), "neck", True)
EAR_BASE = {}
for s, sx in (("l", 1), ("r", -1)):
    EAR_BASE[sx] = Vector((sx * 0.025, -0.199, 0.331))
    bone(f"ear_{s}", EAR_BASE[sx], EAR_BASE[sx] + Vector((sx * 0.012, -0.004, 0.045)), "head")
    bone(f"scapula_{s}", (sx * 0.026, -0.078, 0.252), (sx * 0.036, -0.123, 0.188), "spine_03")
    bone(f"upperarm_{s}", (sx * 0.036, -0.123, 0.188), (sx * 0.039, -0.098, 0.113), f"scapula_{s}", True)
    bone(f"forearm_{s}", (sx * 0.039, -0.098, 0.113), (sx * 0.038, -0.106, 0.034), f"upperarm_{s}", True)
    bone(f"hand_{s}", (sx * 0.038, -0.106, 0.034), (sx * 0.0375, -0.118, 0.0125), f"forearm_{s}", True)
    bone(f"finger_{s}", (sx * 0.0375, -0.118, 0.0125), (sx * 0.0378, -0.142, 0.008), f"hand_{s}", True)
    bone(f"thigh_{s}", (sx * 0.037, 0.148, 0.196), (sx * 0.042, 0.098, 0.122), "pelvis")
    bone(f"calf_{s}", (sx * 0.042, 0.098, 0.122), (sx * 0.04, 0.167, 0.066), f"thigh_{s}", True)
    bone(f"foot_{s}", (sx * 0.04, 0.167, 0.066), (sx * 0.039, 0.152, 0.0135), f"calf_{s}", True)
    bone(f"toe_{s}", (sx * 0.039, 0.152, 0.0135), (sx * 0.039, 0.128, 0.008), f"foot_{s}", True)
for i in range(len(TAIL) - 1):
    bone(f"tail_{i + 1:02d}", TAIL[i], TAIL[i + 1], "pelvis" if i == 0 else f"tail_{i:02d}", i > 0)

arm_data = bpy.data.armatures.new("Armature")
arm = bpy.data.objects.new("Armature", arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
eb = {}
for name, h, t, parent, connect in BONES:
    b = arm_data.edit_bones.new(name)
    b.head, b.tail = h, t
    b.roll = 0.0
    b.use_deform = name != "root"
    if parent:
        b.parent = eb[parent]
        b.use_connect = connect
    eb[name] = b
bpy.ops.object.mode_set(mode='OBJECT')

# =====================================================================================================================
# 3. Skin weights for the body: Blender's heat diffusion, with a distance fallback for anything it missed.
# =====================================================================================================================
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
try:
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
except Exception as e:  # noqa: BLE001
    print("CAT heat weights failed:", e)
segs = {name: (h, t) for name, h, t, _, _ in BONES if name != "root"}


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length


for name in segs:
    if name not in body.vertex_groups:
        body.vertex_groups.new(name=name)
gidx = {g.index: g.name for g in body.vertex_groups}
missing = 0
for v in me.vertices:
    tot = sum(g.weight for g in v.groups if g.group in gidx)
    if tot > 1e-4:
        continue
    missing += 1
    p = v.co
    best = sorted(((seg_dist(p, a, b), n) for n, (a, b) in segs.items()
                   if not (n.endswith("_l") and p.x < -0.004) and not (n.endswith("_r") and p.x > 0.004)))[:2]
    ws = [(1.0 / (dd + 0.004) ** 3, n) for dd, n in best]
    tw = sum(w for w, _ in ws)
    for w, n in ws:
        body.vertex_groups[n].add([v.index], w / tw, 'REPLACE')
print("CAT verts without heat weights (filled by distance):", missing)
# Keep at most 4 influences, normalised, and make sure the ears/face never pull on the spine.
bpy.context.view_layer.objects.active = body
bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
bpy.ops.object.vertex_group_normalize_all(group_select_mode='ALL', lock_active=False)
bpy.ops.object.mode_set(mode='OBJECT')

# =====================================================================================================================
# 4. Coat colours (linear; exported as sRGB bytes). Ginger mackerel tabby, cream bib/belly/muzzle/socks.
# =====================================================================================================================
GINGER = Vector((0.79, 0.43, 0.16))
DARK = Vector((0.47, 0.18, 0.05))
CREAM = Vector((0.91, 0.82, 0.67))
PINK = Vector((0.78, 0.40, 0.38))
PAD = Vector((0.55, 0.26, 0.25))
LID = Vector((0.16, 0.07, 0.03))
INNER_EAR = Vector((0.72, 0.45, 0.39))
SOCKETS = {sx: (Vector((sx * 0.019, -0.2525, 0.3075)), rot(yaw=sx * 26, roll=-sx * 13)) for sx in (1, -1)}
NOSE = Vector((0, -0.2672, 0.2962))


def tail_axis_z(y):
    for i in range(len(TAIL) - 1):
        if TAIL[i][1] <= y <= TAIL[i + 1][1]:
            t = (y - TAIL[i][1]) / (TAIL[i + 1][1] - TAIL[i][1])
            return TAIL[i][2] + (TAIL[i + 1][2] - TAIL[i][2]) * t
    return TAIL[-1][2]


def socket_rim(p):
    """0 far, 1 on the eyelid rim (just outside the almond opening)."""
    best = 0.0
    for c, R in SOCKETS.values():
        q = R.transposed() @ (p - c)
        e = math.sqrt((q.x / 0.0098) ** 2 + (q.y / 0.0045) ** 2 + (q.z / 0.0062) ** 2)
        best = max(best, smoothstep(1.22, 1.02, e))
    return best


def coat(p):
    n1 = noise.noise(Vector((p.x * 22, p.y * 22, p.z * 22)))
    n2 = noise.noise(Vector((p.x * 60, p.y * 60, p.z * 60)))
    c = GINGER * (0.94 + 0.08 * n2)
    is_tail = p.y > 0.178 and abs(p.z - tail_axis_z(p.y)) < 0.03
    is_head = p.y < -0.178 and p.z > 0.25
    leg = p.z < 0.15 and not is_tail and (p.y < -0.085 or p.y > 0.09) and abs(p.x) > 0.018
    if is_tail:
        s = math.sin(p.y * 2 * math.pi / 0.034 + n1 * 1.2)
        stripe = smoothstep(0.1, 0.65, s) * (0.6 + 0.4 * smoothstep(0.25, 0.45, p.y))
        tip = smoothstep(0.45, 0.485, p.y)
        c = c.lerp(DARK, max(stripe * 0.85, tip * 0.75))
    elif is_head:
        # Forehead "M" (narrow stripes converging between the ears) + cheek lines from the eye corners.
        fore = smoothstep(0.305, 0.325, p.z) * smoothstep(-0.25, -0.215, p.y)
        s = math.sin(abs(p.x) * 2 * math.pi / 0.0105 + p.y * 40 + n1 * 0.8)
        stripe = smoothstep(0.35, 0.8, s) * fore
        back = smoothstep(-0.215, -0.19, p.y) * smoothstep(0.29, 0.32, p.z)
        stripe = max(stripe, smoothstep(0.2, 0.75, math.sin(p.y * 2 * math.pi / 0.016 + abs(p.x) * 30 + n1)) * back)
        cheek = smoothstep(0.016, 0.03, abs(p.x)) * smoothstep(-0.255, -0.235, p.y) * smoothstep(0.31, 0.29, p.z) * smoothstep(0.282, 0.29, p.z)
        stripe = max(stripe, smoothstep(0.3, 0.8, math.sin((p.z - 0.29 + (p.y + 0.24) * 0.6) * 2 * math.pi / 0.009)) * cheek)
        c = c.lerp(DARK, stripe * 0.8)
    elif leg:
        s = math.sin(p.z * 2 * math.pi / 0.024 + n1 * 1.5)
        c = c.lerp(DARK, smoothstep(0.3, 0.8, s) * 0.55 * smoothstep(0.03, 0.06, p.z))
    else:
        # Mackerel stripes ringing the body, strongest along the back; a darker dorsal line.
        s = math.sin(p.y * 2 * math.pi / 0.036 + abs(p.x) * 26 + n1 * 1.6)
        top = smoothstep(0.17, 0.25, p.z)
        stripe = smoothstep(0.25, 0.75, s) * (0.35 + 0.6 * top)
        dorsal = smoothstep(0.012, 0.0, abs(p.x)) * smoothstep(0.24, 0.27, p.z)
        c = c * (0.86 + 0.14 * smoothstep(0.27, 0.15, p.z))
        c = c.lerp(DARK, min(1.0, stripe * 0.9 + dorsal * 0.5))
    # Cream: bib, belly, inner legs, muzzle, chin, socks.
    bib = smoothstep(0.026, 0.0, abs(p.x) - 0.011 + 0.006 * n1) * smoothstep(0.22, 0.16, p.z) * smoothstep(-0.03, -0.12, p.y)
    throat = smoothstep(-0.13, -0.2, p.y) * smoothstep(0.27, 0.24, p.z) * smoothstep(0.035, 0.015, abs(p.x))
    belly = smoothstep(0.175, 0.155, p.z) * smoothstep(0.04, 0.02, abs(p.x)) * smoothstep(0.16, 0.08, p.y)
    muzzle = smoothstep(-0.243, -0.258, p.y) * smoothstep(0.297, 0.287, p.z)
    chin = smoothstep(-0.235, -0.25, p.y) * smoothstep(0.278, 0.27, p.z)
    socks = smoothstep(0.04, 0.02, p.z)
    inner = smoothstep(0.03, 0.022, abs(p.x)) * smoothstep(0.11, 0.07, p.z) * (1.0 if leg else 0.0)
    ed = eye_dist(p)
    around_eye = max(0.0, smoothstep(1.9, 1.35, ed) - smoothstep(1.35, 1.12, ed)) * 0.6
    c = c.lerp(CREAM, min(1.0, max(bib, throat, belly, muzzle, chin, socks, inner, around_eye)))
    # Leather + liner: pink nose, dark lids and lip line, pink pads.
    c = c.lerp(PINK, smoothstep(0.0068, 0.0045, (p - NOSE).length))
    lip = smoothstep(0.0022, 0.0008, abs(p.z - (0.2785 - abs(p.x) * 0.25))) * smoothstep(0.02, 0.012, abs(p.x)) * smoothstep(-0.25, -0.262, p.y)
    c = c.lerp(LID, max(socket_rim(p) * 0.75, lip * 0.35))
    if p.z < 0.0045:
        c = c.lerp(PAD, 0.85)
    return c


def eye_dist(p):
    best = 9.0
    for cc, R in SOCKETS.values():
        q = R.transposed() @ (p - cc)
        best = min(best, math.sqrt((q.x / 0.0098) ** 2 + (q.y / 0.0045) ** 2 + (q.z / 0.0062) ** 2))
    return best


def fur_length(p):
    L = 0.0078
    if p.y > 0.178 and abs(p.z - tail_axis_z(p.y)) < 0.03:
        return 0.0092
    if p.y < -0.178 and p.z > 0.25:
        face = smoothstep(-0.232, -0.252, p.y)
        L = 0.0055 * (1 - face) + 0.0016 * face
        L = max(L, 0.0085 * smoothstep(0.02, 0.033, abs(p.x)) * smoothstep(0.302, 0.28, p.z))  # cheek ruff
        return L * (1 - 0.8 * smoothstep(1.6, 1.1, eye_dist(p)))
    L = max(L, 0.0115 * smoothstep(-0.1, -0.15, p.y) * smoothstep(0.24, 0.2, p.z))           # chest ruff
    L *= 0.42 + 0.58 * smoothstep(0.02, 0.12, p.z)                                         # short on the legs
    return max(L, 0.0014)


col = me.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
for v in me.vertices:
    c = coat(v.co)
    col.data[v.index].color = (c.x, c.y, c.z, 1.0)

# =====================================================================================================================
# 5. Fur shells (before the accessories so they only wrap the body). Weights are duplicated with the vertices.
# =====================================================================================================================
N_SHELLS = 5
bm = bmesh.new()
bm.from_mesh(me)
lay = bm.faces.layers.int.new("shell")  # create layers before holding element references
bm.normal_update()
bm.verts.ensure_lookup_table()
base_faces = list(bm.faces)
base_verts = list(bm.verts)
normals = [v.normal.copy() for v in base_verts]
lengths = [fur_length(v.co) for v in base_verts]
combs = []
for v, nrm in zip(base_verts, normals):
    p = v.co
    leg = smoothstep(0.14, 0.09, p.z)
    comb = Vector((0, 1, 0)).lerp(Vector((0, 0.15, -1)), leg).normalized()
    comb = comb - nrm * nrm.dot(comb)
    combs.append(comb.normalized() if comb.length > 1e-5 else Vector((0, 0, 0)))
for f in base_faces:
    f.material_index = 0
for i in range(1, N_SHELLS + 1):
    h = i / N_SHELLS
    ret = bmesh.ops.duplicate(bm, geom=base_faces)
    bm.verts.index_update()
    for va, vb in ret["vert_map"].items():
        if not isinstance(va, bmesh.types.BMVert):
            continue
        ov, nv = (va, vb) if va.index < len(base_verts) else (vb, va)
        k = ov.index
        L = lengths[k]
        nv.co = ov.co + normals[k] * (L * h) + combs[k] * (L * 0.55 * h ** 1.4) + Vector((0, 0, -L * 0.12 * h * h))
    for f in ret["geom"]:
        if isinstance(f, bmesh.types.BMFace):
            f.material_index = 3
            f.smooth = True
            f.tag = True
    for f in ret["geom"]:
        if isinstance(f, bmesh.types.BMFace):
            f[lay] = i
bm.to_mesh(me)
bm.free()


# =====================================================================================================================
# 6. Accessories: ears, eyes, whiskers (joined, rigid-weighted).
# =====================================================================================================================
def add_island(bm_src, material, weights, color_fn):
    tmp = bpy.data.meshes.new("tmp")
    bm_src.to_mesh(tmp)
    bm_src.free()
    for m_ in MATS:
        tmp.materials.append(m_)
    ob = bpy.data.objects.new("tmp", tmp)
    scene.collection.objects.link(ob)
    for p in tmp.polygons:
        p.material_index = material
        p.use_smooth = True
    c = tmp.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
    groups = {}
    for v in tmp.vertices:
        cc = color_fn(v.co)
        c.data[v.index].color = (cc.x, cc.y, cc.z, 1.0)
        for name, w in weights(v.co):
            if name not in groups:
                groups[name] = ob.vertex_groups.new(name=name)
            groups[name].add([v.index], w, 'REPLACE')
    return ob


def ear_mesh(sx):
    """Cupped ear: a U-shaped shell open to the front with a rounded rim and a soft tip."""
    em = bmesh.new()
    rings, seg = 12, 22
    height, r0, depth = 0.053, 0.025, 0.62
    A = math.radians(118)
    outer, inner = [], []
    for j in range(rings + 1):
        t = j / rings
        r = r0 * (1 - t) ** 0.9 * (1 + 0.16 * math.sin(math.pi * t)) + 0.0012
        th = 0.0034 * (1 - t) + 0.0011
        ro, ri = [], []
        for i in range(seg + 1):
            a = -A + 2 * A * i / seg
            xo, yo = r * math.sin(a), r * math.cos(a) * depth
            xi, yi = (r - th) * math.sin(a), (r - th) * math.cos(a) * depth - th * 0.6
            z = height * t
            ro.append(em.verts.new((xo, yo, z)))
            ri.append(em.verts.new((xi * 0.96, yi, z)))
        outer.append(ro)
        inner.append(ri)
    for j in range(rings):
        for i in range(seg):
            em.faces.new((outer[j][i], outer[j][i + 1], outer[j + 1][i + 1], outer[j + 1][i]))
            em.faces.new((inner[j][i + 1], inner[j][i], inner[j + 1][i], inner[j + 1][i + 1]))
        for i in (0, seg):  # rim joining outer and inner
            a, b = (outer[j][i], inner[j][i]) if i == 0 else (inner[j][i], outer[j][i])
            c, d = (inner[j + 1][i], outer[j + 1][i]) if i == 0 else (outer[j + 1][i], inner[j + 1][i])
            em.faces.new((a, b, c, d))
    tip = em.verts.new((0, 0.0008, height + 0.0022))
    for i in range(seg):
        em.faces.new((outer[rings][i], outer[rings][i + 1], tip))
        em.faces.new((inner[rings][i + 1], inner[rings][i], tip))
    bmesh.ops.recalc_face_normals(em, faces=em.faces)
    m = (Matrix.Translation(EAR_BASE[sx] + Vector((0, 0, -0.0095))) @ Matrix.Rotation(math.radians(-17 * sx), 4, 'Y')
         @ Matrix.Rotation(math.radians(-6), 4, 'X') @ Matrix.Rotation(math.radians(-16 * sx), 4, 'Z'))
    bmesh.ops.transform(em, matrix=m, verts=em.verts)
    return em


def ear_color(sx):
    base = EAR_BASE[sx]

    def f(p):
        q = p - base
        h = smoothstep(0.0, 0.05, q.z)
        # inside of the cup faces -Y (front); front-facing verts are pink with pale tufts at the base
        front = q.y < -0.001 - 0.002 * (1 - h)
        c = GINGER.lerp(DARK, 0.35 + 0.4 * h)
        if front:
            c = INNER_EAR.lerp(CREAM, 0.3 * (1 - h) ** 2)
        return c
    return f


def ear_weights(sx):
    s = "l" if sx > 0 else "r"

    def f(p):
        h = smoothstep(0.0, 0.012, (p - EAR_BASE[sx]).z + 0.006)
        return [(f"ear_{s}", 0.25 + 0.75 * h), ("head", 0.75 * (1 - h))] if h < 1 else [(f"ear_{s}", 1.0)]
    return f


accessories = []
for sx in (1, -1):
    accessories.append(add_island(ear_mesh(sx), 0, ear_weights(sx), ear_color(sx)))
_bm_body = None

EYE_RADII = (0.0095, 0.0024, 0.006)  # lens-shaped eyeball that fills the almond opening, cornea bulging ~2 mm
EYE_UV = 0.034  # metres per UV unit on the eye (the material's iris/pupil sizes assume this)
EYE_FRAMES = {}
_bm_body = bmesh.new()
_bm_body.from_mesh(me)
bmesh.ops.delete(_bm_body, geom=[f for f in _bm_body.faces if f.material_index != 0], context='FACES')
_bvh = BVHTree.FromBMesh(_bm_body)
for sx in (1, -1):
    c, R = SOCKETS[sx]
    fwd = R @ Vector((0, -1, 0))
    # Depth of the eyelid rim along the eye axis (ray-cast a ring just outside the almond opening).
    depths = []
    for ox, oz in ((0.0108, 0.0), (-0.0108, 0.0), (0.0, 0.0071), (0.0, -0.0071), (0.0076, 0.005), (-0.0076, 0.005),
                   (0.0076, -0.005), (-0.0076, -0.005)):
        o = c + R @ Vector((ox, 0, oz)) + fwd * 0.03
        hit, _, _, _ = _bvh.ray_cast(o, -fwd, 0.06)
        if hit is not None:
            depths.append((hit - c).dot(fwd))
    rim = sum(depths) / len(depths) if depths else 0.0
    print("CAT eye rim depth", sx, round(rim, 4), len(depths))
    center = c + fwd * (rim - 0.0002)
    EYE_FRAMES[sx] = (center, R)
    em = bmesh.new()
    bmesh.ops.create_uvsphere(em, u_segments=32, v_segments=20, radius=1.0)
    for v in em.verts:
        v.co = Vector((v.co.x * EYE_RADII[0], v.co.y * EYE_RADII[1], v.co.z * EYE_RADII[2]))
    bmesh.ops.transform(em, matrix=Matrix.Translation(center) @ R.to_4x4(), verts=em.verts)
    accessories.append(add_island(em, 1, lambda p: [("head", 1.0)], lambda p: Vector((1, 1, 1))))

# Whiskers: tapered three-sided tubes curving out and drooping, plus brow whiskers.
PADS = {sx: Vector((sx * 0.0078, -0.2605, 0.2818)) for sx in (1, -1)}


def whisker_tube(em, pts, r0, r1):
    rings = []
    for k, p in enumerate(pts):
        t = k / (len(pts) - 1)
        r = r0 + (r1 - r0) * t
        tang = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]).normalized()
        side = tang.cross(Vector((0, 0, 1)))
        if side.length < 1e-4:
            side = Vector((1, 0, 0))
        side.normalize()
        up = side.cross(tang).normalized()
        ring = []
        for a in range(3):
            ang = a * 2 * math.pi / 3
            ring.append(em.verts.new(p + (side * math.cos(ang) + up * math.sin(ang)) * r))
        rings.append(ring)
    for k in range(len(rings) - 1):
        for a in range(3):
            b = (a + 1) % 3
            em.faces.new((rings[k][a], rings[k][b], rings[k + 1][b], rings[k + 1][a]))
    em.faces.new((rings[-1][0], rings[-1][1], rings[-1][2]))


wm = bmesh.new()
for sx in (1, -1):
    for row in range(3):
        for colm in range(4):
            root = PADS[sx] + Vector((sx * (0.003 + 0.0016 * colm), 0.0015 * colm - 0.001, 0.0035 - 0.0032 * row))
            direction = Vector((sx * (0.85 + RNG.uniform(-0.08, 0.08)), -0.42 + 0.12 * colm + RNG.uniform(-0.05, 0.05),
                                0.12 - 0.14 * row + RNG.uniform(-0.04, 0.04))).normalized()
            length = 0.058 + 0.012 * (1 - abs(row - 1)) + RNG.uniform(-0.006, 0.006) - 0.004 * colm
            pts = []
            for k in range(9):
                s = k / 8 * length
                pts.append(root + direction * s + Vector((0, 0.16, -0.28)) * (s * s / length))
            whisker_tube(wm, pts, 0.00055, 0.00012)
    for k in range(3):
        c, R = SOCKETS[sx]
        root = c + Vector((sx * (0.002 + 0.004 * k), 0.003, 0.0105))
        direction = Vector((sx * (0.35 + 0.12 * k), -0.25, 0.9)).normalized()
        length = 0.032 + 0.004 * k
        pts = [root + direction * (j / 6 * length) + Vector((sx * 0.3, 0.1, -0.2)) * ((j / 6 * length) ** 2 / length) for j in range(7)]
        whisker_tube(wm, pts, 0.0004, 0.0001)
accessories.append(add_island(wm, 2, lambda p: [("head", 1.0)], lambda p: Vector((0.97, 0.95, 0.9))))

# Join everything into the body (keeps vertex groups by name).
bpy.ops.object.select_all(action='DESELECT')
for o in accessories:
    o.select_set(True)
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.object.join()
me = body.data

# =====================================================================================================================
# 7. Materials, UVs (UV0 fur/eye, UV1.x = shell height), export
# =====================================================================================================================
print("CAT material slots", [m_.name for m_ in me.materials])
shell_layer = me.attributes.get("shell")
shell_of = [0] * len(me.polygons)
if shell_layer is not None:
    vals = np.zeros(len(me.polygons), dtype=np.int32)
    shell_layer.data.foreach_get("value", vals)
    shell_of = vals.tolist()

uv0 = me.uv_layers.new(name="UVMap")
uv1 = me.uv_layers.new(name="Shell")
me.uv_layers.active = uv0
# Fur UVs: a seam-light cylindrical projection around the body axis (fur strands tile at ~1 cm).
for poly in me.polygons:
    for li in poly.loop_indices:
        co = me.vertices[me.loops[li].vertex_index].co
        if poly.material_index == 1:
            sx = 1 if co.x > 0 else -1
            c, R = EYE_FRAMES[sx]
            q = R.transposed() @ (co - c)
            uv0.data[li].uv = (q.x / EYE_UV + 0.5, q.z / EYE_UV + 0.5)
        elif poly.material_index == 2:
            uv0.data[li].uv = (0.5, 0.5)
        else:
            n = poly.normal
            if abs(n.x) > 0.6:
                uv0.data[li].uv = (co.y * 9.0, co.z * 9.0)
            elif abs(n.z) > abs(n.y):
                uv0.data[li].uv = (co.x * 9.0, co.y * 9.0)
            else:
                uv0.data[li].uv = (co.x * 9.0, co.z * 9.0)
        uv1.data[li].uv = (shell_of[poly.index] / N_SHELLS, 0.0)
if shell_layer is not None:
    me.attributes.remove(shell_layer)
col_attr = me.color_attributes.get("Col")
me.color_attributes.active_color = col_attr
me.color_attributes.render_color_index = list(me.color_attributes).index(col_attr)
print("CAT final tris", sum(len(p.vertices) - 2 for p in me.polygons), "verts", len(me.vertices))

bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(OUT_DIR, "SK_Cat.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                         bake_anim=False, mesh_smooth_type='FACE', apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL',
                         use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X', colors_type='SRGB',
                         armature_nodetype='NULL')
print("CAT exported", fbx)

# =====================================================================================================================
# 8. Previews (Workbench, vertex colours, shells hidden) + a posed deformation check.
# =====================================================================================================================
weighted = sum(1 for v in me.vertices if any(g.weight > 1e-4 for g in v.groups))
print("CAT weighted verts", weighted, "/", len(me.vertices), "modifiers", [m_.type for m_ in body.modifiers])
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'VERTEX'
scene.display.shading.light = 'STUDIO'
scene.display.shading.show_cavity = True
scene.render.resolution_x = 900
scene.render.resolution_y = 600
scene.world = bpy.data.worlds.new("W")
cam_data = bpy.data.cameras.new("Cam")
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='POSE')
pb = arm.pose.bones
for name, e in (("thigh_l", (35, 0, 0)), ("calf_l", (-30, 0, 0)), ("upperarm_r", (-40, 0, 0)), ("forearm_r", (60, 0, 0)),
                ("head", (-20, 0, 25)), ("neck", (10, 0, 0)), ("spine_02", (0, 0, 10))):
    pb[name].rotation_mode = 'XYZ'
    pb[name].rotation_euler = tuple(math.radians(a) for a in e)
for i in range(1, 9):
    pb[f"tail_{i:02d}"].rotation_mode = 'XYZ'
    pb[f"tail_{i:02d}"].rotation_euler = (math.radians(8), 0, math.radians(12))
bpy.ops.object.mode_set(mode='OBJECT')
cam_data.lens = 85
cam.location = (1.5, 0.08, 0.3)
cam.rotation_euler = (Vector((0, 0.08, 0.2)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.render.filepath = os.path.join(PREVIEW_DIR, "cat_posed.png")
bpy.ops.render.render(write_still=True)
bpy.ops.object.mode_set(mode='POSE')
for pbn in arm.pose.bones:
    pbn.rotation_euler = (0, 0, 0)
bpy.ops.object.mode_set(mode='OBJECT')
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index == 3], context='FACES_ONLY')
prev_me = bpy.data.meshes.new("preview")
bm.to_mesh(prev_me)
bm.free()
body.data = prev_me
pc = prev_me.color_attributes.get("Col")
if pc:
    prev_me.color_attributes.active_color = pc
    prev_me.color_attributes.render_color_index = list(prev_me.color_attributes).index(pc)
VIEWS = {"side": ((1.5, 0.08, 0.26), (0, 0.08, 0.18), 85), "front34": ((0.75, -1.05, 0.45), (0, -0.04, 0.2), 85),
         "face": ((0.22, -0.8, 0.38), (0, -0.25, 0.3), 200), "back34": ((-0.8, 1.0, 0.55), (0, 0.06, 0.2), 85),
         "top": ((0.01, 0.08, 1.6), (0, 0.08, 0.2), 85)}
for label, (pos, tgt, lens) in VIEWS.items():
    cam_data.lens = lens
    cam.location = pos
    cam.rotation_euler = (Vector(tgt) - Vector(pos)).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(PREVIEW_DIR, f"cat_{label}.png")
    bpy.ops.render.render(write_still=True)
print("CAT previews in", PREVIEW_DIR)
