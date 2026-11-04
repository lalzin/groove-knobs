// HEAD SPIN step 1 renders and checks.
//   render <in-dir with guitar.wav (mono 48 kHz 24-bit)> <out-dir>
// Writes WAVs (guitar and a shaker going round, several speeds, the other
// direction, an oval, DISTANCE 0 / 1, a MIX sweep, a direction flip while
// playing) and prints the checks: the side of the sound over one turn
// (left -> front -> right for "left to right"), MIX 0 = the input, the mono
// sum, the largest jump when the direction flips.
#include "Spinner.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>

using namespace headspin;

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
            {
                const std::int32_t v = ((std::uint8_t) b[k] << 8 | (std::uint8_t) b[k + 1] << 16 | (std::uint8_t) b[k + 2] << 24);
                out.push_back ((float) v / 2147483648.0f);
            }
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

static std::vector<float> loop (const std::vector<float>& x, size_t n, float gain)
{
    std::vector<float> y (n);
    for (size_t i = 0; i < n; ++i)
        y[i] = gain * x[i % x.size()];
    return y;
}

/** A shaker: noise bursts on sixteenths at 115 BPM, accents on the off-beats. */
static std::vector<float> shaker (size_t n)
{
    std::vector<float> y (n);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    const double step = 60.0 / 115.0 / 4.0 * sr;
    float hp = 0.0f, prev = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const double pos = std::fmod ((double) i, step);
        const int k = (int) ((double) i / step) % 4;
        const float env = (float) std::exp (-pos / (0.035 * sr)) * (k == 2 ? 1.0f : 0.55f);
        const float w = u (rng);
        hp = 0.85f * (hp + w - prev); // high-pass: a bright shaker
        prev = w;
        y[i] = 0.5f * env * hp;
    }
    return y;
}

static Stereo run (const std::vector<float>& mono, Params p, const std::function<void (Params&, size_t)>& change = {})
{
    Spinner s;
    s.prepare (sr);
    s.setParams (p);
    Stereo out { std::vector<float> (mono.size()), std::vector<float> (mono.size()) };
    const int block = 256;
    for (size_t i = 0; i < mono.size(); i += block)
    {
        if (change)
        {
            change (p, i);
            s.setParams (p);
        }
        const int len = (int) std::min ((size_t) block, mono.size() - i);
        s.process (mono.data() + i, mono.data() + i, out.l.data() + i, out.r.data() + i, len);
    }
    return out;
}

static double energy (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i)
        e += (double) x[i] * x[i];
    return e;
}

int main (int argc, char** argv)
{
    const std::string in = argc > 1 ? argv[1] : ".", out = argc > 2 ? argv[2] : ".";
    const auto guitarRaw = readMono24 (in + "/guitar.wav");
    if (guitarRaw.empty())
    {
        std::printf ("missing %s/guitar.wav\n", in.c_str());
        return 1;
    }
    static const size_t n = (size_t) (16 * sr);
    const auto guitar = loop (guitarRaw, n, 0.8f);
    const auto shake = shaker (n);

    struct Job { const char* name; const std::vector<float>* src; Params p; };
    auto P = [] (float rate, bool l2r = true) { Params p; p.rateHz = rate; p.leftToRight = l2r; return p; };
    Params oval = P (0.5f); oval.depth = 0.35f;
    Params ovalRot = oval; ovalRot.rotationDeg = 45.0f;
    Params far = P (0.25f); far.distance = 1.0f; far.depth = 1.0f;
    Params near0 = P (0.25f); near0.distance = 0.0f;
    const Job jobs[] {
        { "guitar_slow_LtoR_0.25Hz", &guitar, P (0.25f) },
        { "guitar_slow_RtoL_0.25Hz", &guitar, P (0.25f, false) },
        { "guitar_1Hz", &guitar, P (1.0f) },
        { "guitar_headspin_4Hz", &guitar, P (4.0f) },
        { "guitar_headspin_8Hz", &guitar, P (8.0f) },
        { "guitar_oval_pingpong_0.5Hz", &guitar, oval },
        { "guitar_oval_rotated45_0.5Hz", &guitar, ovalRot },
        { "guitar_distance0_0.25Hz", &guitar, near0 },
        { "guitar_distance1_0.25Hz", &guitar, far },
        { "shaker_0.5Hz", &shake, P (0.5f) },
        { "shaker_2Hz", &shake, P (2.0f) },
    };
    for (const auto& j : jobs)
    {
        const auto o = run (*j.src, j.p);
        writeWav (out + "/headspin_step1_" + j.name + ".wav", o);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i)
            peak = std::fmax (peak, std::fmax (std::fabs (o.l[i]), std::fabs (o.r[i])));
        std::printf ("%-32s peak %.2f\n", j.name, peak);
    }
    // MIX sweep 0 -> 1 -> 0 at 0.5 Hz
    writeWav (out + "/headspin_step1_guitar_mix_sweep.wav",
              run (guitar, P (0.5f), [] (Params& p, size_t i) { const double t = (double) i / n; p.mix = (float) (1.0 - std::fabs (1.0 - 2.0 * t)); }));
    // direction flipped every 2 s at 0.5 Hz
    const auto flip = run (guitar, P (0.5f), [] (Params& p, size_t i) { p.leftToRight = (i / (2 * sr)) % 2 == 0; });
    writeWav (out + "/headspin_step1_guitar_direction_flips.wav", flip);

    // --- Checks -----------------------------------------------------------
    // 1. The side over one turn, a 1 kHz sine at 0.25 Hz: L - R balance in 8 steps
    std::vector<float> sine (n);
    for (size_t i = 0; i < n; ++i)
        sine[i] = 0.5f * (float) std::sin (2.0 * M_PI * 1000.0 * i / sr);
    for (bool l2r : { true, false })
    {
        Params p = P (0.25f, l2r);
        p.distance = 0.0f;
        p.startDeg = 270.0f; // start on the left
        const auto o = run (sine, p);
        std::printf ("%s, from the left, balance R - L (dB) every 1/8 turn:", l2r ? "LEFT TO RIGHT" : "RIGHT TO LEFT");
        const size_t eighth = (size_t) (4.0 * sr / 8);
        for (int k = 0; k < 8; ++k)
        {
            const size_t a = (size_t) k * eighth, b = a + eighth / 4; // the start of each eighth
            std::printf (" %+.1f", 10.0 * std::log10 (energy (o.r, a + 100, b) / energy (o.l, a + 100, b)));
        }
        std::printf ("\n");
    }
    // 1b. The path itself: azimuth every 1/8 turn (0 front, 90 right, 180 back, -90 left)
    for (bool l2r : { true, false })
    {
        Spinner s;
        s.prepare (sr);
        Params p = P (0.25f, l2r);
        p.startDeg = 270.0f;
        s.setParams (p);
        std::vector<float> z ((size_t) (4.0 * sr / 8)), oz (z.size());
        std::printf ("%s, azimuth every 1/8 turn:", l2r ? "LEFT TO RIGHT" : "RIGHT TO LEFT");
        for (int k = 0; k < 8; ++k)
        {
            std::printf (" %+.0f", s.position().azimuthDeg);
            s.process (z.data(), z.data(), oz.data(), oz.data(), (int) z.size());
        }
        std::printf ("\n");
    }
    // 2. MIX 0: the input, exactly
    {
        Params p = P (3.0f);
        p.mix = 0.0f;
        const auto o = run (guitar, p);
        double maxDiff = 0.0;
        for (size_t i = 0; i < n; ++i)
            maxDiff = std::fmax (maxDiff, std::fmax (std::fabs (o.l[i] - guitar[i]), std::fabs (o.r[i] - guitar[i])));
        std::printf ("MIX 0: max difference with the input %.3g\n", maxDiff);
    }
    // 3. Mono sum: the level of (L + R) / 2 against the stereo level, worst 50 ms window
    for (float rate : { 0.25f, 4.0f })
    {
        Params p = P (rate);
        const auto o = run (guitar, p);
        double worst = 0.0;
        const size_t w = (size_t) (0.05 * sr);
        for (size_t a = sr; a + w < n; a += w)
        {
            double es = 0.0, em = 0.0;
            for (size_t i = a; i < a + w; ++i)
            {
                es += 0.5 * ((double) o.l[i] * o.l[i] + (double) o.r[i] * o.r[i]);
                const double m = 0.5 * (o.l[i] + o.r[i]);
                em += m * m;
            }
            if (es > 1e-6)
                worst = std::fmin (worst, 10.0 * std::log10 (em / es));
        }
        std::printf ("mono sum at %.2f Hz: worst 50 ms window %.1f dB under the stereo level (pan law alone: -3 dB at the sides)\n", rate, worst);
    }
    // 4. Direction flips: the largest sample step, against the same render without flips
    {
        const auto steady = run (guitar, P (0.5f));
        float a = 0.0f, b = 0.0f;
        for (size_t i = 1; i < n; ++i)
        {
            a = std::fmax (a, std::fabs (flip.l[i] - flip.l[i - 1]));
            b = std::fmax (b, std::fabs (steady.l[i] - steady.l[i - 1]));
        }
        std::printf ("direction flips: largest step %.3f (steady: %.3f)\n", a, b);
    }
    return 0;
}
