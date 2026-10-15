#pragma once

#include "Score.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Read-only piano roll of the current arrangement: chord lane, notes coloured per part,
// a drum lane and a moving playhead.
class PianoRoll final : public juce::Component
{
public:
    void setScore (const mv::Score& s);
    void setPlayhead (double beat, bool playing);
    void setPartVisible (int part, bool visible);
    void paint (juce::Graphics& g) override;

    static juce::Colour partColour (int part);

private:
    mv::Score score;
    double playhead = 0;
    bool isPlaying = false;
    std::array<bool, 5> visible { true, true, true, true, true };
    int lowPitch = 36, highPitch = 96;
};
