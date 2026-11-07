"""Stage E - build /Game/Shade/Maps/L_Vellara, a cliffside Mediterranean village at late afternoon.

World axes (cm): +X = east = the route; +Y = south = the sea; Z up. The sun hangs over the sea (south) at 30 deg, so
every patch of shade on the route is cast by something *seaward* of it. Gameplay layout:

  x -700..-150   start: vaulted passage under the "bridge house"          (shaded)
  x -150..1700   Via del Gatto: lane between house rows                   (shaded by the sea-side row)
     x 900..1080   alley gap down to the harbour -> sunbeam  -> P1: push the lemon cart into it
  x 1700..2500   Piazzetta: open to the sea, Caffe Rosa                   -> P2: tug the awning cord
  x 2500..3330   stairs up (z 0 -> 300) between tall houses               (shaded)
  x 3330..       church terrace (z 300)
     x 3500..3650  gap between Casa Azzurra and the bell tower  -> P3: knock the potted lemon tree off the wall
     tower x 3650..3800, y 100..250; Biscotto asleep on a bench at (3894,-470) in the sun, *shadowless*
  Finale: the sun swings 15 deg west and sinks to 24 deg; the bell tower's shadow sweeps over Biscotto.
"""
import math
import os
import random
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402
from sngeo import (GT, T, V, append, box, cyl, extrude, arch_poly, subtract, save, mat, new, log, eal, opts)  # noqa: E402
from sns_bridge import build_bridge_mesh  # noqa: E402

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
MAP = "/Game/Shade/Maps/L_Vellara"
HOUSES = "/Game/Shade/Meshes/Houses"
R = random.Random(1987)
SUN_ROT = (-30.0, -90.0, 0.0)
SUN_FINALE = (-24.0, -75.0, 0.0)

# Rebuild in place: reopen the map (the editor may already have it loaded) and clear it.
if eal.does_asset_exist(MAP):
    lvl.load_level(MAP)
    for _a in actors.get_all_level_actors():
        actors.destroy_actor(_a)
elif not lvl.new_level(MAP):
    raise RuntimeError("new_level failed")


def Rot(pitch=0, yaw=0, roll=0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def load(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def prop(mid, variant=None):
    name = f"SM_{mid}" if variant is None else f"SM_{mid}_V{variant}"
    return load(f"/Game/Shade/Props/{mid}/{name}")


def mesh(name):
    return load(f"/Game/Shade/Meshes/{name}")


def height_of(sm):
    if not sm:
        return 1.0
    b = sm.get_bounds()
    return max(b.box_extent.z * 2, 0.01)


def width_of(sm):
    if not sm:
        return 1.0
    b = sm.get_bounds()
    return max(b.box_extent.x, b.box_extent.y) * 2


def place(sm, loc, rot=(0, 0, 0), scale=1.0, folder="World", tags=(), mats=None, movable=False, shadow=True, collision=True, sun=True):
    if sm is None:
        return None
    a = actors.spawn_actor_from_object(sm, V(*loc), Rot(*rot))
    if a is None:
        return None
    s = scale if isinstance(scale, (tuple, list)) else (scale, scale, scale)
    a.set_actor_scale3d(V(*s))
    a.set_folder_path(folder)
    comp = a.static_mesh_component
    if movable:
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    for i, m in enumerate(mats or []):
        if m:
            comp.set_material(i, m)
    if not shadow:
        comp.set_cast_shadow(False)
    if "island_tree" in sm.get_name():
        # Heavy Nanite foliage: never evaluate WPO (keeps virtual shadow map pages cached).
        try:
            comp.set_editor_property("evaluate_world_position_offset", False)
        except Exception as e:
            log("wpo flag", e)
    if not collision:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    elif not sun:
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_SUNLIGHT, unreal.CollisionResponseType.ECR_IGNORE)
    if tags:
        a.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return a


def place_h(sm, loc, h, rot=(0, 0, 0), **kw):
    """Place scaled to a target height."""
    return place(sm, loc, rot, h / height_of(sm), **kw)


def spawn(cls, loc, rot=(0, 0, 0), folder="Gameplay", tags=(), **props):
    a = actors.spawn_actor_from_class(cls, V(*loc), Rot(*rot))
    a.set_folder_path(folder)
    for k, v in props.items():
        a.set_editor_property(k, v)
    if tags:
        a.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return a


def gclass(name):
    return unreal.load_class(None, f"/Script/shadeNseek.{name}")


def blocker(cx, cy, cz, dx, dy, dz, folder="Blockers"):
    a = place(mesh("SM_Block"), (cx, cy, cz), (0, 0, 0), (dx / 100, dy / 100, dz / 100), folder)
    if a:
        c = a.static_mesh_component
        c.set_collision_profile_name("BlockAllDynamic")
        c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_SUNLIGHT, unreal.CollisionResponseType.ECR_IGNORE)
        c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
        c.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponseType.ECR_IGNORE)
        c.set_editor_property("visible", False)
        a.set_actor_hidden_in_game(True)
        c.set_cast_shadow(False)
    return a


_SLABS = [0]


def slab(x0, x1, y0, y1, ztop, thick, material, folder="Ground"):
    """A true-size slab mesh (UVs at world scale, so ground textures never stretch)."""
    _SLABS[0] += 1
    m = box(x1 - x0, y1 - y0, thick, 0, (0, 0, -thick / 2), bev=2.0 if thick < 200 else 0.0)
    sm = save(m, f"SM_Slab_{_SLABS[0]:02d}", [mat(material)], folder=HOUSES, nanite=True, uv_tile=200.0)
    return place(sm, ((x0 + x1) / 2, (y0 + y1) / 2, ztop), folder=folder)


def waypoint(tag, x, y, z=0):
    spawn(unreal.TargetPoint, (x, y, z + 40), folder="Autoplay", tags=(tag,))


def target(tag, x, y, z=0, rot=(0, 0, 0)):
    spawn(unreal.TargetPoint, (x, y, z), rot, folder="Targets", tags=(tag,))


# =====================================================================================================================
# House generator
# =====================================================================================================================
PALETTE = ["MI_Wall_White", "MI_Wall_Cream", "MI_Wall_Peach", "MI_Wall_Salmon", "MI_Wall_Lemon", "MI_Wall_Ochre", "MI_Wall_Rose",
           "MI_Wall_White", "MI_Wall_Cream", "MI_Wall_Beige", "MI_Wall_Aged"]
SHUTTERS = ["MI_Shutter_Green", "MI_Shutter_Blue", "MI_Shutter_Teal", "MI_Shutter_Olive", "MI_Shutter_Green"]
DOORS = ["MI_Door_Blue", "MI_Door_Red", "MI_Door_Wood", "MI_Door_Blue"]
# slots: 0 wall, 1 trim, 2 glass, 3 shutter, 4 door, 5 iron, 6 roof tiles, 7 stone, 8 wood
FACE = {"+y": ((1, 0), (0, 1)), "-y": ((-1, 0), (0, -1)), "+x": ((0, -1), (1, 0)), "-x": ((0, 1), (-1, 0))}
DECOR = []  # (kind, world_pos, yaw_out, extra)


class House:
    def __init__(self, name, x0, x1, y0, y1, z0, h, wall=None, faces=("+y",), rng=None, roof="flat", detail=1.0,
                 door=None, shop=False, floors=None, balconies=True, arches=0.3, glass_glow=False):
        self.rng = rng or random.Random(hash(name) & 0xffff)
        self.name, self.z0, self.h = name, z0, h
        self.cx, self.cy = (x0 + x1) / 2, (y0 + y1) / 2
        self.w, self.d = x1 - x0, y1 - y0
        self.wall = wall or self.rng.choice(PALETTE)
        self.shutter = self.rng.choice(SHUTTERS)
        self.door_mat = self.rng.choice(DOORS)
        self.m = box(self.w, self.d, h, 0, (0, 0, h / 2), bev=4.0)
        self.parts = []
        self.faces = faces
        self.detail = detail
        self.roof = roof
        self.door = door  # (face, u) in local face coords, or None
        self.shop = shop
        self.floors = floors
        self.balconies = balconies
        self.arches = arches
        self.glass = "MI_Glass_Warm" if (glass_glow or self.rng.random() < 0.22) else "MI_Glass"

    # face helpers ---------------------------------------------------------------------------------------------------
    def frame(self, face):
        t, n = FACE[face]
        L = self.w if face in ("+y", "-y") else self.d
        off = self.d / 2 if face in ("+y", "-y") else self.w / 2
        return t, n, L, off

    def P(self, face, u, z, depth):
        t, n, L, off = self.frame(face)
        return (t[0] * u + n[0] * (off + depth), t[1] * u + n[1] * (off + depth), z)

    def yaw(self, face):
        t, n, _, _ = self.frame(face)
        return math.degrees(math.atan2(n[1], n[0]))

    def world(self, p):
        return (self.cx + p[0], self.cy + p[1], self.z0 + p[2])

    def part(self, m):
        self.parts.append(m)

    def oriented_box(self, face, u, z, depth, sx, sy_depth, sz, slot, rot_extra=0.0):
        """Box whose X runs along the face, Y along the face normal."""
        b = box(sx, sy_depth, sz, slot, (0, 0, 0))
        yaw = self.yaw(face) - 90.0 + rot_extra
        GT.transform_mesh(b, T(self.P(face, u, z, depth), (0, yaw, 0)))
        return b

    # openings -------------------------------------------------------------------------------------------------------
    def normal_cyl(self, face, u, z, depth, radius, length, slot, steps=20):
        """Cylinder whose axis runs along the face normal (roll 90 maps Z onto Y, then the face yaw)."""
        c = new()
        unreal.GeometryScript_Primitives.append_cylinder(c, opts(slot), T(), float(radius), float(length), steps, 0, True,
                                                         unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        GT.transform_mesh(c, T(rot=(0, 0, 90)))
        GT.transform_mesh(c, T(self.P(face, u, z, depth), (0, self.yaw(face) - 90.0, 0)))
        return c

    def cut(self, face, u, zb, ow, oh, arched):
        spring = oh - ow / 2 if arched else oh
        tool = box(ow, 44, spring, 0, (0, 0, spring / 2))
        GT.transform_mesh(tool, T(self.P(face, u, zb, -3.0), (0, self.yaw(face) - 90.0, 0)))
        subtract(self.m, tool)
        if arched:
            subtract(self.m, self.normal_cyl(face, u, zb + spring, -3.0, ow / 2, 44, 0, 24))

    def window(self, face, u, zb, ow, oh, arched, kind):
        self.cut(face, u, zb, ow, oh, arched)
        rec = -20.0
        glass_h = oh - (ow / 2 if arched else 0)
        self.part(self.oriented_box(face, u, zb + glass_h / 2, rec, ow - 4, 2, glass_h - 4, 2))
        if arched:
            self.part(self.normal_cyl(face, u, zb + glass_h, rec, ow / 2 - 2, 2, 2))
        # frame + mullion
        for (du, dz, sx, sz) in ((0, glass_h - 3, ow, 6), (0, 3, ow, 6), (-ow / 2 + 3, glass_h / 2, 6, glass_h), (ow / 2 - 3, glass_h / 2, 6, glass_h),
                                 (0, glass_h / 2, 4, glass_h), (0, glass_h * 0.62, ow, 4)):
            self.part(self.oriented_box(face, u + du, zb + dz, rec + 2.5, sx, 4, sz, 1))
        if kind == "door":
            return
        # sill
        self.part(self.oriented_box(face, u, zb - 3, 5, ow + 20, 22, 6, 7))
        DECOR.append(("sill", self.world(self.P(face, u, zb + 1, 9)), self.yaw(face), ow))
        # shutters: open flat against the wall, closed, or ajar
        state = self.rng.choices(["open", "closed", "ajar"], [0.55, 0.25, 0.2])[0]
        pw = ow / 2 + 2
        for s in (-1, 1):
            if state == "open":
                cu = u + s * (ow / 2 + pw / 2 + 2)
                self.part(self.oriented_box(face, cu, zb + glass_h / 2, 2.5, pw, 3.5, glass_h, 3))
                for k in range(6):
                    self.part(self.oriented_box(face, cu, zb + 12 + k * (glass_h - 24) / 5, 4.5, pw - 8, 1.5, 3, 3))
            elif state == "closed":
                cu = u + s * (pw / 2 - 1)
                self.part(self.oriented_box(face, cu, zb + glass_h / 2, -5, pw - 2, 3.5, glass_h - 2, 3))
                for k in range(7):
                    self.part(self.oriented_box(face, cu, zb + 10 + k * (glass_h - 20) / 6, -3, pw - 10, 1.5, 3, 3))
            else:
                hinge = u + s * (ow / 2)
                ang = s * -55.0
                b = box(pw, 3.5, glass_h, 3, (s * -pw / 2, 0, 0))
                GT.transform_mesh(b, T((0, 0, 0), (0, ang, 0)))
                GT.transform_mesh(b, T(self.P(face, hinge, zb + glass_h / 2, 1.5), (0, self.yaw(face) - 90.0, 0)))
                self.part(b)
        if self.rng.random() < 0.25 * self.detail:
            # painted lintel band
            self.part(self.oriented_box(face, u, zb + oh + 7, 1.5, ow + 26, 4, 12, 1))

    def door_at(self, face, u):
        ow, oh = 112, 236
        self.cut(face, u, 0, ow, oh, True)
        self.part(self.oriented_box(face, u, (oh - ow / 2) / 2, -16, ow - 6, 6, oh - ow / 2 - 2, 4))
        self.part(self.normal_cyl(face, u, oh - ow / 2, -16, ow / 2 - 3, 6, 4))
        for k in range(5):
            self.part(self.oriented_box(face, u - ow / 2 + 14 + k * (ow - 28) / 4, (oh - ow / 2) / 2, -12.5, 2.5, 2, oh - ow / 2 - 30, 4))
        self.part(self.oriented_box(face, u, 7, 22, ow + 50, 44, 14, 7))
        DECOR.append(("door", self.world(self.P(face, u, 0, 30)), self.yaw(face), ow))

    def shopfront(self, face, u):
        ow, oh = 230, 250
        self.cut(face, u, 0, ow, oh, True)
        self.part(self.oriented_box(face, u, 110, -22, ow - 4, 2, 220, 2))
        self.part(self.oriented_box(face, u, 6, 22, ow + 40, 40, 12, 7))
        DECOR.append(("shop", self.world(self.P(face, u, 0, 30)), self.yaw(face), ow))

    def balcony(self, face, u, zb, ow):
        oh = 215
        self.window(face, u, zb, ow, oh, False, "door")
        depth = 72
        self.part(self.oriented_box(face, u, zb - 8, depth / 2, ow + 90, depth, 14, 7))
        rail_y = depth - 3
        self.part(self.oriented_box(face, u, zb + 96, rail_y, ow + 90, 4, 4, 5))
        self.part(self.oriented_box(face, u, zb + 12, rail_y, ow + 90, 3, 3, 5))
        n = int((ow + 90) / 11)
        for k in range(n + 1):
            self.part(self.oriented_box(face, u - (ow + 90) / 2 + k * (ow + 90) / n, zb + 52, rail_y, 1.6, 1.6, 88, 5))
        for s in (-1, 1):
            su = u + s * (ow + 90) / 2
            self.part(self.oriented_box(face, su, zb + 96, depth / 2, 4, depth, 4, 5))
            for k in range(6):
                self.part(self.oriented_box(face, su, zb + 52, 6 + k * (depth - 8) / 5, 1.6, 1.6, 88, 5))
        DECOR.append(("balcony", self.world(self.P(face, u, zb, depth * 0.55)), self.yaw(face), ow + 80))

    def build(self):
        rng = self.rng
        floor_h = 290
        nfl = self.floors or max(1, int((self.h - 80) // floor_h))
        for face in self.faces:
            t, n, L, off = self.frame(face)
            ncol = max(1, int((L - 60) // 240))
            pitch = (L - 60) / ncol
            door_col = None
            if self.door and self.door[0] == face:
                door_col = min(range(ncol), key=lambda c: abs((-L / 2 + 30 + pitch * (c + 0.5)) - self.door[1]))
            for f in range(nfl):
                for c in range(ncol):
                    u = -L / 2 + 30 + pitch * (c + 0.5) + rng.uniform(-10, 10)
                    if f == 0:
                        if c == door_col:
                            if self.shop:
                                self.shopfront(face, u)
                            else:
                                self.door_at(face, u)
                            continue
                        zb = 105
                    else:
                        zb = f * floor_h + 75
                    if zb + 150 > self.h - 40:
                        continue
                    if rng.random() > 0.85 + 0.15 * self.detail:
                        continue
                    if f > 0 and self.balconies and rng.random() < 0.28 * self.detail and face in ("+y", "-y"):
                        self.balcony(face, u, f * floor_h + 25, 96)
                        continue
                    arched = rng.random() < self.arches
                    ow = rng.choice([80, 86, 92])
                    self.window(face, u, zb, ow, rng.choice([120, 128, 135]), arched, "window")
            # bougainvillea anchors along the upper edge
            for k in range(rng.randint(0, 2) + int(self.detail)):
                u = rng.uniform(-L / 2 + 40, L / 2 - 40)
                DECOR.append(("drape", self.world(self.P(face, u, self.h - rng.uniform(20, 120), 25)), self.yaw(face), 0))
            if rng.random() < 0.35 * self.detail and self.h > 500:
                u = rng.choice([-L / 2 + 70, L / 2 - 70])
                DECOR.append(("ac", self.world(self.P(face, u, rng.uniform(320, self.h - 150), 22)), self.yaw(face), 0))
        # cornice + roof
        if self.roof in ("flat", "pergola", "vault"):
            self.part(box(self.w + 14, self.d + 14, 14, 1, (0, 0, self.h - 30)))
            ph = 48
            for (cx, cy, sx, sy) in ((0, self.d / 2 - 8, self.w, 16), (0, -self.d / 2 + 8, self.w, 16), (self.w / 2 - 8, 0, 16, self.d), (-self.w / 2 + 8, 0, 16, self.d)):
                self.part(box(sx, sy, ph, 0, (cx, cy, self.h + ph / 2), bev=2.0))
                self.part(box(sx + 4, sy + 6, 6, 1, (cx, cy, self.h + ph + 3)))
            if self.roof == "vault":
                along_x = self.w >= self.d
                r = (self.d if along_x else self.w) / 2 - 30
                L2 = (self.w if along_x else self.d) - 40
                v = new()
                unreal.GeometryScript_Primitives.append_cylinder(v, unreal.GeometryScriptPrimitiveOptions(), T(), float(r), float(L2), 28, 0, True,
                                                                 unreal.GeometryScriptPrimitiveOriginMode.CENTER)
                GT.transform_mesh(v, T(rot=(90, 0, 0) if along_x else (0, 0, 90)))   # axis -> X (pitch) or Y (roll)
                GT.transform_mesh(v, T((0, 0, self.h)))
                subtract(v, box(self.w + 80, self.d + 80, r + 60, 0, (0, 0, self.h - (r + 60) / 2)))
                self.part(v)
            if self.roof == "pergola":
                for k in range(int(self.w // 60)):
                    self.part(box(10, self.d - 40, 10, 8, (-self.w / 2 + 40 + k * 60, 0, self.h + 210)))
                for (px, py) in ((-self.w / 2 + 30, -self.d / 2 + 30), (self.w / 2 - 30, -self.d / 2 + 30), (-self.w / 2 + 30, self.d / 2 - 30), (self.w / 2 - 30, self.d / 2 - 30)):
                    self.part(box(12, 12, 210, 8, (px, py, self.h + 105)))
                DECOR.append(("roofpergola", self.world((0, 0, self.h + 220)), 0, self.w))
        elif self.roof == "tiles":
            over = 18
            hh = min(self.d, 500) * 0.28
            prism = extrude([(-self.d / 2 - over, 0), (self.d / 2 + over, 0), (0, hh)], self.w + 2 * over, 6)
            GT.transform_mesh(prism, T(rot=(90, 0, 0)))
            GT.transform_mesh(prism, T((0, 0, 0), (0, 90, 0)))
            GT.transform_mesh(prism, T((-(self.w + 2 * over) / 2, 0, self.h)))
            self.part(prism)
            self.part(box(self.w + 10, self.d + 10, 12, 1, (0, 0, self.h - 6)))
        if self.rng.random() < 0.5 * self.detail:
            corner = (self.w / 2 - 12, self.d / 2 - 6 if "+y" in self.faces else -self.d / 2 + 6)
            self.part(cyl(5, self.h - 10, 5, (corner[0], corner[1] + (6 if "+y" in self.faces else -6), 0), steps=8))
        for p in self.parts:
            append(self.m, p)
        mats = [mat(self.wall), mat("MI_Trim_White"), mat(self.glass), mat(self.shutter), mat(self.door_mat), mat("MI_Iron_Black"),
                mat("MI_RoofTiles"), mat("MI_StepStone"), mat("MI_Wood_Beam")]
        sm = save(self.m, f"SM_H_{self.name}", mats, folder=HOUSES, nanite=True, collision="complex", uv_tile=200.0)
        a = place(sm, (self.cx, self.cy, self.z0), folder="Houses")
        return a


def house(name, x0, x1, y0, y1, z0, h, **kw):
    try:
        return House(name, x0, x1, y0, y1, z0, h, **kw).build()
    except Exception as e:
        import traceback
        log("HOUSE FAILED", name, e, traceback.format_exc())
        return None


# =====================================================================================================================
# 1. Ground: lane, piazza, alley, stairs, terrace, cliff mass below
# =====================================================================================================================
slab(-1000, 2650, -160, 160, 0, 40, "MI_Cobble")
slab(1700, 2500, 160, 560, 0, 40, "MI_Flag")              # piazza terrace
slab(900, 1080, 160, 520, 0, 40, "MI_StepStone")          # alley
slab(-1000, 3330, 160, 900, -40, 1400, "MI_Wall_Stone")    # bedrock under the sea-side row
slab(-1000, 4800, -2600, -160, -40, 1400, "MI_Wall_Stone")
# stairs up to the church terrace (17 steps x 17.65 cm, 40 cm deep)
st = new()
unreal.GeometryScript_Primitives.append_linear_stairs(st, unreal.GeometryScriptPrimitiveOptions(), T(), 320.0, 300.0 / 17.0, 40.0, 17, False)
stairs_sm = save(st, "SM_Stairs_Main", [mat("MI_StepStone")], uv_tile=120.0)
place(stairs_sm, (2650, 0, 0), (0, 0, 0), 1.0, "Ground")  # authored along +X from 0, centred on Y
slab(3330, 4800, -560, 250, 300, 340, "MI_Flag")            # church terrace
slab(3330, 4800, 250, 900, 300 - 40, 1600, "MI_Wall_Stone")  # terrace retaining mass (seaward)
slab(3330, 4800, -1400, -560, 300, 40, "MI_Flag")
log("stairs bounds", stairs_sm.get_bounds().box_extent if stairs_sm else None)

# =====================================================================================================================
# 2. Houses along the route
# =====================================================================================================================
F = "Houses"
# start passage: north + south houses joined by a bridge block over the lane (the vault)
house("N0", -1000, -100, -900, -160, 0, 900, wall="MI_Wall_Cream", faces=("+y",), door=("+y", 250))
house("S0", -1000, -250, 160, 760, 0, 820, wall="MI_Wall_Peach", faces=("-y", "+y"), door=("-y", 200))
bsm = build_bridge_mesh()  # sns_bridge.py: vault cut 2 cm inside the house fronts (no z-fighting)
place(bsm, (-450, 0, 0), folder=F)
place(prop("street_lamp_02"), (-450, -150, 280), (0, 90, 0), 1.0, F)
blocker(-1010, 0, 200, 40, 340, 400)
house("W0", -1400, -1000, -160, 160, 0, 700, wall="MI_Wall_White", faces=("+x",), door=("+x", 0))

# north row of Via del Gatto (faces the lane = +Y)
north = [("N1", -100, 450, 780, "MI_Wall_Cream", 150), ("N2", 450, 900, 900, "MI_Wall_Peach", None), ("N3", 900, 1300, 820, "MI_Wall_White", 100),
         ("N4", 1300, 1700, 760, "MI_Wall_Lemon", None)]
for (n, x0, x1, h, w, door) in north:
    house(n, x0, x1, -900, -160, 0, h, wall=w, faces=("+y",), door=("+y", door) if door is not None else None, roof=R.choice(["flat", "vault", "pergola"]))
# sea-side row (faces the lane = -Y, and the sea = +Y); the gap at x 900..1080 is the sunlit alley
south = [("S1", -250, 300, 600, "MI_Wall_White", -80), ("S2", 300, 900, 650, "MI_Wall_Rose", None), ("S3", 1080, 1700, 620, "MI_Wall_Cream", 200)]
for (n, x0, x1, h, w, door) in south:
    house(n, x0, x1, 160, 760, 0, h, wall=w, faces=("-y", "+y", "+x" if n == "S3" else "-x"), door=("-y", door) if door is not None else None,
          roof=R.choice(["flat", "vault"]))
# Caffe Rosa: north side of the piazza, a shopfront and the sign
house("Caffe", 1700, 2500, -900, -160, 0, 820, wall="MI_Wall_Ochre", faces=("+y",), door=("+y", -150), shop=True)
place(mesh("SM_Card"), (1950, -158, 330), (0, -90, 0), (1, 2.6, 0.65), "Piazza", mats=[mat("MI_Sign_Caffe")], collision=False)
place(mesh("SM_Card"), (2210, -150, 150), (0, -90, 0), (1, 0.55, 0.7), "Piazza", mats=[mat("MI_Sign_Menu")], collision=False)
# lane 2 + stairs
house("S4", 2500, 3000, 160, 760, 0, 650, wall="MI_Wall_Salmon", faces=("-y", "+y", "-x"), roof="vault")
house("N5", 2500, 3330, -900, -160, 0, 1000, wall="MI_Wall_White", faces=("+y",), door=("+y", -250), roof="pergola")
house("Azzurra", 3000, 3500, 160, 760, 0, 750, wall="MI_Wall_Sky", faces=("-y", "+y", "+x"), roof="flat", door=("-y", -150))

# =====================================================================================================================
# 3. Church terrace: church, dome, bell tower, railing, bench
# =====================================================================================================================
F = "Church"
church = House("Church", 3550, 4350, -1400, -560, 300, 1100, wall="MI_Wall_Cream", faces=("+y",), door=("+y", 0), floors=3, balconies=False, arches=1.0)
church.build()
cf = new()
for px in (-330, -110, 110, 330):
    append(cf, box(46, 22, 1060, 0, (px, 0, 530), bev=2.0))
append(cf, box(800, 30, 28, 1, (0, 0, 1080)))
gable = new()
unreal.GeometryScript_Primitives.append_cylinder(gable, opts(0), T(), 300.0, 34.0, 40, 0, True, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
GT.transform_mesh(gable, T(rot=(0, 0, 90)))
subtract(gable, box(700, 120, 330, 0, (0, 0, -165)))
GT.transform_mesh(gable, T((0, 0, 1094)))
append(cf, gable)
append(cf, box(140, 40, 26, 1, (0, 0, 1395)))
rose = new()
unreal.GeometryScript_Primitives.append_torus(rose, opts(1), T(), unreal.GeometryScriptRevolveOptions(), 78.0, 9.0, 32, 8, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
GT.transform_mesh(rose, T(rot=(0, 0, 90)))
GT.transform_mesh(rose, T((0, 6, 870)))
append(cf, rose)
disc = new()
unreal.GeometryScript_Primitives.append_cylinder(disc, opts(2), T(), 72.0, 4.0, 32, 0, True, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
GT.transform_mesh(disc, T(rot=(0, 0, 90)))
GT.transform_mesh(disc, T((0, -2, 870)))
append(cf, disc)
for k in range(3):
    append(cf, box(260 - k * 40, 24, 16, 3, (0, 26 + (2 - k) * 12, 8 + k * 16)))
cfs = save(cf, "SM_ChurchFront", [mat("MI_Wall_Cream"), mat("MI_Trim_White"), mat("MI_Glass_Warm"), mat("MI_StepStone")], folder=HOUSES, nanite=True)
place(cfs, (3950, -560 + 11, 300), folder="Church")
place(mesh("SM_Dome"), (3950, -1000, 1400), (0, 0, 0), 1.0, F)
place(mesh("SM_Campanile"), (3725, 175, -200), (0, 0, 0), 1.0, F, tags=("BellTower",))
place(mesh("SM_Bell"), (3725, 175, 1420), (0, 0, 0), 1.0, F, collision=False)
for x in range(3820, 4700, 200):
    place(mesh("SM_Railing_200"), (x, 250, 300), (0, 0, 0), 1.0, F, collision=False)
for x in range(1700, 2500, 200):
    place(mesh("SM_Railing_200"), (x, 560, 0), (0, 0, 0), 1.0, "Piazza", collision=False)
blocker(4260, 262, 380, 900, 20, 200)
blocker(2100, 572, 100, 820, 20, 200)
blocker(4700, -150, 400, 20, 900, 300)
house("E1", 4350, 4800, -1400, -560, 300, 800, faces=("+y", "-x"), roof="tiles")
place(mesh("SM_Bench_Stone"), (3894, -430, 300), (0, 0, 0), 1.0, F)
place(mesh("SM_Bench_Stone"), (4250, -470, 300), (0, 0, 0), 1.0, F)
# low stone wall + planter in front of Casa Azzurra (the pot sits on its east end)
wall = box(115, 60, 80, 0, (0, 0, 40), bev=3.0)
append(wall, box(119, 64, 6, 1, (0, 0, 81)))
wsm = save(wall, "SM_PotWall", [mat("MI_Wall_Stacked"), mat("MI_StepStone")], folder=HOUSES)
place(wsm, (3435, 130, 300), folder=F)

# =====================================================================================================================
# 4. Gameplay
# =====================================================================================================================
F = "Gameplay"
spawn(unreal.PlayerStart, (-520, -40, 60), (0, 0, 0), F)
cart = spawn(gclass("ShadeLemonCart"), (550, 95, 0), (0, 0, 0), F, tags=("Dev_Cart",), track_length=440.0, solved_offset=420.0, solved_event=unreal.Name("CartSolved"))
place(mesh("SM_Bollard"), (1112, 112, 0), (0, 0, 0), 1.0, "Lane")
aw = spawn(gclass("ShadeAwning"), (1706, 250, 282), (0, 0, 0), F, tags=("Dev_Awning",), depth=780.0, width=300.0, slope=1.2,
           start_extension=0.05, open_speed=0.36, cord_local=V(-16, -150, -247), interact_offset=V(-30, -40, 0), show_arms=False,
           solved_event=unreal.Name("AwningSolved"))
# pergola frame for the café canvas
for (px, py) in ((2492, 100), (2492, 400), (1714, 400)):
    place(mesh("SM_Post_100"), (px, py, 0), (0, 0, 0), (1, 1, 2.78), "Piazza", sun=False)
for py in (100, 400):
    place(mesh("SM_Beam_100"), (1706, py, 286), (0, 0, 0), (7.9, 1, 1), "Piazza", sun=False)
pot = spawn(gclass("ShadeFlowerPot"), (3468, 130, 386), (0, 0, 0), F, tags=("Dev_Pot",), land_offset=V(107, -15, -86), land_yaw=20.0,
            solved_event=unreal.Name("PotSolved"))
pot_mesh = prop("planter_pot_clay")
pc = pot.get_editor_property("pot")
pc.set_static_mesh(pot_mesh)
pc.set_editor_property("relative_scale3d", V(*([62.0 / height_of(pot_mesh)] * 3)))
tree = prop("island_tree_02")
plant = pot.get_editor_property("plant")
plant.set_static_mesh(tree)
ts = 190.0 / height_of(tree)
plant.set_editor_property("relative_scale3d", V(ts, ts, ts))
plant.set_editor_property("relative_location", V(4.6, 49.0, 43.0))  # island_tree_02's trunk is off its pivot (sns_fix_pot.py)
# The lemon tree stands in the terracotta pot: a disc of earth just under the rim hides the trunk's base.
pot_h = 62.0
pot_r = (width_of(pot_mesh) * (62.0 / height_of(pot_mesh))) / 2
soil = pot.get_editor_property("soil")
soil.set_static_mesh(eal.load_asset("/Engine/BasicShapes/Cylinder"))
soil.set_material(0, mat("MI_Soil"))
soil.set_editor_property("relative_scale3d", V(pot_r * 1.72 / 100.0, pot_r * 1.72 / 100.0, 0.03))
soil.set_editor_property("relative_location", V(0, 0, pot_h - 7.0))
log("pot", "radius", round(pot_r), "tree height", 190)
try:
    plant.set_editor_property("evaluate_world_position_offset", False)
except Exception as e:
    log("wpo flag", e)
canopy = pot.get_editor_property("canopy_shade")
canopy.set_editor_property("box_extent", V(85, 85, 55))
canopy.set_editor_property("relative_location", V(0, 0, 150))
lemons = mesh("SM_LemonCluster")
if lemons:
    pass
bis = spawn(gclass("ShadeNapCat"), (3894, -430, 350), (0, 90, 0), F, tags=("Dev_Biscotto",), is_biscotto=True)
spawn(gclass("ShadeNapCat"), (180, -128, 0), (0, 40, 0), F, coat_tint=unreal.LinearColor(0.55, 0.55, 0.6, 1), coat_dark=0.35, cat_scale=0.92)
spawn(gclass("ShadeNapCat"), (2080, 545, 96), (0, 160, 0), F, coat_tint=unreal.LinearColor(1, 1, 1, 1), coat_dark=0.85, cat_scale=1.0)

for (ev, x, y, z, ex, ey, ez) in (("Zone_Lane", 120, 0, 150, 60, 170, 160), ("Zone_AfterAlley", 1260, 0, 150, 60, 170, 160),
                                  ("Zone_AfterPiazza", 2600, 0, 150, 60, 170, 160), ("Zone_Terrace", 3720, -150, 420, 45, 320, 160)):
    z_ = spawn(gclass("ShadeZone"), (x, y, z), (0, 0, 0), F, event_name=unreal.Name(ev))
    z_.get_editor_property("box").set_editor_property("box_extent", V(ex, ey, ez))

for (tag, x, y, z) in (("OBJ_Lane", 300, -60, 40), ("OBJ_Cart", 420, 95, 110), ("OBJ_Awning", 1690, 100, 70), ("OBJ_Pot", 3468, 130, 470),
                       ("OBJ_Wait", 3760, -100, 360), ("OBJ_Biscotto", 3894, -430, 420)):
    target(tag, x, y, z)
target("SunFinale", 0, 0, 3000, SUN_FINALE)
target("Gallery", 2100, 180, 0, (0, 0, 0))

# Autoplay route (all points are in shade at the moment they're used)
for (tag, x, y, z) in (("AP_Lane1", 200, -60, 0), ("AP_SunTouch", 906, -60, 0), ("AP_SunBack", 780, -60, 0), ("AP_CartGrab", 400, 95, 0),
                       ("AP_Alley1", 840, -15, 0), ("AP_Alley2", 1000, -12, 0), ("AP_Alley3", 1220, -30, 0), ("AP_Cord", 1672, 58, 0),
                       ("AP_Piazza1", 1800, -110, 0), ("AP_Piazza2", 2440, -110, 0), ("AP_Steps1", 2600, -40, 0), ("AP_Steps2", 2900, -40, 110),
                       ("AP_Steps3", 3250, -40, 270), ("AP_WallJump", 3420, 45, 300), ("AP_WallTop", 3420, 128, 380), ("AP_PotNudge", 3442, 130, 380),
                       ("AP_WallDown", 3400, 30, 300), ("AP_Terrace", 3480, -150, 300), ("AP_Porch", 3760, -100, 300), ("AP_Biscotto", 3880, -372, 300)):
    waypoint(tag, x, y, z)

# Cameras: see sns_lighting.py (TitleCam / EndCam + QA candidates)

def warm_light(loc, lumens, radius, color=(1.0, 0.68, 0.38), folder="Lighting/Warm"):
    a = spawn(unreal.PointLight, loc, (0, 0, 0), folder)
    c = a.get_component_by_class(unreal.PointLightComponent)
    c.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
    c.set_editor_property("intensity", float(lumens))
    # keyword args: positional unreal.Color(...) fills B, G, R (FColor memory order) - that turned every "warm" light blue
    c.set_editor_property("light_color", unreal.Color(r=int(color[0] * 255), g=int(color[1] * 255), b=int(color[2] * 255), a=255))
    c.set_editor_property("attenuation_radius", float(radius))
    c.set_editor_property("cast_shadows", False)
    c.set_editor_property("source_radius", 6.0)
    return a


bulb = new()
append(bulb, cyl(3.4, 9.0, 0, (0, 0, -9.0), steps=10))
append(bulb, cyl(1.6, 4.0, 1, (0, 0, -1.0), steps=8))
bulb_sm = save(bulb, "SM_Bulb", [mat("MI_Bulb_Warm"), mat("MI_Iron_Black")], folder=HOUSES, collision=None)


def festoon(x0, y0, x1, y1, z, n=10, sag=45.0, light=True):
    """A string of warm bulbs on a sagging wire between two facades."""
    pts = []
    for i in range(n + 1):
        t = i / n
        pts.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z - sag * 4 * t * (1 - t)))
    for a, b in zip(pts[:-1], pts[1:]):
        d = V(b[0] - a[0], b[1] - a[1], b[2] - a[2])
        L = math.sqrt(d.x ** 2 + d.y ** 2 + d.z ** 2)
        r = unreal.MathLibrary.make_rot_from_x(d)
        w = place(mesh("SM_Rope_100"), a, (r.pitch, r.yaw, 0), (L / 100, 1.4, 1.4), "Festoon", collision=False, shadow=False)
        if w:
            w.static_mesh_component.set_material(0, mat("MI_Iron_Black"))
    for p in pts[1:-1]:
        place(bulb_sm, p, (0, 0, 0), 1.0, "Festoon", collision=False, shadow=False)
    if light:
        mid = pts[len(pts) // 2]
        warm_light((mid[0], mid[1], mid[2] - 25), 260, 520)


for (x, z) in ((60, 430), (520, 470), (1180, 450), (1560, 440), (2560, 520), (3120, 640)):
    festoon(x, -158, x + 140, 158, z)
festoon(1720, -158, 2480, 90, 470, n=16, sag=60)
festoon(1720, 90, 2480, -158, 520, n=16, sag=60)
festoon(3560, -555, 4400, -555, 760, n=16, sag=50)

# =====================================================================================================================
# 5. Dressing along the route
# =====================================================================================================================
F = "Dressing"
POTS = [("potted_plant_01", None), ("potted_plant_02", None), ("potted_plant_04", None), ("planter_pot_clay", None)]
FLOWERS = [("flower_gazania", i) for i in range(3)] + [("flower_heliophila", i) for i in range(3)] + [("flower_ursinia", i) for i in range(3)] + \
          [("periwinkle_plant", i) for i in range(3)] + [("shrub_sorrel_01", i) for i in range(3)]


def pick(cat):
    for _ in range(8):
        mid, v = R.choice(cat)
        sm = prop(mid, v)
        if sm:
            return sm
    return None


def pot_cluster(x, y, z, n=3, spread=45, h=(45, 95)):
    for k in range(n):
        sm = pick(POTS)
        place_h(sm, (x + R.uniform(-spread, spread), y + R.uniform(-spread * 0.4, spread * 0.4), z), R.uniform(*h), (0, R.uniform(0, 360), 0), folder=F, sun=False)


for (kind, (x, y, z), yaw, extra) in DECOR:
    if kind == "sill" and R.random() < 0.75:
        place(mesh("SM_WindowBox"), (x, y, z), (0, yaw - 90, 0), (extra / 80 * 0.95, 1, 1), F, collision=False)
        for k in range(3):
            fl = pick(FLOWERS)
            ox = math.cos(math.radians(yaw - 90)) * R.uniform(-extra * 0.35, extra * 0.35)
            oy = math.sin(math.radians(yaw - 90)) * R.uniform(-extra * 0.35, extra * 0.35)
            place_h(fl, (x + ox, y + oy, z + 18), R.uniform(22, 38), (0, R.uniform(0, 360), 0), folder=F, collision=False)
    elif kind == "balcony":
        pot_cluster(x, y, z, 2, extra * 0.3, (40, 80))
        if R.random() < 0.6:
            place(mesh("SM_BougainClump_A"), (x, y, z + 150), (0, R.uniform(0, 360), 0), R.uniform(0.9, 1.3), F, collision=False)
    elif kind == "door":
        side = math.radians(yaw - 90)
        for s in (-1, 1):
            if R.random() < 0.75:
                place_h(pick(POTS), (x + math.cos(side) * s * (extra * 0.5 + 40), y + math.sin(side) * s * (extra * 0.5 + 40), z), R.uniform(55, 110),
                        (0, R.uniform(0, 360), 0), folder=F, sun=False)
        if R.random() < 0.6:
            lx = x + math.cos(side) * (extra * 0.5 + 25) - math.cos(math.radians(yaw)) * 25
            ly = y + math.sin(side) * (extra * 0.5 + 25) - math.sin(math.radians(yaw)) * 25
            place(prop("street_lamp_02"), (lx, ly, 255), (0, yaw + 180, 0), 1.0, F, collision=False)
            if abs(ly) < 1000 and -1200 < lx < 5000:
                warm_light((lx + math.cos(math.radians(yaw)) * 30, ly + math.sin(math.radians(yaw)) * 30, 270), 170, 450)
    elif kind == "drape" and R.random() < 0.75:
        place(mesh(R.choice(["SM_BougainDrape", "SM_BougainClump_B", "SM_LeafyClump", "SM_BougainClump_A"])), (x, y, z), (0, R.uniform(0, 360), 0),
              R.uniform(0.9, 1.5), F, collision=False)
    elif kind == "ac":
        place(prop("exterior_aircon_unit"), (x, y, z), (0, yaw + 90, 0), 0.5, F, collision=False)
    elif kind == "roofpergola":
        place(mesh("SM_BougainDrape"), (x, y, z), (0, R.uniform(0, 360), 0), (extra / 200, 1.2, 0.8), F, collision=False)
    elif kind == "shop":
        pass

# Lane clutter (north side, out of the shade path; nothing tall on the seaward side of the route)
for (x, y) in ((-60, -120), (330, -125), (760, -128), (1250, -122), (1590, -120), (2560, -125)):
    pot_cluster(x, y, 0, R.randint(2, 3), 40)
place(prop("wine_barrel_01"), (620, -120, 0), (0, 25, 0), 0.9, F, sun=False)
place(prop("wicker_basket_02"), (690, -128, 0), (0, 70, 0), 1.0, F, sun=False)
place(prop("wooden_bucket_01"), (1380, -130, 0), (0, 0, 0), 1.0, F, sun=False)
place(prop("watering_can_metal_01"), (1420, -128, 0), (0, 130, 0), 1.0, F, sun=False)
place(prop("painted_wooden_bench"), (2150, -122, 0), (0, 0, 0), 1.0, "Piazza", sun=False)
place(prop("concrete_cat_statue"), (-760, -120, 0), (0, 30, 0), 0.6, F, sun=False)
place(prop("garden_hose_wall_mounted_01"), (380, -156, 70), (0, 90, 0), 1.0, F, collision=False)
place(prop("lion_head"), (2380, -158, 120), (0, 90, 0), 0.6, "Piazza", collision=False)
place(prop("lifebuoy"), (2470, 548, 120), (0, 0, 0), 1.0, "Piazza", collision=False)
place(prop("marble_bust_01"), (4060, -540, 300), (0, 90, 0), 1.0, F, sun=False)
# café terrace by the railing, under the canvas
for (x, y, r) in ((1860, 300, 10), (2100, 250, -20), (2330, 320, 35)):
    place(prop("outdoor_table_chair_set_01"), (x, y, 0), (0, r, 0), 1.0, "Piazza", sun=False)
place(prop("tea_set_01"), (2100, 250, 76), (0, 20, 0), 1.0, "Piazza", collision=False)
place(prop("wine_bottles_01"), (2330, 320, 76), (0, 0, 0), 1.0, "Piazza", collision=False)
pot_cluster(1760, 520, 0, 3, 40, (70, 120))
pot_cluster(2440, 520, 0, 3, 40, (70, 120))
# terrace dressing
pot_cluster(4450, -500, 300, 3, 50, (60, 120))
pot_cluster(3600, -500, 300, 2, 40, (60, 110))
place(prop("island_tree_01"), (4500, 150, 300), (0, 40, 0), 0.38, "Trees", collision=False)

# Laundry lines high across the lane (catch the light on the north facades).
def laundry(x, z, y0=-160, y1=160):
    L = y1 - y0
    place(mesh("SM_Rope_100"), (x, y0, z), (0, 90, 0), (L / 100, 1, 1), "Laundry", collision=False)
    y = y0 + 25
    while y < y1 - 50:
        sm = mesh(R.choice(["SM_Sheet_White", "SM_Sheet_Blue", "SM_Sheet_Lemon", "SM_Towel_Coral", "SM_Towel_Sea", "SM_Towel_Terry"]))
        place(sm, (x, y + 30, z), (0, 90 + R.uniform(-4, 4), 0), 1.0, "Laundry", collision=False)
        y += 85 + R.uniform(0, 40)


laundry(240, 560)
laundry(1500, 600)
laundry(2900, 760)

# =====================================================================================================================
# 6. The town beyond: lower town to the harbour, upper rows up the mountain, sea, cliffs
# =====================================================================================================================
def fill_row(prefix, x0, x1, y0, y1, z0, hmin, hmax, faces=("+y",), detail=0.45, gap=(0, 60)):
    x = x0
    i = 0
    while x < x1 - 250:
        w = R.uniform(380, 720)
        h = R.uniform(hmin, hmax)
        yy0 = y0 + R.uniform(0, 80)
        house(f"{prefix}{i}", x, min(x + w, x1), yy0, y1, z0 + R.uniform(-30, 30), h, faces=faces, detail=detail,
              roof=R.choices(["flat", "vault", "tiles", "pergola"], [4, 3, 1, 1])[0], balconies=True, door=(faces[0], 0) if R.random() < 0.4 else None)
        x += w + R.uniform(*gap)
        i += 1


# lower town, stepping down to the harbour (seen from the piazza and in the title shot)
fill_row("L1_", -2200, 4800, 900, 1500, -620, 550, 750, faces=("+y", "-x", "+x"), detail=0.85)
fill_row("L2_", -2600, 5200, 1700, 2300, -1180, 500, 700, faces=("+y", "-x"), detail=0.65)
slab(-3000, 5600, 760, 1500, -620, 1400, "MI_Wall_Stone")
slab(-3000, 5600, 1500, 2300, -1180, 600, "MI_Wall_Stone")
# upper town up the slope
fill_row("U1_", -2200, 5000, -1700, -950, 420, 600, 850, faces=("+y", "-x", "+x"), detail=0.8)
fill_row("U2_", -2600, 5400, -2500, -1750, 980, 600, 800, faces=("+y", "-x"), detail=0.6)
fill_row("U3_", -2400, 5200, -3300, -2550, 1560, 500, 750, detail=0.5)
slab(-3000, 5600, -1700, -950, 420, 1400, "MI_Wall_Stone")
slab(-3000, 5600, -2500, -1700, 980, 1600, "MI_Wall_Stone")
slab(-3000, 5600, -3300, -2500, 1560, 1800, "MI_Wall_Stone")
# A few olive-like trees on the terraces (Nanite, still, no shadow casting in the distance to keep the GPU budget sane).
for (x, y, z) in ((-1800, -1250, 420), (1200, -1300, 420), (4300, -1250, 420), (-600, -2100, 980), (2600, -2150, 980),
                  (-1500, 1150, -620), (2200, 1100, -620), (4200, 1900, -1180)):
    place(prop("island_tree_0" + str(R.choice([1, 2]))), (x, y, z), (0, R.uniform(0, 360), 0), R.uniform(0.32, 0.45), "Trees", collision=False, shadow=False)
# mountains behind, headlands either side
mt = prop("mountainside")
for (x, y, s, yaw) in ((-1500, -6200, 9.0, 0), (3500, -6800, 11.0, 20), (8500, -5200, 9.0, -40), (-6500, -4200, 8.0, 50)):
    place(mt, (x, y, 600), (0, yaw, 0), s, "Mountains", collision=False, shadow=False)
for (mid, x, y, z, s, yaw) in (("coastal_cliff_01", -7500, 3000, -1600, 1.6, 75), ("coastal_cliff_02", 10500, 2500, -1600, 2.0, -80),
                               ("coastal_cliff_04", 9500, 6500, -1500, 1.4, -60), ("coast_rocks_01", 2000, 3600, -1300, 1.0, 10)):
    place(prop(mid), (x, y, z), (0, yaw, 0), s, "Coast", collision=False, shadow=False)
# beach + pier + boats
slab(-3000, 6000, 2300, 3300, -1235, 300, "MI_Sand")
pier = prop("modular_wooden_pier")
place(pier, (1200, 3500, -1240), (0, 90, 0), 1.4, "Coast", collision=False)
boat = prop("ship_pinnace")
if boat:
    bs = 520.0 / max(width_of(boat), 1)
    for (x, y, yaw) in ((600, 3900, 30), (1900, 4300, -70), (3300, 3700, 110)):
        place(boat, (x, y, -1258), (0, yaw, 0), bs, "Coast", collision=False)
ship = prop("dutch_ship_medium")
place(ship, (6500, 16000, -1265), (0, 60, 0), 1.0, "Coast", collision=False, shadow=False)
sea = box(400000, 400000, 10, 0, (0, 0, -5))
ssm = save(sea, "SM_Sea", [mat("M_Water")], collision=None, uv_tile=1000.0)
place(ssm, (0, 150000, -1250), folder="Coast", collision=False, shadow=False)

# =====================================================================================================================
# 7. Sky, sun, fog, post, cinematic cameras: Scripts/unreal/sns_lighting.py (also runnable on its own)
# =====================================================================================================================
# Ambience: gulls over the bay.
spawn(gclass("ShadeSoundScatter"), (0, 0, 0), (0, 0, 0), "Audio", sound_name="SFX_Gull", variants=4, interval=unreal.Vector2D(4.0, 10.0),
      distance=unreal.Vector2D(1500.0, 4500.0), height=1100.0, volume=0.7, bias_yaw=90.0)

lvl.save_current_level()
exec(open(os.path.join(os.path.dirname(__file__), "sns_lighting.py"), encoding="utf-8").read(), {"__file__": os.path.join(os.path.dirname(__file__), "sns_lighting.py"), "__name__": "sns_lighting"})
log("LEVEL actors", len(actors.get_all_level_actors()), "decor", len(DECOR))
log("ALL DONE")
