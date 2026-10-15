#include "Theory.h"

#include "Styles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <regex>
#include <stdexcept>

namespace mv
{

const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kFlatNames[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

namespace
{
int basePc (char letter)
{
    switch (std::toupper ((unsigned char) letter))
    {
        case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
        case 'G': return 7; case 'A': return 9; case 'B': return 11;
        default: throw std::invalid_argument ("not a note letter");
    }
}

int mod12 (int x) { return ((x % 12) + 12) % 12; }

const std::map<std::string, std::vector<int>>& scaleMap()
{
    static const std::map<std::string, std::vector<int>> m = [] {
        std::map<std::string, std::vector<int>> out;
        for (auto& [name, ivs] : data::scales())
            out[name] = ivs;
        return out;
    }();
    return m;
}

struct QualityInfo { std::vector<int> intervals; std::string suffix; };

const std::map<std::string, QualityInfo>& qualityMap()
{
    static const std::map<std::string, QualityInfo> m = [] {
        std::map<std::string, QualityInfo> out;
        for (auto& [name, ivs, suffix] : data::chordQualities())
            out[name] = { ivs, suffix };
        return out;
    }();
    return m;
}
} // namespace

std::string toLower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::vector<std::string> splitWords (const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (std::isspace ((unsigned char) c) || c == '|' || c == ',')
        {
            if (! cur.empty()) out.push_back (cur);
            cur.clear();
        }
        else
            cur += c;
    }
    if (! cur.empty()) out.push_back (cur);
    return out;
}

int parsePitchClass (const std::string& name)
{
    if (name.empty())
        throw std::invalid_argument ("empty note name");
    int pc = basePc (name[0]);
    for (size_t i = 1; i < name.size(); ++i)
    {
        if (name[i] == '#') ++pc;
        else if (name[i] == 'b') --pc;
        else throw std::invalid_argument ("bad note name: " + name);
    }
    return mod12 (pc);
}

std::string pcName (int pc, bool flats) { return (flats ? kFlatNames : kNoteNames)[mod12 (pc)]; }

std::string midiToName (int midi, bool flats) { return pcName (midi, flats) + std::to_string (midi / 12 - 1); }

const std::vector<int>& scaleIntervals (const std::string& mode)
{
    auto it = scaleMap().find (mode);
    if (it == scaleMap().end())
        throw std::invalid_argument ("unknown mode: " + mode);
    return it->second;
}

bool hasScale (const std::string& mode) { return scaleMap().count (mode) > 0; }

std::vector<std::string> scaleNames()
{
    std::vector<std::string> out;
    for (auto& [name, ivs] : data::scales()) out.push_back (name);
    return out;
}

const std::vector<int>& chordIntervals (const std::string& quality)
{
    auto it = qualityMap().find (quality);
    if (it == qualityMap().end())
        throw std::invalid_argument ("unknown chord quality: " + quality);
    return it->second.intervals;
}

const std::string& chordSuffix (const std::string& quality)
{
    auto it = qualityMap().find (quality);
    if (it == qualityMap().end())
        throw std::invalid_argument ("unknown chord quality: " + quality);
    return it->second.suffix;
}

bool hasChordQuality (const std::string& quality) { return qualityMap().count (quality) > 0; }

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

Key Key::parse (const std::string& textIn)
{
    std::string text = textIn;
    std::replace (text.begin(), text.end(), '_', ' ');
    auto parts = splitWords (text);
    if (parts.empty())
        throw std::invalid_argument ("empty key");
    std::string head = parts[0];
    std::string rest;
    for (size_t i = 1; i < parts.size(); ++i)
        rest += (i > 1 ? "_" : "") + toLower (parts[i]);
    if (rest.empty())
    {
        if (head.size() > 1 && head.back() == 'm')
            return { parsePitchClass (head.substr (0, head.size() - 1)), "minor" };
        return { parsePitchClass (head), "major" };
    }
    if (rest == "maj") rest = "major";
    if (rest == "min" || rest == "m") rest = "minor";
    if (! hasScale (rest))
        throw std::invalid_argument ("unknown mode: " + rest);
    return { parsePitchClass (head), rest };
}

std::set<int> Key::pitchClasses() const
{
    std::set<int> out;
    for (int s : scale()) out.insert (mod12 (tonic + s));
    return out;
}

bool Key::isMinorish() const
{
    auto& s = scale();
    bool has3 = std::find (s.begin(), s.end(), 3) != s.end();
    bool has4 = std::find (s.begin(), s.end(), 4) != s.end();
    return has3 && ! has4;
}

bool Key::prefersFlats() const
{
    int rel = isMinorish() ? mod12 (tonic + 3) : tonic;
    return rel == 5 || rel == 10 || rel == 3 || rel == 8 || rel == 1;
}

std::string Key::name() const
{
    bool flats = prefersFlats() || tonic == 3 || tonic == 10 || tonic == 8;
    return pcName (tonic, flats) + " " + mode;
}

std::vector<int> Key::scalePitches (int low, int high) const
{
    auto pcs = pitchClasses();
    std::vector<int> out;
    for (int p = low; p <= high; ++p)
        if (pcs.count (mod12 (p))) out.push_back (p);
    return out;
}

// ---------------------------------------------------------------------------
// Chord
// ---------------------------------------------------------------------------

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> out;
    for (int i : intervals()) out.push_back (mod12 (root + i));
    return out;
}

bool Chord::contains (int pc) const
{
    for (int p : pitchClasses())
        if (p == mod12 (pc)) return true;
    return false;
}

bool Chord::isMinor() const
{
    static const std::set<std::string> minor { "min", "m7", "m6", "m9", "madd9", "mmaj7", "dim", "dim7", "m7b5" };
    return minor.count (quality) > 0;
}

std::string Chord::name (bool flats) const
{
    std::string n = pcName (root, flats) + chordSuffix (quality);
    if (bass >= 0 && bass != root)
        n += "/" + pcName (bass, flats);
    return n;
}

Chord Chord::parse (const std::string& textIn)
{
    std::string text = textIn;
    int bass = -1;
    auto slash = text.find ('/');
    if (slash != std::string::npos)
    {
        bass = parsePitchClass (text.substr (slash + 1));
        text = text.substr (0, slash);
    }
    if (text.empty() || text[0] < 'A' || text[0] > 'G')
        throw std::invalid_argument ("not a chord: " + textIn);
    size_t n = 1;
    if (text.size() > 1 && (text[1] == '#' || text[1] == 'b')) n = 2;
    int root = parsePitchClass (text.substr (0, n));
    std::string suffix = text.substr (n);
    std::string q;
    if (suffix == "M7") q = "maj7";
    else if (suffix == "M9") q = "maj9";
    else
    {
        std::string s = toLower (suffix);
        static const std::map<std::string, std::string> extra {
            { "", "maj" }, { "min", "min" }, { "maj", "maj" }, { "-", "min" }, { "m", "min" }, { "o", "dim" },
            { "+", "aug" }, { "min7", "m7" }, { "-7", "m7" }, { "mmaj7", "mmaj7" }
        };
        auto it = extra.find (s);
        if (it != extra.end()) q = it->second;
        else
            for (auto& [name, ivs, suf] : data::chordQualities())
                if (toLower (suf) == s) { q = name; break; }
    }
    if (q.empty())
        throw std::invalid_argument ("unknown chord quality in " + textIn);
    return { root, q, bass };
}

std::vector<int> Chord::voicing (int center, const std::vector<int>* previous) const
{
    std::vector<int> base;
    for (int i : intervals()) base.push_back (root + i);
    std::vector<std::vector<int>> candidates;
    for (size_t inv = 0; inv < base.size(); ++inv)
    {
        std::vector<int> notes;
        for (size_t k = 0; k < base.size(); ++k)
            notes.push_back (base[k] + (k < inv ? 12 : 0));
        std::sort (notes.begin(), notes.end());
        double mean = 0;
        for (int x : notes) mean += x;
        mean /= (double) notes.size();
        for (int octave = -1; octave <= 1; ++octave)
        {
            int shift = 12 * ((int) std::lround ((center - mean) / 12.0) + octave);
            std::vector<int> c;
            for (int x : notes) c.push_back (x + shift);
            candidates.push_back (c);
        }
    }
    auto meanOf = [] (const std::vector<int>& c) { double m = 0; for (int x : c) m += x; return m / (double) c.size(); };
    if (previous && ! previous->empty())
    {
        double prevMean = meanOf (*previous);
        auto cost = [&] (const std::vector<int>& c) {
            double movement = 0;
            for (int n : c)
            {
                int best = 1000;
                for (int p : *previous) best = std::min (best, std::abs (n - p));
                movement += best;
            }
            double m = meanOf (c);
            return movement + std::abs (m - center) * 0.5 + std::abs (m - prevMean) * 0.1;
        };
        return *std::min_element (candidates.begin(), candidates.end(),
                                  [&] (auto& a, auto& b) { return cost (a) < cost (b); });
    }
    return *std::min_element (candidates.begin(), candidates.end(), [&] (auto& a, auto& b) {
        double da = std::abs (meanOf (a) - center), db = std::abs (meanOf (b) - center);
        return da < db || (da == db && a[0] < b[0]);
    });
}

// ---------------------------------------------------------------------------
// Roman numerals
// ---------------------------------------------------------------------------

Chord parseRoman (const std::string& token, const Key& key)
{
    static const std::regex re (R"(^([b#]*)(VII|VI|IV|V|III|II|I|vii|vi|iv|v|iii|ii|i)([^/]*)(?:/(.+))?$)");
    std::smatch m;
    if (! std::regex_match (token, m, re))
        throw std::invalid_argument ("not a roman numeral: " + token);
    std::string acc = m[1], num = m[2], s = m[3];
    std::string upper = num;
    std::transform (upper.begin(), upper.end(), upper.begin(), ::toupper);
    static const std::map<std::string, int> degrees { { "I", 0 }, { "II", 2 }, { "III", 4 }, { "IV", 5 }, { "V", 7 }, { "VI", 9 }, { "VII", 11 } };
    int offset = degrees.at (upper);
    for (char c : acc) offset += c == '#' ? 1 : -1;
    int root = mod12 (key.tonic + offset);
    bool minor = std::islower ((unsigned char) num[0]);
    std::string q;
    if (s == "o" || s == "\xC2\xB0") q = "dim";
    else if (s == "o7" || s == "\xC2\xB0" "7") q = "dim7";
    else if (s == "\xC3\xB8" || s == "\xC3\xB8" "7" || s == "m7b5") q = "m7b5";
    else if (s == "+") q = "aug";
    else if (s == "7") q = minor ? "m7" : "7";
    else if (s == "maj7") q = minor ? "mmaj7" : "maj7";
    else if (s == "9") q = minor ? "m9" : "9";
    else if (s == "maj9") q = "maj9";
    else if (s == "add9") q = minor ? "madd9" : "add9";
    else if (s == "6") q = minor ? "m6" : "6";
    else if (s == "sus2" || s == "sus4" || s == "5" || s == "7b9") q = s;
    else if (s.empty()) q = minor ? "min" : "maj";
    else throw std::invalid_argument ("unknown roman suffix in " + token);
    int bass = -1;
    if (m[4].matched)
    {
        std::string b = m[4];
        if (! b.empty() && std::isdigit ((unsigned char) b[0]))
        {
            int idx = b == "3" ? 1 : b == "5" ? 2 : b == "7" ? 3 : -1;
            auto& ivs = chordIntervals (q);
            if (idx >= 0 && idx < (int) ivs.size()) bass = mod12 (root + ivs[(size_t) idx]);
        }
        else
            bass = parseRoman (b, key).root;
    }
    return { root, q, bass };
}

std::vector<Chord> parseProgression (const std::string& text, const Key& key)
{
    std::vector<Chord> out;
    for (auto& tok : splitWords (text))
    {
        if (tok == "-") continue;
        if (tok[0] >= 'A' && tok[0] <= 'G') out.push_back (Chord::parse (tok));
        else out.push_back (parseRoman (tok, key));
    }
    return out;
}

std::string romanName (const Chord& chord, const Key& key)
{
    static const char* const names[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
    std::string n = names[mod12 (chord.root - key.tonic)];
    std::string acc, num = n;
    if (n[0] == 'b' || n[0] == '#') { acc = n.substr (0, 1); num = n.substr (1); }
    if (chord.isMinor())
        std::transform (num.begin(), num.end(), num.begin(), ::tolower);
    static const std::map<std::string, std::string> suf {
        { "dim", "o" }, { "dim7", "o7" }, { "m7b5", "\xC3\xB8" "7" }, { "aug", "+" }, { "7", "7" }, { "m7", "7" },
        { "maj7", "maj7" }, { "9", "9" }, { "m9", "9" }, { "sus2", "sus2" }, { "sus4", "sus4" }, { "5", "5" }
    };
    auto it = suf.find (chord.quality);
    return acc + num + (it != suf.end() ? it->second : "");
}

} // namespace mv
