import pytest

from melodyverse.progressions import find_famous, roman
from melodyverse.theory import Chord, Key, midi_to_name, note_to_midi, parse_progression, parse_roman


def test_note_parsing():
    assert note_to_midi("C4") == 60
    assert note_to_midi("A4") == 69
    assert note_to_midi("Bb3") == 58
    assert midi_to_name(61) == "C#4"
    assert midi_to_name(61, flats=True) == "Db4"


@pytest.mark.parametrize("text,tonic,mode", [
    ("C", 0, "major"), ("Am", 9, "minor"), ("F# minor", 6, "minor"),
    ("E phrygian_dominant", 4, "phrygian_dominant"), ("D phrygian dominant", 2, "phrygian_dominant"),
    ("C khamaj", 0, "khamaj"),
])
def test_key_parse(text, tonic, mode):
    k = Key.parse(text)
    assert (k.tonic, k.mode) == (tonic, mode)


def test_flat_spelling():
    assert Key.parse("D minor").prefers_flats
    assert Key.parse("F").prefers_flats
    assert not Key.parse("E minor").prefers_flats
    assert [c.name(True) for c in parse_progression("i bVI bVII", Key.parse("D minor"))] == ["Dm", "Bb", "C"]


def test_roman_numerals_in_minor():
    names = [c.name() for c in parse_progression("i bVI bIII bVII V7", Key.parse("A minor"))]
    assert names == ["Am", "F", "C", "G", "E7"]


def test_roman_suffixes():
    k = Key.parse("C")
    assert parse_roman("ii7", k) == Chord(2, "m7")
    assert parse_roman("V7", k) == Chord(7, "7")
    assert parse_roman("viio", k) == Chord(11, "dim")
    assert parse_roman("Imaj7", k) == Chord(0, "maj7")
    assert parse_roman("V/3", k) == Chord(7, "maj", bass=11)
    assert parse_roman("I5", k) == Chord(0, "5")


def test_chord_parse_and_name():
    for name in ["Am7", "F#m", "Bbmaj7", "Dsus4", "C/G", "Bdim", "E7"]:
        assert Chord.parse(name).name(flats="b" in name) == name


def test_voice_leading_moves_little():
    c = Chord.parse("C").voicing(60)
    f = Chord.parse("F").voicing(60, previous=c)
    assert sum(abs(a - b) for a, b in zip(sorted(c), sorted(f))) <= 4


def test_roman_naming_and_famous_detection():
    k = Key.parse("A minor")
    chords = [Chord.parse(x) for x in ["Am", "G", "F", "E"]]
    romans = [roman(c, k) for c in chords]
    assert romans == ["i", "bVII", "bVI", "V"]
    names = [h[0] for h in find_famous(romans)]
    assert "Andalusian cadence" in names
