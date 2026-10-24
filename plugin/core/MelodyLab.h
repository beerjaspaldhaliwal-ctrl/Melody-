// Melodyverse core — Melody Lab: many different, musically developed melodies over one chord progression.
//
// Each variation has a character (hook, lyrical, syncopated, arpeggio, call & response, Punjabi folk,
// soulful, cinematic). Melodies are built from motifs that are repeated, sequenced to follow the chords,
// inverted and varied; phrases are shaped as question/answer with a planned climax; strong beats land on
// guide tones with smooth voice leading; weak beats use passing, neighbour and approach notes, with
// anticipations across bar lines. Several candidates are written per character and the best one is kept.
#pragma once

#include "Score.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mv
{

struct MelodyCharacterInfo
{
    std::string id, name, description, instrument;
};

const std::vector<MelodyCharacterInfo>& melodyCharacters();

struct MelodyMetrics
{
    double chordToneStrong = 0;  // share of strong-beat notes that are chord tones
    double stepwise = 0;         // share of intervals of a whole tone or less
    double leapRecovery = 0;     // share of leaps (>= 5 semitones) answered by a step back
    double motifCoherence = 0;   // share of bars that reuse a motif rhythm
    int rangeSemitones = 0;
    double notesPerBar = 0;
    bool endsOnTonic = false;
    double score = 0;            // 0..100
};

struct MelodyVariation
{
    int character = 0;
    std::string name, description, instrument, tags;
    std::vector<Note> notes;
    MelodyMetrics metrics;
    uint32_t seed = 0;
};

struct MelodyLabOptions
{
    int count = 8;
    int focusCharacter = -1;  // -1: one of each character; otherwise `count` takes of this character
    uint32_t seed = 1;
    int registerShift = 0;    // -1 low, 0 middle, +1 high
    int candidates = 6;       // melodies written per variation; the best one is kept
};

// Read a chord progression from MIDI notes (block chords or arpeggios), one decision per `window` beats.
std::vector<ChordEvent> chordsFromNotes (const std::vector<Note>& notes, double window = 1.0);

std::vector<MelodyVariation> melodyVariations (const std::vector<ChordEvent>& timeline, const Key& key, const MelodyLabOptions& options);

MelodyMetrics evaluateMelody (const std::vector<Note>& melody, const std::vector<ChordEvent>& timeline, const Key& key);

} // namespace mv
