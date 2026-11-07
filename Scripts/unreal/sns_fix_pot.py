"""Seat the lemon tree inside the pushable terracotta pot (AShadeFlowerPot "Dev_Pot" in L_Vellara).

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_pot.py      (pass --inspect via SNS_POT_INSPECT=1 to only print)

island_tree_02's pivot is not under its trunk, so placing the plant at the pot's centre left the trunk outside the
rim. This finds the trunk base (centroid of the lowest vertices), then moves the Plant component so that point sits
on the pot's axis just below the soil disc. Only the Plant component's relative location changes.
"""
import os

import unreal

MAP = "/Game/Shade/Maps/L_Vellara"
INSPECT = os.environ.get("SNS_POT_INSPECT") == "1"
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
GA = unreal.GeometryScript_AssetUtils
GQ = unreal.GeometryScript_MeshQueries
lvl.load_level(MAP)


def log(*a):
    unreal.log("SNS_POT " + " ".join(str(x) for x in a))


def trunk_base(sm):
    m = unreal.DynamicMesh()
    GA.copy_mesh_from_static_mesh(sm, m, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    bb = GQ.get_mesh_bounding_box(m)
    zmin, zmax = bb.min.z, bb.max.z
    cut_z = zmin + 0.04 * (zmax - zmin)
    # keep only the bottom 4% of the tree (the trunk foot): plane cut, check which side survived
    for flip in (False, True):
        c = unreal.DynamicMesh()
        GA.copy_mesh_from_static_mesh(sm, c, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        opts = unreal.GeometryScriptMeshPlaneCutOptions()
        opts.set_editor_property("flip_cut_side", flip)
        opts.set_editor_property("fill_holes", False)
        unreal.GeometryScript_MeshBooleans.apply_mesh_plane_cut(c, unreal.Transform(unreal.Vector(0, 0, cut_z)), opts)
        fb = GQ.get_mesh_bounding_box(c)
        if fb.max.z <= cut_z + 1.0:
            break
    cx = (fb.min.x + fb.max.x) / 2
    cy = (fb.min.y + fb.max.y) / 2
    log("tree bounds", bb.min, bb.max, "foot box", fb.min, fb.max, "centre", round(cx, 1), round(cy, 1))
    return cx, cy, zmin


pot = next((a for a in actors.get_all_level_actors() if "Dev_Pot" in [str(t) for t in a.tags]), None)
if not pot:
    log("ERROR no Dev_Pot")
else:
    potc = pot.get_editor_property("pot")
    plant = pot.get_editor_property("plant")
    soil = pot.get_editor_property("soil")
    pm = potc.get_editor_property("static_mesh")
    pb = pm.get_bounds()
    ps = potc.get_editor_property("relative_scale3d")
    log("pot mesh", pm.get_name(), "bounds origin", pb.origin, "extent", pb.box_extent, "scale", ps, "rel", potc.get_editor_property("relative_location"))
    log("soil rel", soil.get_editor_property("relative_location"), "scale", soil.get_editor_property("relative_scale3d"))
    tree = plant.get_editor_property("static_mesh")
    ts = plant.get_editor_property("relative_scale3d")
    log("plant", tree.get_name(), "rel", plant.get_editor_property("relative_location"), "rot", plant.get_editor_property("relative_rotation"), "scale", ts)
    cx, cy, zmin = trunk_base(tree)
    # Pot axis in the Pivot frame (the pot mesh may not be centred on its pivot either).
    ax = (pb.origin.x) * ps.x + potc.get_editor_property("relative_location").x
    ay = (pb.origin.y) * ps.y + potc.get_editor_property("relative_location").y
    soil_z = soil.get_editor_property("relative_location").z
    new = unreal.Vector(ax - cx * ts.x, ay - cy * ts.y, soil_z - 12.0 - zmin * ts.z)
    log("pot axis", round(ax, 1), round(ay, 1), "soil z", round(soil_z, 1), "-> plant rel", new)
    if not INSPECT:
        plant.set_editor_property("relative_location", new)
        lvl.save_current_level()
        log("saved")
