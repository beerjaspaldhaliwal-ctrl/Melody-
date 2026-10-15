"""Generate original melodies, chord parts, bass lines and drum grooves in any style."""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

from .score import ChordEvent, Note, Score, Track
from .styles import Style, get_style
from .theory import Chord, Key, parse_progression

STEP = 0.25  # one grid step = a 16th note = 0.25 beats
BAR = 4.0

ONSET_CHARS = set("xX1358c")


@dataclass
class GridEvent:
    start: float  # beats, relative to pattern start
    length: float  # beats
    accent: bool
    symbol: str


def parse_grid(grid: str) -> list[GridEvent]:
    events: list[GridEvent] = []
    current: GridEvent | None = None
    for i, ch in enumerate(grid):
        if ch in ONSET_CHARS:
            current = GridEvent(i * STEP, STEP, ch == "X", ch)
            events.append(current)
        elif ch == "-" and current is not None:
            current.length += STEP
        else:  # rest or unknown
            current = None
    return events


def grid_bars(grid: str) -> int:
    return max(1, len(grid) // 16)


def _iter_grid(grid: str, total_beats: float):
    """Yield GridEvents in absolute beats, repeating `grid` for `total_beats`."""
    span = grid_bars(grid) * BAR
    events = parse_grid(grid)
    offset = 0.0
    while offset < total_beats - 1e-9:
        for e in events:
            s = offset + e.start
            if s < total_beats - 1e-9:
                yield GridEvent(s, min(e.length, total_beats - s), e.accent, e.symbol)
        offset += span


# --------------------------------------------------------------------------
# Harmony
# --------------------------------------------------------------------------

def chord_timeline(style: Style, key: Key, bars: int, progression: str | None, rng: random.Random) -> list[ChordEvent]:
    prog_text = progression or rng.choice(style.progressions)
    chords = parse_progression(prog_text, key)
    total = bars * BAR
    timeline: list[ChordEvent] = []
    t, i = 0.0, 0
    while t < total - 1e-9:
        dur = min(style.beats_per_chord, total - t)
        timeline.append(ChordEvent(t, dur, chords[i % len(chords)]))
        t += dur
        i += 1
    return timeline


def chord_at(timeline: list[ChordEvent], beat: float) -> ChordEvent | None:
    for ev in timeline:
        if ev.start - 1e-9 <= beat < ev.start + ev.duration - 1e-9:
            return ev
    return None


def _segments(timeline: list[ChordEvent], grid: str, total: float):
    """For every chord, the grid events that sound during it (re-articulated at chord changes)."""
    events = list(_iter_grid(grid, total))
    for ev in timeline:
        if ev.chord is None:
            continue
        c_end = ev.start + ev.duration
        for g in events:
            g_end = g.start + g.length
            if g_end <= ev.start + 1e-9 or g.start >= c_end - 1e-9:
                continue
            start = max(g.start, ev.start)
            yield ev, GridEvent(start, min(g_end, c_end) - start, g.accent, g.symbol)


def comp_chords(timeline: list[ChordEvent], style: Style, total: float, rng: random.Random) -> Track:
    track = Track("Chords", style.instruments.get("chords", "piano"))
    prev_voicing: list[int] | None = None
    current_chord = None
    voicing: list[int] = []
    arp_index = 0
    arp_shape = [0, 1, 2, 3, 2, 1]
    for ev, g in _segments(timeline, style.chord_pattern, total):
        if ev.chord is not current_chord:
            current_chord = ev.chord
            voicing = ev.chord.voicing(style.chord_octave, prev_voicing)
            prev_voicing = voicing
            arp_index = 0
        vel = 84 if g.accent else 70
        if style.chord_style == "arp":
            tones = voicing + [voicing[0] + 12]
            pitch = tones[arp_shape[arp_index % len(arp_shape)] % len(tones)]
            arp_index += 1
            ring = min(g.length * 2, ev.start + ev.duration - g.start)
            track.notes.append(Note(g.start, ring, pitch, vel - 6 + rng.randint(-4, 4)))
        elif style.chord_style == "strum":
            down = g.accent or arp_index % 2 == 0
            order = voicing if down else list(reversed(voicing))
            arp_index += 1
            for k, p in enumerate(order):
                off = k * 0.03
                track.notes.append(Note(g.start + off, max(0.05, g.length - off), p, vel - k * 2))
        else:
            for p in voicing:
                track.notes.append(Note(g.start, g.length, p, vel + rng.randint(-3, 3)))
    return track


def bass_line(timeline: list[ChordEvent], style: Style, total: float) -> Track:
    track = Track("Bass", style.instruments.get("bass", "bass"))
    for idx, (ev, g) in enumerate(_segments(timeline, style.bass_pattern, total)):
        chord = ev.chord
        root_pc = chord.bass if chord.bass is not None else chord.root
        root = 36 + (root_pc - 36) % 12
        ivs = chord.intervals
        sym = g.symbol
        if sym in ("x", "X", "1"):
            pitch = root
        elif sym == "3":
            pitch = root + (ivs[1] if len(ivs) > 2 else 7)
        elif sym == "5":
            pitch = root + (ivs[2] if len(ivs) > 2 else ivs[-1])
        elif sym == "8":
            pitch = root + 12
        else:  # 'c' chromatic approach to next chord root
            nxt = next((c for c in timeline if c.start > ev.start + 1e-9 and c.chord), None)
            if nxt is None:
                pitch = root
            else:
                target = 36 + ((nxt.chord.bass if nxt.chord.bass is not None else nxt.chord.root) - 36) % 12
                pitch = target - 1 if target - 1 >= 33 else target + 1
        track.notes.append(Note(g.start, g.length, pitch, 100 if g.accent else 88))
    return track


def drum_track(style: Style, total: float, rng: random.Random) -> Track:
    track = Track("Drums", "drums", is_drums=True)
    for name, grid in style.drums.items():
        for g in _iter_grid(grid, total):
            vel = 110 if g.accent else 82 + rng.randint(-8, 8)
            track.drum_hits.append((g.start, name, vel))
    track.drum_hits.sort()
    return track


# --------------------------------------------------------------------------
# Melody
# --------------------------------------------------------------------------

def _contour(shape: str, t: float) -> float:
    if shape == "ascending":
        return 0.15 + 0.75 * t
    if shape == "descending":
        return 0.9 - 0.75 * t
    if shape == "wave":
        return 0.5 + 0.35 * math.sin(2 * math.pi * t)
    return 0.25 + 0.7 * math.sin(math.pi * t)  # arch


def _weighted_choice(rng: random.Random, items: list[int], weights: list[float]) -> int:
    total = sum(weights)
    if total <= 0:
        return rng.choice(items)
    r = rng.random() * total
    acc = 0.0
    for it, w in zip(items, weights):
        acc += w
        if r <= acc:
            return it
    return items[-1]


def _nearest_index(scale: list[int], pitch: float, allowed_pcs: set[int] | None = None) -> int:
    best, best_d = 0, 1e9
    for i, p in enumerate(scale):
        if allowed_pcs is not None and p % 12 not in allowed_pcs:
            continue
        d = abs(p - pitch)
        if d < best_d:
            best, best_d = i, d
    return best


def write_melody(timeline: list[ChordEvent], key: Key, style: Style, bars: int, rng: random.Random) -> Track:
    low, high = style.melody_range
    scale = key.scale_pitches(low, high)
    track = Track("Melody", style.instruments.get("melody", "piano"))
    form = style.phrase_form
    phrase_bars = min(4, max(1, bars))
    motifs: dict[str, tuple[list[GridEvent], list[int]]] = {}
    prev_idx = _nearest_index(scale, (low + high) / 2)
    last_interval = 0
    repeats = 0
    used_cells: list[str] = []

    def chord_pcs(beat: float) -> set[int]:
        ev = chord_at(timeline, beat)
        return set(ev.chord.pitch_classes) if ev and ev.chord else key.pitch_classes

    for bar in range(bars):
        bar_start = bar * BAR
        letter = form[bar % len(form)]
        phrase_pos = bar % phrase_bars
        final_bar = bar == bars - 1
        phrase_end = phrase_pos == phrase_bars - 1
        if bar % len(form) == 0 and bar > 0:
            # a new phrase repeats the same letters: keep A, refresh the others
            motifs = {k: v for k, v in motifs.items() if k == "A"}

        if letter in motifs and not final_bar:
            cells, intervals = motifs[letter]
            pcs = chord_pcs(bar_start + cells[0].start) if cells else key.pitch_classes
            idx = _nearest_index(scale, scale[prev_idx], pcs)
            new_idx = []
            for k, iv in enumerate(intervals):
                idx = idx + iv if k else idx
                idx = max(0, min(len(scale) - 1, idx))
                new_idx.append(idx)
            # vary the tail occasionally so repeats breathe
            if rng.random() < 0.35 and len(new_idx) > 1:
                new_idx[-1] = max(0, min(len(scale) - 1, new_idx[-1] + rng.choice([-2, -1, 1, 2])))
            for g, idx in zip(cells, new_idx):
                beat = bar_start + g.start
                strong = g.start % 2 == 0
                pcs = chord_pcs(beat)
                if strong and scale[idx] % 12 not in pcs:
                    idx = _nearest_index(scale, scale[idx], pcs)
                vel = (100 if strong else 88) + rng.randint(-5, 5)
                track.notes.append(Note(beat, g.length, scale[idx], vel))
                prev_idx = idx
            continue

        choices = [c for c in style.melody_rhythms if c not in used_cells[-1:]] or style.melody_rhythms
        cell = rng.choice(choices)
        used_cells.append(cell)
        cells = parse_grid(cell)
        if final_bar or phrase_end:
            # cadence: make the last note long so the phrase lands
            if cells:
                last = cells[-1]
                last.length = max(last.length, BAR - last.start)
        intervals: list[int] = []
        for n, g in enumerate(cells):
            beat = bar_start + g.start
            strong = g.start % 2 == 0
            pcs = chord_pcs(beat)
            t = (phrase_pos + g.start / BAR) / phrase_bars
            target = low + (high - low) * _contour(style.contour, t)
            is_last = n == len(cells) - 1
            cands = list(range(len(scale)))
            weights = []
            for j in cands:
                d = j - prev_idx
                ad = abs(d)
                w = style.step_weights.get(ad, 0.05 if ad <= 7 else 0.0)
                p = scale[j]
                if strong:
                    w *= 4.0 if p % 12 in pcs else 0.25
                elif p % 12 in pcs:
                    w *= 1.5
                w *= math.exp(-abs(p - target) / 5.0)
                if abs(last_interval) >= 3 and d != 0 and (d > 0) == (last_interval > 0):
                    w *= 0.3  # after a leap, turn around (gap fill)
                elif abs(last_interval) >= 3 and 0 < ad <= 2:
                    w *= 2.0
                if d == 0 and repeats >= 2:
                    w *= 0.1
                if is_last and (final_bar or phrase_end):
                    if final_bar:
                        w *= 12.0 if p % 12 == key.tonic and p % 12 in pcs else (2.0 if p % 12 in pcs else 0.05)
                    else:
                        w *= 3.0 if p % 12 in pcs else 0.2
                weights.append(w)
            idx = _weighted_choice(rng, cands, weights)
            if final_bar and is_last:
                # land the piece: tonic if the last chord allows it, else that chord's root
                goal = key.tonic if key.tonic in pcs else (chord_at(timeline, beat).chord.root if chord_at(timeline, beat) else key.tonic)
                idx = _nearest_index(scale, scale[prev_idx], {goal}) if any(p % 12 == goal for p in scale) else idx
            interval = idx - prev_idx
            repeats = repeats + 1 if interval == 0 else 0
            intervals.append(interval if n else 0)
            last_interval = interval
            prev_idx = idx
            vel = (100 if strong else 86) + rng.randint(-5, 5)
            track.notes.append(Note(beat, g.length, scale[idx], vel))
        if letter not in motifs:
            motifs[letter] = (cells, intervals)
    return track


# --------------------------------------------------------------------------
# Ornaments & feel
# --------------------------------------------------------------------------

def _neighbour(key: Key, pitch: int, direction: int) -> int:
    pcs = key.pitch_classes
    p = pitch + direction
    while p % 12 not in pcs:
        p += direction
    return p


def apply_ornaments(notes: list[Note], key: Key, style: Style, rng: random.Random) -> list[Note]:
    if not style.ornaments:
        return notes
    out: list[Note] = []
    prev: Note | None = None
    for n in notes:
        orn = rng.choice(style.ornaments) if rng.random() < style.ornament_rate else None
        if orn == "murki" and n.duration >= 0.75:
            g = 0.0833
            up, down = _neighbour(key, n.pitch, 1), _neighbour(key, n.pitch, -1)
            for k, p in enumerate((up, n.pitch, down)):
                out.append(Note(n.start + k * g, g, p, n.velocity - 15))
            out.append(Note(n.start + 3 * g, n.duration - 3 * g, n.pitch, n.velocity))
        elif orn == "meend" and n.duration >= 0.75:
            if prev is not None and prev.pitch != n.pitch and abs(prev.end - n.start) < 0.3:
                bend = max(-3.0, min(3.0, float(prev.pitch - n.pitch)))
            else:
                bend = -2.0
            out.append(Note(n.start, n.duration, n.pitch, n.velocity, bend_from=bend))
        elif orn == "flamenco_turn" and n.duration >= 0.75:
            g = 1 / 6
            up, down = _neighbour(key, n.pitch, 1), _neighbour(key, n.pitch, -1)
            for k, p in enumerate((up, n.pitch, down)):
                out.append(Note(n.start + k * g, g, p, n.velocity - 10))
            out.append(Note(n.start + 0.5, n.duration - 0.5, n.pitch, n.velocity))
        elif orn == "trill" and n.duration >= 1.5:
            up = _neighbour(key, n.pitch, 1)
            t, k = n.start, 0
            half = n.start + n.duration / 2
            while t < half - 1e-9:
                out.append(Note(t, 0.125, n.pitch if k % 2 == 0 else up, n.velocity - 8))
                t += 0.125
                k += 1
            out.append(Note(t, n.end - t, n.pitch, n.velocity))
        elif orn == "blue_note" and n.duration >= 0.5 and (n.pitch - key.tonic) % 12 == 3:
            out.append(Note(n.start, n.duration, n.pitch + 1, n.velocity, bend_from=-1.0))
        else:
            out.append(n)
        prev = n
    return out


def apply_swing(notes: list[Note], swing: float) -> None:
    if swing <= 0:
        return
    for n in notes:
        frac = round((n.start % 1.0) / STEP) * STEP
        shift = 0.0
        if abs(frac - 0.5) < 1e-6:
            shift = swing * 0.5
        elif abs(frac - 0.25) < 1e-6 or abs(frac - 0.75) < 1e-6:
            shift = swing * 0.25
        if shift:
            n.start += shift
            n.duration = max(0.05, n.duration - shift)


def swing_hits(hits: list[tuple[float, str, int]], swing: float) -> list[tuple[float, str, int]]:
    if swing <= 0:
        return hits
    out = []
    for b, name, v in hits:
        frac = round((b % 1.0) / STEP) * STEP
        if abs(frac - 0.5) < 1e-6:
            b += swing * 0.5
        elif abs(frac - 0.25) < 1e-6 or abs(frac - 0.75) < 1e-6:
            b += swing * 0.25
        out.append((b, name, v))
    return out


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

DEFAULT_PARTS = ("melody", "chords", "bass", "drums")


def generate(
    style: str | Style,
    key: str | Key | None = None,
    bpm: float | None = None,
    bars: int = 8,
    seed: int | None = None,
    progression: str | None = None,
    timeline: list[ChordEvent] | None = None,
    parts: tuple[str, ...] = DEFAULT_PARTS,
) -> Score:
    """Write an original piece in `style`.

    Pass `timeline` (e.g. chords detected from a track) to write a new melody
    over existing harmony instead of a stock progression.
    """
    st = style if isinstance(style, Style) else get_style(style)
    rng = random.Random(seed)
    k = key if isinstance(key, Key) else Key.parse(key or st.default_key)
    tempo = float(bpm) if bpm else float(rng.randint(*st.tempo))
    if timeline is None:
        timeline = chord_timeline(st, k, bars, progression, rng)
    else:
        bars = max(1, math.ceil(max(e.start + e.duration for e in timeline) / BAR - 1e-6))
    total = bars * BAR
    score = Score(bpm=tempo, key=k, chords=timeline, style=st.name)
    if "melody" in parts:
        mel = write_melody(timeline, k, st, bars, rng)
        mel.notes = apply_ornaments(mel.notes, k, st, rng)
        apply_swing(mel.notes, st.swing)
        score.tracks.append(mel)
    if "chords" in parts:
        ch = comp_chords(timeline, st, total, rng)
        apply_swing(ch.notes, st.swing)
        score.tracks.append(ch)
    if "bass" in parts:
        bs = bass_line(timeline, st, total)
        apply_swing(bs.notes, st.swing)
        score.tracks.append(bs)
    if "drums" in parts and st.drums:
        dr = drum_track(st, total, rng)
        dr.drum_hits = swing_hits(dr.drum_hits, st.swing)
        score.tracks.append(dr)
    return score
