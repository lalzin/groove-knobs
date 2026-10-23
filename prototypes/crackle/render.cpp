// CRACKLE step 1 renders: music (a funk break + Rhodes chords) through the
// chain at GRAIN 0 / 0.25 / 0.5 / 0.75 / 1, a 20 s GRAIN sweep, and a 440 Hz
// tone at WOW 1 (its pitch swing measured: spec < 3 %).
//   render <in-dir with drums.wav, keys.wav> <out-dir>
#include "CrackleChain.h"
#include "GrainMap.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace crackle;

struct Stereo { std::vector<float> l, r; };

static Stereo readWav (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> b ((std::istreambuf_iterator<char> (f)), {});
    Stereo s;
    size_t i = 12;
    int ch = 2, bits = 24;
    while (i + 8 <= b.size())
    {
        std::uint32_t n;
        std::memcpy (&n, &b[i + 4], 4);
        if (! std::memcmp (&b[i], "fmt ", 4)) { ch = *(std::uint16_t*) &b[i + 10]; bits = *(std::uint16_t*) &b[i + 22]; }
        if (! std::memcmp (&b[i], "data", 4))
        {
            const int B = bits / 8;
            for (size_t k = i + 8; k + (size_t) (B * ch) <= i + 8 + n && k + (size_t) (B * ch) <= b.size(); k += (size_t) (B * ch))
                for (int c = 0; c < 2; ++c)
                {
                    const size_t o = k + (size_t) (B * std::min (c, ch - 1));
                    const std::int32_t v = ((std::uint8_t) b[o] << 8 | (std::uint8_t) b[o + 1] << 16 | (std::uint8_t) b[o + 2] << 24);
                    (c ? s.r : s.l).push_back ((float) v / 2147483648.0f);
                }
        }
        i += 8 + n + (n & 1);
    }
    return s;
}

static void writeWav (const std::string& path, const Stereo& s, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    const std::uint32_t data = (std::uint32_t) s.l.size() * 6;
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (2); u32 ((std::uint32_t) sr); u32 ((std::uint32_t) sr * 6); u16 (6); u16 (24);
    f.write ("data", 4); u32 (data);
    for (size_t i = 0; i < s.l.size(); ++i)
        for (float v : { s.l[i], s.r[i] })
        {
            const auto q = (std::int32_t) std::lround (std::fmax (-1.0, std::fmin (1.0, v)) * 8388607.0);
            f.put ((char) (q & 0xff)); f.put ((char) ((q >> 8) & 0xff)); f.put ((char) ((q >> 16) & 0xff));
        }
}

static Amounts at (float g) { Knobs k; k.grain = g; return k.amounts(); }

int main (int argc, char** argv)
{
    const int sr = 48000;
    const std::string in = argc > 1 ? argv[1] : ".", out = argc > 2 ? argv[2] : ".";
    auto drums = readWav (in + "/drums.wav"), keys = readWav (in + "/keys.wav");
    if (drums.l.empty() || keys.l.empty()) { std::printf ("missing input\n"); return 1; }
    // the music: 12 s of the break with the chords looped on top
    Stereo music;
    const size_t n = (size_t) (12 * sr);
    for (size_t i = 0; i < n; ++i)
    {
        music.l.push_back (0.7f * drums.l[i % drums.l.size()] + 0.6f * keys.l[i % keys.l.size()]);
        music.r.push_back (0.7f * drums.r[i % drums.r.size()] + 0.6f * keys.r[i % keys.r.size()]);
    }
    for (float g : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Chain c;
        c.prepare (sr);
        Stereo s = music;
        // (the smoothers start from 0: let them settle on silence first)
        std::vector<float> zl (sr / 2, 0.0f), zr (sr / 2, 0.0f);
        c.process (zl.data(), zr.data(), (int) zl.size(), at (g));
        c.process (s.l.data(), s.r.data(), (int) n, at (g));
        float peak = 0.0f;
        double e = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            peak = std::fmax (peak, std::fmax (std::fabs (s.l[i]), std::fabs (s.r[i])));
            e += (double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i];
        }
        const double rmsDb = 10.0 * std::log10 (e / (2.0 * (double) n));
        char name[64];
        std::snprintf (name, sizeof name, "/crackle_step2b_grain%03d.wav", (int) std::lround (g * 100));
        writeWav (out + name, s, sr);
        std::printf ("GRAIN %.2f: band %.2f wow %.2f hiss %.2f sat %.2f vinyl %.2f dropout %.2f, peak %.2f, RMS %.1f dB, %d crackles, %d dropouts in 12 s\n", g, at (g).band, at (g).wow,
                     at (g).hiss, at (g).saturation, at (g).vinyl, at (g).dropout, peak, rmsDb, c.crackleCount, c.dropoutCount);
    }
    // each effect alone, its knob at 1 (GRAIN 0, every other knob at 0), on the music
    {
        const char* names[] { "band", "wow", "flutter", "hiss", "saturation", "vinyl", "dropout" };
        for (int e = 0; e < 7; ++e)
        {
            Knobs k;
            float* knob[] { &k.band, &k.wow, &k.flutter, &k.hiss, &k.saturation, &k.vinyl, &k.dropout };
            for (auto* p : knob)
                *p = 0.0f;
            *knob[e] = 1.0f;
            Chain c;
            c.prepare (sr);
            Stereo s = music;
            c.process (s.l.data(), s.r.data(), (int) n, k.amounts());
            writeWav (out + "/crackle_step2b_only_" + names[e] + ".wav", s, sr);
        }
    }
    // SATURATION alone at 0.7, each TYPE; then SPEAKER with FOCUS 300 Hz - 3 kHz
    for (float amount : { 0.3f, 1.0f })
        for (int t = 0; t < (int) SatType::count; ++t)
        {
            Knobs k;
            float* knob[] { &k.band, &k.wow, &k.flutter, &k.hiss, &k.vinyl, &k.dropout };
            for (auto* p : knob)
                *p = 0.0f;
            k.saturation = amount;
            auto a = k.amounts();
            a.satType = (SatType) t;
            Chain c;
            c.prepare (sr);
            Stereo s = music;
            c.process (s.l.data(), s.r.data(), (int) n, a);
            double e = 0.0;
            for (size_t i = 0; i < n; ++i)
                e += (double) s.l[i] * s.l[i];
            std::printf ("SATURATION %.1f, %s: RMS %.1f dB\n", amount, Saturator::name ((SatType) t), 10.0 * std::log10 (e / (double) n));
        }
    for (int t = 0; t <= (int) SatType::count; ++t)
    {
        Knobs k;
        float* knob[] { &k.band, &k.wow, &k.flutter, &k.hiss, &k.vinyl, &k.dropout };
        for (auto* p : knob)
            *p = 0.0f;
        k.saturation = 0.7f;
        auto a = k.amounts();
        const bool focus = t == (int) SatType::count;
        a.satType = focus ? SatType::speaker : (SatType) t;
        if (focus)
        {
            a.focusLow = 300.0f;
            a.focusHigh = 3000.0f;
        }
        Chain c;
        c.prepare (sr);
        Stereo s = music;
        c.process (s.l.data(), s.r.data(), (int) n, a);
        double e = 0.0;
        for (size_t i = 0; i < n; ++i)
            e += (double) s.l[i] * s.l[i];
        const std::string name = focus ? std::string ("speaker_focus300-3k") : std::string (Saturator::name ((SatType) t));
        std::printf ("SATURATION 0.7, %s: RMS %.1f dB\n", name.c_str(), 10.0 * std::log10 (e / (double) n));
        writeWav (out + "/crackle_sat_" + name + ".wav", s, sr);
    }
    // NOISE at GRAIN 0.75: 0 (no noise), 0.5 (as GRAIN sets it), 1 (+6 dB)
    for (float nz : { 0.0f, 0.5f, 1.0f })
    {
        Chain c;
        c.prepare (sr);
        Stereo s = music;
        auto a = at (0.75f);
        a.noise = nz;
        std::vector<float> zl (sr / 2, 0.0f), zr (sr / 2, 0.0f);
        c.process (zl.data(), zr.data(), (int) zl.size(), a);
        c.process (s.l.data(), s.r.data(), (int) n, a);
        char name[64];
        std::snprintf (name, sizeof name, "/crackle_step2b_noise%03d.wav", (int) std::lround (nz * 100));
        writeWav (out + name, s, sr);
    }
    // the sweep: GRAIN 0 -> 1 over 12 s, in 20 ms steps (the chain smooths)
    {
        Chain c;
        c.prepare (sr);
        Stereo s = music;
        const int step = sr / 50;
        for (size_t i = 0; i < n; i += (size_t) step)
        {
            const int len = (int) std::min ((size_t) step, n - i);
            c.process (s.l.data() + i, s.r.data() + i, len, at ((float) i / (float) n));
        }
        writeWav (out + "/crackle_step2b_sweep.wav", s, sr);
    }
    // a 440 Hz tone at WOW 1 (nothing else): the pitch swing, from the zero crossings
    {
        Chain c;
        c.prepare (sr);
        Stereo s;
        for (int i = 0; i < 8 * sr; ++i)
        {
            const float v = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / sr);
            s.l.push_back (v);
            s.r.push_back (v);
        }
        Amounts a;
        a.wow = 1.0f;
        c.process (s.l.data(), s.r.data(), (int) s.l.size(), a);
        double last = -1.0, lo = 1e9, hi = 0.0;
        for (int i = sr; i < 8 * sr; ++i)
            if (s.l[(size_t) i - 1] < 0.0f && s.l[(size_t) i] >= 0.0f)
            {
                const double t = i - 1 + s.l[(size_t) i - 1] / (s.l[(size_t) i - 1] - s.l[(size_t) i]);
                if (last > 0.0)
                {
                    const double hz = sr / (t - last);
                    lo = std::fmin (lo, hz);
                    hi = std::fmax (hi, hz);
                }
                last = t;
            }
        std::printf ("WOW 1 on 440 Hz: %.1f .. %.1f Hz (swing %.2f %% of 440, limit 3 %%)\n", lo, hi, 100.0 * (hi - lo) / 440.0);
        writeWav (out + "/crackle_step1_wow_440.wav", s, sr);
    }
    return 0;
}
