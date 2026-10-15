// Melodyverse core — music theory: notes, scales, keys, chords, roman numerals.
// Pure C++17, no dependencies. Mirrors melodyverse/theory.py.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mv
{

extern const char* const kNoteNames[12];
extern const char* const kFlatNames[12];

int parsePitchClass (const std::string& name);          // "C#" -> 1, "Eb" -> 3; throws std::invalid_argument
std::string pcName (int pc, bool flats = false);
std::string midiToName (int midi, bool flats = false);   // 60 -> "C4"

const std::vector<int>& scaleIntervals (const std::string& mode);  // throws if unknown
std::vector<std::string> scaleNames();
bool hasScale (const std::string& mode);

const std::vector<int>& chordIntervals (const std::string& quality);
const std::string& chordSuffix (const std::string& quality);
bool hasChordQuality (const std::string& quality);

struct Key
{
    int tonic = 0;               // pitch class
    std::string mode = "major";

    static Key parse (const std::string& text);  // "Am", "F# minor", "E phrygian_dominant", "C khamaj"
    const std::vector<int>& scale() const { return scaleIntervals (mode); }
    std::set<int> pitchClasses() const;
    bool isMinorish() const;
    bool prefersFlats() const;
    std::string name() const;
    std::vector<int> scalePitches (int low, int high) const;
    bool operator== (const Key& o) const { return tonic == o.tonic && mode == o.mode; }
};

struct Chord
{
    int root = 0;
    std::string quality = "maj";
    int bass = -1;  // slash-chord bass pitch class, -1 = none

    static Chord parse (const std::string& text);  // "Am7", "Bbmaj7", "C/G"
    const std::vector<int>& intervals() const { return chordIntervals (quality); }
    std::vector<int> pitchClasses() const;
    bool contains (int pc) const;
    std::string name (bool flats = false) const;
    std::vector<int> voicing (int center = 60, const std::vector<int>* previous = nullptr) const;
    bool isMinor() const;
    bool operator== (const Chord& o) const { return root == o.root && quality == o.quality && bass == o.bass; }
    bool operator!= (const Chord& o) const { return ! (*this == o); }
};

Chord parseRoman (const std::string& token, const Key& key);
std::vector<Chord> parseProgression (const std::string& text, const Key& key);  // roman numerals or chord names
std::string romanName (const Chord& chord, const Key& key);

std::vector<std::string> splitWords (const std::string& text);
std::string toLower (std::string s);

} // namespace mv
