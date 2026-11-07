"""Audio QA for the game sounds built by prep_audio.py: is every sound clean, unclipped and at a consistent loudness?

For every TABLE entry in prep_audio.py it checks the OUTPUT wav (asset-library/Audio/game/<name>.wav) and the SOURCE
segment(s) it is cut from:

  level   output: true peak (4x oversampled) <= -1 dBTP; integrated LUFS (BS.1770, prep_audio.lufs) within 1 LU of the
          category target; DC offset below -60 dBFS (Hann-weighted mean, so a short one-shot's low end is not mistaken
          for an offset). The source segment's DC is reported too (the build's high-pass removes it).
  clip    source segment, per channel: flat-topped peaks (see thresholds). The limiter hides overs in the output; a
          clipped source still sounds broken.
  noise   one-shots / UI / stingers / music (everything that is not a loop), over the frames inside the sound (fades
          excluded; 10 ms frames for sounds under 0.6 s, else 20 ms): "floor" = 5th-percentile frame level re the loudest
          frame; quiet frames = frames >= 30 dB under the loudest (a sound that never gets that quiet masks its own
          floor); "hf" = energy above 6 kHz in the quieter half of the quiet frames, with its spectral flatness ("flat",
          ~0.6-0.95 = noise, < 0.2 = tonal partials) and "slope" (HF level vs total level over the decay: ~1 = the highs
          follow the sound, ~0 = a constant floor). "rec" = the recording's own background next to the cut (5th
          percentile of 20 ms frames within +-10 s, above the entry's high-pass) re the segment's loudest frame.
          Loops (ambience beds and continuous SFX like purr / sizzle / cart roll): broadband noise is the content, so
          only steady narrow lines are checked: mains hum (50/60 Hz family) and electronic whine above 10 kHz (this is
          also run on one-shots of 1 s or more).
  seam    *_Loop: sample jump across the wrap point (vs the local sample-to-sample motion), 250 ms level change across
          the wrap (vs the loop's own 250 ms level fluctuation) and a high-frequency burst (click) at the wrap.
  ends    one-shots: first / last sample at zero, no abrupt stop (a loud tail cut with a very short fade), and whether
          the source cut starts / ends in the middle of a sound with a short fade (warning).

Thresholds (why):
  * hiss FAIL: hf > -55 dB re peak AND noise-like (flat > 0.3) AND steady (slope < 0.35, or std < 3 dB over the quiet
    frames when the sound has too little decay for a slope) AND the quiet frames are the recording's own background
    (floor within 8 dB of rec; a soft scrape at the start of a sound is content, not hiss). Natural decays lose their
    highs first; a flat HF floor that stays put while the body decays is mic / preamp / surf noise. -55 dB re peak is
    about -56 to -62 dBFS for these one-shots (peaks -1 to -7 dBFS): below that it is masked by any game bed, above it
    it is heard in the gaps on headphones.
  * background WARN: floor > -40 dB re peak AND within 8 dB of the recording's own background (so the quiet frames ARE
    that background, not the sound's decay) AND noise-like (flat 300 Hz-12 kHz > 0.1). A constant background 40 dB under
    a one-shot's peak sits above the quiet passages of the -24 LUFS beds it plays over: a "halo" that starts and stops
    with the sound.
  * hum / whine lines: > 12 dB above the local spectral median, present (> 6 dB) in >= 80 % of 0.7 s frames with a level
    spread < 3 dB, at 50/60 Hz multiples (+-1.5 Hz) or above 10 kHz (whine in one-shots only counts above -70 dB re the
    total). Natural tonal content (bells, cicadas) moves; electrical lines do not.
  * clip: a run of >= 3 samples within 3 % of the segment's absolute max (lossy decoders put ~+-1.5 % ripple on a
    clipped plateau). A smooth crest of the half-cycle it sits in (zero crossing to zero crossing, H samples) stays
    within 3 % for 2 acos(0.97) / pi * H = 0.156 H samples; a run twice that long is a plateau. Checked on a known
    clipped Freesound preview (470760: 9-11 sample plateaus at 20.49 s found) and on clean sines from 40 Hz to 5 kHz
    (none found).
  * seam: jump > 4x the RMS sample-to-sample change near the wrap is a click; a 250 ms level change > max(2 dB, the
    loop's own 95th-percentile change) is an audible "bump"; an HF burst > 12 dB above its surroundings is a click.

Run:   python -I Scripts/qa_audio.py [name ...] [--src]    (--src also lists noise metrics of each source segment)
Exit code 1 if anything FAILs.
"""
import os
import sys

import numpy as np
import soundfile as sf
from scipy import signal
from scipy.ndimage import median_filter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import prep_audio as pa  # noqa: E402  (TABLE, targets, loudness / true-peak meters, decoder)

SR = pa.SR
TP_MAX = pa.CEILING_DBTP + 0.05     # 16-bit dither can add a few hundredths of a dB
LU_TOL = 1.0
DC_MAX_DB = -60.0
FLOOR_WARN = -40.0
QUIET = 30.0
NOISY = 0.3
HISS_HF = -55.0
HISS_STD = 3.0
HISS_SLOPE = 0.35
LINE_PROM = 12.0
CLIP_TOL = 0.03
CLIP_RATIO = 2.0
SEAM_JUMP = 4.0
SEAM_LVL = 2.0
SEAM_CLICK = 12.0


def db(v):
    return 10 * np.log10(np.maximum(v, 1e-30))


def is_loop(e):
    return bool(e.get("loop", 0))


# --------------------------------------------------------------------------------------------------------------------
# Measurements
# --------------------------------------------------------------------------------------------------------------------
def frame_spectra(m, sr, win=0.02, hop=0.01):
    n, h = int(win * sr), int(hop * sr)
    if len(m) < n:
        m = np.pad(m, (0, n - len(m)))
    idx = np.arange(0, len(m) - n + 1, h)
    fr = np.stack([m[i:i + n] for i in idx]) * np.hanning(n)
    P = np.abs(np.fft.rfft(fr, axis=1)) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    return idx / sr + win / 2, f, P


def flatness(p):
    p = np.maximum(p, 1e-30)
    return float(np.exp(np.mean(np.log(p))) / np.mean(p))


def noise_floor(m, sr, t0=0.0, t1=None):
    """Quiet-frame floor and HF hiss metrics between t0 and t1 (seconds); fades are excluded by the caller.
    Quiet frames = frames at least QUIET dB under the loudest frame (a sound that never gets that quiet masks its own
    floor). Returns floor (5th percentile frame level re peak), hf (mean > 6 kHz level of the quiet frames re peak),
    hf_std, slope (HF level vs total level over the decay; ~1 = HF follows the sound, ~0 = constant floor),
    flat_hf / flat_bb (spectral flatness of the quiet frames' mean spectrum, 6-16 kHz / 300 Hz-12 kHz: ~0.6-0.9 noise,
    < 0.1 tonal), nq (number of quiet frames)."""
    short = len(m) / sr < 0.6
    t, f, P = frame_spectra(m, sr, *((0.01, 0.005) if short else (0.02, 0.01)))
    t1 = t[-1] if t1 is None else t1
    sel = (t >= t0) & (t <= t1)
    if sel.sum() < 5:
        sel = np.ones_like(t, bool)
    P = P[sel]
    tot = db(P.sum(axis=1))
    hf = db(P[:, f > 6000].sum(axis=1))
    ref = tot.max()
    tot -= ref
    hf -= ref
    floor = float(np.percentile(tot, 5))
    q = tot <= -QUIET
    out = dict(floor=floor, nq=int(q.sum()), hf=float("nan"), hf_std=float("nan"), slope=float("nan"),
               flat_hf=float("nan"), flat_bb=float("nan"), floor_std=float("nan"))
    if q.sum() < 3:
        return out
    qq = q & (tot <= np.percentile(tot[q], 50))     # the quieter half of the quiet frames
    if qq.sum() < 3:
        qq = q
    mp = P[qq].mean(axis=0)
    dec = tot < -20                                  # decay / quiet region: does HF follow the body?
    slope = float(np.polyfit(tot[dec], hf[dec], 1)[0]) if dec.sum() >= 6 and np.ptp(tot[dec]) > 8 else float("nan")
    out.update(hf=float(np.mean(hf[qq])), hf_std=float(np.std(hf[qq])), slope=slope,
               flat_hf=flatness(mp[(f > 6000) & (f < 16000)]), flat_bb=flatness(mp[(f > 300) & (f < 12000)]),
               floor_std=float(np.std(tot[qq])))
    return out


def dc_offset(x):
    """DC per channel as a Hann-weighted mean (dBFS): a plain mean over a short one-shot measures the half-cycles of
    its low end, not an offset; the window keeps AC content above ~2/T Hz out of the estimate."""
    w = np.hanning(len(x))[:, None]
    return 20 * np.log10(np.abs((x * w).sum(axis=0) / w.sum()).max() + 1e-12)


def steady_lines(m, sr):
    """Narrow lines that never move: mains hum family and > 10 kHz whine. Returns [(Hz, dB re total, kind)]."""
    if len(m) < sr:
        return []
    nper = 2 ** int(np.log2(sr * 0.68))
    f, psd = signal.welch(m, sr, nperseg=nper, noverlap=nper // 2)
    L = db(psd)
    prom = L - median_filter(L, size=61, mode="nearest")
    cand = []
    for i in signal.find_peaks(prom, height=LINE_PROM)[0]:
        fr = f[i]
        mains = any(abs(fr - k * base) <= 1.5 for base in (50.0, 60.0) for k in range(1, 9))
        if mains or fr > 10000:
            cand.append((i, fr, "hum" if mains else "whine"))
    if not cand:
        return []
    f2, t2, S = signal.spectrogram(m, sr, nperseg=nper, noverlap=0)
    S = db(S)
    base = median_filter(S, size=(61, 1), mode="nearest")
    out = []
    for i, fr, kind in cand:
        p = S[i] - base[i]
        if (p > 6).mean() >= 0.8 and np.std(S[i][p > 6]) < 3.0:
            out.append((round(fr, 1), round(float(L[i] - db(psd.sum())), 1), kind))
    return out


def clip_runs(x):
    """Flat-topped peaks per channel of a raw source segment: (count, longest run in samples).
    A run = >= 3 consecutive samples within CLIP_TOL of the segment's absolute max (3 %: lossy decoders put about +-1.5 %
    ripple on a clipped plateau). A smooth crest of the half-cycle it sits in (zero crossing to zero crossing, H samples)
    stays within the tolerance for H * 2 acos(1 - tol) / pi samples; a run more than CLIP_RATIO times that long is a
    plateau, i.e. clipping."""
    count, longest = 0, 0
    k = 2 * np.arccos(1 - CLIP_TOL) / np.pi
    for c in range(x.shape[1]):
        y = x[:, c]
        a = np.abs(y)
        M = a.max()
        if M <= 0:
            continue
        hit = a >= M * (1 - CLIP_TOL)
        d = np.diff(np.concatenate([[0], hit.astype(np.int8), [0]]))
        for s, e in zip(np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]):
            if e - s < 3:
                continue
            sg = np.sign(y[s:e].mean())
            lo = s - 1
            while lo >= 0 and lo > s - 8192 and y[lo] * sg > 0:
                lo -= 1
            hi = e
            while hi < len(y) and hi < e + 8192 and y[hi] * sg > 0:
                hi += 1
            if (e - s) >= max(3.0, CLIP_RATIO * k * (hi - lo - 1)):
                count += 1
                longest = max(longest, e - s)
    return count, longest


def seam(x):
    """Wrap-point checks for a loop."""
    m = x.mean(axis=1)
    k = int(0.05 * SR)
    around = np.concatenate([m[-k:], m[:k]])
    dmot = np.sqrt(np.mean(np.diff(around) ** 2)) + 1e-12
    jump = float(np.max(np.abs(x[0] - x[-1])) / dmot)
    w = int(0.25 * SR)
    rms = lambda s: db(np.mean(s ** 2))  # noqa: E731
    seam_d = abs(rms(m[-w:]) - rms(m[:w]))
    nat = [abs(rms(m[i:i + w]) - rms(m[i + w:i + 2 * w])) for i in range(0, len(m) - 2 * w, w // 2)]
    nat95 = float(np.percentile(nat, 95)) if nat else 0.0
    hp = signal.sosfilt(signal.butter(4, 3000, "high", fs=SR, output="sos"),
                        np.concatenate([m[-SR // 2:], m[:SR // 2]]))
    b = int(0.002 * SR)
    e = np.array([np.mean(hp[i:i + b] ** 2) for i in range(0, len(hp) - b, b)])
    c = len(e) // 2
    burst = float(db(e[c - 1:c + 1].max()) - db(np.median(e)))
    return dict(jump=jump, lvl=float(seam_d), nat95=nat95, burst=burst)


def ends(x, e):
    m = np.abs(x).max(axis=1)
    pk = m.max()
    first, last = float(m[0]), float(m[-1])
    fout = e.get("fout", 0.02)
    n = int((fout + 0.03) * SR)
    pre = m[len(m) - n: len(m) - int(fout * SR)] if len(m) > n else m
    tail_db = 20 * np.log10(np.sqrt(np.mean(pre ** 2)) / pk + 1e-12)
    return dict(first=20 * np.log10(first + 1e-12), last=20 * np.log10(last + 1e-12), tail=float(tail_db), fout=fout)


def cut_context(ly, hp=40.0):
    """Source segment plus: level just before the cut start / just after the cut end re the segment peak (dB), and the
    recording's own background = 5th percentile of 20 ms frame levels within +-10 s of the cut, re the segment's loudest
    20 ms frame (dB), measured above the entry's high-pass (inaudible rumble the build removes does not count)."""
    x, sr = pa.read(os.path.join(pa.RAW, ly["src"]))
    a = int(round(ly["start"] * sr))
    b = len(x) if ly["end"] is None else min(len(x), int(round(ly["end"] * sr)))
    seg = x[a:b]
    pk = np.abs(seg).max() + 1e-12
    w = int(0.01 * sr)
    before = 20 * np.log10(np.sqrt(np.mean(x[max(0, a - w):a] ** 2)) / pk + 1e-12) if a >= w else -120.0
    after = 20 * np.log10(np.sqrt(np.mean(x[b:b + w] ** 2)) / pk + 1e-12) if b + w <= len(x) else -120.0
    h = int(0.02 * sr)
    m = signal.sosfilt(signal.butter(4, hp, "high", fs=sr, output="sos"), x.mean(axis=1))
    lv = db(np.mean(m[: len(m) // h * h].reshape(-1, h) ** 2, axis=1))
    lo, hi = max(0, a // h - 500), min(len(lv), b // h + 500)
    near = lv[lo:hi][lv[lo:hi] > -200]
    seg_pk = lv[a // h: max(a // h + 1, b // h)].max()
    rec = float(np.percentile(near, 5) - seg_pk) if len(near) else -120.0
    return seg, sr, float(before), float(after), rec


# --------------------------------------------------------------------------------------------------------------------
# Per-entry report
# --------------------------------------------------------------------------------------------------------------------
def check(e):
    name = e["name"]
    fails, warns, info = [], [], {}
    path = os.path.join(pa.OUT, name + ".wav")
    if not os.path.exists(path):
        return dict(name=name, fails=["missing output"], warns=[], info={})
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    if sr != SR or x.shape[1] != e["ch"]:
        fails.append(f"format {sr} Hz / {x.shape[1]} ch (want {SR} / {e['ch']})")
    target = e.get("target", pa.TARGET[e["cat"]])
    tp, ld = pa.true_peak(x), pa.lufs(x)
    dc = dc_offset(x)
    info.update(tp=tp, lufs=ld, target=target, dc=dc, dur=len(x) / SR)
    if tp > TP_MAX:
        fails.append(f"true peak {tp:+.2f} dBTP")
    if abs(ld - target) > LU_TOL:
        fails.append(f"loudness {ld:.1f} LUFS vs {target:.0f}")
    if dc > DC_MAX_DB:
        fails.append(f"DC {dc:.0f} dBFS")

    # Sources: clipping (+ cut context for one-shots).
    clips, recs, sdc = [], [], []
    for ly in e["layers"]:
        seg, ssr, before, after, rec = cut_context(ly, e.get("hp", 40.0))
        n, longest = clip_runs(seg)
        if n:
            clips.append(f"{os.path.basename(ly['src'])[:28]}@{ly['start']:.2f}: {n} flat tops (<= {longest} smp)")
        recs.append(rec)
        sdc.append(dc_offset(seg))
        if not is_loop(e) and len(e["layers"]) == 1:
            info.setdefault("cut", (before, after))
    info["rec"], info["src_dc"] = max(recs), max(sdc)
    if clips:
        fails.append("source clipped: " + "; ".join(clips))

    m = x.mean(axis=1)
    if is_loop(e):
        lines = steady_lines(m, SR)
        info["lines"] = lines
        if lines:
            fails.append("steady line(s): " + ", ".join(f"{fq:g} Hz {kind} ({lv:+.0f} dB)" for fq, lv, kind in lines))
        s = seam(x)
        info["seam"] = s
        if s["jump"] > SEAM_JUMP:
            fails.append(f"seam jump {s['jump']:.1f}x")
        if s["lvl"] > max(SEAM_LVL, s["nat95"]):
            fails.append(f"seam level step {s['lvl']:.1f} dB (own p95 {s['nat95']:.1f})")
        if s["burst"] > SEAM_CLICK:
            fails.append(f"seam click +{s['burst']:.0f} dB HF")
    else:
        fin, fout = e.get("fin", 0.002), e.get("fout", 0.02)
        nf = noise_floor(m, SR, fin + 0.005, len(m) / SR - max(fout, 0.005))
        info["noise"] = nf
        if nf["nq"] >= 3:
            # The quiet frames ARE the recording's background (within 8 dB of it) rather than a soft part of the sound.
            exposed = nf["floor"] - info["rec"] < 8.0
            steady_hf = (nf["slope"] < HISS_SLOPE) if not np.isnan(nf["slope"]) else (nf["hf_std"] < HISS_STD)
            if exposed and nf["hf"] > HISS_HF and nf["flat_hf"] > NOISY and steady_hf:
                fails.append(f"hiss: HF floor {nf['hf']:.0f} dB re peak, noise-like (flat {nf['flat_hf']:.2f}), "
                             f"steady (std {nf['hf_std']:.1f}, slope {nf['slope']:.2f})")
            if exposed and nf["floor"] > FLOOR_WARN and nf["flat_bb"] > NOISY / 3:
                warns.append(f"steady background {nf['floor']:.0f} dB re peak (recording floor {info['rec']:.0f})")
        if len(m) >= SR:
            lines = [ln for ln in steady_lines(m, SR) if ln[2] == "hum" or ln[1] > -70]
            if lines:
                fails.append("steady line(s): " + ", ".join(f"{fq:g} Hz {kind} ({lv:+.0f} dB)"
                                                             for fq, lv, kind in lines))
        en = ends(x, e)
        info["ends"] = en
        if en["first"] > -60 or en["last"] > -60:
            fails.append(f"end samples not at zero ({en['first']:.0f} / {en['last']:.0f} dBFS)")
        if en["tail"] > -20 and en["fout"] < 0.05:
            fails.append(f"abrupt stop: tail {en['tail']:.0f} dB re peak with a {en['fout'] * 1000:.0f} ms fade")
        if "cut" in info:
            before, after = info["cut"]
            if before > -30 and fin < 0.02:
                warns.append(f"cut starts mid-sound ({before:.0f} dB) with a short fade-in")
            if after > -30 and en["fout"] < 0.05:
                warns.append(f"cut ends mid-sound ({after:.0f} dB) with a short fade")
    return dict(name=name, fails=fails, warns=warns, info=info)


def source_noise(e):
    """Noise metrics of the (first) source segment, for the report: where does a high floor come from?"""
    ly = e["layers"][0]
    seg, ssr, _, _, _ = cut_context(ly)
    return noise_floor(seg.mean(axis=1), ssr)


def main(only):
    rows = [check(e) for e in pa.TABLE if not only or e["name"] in only]
    print(f"\n{'name':24} {'cat':5} {'dur':>6} {'TP':>6} {'LUFS':>6} {'tgt':>4} {'DC':>5} {'srcDC':>5} {'rec':>4}  "
          f"{'floor':>5} {'hf6k':>5} {'flat':>4} {'slope':>5} | seam jump/lvl/p95/click | verdict")
    nfail = nwarn = 0
    for e, r in zip([e for e in pa.TABLE if not only or e["name"] in only], rows):
        i = r["info"]
        if not i:
            print(f"{r['name']:24} MISSING")
            nfail += 1
            continue
        nz = i.get("noise")
        nzs = (f"{nz['floor']:5.0f} {nz['hf']:5.0f} {nz['flat_hf']:4.2f} {nz['slope']:5.2f}" if nz
               else f"{'':5} {'':5} {'':4} {'':5}")
        s = i.get("seam")
        ss = f"{s['jump']:4.1f} {s['lvl']:4.1f} {s['nat95']:4.1f} {s['burst']:+4.0f}" if s else f"{'':19}"
        verdict = (("FAIL: " + "; ".join(r["fails"])) if r["fails"] else "ok") + \
            (("  (warn: " + "; ".join(r["warns"]) + ")") if r["warns"] else "")
        nfail += bool(r["fails"])
        nwarn += bool(r["warns"])
        print(f"{r['name']:24} {e['cat']:5} {i['dur']:6.2f} {i['tp']:6.2f} {i['lufs']:6.1f} {i['target']:4.0f} "
              f"{i['dc']:5.0f} {i['src_dc']:5.0f} {i['rec']:4.0f}  {nzs} | {ss:19} | {verdict}")
    # Category consistency
    print("\nper-category loudness (LUFS):")
    cats = {}
    for e, r in zip([e for e in pa.TABLE if not only or e["name"] in only], rows):
        if r["info"]:
            cats.setdefault(e["cat"], []).append(r["info"]["lufs"])
    for c, v in cats.items():
        print(f"  {c:6} target {pa.TARGET[c]:5.1f}  n={len(v):2}  min {min(v):6.1f}  max {max(v):6.1f}  "
              f"spread {max(v) - min(v):4.1f} LU")
    if "--src" in sys.argv:
        print("\nsource segments (first layer): floor / hf6k / flat_hf / flat_bb / std / slope / quiet frames")
        for e in pa.TABLE:
            if (not only or e["name"] in only) and not is_loop(e):
                nz = source_noise(e)
                print(f"  {e['name']:24} {nz['floor']:6.1f} {nz['hf']:6.1f} {nz['flat_hf']:5.2f} {nz['flat_bb']:5.2f} "
                      f"{nz['hf_std']:4.1f} {nz['slope']:5.2f} {nz['nq']:4d}")
    print(f"\n{len(rows)} sounds checked: {len(rows) - nfail} pass, {nfail} FAIL, {nwarn} with warnings")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main({a for a in sys.argv[1:] if not a.startswith("--")}))
