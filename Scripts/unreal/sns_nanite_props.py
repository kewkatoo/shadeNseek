"""Turn Nanite on for every dense Poly Haven prop mesh (and keep it on).

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_nanite_props.py

GeometryScriptCreateNewStaticMeshAssetOptions.enable_nanite does not stick in UE 5.8, so the props built by
sns_props.py were all classic meshes - including the island trees (1.6 M and 1.1 M triangles each, ten of them),
potted plants, cliffs and the mountainside: ~24 M triangles redrawn in the depth, base and every shadow-cascade pass,
which held the game near 60 fps on a laptop RTX 4050. sns_props.py now sets the flag explicitly; this script fixes the
existing assets in place. Small props (under MIN_TRIS) stay classic: Nanite gains nothing on a few thousand triangles.
"""
import unreal

eal = unreal.EditorAssetLibrary
ROOT = "/Game/Shade/Props"
MIN_TRIS = 8000

changed = 0
for path in eal.list_assets(ROOT, recursive=True, include_folder=False):
    if "/SM_" not in path:
        continue
    sm = eal.load_asset(path)
    if not isinstance(sm, unreal.StaticMesh):
        continue
    tris = sm.get_num_triangles(0)
    ns = sm.get_editor_property("nanite_settings")
    if ns.get_editor_property("enabled") or tris < MIN_TRIS:
        continue
    masked = any(m.material_interface and m.material_interface.get_blend_mode() == unreal.BlendMode.BLEND_MASKED
                 for m in sm.get_editor_property("static_materials"))
    ns.set_editor_property("enabled", True)
    if masked:
        # Thin leaves and petals: keep their coverage as Nanite simplifies them in the distance.
        ns.set_editor_property("shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)
    sm.set_editor_property("nanite_settings", ns)  # set_editor_property runs the Nanite rebuild (PostEditChange)
    eal.save_loaded_asset(sm)
    changed += 1
    unreal.log(f"SNS_NANITE on {path.split('.')[0]} tris {tris} masked {masked}")
unreal.log(f"SNS_NANITE done, {changed} meshes switched to Nanite")
