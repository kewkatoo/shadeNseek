"""Stage C - merge each imported Poly Haven model into SM_<id> (or SM_<id>_V<n> for variant sets) with our materials.

Interchange imports one StaticMesh per glTF node, so the pieces are combined with GeometryScript, material slots are
remapped, and the Interchange materials are swapped for instances of M_Foliage (alpha-tested) or M_Prop.
"""
import os
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402
from sngeo import NEW, GQ, log  # noqa: E402

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
GA = unreal.GeometryScript_AssetUtils
GMat = unreal.GeometryScript_Materials
GE = unreal.GeometryScript_MeshEdits
ROOT = "/Game/Shade/Props"

COLLISION_BOX = {"wine_barrel_01", "planter_pot_clay", "potted_plant_01", "potted_plant_02", "potted_plant_04", "wicker_basket_01",
                 "wicker_basket_02", "wooden_bucket_01", "painted_wooden_bench", "painted_wooden_table", "wooden_stool_01",
                 "folding_wooden_stool", "outdoor_table_chair_set_01", "painted_wooden_chair_01", "antique_ceramic_vase_01",
                 "ceramic_vase_01", "ceramic_vase_02", "exterior_aircon_unit", "lifebuoy", "ship_pinnace", "marble_bust_01",
                 "concrete_cat_statue"}
COLLISION_COMPLEX = {"coastal_cliff_01", "coastal_cliff_02", "coastal_cliff_04", "coast_rocks_01", "mountainside", "modular_wooden_pier"}
NO_SPLIT = {"ship_pinnace", "dutch_ship_medium", "outdoor_table_chair_set_01", "modular_wooden_pier", "tea_set_01", "wine_bottles_01",
            "island_tree_01", "island_tree_02", "mountainside", "coastal_cliff_01", "coastal_cliff_02", "coastal_cliff_04", "coast_rocks_01",
            "street_lamp_01", "street_lamp_02", "wooden_lantern_01", "exterior_aircon_unit", "modular_electric_cables",
            "garden_hose_wall_mounted_01", "lion_head", "marble_bust_01", "concrete_cat_statue", "potted_plant_01", "potted_plant_02",
            "potted_plant_04", "planter_pot_clay", "lemon"}
TRUNK_HINTS = ("trunk", "branch", "bark")


def mi(name, parent, textures, folder, scalars=None):
    path = f"{folder}/{name}"
    inst = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, parent)
    for k, v in textures.items():
        if v:
            mel.set_material_instance_texture_parameter_value(inst, k, v)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(inst, k, float(v))
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def maps_for(mid, slot_name, maps):
    candidates = []
    part = slot_name.replace(mid + "_", "") if slot_name.startswith(mid + "_") else None
    if part:
        candidates.append(f"{mid}_{part}")
    candidates.append(slot_name)
    candidates.append(mid)

    def pick(pre):
        return {k: eal.load_asset(maps[f"T_{pre}_{k}"]) if f"T_{pre}_{k}" in maps else None for k in ("diff", "alpha", "arm", "nor_dx")}

    for pre in candidates:
        if f"T_{pre}_diff" in maps:
            return pick(pre)
    if part:
        for k in maps:
            if k.endswith("_diff") and part in k:
                return pick(k[2:-5])
    return None


def merge_model(mid, foliage, prop):
    folder = f"{ROOT}/{mid}"
    assets = [a.split(".")[0] for a in eal.list_assets(folder, recursive=True, include_folder=False)]
    maps = {a.split("/")[-1]: a for a in assets if "/Maps/" in a}
    pieces = [a for a in assets if "/Mesh/" in a and isinstance(eal.load_asset(a), unreal.StaticMesh)]
    if not pieces:
        log("no pieces", mid)
        return
    combined = unreal.DynamicMesh()
    global_slots = []
    opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
    lod = unreal.GeometryScriptMeshReadLOD()
    for p in pieces:
        if "LOD1" in p or "LOD2" in p or "LOD3" in p:
            continue
        sm = eal.load_asset(p)
        piece = unreal.DynamicMesh()
        GA.copy_mesh_from_static_mesh(sm, piece, opts, lod)
        slots = [str(s.get_editor_property("material_slot_name")) for s in sm.get_editor_property("static_materials")]
        GMat.enable_material_i_ds(piece)
        for local, name in enumerate(slots):
            if name not in global_slots:
                global_slots.append(name)
            GMat.remap_material_i_ds(piece, local, 1000 + global_slots.index(name))
        GE.append_mesh(combined, piece, unreal.Transform())
    for gi in range(len(global_slots)):
        GMat.remap_material_i_ds(combined, 1000 + gi, gi)

    materials = []
    any_alpha = False
    for name in global_slots:
        m = maps_for(mid, name, maps)
        if not m or not m["diff"]:
            log("no maps for", mid, name)
            materials.append(None)
            continue
        alpha = m["alpha"]
        any_alpha |= alpha is not None
        texs = {"BaseColorTex": m["diff"], "NormalTex": m["nor_dx"], "ARMTex": m["arm"]}
        if alpha:
            texs["AlphaTex"] = alpha
        # Nanite trees stay still: WPO on million-triangle masked foliage invalidates virtual shadow maps every frame.
        sc = {"WindAmp": 0.0 if "tree" in mid else 1.8} if alpha else None
        materials.append(mi(f"MI_{name}", foliage if alpha else prop, texs, f"{folder}/Materials", sc))

    tris = GQ.get_num_triangle_i_ds(combined) if hasattr(GQ, "get_num_triangle_i_ds") else 0
    if mid in NO_SPLIT:
        groups_meshes = [combined]
    else:
        pool = unreal.DynamicMeshPool()
        _, comps = unreal.GeometryScript_MeshDecomposition.split_mesh_by_components(combined, pool)
        boxes = [GQ.get_mesh_bounding_box(c) for c in comps]
        parent = list(range(len(comps)))

        def find(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        pad = 2.0
        for i in range(len(comps)):
            for j in range(i + 1, len(comps)):
                a, b = boxes[i], boxes[j]
                if a.min.x - pad <= b.max.x and b.min.x - pad <= a.max.x and a.min.y - pad <= b.max.y and b.min.y - pad <= a.max.y:
                    parent[find(i)] = find(j)
        clusters = {}
        for i in range(len(comps)):
            clusters.setdefault(find(i), []).append(i)
        groups = sorted(clusters.values(), key=lambda g: min(boxes[i].min.x for i in g))
        groups = [g for g in groups if max(max(boxes[i].max.x for i in g) - min(boxes[i].min.x for i in g),
                                           max(boxes[i].max.y for i in g) - min(boxes[i].min.y for i in g)) > 4.0] or groups
        groups_meshes = []
        for g in groups:
            mesh = unreal.DynamicMesh()
            for i in g:
                GE.append_mesh(mesh, comps[i], unreal.Transform())
            groups_meshes.append(mesh)

    for vi, mesh in enumerate(groups_meshes):
        bb = GQ.get_mesh_bounding_box(mesh)
        cx, cy = (bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2
        unreal.GeometryScript_MeshTransforms.translate_mesh(mesh, unreal.Vector(-cx, -cy, -bb.min.z))
        name = f"SM_{mid}" if len(groups_meshes) == 1 else f"SM_{mid}_V{vi}"
        path = f"{folder}/{name}"
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        ntri = GQ.get_num_triangle_i_ds(mesh) if hasattr(GQ, "get_num_triangle_i_ds") else 0
        heavy = ntri > 150000
        o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        o.set_editor_property("enable_nanite", heavy or (not any_alpha and mid not in COLLISION_BOX))
        o.set_editor_property("enable_collision", mid in COLLISION_COMPLEX)
        o.set_editor_property("enable_recompute_normals", False)
        o.set_editor_property("enable_recompute_tangents", True)
        sm, outcome = NEW.create_new_static_mesh_asset_from_mesh(mesh, path, o)
        if sm is None:
            log("FAILED", name, outcome)
            continue
        sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=m, material_slot_name=n) for m, n in zip(materials, global_slots)])
        if heavy and any_alpha:
            ns = sm.get_editor_property("nanite_settings")
            try:
                ns.set_editor_property("preserve_area", True)
                sm.set_editor_property("nanite_settings", ns)
            except Exception:
                pass
        if mid in COLLISION_BOX:
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).add_simple_collisions(sm, unreal.ScriptCollisionShapeType.BOX)
        if mid in COLLISION_COMPLEX:
            bs = sm.get_editor_property("body_setup")
            if bs:
                bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        eal.save_loaded_asset(sm)
        log("prop", name, "size", round(bb.max.x - bb.min.x), round(bb.max.y - bb.min.y), round(bb.max.z - bb.min.z), "tris", ntri, "nanite", heavy or (not any_alpha and mid not in COLLISION_BOX))


foliage = eal.load_asset("/Game/Shade/Materials/M_Foliage")
prop = eal.load_asset("/Game/Shade/Materials/M_Prop")
only = set(filter(None, os.environ.get("SNS_ONLY", "").split(",")))
mids = sorted({a.split("/")[4] for a in eal.list_assets(ROOT, recursive=True, include_folder=False)})
for mid in mids:
    if only and mid not in only:
        continue
    try:
        merge_model(mid, foliage, prop)
    except Exception as e:
        import traceback
        log("ERROR", mid, e, traceback.format_exc())
eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
log("ALL DONE")
