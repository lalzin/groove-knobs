// BALLROOM step 1: the late FDN alone — WAV renders and RT60 measurements.
//   render <in-dir with guitar.wav (mono 48 kHz 24-bit)> <out-dir>
// Build: c++ -std=c++20 -O2 -o render render.cpp LateFdn.cpp
//
// RT60: the impulse response, band-filtered (eight one-pole high-passes and
// eight low-passes), Schroeder's backward integration, a straight line fitted
// to the decay curve between -5 and -35 dB (T30), extrapolated to -60 dB.
#include "LateFdn.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <string>

using namespace ballroom;

struct Stereo { std::vector<float> l, r; };
static const int sr = 48000;

static std::vector<float> readMono24 (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> b ((std::istreambuf_iterator<char> (f)), {});
    std::vector<float> out;
    size_t i = 12;
    int ch = 1;
    while (i + 8 <= b.size())
    {
        std::uint32_t n;
        std::memcpy (&n, &b[i + 4], 4);
        if (! std::memcmp (&b[i], "fmt ", 4))
            ch = *(std::uint16_t*) &b[i + 10];
        if (! std::memcmp (&b[i], "data", 4))
            for (size_t k = i + 8; k + (size_t) (3 * ch) <= i + 8 + n && k + 3 <= b.size(); k += (size_t) (3 * ch))
                out.push_back ((float) (std::int32_t) ((std::uint8_t) b[k] << 8 | (std::uint8_t) b[k + 1] << 16 | (std::uint8_t) b[k + 2] << 24) / 2147483648.0f);
        i += 8 + n + (n & 1);
    }
    return out;
}

static void writeWav (const std::string& path, const Stereo& s)
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

static Stereo run (const std::vector<float>& l, const std::vector<float>& r, const LateParams& p, size_t length)
{
    LateFdn fdn;
    fdn.prepare (sr);
    fdn.setParams (p);
    Stereo out { std::vector<float> (length), std::vector<float> (length) };
    std::vector<float> il (length, 0.0f), ir (length, 0.0f);
    std::copy_n (l.begin(), std::min (length, l.size()), il.begin());
    std::copy_n (r.begin(), std::min (length, r.size()), ir.begin());
    for (size_t i = 0; i < length; i += 256)
    {
        const int n = (int) std::min ((size_t) 256, length - i);
        fdn.process (il.data() + i, ir.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

/** RT60 of a band of the response (lo..hi Hz), from its T30. */
static double rt60 (const Stereo& ir, double lo, double hi)
{
    std::vector<double> e (ir.l.size());
    const double aH = std::exp (-2.0 * M_PI * lo / sr), aL = 1.0 - std::exp (-2.0 * M_PI * hi / sr);
    for (int c = 0; c < 2; ++c)
    {
        const auto& x = c == 0 ? ir.l : ir.r;
        // 8 high-passes and 8 low-passes (48 dB / octave): a band decaying
        // fast is not hidden by the slower neighbours leaking in
        double h[8] {}, p[8] {}, lp[8] {};
        for (size_t i = 0; i < x.size(); ++i)
        {
            double v = x[i];
            for (int k = 0; k < 8; ++k)
            {
                h[k] = aH * (h[k] + v - p[k]);
                p[k] = v;
                v = h[k];
            }
            for (int k = 0; k < 8; ++k)
            {
                lp[k] += aL * (v - lp[k]);
                v = lp[k];
            }
            e[i] += v * v;
        }
    }
    // Schroeder: energy remaining after each sample
    std::vector<double> edc (e.size());
    double acc = 0.0;
    for (size_t i = e.size(); i-- > 0;)
        edc[i] = (acc += e[i]);
    const double total = edc[0];
    // straight line through the -5 .. -35 dB part (least squares)
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = (double) i / sr;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++count;
        }
    }
    if (count < 10)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx); // dB per second
    return -60.0 / slope;
}

int main (int argc, char** argv)
{
    const std::string in = argc > 1 ? argv[1] : ".", out = argc > 2 ? argv[2] : ".";
    std::vector<float> impulse (1, 1.0f), none;

    // 1. RT60: DECAY 0.5 / 2 / 8 s, mids (500 Hz - 4 kHz) within 15 %
    std::printf ("RT60, 500 Hz - 4 kHz (spec: within 15 %% of DECAY):\n");
    for (double decay : { 0.5, 2.0, 8.0 })
        for (double size : { 0.2, 0.6, 1.0 })
        {
            LateParams p;
            p.decaySeconds = decay;
            p.size = size;
            p.bassMult = 1.0;
            p.highMult = 1.0;
            const auto ir = run (impulse, impulse, p, (size_t) ((decay * 0.9 + 0.6) * sr));
            const double m = rt60 (ir, 500, 4000);
            std::printf ("  DECAY %4.1f s, SIZE %3.0f %%: %6.2f s (%+5.1f %%)\n", decay, size * 100, m, 100.0 * (m / decay - 1.0));
        }
    // 2. BASS MULT / HIGH MULT: the low (80 - 200 Hz) and high (9 - 14 kHz) bands follow within 20 %
    std::printf ("bands (DECAY 2 s, XOVERs 500 Hz / 5 kHz; spec: within 20 %%):\n");
    for (auto [bm, hm] : { std::pair { 1.2, 0.5 }, { 2.0, 0.3 }, { 0.5, 1.0 }, { 4.0, 0.1 } })
    {
        LateParams p;
        p.decaySeconds = 2.0;
        p.bassMult = bm;
        p.highMult = hm;
        const auto ir = run (impulse, impulse, p, (size_t) ((2.0 * std::max (1.0, bm) * 0.9 + 0.6) * sr));
        const double lo = rt60 (ir, 80, 200), mi = rt60 (ir, 700, 2500), hi = rt60 (ir, 9000, 14000);
        std::printf ("  BASS x%.2f HIGH x%.2f: low %.2f s (want %.2f, %+.0f %%), mid %.2f s, high %.2f s (want %.2f, %+.0f %%)\n", bm, hm, lo, 2.0 * bm,
                     100.0 * (lo / (2.0 * bm) - 1.0), mi, hi, 2.0 * hm, 100.0 * (hi / (2.0 * hm) - 1.0));
    }

    // 3. Renders: impulse responses, a guitar, snare hits
    for (double decay : { 0.5, 2.0, 8.0 })
    {
        LateParams p;
        p.decaySeconds = decay;
        char name[96];
        std::snprintf (name, sizeof name, "/ballroom_step1_ir_decay%.1fs.wav", decay);
        writeWav (out + name, run (impulse, impulse, p, (size_t) ((decay + 1.0) * sr)));
    }
    const auto guitar = readMono24 (in + "/guitar.wav");
    // snare-ish hits: noise bursts on 2 and 4 at 115 BPM, then silence for the tail
    std::vector<float> snare ((size_t) (8 * sr), 0.0f);
    std::mt19937 rng (2);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (int beat = 1; beat < 8; beat += 2)
    {
        const size_t at = (size_t) (beat * 60.0 / 115.0 * sr);
        for (size_t i = 0; i < (size_t) (0.12 * sr) && at + i < snare.size(); ++i)
            snare[at + i] = 0.6f * u (rng) * (float) std::exp (-(double) i / (0.03 * sr));
    }
    for (double decay : { 1.2, 4.0 })
    {
        LateParams p;
        p.decaySeconds = decay;
        const size_t len = (size_t) ((8.0 + decay) * sr);
        auto mixDown = [&] (const std::vector<float>& src, const Stereo& wet, double mix)
        {
            Stereo o { std::vector<float> (len), std::vector<float> (len) };
            const double dg = std::cos (0.5 * M_PI * mix), wg = std::sin (0.5 * M_PI * mix);
            for (size_t i = 0; i < len; ++i)
            {
                const float dry = i < src.size() ? src[i] : 0.0f;
                o.l[i] = (float) (dg * dry + wg * wet.l[i]);
                o.r[i] = (float) (dg * dry + wg * wet.r[i]);
            }
            return o;
        };
        char name[96];
        if (! guitar.empty())
        {
            std::vector<float> g ((size_t) (8 * sr));
            for (size_t i = 0; i < g.size(); ++i)
                g[i] = 0.7f * guitar[i % guitar.size()];
            const auto wet = run (g, g, p, len);
            std::snprintf (name, sizeof name, "/ballroom_step1_guitar_decay%.1fs_mix35.wav", decay);
            writeWav (out + name, mixDown (g, wet, 0.35));
        }
        const auto wet = run (snare, snare, p, len);
        std::snprintf (name, sizeof name, "/ballroom_step1_snare_decay%.1fs_mix40.wav", decay);
        writeWav (out + name, mixDown (snare, wet, 0.4));
    }
    // FREEZE: a guitar chord caught then held 6 s (the level must not grow)
    if (! guitar.empty())
    {
        LateFdn fdn;
        fdn.prepare (sr);
        LateParams p;
        p.decaySeconds = 3.0;
        const size_t len = (size_t) (10 * sr);
        Stereo o { std::vector<float> (len), std::vector<float> (len) };
        std::vector<float> g (len, 0.0f);
        for (size_t i = 0; i < (size_t) (2 * sr); ++i)
            g[i] = 0.7f * guitar[i % guitar.size()];
        double before = 0, after = 0;
        for (size_t i = 0; i < len; i += 256)
        {
            p.freeze = i >= (size_t) (2 * sr);
            fdn.setParams (p);
            fdn.process (g.data() + i, g.data() + i, o.l.data() + i, o.r.data() + i, 256);
        }
        for (size_t i = (size_t) (3 * sr); i < (size_t) (4 * sr); ++i) before += o.l[i] * o.l[i];
        for (size_t i = (size_t) (9 * sr); i < (size_t) (10 * sr); ++i) after += o.l[i] * o.l[i];
        std::printf ("FREEZE: level after 1 s %.1f dB, after 7 s %.1f dB\n", 10 * std::log10 (before / sr), 10 * std::log10 (after / sr));
        writeWav (out + "/ballroom_step1_guitar_freeze.wav", o);
    }
    return 0;
}
