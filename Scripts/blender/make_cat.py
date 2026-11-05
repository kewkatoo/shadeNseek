"""Builds the cat used for both Shade (the living shadow) and Biscotto (the real cat).

Original model, generated from scratch: metaball anatomy -> voxel remesh -> decimate, separate ears/eyes,
baked tabby vertex colours, a 25-bone armature with procedural distance-gated skin weights.
Exports asset-library/Models/generated/SK_Cat.fbx and preview renders.

Run:  blender -b --factory-startup --python Scripts/blender/make_cat.py -- <project_root>
Conventions: Blender metres, cat faces -Y (Blender front), Z up, cat's left = +X.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(argv[0] if argv else ".")
OUT_DIR = os.path.join(ROOT, "asset-library", "Models", "generated")
PREVIEW_DIR = os.path.join(ROOT, "Saved", "CatPreview")
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(PREVIEW_DIR, exist_ok=True)

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
for m in list(bpy.data.meshes):
    bpy.data.meshes.remove(m)
scene = bpy.context.scene

K = 1.0 / 0.574  # metaball radius needed for a visible radius of 1 (stiffness 2, threshold 0.6)

# ---------------------------------------------------------------------------------------------------------------------
# Anatomy
# ---------------------------------------------------------------------------------------------------------------------
mb = bpy.data.metaballs.new("CatMB")
mb.resolution = 0.0045
mb.render_resolution = 0.0045
mb.threshold = 0.6
mbo = bpy.data.objects.new("CatMB", mb)
scene.collection.objects.link(mbo)


def ell(co, rx, ry, rz, stiff=2.0, rot=None):
    e = mb.elements.new(type='ELLIPSOID')
    rmax = max(rx, ry, rz)
    e.co = co
    e.radius = K * rmax
    e.size_x, e.size_y, e.size_z = rx / rmax, ry / rmax, rz / rmax
    e.stiffness = stiff
    if rot is not None:
        e.rotation = rot
    return e


def ball(co, r, stiff=2.0):
    return ell(co, r, r, r, stiff)


def chain(points, radii, stiff=2.4, steps=4):
    """Balls along a polyline with interpolated radii (legs, tail)."""
    for i in range(len(points) - 1):
        a, b = Vector(points[i]), Vector(points[i + 1])
        for s in range(steps):
            t = s / steps
            ball(a.lerp(b, t), radii[i] + (radii[i + 1] - radii[i]) * t, stiff)
    ball(Vector(points[-1]), radii[-1], stiff)


# Torso: chest, ribcage, waist, hips. Slightly deeper chest, tucked waist.
ell((0, -0.105, 0.205), 0.062, 0.085, 0.075)
ell((0, -0.03, 0.205), 0.060, 0.09, 0.066)
ell((0, 0.06, 0.205), 0.056, 0.08, 0.060)
ell((0, 0.145, 0.212), 0.062, 0.075, 0.066)
ell((0, -0.15, 0.17), 0.045, 0.05, 0.055)        # chest ruff / sternum
# Neck and head (cats have big round heads, wide cheeks, a short muzzle with whisker pads)
ell((0, -0.185, 0.25), 0.042, 0.05, 0.05, rot=None)
ell((0, -0.262, 0.304), 0.062, 0.056, 0.056)      # cranium
ell((0.032, -0.288, 0.284), 0.036, 0.03, 0.03)    # cheeks
ell((-0.032, -0.288, 0.284), 0.036, 0.03, 0.03)
ell((0.011, -0.318, 0.281), 0.015, 0.014, 0.013)  # whisker pads
ell((-0.011, -0.318, 0.281), 0.015, 0.014, 0.013)
ell((0, -0.316, 0.292), 0.012, 0.016, 0.012)      # nose bridge
ell((0, -0.307, 0.268), 0.017, 0.016, 0.011)      # chin
ell((0, -0.292, 0.322), 0.036, 0.028, 0.02)       # brow
# Front legs (shoulder -> elbow -> wrist -> paw)
for sx in (1, -1):
    x = 0.042 * sx
    chain([(x, -0.115, 0.195), (x * 1.05, -0.125, 0.12), (x * 1.0, -0.128, 0.05), (x * 0.98, -0.138, 0.018)],
          [0.03, 0.021, 0.016, 0.016], steps=5)
    ell((x * 0.98, -0.148, 0.014), 0.018, 0.022, 0.012)   # paw
# Hind legs (hip -> knee -> hock -> paw), Z-shaped
for sx in (1, -1):
    x = 0.047 * sx
    chain([(x, 0.15, 0.2), (x * 1.06, 0.11, 0.125), (x, 0.175, 0.062), (x * 0.98, 0.165, 0.018)],
          [0.042, 0.025, 0.015, 0.015], steps=5)
    ell((x, 0.15, 0.16), 0.036, 0.05, 0.05)                 # haunch
    ell((x * 0.98, 0.155, 0.014), 0.017, 0.024, 0.012)      # paw
# Tail: up and back in a soft S
tail_pts = [(0, 0.2, 0.235), (0, 0.255, 0.255), (0, 0.3, 0.29), (0, 0.335, 0.34), (0, 0.355, 0.4), (0, 0.36, 0.455)]
chain(tail_pts, [0.022, 0.018, 0.017, 0.016, 0.015, 0.012], stiff=2.6, steps=4)

dg = bpy.context.evaluated_depsgraph_get()
body_me = bpy.data.meshes.new_from_object(mbo.evaluated_get(dg))
bpy.data.objects.remove(mbo, do_unlink=True)
body = bpy.data.objects.new("CatBody", body_me)
scene.collection.objects.link(body)

# Voxel remesh for even topology, then a light smooth and decimate.
rm = body.modifiers.new("remesh", 'REMESH')
rm.mode = 'VOXEL'
rm.voxel_size = 0.0042
rm.use_smooth_shade = True
sm = body.modifiers.new("smooth", 'SMOOTH')
sm.factor = 0.6
sm.iterations = 4
dc = body.modifiers.new("decimate", 'DECIMATE')
dc.ratio = 0.45
dg = bpy.context.evaluated_depsgraph_get()
me = bpy.data.meshes.new_from_object(body.evaluated_get(dg))
body.modifiers.clear()
body.data = me
print("CAT body tris", sum(len(p.vertices) - 2 for p in me.polygons))

bm = bmesh.new()
bm.from_mesh(me)
for f in bm.faces:
    f.material_index = 0
    f.smooth = True


# Ears: curved, thin cones with a hollow front, merged as separate islands.
def add_ear(sx):
    ear = bmesh.new()
    seg, rings = 18, 7
    base_r, height = 0.029, 0.044
    verts = []
    for j in range(rings + 1):
        t = j / rings
        ring = []
        # rounded-triangle profile: wide base, soft convex sides, slightly rounded tip
        r = base_r * (1 - t ** 1.15) * (1 - 0.18 * math.sin(math.pi * t)) + 0.002
        for i in range(seg):
            a = 2 * math.pi * i / seg
            x = math.cos(a) * r
            y = math.sin(a) * r * 0.42
            # hollow cup on the front face (-Y), thin rim
            if math.sin(a) < 0:
                y *= 0.15
            ring.append(ear.verts.new((x, y + 0.004 * t, t * height)))
        verts.append(ring)
    for j in range(rings):
        for i in range(seg):
            a, b = verts[j][i], verts[j][(i + 1) % seg]
            c, d = verts[j + 1][(i + 1) % seg], verts[j + 1][i]
            ear.faces.new((a, b, c, d))
    ear.faces.new(list(reversed(verts[0])))
    tip = ear.verts.new((0, 0.005, height + 0.004))
    for i in range(seg):
        ear.faces.new((verts[rings][i], verts[rings][(i + 1) % seg], tip))
    m = Matrix.Translation((0.037 * sx, -0.255, 0.334)) @ Matrix.Rotation(math.radians(-20 * sx), 4, 'Y') @ \
        Matrix.Rotation(math.radians(-8), 4, 'X') @ Matrix.Rotation(math.radians(14 * sx), 4, 'Z')
    bmesh.ops.transform(ear, matrix=m, verts=ear.verts)
    tmp = bpy.data.meshes.new("ear")
    ear.to_mesh(tmp)
    bm.from_mesh(tmp)
    bpy.data.meshes.remove(tmp)


add_ear(1)
add_ear(-1)

# Eyes: material slot 1. Placed on the actual face surface (ray cast from the front), slightly sunk in.
from mathutils.bvhtree import BVHTree  # noqa: E402
_bvh = BVHTree.FromBMesh(bm)
EYE_R = 0.0125
EYE_CENTERS = {}
for sx in (1, -1):
    hit, nrm, _, _ = _bvh.ray_cast(Vector((0.026 * sx, -0.6, 0.31)), Vector((0, 1, 0)))
    if hit is None:
        hit, nrm = Vector((0.026 * sx, -0.318, 0.31)), Vector((0, -1, 0))
    EYE_CENTERS[sx] = hit + Vector((0, 0.0068, 0))
    print("CAT eye", sx, tuple(round(c, 4) for c in EYE_CENTERS[sx]))
for sx in (1, -1):
    eye = bmesh.new()
    bmesh.ops.create_uvsphere(eye, u_segments=20, v_segments=14, radius=EYE_R)
    for v in eye.verts:
        v.co.y *= 0.8
    m = Matrix.Translation(EYE_CENTERS[sx]) @ Matrix.Rotation(math.radians(22 * sx), 4, 'Z') @ Matrix.Rotation(math.radians(8), 4, 'X')
    bmesh.ops.transform(eye, matrix=m, verts=eye.verts)
    # UVs: planar projection from the front so the iris/pupil maps onto the visible face of the eye.
    tmp = bpy.data.meshes.new("eye")
    eye.to_mesh(tmp)
    for p in tmp.polygons:
        p.material_index = 1
        p.use_smooth = True
    bm.from_mesh(tmp)
    bpy.data.meshes.remove(tmp)

bm.to_mesh(me)
bm.free()
me.materials.clear()
for n in ("M_CatBody", "M_CatEye"):
    me.materials.append(bpy.data.materials.new(n))

# Front-projected UVs (eyes use them; body uses a box-ish projection for fur detail).
uv = me.uv_layers.new(name="UVMap")
for poly in me.polygons:
    for li in poly.loop_indices:
        co = me.vertices[me.loops[li].vertex_index].co
        if poly.material_index == 1:
            ec = EYE_CENTERS[1 if co.x > 0 else -1]
            uv.data[li].uv = ((co.x - ec.x) / (2.2 * EYE_R) + 0.5, (co.z - ec.z) / (2.2 * EYE_R) + 0.5)
        else:
            n = poly.normal
            if abs(n.x) > max(abs(n.y), abs(n.z)):
                uv.data[li].uv = (co.y * 8, co.z * 8)
            elif abs(n.y) > abs(n.z):
                uv.data[li].uv = (co.x * 8, co.z * 8)
            else:
                uv.data[li].uv = (co.x * 8, co.y * 8)

# ---------------------------------------------------------------------------------------------------------------------
# Tabby vertex colours (linear values; exported as sRGB bytes)
# ---------------------------------------------------------------------------------------------------------------------
def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


GINGER = Vector((0.93, 0.52, 0.22))
DARK = Vector((0.62, 0.27, 0.085))
CREAM = Vector((0.98, 0.93, 0.84))
PINK = Vector((0.93, 0.55, 0.55))

col = me.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
# Eyeballs are painted pure blue so one material section can shade both fur and eyes (see M_CatFur / M_ShadeBody).
EYE_VERTS = {vi for p in me.polygons if p.material_index == 1 for vi in p.vertices}
for v in me.vertices:
    p = v.co
    c = GINGER.copy()
    # tabby stripes: across the back/flanks (rings around the body axis), rings on legs and tail
    n = noise.noise(Vector((p.x * 18, p.y * 18, p.z * 18)))
    if p.y > 0.19 and p.z > 0.2:   # tail rings
        s = math.sin((p.y * 0.6 + p.z) * 95 + n * 1.5)
    elif p.z < 0.16:                # leg rings
        s = math.sin(p.z * 120 + n * 2.0)
    elif p.y < -0.22:               # forehead "M" + cheek lines
        s = math.sin(p.x * 160 + n * 2.0) * smoothstep(0.30, 0.34, p.z) + math.sin(p.z * 110 + abs(p.x) * 60) * (1 - smoothstep(0.29, 0.31, p.z)) * 0.8
    else:
        s = math.sin(p.y * 70 + abs(p.x) * 25 + n * 2.2)
    stripe = smoothstep(0.35, 0.8, s)
    top = smoothstep(0.17, 0.25, p.z)          # stripes strongest on the back
    c = c.lerp(DARK, stripe * (0.45 + 0.4 * top))
    # cream chest/belly, muzzle, paws, tail tip underside
    chest = smoothstep(0.03, 0.0, abs(p.x) - 0.02) * smoothstep(0.2, 0.15, p.z) * smoothstep(0.05, -0.12, p.y)
    belly = smoothstep(0.17, 0.13, p.z) * smoothstep(0.05, 0.02, abs(p.x)) * smoothstep(0.17, 0.1, p.y)
    muzzle = smoothstep(-0.295, -0.315, p.y) * smoothstep(0.302, 0.285, p.z)
    paws = smoothstep(0.045, 0.02, p.z)
    white = max(chest, belly, muzzle, paws)
    c = c.lerp(CREAM, white)
    # pink nose tip + inner ears
    nose = smoothstep(0.012, 0.004, (p - Vector((0, -0.341, 0.292))).length)
    c = c.lerp(PINK, nose)
    if p.z > 0.345 and abs(p.x) > 0.02 and p.y < -0.245:
        c = c.lerp(PINK, 0.55)
    col.data[v.index].color = (0.0, 0.0, 1.0, 1.0) if v.index in EYE_VERTS else (c.x, c.y, c.z, 1.0)

# ---------------------------------------------------------------------------------------------------------------------
# Armature
# ---------------------------------------------------------------------------------------------------------------------
arm_data = bpy.data.armatures.new("Armature")
arm = bpy.data.objects.new("Armature", arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')

BONES = []  # (name, head, tail, parent, connected)


def bone(name, head, tail, parent=None, connect=False):
    BONES.append((name, Vector(head), Vector(tail), parent, connect))


bone("root", (0, 0, 0), (0, 0, 0.06))
bone("pelvis", (0, 0.15, 0.21), (0, 0.06, 0.208), "root")
bone("spine_01", (0, 0.06, 0.208), (0, -0.03, 0.21), "pelvis", True)
bone("spine_02", (0, -0.03, 0.21), (0, -0.12, 0.215), "spine_01", True)
bone("neck", (0, -0.14, 0.23), (0, -0.215, 0.275), "spine_02")
bone("head", (0, -0.215, 0.275), (0, -0.33, 0.30), "neck", True)
for s, sx in (("l", 1), ("r", -1)):
    bone(f"ear_{s}", (0.036 * sx, -0.252, 0.34), (0.045 * sx, -0.26, 0.395), "head")
    bone(f"upperarm_{s}", (0.042 * sx, -0.115, 0.19), (0.044 * sx, -0.125, 0.12), "spine_02")
    bone(f"forearm_{s}", (0.044 * sx, -0.125, 0.12), (0.042 * sx, -0.128, 0.04), f"upperarm_{s}", True)
    bone(f"hand_{s}", (0.042 * sx, -0.128, 0.04), (0.041 * sx, -0.158, 0.012), f"forearm_{s}", True)
    bone(f"thigh_{s}", (0.047 * sx, 0.15, 0.195), (0.05 * sx, 0.11, 0.125), "pelvis")
    bone(f"calf_{s}", (0.05 * sx, 0.11, 0.125), (0.047 * sx, 0.175, 0.06), f"thigh_{s}", True)
    bone(f"foot_{s}", (0.047 * sx, 0.175, 0.06), (0.046 * sx, 0.15, 0.012), f"calf_{s}", True)
for i in range(len(tail_pts) - 1):
    bone(f"tail_{i + 1:02d}", tail_pts[i], tail_pts[i + 1], "pelvis" if i == 0 else f"tail_{i:02d}", i > 0)

eb = {}
for name, h, t, parent, connect in BONES:
    b = arm_data.edit_bones.new(name)
    b.head, b.tail = h, t
    b.roll = 0.0
    if parent:
        b.parent = eb[parent]
        b.use_connect = connect
    eb[name] = b
bpy.ops.object.mode_set(mode='OBJECT')

# ---------------------------------------------------------------------------------------------------------------------
# Skin weights: inverse-distance to bone segments, gated by anatomy so limbs/tail never bleed across.
# ---------------------------------------------------------------------------------------------------------------------
segs = {name: (h, t) for name, h, t, _, _ in BONES if name != "root"}


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length


def allowed(name, p, is_ear, is_eye):
    """Soft anatomical gate in [0,1] (hard gates crease the skin at the joints)."""
    side = 'l' if p.x > 0 else 'r'
    if is_eye:
        return 1.0 if name == "head" else 0.0
    if is_ear:
        return 1.0 if name == f"ear_{side}" else (0.15 if name == "head" else 0.0)
    if name.startswith("ear_"):
        return 0.0
    f = 1.0
    if name.endswith(("_l", "_r")):
        s = 1.0 if name.endswith("_l") else -1.0
        f *= smoothstep(-0.012, 0.006, p.x * s)
    if name.startswith(("upperarm", "forearm", "hand")):
        f *= smoothstep(-0.015, -0.06, p.y) * smoothstep(0.245, 0.195, p.z)
    if name.startswith(("thigh", "calf", "foot")):
        f *= smoothstep(0.045, 0.1, p.y) * smoothstep(0.26, 0.2, p.z)
    if name.startswith("tail"):
        f *= smoothstep(0.17, 0.205, p.y)
    if name in ("neck", "head"):
        f *= smoothstep(-0.08, -0.13, p.y)
    return f


# Identify islands (ears / eyes) by connectivity.
bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()
island = [-1] * len(bm.verts)
iid = 0
for v in bm.verts:
    if island[v.index] >= 0:
        continue
    stack = [v]
    island[v.index] = iid
    while stack:
        x = stack.pop()
        for e in x.link_edges:
            o = e.other_vert(x)
            if island[o.index] < 0:
                island[o.index] = iid
                stack.append(o)
    iid += 1
sizes = {}
for i in island:
    sizes[i] = sizes.get(i, 0) + 1
main_island = max(sizes, key=sizes.get)
eye_verts = set()
for f in bm.faces:
    if f.material_index == 1:
        for v in f.verts:
            eye_verts.add(v.index)
bm.free()

groups = {name: body.vertex_groups.new(name=name) for name in segs}
for v in me.vertices:
    p = v.co
    is_eye = v.index in eye_verts
    is_ear = (island[v.index] != main_island) and not is_eye
    ws = []
    for name, (a, b) in segs.items():
        g = allowed(name, p, is_ear, is_eye)
        if g <= 1e-4:
            continue
        d = seg_dist(p, a, b)
        ws.append((g / (d + 0.006) ** 4, name))
    if not ws:
        ws = [(1.0, "pelvis")]
    ws.sort(reverse=True)
    ws = ws[:3]
    tot = sum(w for w, _ in ws)
    for w, name in ws:
        groups[name].add([v.index], w / tot, 'REPLACE')

body.parent = arm
mod = body.modifiers.new("Armature", 'ARMATURE')
mod.object = arm

# ---------------------------------------------------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(OUT_DIR, "SK_Cat.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                         bake_anim=False, mesh_smooth_type='FACE', apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL',
                         use_armature_deform_only=False, primary_bone_axis='Y', secondary_bone_axis='X', colors_type='SRGB',
                         armature_nodetype='NULL')
print("CAT exported", fbx, "verts", len(me.vertices))

# ---------------------------------------------------------------------------------------------------------------------
# Preview renders (Workbench, vertex colours) so the shape can be checked without Unreal.
# ---------------------------------------------------------------------------------------------------------------------
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'VERTEX'
scene.display.shading.light = 'STUDIO'
scene.render.resolution_x = 900
scene.render.resolution_y = 600
scene.render.film_transparent = False
world = bpy.data.worlds.new("W")
scene.world = world
cam_data = bpy.data.cameras.new("Cam")
cam_data.lens = 70
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
target = Vector((0, 0.02, 0.2))
for label, pos in (("side", (1.4, 0.02, 0.32)), ("front34", (0.75, -1.05, 0.45)), ("back34", (-0.8, 1.0, 0.55)), ("top", (0.01, 0.02, 1.6))):
    cam.location = pos
    d = target - Vector(pos)
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(PREVIEW_DIR, f"cat_{label}.png")
    bpy.ops.render.render(write_still=True)

# Posed check: bend a few bones to verify weights deform cleanly.
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='POSE')
pb = arm.pose.bones
pb["thigh_l"].rotation_mode = 'XYZ'
pb["thigh_l"].rotation_euler = (math.radians(35), 0, 0)
pb["upperarm_r"].rotation_mode = 'XYZ'
pb["upperarm_r"].rotation_euler = (math.radians(-40), 0, 0)
pb["head"].rotation_mode = 'XYZ'
pb["head"].rotation_euler = (math.radians(-20), 0, math.radians(25))
for i in range(1, 6):
    pb[f"tail_{i:02d}"].rotation_mode = 'XYZ'
    pb[f"tail_{i:02d}"].rotation_euler = (0, 0, math.radians(18))
bpy.ops.object.mode_set(mode='OBJECT')
cam.location = (1.4, 0.02, 0.32)
cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.render.filepath = os.path.join(PREVIEW_DIR, "cat_posed.png")
bpy.ops.render.render(write_still=True)
print("CAT previews in", PREVIEW_DIR)
