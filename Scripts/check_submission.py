"""Checks the two submission ZIPs in Dist/ against the required layout.

    python -I Scripts/check_submission.py <project root>

shadeNseek.zip:     shadeNseek/shadeNseek.uproject at the root, with shadeNseek/Source, Content, Config; no other top folder,
                    no build artefacts (Binaries, Intermediate, Saved, DerivedDataCache).
asset-library.zip:  originals under asset-library/; every library file named in asset-library/SOURCES.csv is inside.
"""
import csv
import os
import sys
import zipfile

root = sys.argv[1] if len(sys.argv) > 1 else "."
dist = os.path.join(root, "Dist")
bad = []

names = zipfile.ZipFile(os.path.join(dist, "shadeNseek.zip")).namelist()
if any("\\" in n for n in names):
    bad.append("shadeNseek.zip has backslash paths")
tops = {n.split("/")[0] for n in names}
if tops != {"shadeNseek"}:
    bad.append(f"shadeNseek.zip top level is {sorted(tops)}, must be only shadeNseek/")
for need in ("shadeNseek/shadeNseek.uproject", "shadeNseek/Source/", "shadeNseek/Content/", "shadeNseek/Config/"):
    if not any(n == need or n.startswith(need) for n in names):
        bad.append(f"shadeNseek.zip lacks {need}")
for art in ("Binaries", "Intermediate", "Saved", "DerivedDataCache"):
    if any(n.startswith(f"shadeNseek/{art}/") for n in names):
        bad.append(f"shadeNseek.zip contains {art}/")
content = {n for n in names if n.startswith("shadeNseek/Content/")}

lib = set(zipfile.ZipFile(os.path.join(dist, "asset-library.zip")).namelist())
if any("\\" in n for n in lib):
    bad.append("asset-library.zip has backslash paths")
with open(os.path.join(root, "asset-library", "SOURCES.csv"), encoding="utf-8") as fh:
    rows = list(csv.DictReader(fh))
for r in rows:
    f = r["asset_library_file"]
    if f == "MISSING" or f not in lib:
        bad.append(f"no source in asset-library.zip for {r['content_asset']} ({f})")
    pkg = "shadeNseek/Content/" + r["content_asset"][len("/Game/"):] + ".uasset"
    if pkg not in content:
        bad.append(f"{r['content_asset']} not in shadeNseek.zip Content/")
# loose files the game reads from disk (fonts) must have their source in the library too
lib_names = {n.rsplit("/", 1)[-1].lower() for n in lib}
for n in content:
    if not n.endswith("/") and not n.endswith((".uasset", ".umap")) and n.rsplit("/", 1)[-1].lower() not in lib_names:
        bad.append(f"loose content file without library source: {n}")

print(f"project zip: {len(names)} entries; asset zip: {len(lib)} entries; {len(rows)} assets checked")
for b in bad:
    print("FAIL", b)
print("SUBMISSION OK" if not bad else f"SUBMISSION HAS {len(bad)} PROBLEMS")
sys.exit(1 if bad else 0)
