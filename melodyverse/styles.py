"""The world style library.

Each style captures the *musical language* of a tradition: its scales, its
stock chord progressions (which are common property, not copyrighted works),
melodic rhythm cells, comping and bass patterns, drum grooves, ornaments and
instrument colours. The generator combines these to write new, original
melodies in that idiom.

Rhythm grids are 16 steps per 4/4 bar (16th notes):
    x  onset (normal)      X  accented onset
    -  sustain previous    .  rest
Bass grids may use 1 3 5 8 (chord root/third/fifth/octave) and c
(chromatic approach to the next chord's root) in place of x.
A grid can span several bars (32, 48 ... characters).
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Style:
    name: str
    region: str
    description: str
    mode: str
    default_key: str
    tempo: tuple[int, int]
    progressions: list[str]
    melody_rhythms: list[str]
    chord_pattern: str
    bass_pattern: str
    drums: dict[str, str] = field(default_factory=dict)
    beats_per_chord: float = 4.0
    chord_style: str = "block"  # block | arp | strum
    # Melodic interval preference in *scale steps*: {steps: weight}
    step_weights: dict[int, float] = field(default_factory=lambda: {0: 1.0, 1: 5.0, 2: 3.0, 3: 1.2, 4: 1.0, 5: 0.4, 7: 0.5})
    melody_range: tuple[int, int] = (60, 79)
    contour: str = "arch"  # arch | ascending | descending | wave
    ornaments: tuple[str, ...] = ()
    ornament_rate: float = 0.25
    swing: float = 0.0  # 0..0.33, delays off-beat 16ths/8ths
    instruments: dict[str, str] = field(default_factory=lambda: {"melody": "piano", "chords": "piano", "bass": "bass"})
    phrase_form: str = "ABAC"
    chord_octave: int = 60
    tags: tuple[str, ...] = ()


def _s(**kw) -> Style:
    return Style(**kw)


STYLES: dict[str, Style] = {}


def register(style: Style) -> Style:
    STYLES[style.name] = style
    return style


# --------------------------------------------------------------------------
# Hollywood / cinematic
# --------------------------------------------------------------------------
register(_s(
    name="hollywood_epic", region="Hollywood",
    description="Epic trailer / blockbuster score: driving string ostinato, minor modal progressions, wide heroic leaps.",
    mode="minor", default_key="D minor", tempo=(70, 100),
    progressions=["i bVI bIII bVII", "i bVI bVII i", "i bVII bVI bVII", "i iv bVI bVII", "bVI bVII i i",
                  "i bIII bVII iv"],
    melody_rhythms=["x-------x---x---", "x-----x-x-------", "x-----------x-x-", "x---x---x-------",
                    "x-------x-------"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="arp",
    bass_pattern="X-------x---x---",
    drums={"taiko": "X-----x-X-x-----", "tom": "--------x-x-x-xx", "snare": "--------X-------"},
    step_weights={0: 0.6, 1: 4.0, 2: 2.5, 3: 2.0, 4: 2.2, 5: 0.6, 7: 1.4},
    melody_range=(57, 81), contour="ascending", ornaments=(), instruments={"melody": "brass", "chords": "strings", "bass": "cello"},
    phrase_form="AABA", tags=("film", "orchestral", "trailer", "zimmer", "epic", "battle"),
))
register(_s(
    name="hollywood_heroic", region="Hollywood",
    description="Golden-age adventure fanfare: bright major key, borrowed bVII/bVI chords, open fifth and octave leaps.",
    mode="major", default_key="C major", tempo=(100, 132),
    progressions=["I bVII IV I", "I bVI bVII I", "I III IV iv", "I V/3 vi IV", "I IV I V"],
    melody_rhythms=["x--xx-------x---", "x---x--xx-------", "x--xx--xx---x---", "x-----x-x---x---"],
    chord_pattern="X---x---x---x---", chord_style="block",
    bass_pattern="X---5---x---5---",
    drums={"snare": "x-xxx-x-x-xxx-x-", "kick": "X-------X-------", "openhat": "------------x---"},
    step_weights={0: 1.2, 1: 3.5, 2: 2.5, 3: 2.0, 4: 3.0, 5: 0.5, 7: 2.0},
    melody_range=(60, 84), contour="arch", instruments={"melody": "brass", "chords": "strings", "bass": "cello"},
    phrase_form="AABA", tags=("film", "adventure", "fanfare", "williams", "hero"),
))
register(_s(
    name="hollywood_wonder", region="Hollywood",
    description="Fantasy / magic wonder: Lydian colour, chromatic-mediant shifts, shimmering arpeggios.",
    mode="lydian", default_key="F lydian", tempo=(70, 96),
    progressions=["I II I II", "I bVI I III", "I II iv I", "Imaj7 II vi7 V"],
    melody_rhythms=["x-----x-x-------", "x---x---x---x---", "x-------x-x-x---", "x-x-x---x-------"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="arp",
    bass_pattern="x---------------",
    drums={},
    step_weights={0: 0.5, 1: 4.0, 2: 3.0, 3: 1.5, 4: 2.0, 7: 0.8},
    melody_range=(62, 86), contour="ascending", ornaments=("trill",), ornament_rate=0.1,
    instruments={"melody": "flute", "chords": "celesta", "bass": "strings"},
    tags=("film", "fantasy", "magic", "whimsical", "disney"),
))
register(_s(
    name="cinematic_suspense", region="Hollywood",
    description="Thriller / horror tension: semitone clusters, Phrygian bII, sparse unsettling melody.",
    mode="hungarian_minor", default_key="C hungarian_minor", tempo=(60, 84),
    progressions=["i bII i bII", "i i bII i", "i bvi i #ivo", "i iv i bII"],
    melody_rhythms=["x-----------x---", "x-------x-------", "--------x-------", "x---------x-x---"],
    chord_pattern="x---------------", chord_style="block",
    bass_pattern="x---------------",
    drums={"taiko": "x---------------", "tom": "------------x---"},
    step_weights={0: 2.0, 1: 5.0, 2: 1.0, 3: 1.5, 4: 0.5, 6: 1.0},
    melody_range=(55, 76), contour="wave", instruments={"melody": "strings", "chords": "pad", "bass": "cello"},
    tags=("film", "horror", "thriller", "dark", "tension"),
))
register(_s(
    name="film_emotional", region="Hollywood",
    description="Emotional piano-and-strings cue: rising sequences, suspensions, ends on the tonic.",
    mode="major", default_key="Eb major", tempo=(60, 80),
    progressions=["vi IV I V", "I V vi iii IV I IV V", "IV I/3 ii V", "I V/3 vi IV", "vi7 IVmaj7 I V"],
    melody_rhythms=["x-----x-x-------", "x---x---x-----x-", "x-------x---x-x-", "x-----------x---"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="arp",
    bass_pattern="x-------x-------",
    step_weights={0: 1.0, 1: 5.0, 2: 3.0, 3: 1.5, 4: 1.2, 5: 0.6, 7: 0.5},
    melody_range=(60, 81), contour="arch", instruments={"melody": "strings", "chords": "piano", "bass": "cello"},
    tags=("film", "sad", "emotional", "piano", "ballad", "drama"),
))

# --------------------------------------------------------------------------
# Spanish & Latin
# --------------------------------------------------------------------------
register(_s(
    name="spanish_flamenco", region="Spain",
    description="Flamenco / rumba flamenca: Andalusian cadence in Phrygian dominant, rasgueado strums, b2 turns.",
    mode="phrygian_dominant", default_key="E phrygian_dominant", tempo=(96, 132),
    progressions=["iv bIII bII I", "I bII I bII", "iv bIII bII I", "bII I iv I"],
    melody_rhythms=["x-xx-x-xx-------", "x---xxx-x---x---", "x-x-x-xxx-------", "xxx-x---x-x-x---"],
    chord_pattern="X-xxX-x-X-xxX-x-", chord_style="strum",
    bass_pattern="x-----x-x-------",
    drums={"clap": "--x---x---x---xx", "cajon_low": "x-----x-x-------", "cajon_high": "----x-------x---"},
    step_weights={0: 1.5, 1: 6.0, 2: 2.0, 3: 1.0, 4: 0.6},
    melody_range=(59, 79), contour="descending", ornaments=("flamenco_turn",), ornament_rate=0.35,
    instruments={"melody": "nylon", "chords": "nylon", "bass": "bass"},
    tags=("spanish", "flamenco", "gypsy", "andalusian", "guitar", "rumba"),
))
register(_s(
    name="latin_reggaeton", region="Latin America / Puerto Rico",
    description="Reggaeton / urbano latino: dembow groove, minor four-chord loop, syncopated hooky vocal melody.",
    mode="minor", default_key="A minor", tempo=(88, 100),
    progressions=["i bVI bIII bVII", "i bVII bVI bVII", "i iv bVII bIII", "iv i bVII bVI"],
    melody_rhythms=["x--x--x-x-x-x---", "--x-x-x-x--x--x-", "x--x--x---x-x-x-", "x-x--x--x-x-----"],
    chord_pattern="x--x--x---x--x--", chord_style="block",
    bass_pattern="X--x--x-X--x--x-",
    drums={"kick": "X---x---X---x---", "snare": "---x--x----x--x-", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 3.0, 1: 5.0, 2: 2.5, 3: 0.8, 4: 0.6},
    melody_range=(57, 76), contour="wave", instruments={"melody": "lead", "chords": "pad", "bass": "sub808"},
    phrase_form="AABB", tags=("latin", "reggaeton", "spanish", "urbano", "dembow", "perreo"),
))
register(_s(
    name="latin_bachata", region="Dominican Republic",
    description="Bachata: romantic minor-key requinto lines, syncopated guitar arpeggios.",
    mode="minor", default_key="E minor", tempo=(120, 140),
    progressions=["i iv V i", "i bVII bVI V", "i iv bVII bIII"],
    melody_rhythms=["x-x-x-x-x---x---", "x-xx-x-x-x------", "--x-x-x-x-x-x---"],
    chord_pattern="x-x-x-xxx-x-x-xx", chord_style="arp",
    bass_pattern="---x--x----x--x-",
    drums={"cajon_high": "x-x-x-x-x-x-x-x-", "cajon_low": "x-------x-------", "shaker": "xxxxxxxxxxxxxxxx"},
    step_weights={0: 1.5, 1: 6.0, 2: 3.0, 3: 0.8, 4: 0.5},
    melody_range=(62, 81), contour="wave", ornaments=("flamenco_turn",), ornament_rate=0.15,
    instruments={"melody": "pluck", "chords": "nylon", "bass": "bass"},
    tags=("latin", "bachata", "romantic", "guitar"),
))
register(_s(
    name="bossa_nova", region="Brazil",
    description="Bossa nova: jazz extended chords, gentle syncopated guitar, smooth stepwise melodies.",
    mode="major", default_key="F major", tempo=(120, 145),
    progressions=["Imaj7 vi7 ii7 V7", "ii7 V7 Imaj7 Imaj7", "Imaj7 bIImaj7 ii7 V7", "iii7 VI7 ii7 V7"],
    melody_rhythms=["x--x--x---x-x---", "x-x--x--x--x----", "--x--x--x---x---"],
    chord_pattern="x--x--x---x--x--", chord_style="block",
    bass_pattern="1--5--5-1--5--5-",
    drums={"rim": "x--x--x---x--x--", "shaker": "xxxxxxxxxxxxxxxx", "kick": "x-----x-x-----x-"},
    step_weights={0: 1.5, 1: 6.0, 2: 3.0, 3: 1.0, 4: 0.8, 5: 0.5},
    melody_range=(60, 79), contour="wave", instruments={"melody": "flute", "chords": "nylon", "bass": "bass"},
    chord_octave=58, tags=("latin", "brazil", "bossa", "jazz", "samba"),
))

# --------------------------------------------------------------------------
# Punjabi & South Asian
# --------------------------------------------------------------------------
register(_s(
    name="punjabi_bhangra", region="Punjab (India/Pakistan)",
    description="Bhangra: dhol chaal groove, tumbi riffs, Khamaj/Mixolydian colour, murki ornaments, call-and-response hooks.",
    mode="khamaj", default_key="C khamaj", tempo=(92, 110),
    progressions=["I bVII I bVII", "I IV bVII I", "I I bVII bVII", "vi IV I V", "I bVII IV I"],
    melody_rhythms=["x-xx-x-xx-x-x---", "x-x-x-xxx-------", "x--x--x-x-x-x-x-", "xx-x-x-x--x-x---"],
    chord_pattern="x--x--x-x--x--x-", chord_style="block",
    bass_pattern="X--x--x-X--x--x-",
    drums={"dhol_low": "X-----x-X-----x-", "dhol_high": "--x-x--x--x-x--x", "clap": "----x-------x---",
           "shaker": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 3.0, 1: 6.0, 2: 2.5, 3: 0.8, 4: 0.8, 7: 0.3},
    melody_range=(62, 84), contour="arch", ornaments=("murki",), ornament_rate=0.3,
    instruments={"melody": "tumbi", "chords": "harmonium", "bass": "bass"},
    phrase_form="AABA", tags=("punjabi", "bhangra", "desi", "dhol", "tumbi", "indian"),
))
register(_s(
    name="punjabi_sad", region="Punjab (India/Pakistan)",
    description="Punjabi sad / romantic ballad: Kafi thaat, slow sustained lines with meend glides and murki.",
    mode="kafi", default_key="A kafi", tempo=(66, 88),
    progressions=["i bVII bVI bVII", "i iv bVII bIII", "i bVII i iv", "bVI bVII i i"],
    melody_rhythms=["x-------x---x---", "x-----x-x-x-----", "x---x-x-x-------", "x-x-x-------x---"],
    chord_pattern="x-------x-------", chord_style="arp",
    bass_pattern="x-------x-------",
    drums={"tabla_dha": "x-----x---x-----", "tabla_na": "---x-----x---x--"},
    step_weights={0: 2.0, 1: 7.0, 2: 2.5, 3: 0.8, 4: 0.6},
    melody_range=(60, 79), contour="descending", ornaments=("meend", "murki"), ornament_rate=0.35,
    instruments={"melody": "bansuri", "chords": "strings", "bass": "bass"},
    tags=("punjabi", "sad", "romantic", "desi", "sufi", "indian"),
))
register(_s(
    name="bollywood_romantic", region="India",
    description="Bollywood romantic song: Bilawal (major), lush strings, tabla groove, ornamented stepwise melody.",
    mode="bilawal", default_key="D bilawal", tempo=(78, 100),
    progressions=["I vi IV V", "vi IV I V", "I iii IV V", "I V vi IV"],
    melody_rhythms=["x-x-x---x-x-x---", "x---x-x-x-------", "x-x-x-x-x---x---", "--x-x-x-x-------"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="arp",
    bass_pattern="x-------x---x---",
    drums={"tabla_dha": "x-----x-x-------", "tabla_na": "--x-x-----x-x-x-", "shaker": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 2.0, 1: 7.0, 2: 3.0, 3: 1.0, 4: 0.8},
    melody_range=(62, 81), contour="arch", ornaments=("murki",), ornament_rate=0.25,
    instruments={"melody": "sitar", "chords": "strings", "bass": "bass"},
    tags=("bollywood", "hindi", "indian", "desi", "romantic", "filmi"),
))
register(_s(
    name="indian_classical", region="India",
    description="Raga-style alap/gat feel over a drone: Bhairav thaat, meend and gamak ornaments.",
    mode="bhairav", default_key="C bhairav", tempo=(60, 90),
    progressions=["I I I I", "I I bII I"],
    melody_rhythms=["x-------x-------", "x---x---x---x---", "x-x-x-x-x-------", "x---------x-x---"],
    chord_pattern="x---------------", chord_style="block",
    bass_pattern="x---------------",
    drums={"tabla_dha": "x-------x-x-----", "tabla_na": "----x-x-----x-x-"},
    step_weights={0: 1.5, 1: 8.0, 2: 2.0, 3: 0.6, 4: 0.5},
    melody_range=(55, 79), contour="ascending", ornaments=("meend", "murki"), ornament_rate=0.4,
    instruments={"melody": "sitar", "chords": "drone", "bass": "drone"},
    tags=("indian", "raga", "classical", "hindustani"),
))

# --------------------------------------------------------------------------
# English-language pop / rock / urban
# --------------------------------------------------------------------------
register(_s(
    name="english_pop", region="UK / USA",
    description="Modern chart pop: four-chord loops, syncopated hooks with lots of repeated notes.",
    mode="major", default_key="G major", tempo=(100, 124),
    progressions=["I V vi IV", "vi IV I V", "I vi IV V", "IV I V vi", "I iii vi IV"],
    melody_rhythms=["x-x-x-x---x-x---", "--x-x-xx-x-x----", "x--x--x-x-x-----", "x-x-x---x-x-x-x-"],
    chord_pattern="x---x---x---x---", chord_style="block",
    bass_pattern="x---x---x---x-x-",
    drums={"kick": "X-------X-x-----", "snare": "----X-------X---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 4.0, 1: 5.0, 2: 3.0, 3: 1.0, 4: 0.8},
    melody_range=(60, 77), contour="arch", instruments={"melody": "lead", "chords": "piano", "bass": "bass"},
    phrase_form="AABA", tags=("pop", "english", "chart", "radio", "uk", "usa"),
))
register(_s(
    name="pop_ballad", region="UK / USA",
    description="Power ballad: Canon-style progression, piano arpeggios, a big climactic chorus range.",
    mode="major", default_key="C major", tempo=(66, 80),
    progressions=["I V/3 vi I/5 IV I/3 ii V", "I V vi IV", "vi IV I V", "IV V iii vi"],
    beats_per_chord=4.0,
    melody_rhythms=["x---x-x-x-------", "x-x-x---x-----x-", "x-------x-x-x---", "--x-x-x-x---x---"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="arp",
    bass_pattern="x-------x-------",
    drums={"kick": "x-------x-------", "snare": "----x-------x---", "hat": "x---x---x---x---"},
    step_weights={0: 2.0, 1: 5.0, 2: 3.0, 3: 1.5, 4: 1.0, 5: 0.7},
    melody_range=(60, 81), contour="ascending", instruments={"melody": "piano", "chords": "piano", "bass": "bass"},
    tags=("ballad", "pop", "english", "love", "piano"),
))
register(_s(
    name="rock", region="UK / USA",
    description="Classic / arena rock: power chords, Mixolydian bVII, driving eighth-note bass.",
    mode="mixolydian", default_key="E mixolydian", tempo=(110, 140),
    progressions=["I5 bVII5 IV5 I5", "I5 IV5 V5 IV5", "vi5 IV5 I5 V5", "I5 bIII5 IV5 I5"],
    melody_rhythms=["x-x-x---x-x-x---", "x---x-x-x-x-----", "x-xx-x-x--------"],
    chord_pattern="X-x-x-x-X-x-x-x-", chord_style="block",
    bass_pattern="x-x-x-x-x-x-x-x-",
    drums={"kick": "X-----x-X-x-----", "snare": "----X-------X---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 3.0, 1: 4.0, 2: 3.0, 3: 1.5, 4: 1.5},
    melody_range=(57, 76), contour="arch", instruments={"melody": "lead", "chords": "guitar", "bass": "bass"},
    chord_octave=52, tags=("rock", "guitar", "english", "arena", "punk"),
))
register(_s(
    name="edm_house", region="Global club",
    description="Progressive house / EDM: four-on-the-floor, minor anthem loop, plucky syncopated lead.",
    mode="minor", default_key="F minor", tempo=(124, 128),
    progressions=["i bVI bIII bVII", "bVI bVII i i", "i bVII bVI bVII", "bVI bIII bVII iv"],
    melody_rhythms=["x--x--x---x--x--", "x-x--x--x--x-x--", "x--x--x-x--x--x-"],
    chord_pattern="--x---x---x---x-", chord_style="block",
    bass_pattern="--x---x---x---x-",
    drums={"kick": "X---X---X---X---", "clap": "----X-------X---", "openhat": "--x---x---x---x-", "hat": "xxxxxxxxxxxxxxxx"},
    step_weights={0: 2.0, 1: 4.0, 2: 3.5, 3: 2.0, 4: 1.0, 7: 1.0},
    melody_range=(63, 84), contour="wave", instruments={"melody": "supersaw", "chords": "supersaw", "bass": "sub808"},
    phrase_form="AABB", tags=("edm", "house", "electronic", "dance", "club", "festival"),
))
register(_s(
    name="trap", region="USA (Atlanta)",
    description="Trap / drill: dark harmonic-minor loop, rolling hi-hats, 808 slides, sparse eerie bell melody.",
    mode="harmonic_minor", default_key="C harmonic_minor", tempo=(130, 150),
    progressions=["i bVI i V", "i iv bVI V", "i i bVI bVI", "i bII i bVI"],
    melody_rhythms=["x--x--x---x-----", "x-----x-x---x---", "x--x--x-x-------", "--x--x--x-------"],
    chord_pattern="x---------------", chord_style="block",
    bass_pattern="X------x--x-----",
    drums={"kick": "X------x--x-----", "snare": "--------X-------", "hat": "x-x-x-xxx-x-xxxx", "openhat": "---------------x"},
    step_weights={0: 2.5, 1: 4.0, 2: 3.0, 3: 1.0, 4: 1.0, 7: 0.8},
    melody_range=(67, 86), contour="descending", instruments={"melody": "bell", "chords": "pad", "bass": "sub808"},
    tags=("trap", "hiphop", "rap", "drill", "808", "urban"),
))
register(_s(
    name="rnb_soul", region="USA",
    description="R&B / neo-soul: lush 7th and 9th chords, laid-back swing, melismatic stepwise lines.",
    mode="dorian", default_key="Eb dorian", tempo=(68, 90),
    progressions=["i9 IV9 i9 IV9", "IVmaj7 iii7 ii7 Imaj7", "ii7 V7 Imaj7 vi7", "Imaj7 iii7 IVmaj7 iv7"],
    melody_rhythms=["x--x-x-x--x-----", "--x-xx-x-x------", "x-x--xx---x-x---"],
    chord_pattern="x-------x--x----", chord_style="block",
    bass_pattern="x-----x---x--x--",
    drums={"kick": "x-----x---x-----", "snare": "----x-------x---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 2.0, 1: 6.0, 2: 3.0, 3: 1.2, 4: 0.8},
    melody_range=(60, 79), contour="wave", ornaments=("meend",), ornament_rate=0.15, swing=0.2,
    instruments={"melody": "epiano", "chords": "epiano", "bass": "bass"}, chord_octave=57,
    tags=("rnb", "soul", "neo-soul", "urban", "smooth"),
))
register(_s(
    name="lofi", region="Global internet",
    description="Lo-fi hip-hop: dusty jazzy 7ths, lazy swing, gentle pentatonic noodles.",
    mode="major", default_key="D major", tempo=(70, 90),
    progressions=["ii7 V7 Imaj7 vi7", "IVmaj7 iii7 ii7 Imaj7", "Imaj7 vi7 ii7 V7", "vi7 ii7 V7 Imaj7"],
    melody_rhythms=["x---x-x---x-----", "--x-x---x-x-----", "x-x-----x--x----"],
    chord_pattern="x-------x-------", chord_style="block",
    bass_pattern="x-----x---------",
    drums={"kick": "x-----x---x-----", "snare": "----x-------x---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 1.5, 1: 5.0, 2: 3.5, 3: 1.5, 4: 1.0},
    melody_range=(64, 81), contour="wave", swing=0.25,
    instruments={"melody": "epiano", "chords": "epiano", "bass": "bass"}, chord_octave=57,
    tags=("lofi", "chill", "study", "jazzy", "beats"),
))
register(_s(
    name="afrobeats", region="West Africa (Nigeria/Ghana)",
    description="Afrobeats / afropop: tresillo-based groove, bright major loop, playful syncopated call-and-response.",
    mode="major", default_key="A major", tempo=(100, 115),
    progressions=["I IV vi V", "ii V I I", "vi IV I V", "I iii IV IV"],
    melody_rhythms=["x--x--x---x-x---", "--x--x--x-x-----", "x-x--x--x--x----"],
    chord_pattern="--x--x----x--x--", chord_style="block",
    bass_pattern="x--x--x---x-----",
    drums={"kick": "x--x--x---x-----", "rim": "---x--x----x--x-", "shaker": "x-xxx-xxx-xxx-xx", "clap": "----x-------x---"},
    step_weights={0: 3.0, 1: 5.0, 2: 3.0, 3: 1.0, 4: 0.8},
    melody_range=(62, 79), contour="wave", instruments={"melody": "pluck", "chords": "epiano", "bass": "bass"},
    phrase_form="AABB", tags=("afrobeats", "afro", "african", "nigeria", "amapiano", "dance"),
))
register(_s(
    name="reggae", region="Jamaica",
    description="Reggae: one-drop groove, off-beat skank chords, relaxed melodic bass.",
    mode="major", default_key="G major", tempo=(70, 90),
    progressions=["I IV I V", "I vi IV V", "ii I ii I", "I IV V IV"],
    melody_rhythms=["x---x-x---x-----", "--x-x-x-x-------", "x-x-x---x---x---"],
    chord_pattern="----x-------x---", chord_style="block",
    bass_pattern="x--x--x-x-------",
    drums={"kick": "--------X-------", "rim": "--------x-------", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 2.5, 1: 5.0, 2: 3.0, 3: 1.0, 4: 0.8},
    melody_range=(60, 77), contour="wave", swing=0.15,
    instruments={"melody": "organ", "chords": "guitar", "bass": "bass"},
    tags=("reggae", "jamaica", "dub", "ska", "roots"),
))
register(_s(
    name="jazz_swing", region="USA",
    description="Jazz standard style: ii-V-I cycles, swing eighths, walking bass, chromatic approach tones.",
    mode="major", default_key="Bb major", tempo=(120, 180),
    progressions=["ii7 V7 Imaj7 Imaj7", "Imaj7 vi7 ii7 V7", "iii7 VI7 ii7 V7", "Imaj7 I7 IVmaj7 iv7"],
    melody_rhythms=["x-x-x-x-x---x---", "--x-x-x-x-x-x---", "x---x-x-x-x-----"],
    chord_pattern="x-----x-----x---", chord_style="block",
    bass_pattern="1---3---5---c---",
    drums={"rim": "----x-------x---", "hat": "x---x-xx--x-x-xx", "kick": "x---------------"},
    step_weights={0: 1.0, 1: 6.0, 2: 3.0, 3: 1.5, 4: 1.0, 5: 0.6},
    melody_range=(60, 81), contour="wave", swing=0.33,
    instruments={"melody": "brass", "chords": "piano", "bass": "upright"}, chord_octave=58,
    tags=("jazz", "swing", "bebop", "standard"),
))
register(_s(
    name="blues", region="USA (Mississippi / Chicago)",
    description="12-bar blues: dominant 7ths, blues scale with bent blue notes, shuffle feel.",
    mode="blues", default_key="A blues", tempo=(80, 120),
    progressions=["I7 IV7 I7 I7 IV7 IV7 I7 I7 V7 IV7 I7 V7"],
    melody_rhythms=["x---x-x---------", "x-x-x---x-------", "--x-x-x-x-------", "x-----x-x-x-----"],
    chord_pattern="x---x---x---x---", chord_style="block",
    bass_pattern="1-3-5-8-5-3-1-3-",
    drums={"kick": "x-------x-------", "snare": "----x-------x---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 2.0, 1: 5.0, 2: 3.0, 3: 1.0, 4: 1.0},
    melody_range=(57, 76), contour="descending", ornaments=("blue_note",), ornament_rate=0.3, swing=0.33,
    instruments={"melody": "guitar", "chords": "piano", "bass": "bass"},
    tags=("blues", "shuffle", "delta", "chicago", "guitar"),
))
register(_s(
    name="gospel", region="USA",
    description="Gospel: rich passing chords, plagal IV-iv-I moves, soaring call-and-response.",
    mode="major", default_key="Ab major", tempo=(70, 100),
    progressions=["I IV/I I I7", "IV iv I vi7", "I iii7 IV V7", "ii7 V7 iii7 VI7"],
    melody_rhythms=["x---x-x-x-------", "x-x-x-x-x---x---", "--x-x---x-x-x---"],
    chord_pattern="x---x---x---x---", chord_style="block",
    bass_pattern="x-----x-x-------",
    drums={"kick": "x-------x-x-----", "clap": "----x-------x---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 2.0, 1: 5.0, 2: 3.0, 3: 1.5, 4: 1.2},
    melody_range=(60, 81), contour="ascending", ornaments=("meend",), ornament_rate=0.15, swing=0.2,
    instruments={"melody": "organ", "chords": "organ", "bass": "bass"},
    tags=("gospel", "church", "choir", "soul"),
))
register(_s(
    name="country", region="USA (Nashville)",
    description="Country / americana: I-IV-V storytelling, major pentatonic melodies, train-beat drums.",
    mode="major_pentatonic", default_key="G major_pentatonic", tempo=(90, 120),
    progressions=["I IV I V", "I V IV I", "I IV vi V", "IV I V I"],
    melody_rhythms=["x-x-x---x-x-x---", "x---x-x-x-------", "x-x-x-x-x---x---"],
    chord_pattern="x---x-x-x---x-x-", chord_style="strum",
    bass_pattern="1---5---1---5---",
    drums={"kick": "x-------x-------", "snare": "----x-------x---", "hat": "xxxxxxxxxxxxxxxx"},
    step_weights={0: 2.5, 1: 6.0, 2: 2.0, 3: 0.8},
    melody_range=(60, 77), contour="arch", instruments={"melody": "guitar", "chords": "guitar", "bass": "bass"},
    tags=("country", "americana", "folk", "nashville"),
))

# --------------------------------------------------------------------------
# Other world traditions
# --------------------------------------------------------------------------
register(_s(
    name="arabic_hijaz", region="Middle East / North Africa",
    description="Maqam Hijaz feel: augmented second, maqsum darbuka rhythm, trills and turns.",
    mode="phrygian_dominant", default_key="D phrygian_dominant", tempo=(90, 112),
    progressions=["I bII I I", "I iv bII I", "I bvii I bII"],
    melody_rhythms=["x-xx-x-xx-------", "x---x-x-xxx-x---", "xxx-x---x-x-x---"],
    chord_pattern="x---------------", chord_style="block",
    bass_pattern="x--x--x-x---x---",
    drums={"darbuka_dum": "x-----x-x-------", "darbuka_tek": "---x-x-----x-xx-", "shaker": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 1.5, 1: 7.0, 2: 2.0, 3: 0.6},
    melody_range=(60, 79), contour="wave", ornaments=("trill", "flamenco_turn"), ornament_rate=0.3,
    instruments={"melody": "oud", "chords": "drone", "bass": "bass"},
    tags=("arabic", "middle-east", "maqam", "hijaz", "turkish", "belly-dance"),
))
register(_s(
    name="celtic", region="Ireland / Scotland",
    description="Celtic folk: Dorian modal tunes, lilting jig swing, ornamented rolls.",
    mode="dorian", default_key="D dorian", tempo=(100, 130),
    progressions=["i bVII i bVII", "i bVII bVI bVII", "i iv bVII i"],
    melody_rhythms=["x-x-x-x-x-x-x-x-", "x---x-x-x-x-x---", "x-x-x---x-x-x---"],
    chord_pattern="x-----x---x-----", chord_style="strum",
    bass_pattern="x-------x-------",
    drums={"tom": "x-----x---x-----", "rim": "----x-------x---"},
    step_weights={0: 1.5, 1: 6.0, 2: 4.0, 3: 1.0, 4: 1.0},
    melody_range=(62, 84), contour="wave", ornaments=("trill",), ornament_rate=0.2, swing=0.25,
    instruments={"melody": "flute", "chords": "guitar", "bass": "bass"},
    tags=("celtic", "irish", "scottish", "folk", "jig"),
))
register(_s(
    name="japanese_anime", region="Japan",
    description="Anime / J-pop: the 'royal road' IVmaj7-V7-iii7-vi progression, fast dramatic melodies.",
    mode="major", default_key="E major", tempo=(130, 170),
    progressions=["IVmaj7 V7 iii7 vi", "vi IV V I", "IV V iii vi ii V I I"],
    melody_rhythms=["x-x-x-x-x---x---", "x-xx-x-x-x-x----", "x---x-x-x-x-x-x-"],
    chord_pattern="x-x-x-x-x-x-x-x-", chord_style="block",
    bass_pattern="x-x-x-x-x-x-x-x-",
    drums={"kick": "X-----x-X-------", "snare": "----X-------X---", "hat": "x-x-x-x-x-x-x-x-"},
    step_weights={0: 1.5, 1: 5.0, 2: 3.5, 3: 1.5, 4: 1.2, 5: 0.6},
    melody_range=(62, 84), contour="ascending", instruments={"melody": "lead", "chords": "piano", "bass": "bass"},
    tags=("anime", "jpop", "japanese", "kpop", "asian"),
))
register(_s(
    name="baroque", region="Europe",
    description="Baroque / classical: Pachelbel-style ground bass, sequences, cadences on V-I.",
    mode="major", default_key="D major", tempo=(80, 110),
    progressions=["I V vi iii IV I IV V", "I IV V I", "i iv V i", "I vi ii V"],
    melody_rhythms=["x-x-x-x-x-x-x-x-", "x---x---x-x-x-x-", "x-x-x-x-x-------"],
    chord_pattern="x---x---x---x---", chord_style="arp",
    bass_pattern="x---x---x---x---",
    step_weights={0: 0.8, 1: 7.0, 2: 3.0, 3: 1.0, 4: 0.8, 7: 0.4},
    melody_range=(62, 84), contour="arch", ornaments=("trill",), ornament_rate=0.1,
    instruments={"melody": "strings", "chords": "harpsichord", "bass": "cello"},
    tags=("classical", "baroque", "bach", "orchestral", "canon"),
))

ALIASES = {
    "hollywood": "hollywood_epic", "film": "hollywood_epic", "cinematic": "hollywood_epic", "epic": "hollywood_epic",
    "heroic": "hollywood_heroic", "fantasy": "hollywood_wonder", "horror": "cinematic_suspense",
    "spanish": "spanish_flamenco", "flamenco": "spanish_flamenco", "latin": "latin_reggaeton",
    "reggaeton": "latin_reggaeton", "bachata": "latin_bachata", "bossa": "bossa_nova",
    "punjabi": "punjabi_bhangra", "bhangra": "punjabi_bhangra", "desi": "punjabi_bhangra",
    "bollywood": "bollywood_romantic", "hindi": "bollywood_romantic", "raga": "indian_classical",
    "english": "english_pop", "pop": "english_pop", "ballad": "pop_ballad", "edm": "edm_house",
    "house": "edm_house", "hiphop": "trap", "rap": "trap", "rnb": "rnb_soul", "soul": "rnb_soul",
    "jazz": "jazz_swing", "afro": "afrobeats", "arabic": "arabic_hijaz", "irish": "celtic",
    "anime": "japanese_anime", "jpop": "japanese_anime", "classical": "baroque",
}


def get_style(name: str) -> Style:
    key = name.strip().lower().replace("-", "_").replace(" ", "_")
    key = ALIASES.get(key, key)
    if key not in STYLES:
        raise KeyError(f"Unknown style {name!r}. Try one of: {', '.join(sorted(STYLES))}")
    return STYLES[key]
