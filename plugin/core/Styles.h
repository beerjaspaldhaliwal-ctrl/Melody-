// Melodyverse core — the world style library (data generated from the Python package).
#pragma once

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mv
{

struct Style
{
    std::string name, region, description, mode, defaultKey;
    int tempoMin = 90, tempoMax = 120;
    std::vector<std::string> progressions;
    std::vector<std::string> melodyRhythms;
    std::string chordPattern, bassPattern;
    std::vector<std::pair<std::string, std::string>> drums;
    double beatsPerChord = 4.0;
    std::string chordStyle = "block";  // block | arp | strum
    std::vector<std::pair<int, double>> stepWeights;
    int melodyLow = 60, melodyHigh = 79;
    std::string contour = "arch";
    std::vector<std::string> ornaments;
    double ornamentRate = 0.25;
    double swing = 0.0;
    std::string melodyInstrument = "piano", chordInstrument = "piano", bassInstrument = "bass";
    std::string phraseForm = "ABAC";
    int chordOctave = 60;
    std::vector<std::string> tags;

    double stepWeight (int steps) const
    {
        for (auto& [k, w] : stepWeights) if (k == steps) return w;
        return steps <= 7 ? 0.05 : 0.0;
    }
    bool hasOrnament (const std::string& o) const
    {
        for (auto& x : ornaments) if (x == o) return true;
        return false;
    }
};

struct FamousProgression
{
    std::string name, roman, heardIn;
};

const std::vector<Style>& styles();
const Style& getStyle (const std::string& nameOrAlias);  // throws std::out_of_range
int styleIndex (const std::string& name);                // -1 if unknown
const std::vector<FamousProgression>& famousProgressions();

namespace data
{
const std::vector<std::pair<std::string, std::vector<int>>>& scales();
const std::vector<std::tuple<std::string, std::vector<int>, std::string>>& chordQualities();
const std::vector<std::pair<std::string, std::string>>& aliases();
} // namespace data

} // namespace mv
