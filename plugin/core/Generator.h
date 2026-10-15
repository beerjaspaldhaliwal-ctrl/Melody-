// Melodyverse core — writes original melodies, comping, bass lines and drum grooves in any style.
// Mirrors melodyverse/generator.py.
#pragma once

#include "Score.h"
#include "Styles.h"

#include <cstdint>
#include <random>

namespace mv
{

struct GenerateOptions
{
    std::string style = "english_pop";
    std::optional<Key> key;          // default: the style's key
    double bpm = 0;                  // 0 = pick from the style's tempo range
    int bars = 8;
    uint32_t seed = 1;
    std::string progression;         // empty = one of the style's progressions
    std::vector<ChordEvent> timeline;  // non-empty = write over this harmony (e.g. from analysis)
    bool melody = true, chords = true, bass = true, drums = true;
};

Score generate (const GenerateOptions& options);

struct GridEvent
{
    double start = 0, length = 0;  // beats, relative to the pattern
    bool accent = false;
    char symbol = 'x';
};

std::vector<GridEvent> parseGrid (const std::string& grid);

class Rng
{
public:
    explicit Rng (uint32_t seed) : engine (seed) {}
    double uniform() { return std::uniform_real_distribution<double> (0.0, 1.0) (engine); }
    int integer (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (engine); }
    template <typename T> const T& choice (const std::vector<T>& v) { return v[(size_t) integer (0, (int) v.size() - 1)]; }

private:
    std::mt19937 engine;
};

const ChordEvent* chordAt (const std::vector<ChordEvent>& timeline, double beat);

} // namespace mv
