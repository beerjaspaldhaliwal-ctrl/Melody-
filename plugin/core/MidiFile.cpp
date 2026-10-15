#include "MidiFile.h"

#include "Synth.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <tuple>
#include <utility>

namespace mv
{
namespace
{
constexpr int kBendRange = 12;

void writeVarLen (std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = (uint8_t) (v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = (uint8_t) ((v & 0x7F) | 0x80);
    while (n--) out.push_back (buf[n]);
}

void write32 (std::vector<uint8_t>& out, uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back ((uint8_t) (v >> s)); }
void write16 (std::vector<uint8_t>& out, uint16_t v) { out.push_back ((uint8_t) (v >> 8)); out.push_back ((uint8_t) v); }

struct Ev { uint32_t tick; int order; std::vector<uint8_t> bytes; };

std::vector<uint8_t> chunk (std::vector<Ev> evs)
{
    std::stable_sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return std::tie (a.tick, a.order) < std::tie (b.tick, b.order); });
    std::vector<uint8_t> data;
    uint32_t last = 0;
    for (auto& e : evs)
    {
        writeVarLen (data, e.tick - last);
        last = e.tick;
        data.insert (data.end(), e.bytes.begin(), e.bytes.end());
    }
    writeVarLen (data, 0);
    data.insert (data.end(), { 0xFF, 0x2F, 0x00 });
    std::vector<uint8_t> out { 'M', 'T', 'r', 'k' };
    write32 (out, (uint32_t) data.size());
    out.insert (out.end(), data.begin(), data.end());
    return out;
}

std::vector<uint8_t> meta (uint8_t type, const std::string& text)
{
    std::vector<uint8_t> b { 0xFF, type };
    writeVarLen (b, (uint32_t) text.size());
    b.insert (b.end(), text.begin(), text.end());
    return b;
}
} // namespace

std::vector<uint8_t> writeMidi (const Score& score, const std::vector<std::string>& only, int tpb)
{
    auto tick = [&] (double beats) { return (uint32_t) std::max (0L, std::lround (beats * tpb)); };
    std::vector<std::vector<uint8_t>> chunks;

    std::vector<Ev> metaEv;
    metaEv.push_back ({ 0, 0, meta (0x03, "Melodyverse - " + (score.style.empty() ? std::string ("score") : score.style)) });
    uint32_t usPerBeat = (uint32_t) std::lround (60000000.0 / std::max (1.0, score.bpm));
    metaEv.push_back ({ 0, 0, { 0xFF, 0x51, 0x03, (uint8_t) (usPerBeat >> 16), (uint8_t) (usPerBeat >> 8), (uint8_t) usPerBeat } });
    metaEv.push_back ({ 0, 0, { 0xFF, 0x58, 0x04, 4, 2, 24, 8 } });
    static const int8_t sharpsMajor[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    bool minor = score.key.isMinorish();
    int8_t sf = sharpsMajor[minor ? (score.key.tonic + 3) % 12 : score.key.tonic];
    metaEv.push_back ({ 0, 0, { 0xFF, 0x59, 0x02, (uint8_t) sf, (uint8_t) (minor ? 1 : 0) } });
    for (auto& c : score.chords)
        if (c.chord) metaEv.push_back ({ tick (c.start), 1, meta (0x06, c.chord->name (score.key.prefersFlats())) });
    chunks.push_back (chunk (metaEv));

    int channel = 0;
    for (auto& t : score.tracks)
    {
        if (! only.empty() && std::find (only.begin(), only.end(), t.name) == only.end()) continue;
        std::vector<Ev> ev;
        ev.push_back ({ 0, 0, meta (0x03, t.name) });
        if (t.isDrums)
        {
            for (auto& h : t.hits)
            {
                uint8_t note = (uint8_t) gmDrumNote (h.name);
                ev.push_back ({ tick (h.beat), 2, { 0x99, note, (uint8_t) std::clamp (h.velocity, 1, 127) } });
                ev.push_back ({ tick (h.beat) + (uint32_t) tpb / 8, 0, { 0x89, note, 0 } });
            }
            chunks.push_back (chunk (ev));
            continue;
        }
        uint8_t ch = (uint8_t) channel;
        channel = channel + 1 == 9 ? 10 : channel + 1;
        ev.push_back ({ 0, 1, { (uint8_t) (0xC0 | ch), (uint8_t) gmProgram (t.instrument) } });
        bool bends = std::any_of (t.notes.begin(), t.notes.end(), [] (auto& n) { return n.bendFrom != 0; });
        if (bends)
            for (auto [cc, v] : std::initializer_list<std::pair<int, int>> { { 101, 0 }, { 100, 0 }, { 6, kBendRange }, { 38, 0 } })
                ev.push_back ({ 0, 1, { (uint8_t) (0xB0 | ch), (uint8_t) cc, (uint8_t) v } });
        for (auto& n : t.notes)
        {
            if (n.pitch < 0 || n.pitch > 127) continue;
            uint32_t on = tick (n.start), off = std::max (on + 1, tick (n.end()));
            if (bends)
            {
                uint32_t glide = std::max (1u, std::min (off - on, tick (std::min (0.5, n.duration * 0.4))));
                const int steps = n.bendFrom != 0 ? 8 : 0;
                for (int k = 0; k <= steps; ++k)
                {
                    double frac = 1.0 - (double) k / std::max (1, steps);
                    int val = 8192 + (int) std::lround (n.bendFrom / kBendRange * 8191 * frac);
                    val = std::clamp (val, 0, 16383);
                    ev.push_back ({ on + glide * (uint32_t) k / (uint32_t) std::max (1, steps), 1,
                                    { (uint8_t) (0xE0 | ch), (uint8_t) (val & 0x7F), (uint8_t) (val >> 7) } });
                }
            }
            ev.push_back ({ on, 2, { (uint8_t) (0x90 | ch), (uint8_t) n.pitch, (uint8_t) std::clamp (n.velocity, 1, 127) } });
            ev.push_back ({ off, 0, { (uint8_t) (0x80 | ch), (uint8_t) n.pitch, 0 } });
        }
        chunks.push_back (chunk (ev));
    }

    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    write32 (out, 6);
    write16 (out, 1);
    write16 (out, (uint16_t) chunks.size());
    write16 (out, (uint16_t) tpb);
    for (auto& c : chunks) out.insert (out.end(), c.begin(), c.end());
    return out;
}

} // namespace mv
