// Melodyverse core — score data model shared by generator, analyser, player and exporters.
#pragma once

#include "Theory.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mv
{

struct Note
{
    double start = 0;     // beats
    double duration = 0;  // beats
    int pitch = 60;
    int velocity = 90;
    double bendFrom = 0;  // semitones: glide from pitch+bendFrom to pitch (meend / blue note)
    double end() const { return start + duration; }
};

struct ChordEvent
{
    double start = 0, duration = 0;  // beats
    std::optional<Chord> chord;       // empty = no chord
};

struct DrumHit
{
    double beat = 0;
    std::string name;
    int velocity = 100;
};

struct Track
{
    std::string name;        // "Melody", "Chords", "Bass", "Drums"
    std::string instrument;  // synth patch name, e.g. "sitar"
    std::vector<Note> notes;
    bool isDrums = false;
    std::vector<DrumHit> hits;
};

struct Score
{
    double bpm = 120;
    Key key;
    std::vector<Track> tracks;
    std::vector<ChordEvent> chords;
    std::string style;
    double lengthBeats = 0;  // loop length (whole bars)

    Track* track (const std::string& name)
    {
        for (auto& t : tracks) if (t.name == name) return &t;
        return nullptr;
    }
    const Track* track (const std::string& name) const
    {
        for (auto& t : tracks) if (t.name == name) return &t;
        return nullptr;
    }
    double contentEnd() const
    {
        double e = 0;
        for (auto& t : tracks)
        {
            for (auto& n : t.notes) e = std::max (e, n.end());
            for (auto& h : t.hits) e = std::max (e, h.beat + 0.25);
        }
        for (auto& c : chords) e = std::max (e, c.start + c.duration);
        return e;
    }
};

} // namespace mv
