// Melodyverse core tests — no framework, just `core_tests` returning non-zero on failure.
#include "../core/Analysis.h"
#include "../core/Generator.h"
#include "../core/MidiFile.h"
#include "../core/Styles.h"
#include "../core/Synth.h"
#include "../core/Vocal.h"
#include "../core/MelodyLab.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test (const char* name, const std::function<void()>& fn)
{
    int before = failures;
    try { fn(); }
    catch (const std::exception& e) { ++failures; std::printf ("  EXCEPTION %s\n", e.what()); }
    std::printf ("%s %s\n", failures == before ? "[ok]  " : "[FAIL]", name);
}

using namespace mv;

static std::vector<std::string> names (const std::vector<Chord>& cs, bool flats = false)
{
    std::vector<std::string> out;
    for (auto& c : cs) out.push_back (c.name (flats));
    return out;
}

int main()
{
    test ("theory: keys, chords, roman numerals", [] {
        CHECK (Key::parse ("Am").tonic == 9 && Key::parse ("Am").mode == "minor");
        CHECK (Key::parse ("E phrygian_dominant").mode == "phrygian_dominant");
        CHECK (Key::parse ("D phrygian dominant").tonic == 2);
        CHECK (Key::parse ("C khamaj").mode == "khamaj");
        CHECK (Key::parse ("D minor").prefersFlats());
        CHECK ((names (parseProgression ("i bVI bIII bVII V7", Key::parse ("A minor"))) == std::vector<std::string> { "Am", "F", "C", "G", "E7" }));
        CHECK ((names (parseProgression ("i bVI bVII", Key::parse ("D minor")), true) == std::vector<std::string> { "Dm", "Bb", "C" }));
        CHECK (parseRoman ("ii7", Key::parse ("C")) == (Chord { 2, "m7", -1 }));
        CHECK (parseRoman ("V/3", Key::parse ("C")) == (Chord { 7, "maj", 11 }));
        CHECK (parseRoman ("viio", Key::parse ("C")) == (Chord { 11, "dim", -1 }));
        for (auto n : { "Am7", "F#m", "Bbmaj7", "Dsus4", "C/G", "Bdim", "E7" })
            CHECK (Chord::parse (n).name (std::string (n).find ('b') != std::string::npos) == n);
        CHECK (romanName (Chord::parse ("G"), Key::parse ("A minor")) == "bVII");
        auto c = Chord::parse ("C").voicing (60);
        auto f = Chord::parse ("F").voicing (60, &c);
        int move = 0;
        for (size_t i = 0; i < 3; ++i) move += std::abs (c[i] - f[i]);
        CHECK (move <= 4);
    });

    test ("styles: library loads and every progression parses", [] {
        CHECK (styles().size() >= 30);
        for (auto& st : styles())
        {
            auto key = Key::parse (st.defaultKey);
            for (auto& p : st.progressions) CHECK (! parseProgression (p, key).empty());
            CHECK (st.chordPattern.size() % 16 == 0 && st.bassPattern.size() % 16 == 0);
        }
        CHECK (getStyle ("punjabi").name == "punjabi_bhangra");
        CHECK (getStyle ("Spanish").name == "spanish_flamenco");
        CHECK (getStyle ("hollywood").name == "hollywood_epic");
        CHECK (famousProgressions().size() >= 20);
    });

    test ("generator: every style writes in-key music", [] {
        for (auto& st : styles())
        {
            GenerateOptions o;
            o.style = st.name;
            o.seed = 42;
            auto s = generate (o);
            auto* mel = s.track ("Melody");
            CHECK (mel && mel->notes.size() >= 8);
            if (! mel) continue;
            auto pcs = s.key.pitchClasses();
            for (auto& n : mel->notes)
            {
                CHECK (n.duration > 0);
                if (n.bendFrom == 0 && n.duration >= 0.5 && ! st.hasOrnament ("blue_note")) CHECK (pcs.count (n.pitch % 12));
            }
            CHECK (s.track ("Chords") && ! s.track ("Chords")->notes.empty());
            CHECK (s.lengthBeats == 32);
            CHECK (s.contentEnd() <= 33);
        }
    });

    test ("generator: deterministic seeds, custom progression, cadence", [] {
        GenerateOptions o;
        o.style = "bollywood";
        o.seed = 3;
        auto a = generate (o), b = generate (o);
        CHECK (a.track ("Melody")->notes.size() == b.track ("Melody")->notes.size());
        for (size_t i = 0; i < a.track ("Melody")->notes.size(); ++i)
            CHECK (a.track ("Melody")->notes[i].pitch == b.track ("Melody")->notes[i].pitch);
        GenerateOptions p;
        p.style = "english_pop";
        p.key = Key::parse ("E minor");
        p.progression = "i bVI bIII bVII";
        p.bars = 4;
        auto s = generate (p);
        std::vector<std::string> got;
        for (auto& c : s.chords) got.push_back (c.chord->name());
        CHECK ((got == std::vector<std::string> { "Em", "C", "G", "D" }));
        GenerateOptions q;
        q.style = "pop_ballad";
        q.key = Key::parse ("C");
        q.progression = "IV V vi I";
        q.seed = 5;
        auto t = generate (q);
        auto& notes = t.track ("Melody")->notes;
        auto last = *std::max_element (notes.begin(), notes.end(), [] (auto& x, auto& y) { return x.start < y.start; });
        CHECK (last.pitch % 12 == 0);
    });

    test ("midi: writes a valid type-1 file", [] {
        GenerateOptions o;
        o.style = "punjabi_sad";
        o.bars = 4;
        auto s = generate (o);
        auto bytes = writeMidi (s);
        CHECK (bytes.size() > 100);
        CHECK (bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd');
        int ntracks = (bytes[10] << 8) | bytes[11];
        CHECK (ntracks == 1 + (int) s.tracks.size());
        auto only = writeMidi (s, { "Melody" });
        CHECK (((only[10] << 8) | only[11]) == 2);
        // count note-ons in the melody-only file
        int noteOns = 0;
        for (size_t i = 0; i + 2 < only.size(); ++i)
            if ((only[i] & 0xF0) == 0x90 && only[i + 2] > 0 && only[i + 1] < 128 && (i == 0 || only[i - 1] < 0x80)) ++noteOns;
        CHECK (noteOns >= (int) s.track ("Melody")->notes.size());
    });

    test ("synth: every instrument and drum renders audio", [] {
        SynthEngine e;
        e.prepare (44100);
        for (auto& inst : instrumentNames())
        {
            e.reset();
            e.noteOn (0, patchIndex (inst), 64, 100, -2.0, 0.1);
            std::vector<float> l (22050, 0.0f), r (22050, 0.0f);
            e.render (l.data(), r.data(), 22050);
            float peak = 0;
            bool finite = true;
            for (float v : l) { peak = std::max (peak, std::abs (v)); finite &= std::isfinite (v); }
            CHECK (finite);
            if (peak <= 0.01f) std::printf ("    silent instrument: %s\n", inst.c_str());
            CHECK (peak > 0.01f && peak <= 1.0f);
        }
        for (auto& d : drumNames())
        {
            e.reset();
            e.drumHit (drumIndex (d), 120);
            std::vector<float> l (4410, 0.0f), r (4410, 0.0f);
            e.render (l.data(), r.data(), 4410);
            float peak = 0;
            for (float v : l) peak = std::max (peak, std::abs (v));
            CHECK (peak > 0.01f);
        }
    });

    test ("player: sample-accurate, loops, sends MIDI", [] {
        Score s;
        s.bpm = 120;
        s.lengthBeats = 4;
        s.tracks.push_back ({ "Melody", "piano", { { 0, 1, 60, 100 }, { 2, 1, 64, 100 } }, false, {} });
        Player p;
        p.prepare (48000);
        p.setScore (s);
        std::vector<std::tuple<int, int, int>> got;  // (absolute sample, status, note)
        long base = 0;
        MidiOut out { [&] (int off, uint8_t st, uint8_t d1, uint8_t) { got.push_back ({ (int) (base + off), st, d1 }); } };
        std::vector<float> l (512), r (512);
        const double spb = 48000 * 60.0 / 120;  // samples per beat
        for (int b = 0; b < 400; ++b)  // ~4.3 s => one full loop and a bit
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            p.process ((double) base / spb, 120, true, 512, l.data(), r.data(), &out);
            base += 512;
        }
        std::vector<std::pair<int, int>> ons;
        for (auto& [t, st, n] : got) if ((st & 0xF0) == 0x90) ons.push_back ({ t, n });
        CHECK (ons.size() >= 3);
        if (ons.size() >= 3)
        {
            CHECK (ons[0].first == 0 && ons[0].second == 60);
            CHECK (std::abs (ons[1].first - (int) (2 * spb)) <= 1 && ons[1].second == 64);
            CHECK (std::abs (ons[2].first - (int) (4 * spb)) <= 1 && ons[2].second == 60);  // looped
        }
        int offs = 0;
        for (auto& [t, st, n] : got) offs += (st & 0xF0) == 0x80;
        CHECK (offs >= 2);
    });

    test ("analysis: listens to a rendered song (tempo, key, chords, melody)", [] {
        GenerateOptions o;
        o.style = "english_pop";
        o.key = Key::parse ("A minor");
        o.bpm = 100;
        o.bars = 8;
        o.seed = 5;
        o.progression = "i bVI bIII bVII";
        auto song = generate (o);
        song.track ("Melody")->instrument = "flute";
        std::vector<float> l, r;
        renderScore (song, 44100, l, r, 1.0);
        std::vector<float> mono (l.size());
        for (size_t i = 0; i < l.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
        auto a = analyze (mono.data(), mono.size(), 44100);
        std::printf ("    bpm %.1f  key %s (%.2f)  chords %s  melody %zu notes\n", a.bpm, a.key.name().c_str(), a.keyConfidence,
                     a.chordChart().c_str(), a.melody.size());
        CHECK (std::abs (a.bpm - 100) < 3);
        CHECK (a.key.name() == "A minor");
        std::vector<int> roots;
        for (auto& c : a.chords)
            if (c.chord && c.end - c.start > 1.0 && (roots.empty() || roots.back() != c.chord->root)) roots.push_back (c.chord->root);
        CHECK (roots.size() >= 4 && roots[0] == 9 && roots[1] == 5 && roots[2] == 0 && roots[3] == 7);
        auto& truth = song.track ("Melody")->notes;
        int hit = 0;
        for (auto& n : truth)
        {
            double t = song.bpm > 0 ? n.start * 60.0 / song.bpm : 0;
            for (auto& g : a.melody)
                if (g.start - 0.05 <= t + 0.05 && t + 0.05 <= g.end && g.pitch == n.pitch) { ++hit; break; }
        }
        double rate = hit / (double) truth.size();
        std::printf ("    melody pitch-hit rate %.2f\n", rate);
        CHECK (rate > 0.7);
        bool epic = false;
        for (auto& f : a.famous()) epic |= f.name == "Epic minor loop";
        CHECK (epic);
        auto sc = analysisToScore (a, 16);
        CHECK (sc.track ("Melody") && ! sc.track ("Melody")->notes.empty());
        auto tl = harmonyTimeline (a);
        GenerateOptions m;
        m.style = "punjabi";
        m.key = a.key;
        m.bpm = a.bpm;
        m.timeline = tl;
        m.chords = m.bass = m.drums = false;
        auto matched = generate (m);
        CHECK (matched.track ("Melody") && matched.track ("Melody")->notes.size() > 10);
    });

    test ("vocals: deep analysis + chord suggestions + arrangement", [] {
        // a synthetic Punjabi-sad "singer": bansuri voice with vibrato and meend glides, Sa = A, Kafi thaat
        GenerateOptions o;
        o.style = "punjabi_sad";
        o.key = Key::parse ("A kafi");
        o.bpm = 80;
        o.bars = 8;
        o.seed = 11;
        o.progression = "i iv bVII i";
        o.chords = o.bass = o.drums = false;
        auto song = generate (o);
        auto* mel = song.track ("Melody");
        for (auto& n : mel->notes)  // breathe at the end of every two-bar phrase
            if (std::fmod (n.end() + 1e-6, 8.0) < 0.01 && n.duration > 1.0) n.duration -= 0.75;
        mel->instrument = "bansuri";
        int bends = 0;
        for (auto& n : mel->notes) bends += n.bendFrom != 0;
        std::vector<float> l, r;
        renderScore (song, 44100, l, r, 1.0, 1, 0.0f);  // a dry vocal
        std::vector<float> mono (l.size());
        for (size_t i = 0; i < l.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
        auto R = analyzeVocal (mono.data(), mono.size(), 44100);
        for (auto& line : R.insights) std::printf ("    %s\n", line.c_str());
        if (std::getenv ("MV_DEBUG"))
        {
            for (size_t i = 0; i < mel->notes.size() && i < 40; ++i)
                std::printf ("      truth %.2f %s %.2fb%s\n", mel->notes[i].start * 0.75, midiToName (mel->notes[i].pitch).c_str(), mel->notes[i].duration, mel->notes[i].bendFrom ? " bend" : "");
            for (size_t i = 0; i < R.notes.size() && i < 50; ++i)
                std::printf ("      heard %.2f-%.2f %s\n", R.notes[i].start, R.notes[i].end, midiToName (R.notes[i].pitch).c_str());
        }
        CHECK (R.key.tonic == 9);
        CHECK (R.thaat == "kafi" || R.thaat == "asavari");
        CHECK (std::abs (R.bpm - 80) < 4 || std::abs (R.bpm - 160) < 8);
        CHECK (! R.notes.empty());
        int hit = 0;
        for (auto& n : mel->notes)
        {
            if (n.duration < 0.5) continue;
            double t = (n.start + n.duration * 0.6) * 60.0 / song.bpm;  // past any glide into the note
            bool ok = false;
            for (auto& g : R.notes) if (g.start <= t && t <= g.end && g.pitch == n.pitch) { ok = true; break; }
            hit += ok;
            if (! ok && std::getenv ("MV_DEBUG")) std::printf ("      MISS %.2fs %s\n", t, midiToName (n.pitch).c_str());
        }
        int longNotes = 0;
        for (auto& n : mel->notes) longNotes += n.duration >= 0.5;
        std::printf ("    held-note pitch accuracy %d/%d, %d meend sung, %d detected\n", hit, longNotes, bends, R.count ("meend"));
        CHECK (hit >= 0.8 * longNotes);
        CHECK (R.vibratoShare > 0.2);
        if (bends > 0) CHECK (R.count ("meend") > 0);
        CHECK (R.lowest >= 55 && R.highest <= 84);
        auto opts = suggestProgressions (R, 5);
        CHECK (opts.size() == 5);
        for (auto& op : opts) std::printf ("    %3.0f%%  %-30s %s  [%s]\n", op.fit * 100, op.name.c_str(), op.roman.c_str(), op.style.c_str());
        CHECK (! opts.empty() && opts[0].fit > 0.6);
        for (size_t i = 1; i < opts.size(); ++i) CHECK (opts[i - 1].fit >= opts[i].fit);
        auto arr = arrangeForVocal (R, opts[0], VocalMelodyIdea::Answers, 3);
        CHECK (arr.track ("Vocal") && arr.track ("Vocal")->notes.size() == R.notes.size());
        CHECK (arr.track ("Chords") && arr.track ("Bass") && arr.track ("Melody"));
        // answer phrases must not sit on top of the singing
        int overlaps = 0;
        for (auto& a : arr.track ("Melody")->notes)
            for (auto& v : arr.track ("Vocal")->notes) overlaps += a.start < v.end() && a.end() > v.start;
        CHECK (overlaps == 0);
        auto harm = arrangeForVocal (R, opts[0], VocalMelodyIdea::HarmonyAbove, 3);
        CHECK (harm.track ("Melody")->notes.size() == harm.track ("Vocal")->notes.size());
        auto pcs = R.key.pitchClasses();
        for (size_t i = 0; i < harm.track ("Melody")->notes.size(); ++i)
        {
            int iv = harm.track ("Melody")->notes[i].pitch - harm.track ("Vocal")->notes[i].pitch;
            CHECK (iv >= 3 && iv <= 5);
        }
    });

    test ("melody lab: chords from MIDI, key, 8 developed variations", [] {
        // block chords as a DAW would export them, with inversions: Am  F/A  C/G  G  (2 bars each)
        std::vector<Note> block;
        auto addChord = [&] (double at, double len, std::vector<int> ps) { for (int p : ps) block.push_back ({ at, len, p, 90 }); };
        for (int rep = 0; rep < 2; ++rep)
        {
            double o = rep * 16.0;
            addChord (o + 0, 4, { 57, 60, 64 });      // Am
            addChord (o + 4, 4, { 57, 60, 65 });      // F/A
            addChord (o + 8, 4, { 55, 60, 64 });      // C/G
            addChord (o + 12, 4, { 55, 59, 62 });     // G
        }
        auto tl = chordsFromNotes (block);
        std::vector<std::string> names;
        for (auto& c : tl) names.push_back (c.chord->name());
        std::string joined;
        for (auto& n : names) joined += n + " ";
        std::printf ("    block chords -> %s\n", joined.c_str());
        CHECK ((names == std::vector<std::string> { "Am", "F/A", "C/G", "G", "Am", "F/A", "C/G", "G" }));
        // the same progression played as 8th-note arpeggios
        std::vector<Note> arp;
        const std::vector<std::vector<int>> shapes { { 45, 52, 57, 60, 64, 60, 57, 52 }, { 41, 48, 53, 57, 60, 57, 53, 48 },
                                                     { 48, 55, 60, 64, 67, 64, 60, 55 }, { 43, 50, 55, 59, 62, 59, 55, 50 } };
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 8; ++k) arp.push_back ({ c * 4.0 + k * 0.5, 0.5, shapes[(size_t) c][(size_t) k], 90 });
        auto tl2 = chordsFromNotes (arp);
        std::string j2;
        for (auto& c : tl2) j2 += c.chord->name() + " ";
        std::printf ("    arpeggios    -> %s\n", j2.c_str());
        CHECK (tl2.size() == 4 && tl2[0].chord->name() == "Am" && tl2[1].chord->name() == "F" && tl2[2].chord->name() == "C" && tl2[3].chord->name() == "G");
        auto key = keyFromChords (tl);
        std::printf ("    key -> %s\n", key.name().c_str());
        CHECK (key.name() == "A minor");

        MelodyLabOptions o;
        o.seed = 7;
        auto vars = melodyVariations (tl, key, o);
        CHECK (vars.size() == 8);
        std::set<int> chars;
        for (auto& v : vars)
        {
            chars.insert (v.character);
            std::printf ("    %-30s score %3.0f  %s\n", v.name.c_str(), v.metrics.score, v.tags.c_str());
            CHECK (! v.notes.empty());
            CHECK (v.metrics.chordToneStrong >= 0.7);
            CHECK (v.metrics.rangeSemitones <= 24);
            for (auto& n : v.notes) { CHECK (n.start >= 0 && n.end() <= 32 + 1e-6); CHECK (n.pitch >= 45 && n.pitch <= 96); }
            // phrases come home: Sa when the last chord holds it, otherwise a tone of that chord
            CHECK (tl.back().chord->contains (v.notes.back().pitch));
        }
        CHECK (chars.size() == 8);
        // variations must really differ from one another
        auto sameShare = [] (const std::vector<Note>& a, const std::vector<Note>& b) {
            int same = 0;
            for (auto& x : a) for (auto& y : b) if (std::abs (x.start - y.start) < 1e-6 && x.pitch == y.pitch) { ++same; break; }
            return (double) same / std::max<size_t> (1, a.size());
        };
        double worst = 0;
        for (size_t i = 0; i < vars.size(); ++i)
            for (size_t k = i + 1; k < vars.size(); ++k) worst = std::max (worst, sameShare (vars[i].notes, vars[k].notes));
        std::printf ("    most similar pair shares %.0f%% of notes\n", worst * 100);
        CHECK (worst < 0.5);
        // "more like this": 8 takes of one character, all different, deterministic
        MelodyLabOptions f = o;
        f.focusCharacter = 5;
        auto takes = melodyVariations (tl, key, f);
        CHECK (takes.size() == 8);
        for (auto& t : takes) CHECK (t.character == 5);
        double worstTake = 0;
        for (size_t i = 0; i < takes.size(); ++i)
            for (size_t k = i + 1; k < takes.size(); ++k) worstTake = std::max (worstTake, sameShare (takes[i].notes, takes[k].notes));
        std::printf ("    most similar Punjabi takes share %.0f%% of notes\n", worstTake * 100);
        CHECK (worstTake < 0.7);
        auto again = melodyVariations (tl, key, o);
        CHECK (again[3].notes.size() == vars[3].notes.size() && again[3].notes.front().pitch == vars[3].notes.front().pitch);
        // the Punjabi character actually ornaments
        bool ornamented = false;
        for (auto& t : takes) for (auto& n : t.notes) ornamented |= n.bendFrom != 0 || n.duration < 0.1;
        CHECK (ornamented);
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
