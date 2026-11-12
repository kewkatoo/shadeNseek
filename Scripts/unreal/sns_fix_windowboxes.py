"""Window boxes in L_Vellara: solid, with the flowers in the soil.

    SNS_MESHES=window_box Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_meshes.py   (the new trough, first)
    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_windowboxes.py

The boxes were placed without collision, so Shade walked straight through them along the sills. Now each one blocks
pawns (Shade steps up onto it: it is 20 cm tall, the step height is 26) but not the camera or the sunlight traces.
The new SM_WindowBox is a hollow trough whose soil is 4 cm under the rim; the flowers were rested on the old solid
block's top, so everything resting on a box comes down by those 4 cm into the soil. Works in place, run once
(SNS_WB_INSPECT=1 only prints).
"""
import os

import unreal

MAP = "/Game/Shade/Maps/L_Vellara"
INSPECT = os.environ.get("SNS_WB_INSPECT") == "1"
OLD_TOP, NEW_SOIL = 20.0, 16.0
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ML = unreal.MathLibrary
CC = unreal.CollisionChannel
IGNORE = unreal.CollisionResponseType.ECR_IGNORE
lvl.load_level(MAP)


def log(*a):
    unreal.log("SNS_WB " + " ".join(str(x) for x in a))


def mesh_name(a):
    if not isinstance(a, unreal.StaticMeshActor):
        return ""
    sm = a.static_mesh_component.get_editor_property("static_mesh")
    return sm.get_name() if sm else ""


all_actors = actors.get_all_level_actors()
boxes = [a for a in all_actors if mesh_name(a) == "SM_WindowBox"]
log("window boxes", len(boxes))
moved = 0
for b in boxes:
    c = b.static_mesh_component
    if not INSPECT:
        c.set_collision_profile_name("BlockAll")
        c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        c.set_collision_response_to_channel(CC.ECC_SUNLIGHT, IGNORE)
        c.set_collision_response_to_channel(CC.ECC_CAMERA, IGNORE)
    xf = b.get_actor_transform()
    s = b.get_actor_scale3d()
    bz = b.get_actor_location().z
    for a in all_actors:
        if a == b or not isinstance(a, unreal.StaticMeshActor) or mesh_name(a) in ("", "SM_WindowBox"):
            continue
        loc = a.get_actor_location()
        lp = ML.inverse_transform_location(xf, loc)
        if abs(lp.x) > 40.0 or abs(lp.y) > 11.0:
            continue
        o, e = a.get_actor_bounds(False)
        foot = (o.z - e.z) - bz
        if OLD_TOP - 2.0 <= foot <= OLD_TOP + 3.0:
            log("rest in soil", a.get_actor_label(), mesh_name(a), "foot above box", round(foot, 1), "box scale", round(s.x, 2))
            if not INSPECT:
                a.set_actor_location(unreal.Vector(loc.x, loc.y, loc.z - (OLD_TOP - NEW_SOIL)), False, False)
            moved += 1
log("moved", moved, "plants into the soil")
if not INSPECT:
    lvl.save_current_level()
    log("saved")
