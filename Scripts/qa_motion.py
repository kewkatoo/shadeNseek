"""
QA for -motioncap captures (Saved/MotionCap/<mode>/f_####.png + tracks.csv).

  python -I Scripts/qa_motion.py <project> <mode> [--sheet a:b:step] [--flicker]

Prints numbers that describe motion problems instead of eyeballing stills:
  * tail: per-segment roll about its own axis (deg/s), direction angular speed, tail-tip azimuth around the pelvis
  * legs: paw slip while planted, joint-angle jumps between frames
  * body / camera: high-frequency jitter (second differences) of the pelvis, head and camera
  * --flicker: per-pixel temporal sign-flip energy over the frames (z-fighting / shimmer hot spots) -> flicker.png
  * --sheet: a contact sheet of frames a..b every step -> sheet_a_b.png
"""
import csv
import math
import sys
from pathlib import Path

import numpy as np


def quat_mul(a, b):
    ax, ay, az, aw = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    bx, by, bz, bw = b[..., 0], b[..., 1], b[..., 2], b[..., 3]
    return np.stack([
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ], -1)


def quat_conj(q):
    return q * np.array([-1, -1, -1, 1.0])


def quat_rot(q, v):
    qv = np.concatenate([v, np.zeros(v.shape[:-1] + (1,))], -1)
    return quat_mul(quat_mul(q, qv), quat_conj(q))[..., :3]


def load(dirp):
    rows = list(csv.DictReader(open(dirp / "tracks.csv")))
    cols = rows[0].keys()
    data = {c: np.array([float(r[c]) for r in rows]) for c in cols}
    return data


def bone(d, b):
    p = np.stack([d[f"{b}_px"], d[f"{b}_py"], d[f"{b}_pz"]], -1)
    q = np.stack([d[f"{b}_qx"], d[f"{b}_qy"], d[f"{b}_qz"], d[f"{b}_qw"]], -1)
    return p, q


def unit(v):
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.maximum(n, 1e-9)


def windows(t):
    # Script beats (see AShadeGameMode::TickMotionCap).
    return [("idle", 0.2, 1.0), ("walk", 1.6, 4.5), ("veer", 4.5, 6.0), ("stop", 6.0, 7.6), ("about-turn", 7.6, 8.8),
            ("idle2", 8.8, 10.0), ("trot", 10.0, 11.8), ("stop2", 11.8, 13.0), ("hop", 13.0, 14.6), ("settle", 14.6, 16.4)]


def report(d):
    t = d["t"]
    dt = np.diff(t).mean()
    print(f"frames {len(t)}  dt {dt*1000:.1f} ms")
    wins = windows(t)

    # ---- tail ----
    tails = [f"tail_0{i}" for i in range(1, 9)]
    P = {}
    Q = {}
    for b in tails + ["pelvis", "spine_03", "head"]:
        P[b], Q[b] = bone(d, b)
    print("\nTAIL roll about own axis (deg/s, |mean| p95 max) per beat, segments 1..7")
    seg_roll = []
    for i in range(7):
        a, b = tails[i], tails[i + 1]
        D = unit(P[b] - P[a])
        dq = quat_mul(Q[a][1:], quat_conj(Q[a][:-1]))
        dq *= np.sign(dq[:, 3:4] + 1e-12)
        Dm = unit(D[1:] + D[:-1])
        tw = 2 * np.degrees(np.arctan2(np.sum(dq[:, :3] * Dm, -1), dq[:, 3])) / dt
        seg_roll.append(tw)
    seg_roll = np.array(seg_roll)  # 7 x N-1
    for name, a, b in wins:
        m = (t[1:] >= a) & (t[1:] < b)
        if not m.any():
            continue
        r = np.abs(seg_roll[:, m])
        print(f"  {name:10s} " + " ".join(f"{np.percentile(r[i], 95):6.0f}" for i in range(7)) + f"   max {r.max():6.0f}")

    print("\nTAIL segment direction angular speed (deg/s, p95) per beat")
    for name, a, b in wins:
        m = (t[1:] >= a) & (t[1:] < b)
        if not m.any():
            continue
        out = []
        for i in range(7):
            D = unit(P[tails[i + 1]] - P[tails[i]])
            ang = np.degrees(np.arccos(np.clip(np.sum(D[1:] * D[:-1], -1), -1, 1))) / dt
            out.append(np.percentile(ang[m], 95))
        print(f"  {name:10s} " + " ".join(f"{v:6.0f}" for v in out))

    # Tail tip around the pelvis, in the body's frame (yaw from actor).
    yaw = np.radians(d["yaw"])
    fwd = np.stack([np.cos(yaw), np.sin(yaw), np.zeros_like(yaw)], -1)
    right = np.stack([-np.sin(yaw), np.cos(yaw), np.zeros_like(yaw)], -1)
    rel = P["tail_08"] - P["tail_01"]
    az = np.degrees(np.arctan2(np.sum(rel * right, -1), -np.sum(rel * fwd, -1)))
    el = np.degrees(np.arctan2(rel[:, 2], np.hypot(np.sum(rel * right, -1), np.sum(rel * fwd, -1))))
    print("\nTAIL tip relative to root in body frame: azimuth (0 = straight back, + = right) / elevation")
    for name, a, b in wins:
        m = (t >= a) & (t < b)
        if not m.any():
            continue
        daz = np.abs(np.diff(np.unwrap(np.radians(az[m]))))
        print(f"  {name:10s} az {az[m].min():6.0f}..{az[m].max():6.0f}  el {el[m].min():5.0f}..{el[m].max():5.0f}  "
              f"az travel {np.degrees(daz.sum()):6.0f} deg")

    # ---- body / head / camera jitter ----
    def jitter(x):
        a = x[2:] - 2 * x[1:-1] + x[:-2]
        return np.linalg.norm(a, axis=-1)
    print("\nJITTER (second difference, cm/frame^2: p95 / max) per beat: pelvis, head, camera")
    cam = np.stack([d["camx"], d["camy"], d["camz"]], -1)
    for name, a, b in wins:
        m = (t[1:-1] >= a) & (t[1:-1] < b)
        if not m.any():
            continue
        vals = []
        for x in (P["pelvis"], P["head"], cam):
            j = jitter(x)[m]
            vals.append(f"{np.percentile(j, 95):5.2f}/{j.max():5.2f}")
        print(f"  {name:10s} " + "  ".join(vals) + f"   arm {d['arm'][1:-1][m].min():5.0f}..{d['arm'][1:-1][m].max():5.0f}")
    camrot = np.stack([d["campitch"], d["camyaw"]], -1)
    jr = np.abs(camrot[2:] - 2 * camrot[1:-1] + camrot[:-2])
    print(f"  camera rotation 2nd diff deg p95 {np.percentile(jr, 95):.3f} max {jr.max():.3f}")

    # ---- legs ----
    legs = [("FL", ["upperarm_l", "forearm_l", "hand_l", "finger_l"]), ("FR", ["upperarm_r", "forearm_r", "hand_r", "finger_r"]),
            ("HL", ["thigh_l", "calf_l", "foot_l", "toe_l"]), ("HR", ["thigh_r", "calf_r", "foot_r", "toe_r"])]
    print("\nLEGS: joint angle jumps (deg/frame p99/max) and paw (ball) speed when low (cm/s p50) per beat")
    for name, a, b in wins:
        m = (t[1:] >= a) & (t[1:] < b)
        if not m.any():
            continue
        out = []
        for lname, ch in legs:
            p = [bone(d, c)[0] for c in ch]
            ang = []
            for k in range(1, 3):
                u = unit(p[k - 1] - p[k])
                v = unit(p[k + 1] - p[k])
                ang.append(np.degrees(np.arccos(np.clip(np.sum(u * v, -1), -1, 1))))
            jumps = np.max(np.abs(np.diff(np.stack(ang, -1), axis=0)), -1)
            ball = p[3]
            sp = np.linalg.norm(np.diff(ball, axis=0), axis=-1) / dt
            low = ball[1:, 2] < np.percentile(ball[:, 2], 30) + 0.5
            slip = np.median(sp[m & low]) if (m & low).any() else float("nan")
            out.append(f"{lname} {np.percentile(jumps[m], 99):4.1f}/{jumps[m].max():5.1f} slip {slip:5.1f}")
        print(f"  {name:10s} " + " | ".join(out))

    # Gait cadence from paw lift-offs (FL) during walk / trot.
    print("\nGAIT")
    for name, a, b in [("walk", 1.6, 4.5), ("trot", 10.3, 11.8)]:
        m = (t >= a) & (t < b)
        z = bone(d, "finger_l")[0][m, 2]
        sp = d["speed"][m]
        zz = z - z.min()
        lifts = np.sum((zz[1:] > 1.5) & (zz[:-1] <= 1.5))
        dur = t[m][-1] - t[m][0]
        print(f"  {name}: speed {sp.mean():5.0f} cm/s, FL lifts {lifts} in {dur:.1f}s -> {lifts/dur:.2f} Hz, "
              f"stride {sp.mean()/max(lifts/dur,1e-3):5.1f} cm, paw lift max {zz.max():4.1f} cm")


def sheet(dirp, a, b, step, out, cols=6, crop=None):
    from PIL import Image
    frames = [dirp / f"f_{i:04d}.png" for i in range(a, b + 1, step)]
    frames = [f for f in frames if f.exists()]
    ims = [Image.open(f).convert("RGB") for f in frames]
    if crop:
        ims = [im.crop(crop) for im in ims]
    w, h = ims[0].size
    sc = min(1.0, 1500 / (cols * w))
    tw, th = int(w * sc), int(h * sc)
    rows = (len(ims) + cols - 1) // cols
    S = Image.new("RGB", (cols * tw, rows * th), (0, 0, 0))
    for k, im in enumerate(ims):
        S.paste(im.resize((tw, th)), ((k % cols) * tw, (k // cols) * th))
    S.save(out)
    print("sheet", out, len(ims), "frames")


def flicker(dirp, a, b, out):
    from PIL import Image
    fr = []
    for i in range(a, b + 1):
        f = dirp / f"f_{i:04d}.png"
        if f.exists():
            fr.append(np.asarray(Image.open(f).convert("L"), dtype=np.float32) / 255.0)
    F = np.stack(fr)
    d1 = F[1:-1] - F[:-2]
    d2 = F[2:] - F[1:-1]
    flip = np.maximum(0, -d1 * d2)            # sign reversals = flicker, not motion
    E = np.sqrt(flip.mean(0))
    print(f"flicker energy: mean {E.mean():.4f}  p99 {np.percentile(E, 99):.4f}  max {E.max():.4f}")
    # Hot spots on a coarse grid.
    H, W = E.shape
    g = 24
    cells = []
    for y in range(0, H - g + 1, g):
        for x in range(0, W - g + 1, g):
            cells.append((E[y:y + g, x:x + g].mean(), x, y))
    cells.sort(reverse=True)
    print("hot cells (energy, x, y):", ", ".join(f"({c[0]:.3f},{c[1]},{c[2]})" for c in cells[:10]))
    base = Image.fromarray((F[len(F) // 2] * 255).astype(np.uint8)).convert("RGB")
    heat = np.clip(E / max(np.percentile(E, 99.5), 1e-6), 0, 1)
    hm = np.zeros((H, W, 3), np.uint8)
    hm[..., 0] = (heat * 255).astype(np.uint8)
    hm[..., 1] = (heat * 80).astype(np.uint8)
    Image.blend(base, Image.fromarray(hm), 0.55).save(out)
    print("flicker map", out)


if __name__ == "__main__":
    root = Path(sys.argv[1])
    mode = sys.argv[2]
    dirp = root / "Saved" / "MotionCap" / mode
    args = sys.argv[3:]
    if "--sheet" in args:
        a, b, s = (int(x) for x in args[args.index("--sheet") + 1].split(":"))
        crop = None
        if "--crop" in args:
            crop = tuple(int(x) for x in args[args.index("--crop") + 1].split(","))
        sheet(dirp, a, b, s, dirp / f"sheet_{a}_{b}.png", crop=crop)
    elif "--flicker" in args:
        a, b = (int(x) for x in args[args.index("--flicker") + 1].split(":"))
        flicker(dirp, a, b, dirp / f"flicker_{a}_{b}.png")
    else:
        report(load(dirp))
