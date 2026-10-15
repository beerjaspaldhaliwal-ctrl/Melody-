"""End-to-end listening tests on synthesized songs with known ground truth."""

import pytest

from melodyverse.analysis import analysis_to_score, analyze
from melodyverse.generator import generate
from melodyverse.synth import render, write_wav


@pytest.fixture(scope="module")
def song(tmp_path_factory):
    score = generate("english_pop", key="A minor", bpm=100, bars=8, seed=5, progression="i bVI bIII bVII")
    score.track("Melody").instrument = "flute"
    path = tmp_path_factory.mktemp("audio") / "song.wav"
    write_wav(path, render(score, sr=22050), 22050)
    return score, path, analyze(path)


def test_tempo(song):
    _, _, an = song
    assert abs(an.bpm - 100) < 3


def test_key(song):
    _, _, an = song
    assert an.key.name() == "A minor"


def test_chords(song):
    _, _, an = song
    roots = [c.chord.root for c in an.chords if c.chord is not None and c.end - c.start > 1.0]
    dedup = [r for i, r in enumerate(roots) if i == 0 or r != roots[i - 1]]
    assert dedup[:4] == [9, 5, 0, 7]  # A F C G


def test_melody_transcription(song):
    score, _, an = song
    truth = [(score.seconds(n.start), n.pitch) for n in score.track("Melody").notes]
    hit = sum(any(g.start - 0.05 <= t + 0.05 <= g.end and g.pitch == p for g in an.melody) for t, p in truth)
    assert hit / len(truth) > 0.75


def test_analysis_to_score(song, tmp_path):
    from melodyverse.midi_io import write_midi

    _, _, an = song
    score = analysis_to_score(an, quantize=16)
    assert score.track("Melody").notes and score.track("Chords").notes
    grid = 0.25
    phase = (an.beat_times[0] * an.bpm / 60) % grid
    for n in score.track("Melody").notes:
        assert abs(((n.start - phase) / grid) - round((n.start - phase) / grid)) < 1e-6 or n.start == 0.0
    write_midi(score, tmp_path / "t.mid")
    d = an.to_dict()
    assert d["key"] == "A minor" and d["chords"] and d["melody"]
    assert any(f["name"] == "Epic minor loop" for f in d["famous_progressions"])
