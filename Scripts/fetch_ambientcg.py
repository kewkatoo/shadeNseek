"""Download the CC0 ambientCG materials used by Shade and Seek into asset-library/Textures/ambientcg/<AssetId>/.

Every ambientCG asset is CC0 1.0 (public domain). Each material is fetched as its <res>-JPG zip and stored with the
same file names as our Poly Haven sets, so Scripts/unreal/sns_import.py can import both the same way:
    <AssetId>_diff_<res>.jpg     <- Color
    <AssetId>_nor_dx_<res>.jpg   <- NormalDX
    <AssetId>_arm_<res>.jpg      <- packed R = AmbientOcclusion (white if absent), G = Roughness, B = Metalness (0 if absent)
    <AssetId>_alpha_<res>.jpg    <- Opacity (leaf atlases only)
NormalGL, Displacement, previews and the .mtlx/.usd side files are not kept. A few ambientCG sheets are 2:1
(e.g. PavingStones150, PaintedPlaster017): those are stacked to a square (copies of the seamless sheet, no resampling)
so a 1:1 UV tile keeps the stones / cracks in proportion. Records (author, source URL) go to
asset-library/ambientcg_records.json.

Run:  python -I Scripts/fetch_ambientcg.py <project_root> [--force] [--only Id1,Id2]
"""
import io
import json
import os
import sys
import urllib.parse
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

_args = sys.argv[1:]
FORCE = "--force" in _args
ONLY = set()
if "--only" in _args:
    ONLY = set(filter(None, _args[_args.index("--only") + 1].split(",")))
    del _args[_args.index("--only"):_args.index("--only") + 2]
_args = [a for a in _args if a != "--force"]
ROOT = os.path.abspath(_args[0] if _args else ".")
LIB = os.path.join(ROOT, "asset-library")
OUT = os.path.join(LIB, "Textures", "ambientcg")
API = "https://ambientcg.com/api/v2/full_json"
UA = {"User-Agent": "ShadeAndSeek-asset-fetch/1.0"}
AUTHOR = "ambientCG / Lennart Demes"

# AssetId -> resolution ("2K" / "1K")
MATERIALS = {
    "PavingStones150": "2K",      # warm beige sampietrini arcs: Via del Gatto lane + alley (MI_Cobble)
    "Tiles143": "2K",             # limestone flags with joints: piazzetta + church terrace (MI_Flag)
    "Travertine009": "2K",        # jointless travertine: stair treads, sills, thresholds, benches (MI_StepStone)
    "RoofingTiles014B": "2K",     # weathered terracotta coppi (MI_RoofTiles)
    "PaintedWood009B": "2K",      # chipped green paint: green / teal / olive shutters, lemon cart body
    "PaintedWood007B": "2K",      # chipped dark-blue paint: blue shutters + doors
    "PaintedPlaster017": "2K",    # white painted lime plaster with hairline cracks (replaces granular white_stucco_02)
    "PaintedPlaster018": "2K",    # yellow plaster peeling to white (MI_Wall_Lemon)
    "GlazedTerracotta001": "1K",  # glazed terracotta for the window boxes (MI_Terracotta; floor tiles showed grout)
    "Fabric061": "1K",            # real canvas weave normal/roughness for M_Cloth (awnings, laundry)
    "Fabric071": "1K",            # blue/white striped linen (MI_Linen_BlueStripe)
    "LeafSet017": "1K",           # ivy leaf atlas with opacity (replaces the procedural T_Leafy_D cards)
}
KEEP_ALPHA = {"LeafSet017"}  # other sets ship an all-white Opacity (e.g. RoofingTiles014B): not kept


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def fetch_bytes(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r:
        return r.read()


def info(aid):
    q = urllib.parse.urlencode({"id": aid, "include": "downloadData,displayData,tagData"})
    found = get_json(f"{API}?{q}")["foundAssets"]
    if not found:
        raise RuntimeError(f"{aid}: not found on ambientCG")
    return found[0]


def zip_entry(asset, attr):
    for d in asset["downloadFolders"]["default"]["downloadFiletypeCategories"]["zip"]["downloads"]:
        if d["attribute"] == attr:
            return d
    raise RuntimeError(f"{asset['assetId']}: no {attr} download")


def squared(raw):
    """Stack copies of a seamless 2:1 (or n:1) sheet into a square image; square sheets pass through untouched."""
    im = Image.open(io.BytesIO(raw))
    w, h = im.size
    if w == h:
        return raw
    n, rem = divmod(max(w, h), min(w, h))
    if rem:
        raise RuntimeError(f"cannot square a {w}x{h} sheet")
    sq = Image.new(im.mode, (max(w, h), max(w, h)))
    for k in range(n):
        sq.paste(im, (0, k * h) if w > h else (k * w, 0))
    buf = io.BytesIO()
    sq.save(buf, "JPEG", quality=95, subsampling=0)
    return buf.getvalue()


def fetch(aid, res):
    lo = res.lower()
    folder = os.path.join(OUT, aid)
    names = {k: os.path.join(folder, f"{aid}_{k}_{lo}.jpg") for k in ("diff", "nor_dx", "arm")}
    asset = info(aid)
    if not FORCE and all(os.path.exists(p) for p in names.values()):
        return asset, "cached", 0
    entry = zip_entry(asset, f"{res}-JPG")
    data = fetch_bytes(entry["downloadLink"])
    maps = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for member in z.namelist():
            base = os.path.basename(member)  # never trust paths inside the archive
            prefix = f"{aid}_{res}-JPG_"
            if not (base.startswith(prefix) and base.lower().endswith(".jpg")):
                continue
            maps[base[len(prefix):-4]] = squared(z.read(member))
    if "Color" not in maps or "NormalDX" not in maps or "Roughness" not in maps:
        raise RuntimeError(f"{aid}: missing maps, got {sorted(maps)}")
    os.makedirs(folder, exist_ok=True)
    with open(names["diff"], "wb") as f:
        f.write(maps["Color"])
    with open(names["nor_dx"], "wb") as f:
        f.write(maps["NormalDX"])
    rough = Image.open(io.BytesIO(maps["Roughness"])).convert("L")
    size = rough.size

    def gray(key, fill):
        if key in maps:
            return Image.open(io.BytesIO(maps[key])).convert("L").resize(size)
        return Image.new("L", size, fill)

    arm = Image.merge("RGB", (gray("AmbientOcclusion", 255), rough, gray("Metalness", 0)))
    arm.save(names["arm"], quality=95, subsampling=0)  # 4:4:4 - chroma subsampling would bleed the packed channels
    if "Opacity" in maps and aid in KEEP_ALPHA:
        with open(os.path.join(folder, f"{aid}_alpha_{lo}.jpg"), "wb") as f:
            f.write(maps["Opacity"])
    return asset, "ok", len(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    unknown = ONLY - set(MATERIALS)
    if unknown:
        raise SystemExit(f"--only ids not in MATERIALS: {sorted(unknown)}")
    todo = {aid: res for aid, res in MATERIALS.items() if not ONLY or aid in ONLY}
    with ThreadPoolExecutor(4) as ex:
        for aid, (asset, status, nbytes) in zip(todo, ex.map(lambda kv: fetch(*kv), todo.items())):
            print(status, aid, todo[aid], f"{nbytes / 1e6:.1f} MB zip" if nbytes else "")
    # Records always cover the whole list, whatever --only fetched.
    with ThreadPoolExecutor(4) as ex:
        assets = dict(zip(MATERIALS, ex.map(info, MATERIALS)))
    records = []
    for aid, res in MATERIALS.items():
        asset = assets[aid]
        records.append({
            "path": f"asset-library/Textures/ambientcg/{aid}",
            "source_name": f"ambientCG - {asset.get('displayName') or aid}",
            "source_url": f"https://ambientcg.com/a/{aid}",
            "license": "CC0-1.0",
            "author": AUTHOR,
            "resolution": res.lower(),
        })
    with open(os.path.join(LIB, "ambientcg_records.json"), "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2)
    print("done", len(records), "materials")


if __name__ == "__main__":
    main()
