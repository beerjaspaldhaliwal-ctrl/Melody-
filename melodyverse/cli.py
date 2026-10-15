"""Command-line interface: `melodyverse <command> ...`"""

from __future__ import annotations

import argparse
import json
import sys
import textwrap
from pathlib import Path

from . import __version__


def _write_bundle(score, out: Path, stem: str, audio: bool, sr: int, parts: list[str] | None = None,
                  backing=None, backing_gain: float = 0.6) -> list[Path]:
    """Write <stem>.mid, <stem>.wav plus one MIDI/WAV per part."""
    from .midi_io import write_midi
    from .synth import master, render, write_wav

    written = [write_midi(score, out / f"{stem}.mid")]
    names = [t.name for t in score.tracks if parts is None or t.name in parts]
    for name in names:
        written.append(write_midi(score, out / f"{stem}_{name.lower()}.mid", tracks=[name]))
    if audio:
        mix = render(score, sr=sr)
        written.append(write_wav(out / f"{stem}.wav", mix, sr))
        for name in names:
            written.append(write_wav(out / f"{stem}_{name.lower()}.wav", render(score, sr=sr, tracks=[name]), sr))
        if backing is not None:
            import numpy as np

            n = max(len(mix), len(backing))
            both = np.zeros((n, 2))
            both[: len(backing)] += backing_gain * backing
            both[: len(mix)] += mix
            written.append(write_wav(out / f"{stem}_over_original.wav", master(both), sr))
    return written


def _load_stereo(path: str, sr: int, offset: float, duration: float | None):
    import librosa
    import numpy as np

    y, _ = librosa.load(path, sr=sr, mono=False, offset=offset, duration=duration)
    if y.ndim == 1:
        y = np.stack([y, y])
    return y.T


def _print_written(paths: list[Path]) -> None:
    for p in paths:
        print(f"  wrote {p}")


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------

def cmd_styles(args) -> int:
    from .styles import STYLES

    q = (args.search or "").lower()
    for st in STYLES.values():
        hay = " ".join([st.name, st.region, st.description, *st.tags]).lower()
        if q and q not in hay:
            continue
        lo, hi = st.tempo
        print(f"{st.name:20s} {st.region:28s} {lo}-{hi} bpm  {st.mode}")
        if args.verbose:
            print(textwrap.indent(textwrap.fill(st.description, 76), "    "))
            print(f"    progressions: {' | '.join(st.progressions)}")
            print(f"    ornaments: {', '.join(st.ornaments) or '-'}   instruments: {st.instruments}")
    return 0


def cmd_progressions(args) -> int:
    from .progressions import FAMOUS
    from .theory import Key, parse_progression

    key = Key.parse(args.key) if args.key else None
    for name, (prog, where) in FAMOUS.items():
        line = f"{name:28s} {prog:28s} {where}"
        if key:
            line += "\n" + " " * 29 + " ".join(c.name(key.prefers_flats) for c in parse_progression(prog, key))
        print(line)
    return 0


def cmd_generate(args) -> int:
    from .generator import generate
    from .styles import get_style

    style = get_style(args.style)
    out = Path(args.out)
    parts = tuple(args.parts.split(",")) if args.parts else ("melody", "chords", "bass", "drums")
    for v in range(args.variations):
        seed = (args.seed if args.seed is not None else _random_seed()) + v
        score = generate(style, key=args.key, bpm=args.bpm, bars=args.bars, seed=seed,
                         progression=args.progression, parts=parts)
        if args.instrument:
            score.track("Melody") and setattr(score.track("Melody"), "instrument", args.instrument)
        stem = f"{style.name}_{score.key.name().replace(' ', '_')}_{int(score.bpm)}bpm_seed{seed}"
        chords = " ".join(c.chord.name(score.key.prefers_flats) for c in score.chords[: max(1, int(16 / style.beats_per_chord))])
        print(f"[{v + 1}/{args.variations}] {style.name} in {score.key.name()} @ {score.bpm:.0f} bpm | {chords}")
        _print_written(_write_bundle(score, out, stem, not args.no_audio, args.sr))
    return 0


def _random_seed() -> int:
    import random

    return random.randint(0, 999_999)


def _analyze(args, with_melody: bool = True):
    from .analysis import analyze

    melody_src = getattr(args, "vocals", None)
    return analyze(args.track, melody_source=melody_src, separate=getattr(args, "separate", False),
                   offset=args.start, duration=args.duration, fmin=getattr(args, "fmin", 110.0),
                   fmax=getattr(args, "fmax", 1400.0), with_melody=with_melody,
                   method=getattr(args, "method", "auto"))


def _report(an) -> str:
    d = an.to_dict()
    lines = [
        f"File:      {d['file']}",
        f"Duration:  {d['duration_seconds']:.1f} s",
        f"Tempo:     {d['bpm']} bpm",
        f"Key:       {d['key']}  (confidence {d['key_confidence']:.2f})",
    ]
    chords = [c for c in d["chords"] if c["chord"] != "N"]
    if chords:
        seq = []
        for c in chords:
            if not seq or seq[-1] != (c["chord"], c["roman"]):
                seq.append((c["chord"], c["roman"]))
        lines.append("Chords:    " + " ".join(ch for ch, _ in seq[:24]) + (" ..." if len(seq) > 24 else ""))
        lines.append("Roman:     " + " ".join(r for _, r in seq[:24]) + (" ..." if len(seq) > 24 else ""))
    if d["famous_progressions"]:
        lines.append("Sounds like these famous progressions:")
        for f in d["famous_progressions"][:4]:
            lines.append(f"   - {f['name']} ({f['roman']}) - {f['heard_in']}")
    if d["style_affinity"]:
        lines.append("Closest styles: " + ", ".join(f"{s['style']} ({s['score']:.2f})" for s in d["style_affinity"][:3]))
    if d["melody"]:
        notes = " ".join(n["note"] for n in d["melody"][:32])
        lines.append(f"Melody:    {len(d['melody'])} notes: {notes}{' ...' if len(d['melody']) > 32 else ''}")
    return "\n".join(lines)


def cmd_analyze(args) -> int:
    an = _analyze(args, with_melody=not args.no_melody)
    print(_report(an))
    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_text(json.dumps(an.to_dict(), indent=2, ensure_ascii=False))
        print(f"  wrote {args.json}")
    return 0


def cmd_transcribe(args) -> int:
    from .analysis import TimedNote, analysis_to_score, transcribe_polyphonic

    an = _analyze(args)
    if args.engine == "basic-pitch":
        poly = transcribe_polyphonic(args.vocals or args.track)
        an.melody = [TimedNote(n.start - args.start, n.end - args.start, n.pitch, n.velocity) for n in poly
                     if n.start >= args.start and (args.duration is None or n.start < args.start + args.duration)]
    print(_report(an))
    score = analysis_to_score(an, quantize=args.quantize, melody_instrument=args.instrument,
                              chord_instrument=args.chord_instrument)
    out = Path(args.out)
    stem = Path(args.track).stem
    written = _write_bundle(score, out, stem, not args.no_audio, args.sr)
    report = out / f"{stem}_analysis.json"
    report.write_text(json.dumps(an.to_dict(), indent=2, ensure_ascii=False))
    written.append(report)
    _print_written(written)
    return 0


def cmd_match(args) -> int:
    """Write brand-new melodies over the harmony, key and tempo of an existing track."""
    from .analysis import analysis_to_score
    from .generator import generate
    from .styles import get_style
    from .theory import Key

    an = _analyze(args, with_melody=False)
    print(_report(an))
    style = get_style(args.style)
    base = analysis_to_score(an, quantize=16)
    timeline = [c for c in base.chords]
    # fill no-chord gaps with the tonic so the generator always has harmony to follow
    from .theory import Chord

    tonic_chord = Chord(an.key.tonic, "min" if an.key.is_minorish else "maj")
    for c in timeline:
        if c.chord is None:
            c.chord = tonic_chord
    key = Key.parse(args.key) if args.key else an.key
    backing = None
    if args.mix and not args.no_audio:
        backing = _load_stereo(args.track, args.sr, args.start, args.duration)
    out = Path(args.out)
    parts = tuple(args.parts.split(","))
    for v in range(args.variations):
        seed = (args.seed if args.seed is not None else _random_seed()) + v
        score = generate(style, key=key, bpm=an.bpm, seed=seed, timeline=timeline, parts=parts)
        if args.instrument:
            score.track("Melody") and setattr(score.track("Melody"), "instrument", args.instrument)
        stem = f"{Path(args.track).stem}_{style.name}_seed{seed}"
        print(f"[{v + 1}/{args.variations}] new {style.name} melody in {key.name()} @ {an.bpm:.1f} bpm")
        _print_written(_write_bundle(score, out, stem, not args.no_audio, args.sr, backing=backing))
    return 0


def cmd_render(args) -> int:
    from .midi_io import read_midi
    from .synth import render, write_wav

    score = read_midi(args.midi)
    if args.instrument:
        for t in score.tracks:
            if not t.is_drums:
                t.instrument = args.instrument
    out = Path(args.out or Path(args.midi).with_suffix(".wav"))
    write_wav(out, render(score, sr=args.sr), args.sr)
    print(f"  wrote {out}")
    return 0


# --------------------------------------------------------------------------
# parser
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="melodyverse", description="Listen, transcribe and compose melodies & chords in world styles.")
    p.add_argument("--version", action="version", version=f"melodyverse {__version__}")
    sub = p.add_subparsers(dest="command", required=True)

    def audio_opts(sp):
        sp.add_argument("--sr", type=int, default=44100, help="audio sample rate for rendered WAVs")
        sp.add_argument("--no-audio", action="store_true", help="only write MIDI")

    def track_opts(sp):
        sp.add_argument("track", help="audio file (wav, mp3, flac, ogg, m4a ...)")
        sp.add_argument("--start", type=float, default=0.0, help="start offset in seconds")
        sp.add_argument("--duration", type=float, default=None, help="only analyse this many seconds")

    def melody_opts(sp):
        sp.add_argument("--vocals", help="isolated vocal/lead stem for cleaner melody transcription")
        sp.add_argument("--separate", action="store_true", help="isolate vocals automatically with Demucs (pip install demucs)")
        sp.add_argument("--method", choices=["auto", "salience", "pyin"], default="auto",
                        help="melody tracker: salience (full mixes) or pyin (solo/isolated); auto picks")
        sp.add_argument("--fmin", type=float, default=110.0, help="lowest melody frequency in Hz")
        sp.add_argument("--fmax", type=float, default=1400.0, help="highest melody frequency in Hz")

    sp = sub.add_parser("styles", help="list the style library")
    sp.add_argument("--search", "-s", help="filter by word, e.g. punjabi, spanish, film")
    sp.add_argument("--verbose", "-v", action="store_true")
    sp.set_defaults(func=cmd_styles)

    sp = sub.add_parser("progressions", help="list famous chord progressions")
    sp.add_argument("--key", help="also spell them in this key, e.g. 'A minor'")
    sp.set_defaults(func=cmd_progressions)

    sp = sub.add_parser("generate", help="compose new melody + chords + bass + drums in a style")
    sp.add_argument("--style", "-s", required=True, help="style name or alias (hollywood, punjabi, spanish, english ...)")
    sp.add_argument("--key", "-k", help="e.g. 'D minor', 'F#m', 'E phrygian_dominant', 'C khamaj'")
    sp.add_argument("--bpm", type=float)
    sp.add_argument("--bars", type=int, default=8)
    sp.add_argument("--progression", "-p", help="roman numerals or chord names, e.g. 'i bVI bIII bVII' or 'Am F C G'")
    sp.add_argument("--seed", type=int)
    sp.add_argument("--variations", "-n", type=int, default=1)
    sp.add_argument("--parts", help="comma list from melody,chords,bass,drums")
    sp.add_argument("--instrument", help="override the melody instrument")
    sp.add_argument("--out", "-o", default="output")
    audio_opts(sp)
    sp.set_defaults(func=cmd_generate)

    sp = sub.add_parser("analyze", help="detect tempo, key, chords and melody of a track")
    track_opts(sp)
    melody_opts(sp)
    sp.add_argument("--json", help="also save the full analysis as JSON")
    sp.add_argument("--no-melody", action="store_true", help="skip melody transcription (faster)")
    sp.set_defaults(func=cmd_analyze)

    sp = sub.add_parser("transcribe", help="listen to a track and export its melody & chords as MIDI + audio")
    track_opts(sp)
    melody_opts(sp)
    sp.add_argument("--engine", choices=["melodyverse", "basic-pitch"], default="melodyverse",
                    help="basic-pitch = full polyphonic transcription (pip install basic-pitch)")
    sp.add_argument("--quantize", type=int, choices=[4, 8, 16, 32], help="snap notes to a 1/N grid")
    sp.add_argument("--instrument", default="piano", help="instrument for the melody audio")
    sp.add_argument("--chord-instrument", default="strings")
    sp.add_argument("--out", "-o", default="output")
    audio_opts(sp)
    sp.set_defaults(func=cmd_transcribe)

    sp = sub.add_parser("match", help="write NEW melodies in any style over a track's own chords, key and tempo")
    track_opts(sp)
    sp.add_argument("--style", "-s", required=True)
    sp.add_argument("--key", help="override detected key/mode")
    sp.add_argument("--seed", type=int)
    sp.add_argument("--variations", "-n", type=int, default=3)
    sp.add_argument("--parts", default="melody", help="comma list from melody,chords,bass,drums")
    sp.add_argument("--instrument", help="override the melody instrument")
    sp.add_argument("--mix", action="store_true", help="also write the new melody mixed over the original track")
    sp.add_argument("--out", "-o", default="output")
    audio_opts(sp)
    sp.set_defaults(func=cmd_match)

    sp = sub.add_parser("render", help="render any MIDI file to WAV with the built-in synth")
    sp.add_argument("midi")
    sp.add_argument("--instrument", help="force one instrument for all non-drum tracks")
    sp.add_argument("--out", "-o")
    sp.add_argument("--sr", type=int, default=44100)
    sp.set_defaults(func=cmd_render)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (KeyError, ValueError, RuntimeError, FileNotFoundError) as e:
        msg = e.args[0] if isinstance(e, KeyError) and e.args else e
        print(f"error: {msg}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
