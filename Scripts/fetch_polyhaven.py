"""Download the CC0 Poly Haven sources used by Shade and Seek into asset-library/.

Every Poly Haven asset is CC0 1.0 (public domain, no attribution needed, no AI/redistribution restriction).
We still record source URL + author per asset in asset-library/CREDITS.md and manifest.json.

Run:  python -I Scripts/fetch_polyhaven.py <project_root>
"""
import hashlib
import json
import os
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
LIB = os.path.join(ROOT, "asset-library")
API = "https://api.polyhaven.com"
UA = {"User-Agent": "ShadeAndSeek-asset-fetch/1.0"}

# Surface textures: id -> resolution
TEXTURES = {
    "white_stucco": "2k", "white_stucco_02": "2k", "white_rough_plaster": "2k", "yellow_plaster": "2k",
    "red_plaster_weathered": "2k", "blue_plaster_weathered": "2k", "painted_plaster_wall": "2k",
    "clay_roof_tiles_02": "2k", "cobblestone_floor_08": "2k", "grey_stone_path": "2k", "stone_pathway": "2k",
    "rustic_stone_wall": "2k", "stacked_stone_wall": "2k", "terracotta_floor_tiles": "2k",
    "blue_painted_planks": "2k", "distressed_painted_planks": "2k", "weathered_planks": "2k",
    "fabric_pattern_07": "1k", "terry_cloth": "1k", "coast_sand_01": "2k",
    "marble_cliff_02": "2k",
}
TEX_MAPS = ["Diffuse", "nor_dx", "arm"]

# Models: id -> resolution
MODELS = {
    # vegetation
    "island_tree_01": "2k", "island_tree_02": "2k", "shrub_01": "1k", "shrub_02": "1k", "shrub_03": "1k", "shrub_sorrel_01": "1k",
    "flower_gazania": "1k", "flower_heliophila": "1k", "flower_ursinia": "1k", "flower_empodium": "1k",
    "periwinkle_plant": "1k", "grass_medium_02": "1k",
    # pots and ceramics
    "planter_pot_clay": "1k", "potted_plant_01": "1k", "potted_plant_02": "1k", "potted_plant_04": "1k",
    "antique_ceramic_vase_01": "1k", "ceramic_vase_01": "1k", "ceramic_vase_02": "1k", "jug_01": "1k",
    # cafe + street furniture
    "outdoor_table_chair_set_01": "1k", "painted_wooden_chair_01": "1k", "painted_wooden_bench": "1k",
    "painted_wooden_table": "1k", "street_lamp_01": "1k", "street_lamp_02": "1k", "wooden_lantern_01": "1k",
    "wooden_stool_01": "1k", "folding_wooden_stool": "1k", "tea_set_01": "1k",
    # harbour + coast
    "ship_pinnace": "2k", "dutch_ship_medium": "1k", "modular_wooden_pier": "1k", "lifebuoy": "1k",
    "mountainside": "2k", "coastal_cliff_01": "2k", "coastal_cliff_02": "2k", "coastal_cliff_04": "2k", "coast_rocks_01": "1k",
    # clutter
    "lemon": "1k", "wine_bottles_01": "1k", "wine_barrel_01": "1k", "wicker_basket_01": "1k", "wicker_basket_02": "1k",
    "wooden_bucket_01": "1k", "watering_can_metal_01": "1k", "lion_head": "1k", "marble_bust_01": "1k",
    "concrete_cat_statue": "1k", "exterior_aircon_unit": "1k", "modular_electric_cables": "1k",
    "garden_hose_wall_mounted_01": "1k",
}
EXTRA_MODEL_MAPS = ("nor_dx", "alpha")


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def download(url, dest, md5=None):
    if os.path.exists(dest) and md5:
        with open(dest, "rb") as f:
            if hashlib.md5(f.read()).hexdigest() == md5:
                return "cached"
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    os.replace(tmp, dest)
    return "ok"


def jobs_for_texture(tid, res):
    files = get_json(f"{API}/files/{tid}")
    out = []
    for m in TEX_MAPS:
        key = m if m in files else ("col_1" if m == "Diffuse" and "col_1" in files else m)
        e = files.get(key, {}).get(res, {}).get("jpg")
        if e:
            name = os.path.basename(e["url"])
            if key != m:  # patterned fabrics ship colourways (col_1..); we use col_1 as the diffuse
                name = f"{tid}_diff_{res}.jpg"
            out.append((e["url"], os.path.join(LIB, "Textures", "polyhaven", tid, name), e.get("md5")))
    return out


def jobs_for_model(mid, res):
    files = get_json(f"{API}/files/{mid}")
    g = files["gltf"][res]["gltf"]
    base = os.path.join(LIB, "Models", "polyhaven", mid)
    out = [(g["url"], os.path.join(base, os.path.basename(g["url"])), g.get("md5"))]
    for rel, e in g.get("include", {}).items():
        out.append((e["url"], os.path.join(base, rel.replace("/", os.sep)), e.get("md5")))
    # DirectX normals + alpha masks are not referenced by the glTF but our UE materials want them.
    for key, val in files.items():
        if key.endswith(EXTRA_MODEL_MAPS) and isinstance(val, dict) and res in val and "jpg" in val[res]:
            e = val[res]["jpg"]
            out.append((e["url"], os.path.join(base, "textures", os.path.basename(e["url"])), e.get("md5")))
    return out


def main():
    jobs = []
    meta = []
    for tid, res in TEXTURES.items():
        jobs += jobs_for_texture(tid, res)
        meta.append(("texture", tid, res))
    for mid, res in MODELS.items():
        jobs += jobs_for_model(mid, res)
        meta.append(("model", mid, res))
    print(f"{len(jobs)} files")
    with ThreadPoolExecutor(8) as ex:
        for (url, dest, md5), st in zip(jobs, ex.map(lambda j: download(*j), jobs)):
            print(st, os.path.relpath(dest, LIB))

    # Credits / manifest records (authors from the info API).
    records = []
    for kind, aid, res in meta:
        info = get_json(f"{API}/info/{aid}")
        folder = ("Textures" if kind == "texture" else "Models") + f"/polyhaven/{aid}"
        records.append({
            "path": f"asset-library/{folder}",
            "source_name": f"Poly Haven - {info.get('name', aid)}",
            "source_url": f"https://polyhaven.com/a/{aid}",
            "license": "CC0-1.0",
            "author": ", ".join(info.get("authors", {}).keys()),
            "resolution": res,
        })
    with open(os.path.join(LIB, "polyhaven_records.json"), "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2)
    print("done", len(records), "assets")


if __name__ == "__main__":
    main()
