"""Submission check: every texture, mesh, sound, font and animation in Content/ against its raw source in asset-library/.

    Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_source_audit.py

For each asset it reads the file it was imported from (asset import data) and looks for that file (by name) anywhere in
asset-library/. Assets that were made in the editor by script (GeometryScript meshes, generated textures) have no
import file: those are exported (FBX / PNG / WAV) into asset-library/<Kind>/generated/ue_export/ so the library holds
their output files, not just the script. Writes asset-library/SOURCES.csv (asset, class, source file, library file).
"""
import csv
import os

import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LIB = os.path.join(ROOT, "asset-library")
ar = unreal.AssetRegistryHelpers.get_asset_registry()
eal = unreal.EditorAssetLibrary


def log(*a):
    unreal.log("SNS_SRC " + " ".join(str(x) for x in a))


# every file in the library, by lower-case name (and stem, for converted downloads: .gltf/.blend -> .fbx etc.)
by_name, by_stem = {}, {}
for d, _, fs in os.walk(LIB):
    for f in fs:
        p = os.path.relpath(os.path.join(d, f), ROOT).replace("\\", "/")
        by_name.setdefault(f.lower(), p)
        by_stem.setdefault(os.path.splitext(f)[0].lower(), p)

KINDS = {
    "Texture2D": "Textures", "TextureCube": "Textures", "Texture2DArray": "Textures",
    "StaticMesh": "Models", "SkeletalMesh": "Models", "AnimSequence": "Models",
    "SoundWave": "Audio", "FontFace": "Fonts", "Font": "Fonts",
}
EXPORTERS = {
    "Texture2D": (unreal.TextureExporterPNG, ".png"),
    "TextureCube": (unreal.TextureCubeExporterHDR if hasattr(unreal, "TextureCubeExporterHDR") else None, ".hdr"),
    "StaticMesh": (unreal.StaticMeshExporterFBX, ".fbx"),
    "SkeletalMesh": (unreal.SkeletalMeshExporterFBX, ".fbx"),
    "AnimSequence": (unreal.AnimSequenceExporterFBX, ".fbx"),
    "SoundWave": (unreal.SoundExporterWAV, ".wav"),
}


def sources(asset):
    out = []
    try:
        aid = asset.get_editor_property("asset_import_data")
        if aid:
            out = [f for f in aid.extract_filenames() if f]
    except Exception:
        pass
    if not out:
        try:
            sf = asset.get_editor_property("source_filename")
            if sf:
                out = [sf]
        except Exception:
            pass
    return out


def find(src):
    f = os.path.basename(src.replace("\\", "/")).lower()
    if f in by_name:
        return by_name[f]
    stem = os.path.splitext(f)[0]
    for s in (stem, stem.replace("_4k", "").replace("_2k", "").replace("_1k", "")):
        if s in by_stem:
            return by_stem[s]
    # converted copy of a library file living under Saved/ etc: same stem in a library folder of that name
    return None


rows, missing, exported = [], [], 0
assets = ar.get_assets_by_path("/Game", recursive=True)
for ad in assets:
    cls = str(ad.asset_class_path.asset_name)
    if cls not in KINDS:
        continue
    pkg = str(ad.package_name)
    if pkg.startswith("/Game/Developers") or "/Collections/" in pkg:
        continue
    asset = ad.get_asset()
    srcs = sources(asset)
    hit = None
    for s in srcs:
        hit = find(s)
        if hit:
            break
    if not hit and srcs:
        # imported from a file that is not in the library: copy the original over if it still exists
        for s in srcs:
            full = s if os.path.isabs(s) else os.path.join(ROOT, s)
            if os.path.isfile(full):
                dst_dir = os.path.join(LIB, KINDS[cls], "generated", "imported_from_project")
                os.makedirs(dst_dir, exist_ok=True)
                dst = os.path.join(dst_dir, os.path.basename(full))
                if not os.path.isfile(dst):
                    with open(full, "rb") as fi, open(dst, "wb") as fo:
                        fo.write(fi.read())
                hit = os.path.relpath(dst, ROOT).replace("\\", "/")
                by_name[os.path.basename(dst).lower()] = hit
                break
    if not hit and cls in EXPORTERS and EXPORTERS[cls][0] is not None:
        exp_cls, ext = EXPORTERS[cls]
        rel = pkg[len("/Game/"):]
        dst = os.path.join(LIB, KINDS[cls], "generated", "ue_export", rel + ext)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        t = unreal.AssetExportTask()
        t.set_editor_property("object", asset)
        t.set_editor_property("filename", dst)
        t.set_editor_property("exporter", exp_cls())
        t.set_editor_property("automated", True)
        t.set_editor_property("prompt", False)
        t.set_editor_property("replace_identical", True)
        if cls in ("StaticMesh", "SkeletalMesh", "AnimSequence"):
            o = unreal.FbxExportOption()
            o.set_editor_property("collision", False)
            o.set_editor_property("level_of_detail", False)
            t.set_editor_property("options", o)
        ok = unreal.Exporter.run_asset_export_task(t)
        if ok and os.path.isfile(dst):
            hit = os.path.relpath(dst, ROOT).replace("\\", "/")
            exported += 1
        else:
            log("EXPORT FAILED", cls, pkg)
    if not hit:
        missing.append((cls, pkg, ";".join(srcs)))
    rows.append((pkg, cls, ";".join(srcs), hit or "MISSING"))

with open(os.path.join(LIB, "SOURCES.csv"), "w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh)
    w.writerow(["content_asset", "class", "imported_from", "asset_library_file"])
    for r in sorted(rows):
        w.writerow(r)
log("assets", len(rows), "exported", exported, "missing", len(missing))
for m in missing:
    log("MISSING", *m)
