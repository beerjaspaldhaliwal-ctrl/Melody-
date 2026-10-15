#include "Synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

namespace mv
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ---------------------------------------------------------------------------
// Patches
// ---------------------------------------------------------------------------

struct Patch
{
    std::string name;
    int gm = 0;
    int type = 0;  // 0 additive, 1 FM, 2 808
    std::array<float, 16> amps {};
    int numHarmonics = 1;
    float decayBase = 0, decaySlope = 0;  // per-harmonic exponential decay (1/s): base + slope*k (0 = sustained)
    float pitchDecay = 0;                 // extra decay per Hz of fundamental (higher piano notes die faster)
    float attack = 0.005f, decay = 0.1f, sustain = 1.0f, release = 0.2f;
    float vibrato = 0;      // semitones
    float tremolo = 0;
    int detune = 1;         // 1..3 oscillators
    float detuneAmt = 0.0035f;
    float drive = 0;        // tanh saturation amount
    float fmRatio = 1, fmIndex = 0, fmIndexDecay = 0, ampDecay = 0;
    float noise = 0;
    float lpBase = 0, lpEnv = 0;  // low-pass cutoff = f0 * (lpBase + lpEnv*env); 0 = off
};

std::vector<Patch> makePatches()
{
    std::vector<Patch> P;
    auto add = [&] (Patch p) { P.push_back (p); return &P.back(); };
    auto setAmps = [] (Patch& p, std::vector<float> a) { p.numHarmonics = (int) std::min<size_t> (16, a.size()); for (int i = 0; i < p.numHarmonics; ++i) p.amps[(size_t) i] = a[(size_t) i]; };
    auto series = [] (int n, auto fn) { std::vector<float> v; for (int k = 1; k <= n; ++k) v.push_back ((float) fn (k)); return v; };

    { Patch p; p.name = "piano"; p.gm = 0; setAmps (p, { 1, .55f, .3f, .22f, .12f, .08f, .05f, .03f }); p.decayBase = 1.2f; p.decaySlope = 0.9f; p.pitchDecay = 1.0f / 600.0f; p.attack = .003f; p.release = .25f; add (p); }
    { Patch p = P[0]; p.name = "upright"; p.gm = 32; p.drive = 1.5f; p.release = .15f; add (p); }
    { Patch p; p.name = "epiano"; p.gm = 4; p.type = 1; p.fmRatio = 1; p.fmIndex = 1.2f; p.fmIndexDecay = 5; p.ampDecay = 1.1f; p.attack = .004f; p.release = .25f; add (p); }
    { Patch p; p.name = "celesta"; p.gm = 8; p.type = 1; p.fmRatio = 4; p.fmIndex = 1.2f; p.fmIndexDecay = 3; p.ampDecay = 2.0f; p.attack = .002f; p.release = .5f; add (p); }
    { Patch p; p.name = "bell"; p.gm = 14; p.type = 1; p.fmRatio = 3.5f; p.fmIndex = 2.5f; p.fmIndexDecay = 3; p.ampDecay = 1.0f; p.attack = .002f; p.release = .6f; add (p); }
    { Patch p; p.name = "organ"; p.gm = 16; setAmps (p, { 1, .8f, .5f, .4f, 0, .25f, 0, .2f }); p.attack = .01f; p.decay = .05f; p.sustain = .9f; p.release = .08f; p.tremolo = .15f; add (p); }
    { Patch p; p.name = "harmonium"; p.gm = 20; setAmps (p, { 1, .7f, .55f, .45f, .35f, .28f, .2f, .15f, .1f, .08f }); p.detune = 2; p.detuneAmt = .004f; p.attack = .04f; p.sustain = .9f; p.release = .1f; add (p); }
    auto pluck = [&] (const char* name, int gm, float bright, float base, float drive) {
        Patch p; p.name = name; p.gm = gm;
        setAmps (p, series (14, [&] (int k) { return std::pow (bright, k - 1) / std::pow (k, 0.6); }));
        p.decayBase = base; p.decaySlope = 0.9f; p.attack = .002f; p.release = .25f; p.drive = drive;
        add (p);
    };
    pluck ("nylon", 24, .55f, 1.6f, 0); pluck ("guitar", 25, .75f, 1.6f, 0); pluck ("pluck", 25, .7f, 2.4f, 0);
    pluck ("tumbi", 104, .95f, 3.0f, 0); pluck ("sitar", 104, .9f, 1.6f, 2.5f); pluck ("oud", 105, .5f, 3.0f, 0);
    pluck ("harpsichord", 6, .95f, 2.5f, 0);
    auto bowed = [&] (const char* name, int gm, float rolloff, int n, float attack, float release, float vib) {
        Patch p; p.name = name; p.gm = gm;
        setAmps (p, series (n, [&] (int k) { return 1.0 / std::pow (k, rolloff); }));
        p.detune = 3; p.attack = attack; p.decay = .3f; p.sustain = .85f; p.release = release; p.vibrato = vib;
        add (p);
    };
    bowed ("strings", 48, 1.1f, 16, .12f, .35f, .12f); bowed ("cello", 42, 1.1f, 16, .12f, .35f, .12f);
    bowed ("pad", 89, 1.6f, 10, .45f, .8f, 0); bowed ("drone", 89, 1.6f, 10, .8f, .8f, 0); bowed ("choir", 52, 1.6f, 10, .3f, .6f, .1f);
    { Patch p; p.name = "brass"; p.gm = 61; setAmps (p, series (13, [] (int k) { return 1.0 / k; })); p.attack = .05f; p.decay = .2f; p.sustain = .85f; p.release = .15f; p.drive = 1.3f; p.vibrato = .06f; p.lpBase = 1.5f; p.lpEnv = 8; add (p); }
    { Patch p; p.name = "flute"; p.gm = 73; setAmps (p, { 1, .25f, .08f }); p.noise = .08f; p.attack = .06f; p.sustain = .9f; p.release = .12f; p.vibrato = .15f; add (p); }
    { Patch p = P.back(); p.name = "bansuri"; p.attack = .09f; p.release = .15f; p.vibrato = .25f; add (p); }
    { Patch p; p.name = "lead"; p.gm = 81; setAmps (p, series (15, [] (int k) { return k % 2 ? 1.0 / k : 0.15 / k; })); p.detune = 2; p.detuneAmt = .006f; p.attack = .006f; p.decay = .15f; p.sustain = .75f; p.release = .1f; p.vibrato = .08f; add (p); }
    { Patch p; p.name = "supersaw"; p.gm = 81; setAmps (p, series (15, [] (int k) { return 1.0 / k; })); p.detune = 3; p.detuneAmt = .01f; p.attack = .006f; p.decay = .15f; p.sustain = .75f; p.release = .25f; add (p); }
    { Patch p; p.name = "bass"; p.gm = 33; setAmps (p, { 1, .45f, .2f, .1f }); p.decayBase = .8f; p.decaySlope = .9f; p.attack = .004f; p.decay = .2f; p.sustain = .9f; p.release = .08f; add (p); }
    { Patch p; p.name = "sub808"; p.gm = 38; p.type = 2; setAmps (p, { 1 }); p.drive = 2.0f; p.attack = .002f; p.decay = .4f; p.sustain = .8f; p.release = .12f; add (p); }
    return P;
}

const std::vector<Patch>& patches()
{
    static const std::vector<Patch> p = makePatches();
    return p;
}

const char* const kDrums[] = { "kick", "snare", "clap", "rim", "hat", "openhat", "shaker", "tom", "taiko", "dhol_low", "dhol_high",
                               "tabla_dha", "tabla_na", "cajon_low", "cajon_high", "darbuka_dum", "darbuka_tek" };
const int kDrumGm[] = { 36, 38, 39, 37, 42, 46, 70, 45, 41, 64, 62, 61, 60, 64, 63, 64, 63 };
const float kDrumPan[] = { 0, 0, -.1f, -.2f, .3f, .3f, -.35f, 0, 0, 0, .2f, 0, .25f, 0, .2f, 0, .25f };
constexpr int kNumDrums = (int) (sizeof (kDrumGm) / sizeof (int));

// sine table
struct SineTable
{
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = (float) std::sin (kTwoPi * i / N); }
    float sin01 (double phase) const  // phase in cycles
    {
        double p = phase - std::floor (phase);
        double x = p * N;
        int i = (int) x;
        float f = (float) (x - i);
        return t[i] + (t[i + 1] - t[i]) * f;
    }
};
const SineTable& sine() { static const SineTable s; return s; }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad bandpass (double f, double q, double sr)
    {
        Biquad b; double w = kTwoPi * f / sr, alpha = std::sin (w) / (2 * q), a0 = 1 + alpha;
        b.b0 = alpha / a0; b.b1 = 0; b.b2 = -alpha / a0; b.a1 = -2 * std::cos (w) / a0; b.a2 = (1 - alpha) / a0; return b;
    }
    static Biquad highpass (double f, double sr)
    {
        Biquad b; double w = kTwoPi * f / sr, alpha = std::sin (w) / (2 * 0.707), c = std::cos (w), a0 = 1 + alpha;
        b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = (1 + c) / 2 / a0; b.a1 = -2 * c / a0; b.a2 = (1 - alpha) / a0; return b;
    }
    float process (float x)
    {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

std::vector<float> makeDrum (int d, double sr)
{
    const std::string name = kDrums[d];
    double dur = name == "taiko" ? 1.2 : name == "openhat" ? 0.4 : name == "tabla_dha" ? 0.7 : name == "tabla_na" ? 0.5 : name == "kick" ? 0.5 : 0.35;
    size_t n = (size_t) (dur * sr);
    std::vector<float> y (n, 0.0f);
    uint32_t seed = 1234567u + (uint32_t) d * 7919u;
    auto noise = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float) ((double) seed / 4294967295.0 * 2.0 - 1.0); };
    auto bandCentre = [] (double lo, double hi) { return std::sqrt (lo * hi); };
    auto bandQ = [] (double lo, double hi) { return std::sqrt (lo * hi) / (hi - lo); };
    double ph = 0, ph2 = 0;
    auto sweep = [&] (double t, double f0, double f1, double rate) { double f = f1 + (f0 - f1) * std::exp (-t * rate); ph += f / sr; return std::sin (kTwoPi * ph); };
    Biquad bp1, hp1;
    auto bp = [&] (double lo, double hi) { bp1 = Biquad::bandpass (bandCentre (lo, hi), bandQ (lo, hi), sr); };
    auto hp = [&] (double f) { hp1 = Biquad::highpass (f, sr); };
    if (name == "snare") bp (1500, 9000);
    else if (name == "clap") bp (900, 3500);
    else if (name == "hat" || name == "openhat") hp (7000);
    else if (name == "shaker") hp (5000);
    else if (name == "rim") bp (2000, 6000);
    else if (name == "taiko") bp (80, 600);
    else if (name == "dhol_low") bp (200, 1200);
    else if (name == "dhol_high") bp (1500, 7000);
    else if (name == "cajon_low") bp (300, 2000);
    else if (name == "cajon_high") bp (1800, 8000);
    else if (name == "darbuka_tek") bp (2500, 9000);
    else if (name == "tabla_na") bp (2000, 6000);
    else if (name == "kick") hp (3000);
    for (size_t i = 0; i < n; ++i)
    {
        double t = (double) i / sr;
        float nz = noise(), v = 0;
        if (name == "kick") v = (float) (sweep (t, 160, 45, 30) * std::exp (-t * 7) + 0.3 * hp1.process (nz) * std::exp (-t * 200));
        else if (name == "snare") v = (float) (0.5 * std::sin (kTwoPi * 185 * t) * std::exp (-t * 25) + 0.8 * bp1.process (nz) * std::exp (-t * 18));
        else if (name == "clap")
        {
            double env = 0.5 * std::exp (-t * 14);
            for (double dd : { 0.0, 0.011, 0.023 }) if (t >= dd) env += std::exp (-(t - dd) * 120);
            v = (float) (bp1.process (nz) * env);
        }
        else if (name == "hat" || name == "openhat") v = (float) (hp1.process (nz) * std::exp (-t * (name == "hat" ? 40 : 7)) * 0.6);
        else if (name == "shaker") v = (float) (hp1.process (nz) * std::min (1.0, t / 0.015) * std::exp (-t * 35) * 0.5);
        else if (name == "rim") v = (float) (std::sin (kTwoPi * 1700 * t) * std::exp (-t * 90) + 0.4 * bp1.process (nz) * std::exp (-t * 120));
        else if (name == "tom") v = (float) (sweep (t, 140, 95, 12) * std::exp (-t * 9));
        else if (name == "taiko") v = (float) (1.2 * sweep (t, 90, 48, 8) * std::exp (-t * 3.5) + 0.4 * bp1.process (nz) * std::exp (-t * 20));
        else if (name == "dhol_low") v = (float) (sweep (t, 110, 62, 18) * std::exp (-t * 8) + 0.3 * bp1.process (nz) * std::exp (-t * 40));
        else if (name == "dhol_high") v = (float) (0.7 * std::sin (kTwoPi * 380 * t) * std::exp (-t * 30) + 0.6 * bp1.process (nz) * std::exp (-t * 60));
        else if (name == "tabla_dha")
        {
            ph2 += (80 + 35 * (1 - std::exp (-t * 6))) / sr;
            double dayan = 0;
            for (auto [a, r] : std::initializer_list<std::pair<double, double>> { { 1.0, 1.0 }, { 0.5, 2.0 }, { 0.3, 3.0 } }) dayan += a * std::sin (kTwoPi * 560 * r * t);
            v = (float) (0.8 * std::sin (kTwoPi * ph2) * std::exp (-t * 5) + 0.5 * dayan * std::exp (-t * 6));
        }
        else if (name == "tabla_na")
        {
            double s = 0;
            for (auto [a, r] : std::initializer_list<std::pair<double, double>> { { 1.0, 1.0 }, { 0.6, 2.0 }, { 0.35, 3.0 }, { 0.2, 4.0 } }) s += a * std::sin (kTwoPi * 620 * r * t);
            v = (float) (s * std::exp (-t * 9) * 0.6 + 0.2 * bp1.process (nz) * std::exp (-t * 150));
        }
        else if (name == "cajon_low") v = (float) (sweep (t, 130, 80, 25) * std::exp (-t * 12) + 0.25 * bp1.process (nz) * std::exp (-t * 35));
        else if (name == "cajon_high") v = (float) (0.4 * std::sin (kTwoPi * 260 * t) * std::exp (-t * 30) + 0.7 * bp1.process (nz) * std::exp (-t * 28));
        else if (name == "darbuka_dum") v = (float) (sweep (t, 140, 100, 20) * std::exp (-t * 7));
        else if (name == "darbuka_tek") v = (float) (0.6 * std::sin (kTwoPi * 850 * t) * std::exp (-t * 60) + 0.5 * bp1.process (nz) * std::exp (-t * 80));
        y[i] = v;
    }
    float peak = 1e-9f;
    for (float v : y) peak = std::max (peak, std::abs (v));
    for (float& v : y) v /= peak;
    return y;
}

double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
} // namespace

const char* partName (Part p)
{
    static const char* const n[] = { "Melody", "Chords", "Bass", "Drums" };
    return n[(int) p];
}

int partFromTrackName (const std::string& name)
{
    for (int i = 0; i < kNumParts; ++i) if (name == partName ((Part) i)) return i;
    return -1;
}

std::vector<std::string> instrumentNames()
{
    std::vector<std::string> out;
    for (auto& p : patches()) out.push_back (p.name);
    return out;
}

int patchIndex (const std::string& instrument)
{
    auto& P = patches();
    for (size_t i = 0; i < P.size(); ++i) if (P[i].name == instrument) return (int) i;
    return 0;
}

std::vector<std::string> drumNames() { return { std::begin (kDrums), std::end (kDrums) }; }

int drumIndex (const std::string& name)
{
    for (int i = 0; i < kNumDrums; ++i) if (name == kDrums[i]) return i;
    return -1;
}

int gmProgram (const std::string& instrument)
{
    for (auto& p : patches()) if (p.name == instrument) return p.gm;
    return 0;
}

int gmDrumNote (const std::string& drum)
{
    int i = drumIndex (drum);
    return i >= 0 ? kDrumGm[i] : 38;
}

// ---------------------------------------------------------------------------
// SynthEngine
// ---------------------------------------------------------------------------

float SynthEngine::Comb::process (float x)
{
    float out = buf[i];
    store = out * (1 - damp) + store * damp;
    buf[i] = x + store * fb;
    if (++i >= buf.size()) i = 0;
    return out;
}

float SynthEngine::Allpass::process (float x)
{
    float b = buf[i];
    float out = -x + b;
    buf[i] = x + b * 0.5f;
    if (++i >= buf.size()) i = 0;
    return out;
}

void SynthEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    drumSamples.clear();
    for (int d = 0; d < kNumDrums; ++d) drumSamples.push_back (makeDrum (d, sr));
    const int combLen[] = { 1116, 1188, 1277, 1356 }, apLen[] = { 556, 441 };
    double scale = sr / 44100.0;
    for (int c = 0; c < 8; ++c)
    {
        combs[(size_t) c].buf.assign ((size_t) ((combLen[c % 4] + (c >= 4 ? 23 : 0)) * scale), 0.0f);
        combs[(size_t) c].fb = 0.84f;
        combs[(size_t) c].damp = 0.2f;
    }
    for (int a = 0; a < 4; ++a) allpasses[(size_t) a].buf.assign ((size_t) ((apLen[a % 2] + (a >= 2 ? 23 : 0)) * scale), 0.0f);
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices) v.active = false;
    for (auto& d : drumVoices) d.active = false;
    for (auto& c : combs) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.store = 0; }
    for (auto& a : allpasses) std::fill (a.buf.begin(), a.buf.end(), 0.0f);
}

void SynthEngine::noteOn (int part, int patch, int pitch, int velocity, double bendFrom, double glideSeconds)
{
    Voice* v = nullptr;
    for (auto& x : voices)
        if (x.active && x.part == part && x.pitch == pitch && ! x.releasing) { x.releasing = true; x.relLevel = envelope (x); x.relT = 0; }
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (! v)  // steal: oldest releasing voice, else oldest voice
    {
        Voice* oldestRel = nullptr; Voice* oldest = &voices[0];
        for (auto& x : voices)
        {
            if (x.releasing && (! oldestRel || x.age < oldestRel->age)) oldestRel = &x;
            if (x.age < oldest->age) oldest = &x;
        }
        v = oldestRel ? oldestRel : oldest;
    }
    *v = Voice {};
    v->active = true;
    v->part = part;
    v->patch = std::clamp (patch, 0, (int) patches().size() - 1);
    v->pitch = pitch;
    v->vel = (float) std::pow (std::clamp (velocity, 1, 127) / 127.0, 1.4);
    v->bend = bendFrom;
    v->glide = std::max (0.01, glideSeconds);
    v->noise = 22222u + (uint32_t) pitch * 101u;
    v->phase2 = 0.37; v->phase3 = 0.71;
    v->hgain.fill (1.0f);
    v->age = ++ageCounter;
}

void SynthEngine::noteOff (int part, int pitch)
{
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.part == part && v.pitch == pitch)
        {
            v.relLevel = envelope (v);
            v.releasing = true;
            v.relT = 0;
        }
}

void SynthEngine::allNotesOff (bool hard)
{
    for (auto& v : voices)
    {
        if (hard) v.active = false;
        else if (v.active && ! v.releasing) { v.relLevel = envelope (v); v.releasing = true; v.relT = 0; }
    }
    if (hard) for (auto& d : drumVoices) d.active = false;
}

void SynthEngine::drumHit (int drum, int velocity)
{
    if (drum < 0 || drum >= (int) drumSamples.size()) return;
    DrumVoice* slot = nullptr;
    for (auto& d : drumVoices) if (! d.active) { slot = &d; break; }
    if (! slot)
    {
        slot = &drumVoices[0];
        for (auto& d : drumVoices) if (d.pos > slot->pos) slot = &d;
    }
    *slot = { true, drum, 0, std::clamp (velocity, 1, 127) / 127.0f };
}

float SynthEngine::envelope (Voice& v) const
{
    auto& P = patches()[(size_t) v.patch];
    if (v.releasing)
        return v.relLevel * (float) std::max (0.0, 1.0 - v.relT / std::max (0.005f, P.release));
    if (v.t < P.attack) return (float) (v.t / P.attack);
    double d = (v.t - P.attack) / std::max (0.005f, P.decay);
    return d >= 1 ? P.sustain : (float) (1.0 - (1.0 - P.sustain) * d);
}

float SynthEngine::renderVoice (Voice& v)
{
    auto& P = patches()[(size_t) v.patch];
    auto& S = sine();
    const double dt = 1.0 / sr;
    double semis = v.pitch;
    if (v.bend != 0)
    {
        double g = std::min (1.0, v.t / v.glide);
        g = g * g * (3 - 2 * g);
        semis += v.bend * (1 - g);
    }
    if (P.vibrato > 0)
        semis += P.vibrato * std::clamp ((v.t - 0.25) / 0.4, 0.0, 1.0) * S.sin01 (5.5 * v.t);
    double f0 = midiToHz (semis);
    if (P.type == 2) f0 *= 1.0 + 1.5 * std::exp (-v.t * 40);
    const double nyq = 0.45 * sr;
    float env = envelope (v);
    float y = 0;

    if (P.type == 1)  // FM
    {
        double index = P.fmIndex * std::exp (-v.t * P.fmIndexDecay);
        float mod = (float) (index * S.sin01 (P.fmRatio * v.phase));
        y = S.sin01 (v.phase + mod / kTwoPi) * (float) std::exp (-v.t * P.ampDecay);
        v.phase += f0 * dt;
    }
    else
    {
        double phases[3] = { v.phase, v.phase2, v.phase3 };
        double ratios[3] = { 1.0, 1.0 - P.detuneAmt, 1.0 + P.detuneAmt };
        if (P.detune == 2) ratios[1] = 1.0 + P.detuneAmt;
        for (int o = 0; o < P.detune; ++o)
        {
            double f = f0 * ratios[o];
            float s1 = S.sin01 (phases[o]), c1 = S.sin01 (phases[o] + 0.25);
            float sPrev = 0, sCur = s1, acc = 0;
            for (int k = 1; k <= P.numHarmonics; ++k)
            {
                if (f * k >= nyq) break;
                acc += P.amps[(size_t) k - 1] * v.hgain[(size_t) k - 1] * sCur;
                float sNext = 2 * c1 * sCur - sPrev;
                sPrev = sCur;
                sCur = sNext;
            }
            y += (o == 0 ? 1.0f : (P.detune == 2 ? 0.67f : 1.0f)) * acc;
            phases[o] += f * dt;
        }
        y /= (float) P.detune;
        v.phase = phases[0] - std::floor (phases[0]);
        v.phase2 = phases[1] - std::floor (phases[1]);
        v.phase3 = phases[2] - std::floor (phases[2]);
        if (P.decayBase > 0)
            for (int k = 0; k < P.numHarmonics; ++k)
                v.hgain[(size_t) k] *= (float) (1.0 - (P.decayBase + P.decaySlope * k + P.pitchDecay * f0) * dt);
    }
    if (v.phase > 1e6) v.phase -= std::floor (v.phase);
    if (P.noise > 0)
    {
        v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
        float nz = (float) ((double) v.noise / 4294967295.0 * 2.0 - 1.0);
        y += P.noise * nz * 0.5f;
    }
    if (P.tremolo > 0) y *= 1.0f + P.tremolo * S.sin01 (6.0 * v.t);
    if (P.lpBase > 0)
    {
        double fc = std::min (nyq, f0 * (P.lpBase + P.lpEnv * env));
        float a = (float) (1.0 - std::exp (-kTwoPi * fc * dt));
        v.lp += a * (y - v.lp);
        y = v.lp * 1.6f;
    }
    if (P.drive > 0) y = std::tanh (P.drive * y) / std::tanh (P.drive);
    v.t += dt;
    if (v.releasing)
    {
        v.relT += dt;
        if (v.relT >= P.release) v.active = false;
    }
    else if ((P.decayBase > 0 && v.hgain[0] < 1e-3f) || (P.type == 1 && v.t * P.ampDecay > 9))
        v.active = false;
    return y * env * v.vel;
}

void SynthEngine::render (float* left, float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0, r = 0, send = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            float s = renderVoice (v) * partGain[(size_t) v.part] * 0.35f;
            float pan = v.part == 1 ? ((v.pitch % 7) / 3.0f - 1.0f) * 0.35f : 0.0f;
            float a = (pan + 1) * (float) kPi / 4;
            l += s * std::cos (a);
            r += s * std::sin (a);
            send += s * (v.part == 2 ? 0.3f : 1.0f);
        }
        for (auto& d : drumVoices)
        {
            if (! d.active) continue;
            auto& smp = drumSamples[(size_t) d.drum];
            float s = smp[d.pos] * d.gain * partGain[3] * 0.6f;
            float a = (kDrumPan[d.drum] + 1) * (float) kPi / 4;
            l += s * std::cos (a);
            r += s * std::sin (a);
            send += s * 0.3f;
            if (++d.pos >= smp.size()) d.active = false;
        }
        if (reverbMix > 0 && ! combs[0].buf.empty())
        {
            float in = send * 0.015f, wl = 0, wr = 0;
            for (int c = 0; c < 4; ++c) wl += combs[(size_t) c].process (in);
            for (int c = 4; c < 8; ++c) wr += combs[(size_t) c].process (in);
            wl = allpasses[1].process (allpasses[0].process (wl));
            wr = allpasses[3].process (allpasses[2].process (wr));
            l += reverbMix * wl * 3;
            r += reverbMix * wr * 3;
        }
        left[i] += std::tanh (l * masterGain * 1.2f) / std::tanh (1.2f);
        right[i] += std::tanh (r * masterGain * 1.2f) / std::tanh (1.2f);
    }
}

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------

void Player::prepare (double sampleRate)
{
    synth.prepare (sampleRate);
    lastPos_ = -1;
    wasPlaying_ = false;
    for (auto& s : sounding_) s.fill (0);
}

std::unique_ptr<Player::Sequence> Player::buildSequence (const Score& score)
{
    auto seq = std::make_unique<Sequence>();
    auto& ev = seq->events;
    const double L = score.lengthBeats > 0 ? score.lengthBeats : std::max (4.0, std::ceil (score.contentEnd() / 4.0) * 4.0);
    for (auto& t : score.tracks)
    {
        int part = partFromTrackName (t.name);
        if (part < 0) continue;
        if (t.isDrums)
        {
            for (auto& h : t.hits)
                if (h.beat >= 0 && h.beat < L) ev.push_back ({ h.beat, true, part, drumIndex (h.name), gmDrumNote (h.name), h.velocity, 0, 0.1 });
            continue;
        }
        int patch = patchIndex (t.instrument);
        for (auto& n : t.notes)
        {
            if (n.start < 0 || n.start >= L || n.pitch < 0 || n.pitch > 127) continue;
            double end = std::min (n.end(), L - 1e-6);
            ev.push_back ({ n.start, true, part, patch, n.pitch, n.velocity, n.bendFrom, end - n.start });
            ev.push_back ({ std::max (n.start + 1e-6, end - 1e-6), false, part, patch, n.pitch, 0, 0, 0 });
        }
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) {
        if (a.beat != b.beat) return a.beat < b.beat;
        return ! a.on && b.on;  // offs first
    });
    seq->length = L;
    return seq;
}

std::unique_ptr<Player::Sequence> Player::exchange (std::unique_ptr<Sequence> next)
{
    if (! next) return nullptr;
    std::swap (seq_, next);
    needsStop_ = true;
    lastPos_ = -1;
    return next;
}

void Player::fire (const Event& e, double bpm, MidiOut* midi, int offset)
{
    const auto part = (size_t) e.part;
    const uint8_t ch = (uint8_t) std::clamp (midiChannel[part], 0, 15);
    if (e.part == (int) Part::Drums)
    {
        if (! partEnabled[part]) return;
        synth.drumHit (e.patch, e.velocity);
        if (sendMidi && midi && midi->send)
        {
            midi->send (offset, (uint8_t) (0x90 | ch), (uint8_t) e.pitch, (uint8_t) std::clamp (e.velocity, 1, 127));
            midi->send (offset, (uint8_t) (0x80 | ch), (uint8_t) e.pitch, 0);
        }
        return;
    }
    if (e.on)
    {
        if (! partEnabled[part]) return;
        double glide = std::min (0.5, e.durationBeats * 0.4) * 60.0 / bpm;
        synth.noteOn (e.part, e.patch, e.pitch, e.velocity, e.bendFrom, glide);
        if (sendMidi && midi && midi->send)
        {
            if (sounding_[part][(size_t) e.pitch]) midi->send (offset, (uint8_t) (0x80 | ch), (uint8_t) e.pitch, 0);
            midi->send (offset, (uint8_t) (0x90 | ch), (uint8_t) e.pitch, (uint8_t) std::clamp (e.velocity, 1, 127));
            sounding_[part][(size_t) e.pitch] = 1;
        }
    }
    else
    {
        synth.noteOff (e.part, e.pitch);
        if (sounding_[part][(size_t) e.pitch] && midi && midi->send)
            midi->send (offset, (uint8_t) (0x80 | ch), (uint8_t) e.pitch, 0);
        sounding_[part][(size_t) e.pitch] = 0;
    }
}

void Player::stop (MidiOut* midi, int sampleOffset)
{
    synth.allNotesOff();
    for (int p = 0; p < kNumParts; ++p)
        for (int n = 0; n < 128; ++n)
            if (sounding_[(size_t) p][(size_t) n])
            {
                if (midi && midi->send) midi->send (sampleOffset, (uint8_t) (0x80 | std::clamp (midiChannel[(size_t) p], 0, 15)), (uint8_t) n, 0);
                sounding_[(size_t) p][(size_t) n] = 0;
            }
    lastPos_ = -1;
}

void Player::process (double ppqStart, double bpm, bool playing, int numSamples, float* left, float* right, MidiOut* midi)
{
    if (needsStop_)
    {
        stop (midi, 0);
        needsStop_ = false;
    }
    const auto& events = seq_->events;
    if (! playing || bpm <= 0 || events.empty())
    {
        if (wasPlaying_) stop (midi, 0);
        wasPlaying_ = false;
        synth.render (left, right, numSamples);
        return;
    }
    const double L = seq_->length;
    const double beatsPerSample = bpm / 60.0 / synth.sampleRate();
    const double blockBeats = numSamples * beatsPerSample;
    double pos = std::fmod (ppqStart, L);
    if (pos < 0) pos += L;
    if (wasPlaying_ && lastPos_ >= 0)
    {
        double diff = std::abs (pos - lastPos_);
        if (diff > 0.05 && std::abs (diff - L) > 0.05) stop (midi, 0);  // transport jumped
    }
    wasPlaying_ = true;

    int rendered = 0;
    auto renderTo = [&] (int offset) {
        offset = std::clamp (offset, rendered, numSamples);
        if (offset > rendered) synth.render (left + rendered, right + rendered, offset - rendered);
        rendered = offset;
    };
    double winStart = pos, consumed = 0;
    while (consumed < blockBeats - 1e-12)
    {
        double winEnd = std::min (L, winStart + (blockBeats - consumed));
        auto it = std::lower_bound (events.begin(), events.end(), winStart - 1e-12,
                                    [] (const Event& e, double b) { return e.beat < b; });
        for (; it != events.end() && it->beat < winEnd; ++it)
        {
            int offset = (int) std::floor ((consumed + (it->beat - winStart)) / beatsPerSample);
            renderTo (offset);
            fire (*it, bpm, midi, std::clamp (offset, 0, numSamples - 1));
        }
        consumed += winEnd - winStart;
        winStart = winEnd >= L - 1e-12 ? 0.0 : winEnd;
    }
    renderTo (numSamples);
    lastPos_ = std::fmod (pos + blockBeats, L);
}

void renderScore (const Score& score, double sampleRate, std::vector<float>& left, std::vector<float>& right,
                  double tailSeconds, int loops)
{
    Player p;
    p.prepare (sampleRate);
    p.sendMidi = false;
    p.setScore (score);
    const int block = 512;
    const double beatsPerSample = score.bpm / 60.0 / sampleRate;
    const size_t playSamples = (size_t) std::ceil (p.loopLength() * loops / beatsPerSample);
    const size_t total = playSamples + (size_t) (tailSeconds * sampleRate);
    left.assign (total, 0.0f);
    right.assign (total, 0.0f);
    for (size_t i = 0; i < total;)
    {
        const bool playing = i < playSamples;
        const size_t limit = playing ? playSamples : total;
        const int n = (int) std::min<size_t> (block, limit - i);
        p.process ((double) i * beatsPerSample, score.bpm, playing, n, left.data() + i, right.data() + i, nullptr);
        i += (size_t) n;
    }
}

} // namespace mv
