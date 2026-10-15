"""A small self-contained synthesizer so every result can be heard as audio
without needing a DAW or SoundFont. Instruments are modelled additively
(harmonics with their own envelopes), plus synthesized drums and a reverb."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from scipy import signal

from .score import Note, Score, Track

SR = 44100


def _freq_curve(note: Note, n: int, bpm: float, sr: int, vibrato: float = 0.0, vib_rate: float = 5.5) -> np.ndarray:
    t = np.arange(n) / sr
    semis = np.full(n, float(note.pitch))
    if note.bend_from:
        glide = min(0.5 * 60 / bpm, note.duration * 0.4 * 60 / bpm)
        g = np.clip(t / max(glide, 1e-3), 0, 1)
        g = g * g * (3 - 2 * g)  # smoothstep
        semis += note.bend_from * (1 - g)
    if vibrato:
        depth = vibrato * np.clip((t - 0.25) / 0.4, 0, 1)
        semis += depth * np.sin(2 * np.pi * vib_rate * t)
    return 440.0 * 2 ** ((semis - 69) / 12)


def _phase(freq: np.ndarray, sr: int) -> np.ndarray:
    return 2 * np.pi * np.cumsum(freq) / sr


def _adsr(n: int, sr: int, sustain_n: int, a: float, d: float, s: float, r: float) -> np.ndarray:
    env = np.zeros(n)
    a_n, d_n, r_n = max(1, int(a * sr)), max(1, int(d * sr)), max(1, int(r * sr))
    idx = np.arange(n)
    env = np.where(idx < a_n, idx / a_n, 1.0)
    dec = np.clip((idx - a_n) / d_n, 0, 1)
    env = np.where(idx >= a_n, 1 - (1 - s) * dec, env)
    held = env[min(sustain_n, n - 1)] if n else 0
    rel = np.clip((idx - sustain_n) / r_n, 0, 1)
    env = np.where(idx >= sustain_n, held * (1 - rel), env)
    return env


def _additive(phase: np.ndarray, freq: np.ndarray, amps: list[float], decays: list[float] | None, t: np.ndarray, sr: int,
              inharm: float = 0.0) -> np.ndarray:
    out = np.zeros_like(phase)
    fmax = float(freq.max())
    for k, a in enumerate(amps, start=1):
        stretch = k * (1 + inharm * k * k)
        if fmax * stretch >= sr * 0.45 or a == 0:
            continue
        part = a * np.sin(phase * stretch)
        if decays is not None:
            part *= np.exp(-t * decays[k - 1])
        out += part
    return out


def synth_note(instrument: str, note: Note, bpm: float, sr: int = SR) -> np.ndarray:
    hold = note.duration * 60.0 / bpm
    release = {"piano": 0.25, "strings": 0.35, "cello": 0.35, "pad": 0.8, "drone": 0.8, "choir": 0.6,
               "organ": 0.08, "flute": 0.12, "bansuri": 0.15, "brass": 0.15, "lead": 0.1, "supersaw": 0.25}.get(instrument, 0.3)
    pluck = instrument in ("nylon", "guitar", "pluck", "tumbi", "sitar", "oud", "harpsichord", "celesta", "bell",
                           "epiano", "piano", "upright")
    n = int((hold + release + (1.2 if pluck else 0)) * sr)
    n = max(n, 64)
    t = np.arange(n) / sr
    hold_n = int(hold * sr)
    vel = (note.velocity / 127.0) ** 1.4

    vib = {"flute": 0.15, "bansuri": 0.25, "strings": 0.12, "cello": 0.12, "lead": 0.08, "brass": 0.06, "choir": 0.1}.get(instrument, 0.0)
    freq = _freq_curve(note, n, bpm, sr, vibrato=vib)
    ph = _phase(freq, sr)
    f0 = float(freq[0])

    if instrument in ("piano", "upright"):
        amps = [1, 0.55, 0.3, 0.22, 0.12, 0.08, 0.05, 0.03]
        decays = [1.2 + k * 0.9 + f0 / 600 for k in range(len(amps))]
        y = _additive(ph, freq, amps, decays, t, sr, inharm=0.0004)
        y *= _adsr(n, sr, hold_n, 0.003, 0.1, 1.0, release)
        if instrument == "upright":
            y = np.tanh(1.5 * y)
    elif instrument == "epiano":
        mod = 1.2 * np.exp(-t * 5) * np.sin(ph)
        y = np.sin(ph + mod) * np.exp(-t * 1.1) + 0.2 * np.sin(2 * ph) * np.exp(-t * 3)
        y *= _adsr(n, sr, hold_n, 0.004, 0.2, 1.0, 0.25)
    elif instrument in ("celesta", "bell"):
        ratio = 3.5 if instrument == "bell" else 4.0
        idx = (2.5 if instrument == "bell" else 1.2) * np.exp(-t * 3)
        y = np.sin(ph + idx * np.sin(ratio * ph)) * np.exp(-t * (1.0 if instrument == "bell" else 2.0))
        y *= _adsr(n, sr, hold_n + int(0.6 * sr), 0.002, 0.1, 1.0, 0.4)
    elif instrument == "harmonium":  # reed organ heard all over Punjabi & Indian music
        amps = [1, 0.7, 0.55, 0.45, 0.35, 0.28, 0.2, 0.15, 0.1, 0.08]
        y = 0.6 * _additive(ph, freq, amps, None, t, sr) + 0.4 * _additive(ph * 1.004, freq * 1.004, amps, None, t, sr)
        y *= _adsr(n, sr, hold_n, min(0.04, 0.3 * max(hold, 0.02)), 0.1, 0.9, 0.1)
    elif instrument == "organ":
        y = _additive(ph, freq, [1, 0.8, 0.5, 0.4, 0, 0.25, 0, 0.2], None, t, sr)
        y *= 1 + 0.15 * np.sin(2 * np.pi * 6.0 * t)
        y *= _adsr(n, sr, hold_n, 0.01, 0.05, 0.9, release)
    elif instrument in ("nylon", "guitar", "pluck", "tumbi", "sitar", "oud", "harpsichord"):
        bright = {"nylon": 0.55, "guitar": 0.75, "pluck": 0.7, "tumbi": 0.95, "sitar": 0.9, "oud": 0.5, "harpsichord": 0.95}[instrument]
        nh = 14
        amps = [bright ** (k - 1) / k ** 0.6 for k in range(1, nh + 1)]
        base_decay = {"tumbi": 3.0, "harpsichord": 2.5, "oud": 3.0, "pluck": 2.4}.get(instrument, 1.6)
        decays = [base_decay + 0.9 * k for k in range(nh)]
        y = _additive(ph, freq, amps, decays, t, sr, inharm=0.0002 if instrument != "sitar" else 0.001)
        if instrument == "sitar":  # jawari buzz + sympathetic shimmer
            y = np.tanh(2.5 * y) + 0.15 * np.sin(2 * ph * 1.003) * np.exp(-t * 1.2)
        y *= _adsr(n, sr, hold_n + int(0.3 * sr), 0.002, 0.05, 1.0, 0.25)
    elif instrument in ("strings", "cello", "pad", "drone", "choir"):
        nh = 18 if instrument in ("strings", "cello") else 10
        amps = [1 / k ** (1.1 if instrument in ("strings", "cello") else 1.6) for k in range(1, nh + 1)]
        detune = (0.997, 1.0, 1.0035)
        y = sum(_additive(ph * d, freq * d, amps, None, t, sr) for d in detune) / 3
        attack = {"strings": 0.12, "cello": 0.12, "pad": 0.45, "drone": 0.8, "choir": 0.3}[instrument]
        attack = min(attack, 0.35 * max(hold, 0.02))  # short stabs still speak
        y *= _adsr(n, sr, hold_n, attack, 0.3, 0.85, release)
    elif instrument == "brass":
        env = _adsr(n, sr, hold_n, 0.05, 0.2, 0.85, release)
        y = np.zeros(n)
        for k in range(1, 14):
            if f0 * k > sr * 0.45:
                break
            y += (1 / k) * env ** (0.5 + 0.35 * k) * np.sin(k * ph)
        y = np.tanh(1.3 * y)
    elif instrument in ("flute", "bansuri"):
        y = np.sin(ph) + 0.25 * np.sin(2 * ph) + 0.08 * np.sin(3 * ph)
        noise = np.random.default_rng(note.pitch).standard_normal(n)
        b, a = signal.butter(2, [min(0.99, f0 * 1.5 / (sr / 2)), min(0.995, f0 * 4 / (sr / 2))], btype="band")
        y += 0.08 * signal.lfilter(b, a, noise)
        y *= _adsr(n, sr, hold_n, 0.06 if instrument == "flute" else 0.09, 0.1, 0.9, release)
    elif instrument in ("lead", "supersaw"):
        if instrument == "lead":
            amps = [1 / k if k % 2 else 0.15 / k for k in range(1, 16)]
            y = _additive(ph, freq, amps, None, t, sr) + 0.5 * _additive(ph * 1.006, freq * 1.006, amps, None, t, sr)
        else:
            amps = [1 / k for k in range(1, 16)]
            y = sum(_additive(ph * d, freq * d, amps, None, t, sr) for d in (0.988, 0.995, 1.0, 1.005, 1.012)) / 2.5
        y *= _adsr(n, sr, hold_n, 0.006, 0.15, 0.75, release)
    elif instrument == "sub808":
        drop = 440.0 * 2 ** ((note.pitch - 69) / 12) * (1 + 1.5 * np.exp(-t * 40))
        y = np.tanh(2.0 * np.sin(_phase(drop * freq / freq[0], sr)))
        y *= _adsr(n, sr, hold_n, 0.002, 0.4, 0.8, 0.12)
    else:  # bass and fallback
        amps = [1, 0.45, 0.2, 0.1]
        decays = [0.8, 1.5, 2.5, 3.5]
        y = _additive(ph, freq, amps, decays, t, sr)
        y *= _adsr(n, sr, hold_n, 0.004, 0.2, 0.9, 0.08)
    return vel * y.astype(np.float64)


# --------------------------------------------------------------------------
# Drums
# --------------------------------------------------------------------------

def _noise(n: int, seed: int) -> np.ndarray:
    return np.random.default_rng(seed).standard_normal(n)


def _band(x: np.ndarray, lo: float, hi: float, sr: int) -> np.ndarray:
    b, a = signal.butter(2, [lo / (sr / 2), min(0.99, hi / (sr / 2))], btype="band")
    return signal.lfilter(b, a, x)


def _hp(x: np.ndarray, f: float, sr: int) -> np.ndarray:
    b, a = signal.butter(2, f / (sr / 2), btype="high")
    return signal.lfilter(b, a, x)


def _sweep(f_start: float, f_end: float, rate: float, t: np.ndarray, sr: int) -> np.ndarray:
    f = f_end + (f_start - f_end) * np.exp(-t * rate)
    return np.sin(_phase(f, sr))


_DRUM_CACHE: dict[tuple[str, int], np.ndarray] = {}


def synth_drum(name: str, sr: int = SR) -> np.ndarray:
    if (name, sr) in _DRUM_CACHE:
        return _DRUM_CACHE[(name, sr)]
    dur = {"taiko": 1.2, "openhat": 0.4, "tabla_dha": 0.7, "tabla_na": 0.5, "kick": 0.5}.get(name, 0.35)
    n = int(dur * sr)
    t = np.arange(n) / sr
    nz = _noise(n, hash(name) % 1000)
    if name == "kick":
        y = _sweep(160, 45, 30, t, sr) * np.exp(-t * 7) + 0.3 * _hp(nz, 3000, sr) * np.exp(-t * 200)
    elif name == "snare":
        y = 0.5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t * 25) + 0.8 * _band(nz, 1500, 9000, sr) * np.exp(-t * 18)
    elif name == "clap":
        env = sum(np.exp(-np.clip(t - d, 0, None) * 120) * (t >= d) for d in (0, 0.011, 0.023)) + 0.5 * np.exp(-t * 14)
        y = _band(nz, 900, 3500, sr) * env
    elif name in ("hat", "openhat"):
        y = _hp(nz, 7000, sr) * np.exp(-t * (40 if name == "hat" else 7)) * 0.6
    elif name == "shaker":
        env = np.clip(t / 0.015, 0, 1) * np.exp(-t * 35)
        y = _hp(nz, 5000, sr) * env * 0.5
    elif name == "rim":
        y = np.sin(2 * np.pi * 1700 * t) * np.exp(-t * 90) + 0.4 * _band(nz, 2000, 6000, sr) * np.exp(-t * 120)
    elif name == "tom":
        y = _sweep(140, 95, 12, t, sr) * np.exp(-t * 9)
    elif name == "taiko":
        y = 1.2 * _sweep(90, 48, 8, t, sr) * np.exp(-t * 3.5) + 0.4 * _band(nz, 80, 600, sr) * np.exp(-t * 20)
    elif name == "dhol_low":
        y = _sweep(110, 62, 18, t, sr) * np.exp(-t * 8) + 0.3 * _band(nz, 200, 1200, sr) * np.exp(-t * 40)
    elif name == "dhol_high":
        y = 0.7 * np.sin(2 * np.pi * 380 * t) * np.exp(-t * 30) + 0.6 * _band(nz, 1500, 7000, sr) * np.exp(-t * 60)
    elif name == "tabla_dha":
        bayan = np.sin(_phase(80 + 35 * (1 - np.exp(-t * 6)), sr)) * np.exp(-t * 5)
        dayan = sum(a * np.sin(2 * np.pi * 560 * r * t) for a, r in ((1, 1), (0.5, 2), (0.3, 3))) * np.exp(-t * 6)
        y = 0.8 * bayan + 0.5 * dayan
    elif name == "tabla_na":
        y = sum(a * np.sin(2 * np.pi * 620 * r * t) for a, r in ((1, 1), (0.6, 2), (0.35, 3), (0.2, 4))) * np.exp(-t * 9) * 0.6
        y += 0.2 * _band(nz, 2000, 6000, sr) * np.exp(-t * 150)
    elif name == "cajon_low":
        y = _sweep(130, 80, 25, t, sr) * np.exp(-t * 12) + 0.25 * _band(nz, 300, 2000, sr) * np.exp(-t * 35)
    elif name == "cajon_high":
        y = 0.4 * np.sin(2 * np.pi * 260 * t) * np.exp(-t * 30) + 0.7 * _band(nz, 1800, 8000, sr) * np.exp(-t * 28)
    elif name == "darbuka_dum":
        y = _sweep(140, 100, 20, t, sr) * np.exp(-t * 7)
    elif name == "darbuka_tek":
        y = 0.6 * np.sin(2 * np.pi * 850 * t) * np.exp(-t * 60) + 0.5 * _band(nz, 2500, 9000, sr) * np.exp(-t * 80)
    else:
        y = _band(nz, 500, 5000, sr) * np.exp(-t * 30)
    y = y / (np.max(np.abs(y)) + 1e-9)
    _DRUM_CACHE[(name, sr)] = y
    return y


# --------------------------------------------------------------------------
# Mixing
# --------------------------------------------------------------------------

TRACK_GAIN = {"Melody": 0.9, "Chords": 0.45, "Bass": 0.7, "Drums": 0.6}
DRUM_PAN = {"hat": 0.3, "openhat": 0.3, "shaker": -0.35, "tabla_na": 0.25, "dhol_high": 0.2, "cajon_high": 0.2,
            "darbuka_tek": 0.25, "clap": -0.1, "rim": -0.2}


def _impulse_response(sr: int, seconds: float = 1.8) -> np.ndarray:
    n = int(seconds * sr)
    t = np.arange(n) / sr
    rng = np.random.default_rng(7)
    ir = rng.standard_normal((n, 2)) * np.exp(-t * 3.2)[:, None]
    b, a = signal.butter(1, 6000 / (sr / 2))
    ir = signal.lfilter(b, a, ir, axis=0)
    return ir / np.sqrt(np.sum(ir ** 2, axis=0, keepdims=True))


def _pan(mono: np.ndarray, pan: float) -> np.ndarray:
    angle = (pan + 1) * np.pi / 4
    return np.stack([mono * np.cos(angle), mono * np.sin(angle)], axis=1)


def render_track(track: Track, bpm: float, total_n: int, sr: int = SR) -> np.ndarray:
    out = np.zeros((total_n, 2))
    if track.is_drums:
        for beat, name, vel in track.drum_hits:
            hit = synth_drum(name, sr) * (vel / 127.0)
            s = int(beat * 60 / bpm * sr)
            if s < 0 or s >= total_n:
                continue
            e = min(total_n, s + len(hit))
            out[s:e] += _pan(hit[: e - s], DRUM_PAN.get(name, 0.0))
        return out
    spread = track.name == "Chords"
    for i, note in enumerate(track.notes):
        y = synth_note(track.instrument, note, bpm, sr)
        s = int(note.start * 60 / bpm * sr)
        if s < 0:
            y, s = y[-s:], 0
        if s >= total_n or len(y) == 0:
            continue
        e = min(total_n, s + len(y))
        pan = ((note.pitch % 7) / 3 - 1) * 0.35 if spread else 0.0
        out[s:e] += _pan(y[: e - s], pan)
    return out


def render(score: Score, sr: int = SR, tracks: list[str] | None = None, reverb: float = 0.18,
           tail: float = 2.0) -> np.ndarray:
    """Render `score` to a stereo float array (n, 2) in [-1, 1]."""
    total_n = int((score.seconds(score.length_beats) + tail) * sr)
    dry = np.zeros((total_n, 2))
    wet_send = np.zeros((total_n, 2))
    for tr in score.tracks:
        if tracks is not None and tr.name not in tracks:
            continue
        audio = render_track(tr, score.bpm, total_n, sr)
        peak = np.max(np.abs(audio)) + 1e-9
        audio = audio / peak * TRACK_GAIN.get(tr.name, 0.6)
        dry += audio
        wet_send += audio * (0.3 if tr.name in ("Bass", "Drums") else 1.0)
    if reverb > 0 and np.any(wet_send):
        ir = _impulse_response(sr)
        wet = np.stack([signal.fftconvolve(wet_send[:, c], ir[:, c])[:total_n] for c in range(2)], axis=1)
        dry += reverb * wet
    return master(dry)


def master(audio: np.ndarray, ceiling: float = 0.89) -> np.ndarray:
    peak = np.max(np.abs(audio)) + 1e-9
    audio = audio / peak
    audio = np.tanh(1.4 * audio) / np.tanh(1.4)
    return audio * ceiling


def write_wav(path: str | Path, audio: np.ndarray, sr: int = SR) -> Path:
    import soundfile as sf

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), audio, sr, subtype="PCM_16")
    return path
