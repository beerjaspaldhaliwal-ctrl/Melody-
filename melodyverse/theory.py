"""Music theory core: notes, scales, chords and roman-numeral progressions."""

from __future__ import annotations

import re
from dataclasses import dataclass

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLAT_NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]

_NOTE_RE = re.compile(r"^([A-Ga-g])([#b]*)(-?\d+)?$")
_BASE_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# Scales/modes as semitone offsets from the tonic. Hindustani thaats are
# included because Punjabi and Bollywood melodies are built on them.
SCALES: dict[str, list[int]] = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10],
    "harmonic_minor": [0, 2, 3, 5, 7, 8, 11],
    "melodic_minor": [0, 2, 3, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "locrian": [0, 1, 3, 5, 6, 8, 10],
    "phrygian_dominant": [0, 1, 4, 5, 7, 8, 10],  # Spanish / flamenco / hijaz
    "double_harmonic": [0, 1, 4, 5, 7, 8, 11],
    "hungarian_minor": [0, 2, 3, 6, 7, 8, 11],
    "major_pentatonic": [0, 2, 4, 7, 9],
    "minor_pentatonic": [0, 3, 5, 7, 10],
    "blues": [0, 3, 5, 6, 7, 10],
    "hirajoshi": [0, 2, 3, 7, 8],
    "whole_tone": [0, 2, 4, 6, 8, 10],
    # Hindustani thaats
    "bilawal": [0, 2, 4, 5, 7, 9, 11],
    "kalyan": [0, 2, 4, 6, 7, 9, 11],
    "khamaj": [0, 2, 4, 5, 7, 9, 10],
    "kafi": [0, 2, 3, 5, 7, 9, 10],
    "asavari": [0, 2, 3, 5, 7, 8, 10],
    "bhairavi": [0, 1, 3, 5, 7, 8, 10],
    "bhairav": [0, 1, 4, 5, 7, 8, 11],
    "todi": [0, 1, 3, 6, 7, 8, 11],
    "purvi": [0, 1, 4, 6, 7, 8, 11],
    "marwa": [0, 1, 4, 6, 7, 9, 11],
    "pahadi": [0, 2, 4, 7, 9],  # pentatonic folk colour used widely in Punjab/Himachal
}

CHORD_QUALITIES: dict[str, list[int]] = {
    "maj": [0, 4, 7],
    "min": [0, 3, 7],
    "dim": [0, 3, 6],
    "aug": [0, 4, 8],
    "sus2": [0, 2, 7],
    "sus4": [0, 5, 7],
    "5": [0, 7],
    "6": [0, 4, 7, 9],
    "m6": [0, 3, 7, 9],
    "7": [0, 4, 7, 10],
    "maj7": [0, 4, 7, 11],
    "m7": [0, 3, 7, 10],
    "mmaj7": [0, 3, 7, 11],
    "m7b5": [0, 3, 6, 10],
    "dim7": [0, 3, 6, 9],
    "add9": [0, 4, 7, 14],
    "madd9": [0, 3, 7, 14],
    "9": [0, 4, 7, 10, 14],
    "maj9": [0, 4, 7, 11, 14],
    "m9": [0, 3, 7, 10, 14],
    "7b9": [0, 4, 7, 10, 13],
}

QUALITY_SUFFIX = {
    "maj": "", "min": "m", "dim": "dim", "aug": "aug", "sus2": "sus2", "sus4": "sus4",
    "5": "5", "6": "6", "m6": "m6", "7": "7", "maj7": "maj7", "m7": "m7", "mmaj7": "mMaj7",
    "m7b5": "m7b5", "dim7": "dim7", "add9": "add9", "madd9": "madd9", "9": "9",
    "maj9": "maj9", "m9": "m9", "7b9": "7b9",
}

ROMAN_DEGREES = {"I": 0, "II": 2, "III": 4, "IV": 5, "V": 7, "VI": 9, "VII": 11}


def parse_pitch_class(name: str) -> int:
    """'C#' -> 1, 'Eb' -> 3, 'bb' -> 10."""
    m = _NOTE_RE.match(name.strip())
    if not m:
        raise ValueError(f"Not a note name: {name!r}")
    letter, accidentals, _ = m.groups()
    pc = _BASE_PC[letter.upper()]
    pc += accidentals.count("#") - accidentals.count("b")
    return pc % 12


def note_to_midi(name: str) -> int:
    """'C4' -> 60."""
    m = _NOTE_RE.match(name.strip())
    if not m or m.group(3) is None:
        raise ValueError(f"Not a note with octave: {name!r}")
    letter, accidentals, octave = m.groups()
    return _BASE_PC[letter.upper()] + accidentals.count("#") - accidentals.count("b") + 12 * (int(octave) + 1)


def midi_to_name(midi: int, flats: bool = False) -> str:
    names = FLAT_NAMES if flats else NOTE_NAMES
    return f"{names[midi % 12]}{midi // 12 - 1}"


def pc_name(pc: int, flats: bool = False) -> str:
    return (FLAT_NAMES if flats else NOTE_NAMES)[pc % 12]


@dataclass(frozen=True)
class Key:
    tonic: int  # pitch class
    mode: str = "major"

    @classmethod
    def parse(cls, text: str) -> "Key":
        """Accepts 'C', 'Am', 'F# minor', 'D dorian', 'E phrygian_dominant'."""
        text = text.strip()
        parts = text.replace("_", " ").split()
        head = parts[0]
        rest = "_".join(parts[1:]).lower()
        if not rest:
            if len(head) > 1 and head.endswith("m") and head[:-1] and _NOTE_RE.match(head[:-1]):
                return cls(parse_pitch_class(head[:-1]), "minor")
            return cls(parse_pitch_class(head), "major")
        mode = {"maj": "major", "min": "minor", "m": "minor"}.get(rest, rest)
        if mode not in SCALES:
            raise ValueError(f"Unknown mode {rest!r}. Known: {', '.join(sorted(SCALES))}")
        return cls(parse_pitch_class(head), mode)

    @property
    def scale(self) -> list[int]:
        return SCALES[self.mode]

    @property
    def pitch_classes(self) -> set[int]:
        return {(self.tonic + s) % 12 for s in self.scale}

    @property
    def is_minorish(self) -> bool:
        return 3 in self.scale and 4 not in self.scale

    @property
    def prefers_flats(self) -> bool:
        """Spell notes with flats in flat keys (F, Bb, Eb ... majors; D, G, C ... minors)."""
        relative_major = (self.tonic + 3) % 12 if self.is_minorish else self.tonic
        return relative_major in (5, 10, 3, 8, 1)

    def name(self) -> str:
        flats = self.prefers_flats or self.tonic in (3, 10, 8)
        return f"{pc_name(self.tonic, flats=flats)} {self.mode}"

    def scale_pitches(self, low: int, high: int) -> list[int]:
        pcs = self.pitch_classes
        return [p for p in range(low, high + 1) if p % 12 in pcs]


@dataclass(frozen=True)
class Chord:
    root: int  # pitch class
    quality: str = "maj"
    bass: int | None = None  # slash-chord bass pitch class

    @property
    def intervals(self) -> list[int]:
        return CHORD_QUALITIES[self.quality]

    @property
    def pitch_classes(self) -> list[int]:
        return [(self.root + i) % 12 for i in self.intervals]

    def name(self, flats: bool = False) -> str:
        n = pc_name(self.root, flats) + QUALITY_SUFFIX[self.quality]
        if self.bass is not None and self.bass != self.root:
            n += "/" + pc_name(self.bass, flats)
        return n

    @classmethod
    def parse(cls, text: str) -> "Chord":
        """'Am7', 'F#dim', 'Bbmaj7', 'C/G', 'Dsus4'."""
        text = text.strip()
        bass = None
        if "/" in text:
            text, b = text.split("/", 1)
            bass = parse_pitch_class(b)
        m = re.match(r"^([A-G][#b]?)(.*)$", text)
        if not m:
            raise ValueError(f"Not a chord: {text!r}")
        root = parse_pitch_class(m.group(1))
        suffix = m.group(2)
        lookup = {v.lower(): k for k, v in QUALITY_SUFFIX.items()}
        lookup.update({"": "maj", "min": "min", "maj": "maj", "-": "min", "m": "min",
                       "°": "dim", "o": "dim", "+": "aug", "ø": "m7b5", "ø7": "m7b5",
                       "min7": "m7", "-7": "m7", "Δ": "maj7", "Δ7": "maj7"})
        q = lookup.get(suffix.lower()) if suffix not in ("M7", "M9") else {"M7": "maj7", "M9": "maj9"}[suffix]
        if q is None:
            raise ValueError(f"Unknown chord quality {suffix!r} in {text!r}")
        return cls(root, q, bass)

    def voicing(self, center: int = 60, previous: list[int] | None = None) -> list[int]:
        """Return concrete MIDI pitches. With `previous`, choose the inversion that
        moves the least (smooth voice leading); otherwise close position near `center`."""
        base = [self.root + i for i in self.intervals]
        normalised: list[list[int]] = []
        for inv in range(len(base)):
            notes = sorted(n + (12 if k < inv else 0) for k, n in enumerate(base))
            for octave in range(-1, 2):
                shift = 12 * (round((center - sum(notes) / len(notes)) / 12) + octave)
                normalised.append([n + shift for n in notes])
        if previous:
            prev_mean = sum(previous) / len(previous)

            def cost(c: list[int]) -> float:
                movement = sum(min(abs(n - p) for p in previous) for n in c)
                drift = abs(sum(c) / len(c) - center) * 0.5
                return movement + drift + abs(sum(c) / len(c) - prev_mean) * 0.1

            return min(normalised, key=cost)
        return min(normalised, key=lambda c: (abs(sum(c) / len(c) - center), c[0]))


_ROMAN_RE = re.compile(
    r"^(?P<acc>[b#]*)(?P<num>VII|VI|IV|V|III|II|I|vii|vi|iv|v|iii|ii|i)"
    r"(?P<suffix>[^/]*)(?:/(?P<bass>.+))?$"
)


def parse_roman(token: str, key: Key) -> Chord:
    """Parse a roman numeral relative to `key`'s tonic.

    Degrees are measured on the major scale (so minor-key chords are written
    with flats: i bIII bVI bVII). Upper case = major, lower case = minor.
    Suffixes: 7 maj7 o (dim) o7 ø7 + sus2 sus4 add9 5 6 9.
    """
    m = _ROMAN_RE.match(token.strip())
    if not m:
        raise ValueError(f"Not a roman numeral: {token!r}")
    acc, num, suffix = m.group("acc"), m.group("num"), m.group("suffix")
    offset = ROMAN_DEGREES[num.upper()] + acc.count("#") - acc.count("b")
    root = (key.tonic + offset) % 12
    minor = num.islower()
    s = suffix
    if s in ("o", "°"):
        q = "dim"
    elif s in ("o7", "°7"):
        q = "dim7"
    elif s in ("ø", "ø7", "m7b5"):
        q = "m7b5"
    elif s == "+":
        q = "aug"
    elif s == "7":
        q = "m7" if minor else "7"
    elif s == "maj7":
        q = "mmaj7" if minor else "maj7"
    elif s == "9":
        q = "m9" if minor else "9"
    elif s == "maj9":
        q = "maj9"
    elif s == "add9":
        q = "madd9" if minor else "add9"
    elif s == "6":
        q = "m6" if minor else "6"
    elif s in ("sus2", "sus4", "5", "7b9"):
        q = s
    elif s == "":
        q = "min" if minor else "maj"
    else:
        raise ValueError(f"Unknown roman suffix {s!r} in {token!r}")
    bass = None
    if m.group("bass"):
        b = m.group("bass")
        if b.isdigit():  # figured-bass-ish inversion: /3 or /5
            idx = {"3": 1, "5": 2, "7": 3}.get(b)
            ivs = CHORD_QUALITIES[q]
            if idx is not None and idx < len(ivs):
                bass = (root + ivs[idx]) % 12
        else:
            bass = parse_roman(b, key).root
    return Chord(root, q, bass)


def parse_progression(text: str, key: Key) -> list[Chord]:
    """'i bVI bIII bVII' or 'Am F C G' -> list of Chords."""
    chords = []
    for tok in re.split(r"[\s|,–]+|\s-\s", text.strip()):
        if not tok or tok == "-":
            continue
        if tok[0] in "ABCDEFG":
            chords.append(Chord.parse(tok))
        else:
            chords.append(parse_roman(tok, key))
    return chords
