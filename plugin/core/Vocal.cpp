#include "Vocal.h"

#include "Generator.h"
#include "Styles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>

namespace mv
{
namespace
{
constexpr double kSR = 22050.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

int mod12 (int x) { return ((x % 12) + 12) % 12; }

double medianOf (std::vector<double> v)
{
    if (v.empty()) return kNaN;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    std::sort (v.begin(), v.end());
    return v[(size_t) std::clamp (p * (double) (v.size() - 1), 0.0, (double) v.size() - 1)];
}

std::string fmt (const char* f, double a) { char b[96]; std::snprintf (b, sizeof b, f, a); return b; }
std::string fmt (const char* f, double a, double c) { char b[96]; std::snprintf (b, sizeof b, f, a, c); return b; }

// ---------------------------------------------------------------------------
// YIN pitch tracker (FFT-accelerated difference function)
// ---------------------------------------------------------------------------

struct PitchFrames { std::vector<double> midi, rms; double hop = 0; };

PitchFrames yin (const std::vector<float>& y, int hop, const AnalyzeOptions& o)
{
    const int W = 1024, tauMin = (int) (kSR / 1100.0), tauMax = (int) (kSR / 70.0), N = 4096;
    PitchFrames P;
    P.hop = hop / kSR;
    const long n = (long) y.size();
    const int frames = (int) (n / hop) + 1;
    P.midi.assign ((size_t) frames, kNaN);
    P.rms.assign ((size_t) frames, 0.0);
    std::vector<double> prefix ((size_t) n + 1, 0.0);
    for (long i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) y[(size_t) i] * y[(size_t) i];
    auto energy = [&] (long a, long len) {
        long s = std::clamp (a, 0L, n), e = std::clamp (a + len, 0L, n);
        return prefix[(size_t) e] - prefix[(size_t) s];
    };
    std::vector<float> ar (N), ai (N), br (N), bi (N);
    std::vector<double> d ((size_t) tauMax + 2), cmnd ((size_t) tauMax + 2);
    for (int f = 0; f < frames; ++f)
    {
        if ((f & 511) == 0)
        {
            if (o.cancel && o.cancel->load()) return P;
            if (o.progress) o.progress (0.05f + 0.55f * (float) f / (float) frames);
        }
        const long start = (long) f * hop - W / 2;
        P.rms[(size_t) f] = std::sqrt (energy (start, W) / W);
        if (P.rms[(size_t) f] < 1e-4) continue;
        for (int i = 0; i < N; ++i)
        {
            long j = start + i;
            float v = (j >= 0 && j < n) ? y[(size_t) j] : 0.0f;
            ar[(size_t) i] = i < W ? v : 0.0f;
            br[(size_t) i] = i < W + tauMax ? v : 0.0f;
            ai[(size_t) i] = bi[(size_t) i] = 0.0f;
        }
        fft (ar, ai);
        fft (br, bi);
        // r(tau) = sum_j a_j b_{j+tau}  ->  IFFT(conj(A) * B); inverse via conj(FFT(conj(X)))/N
        for (int i = 0; i < N; ++i)
        {
            float re = ar[(size_t) i] * br[(size_t) i] + ai[(size_t) i] * bi[(size_t) i];
            float im = ar[(size_t) i] * bi[(size_t) i] - ai[(size_t) i] * br[(size_t) i];
            ar[(size_t) i] = re;
            ai[(size_t) i] = -im;
        }
        fft (ar, ai);
        const double e0 = energy (start, W);
        double running = 0;
        d[0] = 0;
        cmnd[0] = 1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double r = ar[(size_t) tau] / N;
            d[(size_t) tau] = std::max (0.0, e0 + energy (start + tau, W) - 2 * r);
            running += d[(size_t) tau];
            cmnd[(size_t) tau] = running > 0 ? d[(size_t) tau] * tau / running : 1;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[(size_t) tau] < 0.12)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;
            if (cmnd[(size_t) best] > 0.22) continue;  // no clear periodicity: breath, consonant or silence
        }
        double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        double den = a - 2 * b + c;
        double shift = den != 0 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        double f0 = kSR / (best + shift);
        P.midi[(size_t) f] = 69.0 + 12.0 * std::log2 (f0 / 440.0);
    }
    // silence gate relative to the loudest singing
    double rmax = *std::max_element (P.rms.begin(), P.rms.end());
    for (size_t i = 0; i < P.midi.size(); ++i)
        if (P.rms[i] < 0.03 * rmax) P.midi[i] = kNaN;
    // fix isolated octave jumps against a local median
    std::vector<double> fixed = P.midi;
    for (size_t i = 0; i < P.midi.size(); ++i)
    {
        if (std::isnan (P.midi[i])) continue;
        std::vector<double> win;
        for (long k = (long) i - 6; k <= (long) i + 6; ++k)
            if (k >= 0 && k < (long) P.midi.size() && ! std::isnan (P.midi[(size_t) k])) win.push_back (P.midi[(size_t) k]);
        double med = medianOf (win);
        if (std::abs (P.midi[i] - med) > 8) fixed[i] = P.midi[i] + 12.0 * std::round ((med - P.midi[i]) / 12.0);
    }
    // drop voiced islands shorter than 30 ms (clicks, consonants)
    const int minRun = std::max (2, (int) std::ceil (0.03 / P.hop));
    for (size_t i = 0; i < fixed.size();)
    {
        if (std::isnan (fixed[i])) { ++i; continue; }
        size_t j = i;
        while (j < fixed.size() && ! std::isnan (fixed[j])) ++j;
        if ((int) (j - i) < minRun) for (size_t k = i; k < j; ++k) fixed[k] = kNaN;
        i = j;
    }
    P.midi = fixed;
    return P;
}

struct ThaatInfo { const char* name; const char* western; };
const ThaatInfo kThaats[] = {
    { "bilawal", "Ionian (major)" }, { "khamaj", "Mixolydian" }, { "kafi", "Dorian" }, { "asavari", "Aeolian (natural minor)" },
    { "bhairavi", "Phrygian" }, { "kalyan", "Lydian" }, { "bhairav", "double harmonic" }, { "purvi", "Purvi (no Western twin)" },
    { "marwa", "Marwa (no Western twin)" }, { "todi", "Todi (no Western twin)" },
};

// ---------------------------------------------------------------------------
// Progression templates (Punjabi-first, then the global staples)
// ---------------------------------------------------------------------------

struct Template { const char* name; const char* roman; const char* style; const char* why; };
const Template kTemplates[] = {
    { "Punjabi sad (Kafi / Aeolian)", "i bVII bVI bVII", "punjabi_sad", "the classic heartbreak loop of Punjabi sad songs" },
    { "Modern Punjabi R&B", "i7 bVImaj7 bIIImaj7 bVII", "rnb_soul", "smooth 7th chords heard in modern Punjabi R&B" },
    { "Epic minor loop", "i bVI bIII bVII", "punjabi_sad", "big, cinematic minor loop used in Punjabi pop and film" },
    { "Heartbreak descent", "i bVII bVI V", "punjabi_sad", "falling bass line with a dramatic major V" },
    { "Folk minor bhangra", "i bVII i bVII", "punjabi_bhangra", "two-chord folk vamp that keeps the dhol driving" },
    { "Night-drive minor", "i iv bVII bIII", "punjabi_sad", "darker minor turn with a lift to the relative major" },
    { "Sufi qawwali drive", "i i bVII i", "punjabi_sad", "stays home on Sa like a qawwali, with one bVII step" },
    { "Bhairavi tension", "i bII i bII", "punjabi_sad", "komal re above Sa: devotional Bhairavi colour" },
    { "Cinematic Punjabi", "i bVI bVII i", "hollywood_epic", "rises through bVI and bVII back home, film-score style" },
    { "Lo-fi Punjabi (minor)", "i7 iv7 bVIImaj7 bIIImaj7", "lofi", "relaxed jazzy 7ths for lo-fi Punjabi edits" },
    { "Punjabi pop anthem", "I V vi IV", "bollywood_romantic", "the four-chord loop under countless Punjabi pop hits" },
    { "Emotional Punjabi pop", "vi IV I V", "bollywood_romantic", "starts on the minor vi for a tender, yearning feel" },
    { "Bhangra folk vamp (Khamaj)", "I bVII I bVII", "punjabi_bhangra", "Khamaj two-chord vamp straight from bhangra boliyan" },
    { "Dhol anthem", "I IV bVII I", "punjabi_bhangra", "big stadium bhangra turnaround" },
    { "Old-school Punjabi", "I vi IV V", "bollywood_romantic", "the classic 50s turn used in older Punjabi and Hindi songs" },
    { "Romantic lift", "I iii IV V", "bollywood_romantic", "gentle climb for romantic verses" },
    { "Lo-fi Punjabi (major)", "Imaj7 vi7 ii7 V7", "lofi", "soft 7th chords for slowed+reverb style versions" },
    { "Sufi drone", "I5 I5 bVII5 I5", "indian_classical", "open drone fifths that let the vocal and its ornaments breathe" },
};

double chordFit (const Chord& c, const std::array<double, 12>& w, const std::set<int>& scale)
{
    double total = 0, score = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (w[(size_t) pc] <= 0) continue;
        total += w[(size_t) pc];
        if (c.contains (pc)) score += w[(size_t) pc];
        else if (scale.count (pc)) score += 0.25 * w[(size_t) pc];
        else score -= 0.6 * w[(size_t) pc];
        // a semitone clash with a chord tone sounds worst of all
        for (int t : c.pitchClasses())
            if (! c.contains (pc) && (mod12 (pc - t) == 1 || mod12 (t - pc) == 1)) { score -= 0.15 * w[(size_t) pc]; break; }
    }
    if (total <= 0) return -2;  // no singing in this bar
    score /= total;
    score += 0.1 * w[(size_t) c.root] / total;
    return score;
}

} // namespace

// ---------------------------------------------------------------------------

int VocalReport::count (const std::string& t) const
{
    int n = 0;
    for (auto& o : ornaments) n += o.type == t;
    return n;
}

int VocalReport::bars() const
{
    double end = 0;
    for (auto& n : notes) end = std::max (end, n.end);
    return std::max (1, (int) std::ceil (beatOf (end) / 4.0 - 1e-6));
}

std::string swaraName (int s)
{
    static const char* const n[12] = { "Sa", "re", "Re", "ga", "Ga", "ma", "Ma", "Pa", "dha", "Dha", "ni", "Ni" };
    return n[mod12 (s)];
}

std::string formatTime (double seconds)
{
    int s = (int) std::lround (std::max (0.0, seconds));
    char b[16];
    std::snprintf (b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

VocalReport analyzeVocal (const float* mono, size_t numSamples, double sampleRate, const AnalyzeOptions& o)
{
    VocalReport R;
    if (numSamples == 0) return R;
    if (o.progress) o.progress (0.0f);
    auto y = resample (mono, numSamples, sampleRate, kSR);
    float peak = 0;
    for (float v : y) peak = std::max (peak, std::abs (v));
    if (peak > 0) for (auto& v : y) v /= peak;
    R.duration = y.size() / kSR;

    // 1. pitch track ---------------------------------------------------------
    const int hop = R.duration > 150 ? 256 : 128;
    auto P = yin (y, hop, o);
    if (o.cancel && o.cancel->load()) return R;
    const double dt = P.hop;
    R.hopSeconds = dt;
    const size_t F = P.midi.size();

    // tuning: how far the singer sits from A440 equal temperament
    std::vector<double> devs;
    for (double m : P.midi) if (! std::isnan (m)) devs.push_back (m - std::round (m));
    R.tuningCents = devs.empty() ? 0 : medianOf (devs) * 100;
    std::vector<double> m (F);
    for (size_t i = 0; i < F; ++i) m[i] = std::isnan (P.midi[i]) ? kNaN : P.midi[i] - R.tuningCents / 100;

    // 2. vibrato map: oscillation of the pitch around its local mean at 4-9 Hz -
    std::vector<double> mean (F, kNaN), resid (F, 0.0);
    const int halfAvg = std::max (2, (int) std::lround (0.09 / dt));  // ~one vibrato cycle, so the mean cancels it
    for (size_t i = 0; i < F; ++i)
    {
        if (std::isnan (m[i])) continue;
        double s = 0;
        int c = 0;
        for (long k = (long) i - halfAvg; k <= (long) i + halfAvg; ++k)
            if (k >= 0 && k < (long) F && ! std::isnan (m[(size_t) k])) { s += m[(size_t) k]; ++c; }
        mean[i] = s / c;
        resid[i] = m[i] - mean[i];
    }
    std::vector<char> vib (F, 0);
    std::vector<double> vibRate (F, 0), vibDepth (F, 0);
    const int halfWin = std::max (4, (int) std::lround (0.15 / dt));
    for (size_t i = 0; i < F; ++i)
    {
        if (std::isnan (m[i])) continue;
        int crossings = 0, c = 0;
        double sq = 0;
        bool ok = true;
        for (long k = (long) i - halfWin; k < (long) i + halfWin; ++k)
        {
            if (k < 0 || k + 1 >= (long) F || std::isnan (m[(size_t) k]) || std::isnan (m[(size_t) k + 1])) { ok = false; break; }
            crossings += (resid[(size_t) k] > 0) != (resid[(size_t) k + 1] > 0);
            sq += resid[(size_t) k] * resid[(size_t) k];
            ++c;
        }
        if (! ok || c == 0) continue;
        double rate = crossings / (2.0 * 2 * halfWin * dt);
        double depth = std::sqrt (sq / c);
        if (rate >= 4.0 && rate <= 9.0 && depth >= 0.12 && depth <= 0.9)
        {
            vib[i] = 1;
            vibRate[i] = rate;
            vibDepth[i] = depth * std::sqrt (2.0) * 100;  // peak cents
        }
    }
    std::vector<double> comp (F, kNaN);  // pitch used for note segmentation
    for (size_t i = 0; i < F; ++i)
    {
        if (std::isnan (m[i])) continue;
        if (vib[i]) { comp[i] = mean[i]; continue; }
        std::vector<double> w;
        for (long k = (long) i - 1; k <= (long) i + 1; ++k)
            if (k >= 0 && k < (long) F && ! std::isnan (m[(size_t) k])) w.push_back (m[(size_t) k]);
        comp[i] = medianOf (w);
    }
    R.pitchTrack.assign (F, NAN);
    for (size_t i = 0; i < F; ++i) R.pitchTrack[i] = (float) comp[i];
    if (o.progress) o.progress (0.62f);

    // 3. notes = stable regions; what lies between them inside one breath = glides
    struct Seg { size_t a, b; double pitch; bool vib; };
    std::vector<Seg> segs;
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < F;)
    {
        if (std::isnan (comp[i])) { ++i; continue; }
        size_t j = i;
        while (j + 1 < F && (! std::isnan (comp[j + 1]) || (j + 2 < F && ! std::isnan (comp[j + 2])))) ++j;
        runs.push_back ({ i, j });
        i = j + 1;
    }
    std::vector<int> segRun;
    for (size_t r = 0; r < runs.size(); ++r)
    {
        auto [a, b] = runs[r];
        size_t i = a;
        while (i <= b)
        {
            if (std::isnan (comp[i])) { ++i; continue; }
            size_t j = i;
            std::vector<double> vals { comp[i] };
            while (j + 1 <= b && ! std::isnan (comp[j + 1]))
            {
                std::vector<double> recent (vals.end() - (long) std::min<size_t> (10, vals.size()), vals.end());
                // compare with the recent pitch AND the whole note, so a slow meend can't drag the note along
                if (std::abs (comp[j + 1] - medianOf (recent)) > 0.5 || std::abs (comp[j + 1] - medianOf (vals)) > 0.6) break;
                vals.push_back (comp[++j]);
            }
            if ((double) (j - i + 1) * dt >= 0.055)
            {
                // a dip in loudness inside a steady pitch = the same note sung again
                std::vector<size_t> cuts { i };
                const int look = std::max (3, (int) std::lround (0.04 / dt));
                for (size_t k = i + (size_t) look; k + (size_t) look <= j; ++k)
                {
                    double before = 0, after = 0;
                    for (int q = 1; q <= look; ++q) { before = std::max (before, P.rms[k - (size_t) q]); after = std::max (after, P.rms[k + (size_t) q]); }
                    bool localMin = P.rms[k] <= P.rms[k - 1] && P.rms[k] <= P.rms[k + 1];
                    if (localMin && P.rms[k] < 0.6 * before && P.rms[k] < 0.6 * after && (double) (k - cuts.back()) * dt >= 0.1)
                        cuts.push_back (k);
                }
                cuts.push_back (j + 1);
                for (size_t c = 0; c + 1 < cuts.size(); ++c)
                {
                    size_t a2 = cuts[c], b2 = cuts[c + 1] - 1;
                    std::vector<double> pv;
                    int vibFrames = 0;
                    for (size_t k = a2; k <= b2; ++k) { if (! std::isnan (comp[k])) pv.push_back (comp[k]); vibFrames += vib[k]; }
                    if (pv.empty()) continue;
                    // singers start vibrato late in a held note: a quarter second of it (or a third of the note) counts
                    const bool hasVib = vibFrames * dt >= 0.25 || vibFrames > (int) (b2 - a2 + 1) / 3;
                    segs.push_back ({ a2, b2, medianOf (pv), hasVib });
                    segRun.push_back ((int) r);
                }
            }
            i = j + 1;
        }
    }
    // merge neighbours that round to the same note
    std::vector<Seg> merged;
    std::vector<int> mergedRun;
    for (size_t k = 0; k < segs.size(); ++k)
    {
        if (! merged.empty() && mergedRun.back() == segRun[k] && std::lround (merged.back().pitch) == std::lround (segs[k].pitch)
            && segs[k].a > merged.back().b + 1 && (double) (segs[k].a - merged.back().b) * dt < 0.05)
        {
            merged.back().b = segs[k].b;
            merged.back().vib = merged.back().vib || segs[k].vib;
            continue;
        }
        merged.push_back (segs[k]);
        mergedRun.push_back (segRun[k]);
    }
    // short notes that leap 7+ semitones away from both neighbours are tracker octave slips
    for (size_t k = 0; k < merged.size();)
    {
        double dur = (double) (merged[k].b - merged[k].a + 1) * dt;
        double prev = k > 0 ? merged[k - 1].pitch : kNaN, next = k + 1 < merged.size() ? merged[k + 1].pitch : kNaN;
        bool farPrev = std::isnan (prev) || std::abs (merged[k].pitch - prev) >= 7;
        bool farNext = std::isnan (next) || std::abs (merged[k].pitch - next) >= 7;
        if (dur < 0.15 && farPrev && farNext && ! (std::isnan (prev) && std::isnan (next)))
        {
            merged.erase (merged.begin() + (long) k);
            mergedRun.erase (mergedRun.begin() + (long) k);
            continue;
        }
        ++k;
    }
    for (size_t k = 0; k < merged.size(); ++k)
    {
        auto& s = merged[k];
        double loud = 0;
        for (size_t i = s.a; i <= s.b; ++i) loud += P.rms[i];
        loud /= (double) (s.b - s.a + 1);
        R.notes.push_back ({ s.a * dt, (s.b + 1) * dt, (int) std::lround (s.pitch), (int) std::lround (loud * 1e4) });
        R.bendFrom.push_back (0);
        R.ornamental.push_back (false);
        R.attack.push_back (s.a * dt);
        if (k > 0 && mergedRun[k] == mergedRun[k - 1])
        {
            double gap = (double) (s.a - merged[k - 1].b) * dt;
            double jump = s.pitch - merged[k - 1].pitch;
            if (gap >= 0.06 && std::abs (jump) >= 1.5)
            {
                R.ornaments.push_back ({ "meend", merged[k - 1].b * dt, s.a * dt, (int) std::lround (s.pitch), jump });
                R.bendFrom.back() = (int) std::clamp (-std::lround (jump), -4L, 4L);
                R.attack.back() = (merged[k - 1].b + 1) * dt;
            }
        }
    }
    // velocities from loudness, and dynamic range
    if (! R.notes.empty())
    {
        std::vector<double> db;
        for (auto& n : R.notes) db.push_back (20 * std::log10 (n.velocity / 1e4 + 1e-6));
        double lo = percentile (db, 0.05), hi = percentile (db, 0.95);
        R.dynamicRangeDb = hi - lo;
        for (size_t i = 0; i < R.notes.size(); ++i)
            R.notes[i].velocity = (int) std::clamp (55 + 60 * (db[i] - lo) / std::max (1e-6, hi - lo), 40.0, 120.0);
    }
    if (o.progress) o.progress (0.7f);

    // 4. murki & kan ----------------------------------------------------------
    const size_t N = R.notes.size();
    for (size_t i = 0; i + 2 < N;)
    {
        size_t j = i;
        while (j + 1 < N && R.notes[j].end - R.notes[j].start < 0.14 && R.notes[j + 1].start - R.notes[j].end < 0.05
               && std::abs (R.notes[j + 1].pitch - R.notes[j].pitch) <= 3)
            ++j;
        if (j - i + 1 >= 3 && R.notes[j].start - R.notes[i].start < 0.6)
        {
            R.ornaments.push_back ({ "murki", R.notes[i].start, R.notes[j].end, R.notes[std::min (j + 1, N - 1)].pitch, (double) (j - i + 1) });
            for (size_t k = i; k <= j; ++k) R.ornamental[k] = true;
            i = j + 1;
        }
        else
            ++i;
    }
    {
        // fast turns the note segmenter can't resolve: >= 3 pitch reversals of >= 0.8 semitone
        // within half a second (vibrato is smaller, meend doesn't reverse)
        std::vector<std::pair<size_t, double>> ext;  // (frame, pitch) of alternating turning points
        for (size_t i = 1; i + 1 < F; ++i)
        {
            if (std::isnan (m[i]) || std::isnan (m[i - 1]) || std::isnan (m[i + 1])) { if (std::isnan (m[i])) ext.push_back ({ i, kNaN }); continue; }
            bool peak = m[i] >= m[i - 1] && m[i] > m[i + 1], trough = m[i] <= m[i - 1] && m[i] < m[i + 1];
            if (! peak && ! trough) continue;
            if (! ext.empty() && ! std::isnan (ext.back().second) && std::abs (m[i] - ext.back().second) < 0.8)
            {
                // keep the more extreme of two same-direction points
                if ((peak && m[i] > ext.back().second) || (trough && m[i] < ext.back().second)) ext.back() = { i, m[i] };
                continue;
            }
            ext.push_back ({ i, m[i] });
        }
        for (size_t a = 0; a < ext.size();)
        {
            size_t b = a;
            while (b + 1 < ext.size() && ! std::isnan (ext[b].second) && ! std::isnan (ext[b + 1].second)
                   && (double) (ext[b + 1].first - ext[b].first) * dt < 0.16)
                ++b;
            if (b >= a + 3 && (double) (ext[b].first - ext[a].first) * dt < 0.6)
            {
                double ts = ext[a].first * dt, te = ext[b].first * dt;
                bool known = false;
                for (auto& orn : R.ornaments) known |= orn.type == "murki" && orn.start < te && orn.end > ts;
                if (! known)
                {
                    int land = (int) std::lround (ext[b].second);
                    for (auto& n : R.notes) if (n.start >= te - 0.05) { land = n.pitch; break; }
                    R.ornaments.push_back ({ "murki", ts, te, land, (double) (b - a + 1) });
                    for (size_t i = 0; i < N; ++i)
                        if (R.notes[i].start >= ts - 0.02 && R.notes[i].end <= te + 0.05 && R.notes[i].end - R.notes[i].start < 0.2) R.ornamental[i] = true;
                }
                a = b + 1;
            }
            else
                ++a;
        }
    }
    for (size_t i = 0; i + 1 < N; ++i)
    {
        auto& a = R.notes[i];
        auto& b = R.notes[i + 1];
        if (! R.ornamental[i] && a.end - a.start < 0.09 && b.start - a.end < 0.03 && b.end - b.start >= 0.2
            && a.pitch != b.pitch && std::abs (a.pitch - b.pitch) <= 4)
        {
            R.ornaments.push_back ({ "kan", a.start, b.start, b.pitch, (double) (a.pitch - b.pitch) });
            R.ornamental[i] = true;
        }
    }
    // vibrato statistics on held notes, pitch stability on plain held notes
    {
        int held = 0, vibNotes = 0;
        double rate = 0, depth = 0, devSum = 0;
        int devCount = 0;
        for (size_t k = 0; k < merged.size(); ++k)
        {
            auto& s = merged[k];
            double dur = (double) (s.b - s.a + 1) * dt;
            if (dur < 0.3) continue;
            ++held;
            if (s.vib)
            {
                ++vibNotes;
                double r = 0, d = 0;
                int c = 0;
                for (size_t i = s.a; i <= s.b; ++i) if (vib[i]) { r += vibRate[i]; d += vibDepth[i]; ++c; }
                if (c) { rate += r / c; depth += d / c; }
                R.ornaments.push_back ({ "vibrato", s.a * dt, (s.b + 1) * dt, (int) std::lround (s.pitch), c ? r / c : 0 });
            }
            else
            {
                std::vector<double> raw;
                for (size_t i = s.a; i <= s.b; ++i) if (! std::isnan (m[i])) raw.push_back (m[i]);
                double centre = medianOf (raw);
                for (double v : raw) { devSum += std::abs (v - centre); ++devCount; }
            }
        }
        R.vibratoShare = held ? (double) vibNotes / held : 0;
        R.vibratoRate = vibNotes ? rate / vibNotes : 0;
        R.vibratoDepthCents = vibNotes ? depth / vibNotes : 0;
        R.stabilityCents = devCount ? devSum / devCount * 100 : 0;
    }
    std::sort (R.ornaments.begin(), R.ornaments.end(), [] (auto& a, auto& b) { return a.start < b.start; });

    // 5. phrases & breathing -----------------------------------------------------
    if (N > 0)
    {
        size_t first = 0;
        for (size_t i = 1; i <= N; ++i)
            if (i == N || R.notes[i].start - R.notes[i - 1].end > 0.3)
            {
                VocalPhrase ph;
                ph.firstNote = (int) first;
                ph.lastNote = (int) i - 1;
                ph.start = R.notes[first].start;
                ph.end = R.notes[i - 1].end;
                ph.low = 127; ph.high = 0;
                size_t peakIdx = first;
                for (size_t k = first; k < i; ++k)
                {
                    ph.low = std::min (ph.low, R.notes[k].pitch);
                    if (R.notes[k].pitch > ph.high) { ph.high = R.notes[k].pitch; peakIdx = k; }
                }
                int a = R.notes[first].pitch, z = R.notes[i - 1].pitch;
                if (peakIdx != first && peakIdx != i - 1 && ph.high >= std::max (a, z) + 3) ph.contour = "arch";
                else if (z <= a - 2) ph.contour = "falling";
                else if (z >= a + 2) ph.contour = "rising";
                else ph.contour = "level";
                R.phrases.push_back (ph);
                first = i;
            }
        double phraseSum = 0, breathSum = 0;
        for (size_t p = 0; p < R.phrases.size(); ++p)
        {
            phraseSum += R.phrases[p].end - R.phrases[p].start;
            if (p) breathSum += R.phrases[p].start - R.phrases[p - 1].end;
        }
        R.avgPhraseSeconds = phraseSum / (double) R.phrases.size();
        R.avgBreathSeconds = R.phrases.size() > 1 ? breathSum / (double) (R.phrases.size() - 1) : 0;
        int mainNotes = 0;
        for (size_t i = 0; i < N; ++i) mainNotes += ! R.ornamental[i];
        R.notesPerSecond = phraseSum > 0 ? mainNotes / phraseSum : 0;
    }

    // 6. Sa (tonic) and thaat ---------------------------------------------------
    std::array<double, 12> hist {}, finals {};
    for (size_t i = 0; i < F; ++i) if (! std::isnan (comp[i])) hist[(size_t) mod12 ((int) std::lround (comp[i]))] += dt;
    for (auto& ph : R.phrases)
    {
        auto& last = R.notes[(size_t) ph.lastNote];
        finals[(size_t) mod12 (last.pitch)] += last.end - last.start + 0.3;
    }
    double hs = std::accumulate (hist.begin(), hist.end(), 0.0), fs = std::accumulate (finals.begin(), finals.end(), 0.0);
    double bestScore = -1e9, second = -1e9;
    for (int t = 0; t < 12; ++t)
        for (auto& th : kThaats)
        {
            std::array<double, 12> tmpl {};
            for (int d : std::vector<int> (scaleIntervals (th.name))) tmpl[(size_t) mod12 (t + d)] = 1;
            double a[12], b[12];
            for (int k = 0; k < 12; ++k) { a[k] = hist[(size_t) k] / std::max (1e-9, hs); b[k] = tmpl[(size_t) k]; }
            double ma = 1.0 / 12, mb = 7.0 / 12, num = 0, da = 0, db = 0;
            for (int k = 0; k < 12; ++k) { num += (a[k] - ma) * (b[k] - mb); da += (a[k] - ma) * (a[k] - ma); db += (b[k] - mb) * (b[k] - mb); }
            double fit = (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0;
            double score = fit + 0.35 * (fs > 0 ? finals[(size_t) t] / fs : 0) + 0.6 * (hist[(size_t) t] + 0.5 * hist[(size_t) mod12 (t + 7)]) / std::max (1e-9, hs);
            std::string nm = th.name;
            if (nm == "bilawal" || nm == "kafi" || nm == "khamaj" || nm == "asavari" || nm == "bhairavi") score += 0.03;
            if (score > bestScore) { second = bestScore; bestScore = score; R.key = { t, nm }; R.thaat = nm; R.westernMode = th.western; }
            else if (score > second) second = score;
        }
    R.keyConfidence = std::clamp ((bestScore - second) * 4 + 0.4, 0.0, 1.0);
    for (int k = 0; k < 12; ++k) R.swaraWeight[(size_t) k] = hist[(size_t) mod12 (R.key.tonic + k)] / std::max (1e-9, hs);

    // 7. range & voice type -----------------------------------------------------
    if (N > 0)
    {
        std::vector<double> pitches, weighted;
        for (auto& n : R.notes)
        {
            pitches.push_back (n.pitch);
            for (int k = 0; k < std::max (1, (int) ((n.end - n.start) / 0.05)); ++k) weighted.push_back (n.pitch);
        }
        R.lowest = (int) percentile (pitches, 0.0);
        R.highest = (int) percentile (pitches, 1.0);
        R.comfortLow = (int) percentile (weighted, 0.1);
        R.comfortHigh = (int) percentile (weighted, 0.9);
        double mid = percentile (weighted, 0.5);
        R.voiceType = mid < 50 ? "bass" : mid < 55 ? "baritone" : mid < 60 ? "tenor" : mid < 64 ? "alto / high tenor" : mid < 67 ? "mezzo-soprano" : "soprano";
    }
    if (o.progress) o.progress (0.78f);

    // 8. tempo & groove (beat tracker on the vocal itself, or the host tempo) -----
    {
        std::vector<double> onsets;
        for (size_t i = 0; i < N; ++i)
            if (! R.ornamental[i] && (i == 0 || R.notes[i].pitch != R.notes[i - 1].pitch || R.notes[i].start - R.notes[i - 1].end > 0.03))
                onsets.push_back (R.attack[i]);
        double bpm = o.fixedBpm, phase = 0;
        if (bpm <= 0 && onsets.size() >= 8)
        {
            // score every tempo/phase by how many note starts land on beats (and half-beats)
            double best = -1;
            for (double cand = 60; cand <= 180; cand += 0.5)
            {
                const double p = 60.0 / cand;
                const double prior = std::exp (-0.5 * std::pow (std::log2 (cand / 95.0) / 0.7, 2));
                for (int ph = 0; ph < 24; ++ph)
                {
                    const double off = p * ph / 24.0;
                    double sc = 0;
                    for (double t : onsets)
                    {
                        double x = (t - off) / p;
                        double db = std::abs (x - std::round (x)) * p, dh = std::abs (2 * x - std::round (2 * x)) * p / 2;
                        sc += std::exp (-std::pow (db / 0.035, 2)) + 0.4 * std::exp (-std::pow (dh / 0.035, 2));
                    }
                    sc = sc / (double) onsets.size() * prior;
                    if (sc > best) { best = sc; bpm = cand; phase = off; }
                }
            }
        }
        if (bpm <= 0)
        {
            AnalyzeOptions ao;
            ao.melody = false;
            auto A = analyze (mono, numSamples, sampleRate, ao);
            bpm = A.bpm;
            phase = A.beatTimes.empty() ? 0 : A.beatTimes.front();
        }
        R.bpm = bpm;
        const double p = 60.0 / bpm;
        phase = std::fmod (phase, p);
        for (double t = phase; t < R.duration; t += p) R.beatTimes.push_back (t);
    }
    const double spb = 60.0 / R.bpm;
    // bar 1 starts on the beat nearest the first sung note
    double t0 = R.beatTimes.empty() ? 0 : R.beatTimes.front();
    if (N && ! R.beatTimes.empty())
        for (double b : R.beatTimes) if (std::abs (b - R.notes.front().start) < std::abs (t0 - R.notes.front().start)) t0 = b;
    double firstNote = N ? R.notes.front().start : 0;
    R.originSeconds = t0;
    while (N && firstNote < R.originSeconds - 0.05) R.originSeconds -= 4 * spb;  // keep pickup notes inside bar 1
    {
        int offbeats = 0;
        double late = 0;
        for (auto& n : R.notes)
        {
            double b = (n.start - R.originSeconds) / spb;
            double frac = b - std::floor (b);
            if (frac > 0.4 && frac < 0.8) { ++offbeats; late += frac; }
        }
        double avg = offbeats ? late / offbeats : 0.5;
        R.swing = std::clamp ((avg - 0.5) / 0.17, 0.0, 1.0);  // 0 straight .. 1 full triplet swing
    }

    // 9. hook: the most repeated melodic shape --------------------------------
    {
        std::vector<size_t> idx;
        for (size_t i = 0; i < N; ++i) if (! R.ornamental[i]) idx.push_back (i);
        int bestLen = 0, bestCount = 1;
        size_t bestStart = 0;
        std::vector<size_t> bestPositions;
        for (int len = 6; len >= 3; --len)
        {
            std::map<std::vector<int>, std::vector<size_t>> seen;
            for (size_t s = 0; s + (size_t) len <= idx.size(); ++s)
            {
                std::vector<int> key;
                bool moves = false;
                for (int k = 1; k < len; ++k)
                {
                    int iv = R.notes[idx[s + (size_t) k]].pitch - R.notes[idx[s + (size_t) k - 1]].pitch;
                    key.push_back (iv);
                    moves |= iv != 0;
                }
                key.push_back (mod12 (R.notes[idx[s]].pitch - R.key.tonic));  // same swaras, not just same shape
                if (! moves) continue;
                auto& v = seen[key];
                if (v.empty() || R.notes[idx[s]].start >= R.notes[idx[v.back() + (size_t) len - 1]].end) v.push_back (s);
            }
            for (auto& [k, v] : seen)
                if ((int) v.size() >= 2 && (int) v.size() * len > bestCount * std::max (1, bestLen))
                {
                    bestLen = len; bestCount = (int) v.size(); bestStart = v[0]; bestPositions = v;
                }
        }
        if (bestLen > 0)
        {
            for (int k = 0; k < bestLen; ++k)
                R.hookText += (k ? " " : "") + swaraName (R.notes[idx[bestStart + (size_t) k]].pitch - R.key.tonic);
            for (auto p : bestPositions) R.hookTimes.push_back (R.notes[idx[p]].start);
        }
    }

    // 10. insights ------------------------------------------------------------
    const bool flats = R.key.prefersFlats();
    auto& I = R.insights;
    {
        std::string used, missing;
        int strong = 0;
        for (int k = 0; k < 12; ++k)
            if (R.swaraWeight[(size_t) k] > 0.025) { used += (used.empty() ? "" : " ") + swaraName (k); ++strong; }
        I.push_back ("Sa (tonic) = " + pcName (R.key.tonic, flats) + ".  Thaat: " + R.thaat + " (" + R.westernMode + ")"
                     + fmt (",  %.0f%% sure.", R.keyConfidence * 100));
        I.push_back ("Swaras sung: " + used + (strong <= 5 ? "  (pentatonic: folk / pahadi flavour)" : ""));
    }
    if (N > 0)
    {
        I.push_back ("Range " + midiToName (R.lowest, flats) + " - " + midiToName (R.highest, flats) + fmt (" (%.1f octaves); ", (R.highest - R.lowest) / 12.0)
                     + "most comfortable " + midiToName (R.comfortLow, flats) + " - " + midiToName (R.comfortHigh, flats) + ": " + R.voiceType + " range.");
        I.push_back (fmt ("Tempo %.0f BPM, ", R.bpm) + fmt ("%.1f notes per second, ", R.notesPerSecond)
                     + (R.swing > 0.5 ? "swung (bhangra / chaal feel)." : R.swing > 0.2 ? "lightly swung." : "straight rhythm."));
        I.push_back (std::to_string (R.phrases.size()) + fmt (" phrases, %.1f s long on average; ", R.avgPhraseSeconds)
                     + fmt ("breaths of %.1f s between them.", R.avgBreathSeconds));
        std::map<std::string, int> contours;
        for (auto& p : R.phrases) contours[p.contour]++;
        std::string dom = "level";
        int domN = 0;
        for (auto& [c, n] : contours) if (n > domN) { dom = c; domN = n; }
        I.push_back ("Phrases are mostly " + dom + (dom == "falling" ? " - they come home to rest, typical of sad and Sufi singing." : dom == "rising" ? " - they push upward, building energy." : dom == "arch" ? " - they climb to a peak and return, very singable." : " - they hover around one area, chant-like."));
        int meend = R.count ("meend"), murki = R.count ("murki"), kan = R.count ("kan");
        double perMin = 60.0 / std::max (1.0, R.duration);
        std::string level = (meend + murki + kan) * perMin > 12 ? "Heavy" : (meend + murki + kan) * perMin > 4 ? "Rich" : "Light";
        I.push_back (level + " ornamentation: " + std::to_string (meend) + " meend (glides), " + std::to_string (murki) + " murki, "
                     + std::to_string (kan) + " kan swar.");
        if (R.vibratoShare > 0)
            I.push_back (fmt ("Vibrato on %.0f%% of held notes", R.vibratoShare * 100) + fmt (" (%.1f Hz, +/-%.0f cents)", R.vibratoRate, R.vibratoDepthCents)
                         + (R.vibratoShare > 0.4 ? " - an expressive, classical-leaning voice." : "."));
        else
            I.push_back ("Almost no vibrato: a straight, modern pop tone.");
        I.push_back (fmt ("Pitch steadiness on held notes: +/-%.0f cents ", R.stabilityCents)
                     + (R.stabilityCents < 15 ? "(very accurate)." : R.stabilityCents < 30 ? "(good, natural)." : "(loose: tuning or Auto-Tune could help).")
                     + fmt ("  Tuning reference: %+.0f cents from A440.", R.tuningCents));
        if (! R.hookText.empty())
        {
            std::string when;
            for (size_t k = 0; k < R.hookTimes.size() && k < 4; ++k) when += (k ? ", " : "") + formatTime (R.hookTimes[k]);
            I.push_back ("Hook: \"" + R.hookText + "\" comes back " + std::to_string (R.hookTimes.size()) + " times (" + when + ").");
        }
        I.push_back (fmt ("Loudness range %.0f dB between soft and strong notes.", R.dynamicRangeDb));
    }
    else
        I.push_back ("No clear singing was found. Use a dry vocal (a cappella or an isolated vocal stem).");
    if (o.progress) o.progress (1.0f);
    return R;
}

// ---------------------------------------------------------------------------
// Chord progressions that fit the vocal
// ---------------------------------------------------------------------------

std::vector<ProgressionOption> suggestProgressions (const VocalReport& R, int count)
{
    std::vector<ProgressionOption> out;
    const int bars = R.bars();
    if (R.notes.empty()) return out;
    const double spb = 60.0 / R.bpm;
    std::vector<std::array<double, 12>> barW ((size_t) bars);
    for (size_t i = 0; i < R.notes.size(); ++i)
    {
        auto& n = R.notes[i];
        for (int b = 0; b < bars; ++b)
        {
            double bs = R.originSeconds + b * 4 * spb, be = bs + 4 * spb;
            double ov = std::min (be, n.end) - std::max (bs, n.start);
            if (ov <= 0) continue;
            double beat = (n.start - R.originSeconds) / spb;
            double w = ov;
            if (std::abs (beat - std::round (beat)) < 0.15) w *= 1.4;   // lands on a beat
            if (n.end - n.start >= 0.6 * spb) w *= 1.3;                  // held
            if (R.ornamental[i]) w *= 0.3;                               // decoration, not harmony
            barW[(size_t) b][(size_t) mod12 (n.pitch)] += w;
        }
    }
    const auto scale = R.key.pitchClasses();
    auto evaluate = [&] (const std::vector<Chord>& perBar) {
        double s = 0;
        int used = 0;
        for (int b = 0; b < bars; ++b)
        {
            double f = chordFit (perBar[(size_t) b], barW[(size_t) b], scale);
            if (f > -1.5) { s += f; ++used; }
        }
        return used ? s / used : 0.0;
    };
    auto toEvents = [&] (const std::vector<Chord>& perBar) {
        std::vector<ChordEvent> ev;
        for (int b = 0; b < bars; ++b) ev.push_back ({ b * 4.0, 4.0, perBar[(size_t) b] });
        return ev;
    };
    auto romanText = [&] (const std::vector<Chord>& cyc) {
        std::string t;
        for (auto& c : cyc) t += (t.empty() ? "" : " ") + romanName (c, R.key);
        return t;
    };
    auto pct = [] (double f) { return std::clamp ((f + 0.1) / 1.1, 0.0, 1.0); };

    // stock progressions, every rotation (so the loop can start where the vocal needs it)
    for (auto& T : kTemplates)
    {
        std::vector<Chord> cyc;
        try { cyc = parseProgression (T.roman, R.key); }
        catch (...) { continue; }
        bool clashesWithScale = false;
        for (auto& c : cyc)
            if (c.root == R.key.tonic)
                for (int pc : c.pitchClasses()) clashesWithScale |= ! scale.count (pc);
        if (clashesWithScale) continue;
        double best = -9;
        std::vector<Chord> bestBars, bestCycle;
        for (size_t rot = 0; rot < cyc.size(); ++rot)
        {
            std::vector<Chord> perBar;
            for (int b = 0; b < bars; ++b) perBar.push_back (cyc[(rot + (size_t) b) % cyc.size()]);
            double f = evaluate (perBar);
            if (f > best)
            {
                best = f;
                bestBars = perBar;
                bestCycle.assign (perBar.begin(), perBar.begin() + (long) std::min (cyc.size(), perBar.size()));
            }
        }
        ProgressionOption opt;
        opt.name = T.name;
        opt.style = T.style;
        opt.roman = romanText (bestCycle);
        opt.chords = toEvents (bestBars);
        opt.fit = pct (best);
        opt.description = std::string ("Supports ") + fmt ("%.0f%% of the sung notes; ", opt.fit * 100) + T.why + ".";
        out.push_back (opt);
    }

    // tailor-made: Viterbi over in-key and favourite borrowed chords, one per bar
    {
        std::vector<Chord> cand;
        auto& sc = R.key.scale();
        for (size_t d = 0; d < sc.size(); ++d)
        {
            int root = mod12 (R.key.tonic + sc[d]);
            int third = mod12 (sc[(d + 2) % sc.size()] - sc[d]), fifth = mod12 (sc[(d + 4) % sc.size()] - sc[d]);
            std::string q = third == 4 && fifth == 7 ? "maj" : third == 3 && fifth == 7 ? "min" : third == 3 && fifth == 6 ? "dim" : "";
            if (! q.empty() && q != "dim") cand.push_back ({ root, q, -1 });
        }
        for (auto* r : { "bVII", "bVI", "IV", "iv", "V", "bIII", "I5" })
        {
            auto c = parseRoman (r, R.key);
            if (std::find (cand.begin(), cand.end(), c) == cand.end()) cand.push_back (c);
        }
        const size_t C = cand.size();
        auto trans = [&] (const Chord& a, const Chord& b) {
            int iv = mod12 (b.root - a.root);
            if (a == b) return -0.04;
            if (iv == 5 || iv == 7) return 0.06;          // fourths/fifths
            if (iv == 10 || iv == 2) return 0.05;         // whole steps (I <-> bVII)
            if (iv == 3 || iv == 4 || iv == 8 || iv == 9) return 0.02;
            return -0.05;
        };
        std::vector<std::vector<double>> score ((size_t) bars, std::vector<double> (C));
        std::vector<std::vector<int>> back ((size_t) bars, std::vector<int> (C, 0));
        for (int b = 0; b < bars; ++b)
            for (size_t c = 0; c < C; ++c)
            {
                double f = chordFit (cand[c], barW[(size_t) b], scale);
                if (f < -1.5) f = cand[c].root == R.key.tonic ? 0.3 : 0.0;  // silent bar: lean towards home
                if ((b == 0 || b == bars - 1) && cand[c].root == R.key.tonic) f += 0.08;
                if (b == 0) { score[0][c] = f; continue; }
                double best = -1e9;
                for (size_t p = 0; p < C; ++p)
                {
                    double v = score[(size_t) b - 1][p] + trans (cand[p], cand[c]);
                    if (v > best) { best = v; back[(size_t) b][c] = (int) p; }
                }
                score[(size_t) b][c] = best + f;
            }
        std::vector<Chord> perBar ((size_t) bars);
        size_t cur = (size_t) (std::max_element (score.back().begin(), score.back().end()) - score.back().begin());
        for (int b = bars - 1; b >= 0; --b)
        {
            perBar[(size_t) b] = cand[cur];
            cur = (size_t) back[(size_t) b][cur];
        }
        ProgressionOption opt;
        opt.name = "Tailor-made for this vocal";
        opt.style = R.key.isMinorish() ? "punjabi_sad" : "punjabi_bhangra";
        std::vector<Chord> firstBars (perBar.begin(), perBar.begin() + std::min<long> (8, bars));
        opt.roman = romanText (firstBars) + (bars > 8 ? " ..." : "");
        opt.chords = toEvents (perBar);
        opt.fit = pct (evaluate (perBar));
        opt.description = fmt ("Chosen bar by bar to support %.0f%% of the sung notes, ", opt.fit * 100)
                          + std::string ("with smooth chord moves and Sa under the first and last bars.");
        out.push_back (opt);
    }

    std::stable_sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.fit > b.fit; });
    // keep the list varied: the same chord loop only once, at most two per style
    std::vector<ProgressionOption> picked;
    std::map<std::string, int> perStyle;
    std::set<std::string> loops;
    for (auto& o : out)
    {
        if ((int) picked.size() >= count) break;
        if (loops.count (o.roman) || perStyle[o.style] >= 2) continue;
        loops.insert (o.roman);
        perStyle[o.style]++;
        picked.push_back (o);
    }
    return picked;
}

Score arrangeForVocal (const VocalReport& R, const ProgressionOption& opt, VocalMelodyIdea idea, uint32_t seed, const std::string& styleOverride)
{
    GenerateOptions g;
    g.style = styleOverride.empty() ? opt.style : styleOverride;
    g.key = R.key;
    g.bpm = R.bpm;
    g.seed = seed;
    g.timeline = opt.chords;
    Score s = generate (g);
    s.chords = opt.chords;

    Track vocal { "Vocal", "bansuri", {}, false, {} };
    for (size_t i = 0; i < R.notes.size(); ++i)
    {
        auto& n = R.notes[i];
        double b0 = R.beatOf (n.start), b1 = R.beatOf (n.end);
        if (b1 <= 0) continue;
        vocal.notes.push_back ({ std::max (0.0, b0), b1 - std::max (0.0, b0), n.pitch, n.velocity, i < R.bendFrom.size() ? (double) R.bendFrom[i] : 0.0 });
    }

    auto* mel = s.track ("Melody");
    if (mel)
    {
        if (idea == VocalMelodyIdea::Answers)
        {
            // call & response: the instrument only plays where the singer rests
            std::vector<Note> keep;
            for (auto& n : mel->notes)
            {
                bool clash = false;
                for (auto& v : vocal.notes)
                    if (n.start < v.end() + 0.5 && n.end() > v.start - 0.25) { clash = true; break; }
                if (! clash) keep.push_back (n);
            }
            mel->notes = keep;
        }
        else
        {
            // harmony line: a chord-aware diatonic third above (or a sixth below) every sung note
            const auto scale = R.key.pitchClasses();
            std::vector<Note> harm;
            for (auto& v : vocal.notes)
            {
                const ChordEvent* ev = chordAt (s.chords, v.start);
                int best = -1;
                double bestCost = 1e9;
                for (int p = v.pitch + 3; p <= v.pitch + 5; ++p)
                {
                    if (! scale.count (mod12 (p))) continue;
                    double cost = std::abs (p - (v.pitch + 4)) * 0.4;
                    if (ev && ev->chord && ! ev->chord->contains (p)) cost += 1.0;
                    if (cost < bestCost) { bestCost = cost; best = p; }
                }
                if (best < 0) best = v.pitch + 4;
                if (idea == VocalMelodyIdea::HarmonyBelow) best -= 12;
                harm.push_back ({ v.start, v.duration, best, std::max (40, v.velocity - 12), v.bendFrom });
            }
            mel->notes = harm;
        }
    }
    s.tracks.insert (s.tracks.begin(), vocal);
    s.lengthBeats = R.bars() * 4.0;
    return s;
}

} // namespace mv
