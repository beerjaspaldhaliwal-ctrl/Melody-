#pragma once

#include "PianoRoll.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MelodyverseLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MelodyverseLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A pill you drag straight into the DAW timeline to drop a .mid clip.
class MidiDragHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragHandle (juce::String label, juce::Colour colour, std::function<juce::File()> makeFile);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String label;
    juce::Colour colour;
    std::function<juce::File()> makeFile;
    bool dragging = false;
};

class MelodyverseEditor final : public juce::AudioProcessorEditor,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer
{
public:
    explicit MelodyverseEditor (MelodyverseProcessor&);
    ~MelodyverseEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void refreshFromProcessor();
    void readSettingsFromUi();
    void syncUiFromSettings();
    void panel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    MelodyverseProcessor& proc;
    MelodyverseLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };

    // compose
    juce::ComboBox styleBox, keyBox, modeBox, barsBox;
    juce::TextEditor progressionEditor;
    juce::Label styleInfo, seedLabel, styleLabel, keyLabel, modeLabel, barsLabel, progLabel;
    juce::TextButton generateButton { "GENERATE" }, rerollButton { "Same seed" };

    // listen
    juce::TextButton loadButton { "Load a song..." }, captureButton { "Capture input" };
    juce::TextButton transcribeButton { "TRANSCRIBE IT" }, matchButton { "NEW MELODY OVER IT" };
    juce::ComboBox quantizeBox;
    juce::Label analysisText, quantizeLabel, tempoHint;

    // parts
    PianoRoll roll;
    std::array<juce::TextButton, mv::kNumParts> partButtons;
    std::array<juce::ComboBox, mv::kNumParts> instBoxes;
    std::array<juce::Slider, mv::kNumParts> volSliders;
    std::array<std::unique_ptr<MidiDragHandle>, mv::kNumParts> dragHandles;
    std::unique_ptr<MidiDragHandle> dragAll;

    // transport / export
    juce::TextButton previewButton { "Preview" }, midiOutButton { "MIDI out" }, monitorButton { "Monitor in" };
    juce::TextButton saveMidiButton { "Save MIDI..." }, saveWavButton { "Save WAV..." };
    juce::Slider masterSlider, reverbSlider;
    juce::Label masterLabel, reverbLabel, statusLabel, sourceLabel;

    // Declared after every control they attach to, so they are destroyed first
    // (an attachment outliving its button/slider is a use-after-free on close).
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    bool dropHover = false;
    juce::Rectangle<int> headerArea, composeArea, listenArea, centreArea, bottomArea, progressArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyverseEditor)
};
