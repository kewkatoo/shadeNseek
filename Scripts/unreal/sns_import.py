"""Stage A - import raw sources from asset-library/ into /Game/Shade.

Run headless:  Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_import.py
Pass SNS_ONLY=audio,textures,gentex,models,cat via environment to run a subset (gentex = generated textures only).
"""
import glob
import os

import unreal

PROJECT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
LIB = os.path.join(PROJECT, "asset-library")
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
ONLY = set(filter(None, os.environ.get("SNS_ONLY", "").split(",")))


def want(stage):
    return not ONLY or stage in ONLY


def log(*a):
    unreal.log("SNS_IMPORT " + " ".join(str(x) for x in a))


def import_file(path, dest_dir, dest_name=None, replace=True, options=None):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = dest_dir
    if dest_name:
        task.destination_name = dest_name
    task.automated = True
    task.replace_existing = replace
    task.save = True
    if options is not None:
        task.options = options
    tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths or [])
    if not paths:
        log("FAILED", path)
    return paths


def load(path):
    return eal.load_asset(path.split(".")[0]) if path else None


# ---------------------------------------------------------------------------------------------------------------------
TWO_D = ("MUS_", "AMB_", "SFX_UI_", "SFX_Solve", "SFX_Objective", "SFX_BellToll")
FAR = ("SFX_Gull",)


def attenuation(name, inner, falloff):
    path = f"/Game/Shade/Audio/{name}"
    att = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, "/Game/Shade/Audio", unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    s = att.get_editor_property("attenuation")
    s.set_editor_property("attenuate", True)
    s.set_editor_property("spatialize", True)
    s.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    s.set_editor_property("attenuation_shape_extents", unreal.Vector(inner, 0, 0))
    s.set_editor_property("falloff_distance", float(falloff))
    s.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    att.set_editor_property("attenuation", s)
    eal.save_loaded_asset(att)
    return att


def import_audio():
    near = attenuation("ATT_Near", 200, 2400)
    far = attenuation("ATT_Far", 1500, 9000)
    for wav in sorted(glob.glob(os.path.join(LIB, "Audio", "generated", "*.wav"))):
        name = os.path.splitext(os.path.basename(wav))[0]
        paths = import_file(wav, "/Game/Shade/Audio", name)
        sw = load(paths[0]) if paths else None
        if not sw:
            continue
        sw.set_editor_property("looping", name.endswith("_Loop") or name in ("MUS_Theme",))
        if not name.startswith(TWO_D):
            sw.set_editor_property("attenuation_settings", far if name.startswith(FAR) else near)
        eal.save_loaded_asset(sw)
    log("audio done")


# ---------------------------------------------------------------------------------------------------------------------
def configure_texture(tex, kind):
    if kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "linear":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    elif kind == "gray":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    eal.save_loaded_asset(tex)


def kind_for(filename):
    f = os.path.basename(filename).lower()
    if "nor_dx" in f or "nor_gl" in f or f.endswith("_n.png") or "normal" in f:
        return "normal"
    if any(k in f for k in ("_arm", "rough", "_r.png", "flatarm")):
        return "linear"
    if any(k in f for k in ("alpha", "wearmask", "t_wisp", "inknoise", "furstrands")):
        return "linear"  # single-channel maps are sampled as Masks everywhere
    return "color"


def import_textures(polyhaven=True):
    for set_dir in sorted(glob.glob(os.path.join(LIB, "Textures", "polyhaven", "*"))) if polyhaven else []:
        sid = os.path.basename(set_dir)
        for f in sorted(glob.glob(os.path.join(set_dir, "*.jpg"))):
            mp = os.path.basename(f).replace(sid + "_", "").rsplit("_", 1)[0]  # diff / nor_dx / arm
            paths = import_file(f, f"/Game/Shade/Textures/{sid}", f"T_{sid}_{mp}")
            if paths:
                configure_texture(load(paths[0]), kind_for(f))
        log("texture set", sid)
    for f in sorted(glob.glob(os.path.join(LIB, "Textures", "generated", "*.png"))):
        name = os.path.splitext(os.path.basename(f))[0]
        paths = import_file(f, "/Game/Shade/Textures/Gen", name)
        if paths:
            configure_texture(load(paths[0]), kind_for(f))
    log("generated textures done")


# ---------------------------------------------------------------------------------------------------------------------
def import_models():
    for mdir in sorted(glob.glob(os.path.join(LIB, "Models", "polyhaven", "*"))):
        mid = os.path.basename(mdir)
        dest = f"/Game/Shade/Props/{mid}"
        gltf = glob.glob(os.path.join(mdir, "*.gltf"))
        if not gltf:
            continue
        merged = eal.does_directory_exist(dest) and any(x.split("/")[-1].startswith("SM_") for x in eal.list_assets(dest, recursive=False, include_folder=False))
        if not merged and not eal.does_directory_have_assets(dest + "/Mesh"):
            paths = import_file(gltf[0], dest + "/Mesh")
            log("model", mid, len(paths), "objects")
        for f in sorted(glob.glob(os.path.join(mdir, "textures", "*.jpg"))):
            base = os.path.splitext(os.path.basename(f))[0]
            base = base.rsplit("_", 1)[0]  # drop resolution suffix
            if "nor_gl" in base:
                continue
            if eal.does_asset_exist(f"{dest}/Maps/T_{base}"):
                continue
            paths = import_file(f, dest + "/Maps", "T_" + base)
            if paths:
                configure_texture(load(paths[0]), kind_for(f))
    log("models done")


def import_cat():
    # The legacy FBX importer keeps the baked tabby vertex colours (Interchange drops them): see sns_cat_reimport.py
    exec(open(os.path.join(os.path.dirname(__file__), "sns_cat_reimport.py"), encoding="utf-8").read(), {"__file__": os.path.join(os.path.dirname(__file__), "sns_cat_reimport.py")})


if want("audio"):
    import_audio()
if want("textures"):
    import_textures()
elif "gentex" in ONLY:
    import_textures(polyhaven=False)
if want("cat"):
    import_cat()
if want("models"):
    import_models()
unreal.EditorAssetLibrary.save_directory("/Game/Shade", only_if_is_dirty=True, recursive=True)
log("ALL DONE")
