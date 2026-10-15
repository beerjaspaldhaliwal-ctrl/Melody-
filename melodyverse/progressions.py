"""Famous chord progressions from around the world, and helpers to spot them in a track."""

from __future__ import annotations

from .theory import Chord, Key

# name -> (roman numerals, where you hear it)
FAMOUS: dict[str, tuple[str, str]] = {
    "Axis / four-chord pop": ("I V vi IV", "Global pop, rock, Punjabi pop, K-pop"),
    "Sensitive / emo pop": ("vi IV I V", "Pop ballads, EDM drops, Bollywood"),
    "50s doo-wop": ("I vi IV V", "1950s rock'n'roll, soul, early Bollywood"),
    "Andalusian cadence": ("i bVII bVI V", "Flamenco, Spanish & Latin music, surf rock"),
    "Phrygian flamenco": ("iv bIII bII I", "Flamenco (por medio / por arriba)"),
    "Epic minor loop": ("i bVI bIII bVII", "Film trailers, reggaeton, EDM, trap"),
    "Aeolian vamp": ("i bVII bVI bVII", "Rock anthems, Punjabi sad songs, film"),
    "Mario / heroic cadence": ("bVI bVII I", "Film & game scores, rock finales"),
    "Mixolydian rock vamp": ("I bVII IV I", "Classic rock, bhangra, Celtic"),
    "Royal road (Ōdō)": ("IV V iii vi", "J-pop, anime, K-pop"),
    "Pachelbel canon": ("I V vi iii IV I IV V", "Baroque, wedding music, pop ballads"),
    "ii-V-I": ("ii V I", "Jazz, bossa nova, lo-fi, R&B"),
    "Minor ii-V-i": ("iiø V i", "Jazz, tango, film noir"),
    "12-bar blues": ("I IV I I IV IV I I V IV I V", "Blues, rock'n'roll, country"),
    "Plagal / gospel amen": ("IV I", "Gospel, hymns, soul"),
    "Minor plagal": ("IV iv I", "Gospel, Beatles, film"),
    "Lydian wonder": ("I II", "Film fantasy scores, dream pop"),
    "Chromatic mediant": ("I bVI", "Film scores (magic, awe)"),
    "Phrygian tension": ("i bII", "Horror, metal, Middle-Eastern, trap"),
    "Circle of fifths": ("vi ii V I", "Jazz, baroque, Bollywood classics"),
    "Classic I-IV-V": ("I IV V", "Folk, country, bhangra, reggae"),
}

_DEGREE_NAMES = ["I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"]
_MINOR_QUALITIES = {"min", "m7", "m6", "m9", "madd9", "mmaj7", "dim", "dim7", "m7b5"}


def roman(chord: Chord, key: Key) -> str:
    deg = (chord.root - key.tonic) % 12
    name = _DEGREE_NAMES[deg]
    acc = name[:-len(name.lstrip("b#"))] if name[0] in "b#" else ""
    num = name[len(acc):]
    if chord.quality in _MINOR_QUALITIES:
        num = num.lower()
    suffix = {"dim": "o", "dim7": "o7", "m7b5": "ø7", "aug": "+", "7": "7", "m7": "7", "maj7": "maj7",
              "9": "9", "m9": "9", "sus2": "sus2", "sus4": "sus4", "5": "5"}.get(chord.quality, "")
    return acc + num + suffix


def _simplify(numeral: str) -> str:
    """Strip extensions so 'V7' matches 'V' and 'iiø7' matches 'ii'-ish patterns."""
    out = numeral
    for suf in ("maj7", "maj9", "sus2", "sus4", "o7", "ø7", "ø", "add9", "7", "9", "6", "5", "+", "o"):
        if out.endswith(suf):
            out = out[: -len(suf)]
            break
    return out


def find_famous(romans: list[str], min_len: int = 2) -> list[tuple[str, str, str]]:
    """Return famous progressions appearing (cyclically) in the sequence of roman numerals."""
    seq = [_simplify(r) for r in romans]
    dedup = [r for i, r in enumerate(seq) if i == 0 or r != seq[i - 1]]
    if not dedup:
        return []
    text = " " + " ".join(dedup + dedup[: 8]) + " "
    hits = []
    for name, (prog, where) in FAMOUS.items():
        pattern = [_simplify(p) for p in prog.split()]
        if len(pattern) < min_len:
            continue
        if " " + " ".join(pattern) + " " in text:
            hits.append((name, prog, where))
    hits.sort(key=lambda h: -len(h[1].split()))
    return hits


def style_affinity(romans: list[str]) -> list[tuple[str, float]]:
    """Rank library styles by how many of their stock progressions' chord pairs appear in `romans`."""
    from .styles import STYLES

    seq = [_simplify(r) for r in romans]
    dedup = [r for i, r in enumerate(seq) if i == 0 or r != seq[i - 1]]
    pairs = {(a, b) for a, b in zip(dedup, dedup[1:])}
    vocab = set(dedup)
    scores = []
    for st in STYLES.values():
        best = 0.0
        for prog in st.progressions:
            toks = [_simplify(t.split("/")[0]) for t in prog.split()]
            p_pairs = {(a, b) for a, b in zip(toks, toks[1:]) if a != b}
            if not p_pairs:
                continue
            pair_score = len(p_pairs & pairs) / len(p_pairs)
            vocab_score = len(set(toks) & vocab) / len(set(toks))
            best = max(best, 0.7 * pair_score + 0.3 * vocab_score)
        scores.append((st.name, round(best, 3)))
    scores.sort(key=lambda s: -s[1])
    return scores
