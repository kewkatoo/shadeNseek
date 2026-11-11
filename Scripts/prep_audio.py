"""Turn the CC0 / public-domain / CC BY source recordings in asset-library/Audio/<source>/ into game-ready WAVs.

Output: asset-library/Audio/game/<UE_NAME>.wav - 48 kHz, 16-bit PCM (TPDF dither), mono for point-source SFX and
footsteps, stereo for music / ambience / 2D stingers. Every output is cut from the source(s) listed in TABLE below,
optionally de-noised (gentle spectral gating with a noise profile from the source's own quiet frames), high-passed below
~40 Hz, silence-trimmed with short fades (one-shots) or made seamless by crossfading the tail into the head (*_Loop),
loudness-normalised to a per-category target (ITU-R BS.1770 K-weighted, gated integrated LUFS) and run through a
4x-oversampled look-ahead limiter with a -1 dBTP ceiling. Scripts/qa_audio.py checks the result.

Sources and licences: asset-library/Audio/manifest_audio.json (CC0 1.0, Public Domain Mark 1.0; the music CC BY 4.0).
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
from scipy.ndimage import maximum_filter, minimum_filter1d, uniform_filter, uniform_filter1d

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "asset-library", "Audio")
OUT = os.path.join(RAW, "game")
SR = 48000
CEILING_DBTP = -1.0

# Loudness targets (LUFS) per category. Raised 3 dB in the second quality pass: a full run measured ~-25 LUFS
# integrated (quiet next to other games / videos); the master limiter still catches stacked peaks.
TARGET = {
    "music": -15.0,   # MUS_*
    "amb": -21.0,     # ambience beds
    "foot": -23.0,    # paws
    "foley": -21.0,   # jump / land (body foley, a touch above paws)
    "ui": -19.0,      # menu sounds, timer tick
    "env": -19.0,     # distant world one-shots (gulls)
    "sfx": -17.0,     # puzzle / interaction SFX
    "sting": -15.0,   # solve / objective stingers
}

FS = "freesound/"
KI = "kenney/interface-sounds/"
KM = "kenney/impact-sounds/"
AP = "aporee/"         # Radio Aporee field recordings (Public Domain Mark), lossless excerpts
SB = "scottbuckley/"   # music, CC BY 4.0
OG = "opengameart/"
# The glockenspiel sample sounds at ~2801 Hz (F7): playback rates that put it on F6 / C6 for the menu open / close pair.
GLOCK_F6 = 0.5
GLOCK_C6 = 1046.5 / 2801.0


def L(src, start=0.0, end=None, gain=0.0, at=0.0, rate=1.0, chan=None, fade=None):
    """One source layer: file (relative to asset-library/Audio), cut [start, end) seconds, gain in dB relative to the
    other layers (each layer is first matched to the same loudness), placed `at` seconds into the output, played back at
    `rate` (>1 = faster/higher), `chan` = 0/1 to take one channel of a stereo file instead of the mid, `fade` =
    (in, out) seconds of fade on this layer alone (e.g. to damp a ringing note under the next one)."""
    return dict(src=src, start=start, end=end, gain=gain, at=at, rate=rate, chan=chan, fade=fade)


# =====================================================================================================================
# UE name -> sources, channels (1 = mono, 2 = stereo), loudness category, loop crossfade seconds (0 = one-shot).
# Optional: hp (Hz, default 40), lp (Hz), fin / fout (fade seconds for one-shots), trim (default True for one-shots),
#           dn (dB of gentle spectral-gating noise reduction, profile from each source's own quiet frames, ~10-12),
#           notch (list of Hz: narrow notches for steady electronic lines), even (seconds: slow level-evening window for
#           loops that should roll steadily; removes 60 % of the level drift slower than the window).
# =====================================================================================================================
TABLE = [
    # ---- Music ------------------------------------------------------------------------------------------------------
    # "Home Was You" (Scott Buckley, CC BY 4.0): piano, strings, accordion and soft folk percussion. The theme is the
    # long build (0:18-3:35) - a ~2 minute run rides the swell; the reunion is the solo-piano coda that reprises it.
    dict(name="MUS_Theme", layers=[L(SB + "home_was_you_scott_buckley.mp3", 18.0, 215.0)], ch=2, cat="music",
         loop=0, fin=1.5, fout=6.0),
    dict(name="MUS_Reunion", layers=[L(SB + "home_was_you_scott_buckley.mp3", 203.0, 269.0)], ch=2, cat="music",
         loop=0, fin=1.0, fout=4.0),

    # ---- Ambience beds (stereo, seamless loops) ---------------------------------------------------------------------
    # Lossless 24-bit field recordings from the coast (Radio Aporee, public domain): sea against the rocks in Naples +
    # water lapping at jetty rocks in Villefranche-sur-Mer.
    dict(name="AMB_Sea_Loop", ch=2, cat="amb", loop=4.0, layers=[
        L(AP + "aporee_67253_naples_waves_on_rocks.flac", 3.0, 78.0),
        L(AP + "aporee_27701_villefranche_jetty_lapping.flac", 5.0, 80.0, gain=-6.0)]),
    # Cicadas in the pines of the Certosa park, Padula (Campania) - the steady stretch before a crow passes.
    dict(name="AMB_Village_Loop", ch=2, cat="amb", loop=4.0, hp=60,
         layers=[L(AP + "aporee_64774_padula_cicadas.flac", 2.0, 52.0)]),
    # Town bed: café terrace on the little square of Corniglia (Cinque Terre) + swifts passing overhead.
    dict(name="AMB_Town_Loop", ch=2, cat="amb", loop=4.0, hp=50, layers=[
        L(AP + "aporee_65200_corniglia_cafe_terrace.flac", 4.0, 98.0),
        L(FS + "690331_swifts_birds.ogg", 60.0, 154.0, gain=-5.0)]),

    # ---- World one-shots --------------------------------------------------------------------------------------------
    # Gull calls from a busy wildtrack (surf + distant gulls only ~22-30 dB under the calls, a steady white-ish hiss in
    # the gaps): gently de-noised (12 dB) and low-passed at 9 kHz (distant calls lose their top anyway), cut at the
    # quietest points between calls, with soft edges.
    *[dict(name=f"SFX_Gull_{k + 1:02d}", ch=1, cat="env", hp=250, lp=9000, dn=12, fin=0.03, fout=0.2,
           layers=[L(FS + "462462_Seagulls_gaviotas_clean_wildtrack.ogg", a, b)])
      for k, (a, b) in enumerate([(7.75, 10.30), (11.00, 13.50), (25.10, 28.55), (39.40, 43.45)])],
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
    # Room noise ~40 dB under the trill: 10 dB of gentle de-noising.
    dict(name="SFX_Mrrp", ch=1, cat="sfx", hp=80, dn=10, layers=[L(FS + "200336_Cat_trilling.ogg", 7.66, 8.45)]),
    # A clean, lossless sleepy purr loop (Kerzoven, OpenGameArt, CC0).
    dict(name="SFX_Purr_Loop", ch=1, cat="sfx", loop=0.5, hp=35, layers=[L(OG + "kerzoven_cat_purrsleepy_loop.wav")]),

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
    # Rolling: the constant rattle of a light wagon's wooden spoked, iron-tyred wheels on hard ground (no horse audible,
    # no squeak or hum), the steadiest stretch, evened out and nudged up in pitch for a smaller cart. Code varies
    # volume with speed and pitch 0.85-1.15 around this.
    dict(name="SFX_CartRoll_Loop", ch=1, cat="sfx", loop=1.0, hp=60, even=0.5,
         layers=[L(FS + "437077_G52-06-Buckboard_Wagon.ogg", 23.0, 28.4, rate=1.08)]),
    # Lemons shifting in the crate as the cart jolts: potatoes settling on a wooden table after a pour (soft, woody
    # knocks, pitched up for smaller fruit) with one apple-on-apple knock on top.
    *[dict(name=f"SFX_LemonRattle_{k + 1:02d}", ch=1, cat="sfx", hp=110, fin=0.005, fout=0.06, layers=[
        L(FS + "656727_Pouring_Potatoes_Onto_a_Table.ogg", a, b, rate=1.2),
        L(FS + "276882_Bumping_apples_Pouring_Apples.ogg", c, c + 0.11, gain=-3.0, at=at)])
      for k, (a, b, c, at) in enumerate([(6.95, 7.48, 9.296, 0.10), (26.50, 26.98, 10.264, 0.05),
                                         (16.62, 17.10, 4.790, 0.13)])],

    # ---- Awning -----------------------------------------------------------------------------------------------------
    dict(name="SFX_AwningClick", ch=1, cat="sfx", hp=200, fout=0.01, layers=[L(FS + "156996_Freewheel_clicks.ogg", 1.49, 1.60)]),
    dict(name="SFX_AwningFlap", ch=1, cat="sfx", hp=60, layers=[L(FS + "244982_Wing_Flap_Flag_Flapping_5a.ogg", 0.0, 0.88)]),
    # Outdoor background ~36 dB under the flap: 10 dB of gentle de-noising.
    dict(name="SFX_AwningOpen", ch=1, cat="sfx", hp=50, fout=0.2, dn=10,
         layers=[L(FS + "330638_Cloth_Flapping_On_A_Clothesline_Thickly.ogg", 4.85, 6.05)]),

    # ---- Terracotta pot ---------------------------------------------------------------------------------------------
    # The recording carries a steady 18 kHz electronic whine (-62 dB): narrow notch.
    dict(name="SFX_PotWobble", ch=1, cat="sfx", hp=60, fout=0.1, notch=[18047.0],
         layers=[L(FS + "776201_SFX_TeaPot_Ceramic_Wobble_Stop2.ogg", 10.40, 12.50)]),
    dict(name="SFX_PotFall", ch=1, cat="sfx", hp=50, layers=[L(FS + "816261_Broomstick_whoosh_2_Soft_low_OKM1.ogg", 0.0, 0.47)]),
    dict(name="SFX_PotLand", ch=1, cat="sfx", hp=45, fout=0.15, layers=[
        L(FS + "810043_Breaking_pot_short.ogg", 0.70, 2.30),
        L(KM + "impactSoft_heavy_002.ogg", gain=-4.0, at=0.04)]),

    # ---- UI + stingers ----------------------------------------------------------------------------------------------
    dict(name="SFX_UI_Move", ch=1, cat="ui", layers=[L(KI + "pluck_002.ogg")]),
    dict(name="SFX_UI_Select", ch=1, cat="ui", hp=80, fout=0.6, layers=[L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 1.6)]),
    # Pause menu open / close: the same glockenspiel as Select / TimerTick, an octave lower (softer, warmer). Open = a
    # damped C6 grace note rising to a ringing F6 (brighter, on top); Close = the mirror, a damped F6 falling to a
    # darker, shorter C6. Cut lengths are output seconds x rate, so the ringing note ends with the long fade-out.
    dict(name="SFX_UI_Open", ch=1, cat="ui", hp=200, lp=6000, fin=0.004, fout=0.3, layers=[
        L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 0.15 * GLOCK_C6, rate=GLOCK_C6, gain=-2.0, fade=(0, 0.06)),
        L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 0.435 * GLOCK_F6, rate=GLOCK_F6, at=0.065)]),
    dict(name="SFX_UI_Close", ch=1, cat="ui", hp=200, lp=3500, fin=0.004, fout=0.25, layers=[
        L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 0.15 * GLOCK_F6, rate=GLOCK_F6, fade=(0, 0.06)),
        L(FS + "348924_Glockenspiel_46_f4_04.ogg", 0.0, 0.335 * GLOCK_C6, rate=GLOCK_C6, gain=-1.0, at=0.065)]),
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


def noise_profile(x, sr, a, b, pad=10.0, nper=2048):
    """Mean power spectrum (mono) of the quietest 10 % of STFT frames of x between a - pad and b + pad seconds: the
    recording's own background, taken from the gaps between events."""
    lo, hi = max(0, int((a - pad) * sr)), min(len(x), int((b + pad) * sr))
    _, _, Z = signal.stft(x[lo:hi].mean(axis=1), sr, nperseg=nper, noverlap=nper * 3 // 4)
    P = np.abs(Z) ** 2
    e = P.sum(axis=0)
    ok = e > 1e-12                                   # ignore digital silence
    q = ok & (e <= np.percentile(e[ok], 10))
    return P[:, q].mean(axis=1)


def denoise(x, sr, noise, reduce_db, nper=2048):
    """Gentle spectral gating. Per STFT bin (2048 / 75 % overlap), the mono SNR against the noise profile maps to a soft
    mask (3 dB above the noise mean -> closed, 12 dB -> open), held over 3 frames so decays are not chopped and smoothed
    over +-2 bins x +-2 frames so single noise bins never pop open (no 'musical noise'). Closed bins are turned down by
    reduce_db only, never muted. Bins well above the noise pass untouched (the STFT round trip is exact there)."""
    _, _, Z = signal.stft(x.T, sr, nperseg=nper, noverlap=nper * 3 // 4)       # (ch, F, T)
    P = (np.abs(Z) ** 2).mean(axis=0)
    snr = 10 * np.log10(P / (noise[:, None] + 1e-30) + 1e-30)
    m = np.clip((snr - 3.0) / 9.0, 0.0, 1.0)
    m = maximum_filter(m, size=(1, 3))
    m = uniform_filter(m, size=(5, 5), mode="nearest")
    floor = 10 ** (-reduce_db / 20)
    _, y = signal.istft(Z * (floor + (1 - floor) * m)[None], sr, nperseg=nper, noverlap=nper * 3 // 4)
    y = y.T[: len(x)]
    return np.pad(y, ((0, len(x) - len(y)), (0, 0))) if len(y) < len(x) else y


def notch(x, hz, zero_phase, q=30.0):
    b, a = signal.iirnotch(hz, q, fs=SR)
    return signal.filtfilt(b, a, x, axis=0) if zero_phase else signal.lfilter(b, a, x, axis=0)


def even_out(x, win, amount=0.6):
    """Slow level-evening for steady loops: removes `amount` of the loudness drift slower than `win` seconds."""
    env = 10 * np.log10(uniform_filter1d(np.mean(x ** 2, axis=1), max(1, int(win * SR)), mode="nearest") + 1e-20)
    g = -amount * (env - np.mean(env))
    return x * (10 ** (g / 20))[:, None]


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


def render_layer(ly, ch, dn=0.0):
    x, sr = read(os.path.join(RAW, ly["src"]))
    a = int(round(ly["start"] * sr))
    b = len(x) if ly["end"] is None else min(len(x), int(round(ly["end"] * sr)))
    if dn:   # de-noise with 0.25 s of real context on both sides (STFT edges), then cut
        lo, hi = max(0, a - sr // 4), min(len(x), b + sr // 4)
        seg = denoise(x[lo:hi], sr, noise_profile(x, sr, a / sr, b / sr), dn)[a - lo: b - lo]
    else:
        seg = x[a:b]
    seg = resample(to_channels(seg, ch, ly["chan"]), sr, ly["rate"])
    return fades(seg, *ly["fade"]) if ly.get("fade") else seg


def build(e):
    ch, loop = e["ch"], e.get("loop", 0)
    layers = []
    for ly in e["layers"]:
        y = render_layer(ly, ch, e.get("dn", 0.0))
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
    for hz in e.get("notch", ()):
        x = notch(x, hz, zero_phase=bool(loop))
    if e.get("even"):
        x = even_out(x, e["even"])
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
