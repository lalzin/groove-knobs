// Renders SLAPPER step-1 demos (WAV, 48 kHz, 24-bit mono) and checks the tuning.
#include "SlapperVoice.h"
#include "SlapperAmp.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace slapper;

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { f.write ((const char*) &v, 2); };
    const std::uint32_t data = (std::uint32_t) x.size() * 3;
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (1); u32 ((std::uint32_t) sr); u32 ((std::uint32_t) sr * 3); u16 (3); u16 (24);
    f.write ("data", 4); u32 (data);
    for (float v : x)
    {
        const auto s = (std::int32_t) std::lround (std::fmax (-1.0, std::fmin (1.0, v)) * 8388607.0);
        f.put ((char) (s & 0xff)); f.put ((char) ((s >> 8) & 0xff)); f.put ((char) ((s >> 16) & 0xff));
    }
}

// the period of a stretch by normalised autocorrelation around the expected one
static double measureHz (const std::vector<float>& x, int from, int len, double expectHz, int sr)
{
    const double T0 = sr / expectHz;
    double best = -1.0, bestLag = T0;
    std::vector<double> r;
    const int lo = (int) (T0 * 0.9), hi = (int) (T0 * 1.1) + 2;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[(size_t) (from + i)], b = x[(size_t) (from + i + lag)];
            xy += a * b; xx += a * a; yy += b * b;
        }
        r.push_back (xy / std::sqrt (xx * yy + 1e-30));
    }
    int k = 0;
    for (int i = 1; i + 1 < (int) r.size(); ++i)
        if (r[(size_t) i] > best) { best = r[(size_t) i]; k = i; }
    const double a = r[(size_t) k - 1], b = r[(size_t) k], c = r[(size_t) k + 1], den = a - 2 * b + c;
    bestLag = lo + k + (std::fabs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
    return sr / bestLag;
}

int main (int argc, char** argv)
{
    const int sr = 48000;
    const std::string out = argc > 1 ? argv[1] : ".";
    const double notes[] { 41.2, 55.0, 110.0, 220.0 };
    const char* names[] { "slap", "pop", "doigt" };
    // 1. each attack on E1, A1, A2, A3 (1.2 s each) + the tuning of 40 / 110 / 220 Hz
    for (int a = 0; a < 3; ++a)
    {
        Voice v;
        v.prepare (sr);
        std::vector<float> x;
        float peak = 0.0f;
        for (double hz : notes)
        {
            v.noteOn (hz, 0.85f, (Attack) a);
            for (int i = 0; i < (int) (1.2 * sr); ++i)
            {
                x.push_back (v.process());
                peak = std::fmax (peak, std::fabs (x.back()));
            }
        }
        // step 2: through the amp (the string's level into it fixed: 3x)
        Amp amp;
        amp.prepare (sr);
        std::vector<float> y (x.size());
        float peakAmp = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
        {
            y[i] = amp.process (3.0f * x[i]);
            peakAmp = std::fmax (peakAmp, std::fabs (y[i]));
        }
        auto subShare = [&] (const std::vector<float>& z)
        {
            // energy under ~100 Hz (two one-pole low-passes) against the whole, over the notes' first 300 ms
            double lo = 0.0, all = 0.0;
            float l1 = 0.0f, l2 = 0.0f;
            const float c = 1.0f - std::exp (-2.0f * 3.14159265f * 100.0f / sr);
            for (size_t i = 0; i < z.size(); ++i)
            {
                l1 += c * (z[i] - l1);
                l2 += c * (l1 - l2);
                const size_t note = i / (size_t) (1.2 * sr);
                if (note < 2 && (int) (i % (size_t) (1.2 * sr)) < (int) (0.3 * sr)) // (E1 and A1 only: above 100 Hz there is no sub to measure)
                {
                    lo += (double) l2 * l2;
                    all += (double) z[i] * z[i];
                }
            }
            return 100.0 * lo / (all + 1e-30);
        };
        std::printf ("%s: peak %.3f, sub share (<100 Hz, E1 / A1, first 300 ms): string %.0f %%, amp %.0f %%\n", names[a], peak, subShare (x), subShare (y));
        for (auto& s : x)
            s *= 0.7f / peak;
        writeWav (out + "/slapper_step1_" + names[a] + ".wav", x, sr);
        for (auto& s : y)
            s *= 0.8f / peakAmp;
        writeWav (out + "/slapper_step2_" + names[a] + ".wav", y, sr);
    }
    // tuning: 40, 110, 220 Hz, each attack, measured 0.3-0.8 s after the note
    double worst = 0.0;
    for (int a = 0; a < 3; ++a)
        for (double hz : { 40.0, 110.0, 220.0 })
        {
            Voice v;
            v.prepare (sr);
            v.noteOn (hz, 0.85f, (Attack) a);
            std::vector<float> x;
            for (int i = 0; i < sr; ++i)
                x.push_back (v.process());
            const double got = measureHz (x, (int) (0.3 * sr), (int) (0.4 * sr), hz, sr);
            const double cents = 1200.0 * std::log2 (got / hz);
            worst = std::fmax (worst, std::fabs (cents));
            std::printf ("  %s %6.1f Hz: %8.3f Hz (%+.2f cents)\n", names[a], hz, got, cents);
        }
    std::printf ("tuning: worst %.2f cents (limit 2)\n", worst);
    // 2. a groove: 100 BPM sixteenths, thumb on the root, pops on the octave
    {
        Voice v;
        v.prepare (sr);
        const double sixteenth = 60.0 / 100.0 / 4.0;
        struct Hit { int step; double hz; Attack a; float vel; };
        const Hit riff[] { { 0, 41.2, Attack::slap, 0.95f }, { 2, 82.4, Attack::pop, 0.8f }, { 3, 41.2, Attack::slap, 0.6f },
                           { 6, 82.4, Attack::pop, 0.85f }, { 8, 49.0, Attack::slap, 0.9f }, { 10, 98.0, Attack::pop, 0.75f },
                           { 11, 55.0, Attack::slap, 0.7f }, { 12, 55.0, Attack::finger, 0.6f }, { 14, 110.0, Attack::pop, 0.9f } };
        std::vector<float> x ((size_t) (sixteenth * 16 * 4 * sr), 0.0f);
        size_t pos = 0;
        float peak = 0.0f;
        for (int bar = 0; bar < 4; ++bar)
            for (const auto& h : riff)
            {
                const size_t at = (size_t) ((bar * 16 + h.step) * sixteenth * sr);
                while (pos < at)
                {
                    x[pos] = v.process();
                    peak = std::fmax (peak, std::fabs (x[pos]));
                    ++pos;
                }
                v.noteOn (h.hz, h.vel, h.a);
            }
        while (pos < x.size())
        {
            x[pos] = v.process();
            peak = std::fmax (peak, std::fabs (x[pos]));
            ++pos;
        }
        Amp amp;
        amp.prepare (sr);
        std::vector<float> y (x.size());
        float peakAmp = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
        {
            y[i] = amp.process (3.0f * x[i]);
            peakAmp = std::fmax (peakAmp, std::fabs (y[i]));
        }
        for (auto& s : x)
            s *= 0.7f / peak;
        writeWav (out + "/slapper_step1_groove.wav", x, sr);
        for (auto& s : y)
            s *= 0.8f / peakAmp;
        writeWav (out + "/slapper_step2_groove.wav", y, sr);
    }
    return worst <= 2.0 ? 0 : 1;
}
