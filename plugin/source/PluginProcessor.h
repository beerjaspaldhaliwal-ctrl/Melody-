#pragma once

#include "Analysis.h"
#include "Generator.h"
#include "Synth.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <optional>

// Melodyverse: listens to audio (dropped files or the plugin's input), transcribes melody & chords,
// and composes new parts in 30 world styles. Plays them in sync with the host, sends MIDI out,
// and exports MIDI/WAV for drag-and-drop into the DAW.
class MelodyverseProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    MelodyverseProcessor();
    ~MelodyverseProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Melodyverse"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- compose settings (message thread) ----
    struct Settings
    {
        juce::String style = "hollywood_epic";
        int keyTonic = -1;          // -1 = style default / detected
        juce::String mode;          // "" = style default / detected
        int bars = 8;
        juce::uint32 seed = 1;
        juce::String progression;   // "" = style default
        int quantize = 16;          // 0 = off
        std::array<juce::String, mv::kNumParts> instruments;  // "" = style default
    };
    Settings settings;

    // ---- actions (message thread) ----
    void generateNew (bool newSeed);
    void transcribe();
    void matchStyle();
    void loadAudioFile (const juce::File& file);
    void setCapturing (bool shouldCapture);
    bool isCapturing() const { return capturing.load(); }
    double capturedSeconds() const;
    void setPartInstrument (int part, const juce::String& instrument);

    // ---- export ----
    juce::File writeMidiTemp (int part);  // part -1 = whole arrangement
    bool saveMidi (const juce::File& file, int part = -1);
    bool saveWav (const juce::File& file);

    // ---- state for the UI ----
    mv::Score getScore() const;
    std::optional<mv::Analysis> getAnalysis() const;
    juce::String getStatus() const;
    float getProgress() const { return progress.load(); }
    bool isBusy() const { return busy.load(); }
    int getVersion() const { return version.load(); }
    double getPlayheadBeat() const { return playheadBeat.load(); }
    double getHostBpm() const { return hostBpm.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    juce::String getSourceDescription() const;
    juce::String getAnalysisLabel() const;

    juce::AudioProcessorValueTreeState params;

    static constexpr const char* kPartIds[mv::kNumParts] = { "melody", "chords", "bass", "drums" };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void commitScore (mv::Score score, const juce::String& source);
    void setStatus (const juce::String& s);
    void startAnalysis (std::vector<float> mono, double sampleRate, double fixedBpm, const juce::String& label);
    void timerCallback() override;
    std::vector<mv::ChordEvent> currentTimeline() const;
    mv::Key currentKey (const mv::Style& st) const;

    // audio thread
    mv::Player player;
    juce::SpinLock pendingLock;
    std::unique_ptr<mv::Player::Sequence> pendingSequence, retiredSequence;
    double sampleRateHz = 44100;
    double previewPpq = 0;
    std::atomic<double> playheadBeat { 0 }, hostBpm { 0 }, scoreBpm { 120 };
    juce::MidiBuffer* currentMidi = nullptr;
    mv::MidiOut midiOut;
    std::atomic<bool> hostPlaying { false };

    // capture
    std::vector<float> captureBuffer;
    std::atomic<size_t> captureLength { 0 };
    std::atomic<bool> capturing { false };

    // model
    mutable juce::CriticalSection modelLock;
    mv::Score score;
    std::optional<mv::Analysis> analysis;
    juce::String status { "Ready" }, source { "generated" }, analysisLabel;
    std::atomic<float> progress { 0 };
    std::atomic<bool> busy { false };
    std::atomic<bool> cancelAnalysis { false };
    std::atomic<int> version { 0 };

    juce::ThreadPool pool { 1 };
    juce::AudioFormatManager formats;

    std::atomic<float>* partOn[mv::kNumParts] {};
    std::atomic<float>* partVol[mv::kNumParts] {};
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* midiOutParam = nullptr;
    std::atomic<float>* monitorParam = nullptr;
    std::atomic<float>* previewParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyverseProcessor)
};
