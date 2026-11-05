"""Procedural sound design + music for Shade and Seek.

Every sound effect, ambience and the score are synthesised here from scratch (no samples, no third-party audio), so
the audio is original and licence-free. Plucked strings use Karplus-Strong via scipy.lfilter (fast), the accordion
is a musette-tuned reed model, bells are inharmonic partial stacks.

Output: 48 kHz 16-bit WAV (stereo for music/ambience, mono for 3D SFX) into asset-library/Audio/generated/.
Run:  python -I Scripts/gen_audio.py <output_dir>
"""
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
RNG = np.random.default_rng(20261105)
OUT = sys.argv[1] if len(sys.argv) > 1 else "asset-library/Audio/generated"
os.makedirs(OUT, exist_ok=True)


# =====================================================================================================================
# DSP building blocks
# =====================================================================================================================
def n_of(dur):
    return int(round(dur * SR))


def t_axis(dur):
    return np.arange(n_of(dur)) / SR


def noise(dur):
    return RNG.standard_normal(n_of(dur))


def pink(dur):
    """Voss-ish pink noise via FFT shaping."""
    n = n_of(dur)
    x = RNG.standard_normal(n)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    f[0] = f[1]
    X /= np.sqrt(f)
    y = np.fft.irfft(X, n)
    return y / (np.std(y) + 1e-9)


def brown(dur):
    y = np.cumsum(RNG.standard_normal(n_of(dur)))
    y = hp(y, 15)
    return y / (np.std(y) + 1e-9)


def bp(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, min(f, SR * 0.45), btype="low", fs=SR, output="sos"), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype="high", fs=SR, output="sos"), x)


def peak(x, f, q, gain_db):
    """RBJ peaking EQ."""
    a = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f / SR
    alpha = np.sin(w0) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w0), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w0), 1 - alpha / a]
    return signal.lfilter(b, den, x)


def env_exp(n, decay):
    return np.exp(-np.arange(n) / max(decay * SR, 1))


def env_ar(n, a, r):
    an = max(1, int(a * SR))
    e = np.ones(n)
    e[:an] = np.linspace(0, 1, an) if an < n else np.linspace(0, 1, n)
    rn = max(1, int(r * SR))
    if rn < n:
        e[-rn:] *= np.linspace(1, 0, rn)
    return e


def pad_to(x, n):
    return np.pad(x, (0, max(0, n - len(x))))[:n]


def normalize(x, peak_level=0.89):
    m = np.max(np.abs(x)) + 1e-9
    return x / m * peak_level


def fade(x, fin=0.003, fout=0.02):
    x = x.copy()
    a, b = int(fin * SR), int(fout * SR)
    if a > 0:
        x[:a] *= np.linspace(0, 1, a)
    if b > 0:
        x[-b:] *= np.linspace(1, 0, b)
    return x


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


def reverb_ir(size=1.6, damp=6000, predelay=0.015, stereo=True):
    n = n_of(size)
    out = []
    for _ in range(2 if stereo else 1):
        ir = RNG.standard_normal(n) * np.exp(-np.arange(n) / (size * SR / 6.0))
        ir = lp(ir, damp)
        ir = np.concatenate([np.zeros(n_of(predelay)), ir])
        ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
        out.append(ir)
    return out


def reverb(x, size=1.4, mix=0.22, damp=6000, predelay=0.015):
    """Mono in -> mono out."""
    ir = reverb_ir(size, damp, predelay, stereo=False)[0]
    wet = signal.fftconvolve(x, ir)[: len(x)]
    return x * (1 - mix) + wet * mix


def reverb_stereo(L, R, size=2.0, mix=0.25, damp=5500, predelay=0.02):
    irL, irR = reverb_ir(size, damp, predelay, stereo=True)
    wl = signal.fftconvolve(L, irL)[: len(L)]
    wr = signal.fftconvolve(R, irR)[: len(R)]
    return L * (1 - mix) + wl * mix, R * (1 - mix) + wr * mix


def write(name, x, stereo_r=None, peak_level=0.89, norm=True):
    if stereo_r is not None:
        st = np.stack([x, stereo_r], axis=1)
        if norm:
            st = st / (np.max(np.abs(st)) + 1e-9) * peak_level
        data = (np.clip(st, -1, 1) * 32767).astype(np.int16)
    else:
        if norm:
            x = normalize(x, peak_level)
        data = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    wavfile.write(os.path.join(OUT, name + ".wav"), SR, data)
    print("wrote", name, f"{len(data) / SR:.2f}s")


def seamless(x, xfade=0.5):
    """Loop-safe: crossfade the tail into the head."""
    n = n_of(xfade)
    head = x[:n].copy()
    tail = x[-n:].copy()
    w = np.linspace(0, 1, n)
    y = x[n:].copy()
    y[-n:] = tail * (1 - w) + head * w
    return y


# =====================================================================================================================
# Instruments
# =====================================================================================================================
def ks_pluck(freq, dur, bright=0.5, decay=0.996, pos=0.18, body=True):
    """Karplus-Strong string (lfilter comb with averaging lowpass)."""
    n = n_of(dur)
    period = SR / freq
    N = max(2, int(round(period - 0.5)))
    exc = RNG.uniform(-1, 1, N)
    exc = lp(exc, 800 + 9000 * bright, 1)
    # Pluck-position comb (removes harmonics that have a node at the pluck point).
    d = max(1, int(N * pos))
    exc = exc - np.concatenate([np.zeros(d), exc[:-d]])
    x = np.zeros(n)
    x[:N] = exc
    g = decay ** (440.0 / max(freq, 40.0)) ** 0.25
    a = np.zeros(N + 2)
    a[0] = 1.0
    a[N] = -0.5 * g
    a[N + 1] = -0.5 * g
    y = signal.lfilter([1.0], a, x)
    y = y * env_ar(n, 0.001, min(0.08, dur * 0.3))
    if body:
        y = peak(y, 105, 1.4, 5)
        y = peak(y, 230, 1.6, 3)
        y = peak(y, 2600, 1.0, -3)
    return y


def nylon(freq, dur, vel=0.8):
    y = ks_pluck(freq, dur, bright=0.18 + 0.12 * vel, decay=0.9965, pos=0.2)
    return lp(y, 2800, 2) * vel


def mandolin_note(freq, dur, vel=0.8):
    # Paired courses, a hair apart in tuning; bright pick.
    a = ks_pluck(freq * 1.0012, dur, bright=0.5, decay=0.9935, pos=0.12, body=False)
    b = ks_pluck(freq * 0.9991, dur, bright=0.45, decay=0.993, pos=0.13, body=False)
    y = peak(a + b, 900, 1.2, 3)
    y = peak(y, 2200, 1.5, 1.5)
    return lp(y, 4500, 2) * vel * 0.6


def mandolin_tremolo(freq, dur, vel=0.75, rate=13.0):
    n = n_of(dur)
    out = np.zeros(n)
    step = 1.0 / rate
    t = 0.0
    k = 0
    while t < dur - 0.02:
        v = vel * (0.78 if k % 2 else 0.95) * (0.9 + 0.1 * np.sin(t * 3.0))
        note = mandolin_note(freq, min(0.5, dur - t + 0.3), v)
        i = n_of(t)
        seg = note[: n - i]
        out[i:i + len(seg)] += seg
        t += step * (1 + RNG.uniform(-0.04, 0.04))
        k += 1
    return out * env_ar(n, 0.02, 0.12)


def accordion(freqs, dur, vel=0.5):
    """Musette reeds: three detuned pulse reeds per note, bellows swell, gentle lowpass."""
    n = n_of(dur)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for f in freqs:
        for cents in (-11.0, 0.0, 12.5):
            ff = f * 2 ** (cents / 1200)
            ph = (ff * t + RNG.uniform()) % 1.0
            out += np.where(ph < 0.32, 1.0, -0.47) * (0.8 if cents == 0 else 0.55)
    out = lp(out, 2600, 2)
    out = hp(out, 120, 1)
    bellows = env_ar(n, 0.35, 0.45) * (0.85 + 0.15 * np.sin(2 * np.pi * 0.35 * t))
    return out * bellows * vel / (len(freqs) * 2.5)


def pizz_bass(freq, dur, vel=0.8):
    y = ks_pluck(freq, dur, bright=0.18, decay=0.994, pos=0.25)
    return lp(y, 900, 2) * vel


def bell(freq, dur, vel=0.6, decay=1.4):
    n = n_of(dur)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for ratio, amp, dk in ((1.0, 1.0, 1.0), (2.76, 0.45, 0.55), (5.4, 0.22, 0.3), (8.93, 0.1, 0.18)):
        y += amp * np.sin(2 * np.pi * freq * ratio * t) * np.exp(-t / (decay * dk))
    y *= env_ar(n, 0.002, 0.05)
    return y * vel


def tambourine(vel=0.5):
    n = n_of(0.35)
    jingle = bp(noise(0.35), 5200, 11000, 2) * env_exp(n, 0.07)
    ring = np.zeros(n)
    t = np.arange(n) / SR
    for f in (6100, 7400, 8800, 10100):
        ring += np.sin(2 * np.pi * f * t + RNG.uniform(0, 6)) * np.exp(-t / 0.09)
    skin = lp(noise(0.35), 400) * env_exp(n, 0.02) * 0.6
    return lp(jingle * 0.6 + ring * 0.08 + skin, 8000) * vel


def shaker(vel=0.3):
    n = n_of(0.12)
    return bp(noise(0.12), 3500, 7000) * env_ar(n, 0.015, 0.08) * vel * 0.5


# =====================================================================================================================
# Score: an original waltz in G major, 3/4, 96 bpm (6 eighths per bar)
# =====================================================================================================================
BPM = 96.0
EIGHTH = 60.0 / BPM / 2.0
BAR = EIGHTH * 6

CHORDS = {
    "G": [43, 50, 55, 59, 62], "Em": [40, 47, 52, 55, 59], "C": [48, 55, 60, 64, 67], "D": [50, 57, 62, 66, 69],
    "Am": [45, 52, 57, 60, 64], "D7": [50, 57, 60, 66, 69], "A7": [45, 52, 55, 61, 64], "GB": [47, 50, 55, 59, 62],
    "B7": [47, 54, 57, 63, 66], "Am7": [45, 52, 55, 60, 64],
}
N = {"G4": 67, "A4": 69, "B4": 71, "C5": 72, "C#5": 73, "D5": 74, "E5": 76, "F#5": 78, "G5": 79, "A5": 81, "F#4": 66}

A_CHORDS = ["G", "Em", "C", "D", "G", "Em", ("Am", "D7"), "G"]
B_CHORDS = ["C", "G", "A7", "D", "C", "GB", ("Am7", "D7"), "G"]
# (note, start eighth within bar, duration in eighths) per bar
A_MEL = [
    [("D5", 0, 2), ("B4", 2, 2), ("G4", 4, 2)],
    [("E5", 0, 3), ("D5", 3, 1), ("B4", 4, 2)],
    [("C5", 0, 2), ("E5", 2, 2), ("G5", 4, 2)],
    [("F#5", 0, 4), ("E5", 4, 1), ("D5", 5, 1)],
    [("D5", 0, 2), ("B4", 2, 2), ("D5", 4, 2)],
    [("G5", 0, 3), ("F#5", 3, 1), ("E5", 4, 2)],
    [("C5", 0, 2), ("E5", 2, 1), ("D5", 3, 1), ("A4", 4, 2)],
    [("G4", 0, 6)],
]
B_MEL = [
    [("E5", 0, 2), ("G5", 2, 2), ("E5", 4, 2)],
    [("D5", 0, 3), ("B4", 3, 1), ("G4", 4, 2)],
    [("C#5", 0, 2), ("E5", 2, 2), ("G5", 4, 2)],
    [("F#5", 0, 6)],
    [("G5", 0, 2), ("E5", 2, 2), ("C5", 4, 2)],
    [("D5", 0, 3), ("B4", 3, 1), ("D5", 4, 2)],
    [("C5", 0, 2), ("A4", 2, 1), ("B4", 3, 1), ("C5", 4, 1), ("A4", 5, 1)],
    [("G4", 0, 4), ("B4", 4, 1), ("D5", 5, 1)],
]


def master(x):
    """Warm, soft top end: gentle low-mid lift, high shelf down, low-pass."""
    x = peak(x, 240, 0.8, 2.0)
    x = peak(x, 4500, 0.7, -4.0)
    return lp(x, 7000, 2)


class Track:
    def __init__(self, dur):
        self.n = n_of(dur)
        self.L = np.zeros(self.n)
        self.R = np.zeros(self.n)

    def add(self, x, t, pan=0.0, gain=1.0):
        i = n_of(t)
        if i >= self.n:
            return
        seg = x[: self.n - i] * gain
        l = np.cos((pan + 1) * np.pi / 4)
        r = np.sin((pan + 1) * np.pi / 4)
        self.L[i:i + len(seg)] += seg * l
        self.R[i:i + len(seg)] += seg * r


def bar_chords(entry):
    return [entry] if isinstance(entry, str) else list(entry)


def guitar_bar(tr, t0, entry, vel=0.7, pattern="arp"):
    chords = bar_chords(entry)
    for half, ch in enumerate(chords):
        v = CHORDS[ch]
        span = 6 // len(chords)
        steps = [0, 2, 3, 4, 3, 2][:span] if len(chords) == 1 else [0, 3, 4][:span]
        for k, idx in enumerate(steps):
            t = t0 + (half * span + k) * EIGHTH + RNG.uniform(-0.006, 0.006)
            f = midi_hz(v[idx])
            vv = vel * (1.0 if k == 0 else 0.72) * RNG.uniform(0.9, 1.05)
            tr.add(nylon(f, 2.2 if k == 0 else 1.4, vv), t, pan=-0.25 + 0.08 * idx, gain=0.9)


def bass_bar(tr, t0, entry, vel=0.7):
    chords = bar_chords(entry)
    for half, ch in enumerate(chords):
        v = CHORDS[ch]
        root = v[0] - 12 if v[0] >= 45 else v[0]
        tr.add(pizz_bass(midi_hz(root), 1.3, vel), t0 + half * 3 * EIGHTH, pan=0.0, gain=1.0)
        if len(chords) == 1:
            tr.add(pizz_bass(midi_hz(root + 7), 0.9, vel * 0.55), t0 + 4 * EIGHTH, pan=0.0, gain=0.8)


def melody_bar(tr, t0, notes, vel=0.75, octave=0, tremolo_from=3):
    for name, st, du in notes:
        f = midi_hz(N[name] + 12 * octave)
        t = t0 + st * EIGHTH + RNG.uniform(-0.004, 0.008)
        dur = du * EIGHTH
        if du >= tremolo_from:
            x = mandolin_tremolo(f, dur + 0.05, vel)
        else:
            x = mandolin_note(f, dur + 0.6, vel)
        tr.add(x, t, pan=0.22, gain=1.0)


def accordion_bar(tr, t0, entry, vel=0.45):
    chords = bar_chords(entry)
    span = BAR / len(chords)
    for half, ch in enumerate(chords):
        v = CHORDS[ch]
        freqs = [midi_hz(m) for m in v[2:5]]
        tr.add(accordion(freqs, span + 0.25, vel), t0 + half * span, pan=-0.35, gain=1.0)


def perc_bar(tr, t0, vel=0.5, shakers=True):
    tr.add(tambourine(vel * 0.55), t0 + 2 * EIGHTH, pan=0.45)
    tr.add(tambourine(vel * 0.45), t0 + 4 * EIGHTH, pan=0.45)
    if shakers:
        for k in range(6):
            tr.add(shaker(vel * (0.35 if k % 2 else 0.5)), t0 + k * EIGHTH + 0.01, pan=0.55)


def make_theme():
    sections = (
        [("intro", "A", i) for i in range(4)]
        + [("A1", "A", i) for i in range(8)]
        + [("A2", "A", i) for i in range(8)]
        + [("B", "B", i) for i in range(8)]
        + [("A3", "A", i) for i in range(8)]
    )
    total = len(sections) * BAR
    tr = Track(total + 3.0)
    for bi, (sec, kind, i) in enumerate(sections):
        t0 = bi * BAR
        chords = A_CHORDS if kind == "A" else B_CHORDS
        entry = chords[i]
        guitar_bar(tr, t0, entry, 0.62 if sec == "intro" else 0.7)
        if sec != "intro":
            bass_bar(tr, t0, entry, 0.65)
        if sec in ("A1", "A3"):
            melody_bar(tr, t0, A_MEL[i], 0.72 if sec == "A1" else 0.8)
        if sec == "A2":
            # second time: melody up an octave, lighter, glock doubles the downbeats
            melody_bar(tr, t0, A_MEL[i], 0.55, octave=0)
            note = A_MEL[i][0]
            tr.add(bell(midi_hz(N[note[0]] + 12), 2.0, 0.18, 1.1), t0, pan=0.4)
        if sec == "B":
            melody_bar(tr, t0, B_MEL[i], 0.78)
            accordion_bar(tr, t0, entry, 0.5)
        if sec == "A3":
            accordion_bar(tr, t0, entry, 0.32)
        if sec in ("A2", "B", "A3"):
            perc_bar(tr, t0, 0.5, shakers=sec != "A2")
        elif sec == "A1" and i >= 4:
            perc_bar(tr, t0, 0.35, shakers=False)
    L, R = reverb_stereo(tr.L, tr.R, size=2.2, mix=0.2, damp=3800)
    L, R = master(L), master(R)
    # Loop: fold the ringing tail back into the start.
    n = n_of(total)
    tail = n_of(2.5)
    L2, R2 = L[:n].copy(), R[:n].copy()
    L2[:tail] += L[n:n + tail] * np.linspace(1, 0, tail)
    R2[:tail] += R[n:n + tail] * np.linspace(1, 0, tail)
    write("MUS_Theme", L2, R2, 0.8)


def make_reunion():
    """Tender reprise: melody on tremolo mandolin, held accordion, ends on a ringing G with bells."""
    slow = 1.18
    global EIGHTH, BAR
    e0, b0 = EIGHTH, BAR
    EIGHTH, BAR = EIGHTH * slow, BAR * slow
    bars = 8
    tr = Track(bars * BAR + 6.0)
    for i in range(bars):
        t0 = i * BAR
        entry = A_CHORDS[i]
        guitar_bar(tr, t0, entry, 0.55)
        bass_bar(tr, t0, entry, 0.5)
        accordion_bar(tr, t0, entry, 0.3)
        melody_bar(tr, t0, A_MEL[i], 0.7, tremolo_from=2)
    end = bars * BAR
    for k, m in enumerate([67, 71, 74, 79, 83]):
        tr.add(bell(midi_hz(m + 12), 4.0, 0.22, 1.8), end - BAR + k * 0.12, pan=-0.3 + 0.15 * k)
    for m in CHORDS["G"]:
        tr.add(nylon(midi_hz(m), 4.5, 0.6), end + 0.02 * (m - 43))
    tr.add(accordion([midi_hz(m) for m in (59, 62, 67)], 4.0, 0.3), end, pan=-0.3)
    EIGHTH, BAR = e0, b0
    L, R = reverb_stereo(tr.L, tr.R, size=2.6, mix=0.26, damp=3800)
    L, R = master(L), master(R)
    write("MUS_Reunion", fade(L, 0.01, 1.5), fade(R, 0.01, 1.5), 0.8)


# =====================================================================================================================
# Ambience
# =====================================================================================================================
def make_sea():
    dur = 48.0
    t = t_axis(dur)
    out = []
    for ch in range(2):
        base = lp(brown(dur), 500) * 0.6 + lp(pink(dur), 1800) * 0.4
        # Wave cycles: swell then a soft break with hiss.
        env = np.zeros(len(t))
        hiss = np.zeros(len(t))
        tt = RNG.uniform(0, 2)
        while tt < dur:
            period = RNG.uniform(5.5, 8.5)
            c = np.exp(-((t - tt - period * 0.45) / (period * 0.28)) ** 2)
            env += c
            b = np.exp(-((t - tt - period * 0.55) / (period * 0.12)) ** 2)
            hiss += b * RNG.uniform(0.6, 1.0)
            tt += period
        wash = bp(noise(dur), 1500, 7000) * hiss * 0.35
        x = base * (0.35 + 0.65 * env) + wash
        out.append(x)
    L, R = seamless(out[0], 2.0), seamless(out[1], 2.0)
    write("AMB_Sea_Loop", L, R, 0.6)


def make_village():
    dur = 40.0
    t = t_axis(dur)
    chans = [np.zeros(len(t)), np.zeros(len(t))]
    # Cicadas: buzzing bands pulsing at insect rates, slowly swelling in and out.
    for k in range(4):
        f0 = RNG.uniform(4200, 6800)
        band = bp(noise(dur), f0 * 0.85, f0 * 1.15, 2)
        rate = RNG.uniform(38, 70)
        am = (0.5 + 0.5 * np.sin(2 * np.pi * rate * t + RNG.uniform(0, 6))) ** 3
        swell = 0.35 + 0.65 * np.clip(np.sin(2 * np.pi * t / RNG.uniform(9, 17) + RNG.uniform(0, 6)), 0, 1) ** 2
        x = band * am * swell * 0.28
        pan = RNG.uniform(-0.8, 0.8)
        chans[0] += x * np.cos((pan + 1) * np.pi / 4)
        chans[1] += x * np.sin((pan + 1) * np.pi / 4)
    # Breeze through the lanes.
    for c in range(2):
        breeze = lp(pink(dur), 700) * (0.5 + 0.5 * np.sin(2 * np.pi * t / 11.0 + c)) * 0.35
        chans[c] += breeze
    # Distant wind chimes now and then.
    for _ in range(5):
        tt = RNG.uniform(2, dur - 6)
        for j in range(RNG.integers(3, 6)):
            f = midi_hz(int(RNG.choice([79, 83, 86, 88, 91, 95])))
            x = bell(f, 3.0, 0.05, 1.2)
            i = n_of(tt + j * RNG.uniform(0.1, 0.35))
            pan = RNG.uniform(-0.5, 0.5)
            for c in range(2):
                seg = x[: len(t) - i] * (np.cos((pan + 1) * np.pi / 4) if c == 0 else np.sin((pan + 1) * np.pi / 4))
                chans[c][i:i + len(seg)] += seg
    L, R = reverb_stereo(chans[0], chans[1], size=1.8, mix=0.3)
    write("AMB_Village_Loop", seamless(L, 2.0), seamless(R, 2.0), 0.55)


def make_gulls():
    for k in range(4):
        calls = RNG.integers(2, 5)
        dur = 0.32 * calls + 1.2
        out = np.zeros(n_of(dur))
        for c in range(calls):
            d = RNG.uniform(0.22, 0.34)
            n = n_of(d)
            tt = np.arange(n) / SR
            f0 = RNG.uniform(1300, 1700)
            f = f0 * (1.0 + 0.35 * np.exp(-tt / 0.03)) * (1 - 0.25 * tt / d)
            ph = 2 * np.pi * np.cumsum(f) / SR
            tone = np.sin(ph) + 0.6 * np.sin(2 * ph) + 0.35 * np.sin(3 * ph) + 0.2 * np.sin(4 * ph)
            rasp = bp(noise(d), 1500, 4500) * 0.45
            x = (tone * (1 + 0.6 * np.sin(2 * np.pi * 85 * tt)) + rasp) * env_ar(n, 0.015, 0.09)
            x = peak(x, 2400, 1.5, 6)
            i = n_of(c * RNG.uniform(0.26, 0.36))
            out[i:i + n] += x[: len(out) - i]
        out = lp(out, 5000)
        write(f"SFX_Gull_{k + 1:02d}", reverb(out, 1.6, 0.35), peak_level=0.7)


def make_bell():
    dur = 7.0
    n = n_of(dur)
    t = np.arange(n) / SR
    f = 196.0  # G3 strike tone, a village campanile
    y = np.zeros(n)
    for ratio, amp, dk in ((0.5, 0.6, 2.6), (1.0, 1.0, 1.8), (1.19, 0.55, 1.3), (1.5, 0.4, 1.1), (2.0, 0.65, 0.9),
                           (2.52, 0.3, 0.6), (3.01, 0.25, 0.45), (4.07, 0.15, 0.3)):
        beat = 1 + 0.03 * np.sin(2 * np.pi * RNG.uniform(0.6, 1.4) * t)
        y += amp * np.sin(2 * np.pi * f * ratio * t) * np.exp(-t / dk) * beat
    strike = bp(noise(0.05), 800, 4000) * env_exp(n_of(0.05), 0.01)
    y[: len(strike)] += strike * 0.4
    write("SFX_BellToll", reverb(y * env_ar(n, 0.001, 0.4), 3.0, 0.4, 4000, 0.05), peak_level=0.8)


# =====================================================================================================================
# SFX
# =====================================================================================================================
def make_sfx():
    # Soft paws on stone: tiny pads, a little grit.
    for k in range(6):
        d = 0.09
        n = n_of(d)
        thump = lp(noise(d), RNG.uniform(500, 800)) * env_exp(n, 0.012)
        grit = bp(noise(d), 2500, 6000) * env_exp(n, 0.006) * RNG.uniform(0.08, 0.16)
        write(f"SFX_Paw_{k + 1:02d}", fade(thump + grit, 0.001, 0.02), peak_level=0.6)

    d = 0.35
    whoosh = bp(noise(d), 300, 2500) * np.sin(np.linspace(0, np.pi, n_of(d))) ** 2
    write("SFX_Jump", whoosh, peak_level=0.5)
    d = 0.25
    land = lp(noise(d), 380) * env_exp(n_of(d), 0.03) + bp(noise(d), 1800, 5000) * env_exp(n_of(d), 0.01) * 0.15
    write("SFX_Land", fade(land), peak_level=0.65)

    # Sizzle loop: hiss band + crackles.
    d = 3.0
    hiss = bp(noise(d), 3000, 9000) * (0.6 + 0.4 * np.abs(lp(noise(d), 6)))
    crack = np.zeros(n_of(d))
    for _ in range(140):
        i = RNG.integers(0, n_of(d) - 400)
        crack[i:i + 400] += hp(RNG.standard_normal(400), 2500) * env_exp(400, 0.0015) * RNG.uniform(0.4, 1.6)
    write("SFX_Sizzle_Loop", seamless(hiss * 0.45 + crack, 0.3), peak_level=0.7)

    # Evaporate: sizzle burst + airy rising "pfff" + low puff.
    d = 1.2
    n = n_of(d)
    t = np.arange(n) / SR
    air = bp(noise(d), 600, 5000) * np.exp(-t / 0.35) * (1 - np.exp(-t / 0.03))
    rise = np.sin(2 * np.pi * np.cumsum(np.geomspace(300, 1400, n)) / SR) * np.exp(-t / 0.25) * 0.15
    puff = lp(noise(d), 200) * np.exp(-t / 0.08) * 0.8
    write("SFX_Evaporate", reverb(air + rise + puff, 1.2, 0.3), peak_level=0.8)

    # Re-form: reversed swell + soft shimmer.
    d = 1.0
    n = n_of(d)
    swell = (bp(noise(d), 400, 3000) * np.linspace(0, 1, n) ** 3)
    shimmer = sum(bell(midi_hz(m), d, 0.12, 0.5) for m in (79, 83, 86))
    write("SFX_Reform", reverb(swell * 0.6 + shimmer, 1.4, 0.35), peak_level=0.6)

    # Wood + cart.
    def knock(f, d=0.18, bright=0.5):
        n = n_of(d)
        t = np.arange(n) / SR
        body = np.sin(2 * np.pi * f * t) * np.exp(-t / 0.03) + 0.5 * np.sin(2 * np.pi * f * 2.3 * t) * np.exp(-t / 0.015)
        click = bp(noise(d), 1500, 6000) * np.exp(-t / 0.004) * bright
        return body + click

    write("SFX_Grab", fade(knock(210) * 0.7 + lp(noise(0.18), 300) * env_exp(n_of(0.18), 0.02)), peak_level=0.55)
    write("SFX_Release", fade(knock(260, bright=0.3)), peak_level=0.45)
    write("SFX_CartBump", fade(reverb(knock(120, 0.4, 0.7) + knock(180, 0.4, 0.4) * 0.6, 0.8, 0.2)), peak_level=0.8)
    d = 2.4
    n = n_of(d)
    t = np.arange(n) / SR
    rumble = lp(brown(d), 160) * 0.8
    bumps = np.zeros(n)
    for i in range(0, n - 4000, n_of(0.11)):
        j = i + RNG.integers(0, 800)
        bumps[j:j + 3000] += knock(RNG.uniform(90, 140), 3000 / SR, 0.2)[:3000] * RNG.uniform(0.3, 0.7)
    creak = bp(noise(d), 700, 1400) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.9 * t)) ** 4 * 0.15
    write("SFX_CartRoll_Loop", seamless(rumble + bumps + creak, 0.2), peak_level=0.7)
    for k in range(3):
        d = 0.6
        x = np.zeros(n_of(d))
        for j in range(RNG.integers(3, 6)):
            i = n_of(RNG.uniform(0, 0.35))
            kk = knock(RNG.uniform(260, 420), 0.15, 0.15) * RNG.uniform(0.3, 0.8)
            x[i:i + len(kk)] += kk[: len(x) - i]
        write(f"SFX_LemonRattle_{k + 1:02d}", fade(x), peak_level=0.5)

    # Awning: ratchet click, canvas flap, final whoomp.
    d = 0.08
    n = n_of(d)
    t = np.arange(n) / SR
    click = (np.sin(2 * np.pi * 2900 * t) + 0.6 * np.sin(2 * np.pi * 4700 * t)) * np.exp(-t / 0.006) + bp(noise(d), 3000, 9000) * np.exp(-t / 0.003)
    write("SFX_AwningClick", click, peak_level=0.5)
    d = 0.5
    n = n_of(d)
    t = np.arange(n) / SR
    flap = bp(noise(d), 200, 2200) * (0.5 + 0.5 * np.sin(2 * np.pi * 18 * t)) * np.exp(-t / 0.15)
    write("SFX_AwningFlap", flap, peak_level=0.6)
    d = 1.2
    n = n_of(d)
    t = np.arange(n) / SR
    whoomp = lp(noise(d), 600) * np.exp(-t / 0.12) * (1 - np.exp(-t / 0.01)) + bp(noise(d), 300, 1500) * (0.5 + 0.5 * np.sin(2 * np.pi * 12 * t)) * np.exp(-t / 0.3) * 0.5
    write("SFX_AwningOpen", reverb(whoomp, 1.0, 0.2), peak_level=0.8)

    # Terracotta pot: rocking, whoosh, heavy thud + soil.
    def clay(f, d=0.25):
        n = n_of(d)
        t = np.arange(n) / SR
        return (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(2 * np.pi * f * 2.7 * t)) * np.exp(-t / 0.05) + bp(noise(d), 800, 3000) * np.exp(-t / 0.01) * 0.4

    wob = np.zeros(n_of(0.6))
    for i, tt in enumerate((0.0, 0.18, 0.34)):
        c = clay(320 + 30 * i, 0.2) * (0.6 + 0.2 * i)
        j = n_of(tt)
        wob[j:j + len(c)] += c[: len(wob) - j]
    write("SFX_PotWobble", fade(wob), peak_level=0.55)
    d = 0.6
    write("SFX_PotFall", bp(noise(d), 300, 2000) * np.sin(np.linspace(0, np.pi, n_of(d))) ** 2, peak_level=0.5)
    d = 1.0
    n = n_of(d)
    t = np.arange(n) / SR
    thud = lp(noise(d), 180) * np.exp(-t / 0.09) * 1.2 + clay(140, d) * 0.7
    soil = bp(noise(d), 1200, 6000) * np.exp(-t / 0.12) * 0.35
    write("SFX_PotLand", reverb(thud + soil, 0.9, 0.22), peak_level=0.9)

    # Purr: ~26 Hz pulse train through a throaty filter, breathing in/out.
    d = 4.0
    n = n_of(d)
    t = np.arange(n) / SR
    breath = np.where((t % 2.0) < 1.1, 1.0, 0.75) * (0.6 + 0.4 * np.sin(np.pi * (t % 2.0) / 2.0))
    rate = 25.5 + 1.5 * np.sin(2 * np.pi * 0.5 * t)
    ph = np.cumsum(rate) / SR
    pulses = np.maximum(0, np.sin(2 * np.pi * ph)) ** 6
    rumble = lp(noise(d), 320) * pulses * 2.0 + lp(pulses - np.mean(pulses), 200)
    purr = peak(rumble, 120, 1.0, 6) * breath
    write("SFX_Purr_Loop", seamless(purr, 0.3), peak_level=0.7)

    # Mrrp: rolled little trill, rising.
    d = 0.42
    n = n_of(d)
    t = np.arange(n) / SR
    f = np.geomspace(330, 560, n)
    ph = 2 * np.pi * np.cumsum(f) / SR
    voice = np.sin(ph) + 0.5 * np.sin(2 * ph) + 0.25 * np.sin(3 * ph)
    roll = 0.55 + 0.45 * np.sin(2 * np.pi * 28 * t)
    x = voice * roll * env_ar(n, 0.03, 0.12)
    x = peak(peak(x, 900, 2.0, 6), 1800, 2.0, 4) + bp(noise(d), 1000, 3000) * 0.05
    write("SFX_Mrrp", reverb(x, 0.6, 0.15), peak_level=0.6)

    # UI + stingers.
    write("SFX_UI_Move", fade(knock(900, 0.06, 0.2)), peak_level=0.35)
    write("SFX_UI_Select", fade(mandolin_note(midi_hz(79), 0.8, 0.8) + mandolin_note(midi_hz(86), 0.8, 0.5)), peak_level=0.5)
    d = 0.35
    sw = bp(noise(d), 800, 5000) * np.sin(np.linspace(0, np.pi, n_of(d))) ** 2
    write("SFX_UI_Open", sw, peak_level=0.3)
    write("SFX_UI_Close", sw[::-1].copy(), peak_level=0.3)
    obj = np.zeros(n_of(1.4))
    for i, m in enumerate((74, 79)):
        x = mandolin_note(midi_hz(m), 1.2, 0.8)
        j = n_of(i * 0.14)
        obj[j:j + len(x)] += x[: len(obj) - j]
    write("SFX_Objective", reverb(obj, 1.2, 0.25), peak_level=0.5)
    solve = np.zeros(n_of(2.6))
    for i, m in enumerate((67, 71, 74, 79, 83)):
        x = bell(midi_hz(m + 12), 2.0, 0.5, 1.0) + pad_to(mandolin_note(midi_hz(m), 1.0, 0.35), n_of(2.0))
        j = n_of(i * 0.09)
        solve[j:j + len(x)] += x[: len(solve) - j]
    write("SFX_Solve", reverb(solve, 1.6, 0.3), peak_level=0.6)


if __name__ == "__main__":
    make_sfx()
    make_bell()
    make_gulls()
    make_sea()
    make_village()
    make_theme()
    make_reunion()
    print("audio ok")
