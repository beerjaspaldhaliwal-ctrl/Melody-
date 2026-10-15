"""MIDI export / import (General MIDI)."""

from __future__ import annotations

from pathlib import Path

import mido

from .score import Note, Score, Track
from .theory import Key

TICKS = 480

GM_PROGRAMS = {
    "piano": 0, "epiano": 4, "harpsichord": 6, "celesta": 8, "bell": 14, "organ": 16, "harmonium": 20,
    "nylon": 24, "guitar": 25, "pluck": 25, "bass": 33, "upright": 32, "sub808": 38,
    "strings": 48, "cello": 42, "pad": 89, "choir": 52, "brass": 61, "flute": 73,
    "bansuri": 73, "lead": 81, "supersaw": 81, "sitar": 104, "tumbi": 104, "oud": 105,
    "drone": 89,
}

GM_DRUMS = {
    "kick": 36, "snare": 38, "clap": 39, "rim": 37, "hat": 42, "openhat": 46, "shaker": 70,
    "tom": 45, "taiko": 41, "dhol_low": 64, "dhol_high": 62, "tabla_dha": 61, "tabla_na": 60,
    "cajon_low": 64, "cajon_high": 63, "darbuka_dum": 64, "darbuka_tek": 63,
}
_DRUM_BY_NOTE = {v: k for k, v in reversed(list(GM_DRUMS.items()))}

BEND_RANGE = 12  # semitones, set via RPN so glides of up to an octave are exact


def _tick(beats: float) -> int:
    return max(0, int(round(beats * TICKS)))


def _track_messages(track: Track, channel: int) -> list[tuple[int, int, mido.Message]]:
    """(tick, order, message) — order makes note-offs sort before note-ons at equal ticks."""
    events: list[tuple[int, int, mido.Message]] = []
    if track.is_drums:
        for beat, name, vel in track.drum_hits:
            note = GM_DRUMS.get(name, 38)
            t = _tick(beat)
            events.append((t, 2, mido.Message("note_on", channel=9, note=note, velocity=min(127, vel))))
            events.append((t + TICKS // 8, 0, mido.Message("note_off", channel=9, note=note, velocity=0)))
        return events
    events.append((0, 1, mido.Message("program_change", channel=channel, program=GM_PROGRAMS.get(track.instrument, 0))))
    uses_bend = any(n.bend_from for n in track.notes)
    if uses_bend:
        for ctrl, val in ((101, 0), (100, 0), (6, BEND_RANGE), (38, 0)):
            events.append((0, 1, mido.Message("control_change", channel=channel, control=ctrl, value=val)))
    for n in track.notes:
        if not 0 <= n.pitch <= 127:
            continue
        on, off = _tick(n.start), _tick(n.end)
        if off <= on:
            off = on + 1
        if uses_bend:
            if n.bend_from:
                glide = max(1, min(off - on, _tick(min(0.5, n.duration * 0.4))))
                steps = 8
                for k in range(steps + 1):
                    frac = 1 - k / steps
                    val = int(round(n.bend_from / BEND_RANGE * 8191 * frac))
                    events.append((on + glide * k // steps, 1, mido.Message("pitchwheel", channel=channel, pitch=max(-8192, min(8191, val)))))
            else:
                events.append((on, 1, mido.Message("pitchwheel", channel=channel, pitch=0)))
        events.append((on, 2, mido.Message("note_on", channel=channel, note=n.pitch, velocity=max(1, min(127, n.velocity)))))
        events.append((off, 0, mido.Message("note_off", channel=channel, note=n.pitch, velocity=0)))
    return events


def write_midi(score: Score, path: str | Path, tracks: list[str] | None = None) -> Path:
    """Write `score` (optionally only the named tracks) as a type-1 MIDI file."""
    path = Path(path)
    mid = mido.MidiFile(type=1, ticks_per_beat=TICKS)
    meta = mido.MidiTrack()
    meta.append(mido.MetaMessage("track_name", name=f"Melodyverse - {score.style or 'score'}", time=0))
    meta.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(score.bpm), time=0))
    meta.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
    meta.append(mido.MetaMessage("key_signature", key=_key_sig(score.key), time=0))
    for c in score.chords:
        if c.chord is not None:
            meta.append(mido.MetaMessage("marker", text=c.chord.name(score.key.prefers_flats), time=_tick(c.start)))
    _absolute_to_delta(meta)
    mid.tracks.append(meta)
    channel = 0
    for track in score.tracks:
        if tracks is not None and track.name not in tracks:
            continue
        mt = mido.MidiTrack()
        mt.append(mido.MetaMessage("track_name", name=track.name, time=0))
        ch = 9 if track.is_drums else channel
        if not track.is_drums:
            channel += 1
            if channel == 9:
                channel = 10
        events = sorted(_track_messages(track, ch), key=lambda e: (e[0], e[1]))
        last = 0
        for t, _, msg in events:
            msg.time = t - last
            last = t
            mt.append(msg)
        mid.tracks.append(mt)
    path.parent.mkdir(parents=True, exist_ok=True)
    mid.save(path)
    return path


def _absolute_to_delta(track: mido.MidiTrack) -> None:
    msgs = sorted(track, key=lambda m: m.time)
    last = 0
    track.clear()
    for m in msgs:
        t = m.time
        m.time = t - last
        last = t
        track.append(m)


def _key_sig(key: Key) -> str:
    names = {0: "C", 1: "Db", 2: "D", 3: "Eb", 4: "E", 5: "F", 6: "F#", 7: "G", 8: "Ab", 9: "A", 10: "Bb", 11: "B"}
    minor_names = {0: "Cm", 1: "C#m", 2: "Dm", 3: "Ebm", 4: "Em", 5: "Fm", 6: "F#m", 7: "Gm", 8: "G#m", 9: "Am", 10: "Bbm", 11: "Bm"}
    return minor_names[key.tonic] if key.is_minorish else names[key.tonic]


def read_midi(path: str | Path) -> Score:
    """Read any MIDI file into a Score (tempo = first tempo event)."""
    mid = mido.MidiFile(path)
    tpb = mid.ticks_per_beat
    bpm = 120.0
    for tr in mid.tracks:
        for m in tr:
            if m.type == "set_tempo":
                bpm = mido.tempo2bpm(m.tempo)
                break
        else:
            continue
        break
    score = Score(bpm=bpm, key=Key(0, "major"), style="imported")
    for i, tr in enumerate(mid.tracks):
        name = f"Track {i}"
        program = 0
        notes: list[Note] = []
        hits: list[tuple[float, str, int]] = []
        active: dict[tuple[int, int], tuple[float, int]] = {}
        tick = 0
        drums = False
        for m in tr:
            tick += m.time
            beat = tick / tpb
            if m.type == "track_name":
                name = m.name
            elif m.type == "program_change":
                program = m.program
            elif m.type == "note_on" and m.velocity > 0:
                if m.channel == 9:
                    drums = True
                    hits.append((beat, _DRUM_BY_NOTE.get(m.note, "snare"), m.velocity))
                else:
                    active[(m.channel, m.note)] = (beat, m.velocity)
            elif m.type in ("note_off", "note_on") and m.channel != 9:
                start = active.pop((m.channel, m.note), None)
                if start:
                    notes.append(Note(start[0], max(0.01, beat - start[0]), m.note, start[1]))
        if not notes and not hits:
            continue
        inst = next((k for k, v in GM_PROGRAMS.items() if v == program), "piano")
        t = Track(name, inst, sorted(notes, key=lambda n: n.start), is_drums=drums and not notes, drum_hits=hits)
        score.tracks.append(t)
    return score
