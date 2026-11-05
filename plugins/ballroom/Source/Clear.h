#pragma once
// BALLROOM — CLEAR: the tail makes room for the music.
//
// The wet is split into 5 bands (crossovers 200 Hz, 630 Hz, 2 kHz, 6.3 kHz),
// the dry (L + R) too, with the same filters. In each band, while the dry
// plays, the tail steps back there, by
//   CLEAR x (how far the dry is over the tail, in dB, + 6 dB)
// limited to CLEAR x 24 dB (the envelopes: energies over ~10 ms), falling in 8 ms and coming back over 250 ms (the tail
// blooms in the gaps, between the notes, the way an engineer rides it by
// hand, but per band: a bass note does not dull the cymbals' tail). The
// bands sum back exactly (each split is a low-pass and the rest: x - low).
// The two sides take the same gains (the stereo image does not move).
// CLEAR 0: the wet untouched (and nothing computed). No JUCE.

#include <algorithm>
#include <array>
#include <cmath>

namespace ballroom
{
class Clear
{
public:
    static constexpr int numBands = 5;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double fc[numBands - 1] { 200.0, 630.0, 2000.0, 6300.0 };
        for (int k = 0; k < numBands - 1; ++k)
        {
            const double g = std::tan (pi * std::min (fc[k], 0.45 * sr) / sr), q = std::sqrt (2.0); // (k = 1 / Q, Q 0.707)
            a1[k] = 1.0 / (1.0 + g * (g + q));
            a2[k] = g * a1[k];
            a3[k] = g * a2[k];
        }
        envCoeff = 1.0 - std::exp (-1.0 / (0.010 * sr));
        fallCoeff = 1.0 - std::exp (-1.0 / (0.008 * sr));
        riseCoeff = 1.0 - std::exp (-1.0 / (0.250 * sr));
        reset();
    }

    void reset()
    {
        for (auto& s : split)
            s = {};
        dryEnv = {};
        wetEnv = {};
        gain.fill (1.0);
    }

    /** wetL / wetR in place; dry: the input (mono sum) at the same time; amount 0..1. */
    void process (const float* dryL, const float* dryR, float* wetL, float* wetR, int n, double amount)
    {
        amount = std::clamp (amount, 0.0, 1.0);
        if (amount <= 1.0e-4 && settled)
            return;
        settled = false;
        bool allOne = true;
        for (int i = 0; i < n; ++i)
        {
            std::array<double, numBands> d {}, l {}, r {};
            bands (0, 0.5 * ((double) dryL[i] + dryR[i]), d);
            bands (1, wetL[i], l);
            bands (2, wetR[i], r);
            double outL = 0.0, outR = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                dryEnv[(size_t) b] += envCoeff * (d[(size_t) b] * d[(size_t) b] - dryEnv[(size_t) b]);
                const double w = 0.5 * (l[(size_t) b] * l[(size_t) b] + r[(size_t) b] * r[(size_t) b]);
                wetEnv[(size_t) b] += envCoeff * (w - wetEnv[(size_t) b]);
                const double overDb = 10.0 * std::log10 ((dryEnv[(size_t) b] + 1.0e-14) / (wetEnv[(size_t) b] + 1.0e-14));
                const double aim = std::pow (10.0, -amount * std::clamp (overDb + 6.0, 0.0, 24.0) / 20.0);
                auto& g = gain[(size_t) b];
                g += (aim < g ? fallCoeff : riseCoeff) * (aim - g);
                outL += g * l[(size_t) b];
                outR += g * r[(size_t) b];
                allOne = allOne && g > 0.999;
            }
            wetL[i] = (float) outL;
            wetR[i] = (float) outR;
        }
        settled = amount <= 1.0e-4 && allOne;
    }

    /** Tests and the editor: a band's gain now (1 = untouched). */
    double bandGain (int b) const noexcept { return gain[(size_t) std::clamp (b, 0, numBands - 1)]; }

private:
    static constexpr double pi = 3.14159265358979323846;
    struct Svf { double ic1 = 0.0, ic2 = 0.0; };

    /** x -> numBands bands summing to x: low-pass, the rest goes on. */
    void bands (int signal, double x, std::array<double, numBands>& out)
    {
        for (int k = 0; k < numBands - 1; ++k)
        {
            auto& s = split[(size_t) signal][(size_t) k];
            const double v3 = x - s.ic2;
            const double v1 = a1[k] * s.ic1 + a2[k] * v3;
            const double v2 = s.ic2 + a2[k] * s.ic1 + a3[k] * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;
            out[(size_t) k] = v2;
            x -= v2;
        }
        out[numBands - 1] = x;
    }

    double sr = 48000.0, envCoeff = 0.0, fallCoeff = 0.0, riseCoeff = 0.0;
    double a1[numBands - 1] {}, a2[numBands - 1] {}, a3[numBands - 1] {};
    std::array<std::array<Svf, numBands - 1>, 3> split {}; // the dry, the wet L, the wet R
    std::array<double, numBands> dryEnv {}, wetEnv {}, gain {};
    bool settled = false;
};
} // namespace ballroom
