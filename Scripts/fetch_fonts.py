"""Fetches the OFL display serif used by the title / menus / narration (Cormorant Garamond, Google Fonts) and bakes
static instances from its variable font (Slate loads static TTFs), then stages every UI font into Content.

Run:  python -I Scripts/fetch_fonts.py <project_root>
Lato, Lobster and Kalam (also OFL, Google Fonts) are already in asset-library/Fonts.
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
BASE = "https://raw.githubusercontent.com/google/fonts/main/ofl/cormorantgaramond/"
FILES = ["CormorantGaramond[wght].ttf", "CormorantGaramond-Italic[wght].ttf", "OFL.txt", "METADATA.pb"]
# (variable source, weight, static file name)
INSTANCES = [
    ("CormorantGaramond[wght].ttf", 500, "CormorantGaramond-Medium.ttf"),
    ("CormorantGaramond[wght].ttf", 600, "CormorantGaramond-SemiBold.ttf"),
    ("CormorantGaramond-Italic[wght].ttf", 400, "CormorantGaramond-Italic.ttf"),
    ("CormorantGaramond-Italic[wght].ttf", 600, "CormorantGaramond-SemiBoldItalic.ttf"),
]

dst = os.path.join(LIB, "cormorantgaramond")
os.makedirs(os.path.join(dst, "static"), exist_ok=True)
for f in FILES:
    path = os.path.join(dst, f)
    if not os.path.exists(path):
        url = BASE + urllib.parse.quote(f)
        req = urllib.request.Request(url, headers={"User-Agent": "shadeNseek-build"})
        with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as o:
            o.write(r.read())
        print("fetched", f)
for src, wght, name in INSTANCES:
    font = TTFont(os.path.join(dst, src))
    inst = instancer.instantiateVariableFont(font, {"wght": wght}, updateFontNames=False)
    inst.save(os.path.join(dst, "static", name))
    print("instanced", name)

os.makedirs(STAGE, exist_ok=True)
staged = []
for sub, files in (("lato", ["Lato-Regular.ttf", "Lato-Bold.ttf", "Lato-Black.ttf", "Lato-Italic.ttf"]),
                   ("lobster", ["Lobster-Regular.ttf"]), ("kalam", ["Kalam-Regular.ttf"]),
                   ("cormorantgaramond/static", [n for _, _, n in INSTANCES])):
    for f in files:
        shutil.copy2(os.path.join(LIB, sub, f), os.path.join(STAGE, f))
        staged.append(f)
print("staged", len(staged), "fonts into", STAGE)
