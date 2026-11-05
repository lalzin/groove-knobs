#include "EarlyNet.h"
#include <algorithm>

namespace ballroom
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    // the allpasses' lengths (ms), left / right: different primes-ish, short
    constexpr double apMs[2][EarlyNet::maxAllpasses] { { 1.13, 1.71, 2.29, 3.07, 3.83, 4.61, 5.39, 6.17 },
                                                       { 1.27, 1.63, 2.41, 2.93, 3.97, 4.49, 5.51, 6.03 } };
    std::uint32_t nextRandom (std::uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return s;
    }
    double unit (std::uint32_t& s) { return (double) (nextRandom (s) >> 8) / 16777216.0; } // 0..1
} // namespace

void EarlyNet::Line::prepare (int maxSamples)
{
    int n = 1;
    while (n < maxSamples + 8)
        n <<= 1;
    buf.assign ((size_t) n, 0.0f);
    mask = n - 1;
    w = 0;
}

float EarlyNet::ModAllpass::process (float x, float g, double modSamples, double rateHz, double sampleRate)
{
    // the length wanders: a new random aim every 1 / rate s, half-cosine glide
    // (computed every 16 samples, straight lines between)
    if (--countdown <= 0)
    {
        countdown = 16;
        phase += 16.0 * rateHz / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            value = aim;
            aim = unit (seed) * 2.0 - 1.0;
        }
        step = (value + (0.5 - 0.5 * std::cos (pi * phase)) * (aim - value) - current) / 16.0;
    }
    current += step;
    const double wander = current;
    const float d = line.read (std::max (1.0, base + modSamples * wander));
    const float v = x + g * d;
    line.push (v);
    return d - g * v;
}

void EarlyNet::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int s = 0; s < 2; ++s)
    {
        lines[(size_t) s].prepare ((int) std::ceil (maxSizeMs * 0.001 * sr) + 4);
        for (int k = 0; k < maxAllpasses; ++k)
        {
            auto& ap = allpasses[(size_t) s][(size_t) k];
            ap.base = apMs[s][k] * 0.001 * sr;
            ap.line.prepare ((int) std::ceil (ap.base + 3.0 * maxModMs * 0.001 * sr) + 4);
            ap.seed = 0x51ed2701u + 977u * (std::uint32_t) (s * maxAllpasses + k);
        }
    }
    crossCoeff = 1.0 - std::exp (-1.0 / (0.03 * sr));
    configure (Mode::club, 40.0);
}

void EarlyNet::configure (Mode m, double sizeMs)
{
    shape = &modeShape (m);
    numTaps = std::clamp (shape->taps, 0, maxTaps);
    sizeMs = std::clamp (sizeMs, 2.0, maxSizeMs);
    for (int s = 0; s < 2; ++s)
    {
        // the pattern: positions spread over the size, jittered (fixed seed per
        // mode and side), gains falling with the position, random signs
        std::uint32_t seed = 0xba11u + 7919u * (std::uint32_t) ((int) m * 2 + s);
        std::array<double, maxTaps> pos {};
        for (int k = 0; k < numTaps; ++k)
            pos[(size_t) k] = std::clamp (((double) k + 0.5 + shape->tapJitter * (unit (seed) - 0.5)) / numTaps, 0.01, 1.0);
        std::sort (pos.begin(), pos.begin() + numTaps);
        double energy = 0.0;
        for (int k = 0; k < numTaps; ++k)
        {
            const double g = std::pow (1.0 - 0.9 * pos[(size_t) k], shape->tapShape) * ((nextRandom (seed) & 0x100u) ? 1.0 : -1.0);
            taps[(size_t) s][(size_t) k] = { pos[(size_t) k] * sizeMs * 0.001 * sr, (float) g, (float) g };
            energy += g * g;
        }
        const double norm = numTaps > 0 ? shape->earlyGain / std::sqrt (energy) : 0.0;
        for (int k = 0; k < numTaps; ++k)
        {
            taps[(size_t) s][(size_t) k].gainOwn *= (float) norm;
            taps[(size_t) s][(size_t) k].gainOther *= (float) norm;
        }
    }
    reset();
}

void EarlyNet::reset()
{
    for (auto& l : lines)
        std::fill (l.buf.begin(), l.buf.end(), 0.0f);
    for (auto& side : allpasses)
        for (auto& ap : side)
            std::fill (ap.line.buf.begin(), ap.line.buf.end(), 0.0f);
}

void EarlyNet::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const int numAp = std::clamp (shape->earlyAllpasses, 0, maxAllpasses);
    const float g = (float) (0.62 * std::clamp (target.diffusion, 0.0, 1.0));
    const double modSamples = std::clamp (target.modDepth, 0.0, 1.0) * shape->earlyModScale * maxModMs * 0.001 * sr;
    for (int i = 0; i < n; ++i)
    {
        cross += crossCoeff * (std::clamp (target.cross, 0.0, 1.0) - cross);
        float x[2] { inL[i], inR[i] };
        for (int s = 0; s < 2; ++s)
        {
            for (int k = 0; k < numAp; ++k)
                x[s] = allpasses[(size_t) s][(size_t) k].process (x[s], g, modSamples, target.modRateHz, sr);
            lines[(size_t) s].push (x[s]);
        }
        float out[2] { 0.0f, 0.0f };
        for (int s = 0; s < 2; ++s)
        {
            const auto& own = lines[(size_t) s];
            const auto& other = lines[(size_t) (1 - s)];
            for (int k = 0; k < numTaps; ++k)
            {
                const auto& t = taps[(size_t) s][(size_t) k];
                const int d = std::max (1, (int) t.delay);
                out[s] += (float) (1.0 - 0.5 * cross) * t.gainOwn * own.tap (d) + (float) (0.5 * cross) * t.gainOther * other.tap (d);
            }
        }
        outL[i] = out[0];
        outR[i] = out[1];
    }
}
} // namespace ballroom
