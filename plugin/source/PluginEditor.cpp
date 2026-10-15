#include "PluginEditor.h"

#include "Styles.h"
#include "Synth.h"

namespace
{
namespace col
{
const juce::Colour bg (0xff0b0e1a), panel (0xff141a2e), panelEdge (0xff222a47), text (0xffe8ebf7), dim (0xff8b93b5),
    accentA (0xffff3d9a), accentB (0xffffa53b), field (0xff0f1324);
}

juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

const char* const kKeyNames[12] = { "C", "C# / Db", "D", "D# / Eb", "E", "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B" };

struct StyleGroup { const char* title; std::vector<const char*> names; };
const std::vector<StyleGroup>& styleGroups()
{
    static const std::vector<StyleGroup> g {
        { "Hollywood & film", { "hollywood_epic", "hollywood_heroic", "hollywood_wonder", "cinematic_suspense", "film_emotional" } },
        { "Spanish & Latin", { "spanish_flamenco", "latin_reggaeton", "latin_bachata", "bossa_nova" } },
        { "Punjabi & Indian", { "punjabi_bhangra", "punjabi_sad", "bollywood_romantic", "indian_classical" } },
        { "English-language", { "english_pop", "pop_ballad", "rock", "edm_house", "trap", "rnb_soul", "lofi", "gospel", "country", "blues", "jazz_swing" } },
        { "Around the world", { "afrobeats", "reggae", "arabic_hijaz", "celtic", "japanese_anime", "baroque" } },
    };
    return g;
}

juce::String pretty (juce::String s) { return s.replace ("_", " ").substring (0, 1).toUpperCase() + s.replace ("_", " ").substring (1); }

void setupLabel (juce::Label& l, const juce::String& text, float size = 12.0f, juce::Colour c = col::dim)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (font (size));
    l.setColour (juce::Label::textColourId, c);
}
} // namespace

// ---------------------------------------------------------------------------
// Look and feel
// ---------------------------------------------------------------------------

MelodyverseLookAndFeel::MelodyverseLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::ComboBox::backgroundColourId, col::field);
    setColour (juce::ComboBox::outlineColourId, col::panelEdge);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::headerTextColourId, col::accentB);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accentA.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId, col::field);
    setColour (juce::TextButton::buttonOnColourId, col::accentA);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, col::field);
    setColour (juce::TextEditor::outlineColourId, col::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accentA);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::Slider::thumbColourId, col::text);
    setColour (juce::Slider::trackColourId, col::accentA);
    setColour (juce::Slider::backgroundColourId, col::field);
    setColour (juce::Slider::rotarySliderFillColourId, col::accentA);
    setColour (juce::Slider::rotarySliderOutlineColourId, col::field);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::panelEdge);
}

void MelodyverseLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = std::min (8.0f, r.getHeight() / 2);
    const bool accent = b.getProperties()["accent"];
    juce::Colour on = b.getProperties().contains ("onColour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"]) : col::accentA;
    if (! b.isEnabled())
    {
        g.setColour (col::field.withAlpha (0.6f));
        g.fillRoundedRectangle (r, radius);
        return;
    }
    if (accent)
    {
        juce::ColourGradient grad (col::accentA, r.getX(), r.getY(), col::accentB, r.getRight(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, radius);
        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.25f : 0.12f));
            g.fillRoundedRectangle (r, radius);
        }
        return;
    }
    const bool toggled = b.getToggleState();
    g.setColour (toggled ? on.withAlpha (0.85f) : col::field);
    g.fillRoundedRectangle (r, radius);
    g.setColour (toggled ? on : (highlighted ? col::dim : col::panelEdge));
    g.drawRoundedRectangle (r, radius, 1.0f);
    if (down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (r, radius);
    }
}

void MelodyverseLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::field);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? col::accentA : col::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (col::dim);
    g.fillPath (arrow);
}

void MelodyverseLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                               juce::Slider::SliderStyle, juce::Slider& s)
{
    auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.5f, (float) w, 5.0f);
    g.setColour (col::field);
    g.fillRoundedRectangle (track, 2.5f);
    auto fillCol = s.findColour (juce::Slider::trackColourId);
    g.setColour (fillCol);
    g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    g.setColour (col::text);
    g.fillEllipse (pos - 6.0f, (float) y + (float) h * 0.5f - 6.0f, 12.0f, 12.0f);
}

void MelodyverseLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = std::min (r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path bgArc, fgArc;
    bgArc.addCentredArc (cx, cy, radius, radius, 0, start, end, true);
    const float angle = start + pos * (end - start);
    fgArc.addCentredArc (cx, cy, radius, radius, 0, start, angle, true);
    g.setColour (col::field);
    g.strokePath (bgArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::accentA);
    g.strokePath (fgArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::text);
    g.fillEllipse (cx + (radius - 6) * std::sin (angle) - 2.5f, cy - (radius - 6) * std::cos (angle) - 2.5f, 5.0f, 5.0f);
}

// ---------------------------------------------------------------------------
// Drag handle
// ---------------------------------------------------------------------------

MidiDragHandle::MidiDragHandle (juce::String l, juce::Colour c, std::function<juce::File()> f)
    : label (std::move (l)), colour (c), makeFile (std::move (f))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag this into your DAW's timeline to drop the MIDI clip");
}

void MidiDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colour.withAlpha (isMouseOver() ? 0.3f : 0.15f));
    g.fillRoundedRectangle (r, r.getHeight() / 2);
    g.setColour (colour);
    g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
    // grip dots
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 9 + (float) j * 4, r.getCentreY() - 5 + (float) i * 4, 2.2f, 2.2f);
    g.setColour (col::text);
    g.setFont (font (11.5f, true));
    g.drawText (label, r.withTrimmedLeft (18), juce::Justification::centred);
}

void MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5) return;
    auto file = makeFile();
    if (! file.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<MidiDragHandle> (this)] {
                                                                    if (safe) safe->dragging = false;
                                                                });
}

// ---------------------------------------------------------------------------
// Progression list
// ---------------------------------------------------------------------------

void ProgressionList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) options.size()) return;
    auto& o = options[(size_t) row];
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2.0f, 2.0f);
    g.setColour (selected ? col::accentA.withAlpha (0.25f) : col::field);
    g.fillRoundedRectangle (r, 6.0f);
    if (selected)
    {
        g.setColour (col::accentA);
        g.drawRoundedRectangle (r, 6.0f, 1.2f);
    }
    auto inner = r.reduced (8.0f, 4.0f);
    auto top = inner.removeFromTop (16.0f);
    // fit meter
    auto meter = top.removeFromRight (64.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (meter.withSizeKeepingCentre (64.0f, 6.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (col::accentA, meter.getX(), 0, col::accentB, meter.getRight(), 0, false));
    g.fillRoundedRectangle (meter.withSizeKeepingCentre (64.0f, 6.0f).withWidth (64.0f * (float) o.fit), 3.0f);
    g.setColour (col::text);
    g.setFont (font (12.5f, true));
    g.drawText (juce::String (row + 1) + ".  " + juce::String (o.name), top.withTrimmedRight (6), juce::Justification::centredLeft, true);
    g.setColour (col::accentB);
    g.setFont (font (11.5f));
    g.drawText (juce::String ((int) std::lround (o.fit * 100)) + "% fit   " + juce::String (o.roman), inner.removeFromTop (15.0f), juce::Justification::centredLeft, true);
    g.setColour (col::dim);
    g.setFont (font (10.5f));
    std::string names;
    std::optional<mv::Chord> last;
    int shown = 0;
    for (auto& c : o.chords)
    {
        if (! c.chord || (last && *last == *c.chord)) continue;
        if (++shown > 8) { names += " ..."; break; }
        names += (names.empty() ? "" : "  ") + c.chord->name (flats);
        last = c.chord;
    }
    g.drawText (juce::String (names) + "   |   " + pretty (o.style), inner, juce::Justification::centredLeft, true);
}

juce::String ProgressionList::getTooltipForRow (int row)
{
    return row >= 0 && row < (int) options.size() ? juce::String (options[(size_t) row].description) : juce::String();
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

MelodyverseEditor::MelodyverseEditor (MelodyverseProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // ---- compose panel ----
    setupLabel (this->styleLabel, "Style");
    setupLabel (keyLabel, "Key");
    setupLabel (modeLabel, "Scale / mode");
    setupLabel (barsLabel, "Bars");
    setupLabel (progLabel, "Chord progression");
    int id = 1;
    for (auto& grp : styleGroups())
    {
        styleBox.addSectionHeading (grp.title);
        for (auto* n : grp.names)
            if (mv::styleIndex (n) >= 0) styleBox.addItem (pretty (n), id++);
    }
    for (auto& st : mv::styles())  // anything not grouped above
    {
        bool listed = false;
        for (auto& grp : styleGroups()) for (auto* n : grp.names) listed |= st.name == n;
        if (! listed) styleBox.addItem (pretty (st.name), id++);
    }
    keyBox.addItem ("Auto", 1);
    for (int k = 0; k < 12; ++k) keyBox.addItem (kKeyNames[k], k + 2);
    modeBox.addItem ("Auto", 1);
    {
        int m = 2;
        for (auto& name : mv::scaleNames()) modeBox.addItem (pretty (name), m++);
    }
    for (int b : { 4, 8, 16, 32 }) barsBox.addItem (juce::String (b) + " bars", b);
    progressionEditor.setTextToShowWhenEmpty ("style default  (e.g. i bVI bIII bVII  or  Am F C G)", col::dim);
    progressionEditor.setFont (font (13.0f));
    progressionEditor.setTooltip ("Roman numerals (I V vi IV, i bVII bVI V ...) or chord names (Am F C G). Leave empty for the style's own progressions.");
    styleInfo.setJustificationType (juce::Justification::topLeft);
    styleInfo.setFont (font (12.0f));
    styleInfo.setColour (juce::Label::textColourId, col::dim);
    seedLabel.setFont (font (11.0f));
    seedLabel.setColour (juce::Label::textColourId, col::dim);
    generateButton.getProperties().set ("accent", true);
    generateButton.setTooltip ("Compose a brand-new melody, chords, bass and drums in this style");
    rerollButton.setTooltip ("Rebuild with the same seed (after changing key, bars or progression)");

    auto onSettingChanged = [this] { readSettingsFromUi(); proc.generateNew (false); };
    styleBox.onChange = [this, onSettingChanged] {
        // a fresh style should sound like itself: reset per-part instruments and progression to its defaults
        for (auto& s : proc.settings.instruments) s = {};
        progressionEditor.setText ({}, false);
        onSettingChanged();
        syncUiFromSettings();
    };
    keyBox.onChange = onSettingChanged;
    modeBox.onChange = onSettingChanged;
    barsBox.onChange = onSettingChanged;
    progressionEditor.onReturnKey = onSettingChanged;
    progressionEditor.onFocusLost = [this] { readSettingsFromUi(); };
    generateButton.onClick = [this] { readSettingsFromUi(); proc.generateNew (true); };
    rerollButton.onClick = onSettingChanged;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &this->styleLabel, &keyLabel, &modeLabel, &barsLabel, &progLabel, &styleBox, &keyBox, &modeBox,
                                                                          &barsBox, &progressionEditor, &styleInfo, &seedLabel, &generateButton, &rerollButton })
        addAndMakeVisible (c);

    // ---- listen panel ----
    loadButton.setTooltip ("Analyse an audio file: WAV, AIFF, FLAC, OGG or MP3. You can also drop files anywhere on the plugin.");
    captureButton.setClickingTogglesState (true);
    captureButton.getProperties().set ("onColour", (juce::int64) 0xffff4757);
    captureButton.setTooltip ("Record what comes into the plugin (route a track or a mic to its input), then stop to analyse it");
    transcribeButton.getProperties().set ("accent", true);
    transcribeButton.setTooltip ("Turn the song's own melody, chords and bass into MIDI you can play and drag out");
    matchButton.getProperties().set ("accent", true);
    matchButton.setTooltip ("Write a brand-new melody and parts in the selected style over the song's chords, key and tempo");
    quantizeBox.addItem ("Off", 1);
    quantizeBox.addItem ("1/8", 8);
    quantizeBox.addItem ("1/16", 16);
    quantizeBox.addItem ("1/32", 32);
    setupLabel (quantizeLabel, "Snap notes to");
    analysisText.setJustificationType (juce::Justification::topLeft);
    analysisText.setFont (font (12.5f));
    analysisText.setColour (juce::Label::textColourId, col::text);
    tempoHint.setFont (font (11.5f));
    tempoHint.setColour (juce::Label::textColourId, col::accentB);
    tempoHint.setJustificationType (juce::Justification::topLeft);

    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Choose a song to listen to", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) proc.loadAudioFile (fc.getResult());
        });
    };
    captureButton.onClick = [this] { proc.setCapturing (captureButton.getToggleState()); };
    quantizeBox.onChange = [this] { readSettingsFromUi(); };
    transcribeButton.onClick = [this] { readSettingsFromUi(); proc.transcribe(); };
    matchButton.onClick = [this] {
        readSettingsFromUi();
        proc.settings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (1000000);
        proc.matchStyle();
    };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadButton, &captureButton, &transcribeButton, &matchButton, &quantizeBox,
                                                                          &quantizeLabel, &analysisText, &tempoHint })
        addAndMakeVisible (c);

    // ---- vocals tab ----
    for (auto* t : { &listenTab, &vocalTabButton })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (7001);
        addAndMakeVisible (*t);
    }
    listenTab.onClick = [this] { showTab (false); };
    vocalTabButton.onClick = [this] { showTab (true); };
    vocalTabButton.setTooltip ("Analyse a vocal in depth and get the chord progressions and melodies that fit it");
    loadVocalButton.setTooltip ("A dry vocal works best: an a cappella, or the vocal stem of your song (WAV, AIFF, FLAC, OGG, MP3)");
    recordVocalButton.setClickingTogglesState (true);
    recordVocalButton.getProperties().set ("onColour", (juce::int64) 0xffff4757);
    recordVocalButton.setTooltip ("Sing into the mic (standalone) or route your vocal track into the plugin, then press Stop");
    loadVocalButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Choose a vocal", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) proc.loadVocalFile (fc.getResult());
        });
    };
    recordVocalButton.onClick = [this] {
        proc.setCapturing (recordVocalButton.getToggleState(), MelodyverseProcessor::CaptureTarget::Vocal);
    };
    vocalReportText.setMultiLine (true, true);
    vocalReportText.setReadOnly (true);
    vocalReportText.setScrollbarsShown (true);
    vocalReportText.setCaretVisible (false);
    vocalReportText.setFont (font (12.0f));
    vocalReportText.setColour (juce::TextEditor::backgroundColourId, col::field);
    setupLabel (progressionsLabel, "TOP CHORD PROGRESSIONS FOR THIS VOCAL", 10.5f);
    setupLabel (vocalHint, "", 11.0f, col::accentB);
    vocalHint.setJustificationType (juce::Justification::topLeft);
    progressionList.setRowHeight (48);
    progressionList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    progressionList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    progressionModel.onPick = [this] (int row) { proc.applyVocalOption (row); };
    ideaBox.addItem ("Melody: answer phrases between your lines", 1);
    ideaBox.addItem ("Melody: harmony a 3rd above the vocal", 2);
    ideaBox.addItem ("Melody: harmony a 6th below the vocal", 3);
    ideaBox.onChange = [this] { proc.setVocalIdea (ideaBox.getSelectedId() - 1); };
    newIdeaButton.getProperties().set ("accent", true);
    newIdeaButton.setTooltip ("Keep the chords, write a fresh melody / arrangement around the vocal");
    newIdeaButton.onClick = [this] { proc.newVocalIdea(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadVocalButton, &recordVocalButton, &vocalReportText, &progressionsLabel,
                                                                          &progressionList, &ideaBox, &newIdeaButton, &vocalHint })
        addChildComponent (c);

    // ---- parts ----
    addAndMakeVisible (roll);
    auto instruments = mv::instrumentNames();
    for (int i = 0; i < mv::kNumParts; ++i)
    {
        auto colour = PianoRoll::partColour (i);
        auto& b = partButtons[(size_t) i];
        b.setButtonText (i == 4 ? "Vocal" : mv::partName ((mv::Part) i));
        b.setClickingTogglesState (true);
        b.getProperties().set ("onColour", (juce::int64) colour.getARGB());
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, juce::String (MelodyverseProcessor::kPartIds[i]) + "_on", b));
        b.onStateChange = [this, i] { roll.setPartVisible (i, partButtons[(size_t) i].getToggleState()); };
        addAndMakeVisible (b);

        auto& s = volSliders[(size_t) i];
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setColour (juce::Slider::trackColourId, colour);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, juce::String (MelodyverseProcessor::kPartIds[i]) + "_vol", s));
        addAndMakeVisible (s);

        auto& box = instBoxes[(size_t) i];
        if (i != 3)
        {
            box.addItem (i == 4 ? "Guide (bansuri)" : "Style sound", 1);
            for (size_t k = 0; k < instruments.size(); ++k) box.addItem (pretty (instruments[k]), (int) k + 2);
            box.onChange = [this, i, instruments] {
                int sel = instBoxes[(size_t) i].getSelectedId();
                proc.setPartInstrument (i, sel <= 1 ? juce::String() : juce::String (instruments[(size_t) sel - 2]));
            };
        }
        else
        {
            box.addItem ("Style kit", 1);
            box.setEnabled (false);
        }
        box.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (box);

        dragHandles[(size_t) i] = std::make_unique<MidiDragHandle> ("drag MIDI", colour, [this, i] { return proc.writeMidiTemp (i); });
        addAndMakeVisible (*dragHandles[(size_t) i]);
    }
    dragAll = std::make_unique<MidiDragHandle> ("drag full arrangement", col::accentB, [this] { return proc.writeMidiTemp (-1); });
    addAndMakeVisible (*dragAll);

    // ---- bottom bar ----
    for (auto* b : { &previewButton, &midiOutButton, &monitorButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    previewButton.getProperties().set ("onColour", (juce::int64) 0xff2ee6a6);
    previewButton.setTooltip ("Play the loop even when the DAW transport is stopped (always needed in the standalone app)");
    midiOutButton.setTooltip ("Also send the parts as MIDI (melody ch.1, chords ch.2, bass ch.3, drums ch.10) to route to your own instruments");
    monitorButton.setTooltip ("Pass the plugin's audio input through to its output");
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "preview", previewButton));
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "midi_out", midiOutButton));
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "monitor", monitorButton));
    for (auto* s : { &masterSlider, &reverbSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (*s);
    }
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "master", masterSlider));
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "reverb", reverbSlider));
    setupLabel (masterLabel, "Master", 11.0f);
    setupLabel (reverbLabel, "Reverb", 11.0f);
    setupLabel (statusLabel, "", 12.0f, col::text);
    setupLabel (sourceLabel, "", 11.0f);
    for (auto* l : { &masterLabel, &reverbLabel, &statusLabel, &sourceLabel }) addAndMakeVisible (*l);

    saveMidiButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (proc.writeMidiTemp (-1).getFileName()), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  proc.saveMidi (f.withFileExtension ("mid"));
                              });
    };
    saveWavButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save WAV", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (proc.writeMidiTemp (-1).getFileNameWithoutExtension() + ".wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  proc.saveWav (f.withFileExtension ("wav"));
                              });
    };
    addAndMakeVisible (saveMidiButton);
    addAndMakeVisible (saveWavButton);

    syncUiFromSettings();
    listenTab.setToggleState (true, juce::dontSendNotification);
    showTab (proc.getVocalReport().has_value());
    setResizable (true, true);
    setResizeLimits (1100, 660, 1900, 1250);
    setSize (1220, 720);
    refreshFromProcessor();
    startTimerHz (30);
}

MelodyverseEditor::~MelodyverseEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MelodyverseEditor::syncUiFromSettings()
{
    auto& s = proc.settings;
    for (int i = 0; i < styleBox.getNumItems(); ++i)
        if (styleBox.getItemText (i) == pretty (s.style)) styleBox.setSelectedItemIndex (i, juce::dontSendNotification);
    keyBox.setSelectedId (s.keyTonic < 0 ? 1 : s.keyTonic + 2, juce::dontSendNotification);
    int modeId = 1, m = 2;
    for (auto& name : mv::scaleNames()) { if (juce::String (name) == s.mode) modeId = m; ++m; }
    modeBox.setSelectedId (modeId, juce::dontSendNotification);
    barsBox.setSelectedId (s.bars, juce::dontSendNotification);
    if (! progressionEditor.hasKeyboardFocus (true))  // never clobber what the user is typing
        progressionEditor.setText (s.progression, false);
    quantizeBox.setSelectedId (s.quantize == 0 ? 1 : s.quantize, juce::dontSendNotification);
    auto instruments = mv::instrumentNames();
    for (int p = 0; p < 3; ++p)
    {
        int sel = 1;
        for (size_t k = 0; k < instruments.size(); ++k) if (s.instruments[(size_t) p] == juce::String (instruments[k])) sel = (int) k + 2;
        instBoxes[(size_t) p].setSelectedId (sel, juce::dontSendNotification);
    }
    if (mv::styleIndex (s.style.toStdString()) >= 0)
    {
        auto& st = mv::styles()[(size_t) mv::styleIndex (s.style.toStdString())];
        styleInfo.setText (juce::String (st.region) + "\n" + juce::String (st.description) + "\n\nProgressions: " + juce::StringArray (
                                                                                                          [&] { juce::StringArray a; for (auto& x : st.progressions) a.add (x); return a; }()).joinIntoString ("  |  "),
                           juce::dontSendNotification);
    }
}

void MelodyverseEditor::readSettingsFromUi()
{
    auto& s = proc.settings;
    auto text = styleBox.getText().toLowerCase().replace (" ", "_");
    if (mv::styleIndex (text.toStdString()) >= 0) s.style = text;
    s.keyTonic = keyBox.getSelectedId() <= 1 ? -1 : keyBox.getSelectedId() - 2;
    auto names = mv::scaleNames();
    int m = modeBox.getSelectedId();
    s.mode = m <= 1 ? juce::String() : juce::String (names[(size_t) m - 2]);
    s.bars = barsBox.getSelectedId() > 0 ? barsBox.getSelectedId() : 8;
    s.progression = progressionEditor.getText();
    s.quantize = quantizeBox.getSelectedId() <= 1 ? 0 : quantizeBox.getSelectedId();
}

void MelodyverseEditor::refreshFromProcessor()
{
    syncUiFromSettings();  // settings can change behind the UI's back (host state restore, automation)
    auto score = proc.getScore();
    roll.setScore (score);
    for (int i = 0; i < mv::kNumParts; ++i) roll.setPartVisible (i, partButtons[(size_t) i].getToggleState());
    statusLabel.setText (proc.getStatus(), juce::dontSendNotification);
    sourceLabel.setText (proc.getSourceDescription() + "   |   " + juce::String (score.key.name()) + "   |   " + juce::String (score.bpm, 1) + " BPM   |   "
                             + juce::String ((int) (score.lengthBeats / 4)) + " bars",
                         juce::dontSendNotification);
    seedLabel.setText ("seed " + juce::String (proc.settings.seed), juce::dontSendNotification);

    // vocals
    {
        auto vr = proc.getVocalReport();
        auto opts = proc.getVocalOptions();
        progressionModel.options = opts;
        progressionModel.flats = vr && vr->key.prefersFlats();
        progressionList.updateContent();
        if (proc.getVocalOptionIndex() >= 0) progressionList.selectRow (proc.getVocalOptionIndex(), true, true);
        else progressionList.deselectAllRows();
        progressionList.repaint();
        ideaBox.setSelectedId (proc.getVocalIdea() + 1, juce::dontSendNotification);
        newIdeaButton.setEnabled (! opts.empty() && ! proc.isBusy());
        ideaBox.setEnabled (! opts.empty());
        juce::String report;
        if (vr)
        {
            report << "VOCAL: " << proc.getVocalLabel() << "\n\n";
            for (auto& line : vr->insights) report << "- " << line << "\n";
            vocalHint.setText (opts.empty() ? juce::String() : "Click a progression to hear it under the vocal guide. Bar 1 = "
                                                                     + juce::String (vr->originSeconds, 2) + " s into your vocal.",
                               juce::dontSendNotification);
        }
        else
        {
            report = "Drop a vocal here (or load / record one).\n\n"
                     "Melodyverse studies it in depth: Sa and thaat, the swaras you use, range and voice type, meend, murki, kan and vibrato, "
                     "pitch accuracy, phrasing and breaths, the hook, tempo and swing.\n\n"
                     "Then it ranks the chord progressions that fit your singing best (Punjabi sad, bhangra, Punjabi pop, R&B, Sufi...) "
                     "and writes answer phrases or harmonies around your voice.\n\n"
                     "Best results: a dry vocal - a cappella or the isolated vocal stem.";
            vocalHint.setText ({}, juce::dontSendNotification);
        }
        if (vocalReportText.getText() != report) vocalReportText.setText (report, false);
    }

    auto a = proc.getAnalysis();
    transcribeButton.setEnabled (a.has_value() && ! proc.isBusy());
    matchButton.setEnabled (a.has_value() && ! proc.isBusy());
    if (a)
    {
        const bool flats = a->key.prefersFlats();
        juce::String t;
        t << "Song:   " << proc.getAnalysisLabel() << "\n";
        t << "Key:    " << a->key.name() << "  (" << juce::String ((int) std::lround (a->keyConfidence * 100)) << "% sure)\n";
        t << "Tempo:  " << juce::String (a->bpm, 1) << " BPM\n";
        t << "Melody: " << (int) a->melody.size() << " notes\n\n";
        t << "Chords:\n" << a->chordChart (16) << "\n\n";
        auto famous = a->famous();
        if (! famous.empty())
        {
            t << "Sounds like:\n";
            for (size_t i = 0; i < famous.size() && i < 2; ++i) t << "  " << famous[i].name << " (" << famous[i].roman << ")\n";
            t << "\n";
        }
        auto aff = a->styleAffinity();
        t << "Closest styles:\n  ";
        for (size_t i = 0; i < aff.size() && i < 3; ++i) t << (i ? ", " : "") << pretty (aff[i].first);
        juce::ignoreUnused (flats);
        analysisText.setText (t, juce::dontSendNotification);
    }
    else
        analysisText.setText ("Drop a song here, load one, or capture the plugin's input.\n\n"
                              "Melodyverse will hear its key, tempo, chords and melody. Then you can:\n\n"
                              "  - TRANSCRIBE IT: the song's melody & chords as MIDI\n\n"
                              "  - NEW MELODY OVER IT: an original part in the style on the left, over the song's chords",
                              juce::dontSendNotification);
}

void MelodyverseEditor::timerCallback()
{
    const int v = proc.getVersion();
    if (v != lastVersion)
    {
        lastVersion = v;
        refreshFromProcessor();
    }
    roll.setPlayhead (proc.getPlayheadBeat(), proc.isHostPlaying());
    const bool capVocal = proc.isCapturing() && proc.getCaptureTarget() == MelodyverseProcessor::CaptureTarget::Vocal;
    const bool capSong = proc.isCapturing() && ! capVocal;
    captureButton.setButtonText (capSong ? "Stop  (" + juce::String (proc.capturedSeconds(), 1) + " s)" : juce::String ("Capture input"));
    captureButton.setToggleState (capSong, juce::dontSendNotification);
    recordVocalButton.setButtonText (capVocal ? "Stop  (" + juce::String (proc.capturedSeconds(), 1) + " s)" : juce::String ("Record vocal"));
    recordVocalButton.setToggleState (capVocal, juce::dontSendNotification);
    juce::String hint;
    if (auto a = proc.getAnalysis())
    {
        const double host = proc.getHostBpm();
        auto src = proc.getSourceDescription();
        if (host > 0 && std::abs (host - a->bpm) > 0.3 && (src.startsWith ("Transcribed") || src.startsWith ("New")))
            hint = "Set your DAW tempo to " + juce::String (a->bpm, 1) + " BPM so the parts line up with the song.";
    }
    tempoHint.setText (hint, juce::dontSendNotification);
    if (proc.isBusy()) repaint (progressArea);
}

void MelodyverseEditor::showTab (bool vocals)
{
    vocalsTab = vocals;
    (vocals ? vocalTabButton : listenTab).setToggleState (true, juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadButton, &captureButton, &transcribeButton, &matchButton, &quantizeBox,
                                                                          &quantizeLabel, &analysisText, &tempoHint })
        c->setVisible (! vocals);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadVocalButton, &recordVocalButton, &vocalReportText, &progressionsLabel,
                                                                          &progressionList, &ideaBox, &newIdeaButton, &vocalHint })
        c->setVisible (vocals);
}

bool MelodyverseEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        for (auto* ext : { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a" })
            if (f.endsWithIgnoreCase (ext)) return true;
    return false;
}

void MelodyverseEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    for (auto& f : files)
        if (isInterestedInFileDrag ({ f }))
        {
            if (vocalsTab) proc.loadVocalFile (juce::File (f));
            else proc.loadAudioFile (juce::File (f));
            break;
        }
}

void MelodyverseEditor::panel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 10.0f);
    g.setColour (col::panelEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (col::dim);
        g.setFont (font (11.0f, true));
        g.drawText (title, r.reduced (14, 10).removeFromTop (14), juce::Justification::topLeft);
    }
}

void MelodyverseEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // header
    auto h = headerArea.reduced (16, 0);
    juce::ColourGradient grad (col::accentA, (float) h.getX(), 0, col::accentB, (float) h.getX() + 260, 0, false);
    g.setGradientFill (grad);
    g.setFont (font (26.0f, true));
    g.drawText ("MELODYVERSE", h.removeFromLeft (260), juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (13.0f));
    g.drawText ("listen  -  transcribe  -  compose in 30 world styles", h, juce::Justification::centredLeft);

    panel (g, composeArea, "COMPOSE");
    panel (g, listenArea, {});
    panel (g, bottomArea, {});

    // progress bar
    if (proc.isBusy())
    {
        auto r = progressArea.toFloat();
        g.setColour (col::field);
        g.fillRoundedRectangle (r, 3.0f);
        g.setGradientFill (juce::ColourGradient (col::accentA, r.getX(), 0, col::accentB, r.getRight(), 0, false));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.02f, 1.0f, proc.getProgress())), 3.0f);
    }
}

void MelodyverseEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dropHover) return;
    g.setColour (col::accentA.withAlpha (0.18f));
    g.fillRect (getLocalBounds());
    g.setColour (col::accentA);
    g.drawRect (getLocalBounds(), 3);
    g.setColour (juce::Colours::white);
    g.setFont (font (24.0f, true));
    g.drawText (vocalsTab ? "Drop to study the vocal" : "Drop to listen", getLocalBounds(), juce::Justification::centred);
}

void MelodyverseEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (52);
    r.reduce (12, 0);
    bottomArea = r.removeFromBottom (58).withTrimmedBottom (10);
    r.removeFromBottom (8);
    composeArea = r.removeFromLeft (270);
    r.removeFromLeft (10);
    listenArea = r.removeFromRight (340);
    r.removeFromRight (10);
    centreArea = r;

    // compose
    {
        auto c = composeArea.reduced (14).withTrimmedTop (20);
        auto row = [&c] (int h) { auto x = c.removeFromTop (h); c.removeFromTop (6); return x; };
        styleLabel.setBounds (row (14));
        styleBox.setBounds (row (28));
        auto lr = row (14);
        keyLabel.setBounds (lr.removeFromLeft (lr.getWidth() / 2));
        modeLabel.setBounds (lr);
        auto kr = row (28);
        keyBox.setBounds (kr.removeFromLeft (kr.getWidth() / 2 - 3));
        kr.removeFromLeft (6);
        modeBox.setBounds (kr);
        barsLabel.setBounds (row (14));
        barsBox.setBounds (row (28));
        progLabel.setBounds (row (14));
        progressionEditor.setBounds (row (28));
        c.removeFromTop (4);
        generateButton.setBounds (row (40));
        auto sr = row (24);
        rerollButton.setBounds (sr.removeFromLeft (110));
        seedLabel.setBounds (sr.withTrimmedLeft (8));
        c.removeFromTop (4);
        styleInfo.setBounds (c);
    }
    // listen / vocals
    {
        auto tabs = listenArea.reduced (14, 10).removeFromTop (26);
        listenTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2 - 3));
        tabs.removeFromLeft (6);
        vocalTabButton.setBounds (tabs);
        auto v = listenArea.reduced (14).withTrimmedTop (32);
        auto vrow = [&v] (int h) { auto x = v.removeFromTop (h); v.removeFromTop (6); return x; };
        auto lr2 = vrow (30);
        loadVocalButton.setBounds (lr2.removeFromLeft (lr2.getWidth() / 2 - 3));
        lr2.removeFromLeft (6);
        recordVocalButton.setBounds (lr2);
        vrow (5);  // progress bar (shared)
        auto bottomV = v.removeFromBottom (36);
        newIdeaButton.setBounds (bottomV);
        v.removeFromBottom (6);
        ideaBox.setBounds (v.removeFromBottom (26));
        v.removeFromBottom (4);
        vocalHint.setBounds (v.removeFromBottom (30));
        auto listArea = v.removeFromBottom (5 * 48 + 2);
        progressionList.setBounds (listArea);
        progressionsLabel.setBounds (v.removeFromBottom (16));
        v.removeFromBottom (4);
        vocalReportText.setBounds (v);
    }
    {
        auto c = listenArea.reduced (14).withTrimmedTop (32);
        auto row = [&c] (int h) { auto x = c.removeFromTop (h); c.removeFromTop (6); return x; };
        auto lr = row (30);
        loadButton.setBounds (lr.removeFromLeft (lr.getWidth() / 2 - 3));
        lr.removeFromLeft (6);
        captureButton.setBounds (lr);
        progressArea = row (5);
        auto bottom = c.removeFromBottom (128);
        auto q = bottom.removeFromTop (26);
        quantizeLabel.setBounds (q.removeFromLeft (100));
        quantizeBox.setBounds (q);
        bottom.removeFromTop (8);
        transcribeButton.setBounds (bottom.removeFromTop (38));
        bottom.removeFromTop (8);
        matchButton.setBounds (bottom.removeFromTop (38));
        tempoHint.setBounds (c.removeFromBottom (32));
        analysisText.setBounds (c);
    }
    // centre: piano roll + part strips
    {
        auto c = centreArea;
        sourceLabel.setBounds (c.removeFromTop (18));
        auto strips = c.removeFromBottom (138);
        c.removeFromBottom (8);
        roll.setBounds (c);
        auto all = strips.removeFromBottom (24);
        dragAll->setBounds (all.withSizeKeepingCentre (220, 24));
        strips.removeFromBottom (6);
        const int w = (strips.getWidth() - (mv::kNumParts - 1) * 8) / mv::kNumParts;
        for (int i : kStripOrder)
        {
            auto s = strips.removeFromLeft (w);
            strips.removeFromLeft (8);
            partButtons[(size_t) i].setBounds (s.removeFromTop (26));
            s.removeFromTop (5);
            instBoxes[(size_t) i].setBounds (s.removeFromTop (24));
            s.removeFromTop (5);
            volSliders[(size_t) i].setBounds (s.removeFromTop (16));
            s.removeFromTop (4);
            dragHandles[(size_t) i]->setBounds (s.removeFromTop (22));
        }
    }
    // bottom bar
    {
        auto b = bottomArea.reduced (10, 8);
        previewButton.setBounds (b.removeFromLeft (84));
        b.removeFromLeft (6);
        midiOutButton.setBounds (b.removeFromLeft (84));
        b.removeFromLeft (6);
        monitorButton.setBounds (b.removeFromLeft (92));
        b.removeFromLeft (14);
        auto knobs = b.removeFromRight (150);
        auto k1 = knobs.removeFromLeft (75), k2 = knobs;
        masterSlider.setBounds (k1.removeFromLeft (32));
        masterLabel.setBounds (k1);
        reverbSlider.setBounds (k2.removeFromLeft (32));
        reverbLabel.setBounds (k2);
        b.removeFromRight (10);
        saveWavButton.setBounds (b.removeFromRight (100));
        b.removeFromRight (6);
        saveMidiButton.setBounds (b.removeFromRight (100));
        b.removeFromRight (10);
        statusLabel.setBounds (b);
    }
}
