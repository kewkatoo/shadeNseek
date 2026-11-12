"""The iron gate at the far end of the church terrace flickered when seen down the lane from the start.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_fix_gate.py

Its bars are ~2 cm thin and it is a Nanite mesh with the default shape preservation: seen from 50 m Nanite simplifies its
clusters, and the thin bars collapse and pop back from frame to frame (worst right after spawning, while the camera,
the dynamic resolution and TSR's history settle). Preserve Area keeps their area as clusters simplify (as for the
foliage), so they stay put and just thin out.
"""
import unreal

eal = unreal.EditorAssetLibrary
P = "/Game/Shade/Props/large_iron_gate/SM_large_iron_gate"
sm = eal.load_asset(P)
ns = sm.get_editor_property("nanite_settings")
ns.set_editor_property("enabled", True)
ns.set_editor_property("shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)
sm.set_editor_property("nanite_settings", ns)
eal.save_loaded_asset(sm)
unreal.log("SNS_GATE nanite " + str(sm.get_editor_property("nanite_settings").shape_preservation))
