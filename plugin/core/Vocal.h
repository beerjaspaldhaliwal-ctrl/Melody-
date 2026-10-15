// Melodyverse core — deep vocal analysis and vocal-driven arranging.
// Built for sung (monophonic) vocals, with a Hindustani/Punjabi lens: Sa, thaat, swaras,
// meend, murki, kan and vibrato; then ranks chord progressions that fit the vocal and
// writes answer phrases or harmony lines around it.
#pragma once

#include "Analysis.h"
#include "Score.h"

#include <array>
#include <string>
#include <vector>

namespace mv
{

struct VocalOrnament
{
    std::string type;  // "meend", "murki", "kan", "vibrato"
    double start = 0, end = 0;  // seconds
    int pitch = 0;              // the note it decorates / lands on
    double amount = 0;          // meend: semitones (signed); murki: notes; kan: interval; vibrato: rate Hz
};

struct VocalPhrase
{
    double start = 0, end = 0;
    int firstNote = 0, lastNote = 0;  // indices into notes
    int low = 0, high = 0;
    std::string contour;              // rising | falling | arch | level
};

struct VocalReport
{
    double duration = 0, bpm = 120;
    double originSeconds = 0;         // audio time of bar 1 beat 1 in the arrangement
    std::vector<double> beatTimes;
    double hopSeconds = 0;
    std::vector<float> pitchTrack;    // MIDI per frame (NaN = silence), tuning-corrected
    std::vector<TimedNote> notes;
    std::vector<int> bendFrom;        // per note: glide (semitones) into it, 0 = none
    std::vector<bool> ornamental;     // per note: part of a murki/kan (light weight for harmony)
    std::vector<double> attack;       // per note: when the singer started moving to it (glide start)
    std::vector<VocalPhrase> phrases;
    std::vector<VocalOrnament> ornaments;

    Key key;                          // tonic = Sa, mode = thaat name (bilawal, kafi, khamaj ...)
    double keyConfidence = 0;
    std::string thaat, westernMode;
    std::array<double, 12> swaraWeight {};  // share of singing time on each swara, from Sa
    int lowest = 0, highest = 0, comfortLow = 0, comfortHigh = 0;
    std::string voiceType;
    double tuningCents = 0, stabilityCents = 0;
    double vibratoRate = 0, vibratoDepthCents = 0, vibratoShare = 0;
    double notesPerSecond = 0, swing = 0, dynamicRangeDb = 0;
    double avgPhraseSeconds = 0, avgBreathSeconds = 0;
    std::string hookText;
    std::vector<double> hookTimes;
    std::vector<std::string> insights;  // human-readable findings

    int count (const std::string& ornamentType) const;
    double beatOf (double seconds) const { return (seconds - originSeconds) * bpm / 60.0; }
    int bars() const;
};

struct ProgressionOption
{
    std::string name, description, style, roman;
    std::vector<ChordEvent> chords;  // in arrangement beats (bar 1 = beat 0)
    double fit = 0;                  // 0..1 share of the sung notes the chords support
};

enum class VocalMelodyIdea { Answers = 0, HarmonyAbove = 1, HarmonyBelow = 2 };

VocalReport analyzeVocal (const float* mono, size_t numSamples, double sampleRate, const AnalyzeOptions& options = {});
std::vector<ProgressionOption> suggestProgressions (const VocalReport& report, int count = 5);
Score arrangeForVocal (const VocalReport& report, const ProgressionOption& option, VocalMelodyIdea idea, uint32_t seed,
                       const std::string& styleOverride = {});

std::string swaraName (int semitonesFromSa);  // 0 -> "Sa", 1 -> "re", 4 -> "Ga", 6 -> "Ma" ...
std::string formatTime (double seconds);       // 75.3 -> "1:15"

} // namespace mv
