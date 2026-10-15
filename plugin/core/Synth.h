// Melodyverse core — real-time synthesizer (additive/FM voices, synthesized drums, reverb)
// and a loop sequencer that plays a Score in sync with a host transport.
// No allocation on the audio path after prepare().
#pragma once

#include "Score.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace mv
{

enum class Part { Melody = 0, Chords = 1, Bass = 2, Drums = 3, Vocal = 4 };
constexpr int kNumParts = 5;
const char* partName (Part p);
int partFromTrackName (const std::string& name);  // -1 if unknown

std::vector<std::string> instrumentNames();  // all melodic patches
int patchIndex (const std::string& instrument);  // falls back to piano
std::vector<std::string> drumNames();
int drumIndex (const std::string& name);         // -1 if unknown
int gmProgram (const std::string& instrument);
int gmDrumNote (const std::string& drum);

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 64;

    void prepare (double sampleRate);
    void reset();
    void noteOn (int part, int patch, int pitch, int velocity, double bendFrom = 0, double glideSeconds = 0.1);
    void noteOff (int part, int pitch);
    void allNotesOff (bool hard = false);
    void drumHit (int drum, int velocity);
    void render (float* left, float* right, int numSamples);  // adds into buffers
    double sampleRate() const { return sr; }

    std::array<float, kNumParts> partGain { 0.9f, 0.5f, 0.75f, 0.7f, 0.5f };
    float masterGain = 0.8f;
    float reverbMix = 0.16f;

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int part = 0, patch = 0, pitch = 60;
        float vel = 1.0f;
        double t = 0;           // seconds since note on
        double relT = 0;        // seconds since release
        float relLevel = 0;
        double phase = 0;       // fundamental phase [0,1)
        double phase2 = 0, phase3 = 0;  // detuned copies
        double bend = 0, glide = 0.1;
        float lp = 0;           // filter state
        std::array<float, 16> hgain {};
        uint32_t noise = 12345;
        uint64_t age = 0;
    };
    struct DrumVoice { bool active = false; int drum = 0; size_t pos = 0; float gain = 1; };

    float renderVoice (Voice& v);
    float envelope (Voice& v) const;

    double sr = 44100;
    std::array<Voice, kMaxVoices> voices {};
    std::array<DrumVoice, 32> drumVoices {};
    std::vector<std::vector<float>> drumSamples;
    uint64_t ageCounter = 0;

    // Schroeder reverb
    struct Comb { std::vector<float> buf; size_t i = 0; float fb = 0.8f, damp = 0.25f, store = 0; float process (float x); };
    struct Allpass { std::vector<float> buf; size_t i = 0; float process (float x); };
    std::array<Comb, 8> combs;
    std::array<Allpass, 4> allpasses;
};

struct MidiOut
{
    // (sampleOffset, status, data1, data2)
    std::function<void (int, uint8_t, uint8_t, uint8_t)> send;
};

class Player
{
public:
    struct Event
    {
        double beat;
        bool on;
        int part, patch, pitch, velocity;
        double bendFrom, durationBeats;
    };
    struct Sequence
    {
        std::vector<Event> events;
        double length = 16;  // loop length in beats
    };

    // Build a sequence from a score (allocates: do this off the audio thread).
    static std::unique_ptr<Sequence> buildSequence (const Score& score);
    // Real-time safe hand-over: installs `next` and returns the previous sequence,
    // which the caller should destroy off the audio thread. Sounding notes are stopped.
    std::unique_ptr<Sequence> exchange (std::unique_ptr<Sequence> next);
    void setScore (const Score& score) { exchange (buildSequence (score)); }  // convenience, not RT-safe

    void prepare (double sampleRate);
    // Plays the loop for the host block starting at ppqStart.
    void process (double ppqStart, double bpm, bool playing, int numSamples, float* left, float* right, MidiOut* midi);
    void stop (MidiOut* midi, int sampleOffset = 0);

    SynthEngine synth;
    std::array<bool, kNumParts> partEnabled { true, true, true, true, true };
    std::array<int, kNumParts> midiChannel { 0, 1, 2, 9, 3 };  // zero-based: melody 1, chords 2, bass 3, drums 10, vocal guide 4
    bool sendMidi = true;

    const std::vector<Event>& events() const { return seq_->events; }
    double loopLength() const { return seq_->length; }

private:
    void fire (const Event& e, double bpm, MidiOut* midi, int offset);
    std::unique_ptr<Sequence> seq_ = std::make_unique<Sequence>();
    double lastPos_ = -1;
    bool wasPlaying_ = false, needsStop_ = false;
    std::array<std::array<uint8_t, 128>, kNumParts> sounding_ {};
};

// Offline render of a whole score (for WAV export and tests). Returns interleaved-free stereo.
void renderScore (const Score& score, double sampleRate, std::vector<float>& left, std::vector<float>& right,
                  double tailSeconds = 2.0, int loops = 1, float reverb = 0.16f);

} // namespace mv
