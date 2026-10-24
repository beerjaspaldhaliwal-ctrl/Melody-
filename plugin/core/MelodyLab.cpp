#include "MelodyLab.h"

#include "Generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace mv
{
namespace
{
constexpr double kBar = 4.0;
constexpr double kPi = 3.14159265358979323846;

int mod12 (int x) { return ((x % 12) + 12) % 12; }

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

// Rhythm cells: one beat on a 16th grid.  x onset, - hold, . rest
const char* const kCells[] = { "x---", "x-x-", "x-xx", "xx-x", "xxxx", "x--x", ".x-x", "..x-", "----", "....", "x-..", ".xxx" };
constexpr int kNumCells = 12;

struct Character
{
    const char* id;
    const char* name;
    const char* description;
    const char* instrument;
    const char* form;                  // bar letters within a 4-bar phrase
    std::array<double, kNumCells> cells;
    double repeatNote, stepBias, arpBias, chromaticApproach, anticipation, ornament, motifVariation;
    int lowOffset, highOffset;
    double climaxPos, leapy, restAtPhraseEnd;
    bool callResponse;
};

//                                       x---  x-x-  x-xx  xx-x  xxxx  x--x  .x-x  ..x-  ----  ....  x-..  .xxx
const Character kCharacters[] = {
    { "hook", "Catchy hook", "A short, sticky motif repeated and sequenced through the chords - made to get stuck in your head.", "lead", "AABA",
      { 1.0, 3.0, 1.2, 0.8, 0.3, 1.2, 1.0, 1.0, 0.6, 0.2, 1.4, 0.2 }, 0.35, 0.6, 0.25, 0.05, 0.25, 0.0, 0.25, -2, -3, 0.6, 0.2, 0.3, false },
    { "lyrical", "Lyrical / singable", "Long, stepwise lines that breathe like a vocal, landing on guide tones at every chord.", "flute", "ABAC",
      { 3.0, 2.5, 0.4, 0.2, 0.0, 1.6, 0.3, 0.4, 2.2, 0.3, 0.2, 0.1 }, 0.1, 0.85, 0.1, 0.05, 0.1, 0.0, 0.35, 0, 0, 0.65, 0.15, 0.6, false },
    { "syncopated", "Syncopated pop", "Off-beat entries and anticipations that push across the bar line - modern pop and Punjabi pop bounce.", "pluck", "AABB",
      { 0.4, 1.5, 1.0, 1.2, 0.4, 1.5, 2.5, 2.2, 0.8, 0.3, 1.0, 0.6 }, 0.3, 0.6, 0.3, 0.1, 0.65, 0.0, 0.3, -2, 0, 0.55, 0.25, 0.4, false },
    { "arpeggio", "Arpeggio rise", "Chord-tone arpeggios sequenced through the changes, climbing to a peak.", "celesta", "AA'BA",
      { 0.8, 2.6, 1.4, 1.0, 0.9, 0.8, 0.4, 0.4, 0.5, 0.1, 0.3, 0.3 }, 0.05, 0.2, 0.9, 0.05, 0.1, 0.0, 0.2, -3, 5, 0.75, 0.8, 0.2, false },
    { "callresponse", "Call & response", "A busy question answered by a calmer, inverted reply - two voices talking.", "brass", "ABAB",
      { 1.2, 2.2, 1.2, 1.0, 0.6, 1.2, 0.8, 0.6, 0.8, 0.8, 0.8, 0.4 }, 0.2, 0.6, 0.35, 0.05, 0.15, 0.0, 0.2, 0, 0, 0.6, 0.35, 0.5, true },
    { "punjabi", "Punjabi folk (murki & meend)", "Repeated-note chants with murki turns and meend glides, tumbi-style 16ths - straight from the boliyan.", "tumbi", "AABA",
      { 0.8, 2.2, 2.0, 1.6, 1.4, 1.0, 0.8, 0.5, 0.8, 0.2, 0.6, 0.6 }, 0.4, 0.75, 0.15, 0.0, 0.2, 0.35, 0.25, 0, 2, 0.6, 0.15, 0.3, false },
    { "soulful", "Soulful R&B", "Laid-back phrasing with chromatic approach notes and colour tones (9ths, 7ths).", "epiano", "ABAC",
      { 0.8, 1.5, 0.8, 1.2, 0.5, 1.4, 1.6, 1.4, 1.0, 0.6, 0.6, 0.8 }, 0.15, 0.7, 0.2, 0.45, 0.35, 0.0, 0.35, -2, 0, 0.6, 0.3, 0.4, false },
    { "cinematic", "Cinematic / epic", "Long notes and heroic leaps of fifths and octaves, slowly building to one big climax.", "strings", "ABAC",
      { 3.0, 1.2, 0.3, 0.2, 0.0, 1.4, 0.2, 0.3, 3.0, 0.3, 0.1, 0.4 }, 0.05, 0.55, 0.35, 0.0, 0.05, 0.0, 0.25, -2, 7, 0.8, 0.9, 0.5, false },
};
constexpr int kNumCharacters = (int) (sizeof (kCharacters) / sizeof (Character));

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::optional<Chord> chordOf (const std::vector<ChordEvent>& tl, double beat)
{
    if (auto* ev = chordAt (tl, beat)) return ev->chord;
    // past the end or in a gap: the nearest earlier chord
    std::optional<Chord> last;
    for (auto& ev : tl) if (ev.start <= beat && ev.chord) last = ev.chord;
    return last;
}

bool chordTone (const std::optional<Chord>& c, int pitch) { return c && c->contains (pitch); }

// role of a pitch in a chord: 0 root, 1 third, 2 fifth, 3 seventh/colour, -1 none
int roleIn (const Chord& c, int pitch)
{
    auto& iv = c.intervals();
    for (size_t k = 0; k < iv.size(); ++k)
        if (mod12 (c.root + iv[k]) == mod12 (pitch)) return (int) std::min<size_t> (k, 3);
    return -1;
}

int weighted (Rng& rng, const std::vector<double>& w)
{
    double total = 0;
    for (double x : w) total += std::max (0.0, x);
    if (total <= 0) return 0;
    double r = rng.uniform() * total;
    for (size_t i = 0; i < w.size(); ++i)
    {
        r -= std::max (0.0, w[i]);
        if (r <= 0) return (int) i;
    }
    return (int) w.size() - 1;
}

struct Context
{
    const std::vector<ChordEvent>& tl;
    const Key& key;
    const Character& ch;
    std::vector<int> scale;  // allowed scale pitches
    int lo = 60, hi = 79;
    double total = 32;
    int bars = 8;

    int scaleIndex (int pitch) const
    {
        int best = 0, d = 1000;
        for (size_t i = 0; i < scale.size(); ++i)
            if (std::abs (scale[i] - pitch) < d) { d = std::abs (scale[i] - pitch); best = (int) i; }
        return best;
    }
    int stepFrom (int pitch, int steps) const
    {
        int i = std::clamp (scaleIndex (pitch) + steps, 0, (int) scale.size() - 1);
        return scale[(size_t) i];
    }
    double contour (double beat) const
    {
        // global arc rising to one climax, with a gentle arch inside every 4-bar phrase
        const double t = beat / std::max (1.0, total), c = ch.climaxPos;
        double g = t < c ? 0.35 + 0.65 * (t / c) : 1.0 - 0.7 * ((t - c) / std::max (1e-6, 1 - c));
        double phrase = std::fmod (beat, 16.0) / 16.0;
        g += 0.12 * std::sin (kPi * phrase) - 0.06;
        return lo + (hi - lo) * std::clamp (g, 0.0, 1.0);
    }
};

// rhythm: a bar of 4 beat-cells, as a 16-step grid string
std::string makeRhythm (Rng& rng, const Character& ch, bool phraseEnd, bool firstBar)
{
    std::string grid;
    std::vector<double> w (ch.cells.begin(), ch.cells.end());
    for (int beat = 0; beat < 4; ++beat)
    {
        auto wb = w;
        if (beat == 0 && firstBar) { wb[8] = 0; wb[9] = 0; }       // the melody has to start
        if (beat == 0 && ! firstBar) wb[9] *= 0.3;
        if (phraseEnd && beat >= 2)
        {
            // land the phrase: a long note, maybe a breath on the last beat
            if (beat == 2) grid += rng.uniform() < 0.6 ? "x---" : kCells[weighted (rng, wb)];
            else grid += rng.uniform() < ch.restAtPhraseEnd ? "...." : "----";
            continue;
        }
        grid += kCells[weighted (rng, wb)];
    }
    if (grid.find ('x') == std::string::npos) grid[0] = 'x';
    return grid;
}

// a lighter answer rhythm for call & response: every other onset becomes a hold
std::string echoRhythm (const std::string& grid)
{
    std::string out = grid;
    int n = 0;
    for (auto& c : out)
        if (c == 'x' && (n++ % 2 == 1)) c = '-';
    return out;
}

struct Motif
{
    std::string rhythm;
    std::vector<int> steps;  // scale-step offsets from the first note
    int role = 0;            // chord role of the first note
};

// choose a strong-beat target: chord tone, voice-led from `prev`, pulled toward the contour
int chooseTarget (Rng& rng, const Context& cx, const std::optional<Chord>& chord, int prev, double beat, bool guideToneFirst)
{
    std::vector<int> cand;
    std::vector<double> w;
    const double target = cx.contour (beat);
    for (int p = cx.lo; p <= cx.hi; ++p)
    {
        if (chord ? ! chord->contains (p) : ! cx.key.pitchClasses().count (mod12 (p))) continue;
        double cost = 0.45 * std::abs (p - prev) + 0.35 * std::abs (p - target);
        if (cx.ch.leapy > 0.5 && std::abs (p - prev) >= 5 && std::abs (p - prev) <= 12) cost -= 2.0 * cx.ch.leapy;  // heroic leaps
        if (p == prev) cost += 1.2;
        if (chord)
        {
            int role = roleIn (*chord, p);
            if (guideToneFirst && (role == 1 || role == 3)) cost -= 1.2;  // 3rds and 7ths define the chord
            if (role == 0) cost -= 0.3;
        }
        cand.push_back (p);
        w.push_back (std::exp (-cost / 1.3));
    }
    if (cand.empty()) return prev;
    return cand[(size_t) weighted (rng, w)];
}

std::vector<Note> composeOnce (const Context& cx, Rng& rng)
{
    const Character& ch = cx.ch;
    const std::string form = ch.form;
    std::vector<std::string> letters;  // "A", "A'", "B" ...
    for (size_t i = 0; i < form.size(); ++i)
    {
        if (form[i] == '\'') { letters.back() += "'"; continue; }
        letters.push_back (std::string (1, form[i]));
    }
    std::map<std::string, Motif> motifs;
    std::vector<Note> out;
    int prev = (int) std::lround (cx.contour (0));
    prev = cx.stepFrom (prev, 0);
    const int phraseBars = std::min (4, cx.bars);

    for (int bar = 0; bar < cx.bars; ++bar)
    {
        const double b0 = bar * kBar;
        const int phrase = bar / phraseBars;
        const std::string letter = letters[(size_t) (bar % phraseBars) % letters.size()];
        const std::string base (1, letter[0]);
        const bool phraseEnd = bar % phraseBars == phraseBars - 1 || bar == cx.bars - 1;
        const bool finalBar = bar == cx.bars - 1;
        const bool answerPhrase = phrase % 2 == 1 || finalBar;  // question / answer pairs of phrases
        if (bar % phraseBars == 0 && phrase > 0)  // a new phrase keeps its A material, re-invents the rest
            for (auto it = motifs.begin(); it != motifs.end();) it = it->first == "A" ? std::next (it) : motifs.erase (it);

        // ---- rhythm ----
        std::string rhythm;
        const bool reuse = motifs.count (base) > 0;
        if (reuse)
        {
            rhythm = motifs[base].rhythm;
            if (letter.size() > 1 || rng.uniform() < ch.motifVariation * 0.5)
            {
                // vary: change one beat-cell
                int beat = rng.integer (1, 3);
                std::vector<double> w (ch.cells.begin(), ch.cells.end());
                std::string cell = kCells[weighted (rng, w)];
                rhythm.replace ((size_t) beat * 4, 4, cell);
            }
        }
        else if (ch.callResponse && base == "B" && motifs.count ("A"))
            rhythm = echoRhythm (motifs["A"].rhythm);
        else
            rhythm = makeRhythm (rng, ch, false, bar == 0);
        if (phraseEnd)
        {
            // keep the first half of the bar's rhythm, then land
            std::string landing = makeRhythm (rng, ch, true, false);
            rhythm.replace (8, 8, landing.substr (8));
        }
        if (rhythm.find ('x') == std::string::npos) rhythm[0] = 'x';
        auto events = parseGrid (rhythm);

        // ---- pitches ----
        std::vector<int> pitches (events.size(), prev);
        const auto chordAtBar = chordOf (cx.tl, b0);
        bool usedMotif = false;
        if (reuse || (ch.callResponse && base == "B" && motifs.count ("A")))
        {
            const Motif& m = reuse ? motifs[base] : motifs["A"];
            const bool invert = ! reuse;  // call & response answer: mirror the question
            // sequence: start on the same chord role in the new chord, near where we are
            int start = prev;
            if (chordAtBar)
            {
                int bestD = 1000;
                for (int p = cx.lo; p <= cx.hi; ++p)
                    if (roleIn (*chordAtBar, p) == m.role && std::abs (p - prev) < bestD) { bestD = std::abs (p - prev); start = p; }
            }
            int si = cx.scaleIndex (start);
            for (size_t k = 0; k < events.size(); ++k)
            {
                int step = k < m.steps.size() ? m.steps[k] : (m.steps.empty() ? 0 : m.steps.back());
                if (invert) step = -step;
                pitches[k] = cx.scale[(size_t) std::clamp (si + step, 0, (int) cx.scale.size() - 1)];
            }
            usedMotif = true;
        }
        else
        {
            // new material: targets on strong positions, then fill between them
            std::vector<size_t> strong;
            for (size_t k = 0; k < events.size(); ++k)
            {
                double beatInBar = events[k].start;
                bool onStrongBeat = std::fmod (beatInBar, 2.0) < 1e-6;
                auto here = chordOf (cx.tl, b0 + beatInBar), before = k ? chordOf (cx.tl, b0 + events[k - 1].start) : chordAtBar;
                bool chordChange = k == 0 || (here && before && *here != *before);
                if (onStrongBeat || chordChange || k == 0) strong.push_back (k);
            }
            int last = prev;
            for (size_t s = 0; s < strong.size(); ++s)
            {
                size_t k = strong[s];
                pitches[k] = chooseTarget (rng, cx, chordOf (cx.tl, b0 + events[k].start), last, b0 + events[k].start, true);
                last = pitches[k];
            }
            // fill weak notes between targets
            for (size_t s = 0; s < strong.size(); ++s)
            {
                size_t a = strong[s], z = s + 1 < strong.size() ? strong[s + 1] : events.size();
                int from = pitches[a];
                int to = z < events.size() ? pitches[z] : (int) std::lround (cx.contour (b0 + kBar));
                int n = (int) (z - a - 1);
                for (int j = 1; j <= n; ++j)
                {
                    size_t k = a + (size_t) j;
                    auto chordHere = chordOf (cx.tl, b0 + events[k].start);
                    int p;
                    double r = rng.uniform();
                    if (r < ch.repeatNote) p = pitches[k - 1];
                    else if (r < ch.repeatNote + ch.arpBias * (1 - ch.repeatNote))
                    {
                        // arpeggiate: next chord tone in the direction of travel
                        int dir = to >= pitches[k - 1] ? 1 : -1;
                        p = pitches[k - 1] + dir;
                        while (p > cx.lo - 3 && p < cx.hi + 3 && ! chordTone (chordHere, p)) p += dir;
                        if (p < cx.lo - 2 || p > cx.hi + 2) p = pitches[k - 1];
                    }
                    else
                    {
                        // passing / neighbour tones stepping toward the next target
                        int dir = to > pitches[k - 1] ? 1 : to < pitches[k - 1] ? -1 : (rng.uniform() < 0.5 ? 1 : -1);
                        int remaining = cx.scaleIndex (to) - cx.scaleIndex (pitches[k - 1]);
                        if (std::abs (remaining) <= 1 && j < n) dir = -dir;  // neighbour, then come back
                        p = cx.stepFrom (pitches[k - 1], dir);
                    }
                    // approach note into the next target
                    if (j == n && z < events.size() && rng.uniform() < ch.chromaticApproach)
                        p = to + (rng.uniform() < 0.7 ? -1 : 1);
                    pitches[k] = std::clamp (p, cx.lo - 2, cx.hi + 2);
                }
                (void) from;
            }
        }
        // strong beats must agree with the harmony (snap stray motif notes to the nearest chord tone)
        for (size_t k = 0; k < events.size(); ++k)
        {
            double beat = b0 + events[k].start;
            auto c = chordOf (cx.tl, beat);
            bool strongBeat = std::fmod (events[k].start, 2.0) < 1e-6 || events[k].length >= 1.0;
            if (strongBeat && c && ! c->contains (pitches[k]))
            {
                int best = pitches[k], d = 100;
                for (int p = pitches[k] - 3; p <= pitches[k] + 3; ++p) if (c->contains (p) && std::abs (p - pitches[k]) < d) { d = std::abs (p - pitches[k]); best = p; }
                pitches[k] = best;
            }
        }
        // cadence: questions end open (3rd or 5th), answers come home (tonic, else the chord root)
        if (phraseEnd && ! events.empty())
        {
            auto c = chordOf (cx.tl, b0 + events.back().start);
            int goalPc = -1;
            if (answerPhrase) goalPc = c && ! c->contains (cx.key.tonic) ? c->root : cx.key.tonic;
            else if (c)
            {
                auto pcs = c->pitchClasses();
                std::vector<int> open;
                for (int pc : pcs) if (pc != cx.key.tonic) open.push_back (pc);
                goalPc = open.empty() ? c->root : open[(size_t) rng.integer (0, (int) open.size() - 1)];
            }
            if (goalPc >= 0)
            {
                int ref = events.size() > 1 ? pitches[events.size() - 2] : prev, best = pitches.back(), d = 100;
                for (int p = cx.lo - 2; p <= cx.hi + 2; ++p) if (mod12 (p) == goalPc && std::abs (p - ref) < d) { d = std::abs (p - ref); best = p; }
                pitches.back() = best;
            }
        }
        // store motif for later reuse
        if (! usedMotif && ! motifs.count (base) && ! events.empty())
        {
            Motif m;
            m.rhythm = rhythm;
            int si = cx.scaleIndex (pitches[0]);
            for (int p : pitches) m.steps.push_back (cx.scaleIndex (p) - si);
            m.role = chordAtBar ? std::max (0, roleIn (*chordAtBar, pitches[0])) : 0;
            motifs[base] = m;
        }
        // emit
        for (size_t k = 0; k < events.size(); ++k)
        {
            double phrasePos = (std::fmod (b0, 16.0) + events[k].start) / 16.0;
            int vel = 82 + (std::fmod (events[k].start, 2.0) < 1e-6 ? 10 : 0) + (int) std::lround (8 * std::sin (kPi * phrasePos)) + rng.integer (-4, 4);
            out.push_back ({ b0 + events[k].start, events[k].length, pitches[k], std::clamp (vel, 40, 120) });
        }
        if (! pitches.empty()) prev = pitches.back();
    }

    // anticipations: a note on the last off-beat of a bar takes the next chord's tone and ties over
    for (size_t i = 0; i + 1 < out.size(); ++i)
    {
        auto& n = out[i];
        double inBar = std::fmod (n.start, kBar);
        double nextBar = std::floor (n.start / kBar) * kBar + kBar;
        if (inBar >= 3.5 - 1e-6 && n.end() >= nextBar - 1e-6 && nextBar < cx.total && rng.uniform() < ch.anticipation)
        {
            auto next = chordOf (cx.tl, nextBar);
            if (next && ! next->contains (n.pitch))
            {
                int best = n.pitch, d = 100;
                for (int p = n.pitch - 4; p <= n.pitch + 4; ++p) if (next->contains (p) && std::abs (p - n.pitch) < d) { d = std::abs (p - n.pitch); best = p; }
                n.pitch = best;
            }
            auto& m = out[i + 1];
            if (std::abs (m.start - nextBar) < 1e-6 && m.pitch == n.pitch)  // tie into the downbeat
            {
                n.duration = m.end() - n.start;
                out.erase (out.begin() + (long) i + 1);
            }
        }
    }
    // Punjabi ornaments: murki turns into long notes, meend glides between neighbours
    if (ch.ornament > 0)
    {
        std::vector<Note> orn;
        for (size_t i = 0; i < out.size(); ++i)
        {
            auto n = out[i];
            if (n.duration >= 1.0 && rng.uniform() < ch.ornament)
            {
                const double g = 0.0833;
                int up = cx.stepFrom (n.pitch, 1), down = cx.stepFrom (n.pitch, -1);
                for (int k = 0; k < 3; ++k) orn.push_back ({ n.start + k * g, g, k == 0 ? up : k == 1 ? n.pitch : down, n.velocity - 15 });
                orn.push_back ({ n.start + 3 * g, n.duration - 3 * g, n.pitch, n.velocity });
                continue;
            }
            if (i > 0 && n.duration >= 0.75 && std::abs (n.pitch - out[i - 1].pitch) <= 3 && n.pitch != out[i - 1].pitch && rng.uniform() < ch.ornament)
                n.bendFrom = out[i - 1].pitch - n.pitch;
            orn.push_back (n);
        }
        out = orn;
    }
    // swing-free soulful lay-back: nudge off-beats slightly late
    if (std::string (ch.id) == "soulful")
        for (auto& n : out)
            if (std::fmod (n.start * 2, 1.0) < 1e-6 && std::fmod (n.start, 1.0) > 0.4) { n.start += 0.04; n.duration = std::max (0.05, n.duration - 0.04); }
    // never past the end of the progression
    std::vector<Note> clipped;
    for (auto n : out)
    {
        if (n.start >= cx.total - 1e-6) continue;
        n.duration = std::min (n.duration, cx.total - n.start);
        clipped.push_back (n);
    }
    return clipped;
}

std::string describe (const MelodyMetrics& m, const Key& key)
{
    char b[200];
    std::snprintf (b, sizeof b, "%.0f%% chord tones on the beat  |  %.1f notes/bar  |  range %d semitones%s",
                   m.chordToneStrong * 100, m.notesPerBar, m.rangeSemitones, m.endsOnTonic ? "  |  ends on Sa" : "");
    (void) key;
    return b;
}

} // namespace

const std::vector<MelodyCharacterInfo>& melodyCharacters()
{
    static const std::vector<MelodyCharacterInfo> v = [] {
        std::vector<MelodyCharacterInfo> out;
        for (auto& c : kCharacters) out.push_back ({ c.id, c.name, c.description, c.instrument });
        return out;
    }();
    return v;
}

MelodyMetrics evaluateMelody (const std::vector<Note>& mel, const std::vector<ChordEvent>& tl, const Key& key)
{
    MelodyMetrics m;
    if (mel.empty()) return m;
    int strong = 0, strongHit = 0, steps = 0, intervals = 0, leaps = 0, recovered = 0;
    int lo = 127, hi = 0;
    std::vector<const Note*> mains;
    for (auto& n : mel) if (n.duration >= 0.2) mains.push_back (&n);  // ignore grace notes
    if (mains.empty()) for (auto& n : mel) mains.push_back (&n);
    for (size_t i = 0; i < mains.size(); ++i)
    {
        auto& n = *mains[i];
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
        if (std::fmod (n.start, 1.0) < 1e-3 || n.duration >= 1.0)
        {
            ++strong;
            auto c = chordOf (tl, n.start + 1e-4);
            strongHit += chordTone (c, n.pitch);
        }
        if (i)
        {
            int iv = n.pitch - mains[i - 1]->pitch;
            ++intervals;
            steps += std::abs (iv) <= 2;
            if (std::abs (iv) >= 5)
            {
                ++leaps;
                if (i + 1 < mains.size())
                {
                    int nx = mains[i + 1]->pitch - n.pitch;
                    recovered += (nx != 0 && (nx > 0) != (iv > 0) && std::abs (nx) <= 2) || nx == 0;
                }
            }
        }
    }
    double total = 0;
    for (auto& ev : tl) total = std::max (total, ev.start + ev.duration);
    const int bars = std::max (1, (int) std::ceil (total / kBar - 1e-6));
    // motif coherence: share of bars whose onset pattern also occurs in another bar
    std::vector<std::string> patterns ((size_t) bars, std::string (16, '.'));
    for (auto& n : mel)
    {
        int b = (int) (n.start / kBar), s = (int) std::lround (std::fmod (n.start, kBar) * 4);
        if (b >= 0 && b < bars && s >= 0 && s < 16) patterns[(size_t) b][(size_t) s] = 'x';
    }
    int coherent = 0;
    for (int i = 0; i < bars; ++i)
        for (int j = 0; j < bars; ++j)
            if (i != j && patterns[(size_t) i] == patterns[(size_t) j] && patterns[(size_t) i].find ('x') != std::string::npos) { ++coherent; break; }
    m.chordToneStrong = strong ? (double) strongHit / strong : 0;
    m.stepwise = intervals ? (double) steps / intervals : 0;
    m.leapRecovery = leaps ? (double) recovered / leaps : 1.0;
    m.motifCoherence = (double) coherent / bars;
    m.rangeSemitones = hi - lo;
    m.notesPerBar = (double) mains.size() / bars;
    m.endsOnTonic = mod12 (mel.back().pitch) == key.tonic;
    const double rangeOk = m.rangeSemitones >= 5 && m.rangeSemitones <= 19 ? 1.0 : m.rangeSemitones <= 24 ? 0.5 : 0.0;
    m.score = 100.0 * (0.32 * m.chordToneStrong + 0.18 * std::min (1.0, m.stepwise / 0.55) + 0.14 * m.leapRecovery
                       + 0.16 * std::min (1.0, m.motifCoherence / 0.5) + 0.1 * rangeOk + 0.1 * (m.endsOnTonic ? 1.0 : 0.0));
    return m;
}

std::vector<ChordEvent> chordsFromNotes (const std::vector<Note>& notes, double window)
{
    std::vector<ChordEvent> out;
    double end = 0;
    for (auto& n : notes) end = std::max (end, n.end());
    if (end <= 0 || window <= 0) return out;
    static const char* const vocab[] = { "maj", "min", "7", "maj7", "m7", "dim", "m7b5", "sus2", "sus4", "aug" };
    const int W = (int) std::ceil (end / window - 1e-6);

    // best chord for the span [a, b) of windows, with its fitness per beat
    struct Fit { std::optional<Chord> chord; double perBeat = 0; bool empty = true; };
    // per-window pitch-class weights, computed once
    std::vector<std::array<double, 12>> win ((size_t) W);
    std::vector<double> winTotal ((size_t) W, 0.0);
    for (int w = 0; w < W; ++w)
        for (auto& n : notes)
        {
            double ov = std::min ((w + 1) * window, n.end()) - std::max (w * window, n.start);
            if (ov <= 1e-6) continue;
            win[(size_t) w][(size_t) mod12 (n.pitch)] += ov;
            winTotal[(size_t) w] += ov;
        }
    auto fitSpan = [&] (int a, int b) {
        Fit f;
        const double ws = a * window, we = b * window;
        std::array<double, 12> pcw {};
        double total = 0;
        int bass = 1000;
        double bassAt = 1e9;
        for (int w = a; w < b; ++w) for (int pc = 0; pc < 12; ++pc) pcw[(size_t) pc] += win[(size_t) w][(size_t) pc];
        for (double t : pcw) total += t;
        if (total <= 0) return f;
        for (auto& n : notes)
        {
            double ov = std::min (we, n.end()) - std::max (ws, n.start);
            if (ov <= 1e-6) continue;
            // the bass is the lowest note sounding as the span starts (else the lowest overall)
            double at = std::max (ws, n.start) - ws;
            if (at < bassAt - 0.26 || (std::abs (at - bassAt) <= 0.26 && n.pitch < bass)) { bass = n.pitch; bassAt = std::min (bassAt, at); }
        }
        f.empty = false;
        const int bassPc = mod12 (bass);
        double best = -1e9;
        for (int root = 0; root < 12; ++root)
            for (auto* q : vocab)
            {
                Chord c { root, q, -1 };
                auto pcs = c.pitchClasses();
                // the chord has to fit every window of the span, not just the total (so two
                // neighbouring triads can't hide inside one seventh chord)
                double fitSum = 0, coverSum = 0;
                int used = 0;
                for (int w = a; w < b; ++w)
                {
                    if (winTotal[(size_t) w] <= 0) continue;
                    double in = 0, outside = 0;
                    int covered = 0;
                    for (int pc = 0; pc < 12; ++pc) (c.contains (pc) ? in : outside) += win[(size_t) w][(size_t) pc];
                    for (int pc : pcs) covered += win[(size_t) w][(size_t) pc] > 0;
                    fitSum += (in - 1.2 * outside) / winTotal[(size_t) w];
                    coverSum += covered / (double) pcs.size();
                    ++used;
                }
                int spanCovered = 0;
                for (int pc : pcs) spanCovered += pcw[(size_t) pc] > 0;
                double cover = 0.5 * spanCovered / (double) pcs.size() + 0.5 * coverSum / std::max (1, used);
                double sc = fitSum / std::max (1, used) + 0.45 * cover + (root == bassPc ? 0.15 : 0.0) - 0.02 * (double) pcs.size();
                if (sc > best)
                {
                    best = sc;
                    c.bass = (bassPc != root && c.contains (bassPc)) ? bassPc : -1;
                    f.chord = c;
                }
            }
        f.perBeat = best;
        return f;
    };

    // segment the timeline: every split costs a little, so arpeggios merge into one chord
    // while real chord changes (which add "outside" notes) still split
    const int maxSpan = std::max (1, (int) std::lround (8.0 / window));
    const double splitCost = 0.35;
    std::vector<double> best ((size_t) W + 1, -1e18);
    std::vector<int> from ((size_t) W + 1, 0);
    best[0] = 0;
    for (int b = 1; b <= W; ++b)
        for (int a = std::max (0, b - maxSpan); a < b; ++a)
        {
            auto f = fitSpan (a, b);
            double gain = f.empty ? 0.0 : f.perBeat * (b - a) * window - splitCost;
            if (best[(size_t) a] + gain > best[(size_t) b]) { best[(size_t) b] = best[(size_t) a] + gain; from[(size_t) b] = a; }
        }
    std::vector<std::pair<int, int>> spans;
    for (int b = W; b > 0; b = from[(size_t) b]) spans.push_back ({ from[(size_t) b], b });
    std::reverse (spans.begin(), spans.end());
    for (auto [a, b] : spans)
    {
        auto f = fitSpan (a, b);
        if (f.empty) continue;
        if (! out.empty() && out.back().chord && *out.back().chord == *f.chord && std::abs (out.back().start + out.back().duration - a * window) < 1e-6)
            out.back().duration += (b - a) * window;
        else
            out.push_back ({ a * window, (b - a) * window, f.chord });
    }
    return out;
}

std::vector<MelodyVariation> melodyVariations (const std::vector<ChordEvent>& tl, const Key& key, const MelodyLabOptions& o)
{
    std::vector<MelodyVariation> out;
    if (tl.empty()) return out;
    double total = 0;
    for (auto& ev : tl) total = std::max (total, ev.start + ev.duration);
    const int bars = std::max (1, (int) std::ceil (total / kBar - 1e-6));
    for (int v = 0; v < o.count; ++v)
    {
        const int charIdx = o.focusCharacter >= 0 ? std::clamp (o.focusCharacter, 0, kNumCharacters - 1) : v % kNumCharacters;
        const Character& ch = kCharacters[charIdx];
        Context cx { tl, key, ch, {}, 60, 79, bars * kBar, bars };
        const int centre = 67 + 7 * std::clamp (o.registerShift, -1, 1);
        cx.lo = centre - 7 + ch.lowOffset;
        cx.hi = centre + 9 + ch.highOffset;
        cx.scale = key.scalePitches (cx.lo - 3, cx.hi + 3);
        MelodyVariation best;
        best.metrics.score = -1;
        for (int c = 0; c < std::max (1, o.candidates); ++c)
        {
            const uint32_t seed = o.seed * 7919u + (uint32_t) v * 104729u + (uint32_t) c * 31u + (uint32_t) charIdx;
            Rng rng (seed);
            auto notes = composeOnce (cx, rng);
            auto met = evaluateMelody (notes, tl, key);
            if (met.score > best.metrics.score)
            {
                best.notes = notes;
                best.metrics = met;
                best.seed = seed;
            }
        }
        best.character = charIdx;
        best.name = o.focusCharacter >= 0 ? std::string (ch.name) + "  - take " + std::to_string (v + 1) : ch.name;
        best.description = ch.description;
        best.instrument = ch.instrument;
        best.tags = describe (best.metrics, key);
        out.push_back (best);
    }
    return out;
}

} // namespace mv
