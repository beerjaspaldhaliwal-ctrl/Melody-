import pytest

from melodyverse.generator import generate, parse_grid
from melodyverse.styles import STYLES, get_style
from melodyverse.theory import Key, parse_progression


def test_grid_parsing():
    ev = parse_grid("x---x-..X-------")
    assert [(e.start, e.length, e.accent) for e in ev] == [(0.0, 1.0, False), (1.0, 0.5, False), (2.0, 2.0, True)]


def test_style_library_is_valid():
    assert len(STYLES) >= 25
    for st in STYLES.values():
        key = Key.parse(st.default_key)
        for prog in st.progressions:
            assert parse_progression(prog, key)
        for grid in [*st.melody_rhythms, st.chord_pattern, st.bass_pattern, *st.drums.values()]:
            assert len(grid) % 16 == 0, (st.name, grid)
        assert st.tempo[0] <= st.tempo[1]


def test_aliases():
    assert get_style("punjabi").name == "punjabi_bhangra"
    assert get_style("Spanish").name == "spanish_flamenco"
    assert get_style("hollywood").name == "hollywood_epic"
    assert get_style("english").name == "english_pop"
    with pytest.raises(KeyError):
        get_style("not-a-style")


@pytest.mark.parametrize("name", sorted(STYLES))
def test_every_style_generates_in_key(name):
    st = STYLES[name]
    score = generate(name, bars=8, seed=42)
    mel = score.track("Melody")
    assert mel and len(mel.notes) >= 8
    lo, hi = st.melody_range
    allowed = score.key.pitch_classes
    for n in mel.notes:
        assert lo - 12 <= n.pitch <= hi + 12
        assert n.duration > 0
        if n.bend_from == 0 and n.duration >= 0.5:  # main notes (not grace notes) stay in the scale
            assert n.pitch % 12 in allowed or "blue_note" in st.ornaments, (name, n)
    assert score.length_beats <= 8 * 4 + 1
    assert score.track("Chords").notes and score.track("Bass").notes


def test_seed_is_deterministic():
    a = generate("bollywood", seed=3)
    b = generate("bollywood", seed=3)
    assert [(n.start, n.pitch) for n in a.track("Melody").notes] == [(n.start, n.pitch) for n in b.track("Melody").notes]


def test_custom_progression_and_key():
    score = generate("english_pop", key="E minor", progression="i bVI bIII bVII", bars=4, seed=1)
    assert [c.chord.name() for c in score.chords] == ["Em", "C", "G", "D"]


def test_strong_beats_land_on_chord_tones():
    score = generate("english_pop", bars=16, seed=9)
    hits = total = 0
    for n in score.track("Melody").notes:
        if abs(n.start % 2) < 1e-6 and n.duration >= 0.5:
            chord = next(c.chord for c in score.chords if c.start <= n.start < c.start + c.duration)
            total += 1
            hits += n.pitch % 12 in chord.pitch_classes
    assert total and hits / total > 0.8


def test_final_note_is_tonic_when_tonic_in_last_chord():
    score = generate("pop_ballad", key="C", progression="IV V vi I", bars=8, seed=5)
    last = max(score.track("Melody").notes, key=lambda n: n.start)
    assert last.pitch % 12 == 0
