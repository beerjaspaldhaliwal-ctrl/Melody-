// Melodyverse core — listen to audio: tempo/beats, key & mode, chords, melody transcription.
// Pure C++17 port of melodyverse/analysis.py (own FFT, no dependencies).
#pragma once

#include "Score.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace mv
{

struct TimedChord
{
    double start = 0, end = 0;  // seconds
    std::optional<Chord> chord;
    double confidence = 0;
};

struct TimedNote
{
    double start = 0, end = 0;  // seconds
    int pitch = 60;
    int velocity = 90;
};

struct FamousMatch { std::string name, roman, heardIn; };

struct Analysis
{
    double duration = 0;
    double bpm = 120;
    std::vector<double> beatTimes;
    Key key;
    double keyConfidence = 0;
    std::vector<TimedChord> chords;
    std::vector<TimedNote> melody;

    std::vector<std::string> romans() const;
    std::vector<FamousMatch> famous() const;
    std::vector<std::pair<std::string, double>> styleAffinity() const;  // best first
    std::string chordChart (size_t maxChords = 24) const;               // "Am F C G"
};

struct AnalyzeOptions
{
    double fixedBpm = 0;           // > 0: trust this tempo (e.g. the host's) instead of estimating
    bool melody = true;
    double fminHz = 110, fmaxHz = 1400;
    std::function<void (float)> progress;    // 0..1
    const std::atomic<bool>* cancel = nullptr;
};

// `mono` is any sample rate; it is resampled internally to 22.05 kHz.
Analysis analyze (const float* mono, size_t numSamples, double sampleRate, const AnalyzeOptions& options = {});

// Convert to a Score in beats (aligned with the audio: beat 0 = second 0). quantize = 0 or 4/8/16/32.
Score analysisToScore (const Analysis& a, int quantize = 16, const std::string& melodyInstrument = "piano",
                       const std::string& chordInstrument = "strings");

// Key & mode implied by a chord progression (beats), e.g. from an uploaded chord MIDI file.
Key keyFromChords (const std::vector<ChordEvent>& timeline);

// Chord timeline in beats for writing new melodies over a track's harmony (gaps filled with the tonic).
std::vector<ChordEvent> harmonyTimeline (const Analysis& a, int quantize = 16);

// Exposed for tests.
void fft (std::vector<float>& re, std::vector<float>& im);  // in-place radix-2, size = power of two
std::vector<float> resample (const float* x, size_t n, double fromRate, double toRate);

} // namespace mv
