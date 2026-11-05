"""Sculpts the cat body as a signed distance field and polygonises it (marching cubes).

Original model, built from anatomical primitives (ellipsoids and round cones joined with smooth unions): deep ribcage,
tucked waist, shoulder blades, muscular haunches, digitigrade legs, a proper cat skull with cheeks, muzzle, whisker
pads, nose leather, chin and almond eye sockets. Ears, eyes, whiskers and fur shells are added in make_cat.py.

Run with the system Python (needs numpy + scikit-image):
    python -I Scripts/blender/cat_sdf.py <project_root>
Writes asset-library/Models/generated/cat_body_sdf.npz (verts, faces) in Blender metres: cat faces -Y, Z up, left = +X.
"""
import os
import sys
import time

import numpy as np
from skimage import measure

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
OUT = os.path.join(ROOT, "asset-library", "Models", "generated", "cat_body_sdf.npz")
os.makedirs(os.path.dirname(OUT), exist_ok=True)

VOX = 0.0011
LO = np.array([-0.075, -0.305, -0.004])
HI = np.array([0.075, 0.50, 0.36])
SHAPE = np.ceil((HI - LO) / VOX).astype(int) + 1
xs = LO[0] + np.arange(SHAPE[0]) * VOX
ys = LO[1] + np.arange(SHAPE[1]) * VOX
zs = LO[2] + np.arange(SHAPE[2]) * VOX
FIELD = np.full(SHAPE, 1.0, dtype=np.float32)
print("grid", SHAPE, f"{SHAPE.prod() / 1e6:.1f}M voxels")


def block(lo, hi):
    i0 = np.searchsorted(xs, lo[0]); i1 = np.searchsorted(xs, hi[0])
    j0 = np.searchsorted(ys, lo[1]); j1 = np.searchsorted(ys, hi[1])
    k0 = np.searchsorted(zs, lo[2]); k1 = np.searchsorted(zs, hi[2])
    if i1 <= i0 or j1 <= j0 or k1 <= k0:
        return None, None
    X, Y, Z = np.meshgrid(xs[i0:i1], ys[j0:j1], zs[k0:k1], indexing="ij")
    return (slice(i0, i1), slice(j0, j1), slice(k0, k1)), np.stack([X, Y, Z], axis=-1)


def rot(yaw=0.0, pitch=0.0, roll=0.0):
    """World->local rotation (degrees): yaw about Z, pitch about X, roll about Y."""
    cy, sy = np.cos(np.radians(yaw)), np.sin(np.radians(yaw))
    cp, sp = np.cos(np.radians(pitch)), np.sin(np.radians(pitch))
    cr, sr = np.cos(np.radians(roll)), np.sin(np.radians(roll))
    Rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    Ry = np.array([[cr, 0, sr], [0, 1, 0], [-sr, 0, cr]])
    return Rz @ Rx @ Ry


def smin(a, b, k):
    if k <= 0:
        return np.minimum(a, b)
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


def smax(a, b, k):
    return -smin(-a, -b, k)


BLEND_MARGIN = 0.045  # >= the largest smooth-union radius used after a primitive, or blends crease at block edges


def apply(d_fn, lo, hi, k, sub=False):
    sl, P = block(np.asarray(lo) - k - BLEND_MARGIN, np.asarray(hi) + k + BLEND_MARGIN)
    if sl is None:
        return
    d = d_fn(P).astype(np.float32)
    FIELD[sl] = smax(FIELD[sl], -d, k) if sub else smin(FIELD[sl], d, k)


def ellipsoid(c, r, k=0.02, R=None, sub=False):
    c = np.asarray(c, float); r = np.asarray(r, float)
    m = r.max()

    def f(P):
        q = P - c
        if R is not None:
            q = q @ R
        k0 = np.linalg.norm(q / r, axis=-1)
        k1 = np.linalg.norm(q / (r * r), axis=-1)
        return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)
    apply(f, c - m, c + m, k, sub)


def sphere(c, r, k=0.02, sub=False):
    c = np.asarray(c, float)
    apply(lambda P: np.linalg.norm(P - c, axis=-1) - r, c - r, c + r, k, sub)


def round_cone(a, b, r1, r2, k=0.02, sub=False):
    """Inigo Quilez' exact round cone (capsule with different end radii)."""
    a = np.asarray(a, float); b = np.asarray(b, float)
    ba = b - a
    l2 = ba @ ba
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2

    def f(P):
        pa = P - a
        y = pa @ ba
        z = y - l2
        x = pa * l2 - y[..., None] * ba
        x2 = np.sum(x * x, axis=-1)
        y2 = y * y * l2
        z2 = z * z * l2
        kk = np.sign(rr) * rr * rr * x2
        d1 = np.sqrt(x2 + z2) * il2 - r2
        d2 = np.sqrt(x2 + y2) * il2 - r1
        d3 = (np.sqrt(np.maximum(x2 * a2 * il2, 0)) + y * rr) * il2 - r1
        return np.where(np.sign(z) * a2 * z2 > kk, d1, np.where(np.sign(y) * a2 * y2 < kk, d2, d3))
    m = max(r1, r2)
    apply(f, np.minimum(a, b) - m, np.maximum(a, b) + m, k, sub)


def chain(pts, radii, k=0.012):
    for i in range(len(pts) - 1):
        round_cone(pts[i], pts[i + 1], radii[i], radii[i + 1], k)


t0 = time.time()
# ---------------------------------------------------------------------------------------------------------------------
# Torso. Cats are narrow and deep-chested with a tucked waist and a rounded, muscular rump.
# ---------------------------------------------------------------------------------------------------------------------
ellipsoid((0, -0.075, 0.2), (0.047, 0.082, 0.066), k=0.0)               # ribcage
ellipsoid((0, -0.122, 0.17), (0.037, 0.04, 0.05), k=0.03)               # sternum / chest ruff
ellipsoid((0, 0.025, 0.21), (0.041, 0.085, 0.052), k=0.045)                # waist: narrower than the ribcage
ellipsoid((0, 0.125, 0.212), (0.048, 0.066, 0.056), k=0.04)             # rump
ellipsoid((0, 0.03, 0.178), (0.036, 0.07, 0.03), k=0.03)                # soft belly skin (primordial pouch)
for sx in (1, -1):
    ellipsoid((sx * 0.028, -0.095, 0.243), (0.016, 0.034, 0.016), k=0.02, R=rot(pitch=-28))  # shoulder blades
# Neck: thick with fur, rising forward to the skull.
round_cone((0, -0.115, 0.222), (0, -0.183, 0.272), 0.044, 0.033, k=0.03)
ellipsoid((0, -0.155, 0.215), (0.03, 0.03, 0.04), k=0.03)               # throat fluff

# ---------------------------------------------------------------------------------------------------------------------
# Head (centre ~ y -0.22). Skull, cheeks, muzzle, whisker pads, nose, chin, jaw, almond eye sockets.
# ---------------------------------------------------------------------------------------------------------------------
ellipsoid((0, -0.21, 0.303), (0.042, 0.046, 0.038), k=0.025)             # cranium
for sx in (1, -1):
    ellipsoid((sx * 0.023, -0.229, 0.289), (0.0215, 0.024, 0.021), k=0.016)  # cheeks / zygomatic arch
    ellipsoid((sx * 0.031, -0.213, 0.284), (0.014, 0.02, 0.018), k=0.018)    # jowl fluff behind the cheeks
ellipsoid((0, -0.252, 0.2855), (0.0175, 0.017, 0.0148), k=0.012)          # muzzle
for sx in (1, -1):
    sphere((sx * 0.0078, -0.2605, 0.2818), 0.0075, k=0.005)                 # whisker pads
round_cone((0, -0.228, 0.312), (0, -0.2625, 0.299), 0.0095, 0.0058, k=0.009)  # nose bridge
ellipsoid((0, -0.2672, 0.2962), (0.0056, 0.0038, 0.0042), k=0.002)       # nose leather
ellipsoid((0, -0.2525, 0.2685), (0.0105, 0.011, 0.0075), k=0.006)        # chin
round_cone((0, -0.212, 0.273), (0, -0.247, 0.2695), 0.0195, 0.0105, k=0.012)  # lower jaw
ellipsoid((0, -0.203, 0.33), (0.026, 0.03, 0.01), k=0.02)                # flat forehead between the ears
# Almond eye sockets: tilted ellipsoids carved in (the eyeballs sit inside, see make_cat.py).
for sx in (1, -1):
    R = rot(yaw=sx * 26, roll=-sx * 13)
    ellipsoid((sx * 0.019, -0.2525, 0.3075), (0.0098, 0.0045, 0.0062), k=0.0022, R=R, sub=True)

# ---------------------------------------------------------------------------------------------------------------------
# Legs (digitigrade). Front: shoulder -> elbow (behind) -> wrist -> paw. Hind: big thigh, Z-shaped hock.
# ---------------------------------------------------------------------------------------------------------------------
for sx in (1, -1):
    x = sx * 0.036
    sh = (x, -0.123, 0.188); el = (sx * 0.039, -0.098, 0.113); wr = (sx * 0.038, -0.106, 0.034)
    mc = (sx * 0.0375, -0.116, 0.0125)
    round_cone((x * 0.8, -0.11, 0.215), sh, 0.03, 0.025, k=0.025)    # upper arm muscle into the chest
    round_cone(sh, el, 0.025, 0.018, k=0.004)
    sphere((sx * 0.039, -0.091, 0.117), 0.0128, k=0.008)              # elbow point
    round_cone(el, wr, 0.018, 0.0122, k=0.003)
    ellipsoid((sx * 0.038, -0.107, 0.078), (0.0135, 0.0155, 0.032), k=0.012)  # forearm muscle
    round_cone(wr, mc, 0.0122, 0.0122, k=0.002)
    ellipsoid((sx * 0.0378, -0.1265, 0.0108), (0.016, 0.02, 0.0108), k=0.008)  # front paw
    for dx in (-0.0085, -0.003, 0.003, 0.0085):                             # toe beans
        sphere((sx * 0.0378 + dx, -0.139 + abs(dx) * 0.35, 0.0085), 0.0058, k=0.004)

    hip = (sx * 0.037, 0.148, 0.196); kn = (sx * 0.042, 0.098, 0.122); hk = (sx * 0.04, 0.167, 0.066)
    ball = (sx * 0.039, 0.152, 0.0135)
    ellipsoid((sx * 0.034, 0.128, 0.165), (0.026, 0.056, 0.058), k=0.035, R=rot(pitch=24))  # haunch / thigh muscle
    round_cone(hip, kn, 0.03, 0.019, k=0.02)
    round_cone(kn, hk, 0.0175, 0.0112, k=0.003)
    ellipsoid((sx * 0.04, 0.133, 0.103), (0.0135, 0.016, 0.03), k=0.014, R=rot(pitch=-48))  # gastrocnemius
    sphere((sx * 0.04, 0.174, 0.068), 0.0098, k=0.006)                                     # hock point
    round_cone(hk, ball, 0.0108, 0.0118, k=0.002)
    ellipsoid((sx * 0.039, 0.142, 0.0108), (0.015, 0.021, 0.0108), k=0.008)               # hind paw
    for dx in (-0.008, -0.0028, 0.0028, 0.008):
        sphere((sx * 0.039 + dx, 0.1295 + abs(dx) * 0.35, 0.0085), 0.0055, k=0.004)

# ---------------------------------------------------------------------------------------------------------------------
# Tail: rest pose straight back with a slight droop (the rig poses it), thick fur base, rounded tip.
# ---------------------------------------------------------------------------------------------------------------------
TAIL = [(0, 0.172, 0.222), (0, 0.215, 0.226), (0, 0.26, 0.226), (0, 0.305, 0.222), (0, 0.35, 0.216), (0, 0.393, 0.208),
        (0, 0.433, 0.199), (0, 0.468, 0.19), (0, 0.488, 0.185)]
chain(TAIL, [0.02, 0.0165, 0.0152, 0.0145, 0.014, 0.0134, 0.0126, 0.0112, 0.0085], k=0.0)
sphere(TAIL[0], 0.021, k=0.02)  # blend the tail root into the rump

print(f"field built in {time.time() - t0:.1f}s")
# Clear any field below the ground plane so paws are flat.
FIELD[:, :, zs < 0.0005] = np.maximum(FIELD[:, :, zs < 0.0005], 0.002)
verts, faces, normals, _ = measure.marching_cubes(FIELD, level=0.0, spacing=(VOX, VOX, VOX))
verts = verts + LO
print("marching cubes", len(verts), "verts", len(faces), "tris", f"{time.time() - t0:.1f}s")
np.savez_compressed(OUT, verts=verts.astype(np.float32), faces=faces.astype(np.int32))
print("wrote", OUT)
