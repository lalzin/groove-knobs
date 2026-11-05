#include "LateFdn.h"
#include <algorithm>

namespace ballroom
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double longestLine48k = 5077.0; // (Space)
    // each loop's allpass, as a fraction of its line (all different: no common period)
    constexpr double loopFraction[8] { 0.103, 0.142, 0.116, 0.170, 0.127, 0.083, 0.158, 0.094 };
    double sizeFactor (const ModeShape& s, double size) { return s.sizeMin + (1.0 - s.sizeMin) * std::clamp (size, 0.0, 1.0); }

    /** The 8-point Hadamard transform, normalised (orthogonal: no energy change). */
    void hadamard8 (std::array<double, 8>& x)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const double a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b;
                    x[(size_t) (j + h)] = a - b;
                }
        for (auto& v : x)
            v *= 0.35355339059327373; // 1 / sqrt (8)
    }
} // namespace

double LateFdn::lineSeconds (Mode m, int line, double size)
{
    const auto& s = modeShape (m);
    return s.lineSamples48k[(size_t) line] / 48000.0 * sizeFactor (s, size);
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

double LateFdn::Wander::next (double rateHz, double sampleRate)
{
    // a new random target every 1 / rate seconds, reached along a half cosine;
    // the curve is computed every 16 samples and followed in straight lines
    // between (a cosine per sample per line was most of the CPU)
    if (--countdown <= 0)
    {
        countdown = 16;
        phase += 16.0 * rateHz / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            value = target;
            seed = seed * 1664525u + 1013904223u;
            target = (double) (seed >> 8) / 8388608.0 - 1.0;
        }
        const double aim = value + (0.5 - 0.5 * std::cos (pi * phase)) * (target - value);
        step = (aim - current) / 16.0;
    }
    current += step;
    return current;
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
    const int maxLen = (int) std::ceil (longestLine48k * sr / 48000.0 + 3.0 * maxModSeconds * sr) + 8;
    for (int i = 0; i < numLines; ++i)
    {
        lines[(size_t) i].prepare (maxLen);
        wander[(size_t) i].seed = 0x9e3779b9u * (std::uint32_t) (i + 1);
    }
    for (auto& ap : loopAllpasses)
        ap.buf.assign ((size_t) (std::ceil (maxLoopFraction * longestLine48k * sr / 48000.0) + 4), 0.0f);
    const double apMs[2][4] { { 4.77, 3.59, 12.73, 9.31 }, { 4.93, 3.71, 12.47, 9.73 } };
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 4; ++k)
        {
            auto& ap = diffusers[(size_t) s][(size_t) k];
            ap.len = (int) std::lround (apMs[s][k] * 0.001 * sr);
            ap.buf.assign ((size_t) ap.len, 0.0f);
        }
    const double a = std::exp (-1.0 / (0.05 * sr)); // parameter smoothing ~50 ms
    for (auto* s : { &sDecay, &sBass, &sHigh, &sDepth, &sCross, &sFreeze })
        s->a = a;
    configure (mode, 0.6);
}

void LateFdn::configure (Mode m, double size)
{
    mode = m;
    shape = &modeShape (m);
    for (int k = 0; k < numLines; ++k)
    {
        lenSamples[(size_t) k] = shape->lineSamples48k[(size_t) k] * sr / 48000.0 * sizeFactor (*shape, size);
        auto& ap = loopAllpasses[(size_t) k];
        ap.len = std::clamp ((int) std::lround (loopFraction[k] * lenSamples[(size_t) k]), 1, (int) ap.buf.size());
    }
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
    for (auto& ap : loopAllpasses)
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
    for (auto* s : { &sDecay, &sBass, &sHigh, &sDepth, &sCross, &sFreeze })
        s->init = false;
}

void LateFdn::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float apGain = (float) (shape->inputDiffusion * std::clamp (target.diffusion, 0.0, 1.0));
    const int numAp = std::clamp (shape->inputAllpasses, 0, 4);
    const float loopGain = (float) std::clamp (shape->loopDiffusion * std::clamp (target.diffusion, 0.0, 1.0), 0.0, 0.75);
    for (int i = 0; i < n; ++i)
    {
        const double decay = std::max (0.05, std::exp (sDecay.next (std::log (std::max (0.05, target.decaySeconds)))));
        const double bassMult = sBass.next (target.bassMult * shape->lowWarmth), highMult = sHigh.next (target.highMult);
        const double depth = sDepth.next (target.modDepth), cross = sCross.next (target.cross);
        const double freeze = sFreeze.next (target.freeze ? 1.0 : 0.0);

        // input: diffused, cut when frozen
        float dl = (float) ((1.0 - freeze) * inL[i]), dr = (float) ((1.0 - freeze) * inR[i]);
        for (int k = 0; k < numAp; ++k)
        {
            dl = diffusers[0][(size_t) k].process (dl, apGain);
            dr = diffusers[1][(size_t) k].process (dr, apGain);
        }
        const float injEven = (float) ((1.0 - 0.5 * cross) * dl + 0.5 * cross * dr);
        const float injOdd = (float) ((1.0 - 0.5 * cross) * dr + 0.5 * cross * dl);

        // control rate: each line's gains (RT60 per band; frozen: 1) and its shelves
        if (--controlCountdown <= 0)
        {
            controlCountdown = 32;
            for (int k = 0; k < numLines; ++k)
            {
                const double lSec = (lenSamples[(size_t) k] + loopAllpasses[(size_t) k].len) / sr; // the whole loop
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
        const double modSamples = (1.0 - freeze) * depth * shape->lateModScale * maxModSeconds * sr; // (frozen: still)
        for (int k = 0; k < numLines; ++k)
        {
            double d = lenSamples[(size_t) k] + modSamples * wander[(size_t) k].next (target.modRateHz, sr);
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
            x[(size_t) k] = loopAllpasses[(size_t) k].process ((float) v, loopGain);
            sum += x[(size_t) k];
        }
        // the feedback matrix (energy preserving): Householder x - (2 / N) sum, or Hadamard
        std::array<double, numLines> fb = x;
        if (shape->hadamard)
            hadamard8 (fb);
        else
            for (auto& v : fb)
                v -= 2.0 / numLines * sum;
        denormal = denormal * 1664525u + 1013904223u;
        const float tiny = (denormal & 1u) ? 1.0e-20f : -1.0e-20f;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < numLines; ++k)
        {
            const float sign = (k / 2) % 2 == 0 ? 1.0f : -1.0f;
            const float inj = (k % 2 == 0 ? injEven : injOdd) * sign;
            lines[(size_t) k].push ((float) fb[(size_t) k] + inj + tiny);
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
