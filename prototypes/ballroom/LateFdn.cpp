#include "LateFdn.h"
#include <algorithm>

namespace ballroom
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    // the eight lengths at LATE SIZE 100 %, in samples at 48 kHz: primes
    // (mutually prime), ~31 to ~74 ms, spread so no two are near a ratio
    constexpr double baseSamples48k[LateFdn::numLines] { 1493, 1823, 2083, 2389, 2707, 2939, 3259, 3539 };
    constexpr double maxModSeconds = 0.0012;
    double sizeFactor (double size) { return 0.25 + 0.75 * std::clamp (size, 0.0, 1.0); } // 25 % .. 100 % of the base
} // namespace

double LateFdn::lineSeconds (int line, double size)
{
    return baseSamples48k[line] / 48000.0 * sizeFactor (size);
}

void LateFdn::Delay::prepare (int maxSamples)
{
    int n = 1;
    while (n < maxSamples + 8)
        n <<= 1;
    buf.assign ((size_t) n, 0.0f);
    mask = n - 1;
    w = 0;
}

float LateFdn::Delay::read (double d) const
{
    const int k0 = std::max (0, (int) std::floor (d) - 1);
    const double t = d - k0;
    const double x0 = tap (k0), x1 = tap (k0 + 1), x2 = tap (k0 + 2), x3 = tap (k0 + 3);
    return (float) (-(t - 1) * (t - 2) * (t - 3) / 6.0 * x0 + t * (t - 2) * (t - 3) / 2.0 * x1 - t * (t - 1) * (t - 3) / 2.0 * x2
                    + t * (t - 1) * (t - 2) / 6.0 * x3);
}

double LateFdn::Wander::next (double rateHz, double sampleRate)
{
    // a new random target every 1 / rate seconds, reached along a half cosine
    phase += rateHz / sampleRate;
    if (phase >= 1.0)
    {
        phase -= std::floor (phase);
        value = target;
        seed = seed * 1664525u + 1013904223u;
        target = (double) (seed >> 8) / 8388608.0 - 1.0;
    }
    const double f = 0.5 - 0.5 * std::cos (pi * phase);
    return value + f * (target - value);
}

void LateFdn::Shelf::set (bool high, double fc, double gain, double sampleRate)
{
    // Simper's SVF shelves: A^2 = the gain beyond the corner
    const double A = std::sqrt (std::max (1.0e-6, gain)), k = std::sqrt (2.0);
    const double t = std::tan (pi * std::min (fc, 0.45 * sampleRate) / sampleRate);
    const double g = high ? t * std::sqrt (A) : t / std::sqrt (A);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    if (high)
    {
        m0 = A * A;
        m1 = k * (1.0 - A) * A;
        m2 = 1.0 - A * A;
    }
    else
    {
        m0 = 1.0;
        m1 = k * (A - 1.0);
        m2 = A * A - 1.0;
    }
}

void LateFdn::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxLen = (int) std::ceil (baseSamples48k[numLines - 1] * sr / 48000.0 + maxModSeconds * sr) + 8;
    for (int i = 0; i < numLines; ++i)
    {
        lines[(size_t) i].prepare (maxLen);
        wander[(size_t) i].seed = 0x9e3779b9u * (std::uint32_t) (i + 1);
    }
    const double apMs[2][4] { { 4.77, 3.59, 12.73, 9.31 }, { 4.93, 3.71, 12.47, 9.73 } };
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 4; ++k)
            diffusers[(size_t) s][(size_t) k].buf.assign ((size_t) std::lround (apMs[s][k] * 0.001 * sr), 0.0f);
    const double a = std::exp (-1.0 / (0.05 * sr)); // parameter smoothing ~50 ms
    for (auto* s : { &sSize, &sDecay, &sBass, &sHigh, &sDepth, &sCross, &sFreeze })
        s->a = a;
    sSize.a = std::exp (-1.0 / (0.25 * sr)); // the size moves slower (it bends the pitch while it moves)
    reset();
}

void LateFdn::reset()
{
    for (auto& l : lines)
        std::fill (l.buf.begin(), l.buf.end(), 0.0f);
    for (auto& side : diffusers)
        for (auto& ap : side)
        {
            std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
            ap.pos = 0;
        }
    for (auto* bank : { &lowShelf, &highShelf })
        for (auto& pair : *bank)
            for (auto& f : pair)
                f.ic1 = f.ic2 = 0.0;
    apState = {};
    controlCountdown = 0;
    for (auto* s : { &sSize, &sDecay, &sBass, &sHigh, &sDepth, &sCross, &sFreeze })
        s->init = false;
}

void LateFdn::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float apGain = (float) (0.72 * std::clamp (target.diffusion, 0.0, 1.0));
    for (int i = 0; i < n; ++i)
    {
        const double size = sSize.next (target.size);
        const double decay = std::max (0.05, std::exp (sDecay.next (std::log (std::max (0.05, target.decaySeconds)))));
        const double bassMult = sBass.next (target.bassMult), highMult = sHigh.next (target.highMult);
        const double depth = sDepth.next (target.modDepth), cross = sCross.next (target.cross);
        const double freeze = sFreeze.next (target.freeze ? 1.0 : 0.0);

        // input: diffused, cut when frozen
        float dl = (float) ((1.0 - freeze) * inL[i]), dr = (float) ((1.0 - freeze) * inR[i]);
        for (auto& ap : diffusers[0])
            dl = ap.process (dl, apGain);
        for (auto& ap : diffusers[1])
            dr = ap.process (dr, apGain);
        const float injEven = (float) ((1.0 - 0.5 * cross) * dl + 0.5 * cross * dr);
        const float injOdd = (float) ((1.0 - 0.5 * cross) * dr + 0.5 * cross * dl);

        const double sf = sizeFactor (size);
        // control rate: each line's gains (RT60 per band; frozen: 1) and its shelves
        if (--controlCountdown <= 0)
        {
            controlCountdown = 32;
            for (int k = 0; k < numLines; ++k)
            {
                const double lSec = baseSamples48k[k] / 48000.0 * sf;
                auto gainFor = [lSec, freeze] (double rt) { const double g = std::pow (10.0, -3.0 * lSec / rt); return g + freeze * (1.0 - g); };
                const double gm = gainFor (decay), gl = gainFor (decay * bassMult), gh = gainFor (decay * highMult);
                midGain[(size_t) k] = gm;
                for (auto& f : lowShelf[(size_t) k])
                    f.set (false, target.bassXoverHz, std::sqrt (gl / gm), sr);
                for (auto& f : highShelf[(size_t) k])
                    f.set (true, target.highXoverHz, std::sqrt (gh / gm), sr);
            }
        }

        // the lines' outputs, absorbed
        std::array<double, numLines> x {};
        double sum = 0.0;
        const double modSamples = (1.0 - freeze) * depth * maxModSeconds * sr; // (frozen: still)
        for (int k = 0; k < numLines; ++k)
        {
            const double len = baseSamples48k[k] * sr / 48000.0 * sf;
            double d = len + modSamples * wander[(size_t) k].next (target.modRateHz, sr);
            d += freeze * (std::round (d) - d); // frozen: whole samples (the interpolation would lose a little each pass)
            // allpass interpolation: M whole samples + a fraction in [0.5, 1.5)
            d = std::max (1.5, d);
            const int m = (int) std::floor (d - 0.5);
            const double frac = d - m, eta = (1.0 - frac) / (1.0 + frac);
            auto& line = lines[(size_t) k];
            const double y = eta * (line.tap (m) - apState[(size_t) k]) + line.tap (m + 1);
            apState[(size_t) k] = y;
            double v = midGain[(size_t) k] * y;
            for (auto& f : lowShelf[(size_t) k])
                v = f.process (v);
            for (auto& f : highShelf[(size_t) k])
                v = f.process (v);
            x[(size_t) k] = v;
            sum += x[(size_t) k];
        }
        // Householder feedback (energy preserving): x - (2 / N) sum
        const double s2 = 2.0 / numLines * sum;
        denormal = denormal * 1664525u + 1013904223u;
        const float tiny = (denormal & 1u) ? 1.0e-20f : -1.0e-20f;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < numLines; ++k)
        {
            const float sign = (k / 2) % 2 == 0 ? 1.0f : -1.0f;
            const float inj = (k % 2 == 0 ? injEven : injOdd) * sign;
            lines[(size_t) k].push ((float) (x[(size_t) k] - s2) + inj + tiny);
            if (k % 2 == 0)
                l += sign * (float) x[(size_t) k];
            else
                r += sign * (float) x[(size_t) k];
        }
        outL[i] = 0.5f * l;
        outR[i] = 0.5f * r;
    }
}
} // namespace ballroom
