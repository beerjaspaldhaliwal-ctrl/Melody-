#include "PluginProcessor.h"

#include "MidiFile.h"
#include "PluginEditor.h"
#include "Styles.h"

namespace
{
constexpr double kMaxCaptureSeconds = 90.0;
constexpr double kMaxFileSeconds = 600.0;

juce::String d2s (double v) { return juce::String (v, 4); }

// ---- compact text serialisation of scores / analyses for plugin state ----
juce::String chordToString (const std::optional<mv::Chord>& c)
{
    if (! c) return "-1,maj,-1";
    return juce::String (c->root) + "," + juce::String (c->quality) + "," + juce::String (c->bass);
}

std::optional<mv::Chord> chordFromTokens (const juce::StringArray& t, int i)
{
    int root = t[i].getIntValue();
    if (root < 0 || ! mv::hasChordQuality (t[i + 1].toStdString())) return std::nullopt;
    return mv::Chord { root, t[i + 1].toStdString(), t[i + 2].getIntValue() };
}

juce::ValueTree scoreToTree (const mv::Score& s, const juce::String& source)
{
    juce::ValueTree v ("Score");
    v.setProperty ("bpm", s.bpm, nullptr);
    v.setProperty ("keyTonic", s.key.tonic, nullptr);
    v.setProperty ("keyMode", juce::String (s.key.mode), nullptr);
    v.setProperty ("style", juce::String (s.style), nullptr);
    v.setProperty ("length", s.lengthBeats, nullptr);
    v.setProperty ("source", source, nullptr);
    juce::StringArray chords;
    for (auto& c : s.chords) chords.add (d2s (c.start) + "," + d2s (c.duration) + "," + chordToString (c.chord));
    v.setProperty ("chords", chords.joinIntoString (";"), nullptr);
    for (auto& t : s.tracks)
    {
        juce::ValueTree tv ("Track");
        tv.setProperty ("name", juce::String (t.name), nullptr);
        tv.setProperty ("instrument", juce::String (t.instrument), nullptr);
        tv.setProperty ("drums", t.isDrums, nullptr);
        juce::StringArray items;
        for (auto& n : t.notes)
            items.add (d2s (n.start) + "," + d2s (n.duration) + "," + juce::String (n.pitch) + "," + juce::String (n.velocity) + "," + d2s (n.bendFrom));
        for (auto& h : t.hits)
            items.add (d2s (h.beat) + "," + juce::String (h.name) + "," + juce::String (h.velocity));
        tv.setProperty ("data", items.joinIntoString (";"), nullptr);
        v.appendChild (tv, nullptr);
    }
    return v;
}

mv::Score scoreFromTree (const juce::ValueTree& v)
{
    mv::Score s;
    s.bpm = v.getProperty ("bpm", 120.0);
    juce::String mode = v.getProperty ("keyMode", "major").toString();
    s.key = { (int) v.getProperty ("keyTonic", 0), mv::hasScale (mode.toStdString()) ? mode.toStdString() : "major" };
    s.style = v.getProperty ("style").toString().toStdString();
    s.lengthBeats = v.getProperty ("length", 32.0);
    for (auto& item : juce::StringArray::fromTokens (v.getProperty ("chords").toString(), ";", ""))
    {
        auto t = juce::StringArray::fromTokens (item, ",", "");
        if (t.size() >= 5) s.chords.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), chordFromTokens (t, 2) });
    }
    for (auto tv : v)
    {
        if (! tv.hasType ("Track")) continue;
        mv::Track t;
        t.name = tv.getProperty ("name").toString().toStdString();
        t.instrument = tv.getProperty ("instrument").toString().toStdString();
        t.isDrums = tv.getProperty ("drums", false);
        for (auto& item : juce::StringArray::fromTokens (tv.getProperty ("data").toString(), ";", ""))
        {
            auto f = juce::StringArray::fromTokens (item, ",", "");
            if (t.isDrums && f.size() >= 3) t.hits.push_back ({ f[0].getDoubleValue(), f[1].toStdString(), f[2].getIntValue() });
            else if (! t.isDrums && f.size() >= 5)
                t.notes.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), f[3].getIntValue(), f[4].getDoubleValue() });
        }
        s.tracks.push_back (std::move (t));
    }
    return s;
}

juce::ValueTree analysisToTree (const mv::Analysis& a, const juce::String& label)
{
    juce::ValueTree v ("Analysis");
    v.setProperty ("label", label, nullptr);
    v.setProperty ("bpm", a.bpm, nullptr);
    v.setProperty ("duration", a.duration, nullptr);
    v.setProperty ("keyTonic", a.key.tonic, nullptr);
    v.setProperty ("keyMode", juce::String (a.key.mode), nullptr);
    v.setProperty ("keyConfidence", a.keyConfidence, nullptr);
    juce::StringArray beats, chords, melody;
    for (double b : a.beatTimes) beats.add (d2s (b));
    for (auto& c : a.chords) chords.add (d2s (c.start) + "," + d2s (c.end) + "," + chordToString (c.chord) + "," + d2s (c.confidence));
    for (auto& n : a.melody) melody.add (d2s (n.start) + "," + d2s (n.end) + "," + juce::String (n.pitch) + "," + juce::String (n.velocity));
    v.setProperty ("beats", beats.joinIntoString (";"), nullptr);
    v.setProperty ("chords", chords.joinIntoString (";"), nullptr);
    v.setProperty ("melody", melody.joinIntoString (";"), nullptr);
    return v;
}

mv::Analysis analysisFromTree (const juce::ValueTree& v)
{
    mv::Analysis a;
    a.bpm = v.getProperty ("bpm", 120.0);
    a.duration = v.getProperty ("duration", 0.0);
    juce::String mode = v.getProperty ("keyMode", "major").toString();
    a.key = { (int) v.getProperty ("keyTonic", 0), mv::hasScale (mode.toStdString()) ? mode.toStdString() : "major" };
    a.keyConfidence = v.getProperty ("keyConfidence", 0.0);
    for (auto& b : juce::StringArray::fromTokens (v.getProperty ("beats").toString(), ";", "")) a.beatTimes.push_back (b.getDoubleValue());
    for (auto& item : juce::StringArray::fromTokens (v.getProperty ("chords").toString(), ";", ""))
    {
        auto t = juce::StringArray::fromTokens (item, ",", "");
        if (t.size() >= 6) a.chords.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), chordFromTokens (t, 2), t[5].getDoubleValue() });
    }
    for (auto& item : juce::StringArray::fromTokens (v.getProperty ("melody").toString(), ";", ""))
    {
        auto t = juce::StringArray::fromTokens (item, ",", "");
        if (t.size() >= 4) a.melody.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), t[2].getIntValue(), t[3].getIntValue() });
    }
    return a;
}

juce::String eventsToString (const std::vector<mv::ChordEvent>& evs)
{
    juce::StringArray a;
    for (auto& c : evs) a.add (d2s (c.start) + "," + d2s (c.duration) + "," + chordToString (c.chord));
    return a.joinIntoString (";");
}

std::vector<mv::ChordEvent> eventsFromString (const juce::String& s)
{
    std::vector<mv::ChordEvent> out;
    for (auto& item : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto t = juce::StringArray::fromTokens (item, ",", "");
        if (t.size() >= 5) out.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), chordFromTokens (t, 2) });
    }
    return out;
}

juce::ValueTree vocalToTree (const mv::VocalReport& r, const std::vector<mv::ProgressionOption>& opts, const juce::String& label, int option, int idea)
{
    juce::ValueTree v ("Vocal");
    v.setProperty ("label", label, nullptr);
    v.setProperty ("option", option, nullptr);
    v.setProperty ("idea", idea, nullptr);
    v.setProperty ("bpm", r.bpm, nullptr);
    v.setProperty ("origin", r.originSeconds, nullptr);
    v.setProperty ("duration", r.duration, nullptr);
    v.setProperty ("keyTonic", r.key.tonic, nullptr);
    v.setProperty ("keyMode", juce::String (r.key.mode), nullptr);
    v.setProperty ("keyConfidence", r.keyConfidence, nullptr);
    v.setProperty ("thaat", juce::String (r.thaat), nullptr);
    juce::StringArray notes, insights;
    for (size_t i = 0; i < r.notes.size(); ++i)
        notes.add (d2s (r.notes[i].start) + "," + d2s (r.notes[i].end) + "," + juce::String (r.notes[i].pitch) + "," + juce::String (r.notes[i].velocity)
                   + "," + juce::String (i < r.bendFrom.size() ? r.bendFrom[i] : 0) + "," + juce::String (i < r.ornamental.size() && r.ornamental[i] ? 1 : 0));
    for (auto& s : r.insights) insights.add (s);
    v.setProperty ("notes", notes.joinIntoString (";"), nullptr);
    v.setProperty ("insights", insights.joinIntoString ("\n"), nullptr);
    for (auto& o : opts)
    {
        juce::ValueTree ov ("Option");
        ov.setProperty ("name", juce::String (o.name), nullptr);
        ov.setProperty ("description", juce::String (o.description), nullptr);
        ov.setProperty ("style", juce::String (o.style), nullptr);
        ov.setProperty ("roman", juce::String (o.roman), nullptr);
        ov.setProperty ("fit", o.fit, nullptr);
        ov.setProperty ("chords", eventsToString (o.chords), nullptr);
        v.appendChild (ov, nullptr);
    }
    return v;
}

void vocalFromTree (const juce::ValueTree& v, mv::VocalReport& r, std::vector<mv::ProgressionOption>& opts)
{
    r.bpm = v.getProperty ("bpm", 120.0);
    r.originSeconds = v.getProperty ("origin", 0.0);
    r.duration = v.getProperty ("duration", 0.0);
    juce::String mode = v.getProperty ("keyMode", "major").toString();
    r.key = { (int) v.getProperty ("keyTonic", 0), mv::hasScale (mode.toStdString()) ? mode.toStdString() : "major" };
    r.keyConfidence = v.getProperty ("keyConfidence", 0.0);
    r.thaat = v.getProperty ("thaat").toString().toStdString();
    for (auto& item : juce::StringArray::fromTokens (v.getProperty ("notes").toString(), ";", ""))
    {
        auto t = juce::StringArray::fromTokens (item, ",", "");
        if (t.size() < 6) continue;
        r.notes.push_back ({ t[0].getDoubleValue(), t[1].getDoubleValue(), t[2].getIntValue(), t[3].getIntValue() });
        r.bendFrom.push_back (t[4].getIntValue());
        r.ornamental.push_back (t[5].getIntValue() != 0);
        r.attack.push_back (t[0].getDoubleValue());
    }
    for (auto& line : juce::StringArray::fromTokens (v.getProperty ("insights").toString(), "\n", "")) r.insights.push_back (line.toStdString());
    for (auto ov : v)
        if (ov.hasType ("Option"))
            opts.push_back ({ ov.getProperty ("name").toString().toStdString(), ov.getProperty ("description").toString().toStdString(),
                              ov.getProperty ("style").toString().toStdString(), ov.getProperty ("roman").toString().toStdString(),
                              eventsFromString (ov.getProperty ("chords").toString()), (double) ov.getProperty ("fit", 0.0) });
}

juce::String safeName (juce::String s) { return s.replaceCharacters (" /\\:#", "_____").retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"); }
} // namespace

// ---------------------------------------------------------------------------

MelodyverseProcessor::MelodyverseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Listen", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    midiOut.send = [this] (int offset, uint8_t statusByte, uint8_t d1, uint8_t d2) {
        if (currentMidi != nullptr) currentMidi->addEvent (juce::MidiMessage (statusByte, d1, d2), offset);
    };
    for (int p = 0; p < mv::kNumParts; ++p)
    {
        partOn[p] = params.getRawParameterValue (juce::String (kPartIds[p]) + "_on");
        partVol[p] = params.getRawParameterValue (juce::String (kPartIds[p]) + "_vol");
    }
    masterParam = params.getRawParameterValue ("master");
    reverbParam = params.getRawParameterValue ("reverb");
    midiOutParam = params.getRawParameterValue ("midi_out");
    monitorParam = params.getRawParameterValue ("monitor");
    previewParam = params.getRawParameterValue ("preview");
    if (juce::JUCEApplicationBase::isStandaloneApp())  // no DAW transport: play the loop straight away
        if (auto* preview = params.getParameter ("preview")) preview->setValueNotifyingHost (1.0f);
    generateNew (false);
    startTimerHz (10);
}

MelodyverseProcessor::~MelodyverseProcessor()
{
    stopTimer();
    cancelAnalysis = true;
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MelodyverseProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const char* names[] = { "Melody", "Chords", "Bass", "Drums", "Vocal guide" };
    const float vols[] = { 0.9f, 0.5f, 0.75f, 0.7f, 0.5f };
    for (int p = 0; p < mv::kNumParts; ++p)
    {
        juce::String id (kPartIds[p]);
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id + "_on", 1 }, juce::String (names[p]) + " On", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "_vol", 1 }, juce::String (names[p]) + " Volume",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), vols[p]));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "master", 1 }, "Master", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb", 1 }, "Reverb", juce::NormalisableRange<float> (0.0f, 1.0f), 0.16f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "midi_out", 1 }, "MIDI Out", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "monitor", 1 }, "Monitor Input", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "preview", 1 }, "Preview (play without transport)", false));
    return layout;
}

bool MelodyverseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void MelodyverseProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    player.prepare (sampleRate);
    captureBuffer.assign ((size_t) (kMaxCaptureSeconds * sampleRate), 0.0f);
    captureLength = 0;
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    if (! pendingSequence)
        pendingSequence = mv::Player::buildSequence (getScore());
}

void MelodyverseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();

    // 1. listen: record the input while capture is armed
    if (capturing.load() && numIn > 0)
    {
        size_t len = captureLength.load();
        const size_t room = captureBuffer.size() - len;
        const int take = (int) std::min<size_t> ((size_t) n, room);
        for (int i = 0; i < take; ++i)
        {
            float s = 0;
            for (int c = 0; c < numIn; ++c) s += buffer.getSample (c, i);
            captureBuffer[len + (size_t) i] = s / (float) numIn;
        }
        captureLength = len + (size_t) take;
        if (take < n) capturing = false;  // full: the timer notices and starts the analysis
    }
    if (monitorParam->load() < 0.5f)
        for (int c = 0; c < numIn; ++c) buffer.clear (c, 0, n);
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);

    // 2. pick up a newly composed pattern (never blocks)
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked() && pendingSequence && ! retiredSequence)
            retiredSequence = player.exchange (std::move (pendingSequence));
    }

    // 3. transport
    bool playing = false;
    double ppq = 0, bpm = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }
    hostBpm = bpm;
    if (! playing && previewParam->load() > 0.5f)
    {
        playing = true;
        if (bpm <= 0) bpm = scoreBpm.load();
        ppq = previewPpq;
        previewPpq += n * bpm / 60.0 / sampleRateHz;
    }
    else if (! playing)
        previewPpq = 0;
    if (bpm <= 0) bpm = 120.0;
    hostPlaying = playing;

    // 4. play
    for (int p = 0; p < mv::kNumParts; ++p)
    {
        player.partEnabled[(size_t) p] = partOn[p]->load() > 0.5f;
        player.synth.partGain[(size_t) p] = partVol[p]->load();
    }
    player.synth.masterGain = masterParam->load();
    player.synth.reverbMix = reverbParam->load();
    player.sendMidi = midiOutParam->load() > 0.5f;

    // the synth adds on top of the (optionally monitored) input
    currentMidi = &midi;
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (numOut > 1 ? 1 : 0);
    player.process (ppq, bpm, playing, n, left, right, &midiOut);
    currentMidi = nullptr;
    if (playing && player.loopLength() > 0)
        playheadBeat = std::fmod (std::max (0.0, ppq), player.loopLength());
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void MelodyverseProcessor::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock lock (modelLock);
        status = s;
    }
    ++version;
}

juce::String MelodyverseProcessor::getStatus() const
{
    const juce::ScopedLock lock (modelLock);
    return status;
}

juce::String MelodyverseProcessor::getSourceDescription() const
{
    const juce::ScopedLock lock (modelLock);
    return source;
}

juce::String MelodyverseProcessor::getAnalysisLabel() const
{
    const juce::ScopedLock lock (modelLock);
    return analysisLabel;
}

mv::Score MelodyverseProcessor::getScore() const
{
    const juce::ScopedLock lock (modelLock);
    return score;
}

std::optional<mv::Analysis> MelodyverseProcessor::getAnalysis() const
{
    const juce::ScopedLock lock (modelLock);
    return analysis;
}

void MelodyverseProcessor::commitScore (mv::Score s, const juce::String& src)
{
    const int styleIdx = mv::styleIndex (s.style);
    for (int p : { 0, 1, 2, 4 })
        if (auto* t = s.track (mv::partName ((mv::Part) p)); t && ! t->isDrums)
        {
            if (settings.instruments[(size_t) p].isNotEmpty())
                t->instrument = settings.instruments[(size_t) p].toStdString();
            else if (p == 4)
                t->instrument = "bansuri";
            else if (styleIdx >= 0)  // back to the style's own colour
            {
                const auto& st = mv::styles()[(size_t) styleIdx];
                t->instrument = p == 0 ? st.melodyInstrument : p == 1 ? st.chordInstrument : st.bassInstrument;
            }
            else
                t->instrument = p == 0 ? "piano" : p == 1 ? "strings" : "bass";
        }
    scoreBpm = s.bpm > 0 ? s.bpm : 120.0;
    auto seq = mv::Player::buildSequence (s);
    {
        const juce::ScopedLock lock (modelLock);
        score = std::move (s);
        source = src;
    }
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        pendingSequence = std::move (seq);
    }
    ++version;
}

mv::Key MelodyverseProcessor::currentKey (const mv::Style& st) const
{
    auto k = mv::Key::parse (st.defaultKey);
    if (settings.keyTonic >= 0) k.tonic = settings.keyTonic;
    if (settings.mode.isNotEmpty() && mv::hasScale (settings.mode.toStdString())) k.mode = settings.mode.toStdString();
    return k;
}

void MelodyverseProcessor::generateNew (bool newSeed)
{
    try
    {
        if (newSeed) settings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (1000000);
        const mv::Style st = mv::getStyle (settings.style.toStdString());
        mv::GenerateOptions o;
        o.style = st.name;
        o.key = currentKey (st);
        o.bpm = hostBpm.load() > 0 ? hostBpm.load() : 0;
        o.bars = settings.bars;
        o.seed = settings.seed;
        o.progression = settings.progression.trim().toStdString();
        auto s = mv::generate (o);
        commitScore (std::move (s), "Composed: " + juce::String (st.name) + ", seed " + juce::String (settings.seed));
        setStatus ("Composed a new " + juce::String (st.name).replace ("_", " ") + " idea in " + juce::String (getScore().key.name()));
    }
    catch (const std::exception& e)
    {
        setStatus (juce::String ("Could not compose: ") + e.what());
    }
}

void MelodyverseProcessor::transcribe()
{
    auto a = getAnalysis();
    if (! a) { setStatus ("Load or capture a song first"); return; }
    auto s = mv::analysisToScore (*a, settings.quantize,
                                  settings.instruments[0].isNotEmpty() ? settings.instruments[0].toStdString() : "piano",
                                  settings.instruments[1].isNotEmpty() ? settings.instruments[1].toStdString() : "strings");
    s.bpm = a->bpm;
    const juce::String label = [this] { const juce::ScopedLock l (modelLock); return analysisLabel; }();
    commitScore (std::move (s), "Transcribed: " + label);
    setStatus ("Transcribed " + juce::String ((int) a->melody.size()) + " melody notes and " + juce::String ((int) a->chords.size()) + " chords");
}

void MelodyverseProcessor::matchStyle()
{
    auto a = getAnalysis();
    if (! a) { setStatus ("Load or capture a song first"); return; }
    try
    {
        const mv::Style st = mv::getStyle (settings.style.toStdString());
        mv::GenerateOptions o;
        o.style = st.name;
        mv::Key k = a->key;
        if (settings.keyTonic >= 0) k.tonic = settings.keyTonic;
        if (settings.mode.isNotEmpty()) k.mode = settings.mode.toStdString();
        o.key = k;
        o.bpm = a->bpm;
        o.seed = settings.seed;
        o.timeline = mv::harmonyTimeline (*a, settings.quantize > 0 ? settings.quantize : 16);
        if (o.timeline.empty()) { setStatus ("No chords were detected in that audio"); return; }
        auto s = mv::generate (o);
        commitScore (std::move (s), "New " + juce::String (st.name) + " melody over the song (seed " + juce::String (settings.seed) + ")");
        setStatus ("Wrote a new " + juce::String (st.name).replace ("_", " ") + " part over the song's chords");
    }
    catch (const std::exception& e)
    {
        setStatus (juce::String ("Could not compose: ") + e.what());
    }
}

void MelodyverseProcessor::setPartInstrument (int part, const juce::String& instrument)
{
    if (part < 0 || part >= mv::kNumParts) return;
    settings.instruments[(size_t) part] = instrument;
    auto s = getScore();
    const juce::String src = getSourceDescription();
    commitScore (std::move (s), src);
}

void MelodyverseProcessor::startAnalysis (std::vector<float> mono, double sr, double fixedBpm, const juce::String& label)
{
    if (busy.exchange (true)) { setStatus ("Still listening to the previous audio..."); return; }
    cancelAnalysis = false;
    progress = 0;
    setStatus ("Listening to " + label + "...");
    pool.addJob ([this, mono = std::move (mono), sr, fixedBpm, label]() mutable {
        mv::AnalyzeOptions o;
        o.fixedBpm = fixedBpm;
        o.cancel = &cancelAnalysis;
        o.progress = [this] (float p) { progress = p; };
        auto a = mv::analyze (mono.data(), mono.size(), sr, o);
        if (! cancelAnalysis.load())
        {
            {
                const juce::ScopedLock lock (modelLock);
                analysis = a;
                analysisLabel = label;
            }
            setStatus ("Heard " + label + ": " + juce::String (a.key.name()) + ", " + juce::String (a.bpm, 1) + " BPM - now Transcribe it or write a New Melody over it");
        }
        progress = 1;
        busy = false;
        ++version;
    });
}

bool MelodyverseProcessor::readAudioFile (const juce::File& file, std::vector<float>& mono, double& sr)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader) { setStatus ("Can't read " + file.getFileName() + " (try WAV, AIFF, FLAC, OGG or MP3)"); return false; }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxFileSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    mono.assign ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        auto* d = buf.getReadPointer (c);
        for (int i = 0; i < len; ++i) mono[(size_t) i] += d[i] / (float) buf.getNumChannels();
    }
    sr = reader->sampleRate;
    return true;
}

void MelodyverseProcessor::loadAudioFile (const juce::File& file)
{
    if (busy.load()) { setStatus ("Still listening to the previous audio..."); return; }
    std::vector<float> mono;
    double sr = 44100;
    if (readAudioFile (file, mono, sr)) startAnalysis (std::move (mono), sr, 0, file.getFileNameWithoutExtension());
}

void MelodyverseProcessor::loadVocalFile (const juce::File& file)
{
    if (busy.load()) { setStatus ("Still listening to the previous audio..."); return; }
    std::vector<float> mono;
    double sr = 44100;
    if (readAudioFile (file, mono, sr)) startVocalAnalysis (std::move (mono), sr, 0, file.getFileNameWithoutExtension());
}

void MelodyverseProcessor::startVocalAnalysis (std::vector<float> mono, double sr, double fixedBpm, const juce::String& label)
{
    if (busy.exchange (true)) { setStatus ("Still listening to the previous audio..."); return; }
    cancelAnalysis = false;
    progress = 0;
    setStatus ("Studying the vocal: " + label + "...");
    pool.addJob ([this, mono = std::move (mono), sr, fixedBpm, label]() mutable {
        mv::AnalyzeOptions o;
        o.fixedBpm = fixedBpm;
        o.cancel = &cancelAnalysis;
        o.progress = [this] (float p) { progress = p * 0.9f; };
        auto report = mv::analyzeVocal (mono.data(), mono.size(), sr, o);
        auto options = mv::suggestProgressions (report, 5);
        if (! cancelAnalysis.load())
        {
            {
                const juce::ScopedLock lock (modelLock);
                vocalReport = report;
                vocalOptions = options;
                vocalLabel = label;
                vocalOption = -1;
            }
            if (report.notes.empty())
                setStatus ("No clear singing found in " + label + " - use a dry vocal (a cappella or an isolated vocal stem)");
            else
                setStatus ("Vocal studied: Sa = " + juce::String (mv::pcName (report.key.tonic, report.key.prefersFlats())) + ", " + juce::String (report.thaat)
                           + " thaat, " + juce::String (report.bpm, 0) + " BPM - pick a chord progression on the right");
        }
        progress = 1;
        busy = false;
        ++version;
    });
}

std::optional<mv::VocalReport> MelodyverseProcessor::getVocalReport() const
{
    const juce::ScopedLock lock (modelLock);
    return vocalReport;
}

std::vector<mv::ProgressionOption> MelodyverseProcessor::getVocalOptions() const
{
    const juce::ScopedLock lock (modelLock);
    return vocalOptions;
}

juce::String MelodyverseProcessor::getVocalLabel() const
{
    const juce::ScopedLock lock (modelLock);
    return vocalLabel;
}

void MelodyverseProcessor::applyVocalOption (int index)
{
    std::optional<mv::VocalReport> r;
    mv::ProgressionOption opt;
    {
        const juce::ScopedLock lock (modelLock);
        if (! vocalReport || index < 0 || index >= (int) vocalOptions.size()) return;
        r = vocalReport;
        opt = vocalOptions[(size_t) index];
        vocalOption = index;
    }
    auto s = mv::arrangeForVocal (*r, opt, (mv::VocalMelodyIdea) vocalIdea, settings.seed);
    static const char* const ideas[] = { "answer phrases", "harmony a 3rd above", "harmony a 6th below" };
    commitScore (std::move (s), juce::String (opt.name) + " + " + ideas[juce::jlimit (0, 2, vocalIdea)] + " for " + getVocalLabel());
    setStatus ("Arranged \"" + juce::String (opt.name) + "\" around the vocal: " + juce::String (opt.roman)
               + ".  Line up: " + juce::String (r->originSeconds, 2) + " s into your vocal = bar 1");
}

void MelodyverseProcessor::newVocalIdea()
{
    settings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (1000000);
    applyVocalOption (vocalOption < 0 ? 0 : vocalOption);
}

void MelodyverseProcessor::setVocalIdea (int idea)
{
    vocalIdea = juce::jlimit (0, 2, idea);
    if (vocalOption >= 0) applyVocalOption (vocalOption);
}

void MelodyverseProcessor::setCapturing (bool shouldCapture, CaptureTarget target)
{
    if (shouldCapture)
    {
        if (busy.load()) { setStatus ("Still listening to the previous audio..."); return; }
        captureTarget = target;
        captureLength = 0;
        capturing = true;
        setStatus (target == CaptureTarget::Vocal ? "Recording the vocal from the plugin input... sing, or play your vocal track, then press Stop"
                                                  : "Capturing the plugin input... press play in your DAW (or sing into the mic in standalone)");
        return;
    }
    if (! capturing.exchange (false) && captureLength.load() == 0) return;
    const size_t len = captureLength.load();
    captureLength = 0;
    if (len < (size_t) (sampleRateHz * 2)) { setStatus ("Captured less than 2 seconds - nothing to analyse"); return; }
    std::vector<float> mono (captureBuffer.begin(), captureBuffer.begin() + (long) len);
    if (captureTarget == CaptureTarget::Vocal)
        startVocalAnalysis (std::move (mono), sampleRateHz, hostBpm.load(), "recorded vocal");
    else
        startAnalysis (std::move (mono), sampleRateHz, hostBpm.load(), "captured input");
}

double MelodyverseProcessor::capturedSeconds() const { return (double) captureLength.load() / sampleRateHz; }

void MelodyverseProcessor::timerCallback()
{
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        retiredSequence.reset();  // free the old pattern off the audio thread
    }
    if (! capturing.load() && captureLength.load() > 0 && captureLength.load() >= captureBuffer.size())
        setCapturing (false, captureTarget);  // buffer filled up
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

juce::File MelodyverseProcessor::writeMidiTemp (int part)
{
    auto s = getScore();
    juce::String partLabel = part < 0 ? "full" : juce::String (mv::partName ((mv::Part) part)).toLowerCase();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Melodyverse");
    dir.createDirectory();
    auto name = safeName (juce::String (s.style.empty() ? "melodyverse" : s.style) + "_" + juce::String (s.key.name()) + "_" + partLabel);
    auto file = dir.getChildFile (name + ".mid");
    saveMidi (file, part);
    return file;
}

bool MelodyverseProcessor::saveMidi (const juce::File& file, int part)
{
    auto s = getScore();
    std::vector<std::string> only;
    if (part >= 0) only.push_back (mv::partName ((mv::Part) part));
    auto bytes = mv::writeMidi (s, only);
    file.deleteFile();
    return file.replaceWithData (bytes.data(), bytes.size());
}

bool MelodyverseProcessor::saveWav (const juce::File& file)
{
    auto s = getScore();
    for (int p = 0; p < mv::kNumParts; ++p)  // export what you hear: muted parts stay muted
        if (partOn[p]->load() < 0.5f)
            if (auto* t = s.track (mv::partName ((mv::Part) p))) { t->notes.clear(); t->hits.clear(); }
    std::vector<float> l, r;
    mv::renderScore (s, 44100, l, r, 2.0);
    float peak = 1e-9f;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
    juce::AudioBuffer<float> buf (2, (int) l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        buf.setSample (0, (int) i, l[i] / peak * 0.89f);
        buf.setSample (1, (int) i, r[i] / peak * 0.89f);
    }
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (file));
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 2, 16, {}, 0));
    if (! writer) return false;
    stream.release();  // the writer owns it now
    return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void MelodyverseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("MelodyverseState");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (params.copyState(), nullptr);
    juce::ValueTree st ("Settings");
    st.setProperty ("style", settings.style, nullptr);
    st.setProperty ("keyTonic", settings.keyTonic, nullptr);
    st.setProperty ("mode", settings.mode, nullptr);
    st.setProperty ("bars", settings.bars, nullptr);
    st.setProperty ("seed", (juce::int64) settings.seed, nullptr);
    st.setProperty ("progression", settings.progression, nullptr);
    st.setProperty ("quantize", settings.quantize, nullptr);
    for (int p = 0; p < mv::kNumParts; ++p) st.setProperty ("inst" + juce::String (p), settings.instruments[(size_t) p], nullptr);
    root.appendChild (st, nullptr);
    {
        const juce::ScopedLock lock (modelLock);
        root.appendChild (scoreToTree (score, source), nullptr);
        if (analysis) root.appendChild (analysisToTree (*analysis, analysisLabel), nullptr);
        if (vocalReport) root.appendChild (vocalToTree (*vocalReport, vocalOptions, vocalLabel, vocalOption, vocalIdea), nullptr);
    }
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void MelodyverseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (! xml) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("MelodyverseState")) return;
    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);
    auto st = root.getChildWithName ("Settings");
    if (st.isValid())
    {
        settings.style = st.getProperty ("style", settings.style);
        settings.keyTonic = st.getProperty ("keyTonic", -1);
        settings.mode = st.getProperty ("mode", "").toString();
        settings.bars = st.getProperty ("bars", 8);
        settings.seed = (juce::uint32) (juce::int64) st.getProperty ("seed", 1);
        settings.progression = st.getProperty ("progression", "").toString();
        settings.quantize = st.getProperty ("quantize", 16);
        for (int i = 0; i < mv::kNumParts; ++i) settings.instruments[(size_t) i] = st.getProperty ("inst" + juce::String (i), "").toString();
    }
    auto an = root.getChildWithName ("Analysis");
    if (an.isValid())
    {
        const juce::ScopedLock lock (modelLock);
        analysis = analysisFromTree (an);
        analysisLabel = an.getProperty ("label").toString();
    }
    auto vt = root.getChildWithName ("Vocal");
    if (vt.isValid())
    {
        mv::VocalReport r;
        std::vector<mv::ProgressionOption> opts;
        vocalFromTree (vt, r, opts);
        const juce::ScopedLock lock (modelLock);
        vocalReport = r;
        vocalOptions = opts;
        vocalLabel = vt.getProperty ("label").toString();
        vocalOption = vt.getProperty ("option", -1);
        vocalIdea = vt.getProperty ("idea", 0);
    }
    auto sc = root.getChildWithName ("Score");
    if (sc.isValid()) commitScore (scoreFromTree (sc), sc.getProperty ("source").toString());
    ++version;
}

juce::AudioProcessorEditor* MelodyverseProcessor::createEditor() { return new MelodyverseEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MelodyverseProcessor(); }
