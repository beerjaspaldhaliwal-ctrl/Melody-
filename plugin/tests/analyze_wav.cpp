// Developer tool: run the C++ analyser on a WAV file and print the report.
//   analyze_wav song.wav [seconds]
#include "../core/Analysis.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static bool readWav (const char* path, std::vector<float>& mono, double& sr)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4)) return false;
    int channels = 0, bits = 0, format = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size())
    {
        uint32_t len;
        std::memcpy (&len, d.data() + pos + 4, 4);
        if (! std::memcmp (d.data() + pos, "fmt ", 4))
        {
            uint16_t fmt, ch, bps;
            uint32_t rate;
            std::memcpy (&fmt, d.data() + pos + 8, 2);
            std::memcpy (&ch, d.data() + pos + 10, 2);
            std::memcpy (&rate, d.data() + pos + 12, 4);
            std::memcpy (&bps, d.data() + pos + 22, 2);
            format = fmt; channels = ch; sr = rate; bits = bps;
        }
        else if (! std::memcmp (d.data() + pos, "data", 4) && channels > 0)
        {
            size_t frames = len / (size_t) (channels * bits / 8);
            mono.assign (frames, 0.0f);
            const char* p = d.data() + pos + 8;
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < channels; ++c)
                {
                    float v = 0;
                    if (bits == 16) { int16_t s; std::memcpy (&s, p, 2); v = s / 32768.0f; }
                    else if (bits == 32 && format == 3) std::memcpy (&v, p, 4);
                    else if (bits == 24) { int32_t s = (uint8_t) p[0] | ((uint8_t) p[1] << 8) | ((int8_t) p[2] << 16); v = s / 8388608.0f; }
                    mono[i] += v / (float) channels;
                    p += bits / 8;
                }
            return true;
        }
        pos += 8 + len + (len & 1);
    }
    return false;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: analyze_wav file.wav [seconds]\n"); return 2; }
    std::vector<float> mono;
    double sr = 44100;
    if (! readWav (argv[1], mono, sr)) { std::printf ("could not read %s\n", argv[1]); return 1; }
    if (argc > 2) mono.resize (std::min (mono.size(), (size_t) (std::stod (argv[2]) * sr)));
    auto a = mv::analyze (mono.data(), mono.size(), sr);
    std::printf ("Duration %.1fs  Tempo %.1f bpm  Key %s (%.2f)\n", a.duration, a.bpm, a.key.name().c_str(), a.keyConfidence);
    std::printf ("Chords   %s\n", a.chordChart().c_str());
    for (auto& f : a.famous()) std::printf ("Famous   %s (%s)\n", f.name.c_str(), f.roman.c_str());
    std::printf ("Melody   %zu notes:", a.melody.size());
    for (size_t i = 0; i < a.melody.size() && i < 32; ++i) std::printf (" %s", mv::midiToName (a.melody[i].pitch, a.key.prefersFlats()).c_str());
    std::printf ("\n");
    return 0;
}
