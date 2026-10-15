// Integration tests for the plugin processor + editor (headless; run under xvfb on Linux CI).
//   plugin_tests [screenshot-dir]
#include "../source/PluginEditor.h"
#include "../source/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct FakePlayHead final : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

static void waitWhileBusy (MelodyverseProcessor& p)
{
    for (int i = 0; i < 600 && p.isBusy(); ++i) juce::Thread::sleep (50);
}

static void snapshot (juce::Component& c, const juce::File& dir, const juce::String& name)
{
    if (dir == juce::File()) return;
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto f = dir.getChildFile (name + ".png");
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.openedOk()) png.writeImageToStream (img, out);
    std::printf ("  screenshot %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File shots = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();
    if (shots != juce::File()) shots.createDirectory();
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("melodyverse_plugin_tests");
    tmp.createDirectory();

    MelodyverseProcessor p;
    FakePlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (48000, 512);

    std::printf ("[..] compose + play in sync with the host\n");
    p.settings.style = "punjabi_bhangra";
    p.generateNew (false);
    auto score = p.getScore();
    CHECK (score.style == "punjabi_bhangra");
    CHECK (score.track ("Melody") && ! score.track ("Melody")->notes.empty());
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    int noteOns = 0, drumOns = 0;
    float peak = 0;
    const double beatsPerBlock = 512.0 / 48000.0 * 120.0 / 60.0;
    for (int b = 0; b < 400; ++b)
    {
        buf.clear();
        midi.clear();
        p.processBlock (buf, midi);
        head.ppq += beatsPerBlock;
        for (const auto meta : midi)
        {
            auto m = meta.getMessage();
            if (m.isNoteOn()) { ++noteOns; drumOns += m.getChannel() == 10; }
        }
        peak = std::max (peak, buf.getMagnitude (0, 512));
    }
    std::printf ("    %d note-ons (%d drums), peak %.2f\n", noteOns, drumOns, peak);
    CHECK (noteOns > 50 && drumOns > 10);
    CHECK (peak > 0.05f && peak <= 1.0f);
    CHECK (std::abs (p.getHostBpm() - 120) < 1e-6);

    std::printf ("[..] muting a part silences its MIDI\n");
    p.params.getParameter ("drums_on")->setValueNotifyingHost (0.0f);
    drumOns = 0;
    for (int b = 0; b < 200; ++b)
    {
        buf.clear(); midi.clear();
        p.processBlock (buf, midi);
        head.ppq += beatsPerBlock;
        for (const auto meta : midi) drumOns += meta.getMessage().isNoteOn() && meta.getMessage().getChannel() == 10;
    }
    CHECK (drumOns == 0);
    p.params.getParameter ("drums_on")->setValueNotifyingHost (1.0f);

    std::printf ("[..] every style composes\n");
    for (auto& st : mv::styles())
    {
        p.settings.style = st.name;
        p.generateNew (true);
        CHECK (p.getScore().style == st.name);
    }
    p.settings.style = "english_pop";
    p.settings.progression = "not a progression";
    p.generateNew (false);
    CHECK (p.getStatus().startsWith ("Could not compose"));
    p.settings.progression = "Am F C G";
    p.generateNew (false);
    CHECK (p.getScore().chords.size() >= 4 && p.getScore().chords[1].chord->name() == "F");
    p.settings.progression = {};

    std::printf ("[..] export WAV, then listen to it\n");
    p.settings.style = "english_pop";
    p.settings.keyTonic = 9;
    p.settings.mode = "minor";
    p.settings.progression = "i bVI bIII bVII";
    p.settings.seed = 5;
    head.bpm = 100;
    buf.clear(); midi.clear();
    p.processBlock (buf, midi);  // the processor learns the host tempo from the play head
    CHECK (std::abs (p.getHostBpm() - 100) < 1e-6);
    p.generateNew (false);
    CHECK (std::abs (p.getScore().bpm - 100) < 1e-6);  // compositions follow the host tempo
    auto song = tmp.getChildFile ("song.wav");
    CHECK (p.saveWav (song));
    CHECK (song.getSize() > 100000);
    p.loadAudioFile (song);
    waitWhileBusy (p);
    auto a = p.getAnalysis();
    CHECK (a.has_value());
    if (a)
    {
        std::printf ("    heard %s, %.1f BPM, chords %s, %zu melody notes\n", a->key.name().c_str(), a->bpm, a->chordChart().c_str(), a->melody.size());
        CHECK (a->key.name() == "A minor");
        CHECK (std::abs (a->bpm - 100) < 3);
        CHECK (a->melody.size() > 20);
    }

    std::printf ("[..] transcribe and match\n");
    p.transcribe();
    auto tr = p.getScore();
    CHECK (p.getSourceDescription().startsWith ("Transcribed"));
    CHECK (tr.track ("Melody") && tr.track ("Melody")->notes.size() > 20);
    CHECK (tr.chords.size() >= 4);
    p.settings.style = "spanish_flamenco";
    p.settings.keyTonic = -1;
    p.settings.mode = {};
    p.matchStyle();
    auto mt = p.getScore();
    CHECK (mt.style == "spanish_flamenco");
    CHECK (mt.key.name() == "A minor");  // follows the song, not the style's default key
    CHECK (mt.track ("Melody") && mt.track ("Melody")->notes.size() > 10);

    std::printf ("[..] MIDI export\n");
    auto mid = p.writeMidiTemp (-1);
    CHECK (mid.existsAsFile());
    {
        juce::FileInputStream in (mid);
        juce::MidiFile mf;
        CHECK (mf.readFrom (in));
        CHECK (mf.getNumTracks() == 1 + (int) mt.tracks.size());
    }
    auto melOnly = p.writeMidiTemp (0);
    {
        juce::FileInputStream in (melOnly);
        juce::MidiFile mf;
        CHECK (mf.readFrom (in) && mf.getNumTracks() == 2);
        int ons = 0;
        for (int i = 0; i < mf.getTrack (1)->getNumEvents(); ++i) ons += mf.getTrack (1)->getEventPointer (i)->message.isNoteOn();
        CHECK (ons == (int) mt.track ("Melody")->notes.size());
    }

    std::printf ("[..] state save / restore\n");
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        MelodyverseProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        auto qs = q.getScore();
        CHECK (qs.style == mt.style);
        CHECK (qs.track ("Melody") && qs.track ("Melody")->notes.size() == mt.track ("Melody")->notes.size());
        CHECK (qs.chords.size() == mt.chords.size());
        CHECK (q.getAnalysis().has_value() && q.getAnalysis()->key.name() == "A minor");
        CHECK (q.settings.style == "spanish_flamenco");
    }

    std::printf ("[..] editor\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr);
        ed->setSize (1120, 680);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        snapshot (*ed, shots, "melodyverse_listen");
        p.settings.style = "hollywood_epic";
        p.settings.keyTonic = -1;
        p.settings.mode = {};
        p.generateNew (true);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        snapshot (*ed, shots, "melodyverse_compose");
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
