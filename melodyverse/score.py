"""Plain data containers shared by the analyser, generator, MIDI and audio writers."""

from __future__ import annotations

from dataclasses import dataclass, field

from .theory import Chord, Key


@dataclass
class Note:
    start: float  # beats
    duration: float  # beats
    pitch: int  # MIDI note number
    velocity: int = 90
    bend_from: float = 0.0  # semitones; glide from pitch+bend_from to pitch (meend / blue-note bend)

    @property
    def end(self) -> float:
        return self.start + self.duration


@dataclass
class ChordEvent:
    start: float  # beats
    duration: float  # beats
    chord: Chord | None  # None = no chord


@dataclass
class Track:
    name: str
    instrument: str
    notes: list[Note] = field(default_factory=list)
    is_drums: bool = False
    drum_hits: list[tuple[float, str, int]] = field(default_factory=list)  # (beat, drum name, velocity)


@dataclass
class Score:
    bpm: float
    key: Key
    tracks: list[Track] = field(default_factory=list)
    chords: list[ChordEvent] = field(default_factory=list)
    style: str = ""

    def track(self, name: str) -> Track | None:
        return next((t for t in self.tracks if t.name == name), None)

    @property
    def length_beats(self) -> float:
        ends = [n.end for t in self.tracks for n in t.notes]
        ends += [b + 0.5 for t in self.tracks for b, _, _ in t.drum_hits]
        ends += [c.start + c.duration for c in self.chords]
        return max(ends, default=0.0)

    def seconds(self, beats: float) -> float:
        return beats * 60.0 / self.bpm
