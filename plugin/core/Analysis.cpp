#include "Analysis.h"

#include "Styles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

namespace mv
{
namespace
{
constexpr double kSR = 22050.0;
constexpr int kNfft = 4096;
constexpr int kHop = 256;
constexpr double kPi = 3.14159265358979323846;

int mod12 (int x) { return ((x % 12) + 12) % 12; }
double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

bool cancelled (const AnalyzeOptions& o) { return o.cancel && o.cancel->load(); }

struct Spectrogram
{
    int frames = 0, bins = 0;
    std::vector<float> mag;  // frames * bins
    float* frame (int t) { return mag.data() + (size_t) t * (size_t) bins; }
    const float* frame (int t) const { return mag.data() + (size_t) t * (size_t) bins; }
};

Spectrogram stft (const std::vector<float>& y, const AnalyzeOptions& o)
{
    Spectrogram S;
    S.bins = kNfft / 2 + 1;
    S.frames = (int) (y.size() / kHop) + 1;
    S.mag.assign ((size_t) S.frames * (size_t) S.bins, 0.0f);
    std::vector<float> win (kNfft), re (kNfft), im (kNfft);
    for (int i = 0; i < kNfft; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / kNfft));
    for (int t = 0; t < S.frames; ++t)
    {
        if ((t & 255) == 0)
        {
            if (cancelled (o)) return S;
            if (o.progress) o.progress (0.05f + 0.35f * (float) t / (float) S.frames);
        }
        long centre = (long) t * kHop;
        for (int i = 0; i < kNfft; ++i)
        {
            long idx = centre - kNfft / 2 + i;
            re[(size_t) i] = (idx >= 0 && idx < (long) y.size()) ? y[(size_t) idx] * win[(size_t) i] : 0.0f;
            im[(size_t) i] = 0.0f;
        }
        fft (re, im);
        float* out = S.frame (t);
        for (int k = 0; k < S.bins; ++k)
            out[k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
    }
    return S;
}

// ---------------------------------------------------------------------------
// Tempo & beats (spectral-flux onset envelope, autocorrelation tempo, Ellis DP beat tracker)
// ---------------------------------------------------------------------------

std::vector<float> onsetEnvelope (const Spectrogram& S)
{
    std::vector<float> env ((size_t) S.frames, 0.0f);
    std::vector<float> prev ((size_t) S.bins, 0.0f), cur ((size_t) S.bins);
    for (int t = 0; t < S.frames; ++t)
    {
        const float* f = S.frame (t);
        double flux = 0;
        for (int k = 1; k < S.bins / 2; ++k)
        {
            cur[(size_t) k] = std::log1p (1000.0f * f[k]);
            double d = cur[(size_t) k] - prev[(size_t) k];
            if (d > 0) flux += d;
        }
        env[(size_t) t] = t ? (float) flux : 0.0f;
        std::swap (prev, cur);
    }
    // remove slow trend, half-wave rectify, normalise
    std::vector<float> out (env.size());
    const int w = 16;
    for (size_t i = 0; i < env.size(); ++i)
    {
        double m = 0;
        int c = 0;
        for (long j = (long) i - w; j <= (long) i + w; ++j)
            if (j >= 0 && j < (long) env.size()) { m += env[(size_t) j]; ++c; }
        out[i] = std::max (0.0f, env[i] - (float) (m / std::max (1, c)));
    }
    float mx = *std::max_element (out.begin(), out.end());
    if (mx > 0) for (auto& v : out) v /= mx;
    return out;
}

double estimateTempo (const std::vector<float>& env)
{
    const double fps = kSR / kHop;
    const int minLag = (int) std::floor (fps * 60.0 / 220.0), maxLag = (int) std::ceil (fps * 60.0 / 50.0);
    if ((int) env.size() < maxLag * 2) return 120.0;
    double bestScore = -1, bestBpm = 120;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0;
        for (size_t i = (size_t) lag; i < env.size(); ++i) ac += env[i] * env[i - (size_t) lag];
        ac /= (double) (env.size() - (size_t) lag);
        double bpm = 60.0 * fps / lag;
        double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.9, 2));
        if (ac * prior > bestScore) { bestScore = ac * prior; bestBpm = bpm; }
    }
    // refine with parabolic interpolation is unnecessary at this resolution; fold into 60..180
    while (bestBpm < 60) bestBpm *= 2;
    while (bestBpm > 180) bestBpm /= 2;
    return bestBpm;
}

std::vector<double> trackBeats (const std::vector<float>& env, double bpm)
{
    const double fps = kSR / kHop;
    const double period = fps * 60.0 / bpm;
    const size_t n = env.size();
    std::vector<double> score (n), localScore (n);
    std::vector<long> backlink (n, -1);
    // smooth onset envelope with a gaussian of width period/32
    double sigma = std::max (1.0, period / 32.0);
    int half = (int) std::ceil (3 * sigma);
    for (size_t i = 0; i < n; ++i)
    {
        double s = 0;
        for (int k = -half; k <= half; ++k)
        {
            long j = (long) i + k;
            if (j >= 0 && j < (long) n) s += env[(size_t) j] * std::exp (-0.5 * (k / sigma) * (k / sigma));
        }
        localScore[i] = s;
    }
    const double alpha = 100.0;
    for (size_t i = 0; i < n; ++i)
    {
        long lo = (long) i - (long) std::round (2 * period), hi = (long) i - (long) std::round (period / 2);
        double best = -1e30;
        long arg = -1;
        for (long j = std::max (0L, lo); j <= hi; ++j)
        {
            double tx = std::log ((double) ((long) i - j) / period);
            double v = score[(size_t) j] - alpha * tx * tx;
            if (v > best) { best = v; arg = j; }
        }
        score[i] = localScore[i] + (arg >= 0 ? best : 0.0);
        backlink[i] = arg;
    }
    // start from the best score within the last period
    size_t start = n > (size_t) period ? n - (size_t) period : 0;
    size_t last = start;
    for (size_t i = start; i < n; ++i) if (score[i] > score[last]) last = i;
    std::vector<double> beats;
    for (long i = (long) last; i >= 0; i = backlink[(size_t) i])
    {
        beats.push_back ((double) i / fps);
        if (backlink[(size_t) i] < 0) break;
    }
    std::reverse (beats.begin(), beats.end());
    // drop weak leading beats (silence before the music starts)
    double thresh = 0.1 * (*std::max_element (localScore.begin(), localScore.end()));
    while (beats.size() > 2 && localScore[(size_t) (beats.front() * fps)] < thresh) beats.erase (beats.begin());
    return beats;
}

// ---------------------------------------------------------------------------
// Chroma (spectral peaks -> pitch classes; treble from C3 up, bass C1..C3)
// ---------------------------------------------------------------------------

struct ChromaFrames { std::vector<std::array<float, 12>> treble, bass; };

ChromaFrames chroma (const Spectrogram& S)
{
    ChromaFrames C;
    C.treble.assign ((size_t) S.frames, {});
    C.bass.assign ((size_t) S.frames, {});
    const double binHz = kSR / kNfft;
    // global tuning from peak deviations
    std::vector<double> devs;
    for (int t = 0; t < S.frames; t += 4)
    {
        const float* f = S.frame (t);
        for (int k = 2; k < S.bins - 1; ++k)
        {
            double hz = k * binHz;
            if (hz < 100 || hz > 2000) continue;
            if (f[k] > f[k - 1] && f[k] >= f[k + 1] && f[k] > 1e-3f)
            {
                double a = std::log (f[k - 1] + 1e-9), b = std::log (f[k] + 1e-9), c = std::log (f[k + 1] + 1e-9);
                double p = 0.5 * (a - c) / (a - 2 * b + c);
                double m = hzToMidi ((k + p) * binHz);
                devs.push_back (m - std::round (m));
            }
        }
    }
    double tuning = 0;
    if (! devs.empty())
    {
        std::nth_element (devs.begin(), devs.begin() + (long) devs.size() / 2, devs.end());
        tuning = devs[devs.size() / 2];
    }
    for (int t = 0; t < S.frames; ++t)
    {
        const float* f = S.frame (t);
        float fmax = *std::max_element (f, f + S.bins);
        if (fmax <= 0) continue;
        for (int k = 2; k < S.bins - 1; ++k)
        {
            double hz = k * binHz;
            if (hz < 32 || hz > 5000) continue;
            if (! (f[k] > f[k - 1] && f[k] >= f[k + 1]) || f[k] < 0.01f * fmax) continue;
            double a = std::log (f[k - 1] + 1e-9), b = std::log (f[k] + 1e-9), c = std::log (f[k + 1] + 1e-9);
            double denom = a - 2 * b + c;
            double p = denom != 0 ? 0.5 * (a - c) / denom : 0;
            double m = hzToMidi ((k + p) * binHz) - tuning;
            double dev = m - std::round (m);
            double w = std::sqrt ((double) f[k]) * std::pow (std::cos (dev * kPi), 2);
            int pc = mod12 ((int) std::lround (m));
            (hz < 130.8 ? C.bass : C.treble)[(size_t) t][(size_t) pc] += (float) w;
        }
    }
    return C;
}

std::vector<std::array<float, 12>> beatSync (const std::vector<std::array<float, 12>>& frames,
                                             const std::vector<int>& bounds)
{
    std::vector<std::array<float, 12>> out;
    for (size_t b = 0; b + 1 < bounds.size(); ++b)
    {
        std::array<float, 12> agg {};
        int lo = bounds[b], hi = std::min ((int) frames.size(), bounds[b + 1]);
        if (hi > lo)
            for (int pc = 0; pc < 12; ++pc)
            {
                std::vector<float> v;
                for (int t = lo; t < hi; ++t) v.push_back (frames[(size_t) t][(size_t) pc]);
                std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
                agg[(size_t) pc] = v[v.size() / 2];
            }
        out.push_back (agg);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

const double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
const double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

double corr (const double* a, const double* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i)
    {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
}

Key estimateKeyProfile (const double profile[12])
{
    double best = -2;
    Key out;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < 2; ++m)
        {
            double ref[12];
            for (int i = 0; i < 12; ++i) ref[mod12 (i + tonic)] = (m ? kMinorProfile : kMajorProfile)[i];
            double r = corr (profile, ref, 12);
            if (r > best) { best = r; out = { tonic, m ? "minor" : "major" }; }
        }
    return out;
}

std::pair<Key, double> resolveKey (const double profile[12], const std::vector<TimedChord>& chords, const Key& fallback)
{
    std::vector<const TimedChord*> named;
    for (auto& c : chords) if (c.chord) named.push_back (&c);
    if (named.empty()) return { fallback, 0.0 };
    std::vector<double> durs;
    for (auto* c : named) durs.push_back (c->end - c->start);
    std::vector<double> sorted = durs;
    std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    const double cap = 2.0 * sorted[sorted.size() / 2];
    double total = 0;
    for (double d : durs) total += std::min (d, cap);
    if (total <= 0) total = 1;
    // cadential arrivals: V->I, bVII->I, bII->I, vii°->I
    std::map<int, int> arrivals;
    int transitions = 0;
    for (size_t i = 0; i + 1 < named.size(); ++i)
    {
        auto& a = *named[i]->chord;
        auto& b = *named[i + 1]->chord;
        if (a.root == b.root) continue;
        ++transitions;
        int iv = mod12 (a.root - b.root);
        if (iv == 7 || iv == 10 || iv == 1 || (iv == 11 && (a.quality == "dim" || a.quality == "dim7")))
            ++arrivals[b.root];
    }
    static const char* const modes[] = { "major", "minor", "mixolydian", "dorian", "harmonic_minor", "phrygian_dominant", "lydian", "phrygian" };
    std::vector<std::pair<double, Key>> ranked;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (auto* mode : modes)
        {
            const std::vector<int> scale = scaleIntervals (mode);
            double tmpl[12] = {};
            bool has7 = false, has4 = false;
            for (int d : scale) { tmpl[mod12 (tonic + d)] = 1.0; has7 |= d == 7; has4 |= d == 4; }
            tmpl[tonic] = 1.6;
            if (has7) tmpl[mod12 (tonic + 7)] = std::max (tmpl[mod12 (tonic + 7)], 1.25);
            double fit = corr (profile, tmpl, 12);
            auto isTonicChord = [&] (const Chord& c) {
                if (c.root != tonic) return false;
                static const std::set<std::string> majQ { "maj", "7", "5", "sus4", "sus2" }, minQ { "min", "m7", "5", "sus4", "sus2" };
                return (has4 ? majQ : minQ).count (c.quality) > 0;
            };
            double tdur = 0;
            for (size_t i = 0; i < named.size(); ++i)
                if (isTonicChord (*named[i]->chord)) tdur += std::min (durs[i], cap);
            double score = fit + 0.6 * tdur / total;
            if (isTonicChord (*named.front()->chord)) score += 0.15;
            if (isTonicChord (*named.back()->chord)) score += 0.06;
            if (std::string (mode) == "major" || std::string (mode) == "minor") score += 0.08;
            score += 0.3 * (arrivals.count (tonic) ? arrivals[tonic] : 0) / std::max (1, transitions);
            ranked.push_back ({ score, Key { tonic, mode } });
        }
    std::sort (ranked.begin(), ranked.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    double margin = ranked[0].first - ranked[1].first;
    return { ranked[0].second, std::clamp (0.5 * ranked[0].first + 2 * margin, 0.0, 1.0) };
}

// ---------------------------------------------------------------------------
// Chords
// ---------------------------------------------------------------------------

std::vector<TimedChord> detectChords (const std::vector<std::array<float, 12>>& treble,
                                      const std::vector<std::array<float, 12>>& bass,
                                      const std::vector<double>& boundsSec, const Key& key)
{
    static const char* const vocab[] = { "maj", "min", "7", "m7", "dim", "sus4" };
    std::vector<std::optional<Chord>> labels;
    std::vector<std::array<double, 12>> tmpl;
    for (auto* q : vocab)
        for (int root = 0; root < 12; ++root)
        {
            std::array<double, 12> v {};
            const std::vector<int> ivs = chordIntervals (q);
            for (size_t k = 0; k < ivs.size(); ++k) v[(size_t) mod12 (root + ivs[k])] = k == 0 ? 1.0 : k < 3 ? 0.85 : 0.6;
            double norm = 0;
            for (double x : v) norm += x * x;
            for (double& x : v) x /= std::sqrt (norm);
            tmpl.push_back (v);
            labels.push_back (Chord { root, q, -1 });
        }
    tmpl.push_back ({});
    for (double& x : tmpl.back()) x = 1.0 / std::sqrt (12.0);
    labels.push_back (std::nullopt);
    const size_t L = labels.size(), T = treble.size();
    if (T == 0) return {};
    auto pcs = key.pitchClasses();
    std::vector<double> prior (L, 1.0);
    for (size_t i = 0; i < L; ++i)
    {
        if (! labels[i]) { prior[i] = 0.6; continue; }
        auto& q = labels[i]->quality;
        if (q == "7" || q == "m7") prior[i] *= 0.93;
        else if (q == "sus4") prior[i] *= 0.9;
        else if (q == "dim") prior[i] *= 0.86;
        bool diatonic = true;
        for (int p : labels[i]->pitchClasses()) diatonic &= pcs.count (p) > 0;
        if (diatonic) prior[i] *= 1.06;
    }
    std::vector<double> energy (T);
    for (size_t t = 0; t < T; ++t)
    {
        energy[t] = 0;
        for (float x : treble[t]) energy[t] += x;
        for (int pc = 0; pc < 12; ++pc) energy[t] += 0.3 * bass[t][(size_t) pc];
    }
    std::vector<double> sortedE = energy;
    std::nth_element (sortedE.begin(), sortedE.begin() + (long) T / 2, sortedE.end());
    double medE = sortedE[T / 2];

    const double beta = 25.0, bassVote = 0.3, stay = 0.85;
    std::vector<std::vector<double>> logp (T, std::vector<double> (L));
    std::vector<std::vector<double>> prob (T, std::vector<double> (L));
    for (size_t t = 0; t < T; ++t)
    {
        std::array<double, 12> v {};
        double n = 0;
        for (int pc = 0; pc < 12; ++pc) { v[(size_t) pc] = treble[t][(size_t) pc] + 0.3 * bass[t][(size_t) pc]; n += v[(size_t) pc] * v[(size_t) pc]; }
        n = std::sqrt (n) + 1e-9;
        double bmax = *std::max_element (bass[t].begin(), bass[t].end()) + 1e-9;
        std::vector<double> sim (L);
        for (size_t i = 0; i < L; ++i)
        {
            double s = 0;
            for (int pc = 0; pc < 12; ++pc) s += tmpl[i][(size_t) pc] * v[(size_t) pc] / n;
            s *= prior[i];
            double support = labels[i] ? bass[t][(size_t) labels[i]->root] / bmax : 0.5;
            sim[i] = s + bassVote * (support - 0.5);
        }
        if (energy[t] < 0.05 * (medE + 1e-9)) sim[L - 1] = 2.0;
        double mx = *std::max_element (sim.begin(), sim.end()), z = 0;
        for (size_t i = 0; i < L; ++i) { prob[t][i] = std::exp (beta * (sim[i] - mx)); z += prob[t][i]; }
        for (size_t i = 0; i < L; ++i) { prob[t][i] /= z; logp[t][i] = std::log (prob[t][i] + 1e-300); }
    }
    // Viterbi with a self-loop transition matrix
    const double lStay = std::log (stay), lMove = std::log ((1 - stay) / (double) (L - 1));
    std::vector<double> delta = logp[0];
    std::vector<std::vector<int>> psi (T, std::vector<int> (L, 0));
    for (size_t t = 1; t < T; ++t)
    {
        size_t bestPrev = (size_t) (std::max_element (delta.begin(), delta.end()) - delta.begin());
        std::vector<double> next (L);
        for (size_t i = 0; i < L; ++i)
        {
            double viaStay = delta[i] + lStay, viaMove = delta[bestPrev] + lMove;
            if (viaStay >= viaMove || bestPrev == i) { next[i] = viaStay + logp[t][i]; psi[t][i] = (int) i; }
            else { next[i] = viaMove + logp[t][i]; psi[t][i] = (int) bestPrev; }
        }
        delta = std::move (next);
    }
    std::vector<int> path (T);
    path[T - 1] = (int) (std::max_element (delta.begin(), delta.end()) - delta.begin());
    for (size_t t = T - 1; t > 0; --t) path[t - 1] = psi[t][(size_t) path[t]];

    std::vector<TimedChord> out;
    for (size_t t = 0; t < T; ++t)
    {
        double s = boundsSec[t], e = boundsSec[t + 1], conf = prob[t][(size_t) path[t]];
        auto& lab = labels[(size_t) path[t]];
        if (! out.empty() && out.back().chord == lab)
        {
            auto& p = out.back();
            double a = p.end - p.start, b = e - s;
            p.confidence = (p.confidence * a + conf * b) / std::max (1e-9, a + b);
            p.end = e;
        }
        else
            out.push_back ({ s, e, lab, conf });
    }
    return out;
}

// ---------------------------------------------------------------------------
// Melody: harmonic-sum salience on a log-frequency spectrum + Viterbi continuity
// ---------------------------------------------------------------------------

std::vector<double> pitchTrack (const Spectrogram& S, const std::vector<float>& rms, double fmin, double fmax,
                                const AnalyzeOptions& o)
{
    const int bpo = 36;
    const double cfmin = 55.0, binHz = kSR / kNfft;
    const int nLog = (int) std::floor (bpo * std::log2 (std::min (kSR / 2 * 0.85, fmax * 6) / cfmin));
    const int lo = (int) std::lround (bpo * std::log2 (fmin / cfmin));
    const int hi = std::min ((int) std::lround (bpo * std::log2 (fmax / cfmin)), nLog - 1);
    const int nStates = hi - lo + 1;
    const int T = S.frames;
    std::vector<double> path (T, NAN);
    if (nStates <= 1 || T == 0) return path;

    // log-frequency magnitude
    std::vector<float> C ((size_t) T * (size_t) nLog);
    float gmax = 1e-9f;
    for (float v : S.mag) gmax = std::max (gmax, v);
    std::vector<double> pos ((size_t) nLog);
    for (int b = 0; b < nLog; ++b) pos[(size_t) b] = cfmin * std::pow (2.0, b / (double) bpo) / binHz;
    for (int t = 0; t < T; ++t)
    {
        const float* f = S.frame (t);
        for (int b = 0; b < nLog; ++b)
        {
            double p = pos[(size_t) b];
            int k = (int) p;
            double fr = p - k;
            double m = (k + 1 < S.bins) ? f[k] * (1 - fr) + f[k + 1] * fr : 0.0;
            C[(size_t) t * (size_t) nLog + (size_t) b] = (float) std::log1p (100.0 * m / gmax);
        }
    }
    // whitening: remove a one-octave moving average across frequency
    std::vector<float> row ((size_t) nLog);
    for (int t = 0; t < T; ++t)
    {
        float* c = C.data() + (size_t) t * (size_t) nLog;
        std::vector<double> csum ((size_t) nLog + 1, 0.0);
        for (int b = 0; b < nLog; ++b) csum[(size_t) b + 1] = csum[(size_t) b] + c[b];
        for (int b = 0; b < nLog; ++b)
        {
            int a = std::max (0, b - bpo / 2), z = std::min (nLog, b + bpo / 2 + 1);
            row[(size_t) b] = std::max (0.0f, c[b] - (float) ((csum[(size_t) z] - csum[(size_t) a]) / (z - a)));
        }
        std::copy (row.begin(), row.end(), c);
    }
    if (cancelled (o)) return path;
    // harmonic sum
    const int harmonics[] = { 1, 2, 3, 4, 5, 6 };
    const double weights[] = { 1.0, 0.8, 0.64, 0.51, 0.41, 0.33 };
    std::vector<float> sal ((size_t) T * (size_t) nStates, 0.0f);
    for (int t = 0; t < T; ++t)
        for (int s = 0; s < nStates; ++s)
        {
            double acc = 0;
            for (int h = 0; h < 6; ++h)
            {
                int idx = lo + s + (int) std::lround (bpo * std::log2 ((double) harmonics[h]));
                if (idx < nLog) acc += weights[h] * C[(size_t) t * (size_t) nLog + (size_t) idx];
            }
            sal[(size_t) t * (size_t) nStates + (size_t) s] = (float) acc;
        }
    // subtract half the per-bin median (sustained accompaniment) and favour higher candidates
    for (int s = 0; s < nStates; ++s)
    {
        std::vector<float> col ((size_t) T);
        for (int t = 0; t < T; ++t) col[(size_t) t] = sal[(size_t) t * (size_t) nStates + (size_t) s];
        std::nth_element (col.begin(), col.begin() + T / 2, col.end());
        float med = col[(size_t) T / 2];
        double bias = 1.0 + 2.0 * s / (double) (nStates - 1);
        for (int t = 0; t < T; ++t)
        {
            float& v = sal[(size_t) t * (size_t) nStates + (size_t) s];
            v = (float) (std::max (0.0f, v - 0.5f * med) * bias);
        }
    }
    if (o.progress) o.progress (0.7f);
    // Viterbi (log domain): banded gaussian transitions + small floor
    const int band = 30;
    std::vector<double> logT ((size_t) band + 1);
    double norm = 0;
    for (int d = -nStates; d <= nStates; ++d) norm += std::exp (-std::pow (d / 9.0, 2)) + 1e-3;
    norm /= 2;  // approximate row sum
    for (int d = 0; d <= band; ++d) logT[(size_t) d] = std::log ((std::exp (-std::pow (d / 9.0, 2)) + 1e-3) / norm);
    const double logFloor = std::log (1e-3 / norm);
    std::vector<double> delta ((size_t) nStates), next ((size_t) nStates);
    std::vector<std::vector<short>> psi ((size_t) T, std::vector<short> ((size_t) nStates));
    auto emission = [&] (int t, std::vector<double>& e) {
        double mx = 1e-9;
        for (int s = 0; s < nStates; ++s) mx = std::max (mx, (double) sal[(size_t) t * (size_t) nStates + (size_t) s]);
        double z = 0;
        for (int s = 0; s < nStates; ++s) { e[(size_t) s] = std::pow (sal[(size_t) t * (size_t) nStates + (size_t) s] / mx, 4) + 1e-6; z += e[(size_t) s]; }
        for (auto& v : e) v = std::log (v / z);
    };
    std::vector<double> e ((size_t) nStates);
    emission (0, e);
    delta = e;
    for (int t = 1; t < T; ++t)
    {
        if ((t & 1023) == 0 && cancelled (o)) return path;
        emission (t, e);
        int gArg = (int) (std::max_element (delta.begin(), delta.end()) - delta.begin());
        double gBest = delta[(size_t) gArg] + logFloor;
        for (int s = 0; s < nStates; ++s)
        {
            double best = gBest;
            int arg = gArg;
            for (int j = std::max (0, s - band); j <= std::min (nStates - 1, s + band); ++j)
            {
                double v = delta[(size_t) j] + logT[(size_t) std::abs (s - j)];
                if (v > best) { best = v; arg = j; }
            }
            next[(size_t) s] = best + e[(size_t) s];
            psi[(size_t) t][(size_t) s] = (short) arg;
        }
        std::swap (delta, next);
    }
    std::vector<int> states ((size_t) T);
    states[(size_t) T - 1] = (int) (std::max_element (delta.begin(), delta.end()) - delta.begin());
    for (int t = T - 1; t > 0; --t) states[(size_t) t - 1] = psi[(size_t) t][(size_t) states[(size_t) t]];
    // voicing
    std::vector<float> ps ((size_t) T);
    for (int t = 0; t < T; ++t) ps[(size_t) t] = sal[(size_t) t * (size_t) nStates + (size_t) states[(size_t) t]];
    std::vector<float> sortedPs = ps;
    std::sort (sortedPs.begin(), sortedPs.end());
    double thresh = 0.3 * sortedPs[(size_t) (0.95 * (T - 1))];
    float rmax = rms.empty() ? 0.0f : *std::max_element (rms.begin(), rms.end());
    const double base = hzToMidi (cfmin);
    for (int t = 0; t < T; ++t)
        if (ps[(size_t) t] > thresh && (size_t) t < rms.size() && rms[(size_t) t] > 0.02f * rmax)
            path[(size_t) t] = base + (lo + states[(size_t) t]) * 12.0 / bpo;
    // remove global tuning offset
    std::vector<double> devs;
    for (double m : path) if (! std::isnan (m)) devs.push_back (std::fmod (m - std::round (m) + 1.5, 1.0) - 0.5);
    if (! devs.empty())
    {
        std::nth_element (devs.begin(), devs.begin() + (long) devs.size() / 2, devs.end());
        double tuning = devs[devs.size() / 2];
        for (double& m : path) if (! std::isnan (m)) m -= tuning;
    }
    return path;
}

std::vector<TimedNote> segmentNotes (const std::vector<double>& midiIn, const std::vector<float>& rms,
                                     const std::set<int>& onsets, double minNote = 0.07)
{
    const double frameDur = kHop / kSR;
    const size_t n = midiIn.size();
    std::vector<double> smooth = midiIn;
    for (size_t i = 0; i < n; ++i)  // 5-point median on voiced frames
    {
        if (std::isnan (midiIn[i])) continue;
        double w[5];
        for (int k = -2; k <= 2; ++k)
        {
            long j = (long) i + k;
            w[k + 2] = (j >= 0 && j < (long) n && ! std::isnan (midiIn[(size_t) j])) ? midiIn[(size_t) j] : 0.0;
        }
        std::sort (w, w + 5);
        smooth[i] = w[2];
    }
    struct Raw { double s, e; int pitch; double loud; };
    std::vector<Raw> notes;
    long segStart = -1;
    std::vector<double> vals, pending;
    std::vector<long> pendingIdx;
    const size_t changeFrames = (size_t) std::max (2, (int) (0.045 / frameDur));
    auto median = [] (std::vector<double> v) {
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    auto close = [&] (long endFrame) {
        if (segStart >= 0 && ! vals.empty())
        {
            double s = segStart * frameDur, e = std::min (endFrame, (long) n - 1) * frameDur + frameDur;
            if (e - s >= minNote)
            {
                double loud = 0;
                int c = 0;
                for (long t = segStart; t <= endFrame && t < (long) rms.size(); ++t) { loud += rms[(size_t) t]; ++c; }
                notes.push_back ({ s, e, (int) std::lround (median (vals)), c ? loud / c : 0.0 });
            }
        }
        segStart = -1;
        vals.clear();
    };
    for (size_t i = 0; i < n; ++i)
    {
        if (std::isnan (midiIn[i]))
        {
            if (segStart >= 0) close ((long) i - 1);
            pending.clear(); pendingIdx.clear();
            continue;
        }
        double v = smooth[i];
        if (segStart < 0) { segStart = (long) i; vals = { v }; pending.clear(); pendingIdx.clear(); continue; }
        std::vector<double> recent (vals.end() - (long) std::min<size_t> (12, vals.size()), vals.end());
        double cur = median (recent);
        if (onsets.count ((int) i) && std::abs (v - cur) < 0.5 && vals.size() * frameDur > 0.12)
        {
            close ((long) i - 1);
            segStart = (long) i;
            vals = { v };
            continue;
        }
        if (std::abs (v - cur) > 0.7)
        {
            pending.push_back (v);
            pendingIdx.push_back ((long) i);
            if (pending.size() >= changeFrames)
            {
                close (pendingIdx.front() - 1);
                segStart = pendingIdx.front();
                vals = pending;
                pending.clear(); pendingIdx.clear();
            }
        }
        else
        {
            vals.insert (vals.end(), pending.begin(), pending.end());
            pending.clear(); pendingIdx.clear();
            vals.push_back (v);
        }
    }
    close ((long) n - 1);
    std::vector<Raw> merged;
    for (auto& r : notes)
    {
        if (! merged.empty() && merged.back().pitch == r.pitch && r.s - merged.back().e < 0.03) merged.back().e = r.e;
        else merged.push_back (r);
    }
    std::vector<TimedNote> out;
    if (merged.empty()) return out;
    std::vector<double> db;
    for (auto& r : merged) db.push_back (20 * std::log10 (r.loud + 1e-6));
    std::vector<double> sorted = db;
    std::sort (sorted.begin(), sorted.end());
    double lo = sorted[(size_t) (0.05 * (sorted.size() - 1))], hi = sorted[(size_t) (0.95 * (sorted.size() - 1))];
    for (size_t i = 0; i < merged.size(); ++i)
        out.push_back ({ merged[i].s, merged[i].e, merged[i].pitch,
                         (int) std::clamp (55 + 60 * (db[i] - lo) / std::max (1e-6, hi - lo), 40.0, 120.0) });
    return out;
}

std::string simplifyRoman (const std::string& r)
{
    static const char* const sufs[] = { "maj7", "maj9", "sus2", "sus4", "o7", ("\xC3\xB8" "7"), "\xC3\xB8", "add9", "7", "9", "6", "5", "+", "o" };
    for (auto* s : sufs)
    {
        std::string suf (s);
        if (r.size() > suf.size() && r.compare (r.size() - suf.size(), suf.size(), suf) == 0)
            return r.substr (0, r.size() - suf.size());
    }
    return r;
}

std::vector<std::string> dedupe (const std::vector<std::string>& v)
{
    std::vector<std::string> out;
    for (auto& x : v) if (out.empty() || out.back() != x) out.push_back (x);
    return out;
}

double quantizeBeat (double b, double grid, double phase) { return std::round ((b - phase) / grid) * grid + phase; }
} // namespace

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

void fft (std::vector<float>& re, std::vector<float>& im)
{
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        double ang = -2 * kPi / (double) len;
        float wr = (float) std::cos (ang), wi = (float) std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            float cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                size_t a = i + k, b = i + k + len / 2;
                float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

std::vector<float> resample (const float* x, size_t n, double fromRate, double toRate)
{
    if (std::abs (fromRate - toRate) < 1e-6) return std::vector<float> (x, x + n);
    std::vector<float> src (x, x + n);
    if (toRate < fromRate)
    {
        // windowed-sinc low-pass at 0.45 * target rate
        const int taps = 63, half = taps / 2;
        const double fc = 0.45 * toRate / fromRate;
        std::vector<double> h ((size_t) taps);
        double sum = 0;
        for (int i = 0; i < taps; ++i)
        {
            int m = i - half;
            double sinc = m == 0 ? 2 * fc : std::sin (2 * kPi * fc * m) / (kPi * m);
            double w = 0.42 - 0.5 * std::cos (2 * kPi * i / (taps - 1)) + 0.08 * std::cos (4 * kPi * i / (taps - 1));
            h[(size_t) i] = sinc * w;
            sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;
        std::vector<float> f (n);
        for (size_t i = 0; i < n; ++i)
        {
            double acc = 0;
            for (int k = 0; k < taps; ++k)
            {
                long j = (long) i + k - half;
                if (j >= 0 && j < (long) n) acc += h[(size_t) k] * x[j];
            }
            f[i] = (float) acc;
        }
        src.swap (f);
    }
    const double ratio = fromRate / toRate;
    const size_t outN = (size_t) std::floor ((double) n / ratio);
    std::vector<float> out (outN);
    for (size_t i = 0; i < outN; ++i)
    {
        double p = (double) i * ratio;
        size_t k = (size_t) p;
        double fr = p - (double) k;
        float a = src[std::min (k, n - 1)], b = src[std::min (k + 1, n - 1)];
        out[i] = (float) (a + (b - a) * fr);
    }
    return out;
}

Analysis analyze (const float* mono, size_t numSamples, double sampleRate, const AnalyzeOptions& o)
{
    Analysis A;
    if (numSamples == 0) return A;
    if (o.progress) o.progress (0.0f);
    auto y = resample (mono, numSamples, sampleRate, kSR);
    float peak = 0;
    for (float v : y) peak = std::max (peak, std::abs (v));
    if (peak > 0) for (auto& v : y) v /= peak;
    A.duration = (double) y.size() / kSR;

    auto S = stft (y, o);
    if (cancelled (o)) return A;
    auto env = onsetEnvelope (S);
    A.bpm = o.fixedBpm > 0 ? o.fixedBpm : estimateTempo (env);
    A.beatTimes = trackBeats (env, A.bpm);
    if (A.beatTimes.size() < 2)
    {
        A.beatTimes.clear();
        for (double t = 0; t < A.duration; t += 60.0 / A.bpm) A.beatTimes.push_back (t);
    }
    if (o.progress) o.progress (0.45f);

    auto C = chroma (S);
    const double fps = kSR / kHop;
    std::vector<int> bounds { 0 };
    std::vector<double> boundsSec { 0.0 };
    for (double b : A.beatTimes)
    {
        int f = (int) std::lround (b * fps);
        if (f > bounds.back() && f < S.frames) { bounds.push_back (f); boundsSec.push_back (f / fps); }
    }
    bounds.push_back (S.frames);
    boundsSec.push_back (A.duration);
    auto tSync = beatSync (C.treble, bounds), bSync = beatSync (C.bass, bounds);

    double profile[12] = {};
    for (size_t t = 0; t < C.treble.size(); ++t)
        for (int pc = 0; pc < 12; ++pc) profile[pc] += C.treble[t][(size_t) pc] + 0.3 * C.bass[t][(size_t) pc];
    Key kk = estimateKeyProfile (profile);
    A.chords = detectChords (tSync, bSync, boundsSec, kk);
    if (! A.chords.empty()) A.chords.back().end = A.duration;
    std::tie (A.key, A.keyConfidence) = resolveKey (profile, A.chords, kk);
    if (o.progress) o.progress (0.55f);

    if (o.melody && ! cancelled (o))
    {
        std::vector<float> rms ((size_t) S.frames, 0.0f);
        for (int t = 0; t < S.frames; ++t)
        {
            double acc = 0;
            int c = 0;
            for (long i = (long) t * kHop - 1024; i < (long) t * kHop + 1024; ++i)
                if (i >= 0 && i < (long) y.size()) { acc += y[(size_t) i] * y[(size_t) i]; ++c; }
            rms[(size_t) t] = c ? (float) std::sqrt (acc / c) : 0.0f;
        }
        auto track = pitchTrack (S, rms, o.fminHz, o.fmaxHz, o);
        // onsets: peaks of the onset envelope
        std::set<int> onsets;
        double mean = std::accumulate (env.begin(), env.end(), 0.0) / std::max<size_t> (1, env.size());
        for (size_t i = 1; i + 1 < env.size(); ++i)
            if (env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > mean + 0.07) onsets.insert ((int) i);
        if (! cancelled (o)) A.melody = segmentNotes (track, rms, onsets);
    }
    if (o.progress) o.progress (1.0f);
    return A;
}

// ---------------------------------------------------------------------------
// Analysis -> labels / Score
// ---------------------------------------------------------------------------

std::vector<std::string> Analysis::romans() const
{
    std::vector<std::string> out;
    for (auto& c : chords) if (c.chord) out.push_back (romanName (*c.chord, key));
    return out;
}

std::string Analysis::chordChart (size_t maxChords) const
{
    std::vector<std::string> names;
    for (auto& c : chords) if (c.chord) names.push_back (c.chord->name (key.prefersFlats()));
    names = dedupe (names);
    std::string out;
    for (size_t i = 0; i < names.size() && i < maxChords; ++i) out += (i ? " " : "") + names[i];
    if (names.size() > maxChords) out += " ...";
    return out;
}

std::vector<FamousMatch> Analysis::famous() const
{
    std::vector<std::string> seq;
    for (auto& r : romans()) seq.push_back (simplifyRoman (r));
    seq = dedupe (seq);
    std::vector<FamousMatch> hits;
    if (seq.empty()) return hits;
    std::string text = " ";
    for (size_t i = 0; i < seq.size() + std::min<size_t> (8, seq.size()); ++i) text += seq[i % seq.size()] + " ";
    for (auto& f : famousProgressions())
    {
        auto toks = splitWords (f.roman);
        if (toks.size() < 2) continue;
        std::string pat = " ";
        for (auto& t : toks) pat += simplifyRoman (t) + " ";
        if (text.find (pat) != std::string::npos) hits.push_back ({ f.name, f.roman, f.heardIn });
    }
    std::stable_sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return splitWords (a.roman).size() > splitWords (b.roman).size(); });
    return hits;
}

std::vector<std::pair<std::string, double>> Analysis::styleAffinity() const
{
    std::vector<std::string> seq;
    for (auto& r : romans()) seq.push_back (simplifyRoman (r));
    seq = dedupe (seq);
    std::set<std::pair<std::string, std::string>> pairs;
    for (size_t i = 0; i + 1 < seq.size(); ++i) pairs.insert ({ seq[i], seq[i + 1] });
    std::set<std::string> vocab (seq.begin(), seq.end());
    std::vector<std::pair<std::string, double>> out;
    for (auto& st : styles())
    {
        double best = 0;
        for (auto& prog : st.progressions)
        {
            std::vector<std::string> toks;
            for (auto& t : splitWords (prog)) toks.push_back (simplifyRoman (t.substr (0, t.find ('/'))));
            std::set<std::pair<std::string, std::string>> pp;
            for (size_t i = 0; i + 1 < toks.size(); ++i) if (toks[i] != toks[i + 1]) pp.insert ({ toks[i], toks[i + 1] });
            if (pp.empty()) continue;
            int pairHits = 0;
            for (auto& p : pp) pairHits += pairs.count (p) ? 1 : 0;
            std::set<std::string> tv (toks.begin(), toks.end());
            int vocabHits = 0;
            for (auto& t : tv) vocabHits += vocab.count (t) ? 1 : 0;
            best = std::max (best, 0.7 * pairHits / (double) pp.size() + 0.3 * vocabHits / (double) tv.size());
        }
        out.push_back ({ st.name, best });
    }
    std::stable_sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    return out;
}

Score analysisToScore (const Analysis& a, int quantize, const std::string& melodyInstrument, const std::string& chordInstrument)
{
    Score score;
    score.bpm = a.bpm;
    score.key = a.key;
    score.style = "transcription";
    const double toBeats = a.bpm / 60.0;
    const double grid = quantize > 0 ? 4.0 / quantize : 0.0;
    const double phase = (grid > 0 && ! a.beatTimes.empty()) ? std::fmod (a.beatTimes[0] * toBeats, grid) : 0.0;

    Track mel { "Melody", melodyInstrument, {}, false, {} };
    for (auto& n : a.melody)
    {
        double s = n.start * toBeats, e = n.end * toBeats;
        if (grid > 0)
        {
            s = quantizeBeat (s, grid, phase);
            e = quantizeBeat (e, grid, phase);
            if (e - s < grid) e = s + grid;
        }
        s = std::max (0.0, s);
        mel.notes.push_back ({ s, e - s, n.pitch, n.velocity });
    }
    if (grid > 0)
    {
        std::sort (mel.notes.begin(), mel.notes.end(), [] (auto& x, auto& y) { return x.start < y.start; });
        std::vector<Note> dedup;
        for (auto& n : mel.notes)
        {
            if (! dedup.empty() && std::abs (dedup.back().start - n.start) < 1e-6) continue;
            if (! dedup.empty() && dedup.back().end() > n.start) dedup.back().duration = std::max (grid, n.start - dedup.back().start);
            dedup.push_back (n);
        }
        mel.notes = dedup;
    }
    score.tracks.push_back (mel);

    Track chordsTrack { "Chords", chordInstrument, {}, false, {} };
    Track bass { "Bass", "bass", {}, false, {} };
    std::vector<int> prev;
    for (auto& c : a.chords)
    {
        double s = c.start * toBeats, e = c.end * toBeats;
        if (grid > 0) { s = quantizeBeat (s, grid, phase); e = quantizeBeat (e, grid, phase); }
        s = std::max (0.0, s);
        if (e - s <= 0) continue;
        score.chords.push_back ({ s, e - s, c.chord });
        if (! c.chord) continue;
        auto v = c.chord->voicing (60, prev.empty() ? nullptr : &prev);
        prev = v;
        for (int p : v) chordsTrack.notes.push_back ({ s, e - s, p, 72 });
        bass.notes.push_back ({ s, e - s, 36 + mod12 (c.chord->root - 36), 90 });
    }
    score.tracks.push_back (chordsTrack);
    score.tracks.push_back (bass);
    score.lengthBeats = std::max (4.0, std::ceil (score.contentEnd() / 4.0) * 4.0);
    return score;
}

Key keyFromChords (const std::vector<ChordEvent>& timeline)
{
    double profile[12] = {};
    std::vector<TimedChord> chords;
    for (auto& ev : timeline)
    {
        if (! ev.chord) continue;
        for (int pc : ev.chord->pitchClasses()) profile[pc] += ev.duration * (pc == ev.chord->root ? 1.2 : 1.0);
        chords.push_back ({ ev.start, ev.start + ev.duration, ev.chord, 1.0 });
    }
    if (chords.empty()) return {};
    Key kk = estimateKeyProfile (profile);
    return resolveKey (profile, chords, kk).first;
}

std::vector<ChordEvent> harmonyTimeline (const Analysis& a, int quantize)
{
    auto score = analysisToScore (a, quantize);
    Chord tonic { a.key.tonic, a.key.isMinorish() ? "min" : "maj", -1 };
    auto tl = score.chords;
    for (auto& c : tl) if (! c.chord) c.chord = tonic;
    return tl;
}

} // namespace mv
