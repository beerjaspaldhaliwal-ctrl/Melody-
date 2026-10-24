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
    std::setvbuf (stdout, nullptr, _IONBF, 0);  // keep progress visible in CI logs even if the process crashes
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

    std::printf ("[..] vocals: study a sung line, suggest chords, arrange around it\n");
    {
        mv::GenerateOptions o;
        o.style = "punjabi_sad";
        o.key = mv::Key::parse ("A kafi");
        o.bpm = 80;
        o.bars = 8;
        o.seed = 11;
        o.progression = "i iv bVII i";
        o.chords = o.bass = o.drums = false;
        auto singer = mv::generate (o);
        singer.track ("Melody")->instrument = "bansuri";
        for (auto& n : singer.track ("Melody")->notes)
            if (std::fmod (n.end() + 1e-6, 8.0) < 0.01 && n.duration > 1.0) n.duration -= 0.75;
        std::vector<float> l, r;
        mv::renderScore (singer, 44100, l, r, 1.0, 1, 0.0f);
        juce::AudioBuffer<float> vb (2, (int) l.size());
        for (size_t i = 0; i < l.size(); ++i) { vb.setSample (0, (int) i, l[i]); vb.setSample (1, (int) i, r[i]); }
        auto vf = tmp.getChildFile ("vocal.wav");
        vf.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (vf), 44100, 2, 16, {}, 0));
            CHECK (w != nullptr);
            if (w) w->writeFromAudioSampleBuffer (vb, 0, vb.getNumSamples());
        }
        p.loadVocalFile (vf);
        waitWhileBusy (p);
        auto vr = p.getVocalReport();
        CHECK (vr.has_value());
        auto opts = p.getVocalOptions();
        CHECK (opts.size() == 5);
        if (vr)
        {
            std::printf ("    Sa %s, thaat %s, %.0f BPM, %zu notes, %d meend; top: %s (%.0f%%)\n", mv::pcName (vr->key.tonic).c_str(), vr->thaat.c_str(), vr->bpm,
                         vr->notes.size(), vr->count ("meend"), opts.empty() ? "-" : opts[0].name.c_str(), opts.empty() ? 0.0 : opts[0].fit * 100);
            CHECK (vr->key.tonic == 9);
            CHECK (! vr->insights.empty());
        }
        p.applyVocalOption (0);
        auto arr = p.getScore();
        CHECK (arr.track ("Vocal") && ! arr.track ("Vocal")->notes.empty());
        CHECK (arr.track ("Chords") && arr.track ("Melody"));
        CHECK (p.getVocalOptionIndex() == 0);
        p.setVocalIdea (1);
        auto harm = p.getScore();
        CHECK (harm.track ("Melody")->notes.size() == harm.track ("Vocal")->notes.size());
        // the vocal guide plays on its own MIDI channel (4)
        bool vocalMidi = false;
        head.ppq = 0;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);  // let the UI thread recycle the old pattern, as a host would
        for (int b = 0; b < 300 && ! vocalMidi; ++b)
        {
            buf.clear(); midi.clear();
            p.processBlock (buf, midi);
            head.ppq += beatsPerBlock;
            for (const auto meta : midi) vocalMidi |= meta.getMessage().isNoteOn() && meta.getMessage().getChannel() == 4;
        }
        CHECK (vocalMidi);
        juce::MemoryBlock st;
        p.getStateInformation (st);
        MelodyverseProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (q.getVocalReport().has_value() && q.getVocalOptions().size() == 5);
        CHECK (q.getVocalOptionIndex() == 0 && q.getVocalIdea() == 1);
        CHECK (q.getScore().track ("Vocal") && q.getScore().track ("Vocal")->notes.size() == harm.track ("Vocal")->notes.size());
        q.newVocalIdea();  // re-arranging after a reload must work from the saved report
        CHECK (q.getScore().track ("Vocal") && q.getScore().track ("Chords"));
        p.setVocalIdea (0);
    }

    std::printf ("[..] melody lab: chords from a MIDI file -> 8 melody variations\n");
    {
        juce::MidiMessageSequence chordsTrack;
        const int tpq = 960;
        chordsTrack.addEvent (juce::MidiMessage::tempoMetaEvent (625000), 0);  // 96 BPM
        const std::vector<std::vector<int>> prog { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };  // Am F C G
        for (int rep = 0; rep < 2; ++rep)
            for (size_t c = 0; c < prog.size(); ++c)
                for (int note : prog[c])
                {
                    double t0 = (rep * 16 + (int) c * 4) * tpq, t1 = t0 + 4 * tpq - 10;
                    chordsTrack.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 90), t0);
                    chordsTrack.addEvent (juce::MidiMessage::noteOff (1, note), t1);
                }
        chordsTrack.updateMatchedPairs();
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (tpq);
        mf.addTrack (chordsTrack);
        auto chordFile = tmp.getChildFile ("my_chords.mid");
        chordFile.deleteFile();
        {
            juce::FileOutputStream out (chordFile);
            CHECK (mf.writeTo (out));
        }
        p.settings.style = "punjabi_bhangra";
        CHECK (p.loadChordMidi (chordFile));
        auto chords = p.getLabChords();
        std::string names;
        for (auto& c : chords) names += c.chord->name() + " ";
        std::printf ("    read: %s| key %s\n", names.c_str(), p.getLabKey().name().c_str());
        CHECK (chords.size() == 8 && chords[0].chord->name() == "Am" && chords[1].chord->name() == "F" && chords[2].chord->name() == "C" && chords[3].chord->name() == "G");
        CHECK (p.getLabKey().name() == "A minor");
        auto vars = p.getVariations();
        CHECK (vars.size() == 8);
        CHECK (p.getLabSelected() == 0);  // the first one is auditioned straight away
        auto sc = p.getScore();
        CHECK (sc.track ("Melody") && sc.track ("Melody")->notes.size() == vars[0].notes.size());
        CHECK (sc.track ("Melody")->instrument == vars[0].instrument);
        CHECK (sc.chords.size() == chords.size());
        p.applyVariation (5);
        CHECK (p.getLabSelected() == 5 && p.getScore().track ("Melody")->notes.size() == vars[5].notes.size());
        p.generateVariations (true);  // more like #6
        auto takes = p.getVariations();
        CHECK (takes.size() == 8);
        for (auto& t : takes) CHECK (t.character == vars[5].character);
        juce::MemoryBlock st;
        p.getStateInformation (st);
        MelodyverseProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        auto restored = q.getVariations();
        CHECK (restored.size() == takes.size());
        if (restored.size() == takes.size())
            for (size_t i = 0; i < takes.size(); ++i) CHECK (restored[i].notes.size() == takes[i].notes.size());
        CHECK (q.getLabChords().size() == chords.size());
        p.generateVariations (false);  // back to one of each character for the screenshot
        CHECK (p.useCurrentChords());  // the arrangement's own chords work as a source too
        CHECK (p.getVariations().size() == 8);
        CHECK (p.loadChordMidi (chordFile));
    }

    std::printf ("[..] editor\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr);
        ed->setSize (1220, 720);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        snapshot (*ed, shots, "melodyverse_listen");
        if (auto* me = dynamic_cast<MelodyverseEditor*> (ed.get()))
        {
            p.applyVocalOption (0);
            me->showTab (1);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            snapshot (*ed, shots, "melodyverse_vocals");
            p.loadChordMidi (tmp.getChildFile ("my_chords.mid"));
            me->showTab (2);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            snapshot (*ed, shots, "melodyverse_melodies");
            me->showTab (0);
        }
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
