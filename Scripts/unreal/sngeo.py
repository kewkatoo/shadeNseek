"""GeometryScript toolkit shared by the Shade and Seek content scripts.

Conventions: centimetres, Z up. UVs are box-projected at 1 UV tile = UV_TILE cm of world space so every surface shares
texel density and a material's "Tiling" means the same thing everywhere.
"""
import math

import unreal

GP = unreal.GeometryScript_Primitives
GE = unreal.GeometryScript_MeshEdits
GM = unreal.GeometryScript_MeshModeling
GN = unreal.GeometryScript_Normals
GU = unreal.GeometryScript_UVs
GT = unreal.GeometryScript_MeshTransforms
GB = unreal.GeometryScript_MeshBooleans
GQ = unreal.GeometryScript_MeshQueries
GMat = unreal.GeometryScript_Materials
NEW = unreal.GeometryScript_NewAssetUtils
eal = unreal.EditorAssetLibrary

UV_TILE = 200.0
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER
MAT_DIR = "/Game/Shade/Materials"


def log(*a):
    unreal.log("SNS_GEO " + " ".join(str(x) for x in a))


def V(x=0.0, y=0.0, z=0.0):
    return unreal.Vector(float(x), float(y), float(z))


def V2(x, y):
    return unreal.Vector2D(float(x), float(y))


def T(loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    """rot = (pitch, yaw, roll) in degrees."""
    return unreal.Transform(location=V(*loc), rotation=unreal.Rotator(roll=float(rot[2]), pitch=float(rot[0]), yaw=float(rot[1])), scale=V(*scale))


def opts(mat=0, groups=unreal.GeometryScriptPrimitivePolygroupMode.PER_FACE):
    o = unreal.GeometryScriptPrimitiveOptions()
    o.set_editor_property("material_id", int(mat))
    o.set_editor_property("polygroup_mode", groups)
    return o


def new():
    return unreal.DynamicMesh()


def bevel(mesh, dist, subdiv=1):
    o = unreal.GeometryScriptMeshBevelOptions()
    o.set_editor_property("bevel_distance", float(dist))
    o.set_editor_property("subdivisions", int(subdiv))
    GM.apply_mesh_polygroup_bevel(mesh, o)
    return mesh


def append(target, part, xf=None):
    GE.append_mesh(target, part, xf or T())
    return target


def box(dx, dy, dz, mat=0, loc=(0, 0, 0), rot=(0, 0, 0), bev=0.0, origin=CENTER, bev_sub=1):
    m = new()
    GP.append_box(m, opts(mat), T(), float(dx), float(dy), float(dz), 0, 0, 0, origin)
    if bev > 0:
        bevel(m, min(bev, min(dx, dy, dz) * 0.45), bev_sub)
    GT.transform_mesh(m, T(loc, rot))
    return m


def cyl(r, h, mat=0, loc=(0, 0, 0), rot=(0, 0, 0), steps=16, origin=BASE, capped=True):
    m = new()
    GP.append_cylinder(m, opts(mat), T(), float(r), float(h), int(steps), 0, capped, origin)
    GT.transform_mesh(m, T(loc, rot))
    return m


def cone(r0, r1, h, mat=0, loc=(0, 0, 0), rot=(0, 0, 0), steps=16):
    m = new()
    GP.append_cone(m, opts(mat), T(), float(r0), float(r1), float(h), int(steps), 1, True, BASE)
    GT.transform_mesh(m, T(loc, rot))
    return m


def sphere(r, mat=0, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), steps=10):
    m = new()
    GP.append_sphere_lat_long(m, opts(mat, unreal.GeometryScriptPrimitivePolygroupMode.SINGLE_GROUP), T(), float(r), steps, steps * 2, CENTER)
    GT.transform_mesh(m, T(loc, rot, scale))
    return m


def revolve(profile, mat=0, loc=(0, 0, 0), rot=(0, 0, 0), steps=32, degrees=360.0):
    """profile: list of (radius, height) bottom -> top."""
    m = new()
    ro = unreal.GeometryScriptRevolveOptions()
    ro.set_editor_property("revolve_degrees", float(degrees))
    GP.append_revolve_path(m, opts(mat, unreal.GeometryScriptPrimitivePolygroupMode.PER_QUAD), T(), [V2(r, h) for r, h in profile], ro, steps, True)
    GT.transform_mesh(m, T(loc, rot))
    return m


def extrude(poly, h, mat=0, loc=(0, 0, 0), rot=(0, 0, 0)):
    """Extrude a 2D polygon (XY) along +Z."""
    m = new()
    GP.append_simple_extrude_polygon(m, opts(mat), T(), [V2(x, y) for x, y in poly], float(h), 0, True, BASE)
    GT.transform_mesh(m, T(loc, rot))
    return m


def sweep(poly, path_points, mat=0, loop=False, capped=True):
    frames = []
    for i, p in enumerate(path_points):
        a = path_points[max(i - 1, 0)]
        b = path_points[min(i + 1, len(path_points) - 1)]
        d = V(b[0] - a[0], b[1] - a[1], b[2] - a[2])
        frames.append(unreal.Transform(location=V(*p), rotation=unreal.MathLibrary.make_rot_from_x(d), scale=V(1, 1, 1)))
    m = new()
    GP.append_simple_swept_polygon(m, opts(mat), T(), [V2(x, y) for x, y in poly], frames, loop, capped, 1.0, 1.0, 0.0, 1.0)
    return m


def circle(r, n=12):
    return [(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def arch_poly(w, h, spring, n=12):
    """Door/window outline (XZ plane as XY): rectangle of width w, height h with a semicircular top above spring."""
    r = w / 2
    pts = [(-r, 0), (r, 0), (r, spring)]
    for i in range(1, n):
        a = math.pi * i / n
        pts.append((r * math.cos(a), spring + r * math.sin(a) * (h - spring) / r))
    pts.append((-r, spring))
    return pts


def subtract(target, tool):
    o = unreal.GeometryScriptMeshBooleanOptions()
    o.set_editor_property("fill_holes", True)
    o.set_editor_property("simplify_output", False)
    GB.apply_mesh_boolean(target, T(), tool, T(), unreal.GeometryScriptBooleanOperation.SUBTRACT, o)
    return target


def finish_uv_normals(mesh, uv_tile=UV_TILE, angle=40.0):
    sel = unreal.GeometryScriptMeshSelection()
    GU.set_mesh_u_vs_from_box_projection(mesh, 0, T(scale=(uv_tile, uv_tile, uv_tile)), sel, 2)
    so = unreal.GeometryScriptSplitNormalsOptions()
    so.set_editor_property("split_by_opening_angle", True)
    so.set_editor_property("opening_angle_deg", float(angle))
    GN.compute_split_normals(mesh, so, unreal.GeometryScriptCalculateNormalsOptions())
    return mesh


def save(mesh, name, materials, folder="/Game/Shade/Meshes", nanite=False, collision="complex", uv_tile=UV_TILE, angle=40.0, uvs=True):
    """materials: list of MaterialInterface (slot index = material id)."""
    if uvs:
        finish_uv_normals(mesh, uv_tile, angle)
    path = f"{folder}/{name}"
    sm = None
    if eal.does_asset_exist(path):
        # Update in place (the asset may be referenced by an open level, so deleting it is unreliable).
        sm = eal.load_asset(path)
        co = unreal.GeometryScriptCopyMeshToAssetOptions()
        co.set_editor_property("enable_recompute_tangents", True)
        co.set_editor_property("enable_recompute_normals", False)
        try:
            unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, sm, co, unreal.GeometryScriptMeshWriteLOD())
        except Exception as e:
            log("copy failed, recreating", name, e)
            eal.delete_asset(path)
            sm = None
        if sm is not None:
            ns = sm.get_editor_property("nanite_settings")
            ns.set_editor_property("enabled", bool(nanite))
            sm.set_editor_property("nanite_settings", ns)
    if sm is None:
        o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        o.set_editor_property("enable_nanite", bool(nanite))
        o.set_editor_property("enable_collision", collision is not None)
        o.set_editor_property("enable_recompute_tangents", True)
        sm, outcome = NEW.create_new_static_mesh_asset_from_mesh(mesh, path, o)
        if sm is None:
            log("FAILED create", name, outcome)
            return None
    sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=m, material_slot_name=f"Slot{i}") for i, m in enumerate(materials)])
    if collision == "complex":
        bs = sm.get_editor_property("body_setup")
        if bs:
            bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    elif collision == "box":
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).add_simple_collisions(sm, unreal.ScriptCollisionShapeType.BOX)
    eal.save_loaded_asset(sm)
    return sm


def mat(name):
    p = f"{MAT_DIR}/{name}"
    if eal.does_asset_exist(p):
        return eal.load_asset(p)
    log("missing material", name)
    return None


def bounds(mesh):
    bb = GQ.get_mesh_bounding_box(mesh)
    return (bb.min.x, bb.min.y, bb.min.z), (bb.max.x, bb.max.y, bb.max.z)
