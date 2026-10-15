"""Listen to a track: tempo, key, chords and melody transcription."""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .progressions import find_famous, roman, style_affinity
from .score import ChordEvent, Note, Score, Track
from .theory import CHORD_QUALITIES, Chord, Key, midi_to_name

SR = 22050
HOP = 512

# Krumhansl-Kessler key profiles
_MAJOR_PROFILE = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MINOR_PROFILE = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])

CHORD_VOCAB = ("maj", "min", "7", "m7", "dim", "sus4")


@dataclass
class TimedChord:
    start: float  # seconds
    end: float
    chord: Chord | None
    confidence: float


@dataclass
class TimedNote:
    start: float  # seconds
    end: float
    pitch: int
    velocity: int = 90


@dataclass
class Analysis:
    path: str
    duration: float
    bpm: float
    beat_times: list[float]
    key: Key
    key_confidence: float
    key_candidates: list[tuple[str, float]]
    chords: list[TimedChord] = field(default_factory=list)
    melody: list[TimedNote] = field(default_factory=list)

    @property
    def romans(self) -> list[str]:
        return [roman(c.chord, self.key) for c in self.chords if c.chord is not None]

    def to_dict(self) -> dict:
        flats = self.key.prefers_flats
        return {
            "file": self.path,
            "duration_seconds": round(self.duration, 2),
            "bpm": round(self.bpm, 1),
            "key": self.key.name(),
            "key_confidence": round(self.key_confidence, 3),
            "key_candidates": [{"key": k, "score": round(s, 3)} for k, s in self.key_candidates],
            "chords": [
                {"start": round(c.start, 3), "end": round(c.end, 3),
                 "chord": c.chord.name(flats) if c.chord else "N",
                 "roman": roman(c.chord, self.key) if c.chord else "N",
                 "confidence": round(c.confidence, 3)}
                for c in self.chords
            ],
            "famous_progressions": [{"name": n, "roman": r, "heard_in": w} for n, r, w in find_famous(self.romans)],
            "style_affinity": [{"style": s, "score": v} for s, v in style_affinity(self.romans)[:5]],
            "melody": [
                {"start": round(n.start, 3), "end": round(n.end, 3), "pitch": n.pitch,
                 "note": midi_to_name(n.pitch, flats), "velocity": n.velocity}
                for n in self.melody
            ],
        }


# --------------------------------------------------------------------------
# Loading & separation
# --------------------------------------------------------------------------

def load_audio(path: str | Path, sr: int = SR, offset: float = 0.0, duration: float | None = None) -> np.ndarray:
    import librosa

    y, _ = librosa.load(str(path), sr=sr, mono=True, offset=offset, duration=duration)
    if y.size == 0:
        raise ValueError(f"No audio decoded from {path}")
    return y


def demucs_available() -> bool:
    return importlib.util.find_spec("demucs") is not None


def separate_vocals(path: str | Path, workdir: str | Path | None = None) -> Path:
    """Isolate the vocal / lead stem with Demucs (optional dependency: `pip install demucs`)."""
    if not demucs_available():
        raise RuntimeError("Source separation needs Demucs: pip install demucs")
    out = Path(workdir or tempfile.mkdtemp(prefix="melodyverse_"))
    subprocess.run(["python", "-m", "demucs", "--two-stems", "vocals", "-o", str(out), str(path)], check=True)
    hits = list(out.rglob("vocals.wav"))
    if not hits:
        raise RuntimeError("Demucs finished but produced no vocals.wav")
    return hits[0]


# --------------------------------------------------------------------------
# Tempo & key
# --------------------------------------------------------------------------

def estimate_tempo(y: np.ndarray, sr: int = SR) -> tuple[float, np.ndarray]:
    import librosa

    tempo, beats = librosa.beat.beat_track(y=y, sr=sr, hop_length=HOP, units="time")
    bpm = float(np.atleast_1d(tempo)[0]) if np.size(tempo) else 120.0
    if not bpm or not np.isfinite(bpm):
        bpm = 120.0
    # fold into a musical range
    while bpm < 60:
        bpm *= 2
    while bpm > 180:
        bpm /= 2
    if len(beats) < 2:
        beats = np.arange(0, len(y) / sr, 60.0 / bpm)
    return bpm, np.asarray(beats)


def harmonic_chroma(y: np.ndarray, sr: int = SR, bass_weight: float = 0.3) -> np.ndarray:
    """Chroma for harmony analysis. Treble (C3 and up) carries the chord quality;
    the bass register is mixed in lightly so a loud bass line can't swamp the thirds."""
    treble, bass = chroma_parts(y, sr)
    return treble + bass_weight * bass


def chroma_parts(y: np.ndarray, sr: int = SR) -> tuple[np.ndarray, np.ndarray]:
    """(treble chroma from C3 up, bass chroma C1-C3) of the harmonic component."""
    import librosa

    y_h = librosa.effects.harmonic(y, margin=4)
    treble = librosa.feature.chroma_cqt(y=y_h, sr=sr, hop_length=HOP, bins_per_octave=36,
                                        fmin=librosa.note_to_hz("C3"), n_octaves=5)
    bass = librosa.feature.chroma_cqt(y=y_h, sr=sr, hop_length=HOP, bins_per_octave=36,
                                      fmin=librosa.note_to_hz("C1"), n_octaves=2)
    n = min(treble.shape[1], bass.shape[1])
    return treble[:, :n], bass[:, :n]


def estimate_key(chroma: np.ndarray) -> tuple[Key, float, list[tuple[str, float]]]:
    profile = chroma.sum(axis=1)
    if not np.any(profile):
        return Key(0, "major"), 0.0, []
    profile = profile / profile.max()
    scores = []
    for tonic in range(12):
        for mode, ref in (("major", _MAJOR_PROFILE), ("minor", _MINOR_PROFILE)):
            r = np.corrcoef(profile, np.roll(ref, tonic))[0, 1]
            scores.append((r, Key(tonic, mode)))
    scores.sort(key=lambda s: -s[0])
    best_r, best = scores[0]
    second = scores[1][0]
    confidence = float(max(0.0, best_r) * min(1.0, 0.5 + (best_r - second) * 5))
    return best, confidence, [(k.name(), float(r)) for r, k in scores[:5]]


KEY_MODES = ("major", "minor", "mixolydian", "dorian", "harmonic_minor", "phrygian_dominant", "lydian", "phrygian")


def resolve_key(chroma: np.ndarray, chords: list["TimedChord"], fallback: Key) -> tuple[Key, float]:
    """Pick tonic *and* mode by combining the pitch-class profile with harmonic
    evidence (which tonic chord sounds longest, and opens/closes the track).
    Profiles alone can't separate C major / A minor / G mixolydian."""
    from .theory import SCALES

    profile = chroma.sum(axis=1)
    if not np.any(profile) or not chords:
        return fallback, 0.0
    profile = profile / profile.max()
    named = [c for c in chords if c.chord is not None]
    if not named:
        return fallback, 0.0
    cap = 2.0 * float(np.median([c.end - c.start for c in named]))  # trailing reverb/silence shouldn't count

    def dur(c: "TimedChord") -> float:
        return min(c.end - c.start, cap)

    total = sum(dur(c) for c in named) or 1.0
    # cadential arrivals: V->I, bVII->I, bII->I, vii°->I (IV->I is left out: it is indistinguishable from I->V)
    transitions = [(a.chord, b.chord) for a, b in zip(named, named[1:]) if a.chord.root != b.chord.root]
    arrivals: dict[int, int] = {}
    for a, b in transitions:
        if (a.root - b.root) % 12 in (7, 10, 1) or ((a.root - b.root) % 12 == 11 and a.quality in ("dim", "dim7")):
            arrivals[b.root] = arrivals.get(b.root, 0) + 1
    ranked = []
    for tonic in range(12):
        for mode in KEY_MODES:
            scale = SCALES[mode]
            tmpl = np.zeros(12)
            for deg in scale:
                tmpl[(tonic + deg) % 12] = 1.0
            tmpl[tonic] = 1.6
            tmpl[(tonic + 7) % 12] = max(tmpl[(tonic + 7) % 12], 1.25) if 7 in scale else tmpl[(tonic + 7) % 12]
            fit = float(np.corrcoef(profile, tmpl)[0, 1])
            third = 4 if 4 in scale else 3
            tonic_q = {"maj", "7", "5", "sus4", "sus2"} if third == 4 else {"min", "m7", "5", "sus4", "sus2"}
            tdur = sum(dur(c) for c in named if c.chord.root == tonic and c.chord.quality in tonic_q)
            score = fit + 0.6 * tdur / total
            if named[0].chord.root == tonic and named[0].chord.quality in tonic_q:
                score += 0.15  # songs very often open on the tonic
            if named[-1].chord.root == tonic and named[-1].chord.quality in tonic_q:
                score += 0.06  # ...but loops often stop anywhere
            if mode in ("major", "minor"):
                score += 0.08  # exotic modes need clear evidence
            score += 0.3 * arrivals.get(tonic, 0) / max(1, len(transitions))
            ranked.append((score, Key(tonic, mode)))
    ranked.sort(key=lambda r: -r[0])
    best_score, best_key = ranked[0]
    margin = best_score - ranked[1][0]
    return best_key, float(min(1.0, max(0.0, 0.5 * best_score + 2 * margin)))


# --------------------------------------------------------------------------
# Chords
# --------------------------------------------------------------------------

def _templates() -> tuple[np.ndarray, list[Chord | None]]:
    vecs, labels = [], []
    for q in CHORD_VOCAB:
        for root in range(12):
            v = np.zeros(12)
            for k, iv in enumerate(CHORD_QUALITIES[q]):
                v[(root + iv) % 12] = 1.0 if k == 0 else 0.85 if k < 3 else 0.6
            vecs.append(v / np.linalg.norm(v))
            labels.append(Chord(root, q))
    vecs.append(np.ones(12) / np.sqrt(12))
    labels.append(None)
    return np.array(vecs), labels


def detect_chords(y: np.ndarray, sr: int, beat_times: np.ndarray, key: Key | None = None,
                  parts: tuple[np.ndarray, np.ndarray] | None = None, stay: float = 0.85,
                  bass_vote: float = 0.3) -> list[TimedChord]:
    """Beat-synchronous chord recognition: treble chroma is matched against chord
    templates, the bass register votes for the root, and a Viterbi pass smooths changes."""
    import librosa

    treble, bass_c = parts if parts is not None else chroma_parts(y, sr)
    chroma = treble + 0.3 * bass_c
    duration = len(y) / sr
    beat_frames = librosa.time_to_frames(beat_times, sr=sr, hop_length=HOP)
    beat_frames = beat_frames[beat_frames < chroma.shape[1]]
    sync = librosa.util.sync(chroma, beat_frames, aggregate=np.median)
    bass_sync = librosa.util.sync(bass_c, beat_frames, aggregate=np.median)
    bounds = np.concatenate([[0.0], librosa.frames_to_time(beat_frames, sr=sr, hop_length=HOP), [duration]])
    bounds = np.unique(bounds)
    n_seg = min(sync.shape[1], len(bounds) - 1)
    sync = sync[:, :n_seg]
    bass_sync = bass_sync[:, :n_seg]
    bass_sync = bass_sync / (bass_sync.max(axis=0, keepdims=True) + 1e-9)

    templates, labels = _templates()
    energy = sync.sum(axis=0)
    norm = sync / (np.linalg.norm(sync, axis=0, keepdims=True) + 1e-9)
    sim = templates @ norm  # (n_labels, n_seg)
    roots = np.array([c.root if c is not None else 0 for c in labels])
    root_support = bass_sync[roots]  # (n_labels, n_seg): how strongly the bass plays each chord's root
    root_support[-1] = 0.5
    sim = sim + bass_vote * (root_support - 0.5)

    # priors: diatonic chords a little more likely; plain triads preferred over 7ths/sus
    prior = np.ones(len(labels))
    for i, c in enumerate(labels):
        if c is None:
            prior[i] = 0.6
            continue
        if c.quality in ("7", "m7"):
            prior[i] *= 0.93
        elif c.quality == "sus4":
            prior[i] *= 0.9
        elif c.quality == "dim":
            prior[i] *= 0.86  # dim templates otherwise soak up noisy frames
        if key is not None and set(c.pitch_classes) <= key.pitch_classes:
            prior[i] *= 1.06
    sim = sim * prior[:, None]
    quiet = energy < 0.05 * (np.median(energy) + 1e-9)
    sim[-1, quiet] = 2.0

    beta = 25.0
    prob = np.exp(beta * (sim - sim.max(axis=0, keepdims=True)))
    prob /= prob.sum(axis=0, keepdims=True)
    trans = librosa.sequence.transition_loop(len(labels), stay)
    path = librosa.sequence.viterbi(prob, trans)

    out: list[TimedChord] = []
    for i, state in enumerate(path):
        start, end = float(bounds[i]), float(bounds[i + 1])
        conf = float(prob[state, i])
        if out and out[-1].chord == labels[state]:
            prev = out[-1]
            span_old, span_new = prev.end - prev.start, end - start
            prev.confidence = (prev.confidence * span_old + conf * span_new) / (span_old + span_new)
            prev.end = end
        else:
            out.append(TimedChord(start, end, labels[state], conf))
    if out:
        out[-1].end = duration
    return out


# --------------------------------------------------------------------------
# Melody
# --------------------------------------------------------------------------

def _segment(times: np.ndarray, midi: np.ndarray, rms: np.ndarray, onsets: set[int], frame_dur: float,
             min_note: float) -> list[TimedNote]:
    """Turn a frame-wise pitch track (NaN = unvoiced) into notes."""
    from scipy.signal import medfilt

    voiced = np.isfinite(midi)
    smooth = midi.copy()
    if np.any(voiced):
        smooth[voiced] = medfilt(np.nan_to_num(midi, nan=0.0), 5)[voiced]
    notes: list[TimedNote] = []
    seg_start: int | None = None
    seg_vals: list[float] = []
    pending: list[int] = []
    change_frames = max(2, int(0.045 / frame_dur))

    def close(end_frame: int) -> None:
        nonlocal seg_start, seg_vals
        if seg_start is not None and seg_vals:
            start_t = times[seg_start]
            end_t = times[min(end_frame, len(times) - 1)] + frame_dur
            if end_t - start_t >= min_note:
                pitch = int(np.round(np.median(seg_vals)))
                loud = float(np.mean(rms[seg_start:end_frame + 1])) if end_frame >= seg_start else 0.0
                notes.append(TimedNote(float(start_t), float(end_t), pitch, loud))
        seg_start, seg_vals = None, []

    for i in range(len(midi)):
        if not voiced[i]:
            if seg_start is not None:
                close(i - 1)
            pending = []
            continue
        v = smooth[i]
        if seg_start is None:
            seg_start, seg_vals, pending = i, [v], []
            continue
        cur = float(np.median(seg_vals[-12:]))
        if i in onsets and abs(v - cur) < 0.5 and len(seg_vals) * frame_dur > 0.12:
            close(i - 1)  # re-articulated same pitch
            seg_start, seg_vals = i, [v]
            continue
        if abs(v - cur) > 0.7:
            pending.append(i)
            if len(pending) >= change_frames:
                close(pending[0] - 1)
                seg_start = pending[0]
                seg_vals = [smooth[j] for j in pending]
                pending = []
        else:
            seg_vals.extend(smooth[j] for j in pending)
            pending = []
            seg_vals.append(v)
    close(len(midi) - 1)

    merged: list[TimedNote] = []
    for n in notes:
        if merged and merged[-1].pitch == n.pitch and n.start - merged[-1].end < 0.03:
            merged[-1].end = n.end
        else:
            merged.append(n)
    if merged:  # loudness -> velocity
        db = 20 * np.log10(np.array([n.velocity for n in merged], dtype=float) + 1e-6)
        lo, hi = np.percentile(db, 5), np.percentile(db, 95)
        for n, d in zip(merged, db):
            n.velocity = int(np.clip(55 + 60 * (d - lo) / max(1e-6, hi - lo), 40, 120))
    return merged


def _remove_tuning(midi: np.ndarray) -> np.ndarray:
    voiced = np.isfinite(midi)
    if not np.any(voiced):
        return midi
    tuning = float(np.median((midi[voiced] - np.round(midi[voiced]) + 0.5) % 1.0 - 0.5))
    return midi - tuning


def pitch_track_pyin(y: np.ndarray, sr: int, fmin: float, fmax: float, hop: int) -> np.ndarray:
    """Frame-wise MIDI pitch (NaN = unvoiced) for an isolated monophonic source (vocals, solo)."""
    import librosa

    f0, voiced, vprob = librosa.pyin(y, fmin=fmin, fmax=fmax, sr=sr, frame_length=2048, hop_length=hop,
                                     fill_na=np.nan)
    voiced = voiced & (vprob > 0.35) & np.isfinite(f0)
    midi = np.full(len(f0), np.nan)
    midi[voiced] = librosa.hz_to_midi(f0[voiced])
    return _remove_tuning(midi)


def pitch_track_salience(y: np.ndarray, sr: int, fmin: float, fmax: float, hop: int,
                         voicing: float = 0.3, top_bias: float = 2.0, whiten: bool = True) -> np.ndarray:
    """Predominant-melody pitch track for a full mix: harmonic-sum salience on a
    constant-Q spectrogram, smoothed with a Viterbi pitch-continuity model."""
    import librosa

    bpo = 36
    c_fmin = 55.0
    n_bins = int(np.floor(bpo * np.log2(min(sr / 2 * 0.85, fmax * 6) / c_fmin)))
    C = np.abs(librosa.cqt(y, sr=sr, hop_length=hop, fmin=c_fmin, n_bins=n_bins, bins_per_octave=bpo))
    C = np.log1p(100 * C / (C.max() + 1e-9))
    if whiten:  # keep spectral peaks, drop the broad low-frequency energy hump
        from scipy.ndimage import uniform_filter1d

        C = np.maximum(0.0, C - uniform_filter1d(C, size=bpo, axis=0, mode="nearest"))
    lo = int(round(bpo * np.log2(fmin / c_fmin)))
    hi = min(int(round(bpo * np.log2(fmax / c_fmin))), C.shape[0] - 1)
    harmonics = [1, 2, 3, 4, 5, 6]
    weights = [1.0, 0.8, 0.64, 0.51, 0.41, 0.33]
    sal = np.zeros((hi - lo + 1, C.shape[1]))
    for h, w in zip(harmonics, weights):
        shift = int(round(bpo * np.log2(h)))
        idx = np.arange(lo, hi + 1) + shift
        valid = idx < C.shape[0]
        sal[valid] += w * C[idx[valid]]
    # flatten slowly varying background (sustained pads/chords) a little
    sal = np.maximum(0.0, sal - 0.5 * np.median(sal, axis=1, keepdims=True))
    # the lead usually sits above the accompaniment: gently favour higher candidates
    sal *= (1.0 + top_bias * np.linspace(0.0, 1.0, sal.shape[0]))[:, None]
    n_states = sal.shape[0]
    frame_max = sal.max(axis=0)
    prob = (sal / (frame_max[None, :] + 1e-9)) ** 4 + 1e-6
    prob /= prob.sum(axis=0, keepdims=True)
    d = np.abs(np.arange(n_states)[:, None] - np.arange(n_states)[None, :])
    trans = np.exp(-(d / 9.0) ** 2) + 1e-3
    trans /= trans.sum(axis=1, keepdims=True)
    path = librosa.sequence.viterbi(prob, trans)
    path_sal = sal[path, np.arange(sal.shape[1])]
    thresh = voicing * np.percentile(path_sal, 95)
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=hop)[0]
    rms = np.pad(rms, (0, max(0, len(path) - len(rms))))[: len(path)]
    voiced = (path_sal > thresh) & (rms > 0.02 * rms.max())
    midi = librosa.hz_to_midi(c_fmin) + (lo + path) * 12.0 / bpo
    midi = np.where(voiced, midi, np.nan)
    return _remove_tuning(midi)


def extract_melody(y: np.ndarray, sr: int = SR, fmin: float = 110.0, fmax: float = 1400.0,
                   min_note: float = 0.07, hop: int = 256, method: str = "salience") -> list[TimedNote]:
    """Transcribe the lead melody. method='salience' for full mixes, 'pyin' for isolated stems."""
    import librosa

    y_h = librosa.effects.harmonic(y, margin=2)
    if method == "pyin":
        from scipy.signal import butter, sosfilt

        sos = butter(4, max(40.0, fmin * 0.8) / (sr / 2), btype="high", output="sos")
        y_h = sosfilt(sos, y_h)
        midi = pitch_track_pyin(y_h, sr, fmin, fmax, hop)
    elif method == "salience":
        midi = pitch_track_salience(y_h, sr, fmin, fmax, hop)
    else:
        raise ValueError(f"Unknown melody method {method!r}")
    times = librosa.times_like(midi, sr=sr, hop_length=hop)
    rms = librosa.feature.rms(y=y_h, frame_length=2048, hop_length=hop)[0]
    rms = np.pad(rms, (0, max(0, len(midi) - len(rms))))[: len(midi)]
    onsets = set(librosa.onset.onset_detect(y=y, sr=sr, hop_length=hop, backtrack=False).tolist())
    return _segment(times, midi, rms, onsets, hop / sr, min_note)


def basic_pitch_available() -> bool:
    return importlib.util.find_spec("basic_pitch") is not None


def transcribe_polyphonic(path: str | Path) -> list[TimedNote]:
    """Full polyphonic transcription with Spotify's Basic Pitch (optional: `pip install basic-pitch`)."""
    if not basic_pitch_available():
        raise RuntimeError("Polyphonic transcription needs Basic Pitch: pip install basic-pitch")
    from basic_pitch.inference import predict

    _, _, events = predict(str(path))
    return [TimedNote(float(s), float(e), int(p), int(np.clip(a * 127, 30, 127))) for s, e, p, a, *_ in events]


# --------------------------------------------------------------------------
# Full analysis
# --------------------------------------------------------------------------

def analyze(path: str | Path, melody_source: str | Path | None = None, separate: bool = False,
            offset: float = 0.0, duration: float | None = None, fmin: float = 110.0, fmax: float = 1400.0,
            with_melody: bool = True, method: str = "auto") -> Analysis:
    """Analyse a track. `melody_source` may point to an isolated vocal/lead stem; `separate`
    runs Demucs to make one automatically."""
    y = load_audio(path, SR, offset, duration)
    bpm, beats = estimate_tempo(y, SR)
    treble, bass = chroma_parts(y, SR)
    chroma = treble + 0.3 * bass
    key, conf, cands = estimate_key(chroma)
    chords = detect_chords(y, SR, beats, key, (treble, bass))
    key, conf = resolve_key(chroma, chords, key)
    melody: list[TimedNote] = []
    if with_melody:
        src = melody_source
        if src is None and separate:
            src = separate_vocals(path)
        y_mel = load_audio(src, SR, offset, duration) if src is not None else y
        if method == "auto":
            method = "pyin" if src is not None else "salience"
        melody = extract_melody(y_mel, SR, fmin=fmin, fmax=fmax, method=method)
    return Analysis(str(path), len(y) / SR, bpm, beats.tolist(), key, conf, cands, chords, melody)


# --------------------------------------------------------------------------
# Convert to a Score (beats) for MIDI / audio export
# --------------------------------------------------------------------------

def _to_beats(t: float, bpm: float) -> float:
    return t * bpm / 60.0


def _quantize(b: float, grid: float, phase: float) -> float:
    return round((b - phase) / grid) * grid + phase


def analysis_to_score(an: Analysis, quantize: int | None = None, melody_instrument: str = "piano",
                      chord_instrument: str = "strings", include_bass: bool = True) -> Score:
    """Times are kept aligned with the original audio (beat 0 = second 0), so the MIDI
    lines up when dropped at the start of the song in a DAW."""
    bpm = an.bpm
    grid = 4.0 / quantize if quantize else None
    phase = (_to_beats(an.beat_times[0], bpm) % grid) if (grid and an.beat_times) else 0.0
    score = Score(bpm=bpm, key=an.key, style="transcription")

    mel = Track("Melody", melody_instrument)
    for n in an.melody:
        s, e = _to_beats(n.start, bpm), _to_beats(n.end, bpm)
        if grid:
            s, e = _quantize(s, grid, phase), _quantize(e, grid, phase)
            if e - s < grid:
                e = s + grid
        mel.notes.append(Note(max(0.0, s), e - s, n.pitch, n.velocity))
    if grid:  # remove overlaps created by quantisation
        mel.notes.sort(key=lambda n: n.start)
        dedup: list[Note] = []
        for n in mel.notes:
            if dedup and abs(dedup[-1].start - n.start) < 1e-6:
                continue
            if dedup and dedup[-1].end > n.start:
                dedup[-1].duration = max(grid, n.start - dedup[-1].start)
            dedup.append(n)
        mel.notes = dedup
    score.tracks.append(mel)

    chords_track = Track("Chords", chord_instrument)
    bass = Track("Bass", "bass")
    prev = None
    for c in an.chords:
        s, e = _to_beats(c.start, bpm), _to_beats(c.end, bpm)
        if grid:
            s, e = _quantize(s, grid, phase), _quantize(e, grid, phase)
        s = max(0.0, s)
        if e - s <= 0:
            continue
        score.chords.append(ChordEvent(s, e - s, c.chord))
        if c.chord is None:
            continue
        voicing = c.chord.voicing(60, prev)
        prev = voicing
        for p in voicing:
            chords_track.notes.append(Note(s, e - s, p, 72))
        root = 36 + (c.chord.root - 36) % 12
        bass.notes.append(Note(s, e - s, root, 90))
    score.tracks.append(chords_track)
    if include_bass:
        score.tracks.append(bass)
    return score
