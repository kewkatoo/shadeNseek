"""Stage D - procedural meshes referenced by C++ and reusable set pieces (all original GeometryScript geometry).

C++-referenced: SM_WispQuad, SM_LemonCart, SM_CartWheel, SM_AwningRoller, SM_AwningCanvas, SM_AwningValance, SM_AwningArm,
                SM_Cord, SM_CordHandle, SM_CatCollar
Set pieces:     SM_Railing_200, SM_Card, SM_BougainClump_*, SM_LeafyClump, SM_Sheet_*, SM_Towel_*, SM_Rope_100, SM_Dome,
                SM_Campanile, SM_Bell, SM_Bollard, SM_Bench_Stone, SM_Planter_Long, SM_Beam_100, SM_Post_100, SM_LemonCluster
"""
import math
import os
import random
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402
from sngeo import (GE, GT, GQ, T, V, append, bevel, box, cyl, cone, sphere, revolve, extrude, sweep, circle, arch_poly,  # noqa: E402
                   subtract, save, mat, new, log, eal)

R = random.Random(7)
GA = unreal.GeometryScript_AssetUtils
GMat = unreal.GeometryScript_Materials
OUT = "/Game/Shade/Meshes"


def prop_mesh(mid, variant=None):
    name = f"SM_{mid}" if variant is None else f"SM_{mid}_V{variant}"
    p = f"/Game/Shade/Props/{mid}/{name}"
    return eal.load_asset(p) if eal.does_asset_exist(p) else None


def copy_static(sm):
    m = unreal.DynamicMesh()
    GA.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    return m


def with_material_offset(m, offset, count):
    GMat.enable_material_i_ds(m)
    for i in reversed(range(count)):
        GMat.remap_material_i_ds(m, i, i + offset)
    return m


# ---------------------------------------------------------------------------------------------------------------------
def wisp_quad():
    m = new()
    unreal.GeometryScript_Primitives.append_rectangle_xy(m, unreal.GeometryScriptPrimitiveOptions(), T(), 100.0, 100.0, 0, 0)
    GT.transform_mesh(m, T(rot=(90, 0, 0)))  # XY plane -> YZ plane (normal +X); keeps the rectangle's 0..1 UVs
    save(m, "SM_WispQuad", [mat("M_Wisp")], collision=None, uvs=False)


def cart_wheel():
    m = new()
    wood = 0
    rim = new()
    ro = unreal.GeometryScriptRevolveOptions()
    unreal.GeometryScript_Primitives.append_torus(rim, unreal.GeometryScriptPrimitiveOptions(), T(), ro, 20.0, 2.6, 28, 8, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    GT.transform_mesh(rim, T(rot=(0, 0, 90)))
    append(m, rim)
    append(m, cyl(5.0, 9.0, wood, (0, -4.5, 0), (0, 0, 90), steps=14))
    for k in range(8):
        a = k * 45.0
        append(m, box(1.8, 2.2, 19.0, wood, (0, 0, 0), (a, 0, 0), origin=unreal.GeometryScriptPrimitiveOriginMode.BASE))
    iron = new()
    unreal.GeometryScript_Primitives.append_torus(iron, unreal.GeometryScriptPrimitiveOptions(), T(), ro, 22.2, 1.0, 32, 6, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    GT.transform_mesh(iron, T(rot=(0, 0, 90)))
    GMat.enable_material_i_ds(iron)
    GMat.remap_material_i_ds(iron, 0, 1)
    append(m, iron)
    save(m, "SM_CartWheel", [mat("MI_Wood_Beam"), mat("MI_Iron_Black")], collision=None, uv_tile=60.0)


def lemon_cart():
    """Vendor's cart, 200 x 80, bed at 38 cm, three crates of lemons. Slots: 0 crate wood, 1 cart paint, 2 sign, 3 lemons."""
    m = new()
    paint, wood, sign = 1, 0, 2
    # bed + sideboards
    append(m, box(200, 82, 6, paint, (0, 0, 40), bev=1.0))
    for y in (-40, 40):
        append(m, box(200, 4, 20, paint, (0, y, 52), bev=0.8))
    for x in (-98, 98):
        append(m, box(4, 82, 20, paint, (x, 0, 52), bev=0.8))
    # frame, axle stand, handles reaching back (-X) for the vendor
    append(m, box(14, 78, 8, paint, (-10, 0, 30)))
    for y in (-30, 30):
        append(m, box(70, 5, 5, paint, (-125, y, 52), (8, 0, 0)))
    append(m, cyl(2.5, 66, paint, (-158, -33, 57), (0, 0, 90)))
    append(m, box(5, 5, 36, paint, (80, -34, 20)))
    append(m, box(5, 5, 36, paint, (80, 34, 20)))
    # Limoni board on the street side (+Y faces the lane when the cart sits against the sea-side wall... both sides)
    append(m, box(110, 1.2, 18, sign, (10, -42.5, 52)))
    append(m, box(110, 1.2, 18, sign, (10, 42.5, 52)))

    # crates: two on the bed, one stacked
    def crate(cx, cy, cz, rot=0.0):
        c = new()
        w, d, h = 58, 42, 28
        for k in range(3):
            z = 3 + k * 9
            append(c, box(w, 2, 7, wood, (0, -d / 2 + 1, z + 3.5), bev=0.4))
            append(c, box(w, 2, 7, wood, (0, d / 2 - 1, z + 3.5), bev=0.4))
            append(c, box(2, d, 7, wood, (-w / 2 + 1, 0, z + 3.5), bev=0.4))
            append(c, box(2, d, 7, wood, (w / 2 - 1, 0, z + 3.5), bev=0.4))
        append(c, box(w, d, 2, wood, (0, 0, 1)))
        for x in (-w / 2 + 2, w / 2 - 2):
            for y in (-d / 2 + 2, d / 2 - 2):
                append(c, box(4, 4, h, wood, (x, y, h / 2)))
        GT.transform_mesh(c, T((cx, cy, cz), (0, rot, 0)))
        return c

    tops = []
    # A heavy load: three crates on the bed, two stacked on top, so the cart throws a long, generous shadow.
    for (cx, cy, cz, r) in ((-64, 2, 43, 3.0), (0, -3, 43, -4.0), (64, 2, 43, 2.0), (-33, 0, 72, 8.0), (33, 1, 72, -6.0)):
        append(m, crate(cx, cy, cz, r))
        tops.append((cx, cy, cz + 18))
    # lemons heaped in each crate (real scanned lemon geometry)
    lemon = prop_mesh("lemon")
    lm = copy_static(lemon) if lemon else sphere(4.0)
    lb_min, lb_max = (GQ.get_mesh_bounding_box(lm).min, GQ.get_mesh_bounding_box(lm).max)
    lsize = max(lb_max.x - lb_min.x, lb_max.y - lb_min.y, lb_max.z - lb_min.z)
    scale = 7.5 / max(lsize, 0.01)
    with_material_offset(lm, 3, 1)
    for (cx, cy, cz) in tops:
        for k in range(16):
            px = cx + R.uniform(-24, 24)
            py = cy + R.uniform(-16, 16)
            pz = cz + R.uniform(-2, 6) + (6 if abs(px - cx) < 12 and abs(py - cy) < 8 else 0)
            s = scale * R.uniform(0.85, 1.1)
            GE.append_mesh(m, lm, T((px, py, pz), (R.uniform(0, 360), R.uniform(0, 360), R.uniform(0, 360)), (s, s, s)))
    lemon_mat = None
    if lemon:
        sm = lemon.get_editor_property("static_materials")
        lemon_mat = sm[0].get_editor_property("material_interface") if sm else None
    save(m, "SM_LemonCart", [mat("MI_Wood_Crate"), mat("MI_Shutter_Teal"), mat("MI_Sign_Limoni"), lemon_mat or mat("MI_Lemon_Skin")], uv_tile=120.0)


def lemon_cluster():
    """Loose lemons for baskets, tables and the potted tree."""
    lemon = prop_mesh("lemon")
    if not lemon:
        return
    lm = copy_static(lemon)
    bb = GQ.get_mesh_bounding_box(lm)
    sc = 7.5 / max(bb.max.x - bb.min.x, 0.01)
    m = new()
    for k in range(7):
        a = k * 2 * math.pi / 7
        s = sc * R.uniform(0.9, 1.1)
        GE.append_mesh(m, lm, T((math.cos(a) * 7 * (k > 0), math.sin(a) * 7 * (k > 0), 0 if k else 4), (R.uniform(0, 360), R.uniform(0, 360), 0), (s, s, s)))
    sm = lemon.get_editor_property("static_materials")
    save(m, "SM_LemonCluster", [sm[0].get_editor_property("material_interface")], collision=None, uvs=False)


def awning_parts():
    m = box(22, 100, 22, 0, (0, 0, 0), bev=3.0)
    append(m, box(26, 4, 26, 1, (0, -51, 0)))
    append(m, box(26, 4, 26, 1, (0, 51, 0)))
    save(m, "SM_AwningRoller", [mat("MI_Paint_Cream"), mat("MI_Iron_Black")], collision="complex", uv_tile=100.0)
    # Canvas: 100 deep (+X) x 100 wide (Y centred), gentle sag.
    c = new()
    for i in range(10):
        x = i * 10 + 5
        sag = math.sin(math.pi * (i + 0.5) / 10) * 1.2
        append(c, box(10.4, 100, 1.6, 0, (x, 0, -sag)))
    save(c, "SM_AwningCanvas", [mat("MI_Awning_Terracotta")], collision="complex", uv_tile=100.0)
    # Valance: scalloped flap hanging from the front bar.
    v = box(4, 100, 4, 1, (0, 0, 0))
    append(v, box(1.2, 100, 16, 0, (0, 0, -8)))
    for k in range(10):
        y = -45 + k * 10
        append(v, cyl(5.0, 1.2, 0, (-0.6, y, -16), (0, 0, 0), steps=14) if False else cyl(5.0, 1.2, 0, (0.6, y, -16), (0, 90, 90), steps=14))
    save(v, "SM_AwningValance", [mat("MI_Awning_Terracotta"), mat("MI_Iron_Black")], collision="complex", uv_tile=100.0)
    a = cyl(2.2, 100, 0, (0, 0, 0), (-90, 0, 0), steps=10)
    save(a, "SM_AwningArm", [mat("MI_Iron_Black")], collision=None)
    cord = cyl(0.7, 100, 0, (0, 0, 0), (-90, 0, 0), steps=8)
    save(cord, "SM_Cord", [mat("MI_Linen_White")], collision=None)
    # Wooden toggle the cord is tied to: a turned bar across Y (centred, 12 long, ~1.8 radius where the paws close round
    # it), the cord wrapped round its middle and knotted on top. The cord leaves the knot at z = 2.6 (GCordKnot in
    # ShadeInteractables.cpp); the toggle's local Z is turned to hang along the cord. Roll 90 turns +Z into +Y, so a
    # piece built up from its base starts at -half to come out centred.
    h = revolve([(1.15, 0), (1.5, 0.5), (1.75, 1.6), (1.95, 4.0), (2.05, 6.0), (1.95, 8.0), (1.75, 10.4), (1.5, 11.5), (1.15, 12)],
                0, (0, -6, 0), (0, 0, 90), steps=14)
    append(h, cyl(2.35, 2.2, 1, (0, -1.1, 0), (0, 0, 90), steps=14))
    append(h, sphere(1.25, 1, (0, 0, 2.3)))
    save(h, "SM_CordHandle", [mat("MI_Wood_Beam"), mat("MI_Linen_White")], collision=None)


def cat_collar():
    """Biscotto's collar: a red leather band round X (unit ring, radius 10; ShadeInteractables.cpp fits it to the neck
    bones every frame) with a small brass bell on a loop at the throat (-Z)."""
    m = new()
    ro = unreal.GeometryScriptRevolveOptions()
    unreal.GeometryScript_Primitives.append_torus(m, unreal.GeometryScriptPrimitiveOptions(), T(), ro, 10.0, 0.9, 40, 8, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    GT.transform_mesh(m, T(rot=(90, 0, 0), scale=(1.0, 1.0, 1.9)))   # flatten the tube into a band, axis -> X
    brass = new()
    unreal.GeometryScript_Primitives.append_torus(brass, unreal.GeometryScriptPrimitiveOptions(), T(), ro, 1.1, 0.35, 12, 6, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    GT.transform_mesh(brass, T((0, 0, -11.2), (0, 90, 90)))          # loop hanging off the band
    append(brass, sphere(2.3, 0, (0, 0, -14.4), steps=12))          # the bell
    append(brass, cyl(0.5, 0.9, 0, (0, 0, -17.1), steps=8))          # clapper peeking out of the slot
    GMat.enable_material_i_ds(brass)
    GMat.remap_material_i_ds(brass, 0, 1)
    append(m, brass)
    save(m, "SM_CatCollar", [mat("MI_Leather_Red"), mat("MI_Brass")], collision=None, uv_tile=20.0)


def railing():
    m = new()
    append(m, box(200, 5, 4, 0, (100, 0, 98)))           # top rail
    append(m, box(200, 3, 3, 0, (100, 0, 12)))           # bottom rail
    for k in range(0, 21):
        x = k * 10
        append(m, box(1.6, 1.6, 86, 0, (x, 0, 55)))
    for x in (0, 200):
        append(m, box(5, 5, 100, 0, (x, 0, 50)))
    # little curls every metre
    for x in (50, 150):
        t = new()
        unreal.GeometryScript_Primitives.append_torus(t, unreal.GeometryScriptPrimitiveOptions(), T(), unreal.GeometryScriptRevolveOptions(), 7.0, 0.8, 16, 4, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        GT.transform_mesh(t, T((x, 0, 88), (0, 0, 90)))
        append(m, t)
    save(m, "SM_Railing_200", [mat("MI_Iron_Black")], collision=None, uv_tile=100.0)
    b = new()
    append(b, box(160, 4, 4, 0, (0, 0, 95)))
    append(b, box(160, 3, 3, 0, (0, 0, 8)))
    for k in range(17):
        append(b, box(1.5, 1.5, 88, 0, (-80 + k * 10, 0, 51)))
    save(b, "SM_BalconyRail_160", [mat("MI_Iron_Green")], collision=None, uv_tile=100.0)


def cards():
    # One card: 100x100 quad facing +X, centred.
    q = new()
    unreal.GeometryScript_Primitives.append_rectangle_xy(q, unreal.GeometryScriptPrimitiveOptions(), T(), 100.0, 100.0, 1, 1)
    GT.transform_mesh(q, T(rot=(90, 0, 0)))
    save(q, "SM_Card", [mat("MI_Bougain")], collision=None, uvs=False)

    def clump(name, material, n, spread, seed):
        rr = random.Random(seed)
        m = new()
        for k in range(n):
            c = new()
            s = rr.uniform(0.7, 1.15)
            # append_rectangle_xy gives each card its own 0..1 UVs, so every card shows the whole spray.
            unreal.GeometryScript_Primitives.append_rectangle_xy(c, unreal.GeometryScriptPrimitiveOptions(), T(), 100.0 * s, 100.0 * s, 1, 1)
            GT.transform_mesh(c, T((rr.uniform(-spread, spread), rr.uniform(-spread, spread) * 0.5, rr.uniform(-spread * 0.4, spread * 0.6)),
                                   (rr.uniform(60, 120), rr.uniform(0, 360), rr.uniform(-30, 30))))
            append(m, c)
        save(m, name, [mat(material)], collision=None, uvs=False)
    clump("SM_BougainClump_A", "MI_Bougain", 9, 45, 1)
    clump("SM_BougainClump_B", "MI_Bougain_Deep", 12, 70, 2)
    clump("SM_BougainDrape", "MI_Bougain", 16, 90, 3)
    clump("SM_LeafyClump", "MI_Leafy", 10, 55, 4)


def laundry():
    def sheet(name, w, h, material, waves=2):
        m = new()
        cols, rows = 8, 8
        for i in range(cols):
            for j in range(rows):
                x0 = -w / 2 + i * w / cols
                z0 = -j * h / rows
                wav = math.sin((i / cols) * math.pi * waves) * 3.0 + j * 0.4
                append(m, box(w / cols + 0.2, 0.8, h / rows + 0.2, 0, (x0 + w / cols / 2, wav, z0 - h / rows / 2)))
        save(m, name, [mat(material)], collision=None, uv_tile=max(w, h))
    sheet("SM_Sheet_White", 110, 130, "MI_Linen_White")
    sheet("SM_Sheet_Blue", 110, 120, "MI_Linen_BlueStripe")
    sheet("SM_Sheet_Lemon", 95, 110, "MI_Linen_Lemon")
    sheet("SM_Towel_Coral", 55, 80, "MI_Linen_Coral", 1)
    sheet("SM_Towel_Sea", 50, 75, "MI_Linen_Sea", 1)
    sheet("SM_Towel_Terry", 50, 70, "MI_Towel_Terry", 1)
    rope = cyl(0.6, 100, 0, (0, 0, 0), (-90, 0, 0), steps=6)
    save(rope, "SM_Rope_100", [mat("MI_Linen_White")], collision=None)


def dome():
    """Church dome: drum + majolica shell + lantern. Pivot at drum base. Slots: 0 drum wall, 1 majolica, 2 trim."""
    m = new()
    append(m, cyl(320, 160, 0, (0, 0, 0), steps=48))
    append(m, cyl(335, 18, 2, (0, 0, 158), steps=48))
    shell = revolve([(318, 0)] + [(318 * math.cos(a), 290 * math.sin(a)) for a in [i * math.pi / 2 / 16 for i in range(1, 17)]] + [(1, 290)], 1, (0, 0, 175), steps=48)
    append(m, shell)
    append(m, cyl(45, 90, 0, (0, 0, 455), steps=16))
    append(m, revolve([(52, 0), (40, 25), (10, 60), (2, 80)], 1, (0, 0, 545), steps=16))
    append(m, cyl(3, 60, 2, (0, 0, 620), steps=6))
    save(m, "SM_Dome", [mat("MI_Wall_Cream"), mat("MI_Majolica"), mat("MI_Trim_White")], nanite=True, uv_tile=220.0)


def campanile():
    """Bell tower 150 x 150 x 2300 (from z = 0), arched belfry, majolica cap. Pivot at base centre."""
    m = box(150, 150, 1850, 0, (0, 0, 925), bev=3.0)
    # belfry openings on all four sides
    for (dx, dy, yaw) in ((0, -75, 0), (0, 75, 0), (-75, 0, 90), (75, 0, 90)):
        tool = extrude(arch_poly(70, 190, 130), 60, 0)
        GT.transform_mesh(tool, T(rot=(90, 0, 0)))
        GT.transform_mesh(tool, T((0, 30, 0), (0, 0, 0)))
        GT.transform_mesh(tool, T((dx, dy, 1590), (0, yaw, 0)))
        subtract(m, tool)
    for z in (1560, 1810):
        append(m, box(168, 168, 16, 1, (0, 0, z)))
    append(m, box(160, 160, 30, 1, (0, 0, 1865)))
    append(m, revolve([(95, 0), (85, 60), (55, 150), (10, 230), (1, 250)], 2, (0, 0, 1880), steps=4))
    append(m, cyl(3, 70, 1, (0, 0, 2130), steps=6))
    # clock face (south)
    append(m, cyl(36, 4, 1, (0, 77, 1350), (0, 0, 90), steps=24))
    save(m, "SM_Campanile", [mat("MI_Wall_Cream"), mat("MI_Trim_White"), mat("MI_Majolica")], nanite=True, uv_tile=220.0)
    bell = revolve([(30, 0), (28, 6), (22, 22), (18, 40), (14, 50), (2, 54)], 0, steps=24)
    save(bell, "SM_Bell", [mat("MI_Iron_Black")], collision=None)


def street_bits():
    save(cyl(14, 55, 0, steps=16), "SM_Bollard", [mat("MI_Wall_Stone")], uv_tile=100.0)
    b = box(160, 48, 8, 0, (0, 0, 42), bev=2.0)
    append(b, box(20, 40, 38, 0, (-62, 0, 19), bev=2.0))
    append(b, box(20, 40, 38, 0, (62, 0, 19), bev=2.0))
    save(b, "SM_Bench_Stone", [mat("MI_StepStone")], uv_tile=100.0)
    p = box(120, 50, 70, 0, (0, 0, 35), bev=3.0)
    append(p, box(108, 40, 6, 1, (0, 0, 66)))
    save(p, "SM_Planter_Long", [mat("MI_Wall_Stacked"), mat("MI_Sand")], uv_tile=100.0)
    save(box(100, 14, 14, 0, (50, 0, 0), bev=1.5), "SM_Beam_100", [mat("MI_Wood_Beam")], uv_tile=100.0)
    save(box(14, 14, 100, 0, (0, 0, 50), bev=1.5), "SM_Post_100", [mat("MI_Wood_Beam")], uv_tile=100.0)
    # A blank box module (pawn blockers / generic masses), 100^3 centred.
    save(box(100, 100, 100, 0), "SM_Block", [mat("MI_Wall_White")], uv_tile=100.0)
    # Hanging basket and window box for flowers.
    wb = box(80, 22, 20, 0, (0, 0, 10), bev=1.5)
    save(wb, "SM_WindowBox", [mat("MI_Terracotta")], collision=None, uv_tile=60.0)


for fn in (wisp_quad, cart_wheel, lemon_cart, lemon_cluster, awning_parts, cat_collar, railing, cards, laundry, dome, campanile, street_bits):
    try:
        fn()
        log("mesh ok", fn.__name__)
    except Exception as e:
        import traceback
        log("ERROR", fn.__name__, e, traceback.format_exc())
eal.save_directory(OUT, only_if_is_dirty=True, recursive=True)
log("ALL DONE")
