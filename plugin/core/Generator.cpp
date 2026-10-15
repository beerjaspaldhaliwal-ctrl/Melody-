#include "Generator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>

namespace mv
{
namespace
{
constexpr double kStep = 0.25;  // a 16th note
constexpr double kBar = 4.0;
constexpr double kEps = 1e-9;

bool isOnset (char c) { return c == 'x' || c == 'X' || c == '1' || c == '3' || c == '5' || c == '8' || c == 'c'; }

int gridBars (const std::string& grid) { return std::max (1, (int) grid.size() / 16); }

std::vector<GridEvent> iterGrid (const std::string& grid, double total)
{
    std::vector<GridEvent> out;
    auto events = parseGrid (grid);
    double span = gridBars (grid) * kBar;
    for (double offset = 0; offset < total - kEps; offset += span)
        for (auto& e : events)
        {
            double s = offset + e.start;
            if (s < total - kEps)
                out.push_back ({ s, std::min (e.length, total - s), e.accent, e.symbol });
        }
    return out;
}

struct Segment { const ChordEvent* chord; GridEvent event; };

// For every chord, the grid events that sound during it (re-articulated at chord changes).
std::vector<Segment> segments (const std::vector<ChordEvent>& timeline, const std::string& grid, double total)
{
    std::vector<Segment> out;
    auto events = iterGrid (grid, total);
    for (auto& ev : timeline)
    {
        if (! ev.chord) continue;
        double cEnd = ev.start + ev.duration;
        for (auto& g : events)
        {
            double gEnd = g.start + g.length;
            if (gEnd <= ev.start + kEps || g.start >= cEnd - kEps) continue;
            double s = std::max (g.start, ev.start);
            out.push_back ({ &ev, { s, std::min (gEnd, cEnd) - s, g.accent, g.symbol } });
        }
    }
    return out;
}

std::vector<ChordEvent> chordTimeline (const Style& st, const Key& key, int bars, const std::string& progression, Rng& rng)
{
    std::string text = progression.empty() ? rng.choice (st.progressions) : progression;
    auto chords = parseProgression (text, key);
    std::vector<ChordEvent> tl;
    double total = bars * kBar;
    double t = 0;
    size_t i = 0;
    while (t < total - kEps && ! chords.empty())
    {
        double dur = std::min (st.beatsPerChord, total - t);
        tl.push_back ({ t, dur, chords[i % chords.size()] });
        t += dur;
        ++i;
    }
    return tl;
}

Track compChords (const std::vector<ChordEvent>& tl, const Style& st, double total, Rng& rng)
{
    Track track { "Chords", st.chordInstrument, {}, false, {} };
    std::vector<int> prev, voicing;
    const ChordEvent* current = nullptr;
    int arpIndex = 0;
    static const int shape[] = { 0, 1, 2, 3, 2, 1 };
    for (auto& [ev, g] : segments (tl, st.chordPattern, total))
    {
        if (ev != current)
        {
            current = ev;
            voicing = ev->chord->voicing (st.chordOctave, prev.empty() ? nullptr : &prev);
            prev = voicing;
            arpIndex = 0;
        }
        int vel = g.accent ? 84 : 70;
        if (st.chordStyle == "arp")
        {
            auto tones = voicing;
            tones.push_back (voicing[0] + 12);
            int pitch = tones[(size_t) (shape[arpIndex % 6] % (int) tones.size())];
            ++arpIndex;
            double ring = std::min (g.length * 2, ev->start + ev->duration - g.start);
            track.notes.push_back ({ g.start, ring, pitch, vel - 6 + rng.integer (-4, 4) });
        }
        else if (st.chordStyle == "strum")
        {
            bool down = g.accent || arpIndex % 2 == 0;
            auto order = voicing;
            if (! down) std::reverse (order.begin(), order.end());
            ++arpIndex;
            for (size_t k = 0; k < order.size(); ++k)
            {
                double off = (double) k * 0.03;
                track.notes.push_back ({ g.start + off, std::max (0.05, g.length - off), order[k], vel - (int) k * 2 });
            }
        }
        else
            for (int p : voicing)
                track.notes.push_back ({ g.start, g.length, p, vel + rng.integer (-3, 3) });
    }
    return track;
}

int bassRoot (const Chord& c)
{
    int pc = c.bass >= 0 ? c.bass : c.root;
    return 36 + ((pc - 36) % 12 + 12) % 12;
}

Track bassLine (const std::vector<ChordEvent>& tl, const Style& st, double total)
{
    Track track { "Bass", st.bassInstrument, {}, false, {} };
    for (auto& [ev, g] : segments (tl, st.bassPattern, total))
    {
        auto& chord = *ev->chord;
        int root = bassRoot (chord);
        auto& ivs = chord.intervals();
        int pitch = root;
        switch (g.symbol)
        {
            case '3': pitch = root + (ivs.size() > 2 ? ivs[1] : 7); break;
            case '5': pitch = root + (ivs.size() > 2 ? ivs[2] : ivs.back()); break;
            case '8': pitch = root + 12; break;
            case 'c':
            {
                const ChordEvent* next = nullptr;
                for (auto& c : tl)
                    if (c.start > ev->start + kEps && c.chord) { next = &c; break; }
                if (next)
                {
                    int target = bassRoot (*next->chord);
                    pitch = target - 1 >= 33 ? target - 1 : target + 1;
                }
                break;
            }
            default: break;
        }
        track.notes.push_back ({ g.start, g.length, pitch, g.accent ? 100 : 88 });
    }
    return track;
}

Track drumTrack (const Style& st, double total, Rng& rng)
{
    Track track { "Drums", "drums", {}, true, {} };
    for (auto& [name, grid] : st.drums)
        for (auto& g : iterGrid (grid, total))
            track.hits.push_back ({ g.start, name, g.accent ? 110 : 82 + rng.integer (-8, 8) });
    std::sort (track.hits.begin(), track.hits.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    return track;
}

double contourAt (const std::string& shape, double t)
{
    constexpr double pi = 3.14159265358979323846;
    if (shape == "ascending") return 0.15 + 0.75 * t;
    if (shape == "descending") return 0.9 - 0.75 * t;
    if (shape == "wave") return 0.5 + 0.35 * std::sin (2 * pi * t);
    return 0.25 + 0.7 * std::sin (pi * t);
}

int weightedChoice (Rng& rng, const std::vector<double>& weights)
{
    double total = 0;
    for (double w : weights) total += w;
    if (total <= 0) return rng.integer (0, (int) weights.size() - 1);
    double r = rng.uniform() * total, acc = 0;
    for (size_t i = 0; i < weights.size(); ++i)
    {
        acc += weights[i];
        if (r <= acc) return (int) i;
    }
    return (int) weights.size() - 1;
}

int nearestIndex (const std::vector<int>& scale, double pitch, const std::set<int>* allowed = nullptr)
{
    int best = 0;
    double bestD = 1e9;
    for (size_t i = 0; i < scale.size(); ++i)
    {
        if (allowed && ! allowed->count (scale[i] % 12)) continue;
        double d = std::abs (scale[i] - pitch);
        if (d < bestD) { best = (int) i; bestD = d; }
    }
    return best;
}

Track writeMelody (const std::vector<ChordEvent>& tl, const Key& key, const Style& st, int bars, Rng& rng)
{
    const int low = st.melodyLow, high = st.melodyHigh;
    auto scale = key.scalePitches (low, high);
    Track track { "Melody", st.melodyInstrument, {}, false, {} };
    if (scale.empty()) return track;
    const std::string& form = st.phraseForm;
    const int phraseBars = std::min (4, std::max (1, bars));
    std::map<char, std::pair<std::vector<GridEvent>, std::vector<int>>> motifs;
    int prevIdx = nearestIndex (scale, (low + high) / 2.0);
    int lastInterval = 0, repeats = 0;
    std::string lastCell;
    const int last = (int) scale.size() - 1;

    auto chordPcs = [&] (double beat) {
        std::set<int> pcs;
        if (auto* ev = chordAt (tl, beat); ev && ev->chord)
            for (int p : ev->chord->pitchClasses()) pcs.insert (p);
        else
            pcs = key.pitchClasses();
        return pcs;
    };

    for (int bar = 0; bar < bars; ++bar)
    {
        const double barStart = bar * kBar;
        const char letter = form[(size_t) bar % form.size()];
        const int phrasePos = bar % phraseBars;
        const bool finalBar = bar == bars - 1;
        const bool phraseEnd = phrasePos == phraseBars - 1;
        if (bar % (int) form.size() == 0 && bar > 0)
            for (auto it = motifs.begin(); it != motifs.end();)
                it = it->first == 'A' ? std::next (it) : motifs.erase (it);

        if (motifs.count (letter) && ! finalBar)
        {
            auto& [cells, intervals] = motifs[letter];
            auto pcs = cells.empty() ? key.pitchClasses() : chordPcs (barStart + cells[0].start);
            int idx = nearestIndex (scale, scale[(size_t) prevIdx], &pcs);
            std::vector<int> newIdx;
            for (size_t k = 0; k < intervals.size(); ++k)
            {
                if (k) idx += intervals[k];
                idx = std::clamp (idx, 0, last);
                newIdx.push_back (idx);
            }
            if (rng.uniform() < 0.35 && newIdx.size() > 1)
            {
                static const std::vector<int> deltas { -2, -1, 1, 2 };
                newIdx.back() = std::clamp (newIdx.back() + rng.choice (deltas), 0, last);
            }
            for (size_t k = 0; k < cells.size() && k < newIdx.size(); ++k)
            {
                auto& g = cells[k];
                double beat = barStart + g.start;
                bool strong = std::fmod (g.start, 2.0) == 0.0;
                auto pcsHere = chordPcs (beat);
                int i = newIdx[k];
                if (strong && ! pcsHere.count (scale[(size_t) i] % 12))
                    i = nearestIndex (scale, scale[(size_t) i], &pcsHere);
                track.notes.push_back ({ beat, g.length, scale[(size_t) i], (strong ? 100 : 88) + rng.integer (-5, 5) });
                prevIdx = i;
            }
            continue;
        }

        std::vector<std::string> choices;
        for (auto& c : st.melodyRhythms)
            if (c != lastCell) choices.push_back (c);
        if (choices.empty()) choices = st.melodyRhythms;
        std::string cell = rng.choice (choices);
        lastCell = cell;
        auto cells = parseGrid (cell);
        if ((finalBar || phraseEnd) && ! cells.empty())
            cells.back().length = std::max (cells.back().length, kBar - cells.back().start);
        std::vector<int> intervals;
        for (size_t n = 0; n < cells.size(); ++n)
        {
            auto& g = cells[n];
            double beat = barStart + g.start;
            bool strong = std::fmod (g.start, 2.0) == 0.0;
            auto pcs = chordPcs (beat);
            double t = (phrasePos + g.start / kBar) / phraseBars;
            double target = low + (high - low) * contourAt (st.contour, t);
            bool isLast = n == cells.size() - 1;
            std::vector<double> weights (scale.size());
            for (size_t j = 0; j < scale.size(); ++j)
            {
                int d = (int) j - prevIdx, ad = std::abs (d);
                double w = st.stepWeight (ad);
                int p = scale[j];
                bool chordTone = pcs.count (p % 12) > 0;
                if (strong) w *= chordTone ? 4.0 : 0.25;
                else if (chordTone) w *= 1.5;
                w *= std::exp (-std::abs (p - target) / 5.0);
                if (std::abs (lastInterval) >= 3 && d != 0 && (d > 0) == (lastInterval > 0)) w *= 0.3;
                else if (std::abs (lastInterval) >= 3 && ad > 0 && ad <= 2) w *= 2.0;
                if (d == 0 && repeats >= 2) w *= 0.1;
                if (isLast && (finalBar || phraseEnd))
                {
                    if (finalBar) w *= (p % 12 == key.tonic && chordTone) ? 12.0 : chordTone ? 2.0 : 0.05;
                    else w *= chordTone ? 3.0 : 0.2;
                }
                weights[j] = w;
            }
            int idx = weightedChoice (rng, weights);
            if (finalBar && isLast)
            {
                int goal = key.tonic;
                if (! pcs.count (goal))
                    if (auto* ev = chordAt (tl, beat); ev && ev->chord) goal = ev->chord->root;
                std::set<int> goalSet { goal };
                bool any = std::any_of (scale.begin(), scale.end(), [&] (int p) { return p % 12 == goal; });
                if (any) idx = nearestIndex (scale, scale[(size_t) prevIdx], &goalSet);
            }
            int interval = idx - prevIdx;
            repeats = interval == 0 ? repeats + 1 : 0;
            intervals.push_back (n ? interval : 0);
            lastInterval = interval;
            prevIdx = idx;
            track.notes.push_back ({ beat, g.length, scale[(size_t) idx], (strong ? 100 : 86) + rng.integer (-5, 5) });
        }
        if (! motifs.count (letter))
            motifs[letter] = { cells, intervals };
    }
    return track;
}

int neighbour (const Key& key, int pitch, int dir)
{
    auto pcs = key.pitchClasses();
    int p = pitch + dir;
    for (int guard = 0; guard < 12 && ! pcs.count (((p % 12) + 12) % 12); ++guard) p += dir;
    return p;
}

std::vector<Note> applyOrnaments (const std::vector<Note>& notes, const Key& key, const Style& st, Rng& rng)
{
    if (st.ornaments.empty()) return notes;
    std::vector<Note> out;
    const Note* prev = nullptr;
    for (auto& n : notes)
    {
        std::string orn;
        if (rng.uniform() < st.ornamentRate) orn = rng.choice (st.ornaments);
        if (orn == "murki" && n.duration >= 0.75)
        {
            const double g = 0.0833;
            int ps[3] = { neighbour (key, n.pitch, 1), n.pitch, neighbour (key, n.pitch, -1) };
            for (int k = 0; k < 3; ++k) out.push_back ({ n.start + k * g, g, ps[k], n.velocity - 15 });
            out.push_back ({ n.start + 3 * g, n.duration - 3 * g, n.pitch, n.velocity });
        }
        else if (orn == "meend" && n.duration >= 0.75)
        {
            double bend = -2.0;
            if (prev && prev->pitch != n.pitch && std::abs (prev->end() - n.start) < 0.3)
                bend = std::clamp ((double) (prev->pitch - n.pitch), -3.0, 3.0);
            out.push_back ({ n.start, n.duration, n.pitch, n.velocity, bend });
        }
        else if (orn == "flamenco_turn" && n.duration >= 0.75)
        {
            const double g = 1.0 / 6.0;
            int ps[3] = { neighbour (key, n.pitch, 1), n.pitch, neighbour (key, n.pitch, -1) };
            for (int k = 0; k < 3; ++k) out.push_back ({ n.start + k * g, g, ps[k], n.velocity - 10 });
            out.push_back ({ n.start + 0.5, n.duration - 0.5, n.pitch, n.velocity });
        }
        else if (orn == "trill" && n.duration >= 1.5)
        {
            int up = neighbour (key, n.pitch, 1);
            double t = n.start, half = n.start + n.duration / 2;
            int k = 0;
            while (t < half - kEps)
            {
                out.push_back ({ t, 0.125, k % 2 == 0 ? n.pitch : up, n.velocity - 8 });
                t += 0.125;
                ++k;
            }
            out.push_back ({ t, n.end() - t, n.pitch, n.velocity });
        }
        else if (orn == "blue_note" && n.duration >= 0.5 && ((n.pitch - key.tonic) % 12 + 12) % 12 == 3)
            out.push_back ({ n.start, n.duration, n.pitch + 1, n.velocity, -1.0 });
        else
            out.push_back (n);
        prev = &n;
    }
    return out;
}

double swingShift (double beat, double swing)
{
    if (swing <= 0) return 0;
    double frac = std::round (std::fmod (beat, 1.0) / kStep) * kStep;
    if (std::abs (frac - 0.5) < 1e-6) return swing * 0.5;
    if (std::abs (frac - 0.25) < 1e-6 || std::abs (frac - 0.75) < 1e-6) return swing * 0.25;
    return 0;
}

void applySwing (std::vector<Note>& notes, double swing)
{
    for (auto& n : notes)
        if (double s = swingShift (n.start, swing); s > 0)
        {
            n.start += s;
            n.duration = std::max (0.05, n.duration - s);
        }
}
} // namespace

std::vector<GridEvent> parseGrid (const std::string& grid)
{
    std::vector<GridEvent> events;
    bool open = false;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        char ch = grid[i];
        if (isOnset (ch))
        {
            events.push_back ({ (double) i * kStep, kStep, ch == 'X', ch });
            open = true;
        }
        else if (ch == '-' && open)
            events.back().length += kStep;
        else
            open = false;
    }
    return events;
}

const ChordEvent* chordAt (const std::vector<ChordEvent>& timeline, double beat)
{
    for (auto& ev : timeline)
        if (ev.start - kEps <= beat && beat < ev.start + ev.duration - kEps) return &ev;
    return nullptr;
}

Score generate (const GenerateOptions& o)
{
    const Style& st = getStyle (o.style);
    Rng rng (o.seed);
    Key key = o.key ? *o.key : Key::parse (st.defaultKey);
    double bpm = o.bpm > 0 ? o.bpm : (double) rng.integer (st.tempoMin, st.tempoMax);
    int bars = std::max (1, o.bars);
    std::vector<ChordEvent> tl = o.timeline;
    if (tl.empty())
        tl = chordTimeline (st, key, bars, o.progression, rng);
    else
    {
        double end = 0;
        for (auto& e : tl) end = std::max (end, e.start + e.duration);
        bars = std::max (1, (int) std::ceil (end / kBar - 1e-6));
    }
    double total = bars * kBar;
    Score score;
    score.bpm = bpm;
    score.key = key;
    score.chords = tl;
    score.style = st.name;
    score.lengthBeats = total;
    if (o.melody)
    {
        auto mel = writeMelody (tl, key, st, bars, rng);
        mel.notes = applyOrnaments (mel.notes, key, st, rng);
        applySwing (mel.notes, st.swing);
        score.tracks.push_back (std::move (mel));
    }
    if (o.chords)
    {
        auto ch = compChords (tl, st, total, rng);
        applySwing (ch.notes, st.swing);
        score.tracks.push_back (std::move (ch));
    }
    if (o.bass)
    {
        auto bs = bassLine (tl, st, total);
        applySwing (bs.notes, st.swing);
        score.tracks.push_back (std::move (bs));
    }
    if (o.drums && ! st.drums.empty())
    {
        auto dr = drumTrack (st, total, rng);
        for (auto& h : dr.hits) h.beat += swingShift (h.beat, st.swing);
        score.tracks.push_back (std::move (dr));
    }
    return score;
}

} // namespace mv
