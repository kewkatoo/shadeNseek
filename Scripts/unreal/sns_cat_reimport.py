"""Re-import SK_Cat with the legacy FBX importer so its baked tabby vertex colours are kept (Interchange drops them).

Materials are imported as placeholders only so the four sections keep their slot names
(M_CatBody, M_CatEye, M_CatWhisker, M_CatShell); the game assigns the real materials by slot name."""
import os

import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PROJECT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
FBX = os.path.join(PROJECT, "asset-library", "Models", "generated", "SK_Cat.fbx")
DEST = "/Game/Shade/Characters"


def log(*a):
    unreal.log("SNS_CAT " + " ".join(str(x) for x in a))


unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in ("SK_Cat", "SK_Cat_PhysicsAsset", "SK_Cat_Skeleton", "M_CatBody", "M_CatEye", "M_CatWhisker", "M_CatShell"):
    p = f"{DEST}/{n}"
    if eal.does_asset_exist(p):
        log("delete", n, eal.delete_asset(p))

ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("create_physics_asset", False)
ui.set_editor_property("import_materials", True)
ui.set_editor_property("import_textures", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
sk.set_editor_property("import_morph_targets", False)
sk.set_editor_property("import_uniform_scale", 100.0)  # Blender metres -> UE centimetres
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)

task = unreal.AssetImportTask()
task.filename = FBX
task.destination_path = DEST
task.destination_name = "SK_Cat"
task.automated = True
task.replace_existing = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
log("imported", list(task.imported_object_paths or []))
m = eal.load_asset(f"{DEST}/SK_Cat")
if m:
    log("bones", m.get_editor_property("skeleton").get_name() if m.get_editor_property("skeleton") else None)
    log("slots", [str(x.get_editor_property("material_slot_name")) for x in m.get_editor_property("materials")])
    log("bounds", m.get_bounds().box_extent if hasattr(m, "get_bounds") else "?")
eal.save_directory(DEST, only_if_is_dirty=True, recursive=True)
log("ALL DONE")
