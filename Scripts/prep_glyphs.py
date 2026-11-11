"""Builds the HUD's key glyphs from Kenney's Input Prompts pack (CC0, https://kenney.nl/assets/input-prompts, mirrored on
https://opengameart.org/content/input-prompts): the original 128 px "Double" icons kept in
asset-library/UI/kenney_input_prompts/ are cropped to the key itself (so wide keys keep their shape when drawn at a cap
height) into asset-library/UI/game/T_Key_<NAME>.png, which Scripts/unreal/sns_import.py (SNS_ONLY=ui) imports.

Run: python -I Scripts/prep_glyphs.py [project_root]
"""
import os
import sys

from PIL import Image

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
SRC = os.path.join(ROOT, "asset-library", "UI", "kenney_input_prompts")
OUT = os.path.join(ROOT, "asset-library", "UI", "game")
# Letter keys and the mouse only: the SPACE / SHIFT / ESC glyphs squeeze their label below legibility at cap height.
GLYPHS = {"W": "keyboard_w", "A": "keyboard_a", "S": "keyboard_s", "D": "keyboard_d", "E": "keyboard_e", "MOUSE": "mouse_move"}

os.makedirs(OUT, exist_ok=True)
for name, src in GLYPHS.items():
    im = Image.open(os.path.join(SRC, src + ".png")).convert("RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    im = im.crop(box)
    # 2 px transparent margin keeps the edges anti-aliased when Slate scales the brush
    pad = Image.new("RGBA", (im.width + 4, im.height + 4), (255, 255, 255, 0))
    pad.paste(im, (2, 2))
    pad.save(os.path.join(OUT, f"T_Key_{name}.png"))
    print(f"T_Key_{name}.png", pad.size)
