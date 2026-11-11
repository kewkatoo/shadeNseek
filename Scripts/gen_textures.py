"""Original procedural textures for Shade and Seek (things with no good CC0 scan equivalent).

  T_Awning_<v>_D/N        striped canvas (terracotta / sea blue / lemon) with weave + sun fade
  T_Majolica_D/R/N        glazed dome tiles (yellow-green-blue chevrons, Amalfi-coast style)
  T_Bougain_D (RGBA)      bougainvillea spray card: magenta paper bracts + leaves, alpha in A
  T_Leafy_D (RGBA)        generic climbing-leaf card (ivy / jasmine)
  T_Sign_*                hand-painted shop signs and a ceramic street-name tile (OFL fonts)
  T_Linen_<v>_D           laundry fabrics
  T_WaterNormal, T_WearMask, T_Wisp, T_FurNormal, T_InkNoise

Run: python -I Scripts/gen_textures.py <project_root>
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
OUT = os.path.join(ROOT, "asset-library", "Textures", "generated")
FONTS = os.path.join(ROOT, "asset-library", "Fonts")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1905)


def save(arr, name, mode=None):
    if arr.dtype != np.uint8:
        arr = (np.clip(arr, 0, 1) * 255).astype(np.uint8)
    Image.fromarray(arr, mode).save(os.path.join(OUT, name + ".png"))
    print("wrote", name, arr.shape)


def srgb(c):
    return np.clip(c, 0, 1) ** (1 / 2.2)


def tile_noise(n, octaves, persistence=0.55, base=4, seed=None):
    r = np.random.default_rng(seed) if seed is not None else rng
    y, x = np.mgrid[0:n, 0:n] / n * 2 * np.pi
    h = np.zeros((n, n))
    amp = 1.0
    for o in range(octaves):
        f = base * (2 ** o)
        for _ in range(6):
            kx, ky = r.integers(-f, f + 1, size=2)
            if kx == 0 and ky == 0:
                continue
            h += amp * np.sin(kx * x + ky * y + r.uniform(0, 2 * np.pi))
        amp *= persistence
    h -= h.min()
    return h / h.max()


def height_to_normal(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    nz = np.ones_like(h)
    l = np.sqrt(dx * dx + dy * dy + nz * nz)
    n = np.stack([-dx / l, dy / l, nz / l], axis=-1)  # DirectX (green down)
    return ((n * 0.5 + 0.5) * 255).astype(np.uint8)


# ---------------------------------------------------------------------------------------------------------------------
def awning():
    W = 1024
    y, x = np.mgrid[0:W, 0:W] / W
    weave = (np.sin(x * W * np.pi * 0.5) * np.sin(y * W * np.pi * 0.5)) * 0.5 + 0.5
    fadeN = tile_noise(W, 4, 0.6, 2, seed=3)
    grime = tile_noise(W, 5, 0.6, 3, seed=4)
    variants = {"Terracotta": (0.62, 0.17, 0.07), "Sea": (0.04, 0.2, 0.42), "Lemon": (0.85, 0.6, 0.08)}
    cream = np.array([0.86, 0.8, 0.66])
    for name, col in variants.items():
        stripe = ((x * 8) % 1.0 < 0.5).astype(float)
        # soft printed edge
        edge = np.abs(((x * 8) % 1.0) - 0.5)
        stripe = np.clip((0.5 - np.abs(((x * 8) % 1.0) - 0.25) * 2) * 40, 0, 1)
        c = cream[None, None] * (1 - stripe[..., None]) + np.array(col)[None, None] * stripe[..., None]
        # sun-bleached top, a little grime
        c = c * (0.9 + 0.1 * weave[..., None]) * (0.92 + 0.08 * fadeN[..., None])
        c = c + (1 - c) * 0.12 * fadeN[..., None]
        c *= (0.9 + 0.1 * grime[..., None])
        save((srgb(c) * 255).astype(np.uint8), f"T_Awning_{name}_D")
    h = weave * 0.6 + tile_noise(W, 3, 0.5, 16, seed=5) * 0.4
    save(height_to_normal(h, 1.6), "T_Awning_N")


def majolica():
    W = 1024
    n = 8  # tiles per side
    s = W // n
    img = Image.new("RGB", (W, W))
    d = ImageDraw.Draw(img)
    pal = {"y": (240, 190, 40), "g": (40, 125, 70), "b": (30, 85, 160), "w": (238, 228, 205), "t": (200, 95, 40)}
    for j in range(n):
        for i in range(n):
            x0, y0 = i * s, j * s
            # chevron bands like the Positano dome
            band = (j + (i % 2)) % 3
            base = [pal["y"], pal["g"], pal["b"]][band]
            d.rectangle([x0, y0, x0 + s, y0 + s], fill=base)
            if band == 0:
                d.polygon([(x0, y0 + s), (x0 + s // 2, y0 + s // 3), (x0 + s, y0 + s)], fill=pal["g"])
            elif band == 1:
                d.polygon([(x0, y0 + s), (x0 + s // 2, y0 + s // 3), (x0 + s, y0 + s)], fill=pal["b"])
            else:
                d.polygon([(x0, y0 + s), (x0 + s // 2, y0 + s // 3), (x0 + s, y0 + s)], fill=pal["y"])
            cx, cy = x0 + s // 2, y0 + s // 2
            d.ellipse([cx - s // 10, cy - s // 10 - s // 8, cx + s // 10, cy + s // 10 - s // 8], fill=pal["w"])
    arr = np.asarray(img).astype(float) / 255
    # glaze variation + grout
    var = tile_noise(W, 4, 0.6, 3, seed=7)[..., None]
    arr = arr * (0.85 + 0.25 * var)
    yy, xx = np.mgrid[0:W, 0:W]
    grout = ((xx % s) < 4) | ((yy % s) < 4)
    arr[grout] = np.array([0.82, 0.78, 0.7])
    save((np.clip(arr, 0, 1) * 255).astype(np.uint8), "T_Majolica_D")
    rough = np.where(grout, 0.85, 0.18 + 0.15 * tile_noise(W, 3, 0.5, 6, seed=8))
    save(rough, "T_Majolica_R")
    hgt = np.where(grout, 0.0, 1.0)
    # pillowed tile edges
    ex = np.minimum(xx % s, s - xx % s) / (s * 0.12)
    ey = np.minimum(yy % s, s - yy % s) / (s * 0.12)
    hgt = np.clip(np.minimum(ex, ey), 0, 1) ** 0.5 * hgt
    save(height_to_normal(hgt.astype(float), 3.0), "T_Majolica_N")


def leaf_shape(d, cx, cy, length, width, angle, fill):
    pts = []
    for k in range(21):
        t = k / 20
        w = math.sin(math.pi * t) ** 0.9 * width
        pts.append((t * length, w))
    for k in range(20, -1, -1):
        t = k / 20
        w = math.sin(math.pi * t) ** 0.9 * width
        pts.append((t * length, -w))
    ca, sa = math.cos(angle), math.sin(angle)
    d.polygon([(cx + px * ca - py * sa, cy + px * sa + py * ca) for px, py in pts], fill=fill)


def bougainvillea():
    W = 1024
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # stems
    for _ in range(14):
        x, y = rng.uniform(0.2, 0.8) * W, rng.uniform(0.2, 0.8) * W
        a = rng.uniform(0, 2 * math.pi)
        for _ in range(8):
            nx, ny = x + math.cos(a) * 40, y + math.sin(a) * 40
            d.line([(x, y), (nx, ny)], fill=(70, 55, 30, 255), width=5)
            x, y = nx, ny
            a += rng.uniform(-0.5, 0.5)
    # leaves
    for _ in range(140):
        cx, cy = rng.normal(0.5, 0.2, 2) * W
        g = rng.uniform(0.6, 1.0)
        leaf_shape(d, cx, cy, rng.uniform(55, 95), rng.uniform(18, 30), rng.uniform(0, 2 * math.pi),
                   (int(40 * g), int(105 * g), int(35 * g), 255))
    # bracts: clusters of three papery hearts, magenta through pink
    for _ in range(95):
        cx, cy = rng.normal(0.5, 0.17, 2) * W
        hue = rng.uniform(0, 1)
        col = (int(215 + 35 * hue), int(30 + 60 * hue), int(120 + 50 * hue), 255)
        for k in range(3):
            a = k * 2 * math.pi / 3 + rng.uniform(-0.3, 0.3)
            leaf_shape(d, cx, cy, rng.uniform(34, 48), rng.uniform(16, 22), a, col)
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(245, 235, 200, 255))
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    arr = np.asarray(img).astype(float) / 255
    # alpha falloff at the card edge so cards never show a square silhouette
    yy, xx = np.mgrid[0:W, 0:W] / W
    r = np.sqrt((xx - 0.5) ** 2 + (yy - 0.5) ** 2)
    arr[..., 3] *= np.clip((0.5 - r) / 0.08, 0, 1)
    # vein/shading
    shade = 0.8 + 0.2 * tile_noise(W, 4, 0.6, 6, seed=11)
    arr[..., :3] *= shade[..., None]
    save((np.clip(arr, 0, 1) * 255).astype(np.uint8), "T_Bougain_D", "RGBA")


def leafy():
    W = 1024
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for _ in range(260):
        cx, cy = rng.normal(0.5, 0.21, 2) * W
        g = rng.uniform(0.5, 1.0)
        leaf_shape(d, cx, cy, rng.uniform(45, 80), rng.uniform(16, 28), rng.uniform(0, 2 * math.pi),
                   (int(35 * g), int(90 * g + 20), int(30 * g), 255))
    for _ in range(30):  # jasmine stars
        cx, cy = rng.normal(0.5, 0.2, 2) * W
        for k in range(5):
            leaf_shape(d, cx, cy, 16, 6, k * 2 * math.pi / 5, (250, 248, 235, 255))
    img = img.filter(ImageFilter.GaussianBlur(0.5))
    arr = np.asarray(img).astype(float) / 255
    yy, xx = np.mgrid[0:W, 0:W] / W
    r = np.sqrt((xx - 0.5) ** 2 + (yy - 0.5) ** 2)
    arr[..., 3] *= np.clip((0.5 - r) / 0.08, 0, 1)
    save((np.clip(arr, 0, 1) * 255).astype(np.uint8), "T_Leafy_D", "RGBA")


def font(name, size):
    paths = {"script": "lobster/Lobster-Regular.ttf", "hand": "kalam/Kalam-Regular.ttf", "bold": "lato/Lato-Black.ttf"}
    return ImageFont.truetype(os.path.join(FONTS, paths[name]), size)


def aged(img, amount=0.15, seed=0):
    arr = np.asarray(img.convert("RGB")).astype(float) / 255
    n = tile_noise(max(arr.shape[:2]), 5, 0.6, 3, seed=seed)[: arr.shape[0], : arr.shape[1]]
    arr = arr * (1 - amount + amount * n[..., None] * 1.4)
    return (np.clip(arr, 0, 1) * 255).astype(np.uint8)


def signs():
    # Café sign: hand-painted wooden board.
    W, H = 1024, 256
    img = Image.new("RGB", (W, H), (28, 70, 96))
    d = ImageDraw.Draw(img)
    d.rectangle([10, 10, W - 10, H - 10], outline=(232, 200, 120), width=8)
    d.text((W // 2, H // 2 + 6), "Caffè Rosa", font=font("script", 150), fill=(245, 232, 200), anchor="mm")
    for x in (60, W - 60):
        d.ellipse([x - 14, H // 2 - 14, x + 14, H // 2 + 14], fill=(232, 200, 120))
    save(aged(img, 0.22, 21), "T_Sign_Caffe")
    # Limoni stencil on crate boards.
    W, H = 512, 256
    img = Image.new("RGB", (W, H), (196, 150, 92))
    d = ImageDraw.Draw(img)
    d.text((W // 2, H // 2 - 20), "LIMONI", font=font("bold", 92), fill=(150, 52, 30), anchor="mm")
    d.text((W // 2, H // 2 + 62), "di Vellara", font=font("script", 56), fill=(60, 90, 40), anchor="mm")
    save(aged(img, 0.3, 22), "T_Sign_Limoni")
    # Ceramic street tile: blue on white, glazed.
    W, H = 512, 256
    img = Image.new("RGB", (W, H), (240, 236, 222))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([12, 12, W - 12, H - 12], radius=20, outline=(30, 80, 150), width=10)
    d.text((W // 2, H // 2 - 26), "Via del Gatto", font=font("script", 78), fill=(30, 80, 150), anchor="mm")
    d.text((W // 2, H // 2 + 58), "· Vellara ·", font=font("hand", 40), fill=(200, 120, 40), anchor="mm")
    for x, y in ((40, 40), (W - 40, 40), (40, H - 40), (W - 40, H - 40)):
        d.regular_polygon((x, y, 14), 4, fill=(230, 180, 40))
    save(aged(img, 0.08, 23), "T_Sign_Street")
    # Little trattoria menu board.
    W, H = 512, 640
    img = Image.new("RGB", (W, H), (34, 40, 38))
    d = ImageDraw.Draw(img)
    d.text((W // 2, 80), "Oggi", font=font("script", 90), fill=(240, 236, 220), anchor="mm")
    for i, line in enumerate(["Granita al limone", "Caffè freddo", "Sfogliatella", "Spaghetti alle vongole"]):
        d.text((W // 2, 200 + i * 95), line, font=font("hand", 44), fill=(240, 230, 200), anchor="mm")
    save(aged(img, 0.2, 24), "T_Sign_Menu")


def linens():
    W = 512
    y, x = np.mgrid[0:W, 0:W] / W
    weave = (np.sin(x * W * np.pi * 0.7) * np.sin(y * W * np.pi * 0.7)) * 0.5 + 0.5
    n = tile_noise(W, 4, 0.6, 3, seed=31)
    designs = {
        "White": lambda: np.ones_like(x)[..., None] * np.array([0.92, 0.9, 0.84]),
        "BlueStripe": lambda: np.where(((y * 6) % 1 < 0.18)[..., None], np.array([0.12, 0.32, 0.6]), np.array([0.92, 0.9, 0.84])),
        "Coral": lambda: np.ones_like(x)[..., None] * np.array([0.85, 0.42, 0.3]),
        "Lemon": lambda: np.where((((x * 10) % 1 < 0.5) ^ ((y * 10) % 1 < 0.5))[..., None], np.array([0.95, 0.82, 0.3]), np.array([0.95, 0.92, 0.8])),
        "Sea": lambda: np.ones_like(x)[..., None] * np.array([0.25, 0.55, 0.7]),
    }
    for name, fn in designs.items():
        c = fn() * (0.88 + 0.12 * weave[..., None]) * (0.95 + 0.05 * n[..., None])
        save((srgb(c) * 255).astype(np.uint8), f"T_Linen_{name}_D")


def utility():
    # Sea: long swells + chop.
    h = tile_noise(1024, 5, 0.5, 2, seed=41) * 0.65 + tile_noise(1024, 3, 0.6, 20, seed=42) * 0.35
    save(height_to_normal(h, 7.0), "T_WaterNormal")
    save(tile_noise(1024, 6, 0.62, 3, seed=43), "T_WearMask")
    # Soft wisp blob with ragged edge (alpha only, packed in R).
    W = 256
    yy, xx = np.mgrid[0:W, 0:W] / W - 0.5
    r = np.sqrt(xx ** 2 + yy ** 2) * 2
    ang = np.arctan2(yy, xx)
    rag = 0.06 * np.sin(ang * 3 + 1.3) + 0.04 * np.sin(ang * 7 + 0.4)
    blob = np.clip(1 - (r + rag * r) ** 1.6, 0, 1)
    save(blob ** 1.2, "T_Wisp")
    # Fine fur: anisotropic streaks.
    W = 512
    base = rng.standard_normal((W, W))
    streak = np.zeros((W, W))
    for k in range(18):
        streak += np.roll(base, k, axis=0) * (1 - k / 18)
    streak = (streak - streak.min()) / (streak.max() - streak.min())
    save(height_to_normal(streak, 2.2), "T_FurNormal")
    save(tile_noise(512, 5, 0.6, 2, seed=44), "T_InkNoise")
    fur_strands()
    # Neutral defaults for materials whose maps are optional.
    flat = np.zeros((16, 16, 3), np.uint8)
    flat[..., 0], flat[..., 1], flat[..., 2] = 255, 190, 0
    save(flat, "T_FlatARM")
    nrm = np.zeros((16, 16, 3), np.uint8)
    nrm[..., 0], nrm[..., 1], nrm[..., 2] = 128, 128, 255
    save(nrm, "T_FlatNormal")
    save(np.full((16, 16, 3), 255, np.uint8), "T_White")


def fur_strands():
    """Fur shells: tileable 1024 strand map in clumped tufts (how real fur lies - hairs gather into locks that taper to a
    shared tip, with stray guard hairs between). R = strand length (0..1) with a tapered round cross-section, 0 between
    strands: a shell at height h keeps the texels whose strand is longer than h, so the coat thins into pointed tufts
    toward the tips. G = tone of the tuft, B = tone of the single hair (colour variation in the material)."""
    r = np.random.default_rng(2611)
    W = 1024
    R = np.zeros((W, W), np.float32)
    G = np.full((W, W), 0.5, np.float32)
    B = np.full((W, W), 0.5, np.float32)

    def stamp(cx, cy, rad, length, tone_g, tone_b):
        n = int(np.ceil(rad)) + 1
        oy, ox = np.mgrid[-n:n + 1, -n:n + 1]
        prof = np.clip(1 - np.sqrt(ox ** 2 + oy ** 2) / rad, 0, 1) ** 0.7
        v = (length * prof).astype(np.float32)
        ys_, xs_ = (int(cy) + oy) % W, (int(cx) + ox) % W
        cur = R[ys_, xs_]
        win = v > cur
        R[ys_[win], xs_[win]] = v[win]
        G[ys_[win], xs_[win]] = tone_g
        B[ys_[win], xs_[win]] = tone_b

    # Tufts: hairs scattered round a centre, longest in the middle, so the upper shells leave a point per tuft.
    for _ in range(1500):
        cx, cy = r.uniform(0, W, 2)
        rc = r.uniform(12, 24)
        lc = r.uniform(0.72, 1.0)
        tone = r.uniform(0, 1)
        for _ in range(int(rc * 6)):
            d = np.abs(r.normal(0, 0.5)) * rc
            a = r.uniform(0, 2 * np.pi)
            length = lc * (1 - 0.5 * min(d / rc, 1.0) ** 2) * r.uniform(0.85, 1.0)
            stamp(cx + d * np.cos(a), cy + d * np.sin(a), r.uniform(1.1, 2.0), length, tone, r.uniform(0, 1))
    # Undercoat and stray guard hairs between the tufts.
    for _ in range(40000):
        cx, cy = r.uniform(0, W, 2)
        guard = r.uniform() < 0.12
        stamp(cx, cy, r.uniform(1.0, 1.6), r.uniform(0.75, 1.0) if guard else r.uniform(0.15, 0.5), r.uniform(0, 1), 1.0 if guard else r.uniform(0, 1))
    save(np.stack([R, G, B], axis=-1), "T_FurStrands")


if __name__ == "__main__":
    awning()
    majolica()
    bougainvillea()
    leafy()
    signs()
    linens()
    utility()
    print("textures ok")
