#include "PianoRoll.h"

#include "Synth.h"

juce::Colour PianoRoll::partColour (int part)
{
    static const juce::uint32 c[] = { 0xffff5ca8, 0xff6c8cff, 0xff2ee6a6, 0xffffc145 };
    return juce::Colour (c[juce::jlimit (0, 3, part)]);
}

void PianoRoll::setScore (const mv::Score& s)
{
    score = s;
    int lo = 127, hi = 0;
    for (auto& t : score.tracks)
        for (auto& n : t.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (lo > hi) { lo = 48; hi = 84; }
    lowPitch = std::max (0, lo - 2);
    highPitch = std::min (127, std::max (hi + 2, lowPitch + 24));
    repaint();
}

void PianoRoll::setPlayhead (double beat, bool playing)
{
    if (std::abs (beat - playhead) > 1e-3 || playing != isPlaying)
    {
        playhead = beat;
        isPlaying = playing;
        repaint();
    }
}

void PianoRoll::setPartVisible (int part, bool v)
{
    if (part >= 0 && part < 4 && visible[(size_t) part] != v) { visible[(size_t) part] = v; repaint(); }
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0c0f1c));
    g.fillRoundedRectangle (area, 8.0f);
    area = area.reduced (8.0f);

    const double length = score.lengthBeats > 0 ? score.lengthBeats : std::max (4.0, std::ceil (score.contentEnd() / 4) * 4);
    const bool hasDrums = score.track ("Drums") && ! score.track ("Drums")->hits.empty();
    auto chordLane = area.removeFromTop (22.0f);
    auto drumLane = hasDrums ? area.removeFromBottom (34.0f) : juce::Rectangle<float>();
    auto notes = area.reduced (0, 4.0f);
    auto xOf = [&] (double beat) { return notes.getX() + (float) (beat / length) * notes.getWidth(); };
    const float rowH = notes.getHeight() / (float) (highPitch - lowPitch + 1);
    auto yOf = [&] (int pitch) { return notes.getBottom() - (float) (pitch - lowPitch + 1) * rowH; };

    // black-key rows and bar lines
    for (int p = lowPitch; p <= highPitch; ++p)
    {
        int pc = p % 12;
        if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)
        {
            g.setColour (juce::Colour (0xff121729));
            g.fillRect (notes.getX(), yOf (p), notes.getWidth(), rowH);
        }
        if (pc == 0)
        {
            g.setColour (juce::Colour (0xff1f2640));
            g.drawHorizontalLine ((int) (yOf (p) + rowH), notes.getX(), notes.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("C" + juce::String (p / 12 - 1), juce::Rectangle<float> (notes.getX() + 2, yOf (p) - 2, 30, rowH + 4), juce::Justification::centredLeft);
        }
    }
    const double beatsPerLine = length > 64 ? 16.0 : 4.0;
    for (double b = 0; b <= length + 1e-6; b += 1.0)
    {
        bool bar = std::fmod (b, 4.0) < 1e-6;
        if (! bar && length > 32) continue;
        g.setColour (bar ? juce::Colour (0xff2a3354) : juce::Colour (0xff181e33));
        g.drawVerticalLine ((int) xOf (b), chordLane.getY(), area.getBottom() + (hasDrums ? 34.0f : 0.0f));
        if (bar && std::fmod (b, beatsPerLine) < 1e-6 && b < length)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String ((int) (b / 4) + 1), juce::Rectangle<float> (xOf (b) + 3, notes.getY(), 30, 12), juce::Justification::topLeft);
        }
    }

    // chord lane
    const bool flats = score.key.prefersFlats();
    for (auto& c : score.chords)
    {
        if (! c.chord) continue;
        auto r = juce::Rectangle<float> (xOf (c.start), chordLane.getY(), xOf (c.start + c.duration) - xOf (c.start), chordLane.getHeight()).reduced (1.0f, 2.0f);
        g.setColour (juce::Colour (0xff1b2240));
        g.fillRoundedRectangle (r, 4.0f);
        if (r.getWidth() > 18)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (c.chord->name (flats), r.reduced (4, 0), juce::Justification::centredLeft, true);
        }
    }

    // notes: chords and bass first, melody on top
    for (int part : { 1, 2, 0 })
    {
        if (! visible[(size_t) part]) continue;
        auto* t = score.track (mv::partName ((mv::Part) part));
        if (! t) continue;
        auto col = partColour (part);
        for (auto& n : t->notes)
        {
            auto r = juce::Rectangle<float> (xOf (n.start), yOf (n.pitch), std::max (2.0f, xOf (n.end()) - xOf (n.start) - 1.0f), std::max (2.0f, rowH - 1.0f));
            g.setColour (col.withAlpha (part == 0 ? 0.95f : 0.55f).withMultipliedBrightness (0.6f + 0.4f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (r, std::min (3.0f, r.getHeight() / 2));
        }
    }

    // drum lane
    if (hasDrums && visible[3])
    {
        g.setColour (juce::Colour (0xff10142a));
        g.fillRoundedRectangle (drumLane, 4.0f);
        auto& hits = score.track ("Drums")->hits;
        std::vector<std::string> kinds;
        for (auto& h : hits) if (std::find (kinds.begin(), kinds.end(), h.name) == kinds.end()) kinds.push_back (h.name);
        const float laneH = drumLane.getHeight() / (float) std::max<size_t> (1, kinds.size());
        for (auto& h : hits)
        {
            auto k = (size_t) (std::find (kinds.begin(), kinds.end(), h.name) - kinds.begin());
            float y = drumLane.getY() + (float) k * laneH;
            g.setColour (partColour (3).withAlpha (0.35f + 0.6f * (float) h.velocity / 127.0f));
            g.fillRect (xOf (h.beat), y + 1, 3.0f, std::max (1.0f, laneH - 2));
        }
    }

    // playhead
    if (isPlaying)
    {
        float x = xOf (playhead);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (x, chordLane.getY(), x, area.getBottom() + (hasDrums ? 34.0f : 0.0f), 1.5f);
    }
    if (score.tracks.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Compose something, or drop a song on the plugin", getLocalBounds(), juce::Justification::centred);
    }
}
