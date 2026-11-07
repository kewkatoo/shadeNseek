"""Turn the CC0 / public-domain source recordings in asset-library/Audio/<source>/ into game-ready WAVs.

Output: asset-library/Audio/game/<UE_NAME>.wav - 48 kHz, 16-bit PCM (TPDF dither), mono for point-source SFX and
footsteps, stereo for music / ambience / 2D stingers. Every output is cut from the source(s) listed in TABLE below,
high-passed below ~40 Hz, silence-trimmed with short fades (one-shots) or made seamless by crossfading the tail into
the head (*_Loop), loudness-normalised to a per-category target (ITU-R BS.1770 K-weighted, gated integrated LUFS) and
run through a 4x-oversampled look-ahead limiter with a -1 dBTP ceiling.

Sources and licences: asset-library/Audio/manifest_audio.json (all CC0 1.0).
Needs: Python 3.11, numpy, scipy, soundfile (libsndfile >= 1.1 for mp3). No ffmpeg.
Run:   python -I Scripts/prep_audio.py [only_this_name ...]
"""
import os
import sys
import zlib
from fractions import Fraction

import numpy as np
import soundfile as sf
from scipy import signal
from scipy.ndimage import minimum_filter1d, uniform_filter1d

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "asset-library", "Audio")
OUT = os.path.join(RAW, "game")
SR = 48000
CEILING_DBTP = -1.0

# Loudness targets (LUFS) per category.
TARGET = {
    "music": -18.0,   # MUS_*
    "amb": -24.0,     # ambience beds
    "foot": -26.0,    # paws
    "foley": -24.0,   # jump / land (body foley, a touch above paws)
    "ui": -22.0,      # menu sounds, timer tick
    "env": -22.0,     # distant world one-shots (gulls)
    "sfx": -20.0,     # puzzle / interaction SFX
    "sting": -18.0,   # solve / objective stingers
}

FS = "freesound/"
KI = "kenney/interface-sounds/"
KM = "kenney/impact-sounds/"
KR = "kenney/rpg-audio/"
PD = "freepd/"


def L(src, start=0.0, end=None, gain=0.0, at=0.0, rate=1.0, chan=None):
    """One source layer: file (relative to asset-library/Audio), cut [start, end) seconds, gain in dB relative to the
    other layers (each layer is first matched to the same loudness), placed `at` seconds into the output, played back at
    `rate` (>1 = faster/higher), `chan` = 0/1 to take one channel of a stereo file instead of the mid."""
    return dict(src=src, start=start, end=end, gain=gain, at=at, rate=rate, chan=chan)


# =====================================================================================================================
# UE name -> sources, channels (1 = mono, 2 = stereo), loudness category, loop crossfade seconds (0 = one-shot).
# Optional: hp (Hz, default 40), lp (Hz), fin / fout (fade seconds for one-shots), trim (default True for one-shots).
# =====================================================================================================================
TABLE = [
    # ---- Music ------------------------------------------------------------------------------------------------------
    # "Wisdom in the Sun" (Kevin MacLeod, FreePD): warm guitars, gentle bass and bells. Full piece with its natural
    # ending (the last ~0.8 s of the archived MP3 has a damaged frame; the outro is already fading there).
    dict(name="MUS_Theme", layers=[L(PD + "wisdom_in_the_sun_kevin_macleod.mp3", 0.0, 154.4)], ch=2, cat="music",
         loop=0, fin=0.01, fout=4.0),
    # "Champ de tournesol" (Komiku, FreePD): last section (from the 65.5 s section boundary) to its natural ending.
    dict(name="MUS_Reunion", layers=[L(PD + "champ_de_tournesol_komiku.mp3", 65.30, 117.9)], ch=2, cat="music",
         loop=0, fin=0.25, fout=3.0),

    # ---- Ambience beds (stereo, seamless loops) ---------------------------------------------------------------------
    # Mediterranean waves against cliff rocks + gentle lapping against moored boats (harbour).
    dict(name="AMB_Sea_Loop", ch=2, cat="amb", loop=4.0, layers=[
        L(FS + "648860_Sea_Waves_Against_Rocks.ogg", 0.5, 88.0),
        L(FS + "637945_water_lake_waves_light_gentle_lap_at_boa.ogg", 4.0, 91.5, gain=-7.0)]),
    # Cicadas on a sunny afternoon in Bolano (La Spezia, Liguria) with the small town faintly in the distance.
    dict(name="AMB_Village_Loop", ch=2, cat="amb", loop=4.0, hp=60,
         layers=[L(FS + "584218_Cicale_Bolano_08_08_21.ogg", 196.0, 296.0)]),
    # NEW town bed: Italian piazza murmur (Padua, after lunch) + café terrace cutlery/talk + swifts passing overhead.
    dict(name="AMB_Town_Loop", ch=2, cat="amb", loop=4.0, hp=50, layers=[
        L(FS + "832691_Italian_Square_at_Sunday_Lunch_Time_Piaz.ogg", 4.0, 98.0),
        L(FS + "320026_Bukarest_2015_Restaurant_Terasse_mittags.ogg", 10.0, 104.0, gain=-9.0),
        L(FS + "690331_swifts_birds.ogg", 60.0, 154.0, gain=-5.0)]),

    # ---- World one-shots --------------------------------------------------------------------------------------------
    dict(name="SFX_Gull_01", ch=1, cat="env", hp=250, layers=[L(FS + "462462_Seagulls_gaviotas_clean_wildtrack.ogg", 7.75, 10.30)]),
    dict(name="SFX_Gull_02", ch=1, cat="env", hp=250, layers=[L(FS + "462462_Seagulls_gaviotas_clean_wildtrack.ogg", 11.00, 13.60)]),
    dict(name="SFX_Gull_03", ch=1, cat="env", hp=250, layers=[L(FS + "462462_Seagulls_gaviotas_clean_wildtrack.ogg", 25.10, 28.60)]),
    dict(name="SFX_Gull_04", ch=1, cat="env", hp=250, layers=[L(FS + "462462_Seagulls_gaviotas_clean_wildtrack.ogg", 39.50, 43.60)]),
    # The 12th (last) toll of a church bell with its full natural decay.
    dict(name="SFX_BellToll", ch=1, cat="sfx", hp=60, fout=2.5, layers=[L(FS + "611469_Church_Bell.ogg", 50.05, 62.0)]),

    # ---- Cat --------------------------------------------------------------------------------------------------------
    # Soft muffled steps (foley: fabric-wrapped fingers on wood) - six separate takes, cut before the cloth rustle tail.
    *[dict(name=f"SFX_Paw_{k + 1:02d}", ch=1, cat="foot", hp=60, fout=0.07,
           layers=[L(FS + "864782_Walking_In_Socks.ogg", a, a + 0.19)])
      for k, a in enumerate([0.03, 0.65, 1.18, 3.00, 3.64, 4.76])],
    dict(name="SFX_Jump", ch=1, cat="foley", hp=60, layers=[L(FS + "391448_Shirt_Whoosh_1.ogg", 9.70, 10.15)]),
    # Landing: two soft paw contacts (front, then hind) over a low body thud.
    dict(name="SFX_Land", ch=1, cat="foley", hp=40, fout=0.08, layers=[
        L(FS + "864782_Walking_In_Socks.ogg", 3.64, 3.86),
        L(FS + "864782_Walking_In_Socks.ogg", 4.76, 4.98, gain=-2.0, at=0.065),
        L(KM + "impactSoft_medium_001.ogg", gain=-6.0, at=0.01)]),
    dict(name="SFX_Mrrp", ch=1, cat="sfx", hp=80, layers=[L(FS + "200336_Cat_trilling.ogg", 7.66, 8.45)]),
    dict(name="SFX_Purr_Loop", ch=1, cat="sfx", loop=1.5, hp=35, layers=[L(FS + "553962_Cat_purr.ogg", 4.0, 16.0)]),

    # ---- Shade (sun damage) -----------------------------------------------------------------------------------------
    dict(name="SFX_Sizzle_Loop", ch=1, cat="sfx", loop=1.0, hp=80, layers=[L(FS + "468911_Bacon_Sizzling.ogg", 4.0, 10.0)]),
    dict(name="SFX_Evaporate", ch=1, cat="sfx", hp=50, fout=0.15, layers=[
        L(FS + "155660_cig_extinguish.ogg", 0.0, 1.06),
        L(FS + "208111_Poof_of_Smoke.ogg", 0.0, 0.92, gain=-3.0, at=0.04)]),
    dict(name="SFX_Reform", ch=1, cat="sfx", hp=60, fout=0.35, layers=[L(FS + "472587_Reversed_Discovery_Sound.ogg", 0.0, 2.84)]),

    # ---- Lemon cart -------------------------------------------------------------------------------------------------
    dict(name="SFX_Grab", ch=1, cat="sfx", layers=[L(KM + "impactWood_light_002.ogg")]),
    dict(name="SFX_Release", ch=1, cat="sfx", layers=[L(KM + "impactWood_light_000.ogg", rate=1.06)]),
    dict(name="SFX_CartBump", ch=1, cat="sfx", layers=[
        L(KM + "impactWood_heavy_001.ogg"),
        L(KM + "impactPlank_medium_002.ogg", gain=-8.0, at=0.005)]),
    dict(name="SFX_CartRoll_Loop", ch=1, cat="sfx", loop=0.4, hp=50, layers=[L(FS + "496727_04_1_Garden_wheelbarrow.ogg", 2.75, 6.55)]),
    *[dict(name=f"SFX_LemonRattle_{k + 1:02d}", ch=1, cat="sfx", hp=80, layers=[L(FS + "556975_Falling_fruit_Drumming_on_cookie_box.ogg", a, b)])
      for k, (a, b) in enumerate([(1.04, 1.64), (1.82, 2.36), (5.08, 5.74)])],

    # ---- Awning -----------------------------------------------------------------------------------------------------
    dict(name="SFX_AwningClick", ch=1, cat="sfx", hp=200, fout=0.01, layers=[L(FS + "156996_Freewheel_clicks.ogg", 1.49, 1.60)]),
    dict(name="SFX_AwningFlap", ch=1, cat="sfx", hp=60, layers=[L(FS + "244982_Wing_Flap_Flag_Flapping_5a.ogg", 0.0, 0.88)]),
    dict(name="SFX_AwningOpen", ch=1, cat="sfx", hp=50, fout=0.2, layers=[L(FS + "330638_Cloth_Flapping_On_A_Clothesline_Thickly.ogg", 4.85, 6.05)]),

    # ---- Terracotta pot ---------------------------------------------------------------------------------------------
    dict(name="SFX_PotWobble", ch=1, cat="sfx", hp=60, fout=0.1, layers=[L(FS + "776201_SFX_TeaPot_Ceramic_Wobble_Stop2.ogg", 10.40, 12.50)]),
    dict(name="SFX_PotFall", ch=1, cat="sfx", hp=50, layers=[L(FS + "816261_Broomstick_whoosh_2_Soft_low_OKM1.ogg", 0.0, 0.47)]),
    dict(name="SFX_PotLand", ch=1, cat="sfx", hp=45, fout=0.15, layers=[
        L(FS + "810043_Breaking_pot_short.ogg", 0.70, 2.30),
        L(KM + "impactSoft_heavy_002.ogg", gain=-4.0, at=0.04)]),

    # ---- UI + stingers ----------------------------------------------------------------------------------------------
    dict(name="SFX_UI_Move", ch=1, cat="ui", layers=[L(KI + "pluck_002.ogg")]),
    dict(name="SFX_UI_Select", ch=1, cat="ui", hp=80, fout=0.6, layers=[L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 1.6)]),
    dict(name="SFX_UI_Open", ch=1, cat="ui", layers=[L(KR + "bookOpen.ogg")]),     # pause menu opens like a journal
    dict(name="SFX_UI_Close", ch=1, cat="ui", layers=[L(KR + "bookClose.ogg")]),
    dict(name="SFX_Objective", ch=2, cat="sting", hp=60, fout=1.0, layers=[L(FS + "563355_Celesta_Chime.ogg", 0.0, 3.6)]),
    dict(name="SFX_Solve", ch=2, cat="sting", hp=60, fout=1.2, layers=[L(FS + "610703_Good_answer_harp_glissando.ogg", 0.0, 3.2)]),

    # ---- New puzzle SFX (no UE asset yet) ---------------------------------------------------------------------------
    dict(name="SFX_ShutterCreak", ch=1, cat="sfx", hp=60, fout=0.15, layers=[L(FS + "444799_Pivoting_Shutters_Opening_and_Closing.ogg", 8.55, 11.15)]),
    dict(name="SFX_LaundryFlap_Loop", ch=1, cat="sfx", loop=2.0, hp=60, layers=[L(FS + "330639_Cloth_Flapping_On_A_Clothesline_Gently.ogg", 2.0, 22.0)]),
    dict(name="SFX_RopeCreak", ch=1, cat="sfx", hp=60, fout=0.15, layers=[L(FS + "394222_Large_rope_pulley_operated.ogg", 0.88, 2.60)]),
    dict(name="SFX_GateCreak", ch=1, cat="sfx", hp=60, fout=0.15, layers=[L(FS + "265583_Wooden_Door_01.ogg", 0.70, 3.20)]),
    # Soft glockenspiel tick, a fourth below the select note.
    dict(name="SFX_TimerTick", ch=1, cat="ui", hp=80, fout=0.25,
         layers=[L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 0.45, rate=0.75)]),
]


# =====================================================================================================================
# I/O
# =====================================================================================================================
_cache = {}


def read(path):
    """Decode a whole file to float64 (n, ch). Reads in blocks and stops at a damaged frame instead of failing."""
    if path in _cache:
        return _cache[path]
    blocks = []
    with sf.SoundFile(path) as f:
        sr = f.samplerate
        try:
            while True:
                b = f.read(sr // 2, dtype="float64", always_2d=True)
                if not len(b):
                    break
                blocks.append(b)
        except Exception as e:  # noqa: BLE001 - libsndfile raises a generic error on a corrupt mp3 frame
            print(f"   note: {os.path.basename(path)}: decode stopped at {sum(map(len, blocks)) / sr:.2f}s ({e})")
    _cache[path] = (np.concatenate(blocks), sr)
    return _cache[path]


def resample(x, sr_from, rate=1.0):
    """Resample (n, ch) from sr_from to SR; rate > 1 also speeds the sound up (and raises its pitch)."""
    src = sr_from * rate
    if abs(src - SR) < 1e-6:
        return x
    fr = Fraction(SR / src).limit_denominator(2000)
    return signal.resample_poly(x, fr.numerator, fr.denominator, axis=0)


def write(name, x):
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(zlib.crc32(name.encode()))  # deterministic dither per file
    lsb = 1.0 / 32768.0
    tpdf = (rng.random(x.shape) - rng.random(x.shape)) * lsb
    q = np.clip(np.round((x + tpdf) * 32767.0), -32768, 32767).astype(np.int16)
    path = os.path.join(OUT, name + ".wav")
    sf.write(path, q if q.shape[1] > 1 else q[:, 0], SR, subtype="PCM_16")
    return path


# =====================================================================================================================
# DSP
# =====================================================================================================================
def k_weight(x):
    """ITU-R BS.1770 K-weighting (48 kHz coefficients)."""
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def lufs(x):
    """Gated integrated loudness (BS.1770-4). Sounds shorter than one 400 ms block are measured as a single block."""
    y = k_weight(x)
    n = len(y)
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    if n < blk:
        z = np.mean(y ** 2, axis=0).sum()
        return -0.691 + 10 * np.log10(z + 1e-20)
    starts = np.arange(0, n - blk + 1, hop)
    cs = np.vstack([np.zeros((1, y.shape[1])), np.cumsum(y ** 2, axis=0)])
    z = ((cs[starts + blk] - cs[starts]) / blk).sum(axis=1)
    ld = -0.691 + 10 * np.log10(z + 1e-20)
    g = z[ld > -70.0]
    if not len(g):
        return -70.0
    rel = -0.691 + 10 * np.log10(g.mean()) - 10.0
    g2 = z[(ld > -70.0) & (ld > rel)]
    return -0.691 + 10 * np.log10(g2.mean() + 1e-20)


def true_peak(x):
    return 20 * np.log10(np.max(np.abs(signal.resample_poly(x, 4, 1, axis=0))) + 1e-12)


def sample_peak(x):
    return 20 * np.log10(np.max(np.abs(x)) + 1e-12)


def highpass(x, hz, zero_phase):
    sos = signal.butter(4, hz, "high", fs=SR, output="sos")
    return signal.sosfiltfilt(sos, x, axis=0) if zero_phase else signal.sosfilt(sos, x, axis=0)


def lowpass(x, hz, zero_phase):
    sos = signal.butter(4, hz, "low", fs=SR, output="sos")
    return signal.sosfiltfilt(sos, x, axis=0) if zero_phase else signal.sosfilt(sos, x, axis=0)


def fades(x, fin, fout):
    x = x.copy()
    a, b = min(len(x), int(fin * SR)), min(len(x), int(fout * SR))
    if a > 1:
        x[:a] *= (np.sin(np.linspace(0, np.pi / 2, a)) ** 2)[:, None]
    if b > 1:
        x[-b:] *= (np.cos(np.linspace(0, np.pi / 2, b)) ** 2)[:, None]
    return x


def trim(x, start_db=-50.0, end_db=-60.0, pre=0.004, post=0.02):
    """Cut leading / trailing silence relative to the peak (5 ms envelope)."""
    env = uniform_filter1d(np.max(np.abs(x), axis=1), max(1, int(0.005 * SR)))
    pk = env.max()
    if pk <= 0:
        return x
    on = np.nonzero(env > pk * 10 ** (start_db / 20))[0]
    off = np.nonzero(env > pk * 10 ** (end_db / 20))[0]
    a = max(0, on[0] - int(pre * SR))
    b = min(len(x), off[-1] + int(post * SR))
    return x[a:b]


def seamless(x, xfade):
    """Equal-power crossfade of the tail into the head; the result loops without a click or a level dip."""
    n = int(xfade * SR)
    t = np.linspace(0, np.pi / 2, n)[:, None]
    y = x[n:].copy()
    y[-n:] = x[-n:] * np.cos(t) + x[:n] * np.sin(t)
    return y


def limiter(x, ceiling_db=CEILING_DBTP, look=0.003, hold=0.02, release=0.08, circular=False):
    """4x-oversampled look-ahead peak limiter. Gain reaches its target before each peak (min-hold + box smoothing over
    the look-ahead window) and recovers with an exponential release. circular=True treats the signal as a loop."""
    n = len(x)
    A, H = int(look * SR), int(hold * SR)
    pad = A + H + int(release * SR * 4)
    if circular:
        xp = np.concatenate([x[-pad:], x, x[:pad]]) if n > pad else np.concatenate([x, x, x])
        off = pad if n > pad else n
    else:
        xp = np.concatenate([np.zeros((pad, x.shape[1])), x, np.zeros((pad, x.shape[1]))])
        off = pad
    c = 10 ** (ceiling_db / 20)
    os4 = np.abs(signal.resample_poly(xp, 4, 1, axis=0)).max(axis=1)
    pk = os4[: len(xp) * 4].reshape(len(xp), 4).max(axis=1)
    g = np.minimum(1.0, c / np.maximum(pk, 1e-12))
    S = A + H + 1
    gp = np.concatenate([np.ones(H), g, np.ones(A)])
    mf = minimum_filter1d(gp, size=S, mode="nearest")
    gh = mf[S // 2: S // 2 + len(g)]                       # min over g[n-H .. n+A]
    gs = uniform_filter1d(gh, size=A + 1, mode="nearest")  # smooth attack, still <= g at every peak
    a = np.exp(-1.0 / (release * SR))
    gr = signal.lfilter([1 - a], [1, -a], gs, zi=[gs[0] * a])[0]
    gain = np.minimum(gs, gr)
    y = xp * gain[:, None]
    return y[off: off + n]


# =====================================================================================================================
# Build
# =====================================================================================================================
def to_channels(x, ch, chan=None):
    if chan is not None and x.shape[1] > 1:
        x = x[:, [chan]]
    if ch == 1:
        return x.mean(axis=1, keepdims=True)
    if x.shape[1] == 1:
        return np.repeat(x, 2, axis=1)
    return x[:, :2]


def render_layer(ly, ch):
    x, sr = read(os.path.join(RAW, ly["src"]))
    a = int(round(ly["start"] * sr))
    b = len(x) if ly["end"] is None else min(len(x), int(round(ly["end"] * sr)))
    seg = to_channels(x[a:b], ch, ly["chan"])
    return resample(seg, sr, ly["rate"])


def build(e):
    ch, loop = e["ch"], e.get("loop", 0)
    layers = []
    for ly in e["layers"]:
        y = render_layer(ly, ch)
        if len(e["layers"]) > 1:   # match layers to a common loudness, then apply their relative gains
            y = y * 10 ** ((-24.0 - lufs(y)) / 20)
        y = y * 10 ** (ly["gain"] / 20)
        layers.append((int(round(ly["at"] * SR)), y))
    n = max(o + len(y) for o, y in layers)
    x = np.zeros((n, ch))
    for o, y in layers:
        x[o:o + len(y)] += y

    x = highpass(x, e.get("hp", 40.0), zero_phase=bool(loop))
    if e.get("lp"):
        x = lowpass(x, e["lp"], zero_phase=bool(loop))
    if loop:
        x = seamless(x, loop)
    else:
        if e.get("trim", True):
            x = trim(x)
        x = fades(x, e.get("fin", 0.002), e.get("fout", 0.02))

    target = e.get("target", TARGET[e["cat"]])
    for _ in range(10):  # normalise -> limit -> re-measure (limiting a peaky sound costs some loudness)
        x = x * 10 ** ((target - lufs(x)) / 20)
        x = limiter(x, circular=bool(loop))
        if abs(lufs(x) - target) < 0.2:
            break
    if not loop:
        x = fades(x, 0.001, 0.003)  # guarantee the very first / last samples are at zero
    return x, target


def main(only):
    rows = []
    for e in TABLE:
        if only and e["name"] not in only:
            continue
        x, target = build(e)
        path = write(e["name"], x)
        y, _ = sf.read(path, always_2d=True)
        rows.append((e["name"], y.shape[1], len(y) / SR, sample_peak(y), true_peak(y), lufs(y), target,
                     e.get("loop", 0)))
    print(f"\n{'name':24} ch    dur(s)  peak dBFS  ~TP dBTP     LUFS  target  loop")
    for name, ch, dur, sp, tp, ld, tg, lp in rows:
        flag = "" if (tp <= CEILING_DBTP + 0.3 and abs(ld - tg) < 1.0) else "  <-- check"
        print(f"{name:24} {ch:2} {dur:9.2f} {sp:9.2f} {tp:9.2f} {ld:8.2f} {tg:7.1f}  {('xf %.1fs' % lp) if lp else '-':>6}{flag}")
    missing = [e["name"] for e in TABLE if not os.path.exists(os.path.join(OUT, e["name"] + ".wav"))]
    print(f"\n{len(rows)} files written to {OUT}" + (f"; MISSING: {missing}" if missing else "; all outputs present"))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(set(sys.argv[1:])))
