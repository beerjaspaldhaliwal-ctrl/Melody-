import numpy as np

from melodyverse.generator import generate
from melodyverse.midi_io import read_midi, write_midi
from melodyverse.synth import render, write_wav


def test_midi_roundtrip(tmp_path):
    score = generate("punjabi_sad", bars=4, seed=2)
    path = write_midi(score, tmp_path / "x.mid")
    back = read_midi(path)
    assert abs(back.bpm - score.bpm) < 0.5
    names = [t.name for t in back.tracks]
    assert names[:3] == ["Melody", "Chords", "Bass"]
    src = sorted((round(n.start, 2), n.pitch) for n in score.track("Melody").notes)
    dst = sorted((round(n.start, 2), n.pitch) for n in back.track("Melody").notes)
    assert src == dst
    assert back.track("Drums").drum_hits


def test_single_track_export(tmp_path):
    score = generate("trap", bars=2, seed=1)
    back = read_midi(write_midi(score, tmp_path / "m.mid", tracks=["Melody"]))
    assert [t.name for t in back.tracks] == ["Melody"]


def test_render_produces_audio(tmp_path):
    score = generate("spanish_flamenco", bars=2, seed=4)
    audio = render(score, sr=22050)
    assert audio.ndim == 2 and audio.shape[1] == 2
    assert np.isfinite(audio).all()
    assert 0.3 < np.max(np.abs(audio)) <= 1.0
    path = write_wav(tmp_path / "a.wav", audio, 22050)
    assert path.stat().st_size > 10_000


def test_every_instrument_renders():
    from melodyverse.midi_io import GM_PROGRAMS
    from melodyverse.score import Note
    from melodyverse.synth import synth_note

    for inst in GM_PROGRAMS:
        y = synth_note(inst, Note(0, 1, 64, 100, bend_from=-2.0), 120, sr=22050)
        assert np.isfinite(y).all() and np.max(np.abs(y)) > 0.01, inst
