"""Fetches the OFL display serif used by the title / menus / narration (Cormorant Garamond, Google Fonts) and bakes
static instances from its variable font (Slate loads static TTFs), then stages every UI font into Content.

Run:  python -I Scripts/fetch_fonts.py <project_root> [--no-stage]
Lato, Lobster and Kalam (also OFL, Google Fonts) are already in asset-library/Fonts.
UI style 2 (Docs/UPGRADE_PLAN.md): Fraunces (display: titles, cards, menus) + Nunito (all UI / body text), both OFL
Google Fonts, baked to static instances the same way.
"""
import os
import shutil
import sys
import urllib.request

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
LIB = os.path.join(ROOT, "asset-library", "Fonts")
STAGE = os.path.join(ROOT, "Content", "Shade", "UI", "Fonts")
FILES = ["CormorantGaramond[wght].ttf", "CormorantGaramond-Italic[wght].ttf", "OFL.txt", "METADATA.pb"]
# (variable source, weight, static file name)
INSTANCES = [
    ("CormorantGaramond[wght].ttf", 500, "CormorantGaramond-Medium.ttf"),
    ("CormorantGaramond[wght].ttf", 600, "CormorantGaramond-SemiBold.ttf"),
    ("CormorantGaramond-Italic[wght].ttf", 400, "CormorantGaramond-Italic.ttf"),
    ("CormorantGaramond-Italic[wght].ttf", 600, "CormorantGaramond-SemiBoldItalic.ttf"),
]

# family folder -> (google/fonts path, files, [(variable source, axis values, static name)])
FAMILIES = {
    "cormorantgaramond": ("ofl/cormorantgaramond/", FILES, [(src, {"wght": w}, n) for src, w, n in INSTANCES]),
    "fraunces": ("ofl/fraunces/", ["Fraunces[SOFT,WONK,opsz,wght].ttf", "Fraunces-Italic[SOFT,WONK,opsz,wght].ttf", "OFL.txt", "METADATA.pb"], [
        ("Fraunces[SOFT,WONK,opsz,wght].ttf", {"wght": 600, "opsz": 72, "SOFT": 50, "WONK": 0}, "Fraunces-SemiBold.ttf"),
        ("Fraunces-Italic[SOFT,WONK,opsz,wght].ttf", {"wght": 600, "opsz": 72, "SOFT": 50, "WONK": 0}, "Fraunces-SemiBoldItalic.ttf"),
    ]),
    "nunito": ("ofl/nunito/", ["Nunito[wght].ttf", "OFL.txt", "METADATA.pb"], [
        ("Nunito[wght].ttf", {"wght": 600}, "Nunito-SemiBold.ttf"),
        ("Nunito[wght].ttf", {"wght": 700}, "Nunito-Bold.ttf"),
        ("Nunito[wght].ttf", {"wght": 800}, "Nunito-ExtraBold.ttf"),
    ]),
}
for fam, (gpath, files, insts) in FAMILIES.items():
    dst = os.path.join(LIB, fam)
    os.makedirs(os.path.join(dst, "static"), exist_ok=True)
    for f in files:
        path = os.path.join(dst, f)
        if not os.path.exists(path):
            url = "https://raw.githubusercontent.com/google/fonts/main/" + gpath + urllib.parse.quote(f)
            req = urllib.request.Request(url, headers={"User-Agent": "shadeNseek-build"})
            with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as o:
                o.write(r.read())
            print("fetched", fam, f)
    for src, axes, name in insts:
        font = TTFont(os.path.join(dst, src))
        inst = instancer.instantiateVariableFont(font, axes, updateFontNames=False)
        inst.save(os.path.join(dst, "static", name))
        print("instanced", name)
if "--no-stage" in sys.argv:
    sys.exit(0)

os.makedirs(STAGE, exist_ok=True)
staged = []
for sub, files in (("lato", ["Lato-Regular.ttf", "Lato-Bold.ttf", "Lato-Black.ttf", "Lato-Italic.ttf"]),
                   ("lobster", ["Lobster-Regular.ttf"]), ("kalam", ["Kalam-Regular.ttf"]),
                   ("cormorantgaramond/static", [n for _, _, n in INSTANCES]),
                   ("fraunces/static", [n for _, _, n in FAMILIES["fraunces"][2]]),
                   ("nunito/static", [n for _, _, n in FAMILIES["nunito"][2]])):
    for f in files:
        shutil.copy2(os.path.join(LIB, sub, f), os.path.join(STAGE, f))
        staged.append(f)
print("staged", len(staged), "fonts into", STAGE)
