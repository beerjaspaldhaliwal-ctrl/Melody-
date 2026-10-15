"""Melodyverse: listen to any track, transcribe its melody & chords to MIDI/audio,
and write new melodies in the musical languages of the world."""

from .generator import generate
from .score import ChordEvent, Note, Score, Track
from .styles import STYLES, get_style
from .theory import Chord, Key

__version__ = "0.1.0"
__all__ = ["generate", "Score", "Track", "Note", "ChordEvent", "STYLES", "get_style", "Chord", "Key", "__version__"]
