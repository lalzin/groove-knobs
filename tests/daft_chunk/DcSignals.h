#pragma once
// DAFT CHUNK tests: synthetic drums (kick, snare, hat) and drum loops.
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace dctest
{
inline constexpr double sr = 44100.0;
inline constexpr double twoPi = 6.283185307179586;

struct Hit { double time; int kind; }; // 0 kick, 1 snare, 2 hat

/** A kick: a sine swept 150 -> 50 Hz, 180 ms decay. */
inline void addKick (std::vector<float>& x, int64_t at, float level)
{
    double ph = 0.0;
    for (int64_t i = 0; i < (int64_t) (0.35 * sr) && at + i < (int64_t) x.size(); ++i)
    {
        const double t = i / sr, f = 50.0 + 100.0 * std::exp (-t / 0.03);
        ph += twoPi * f / sr;
        x[(size_t) (at + i)] += level * (float) (std::sin (ph) * std::exp (-t / 0.09));
    }
}

/** A snare: band-limited noise + a 190 Hz body, 120 ms decay. */
inline void addSnare (std::vector<float>& x, int64_t at, float level, std::mt19937& rng)
{
    std::normal_distribution<float> g (0.0f, 1.0f);
    float lp = 0.0f, prevIn = 0.0f, hp = 0.0f;
    for (int64_t i = 0; i < (int64_t) (0.25 * sr) && at + i < (int64_t) x.size(); ++i)
    {
        const double t = i / sr;
        const float w = g (rng);
        hp = 0.85f * (hp + w - prevIn); // high-pass ~1 kHz
        prevIn = w;
        lp += 0.5f * (hp - lp);         // low-pass ~6 kHz
        const float body = (float) std::sin (twoPi * 190.0 * t) * (float) std::exp (-t / 0.05);
        x[(size_t) (at + i)] += level * (0.45f * lp * (float) std::exp (-t / 0.06) + 0.4f * body);
    }
}

/** A closed hat: high-passed noise, 25 ms decay. */
inline void addHat (std::vector<float>& x, int64_t at, float level, std::mt19937& rng)
{
    std::normal_distribution<float> g (0.0f, 1.0f);
    float prev = 0.0f, h1 = 0.0f, prev2 = 0.0f, h2 = 0.0f;
    for (int64_t i = 0; i < (int64_t) (0.08 * sr) && at + i < (int64_t) x.size(); ++i)
    {
        const float w = g (rng);
        h1 = 0.55f * (h1 + w - prev); // two high-passes ~ 7 kHz
        prev = w;
        h2 = 0.55f * (h2 + h1 - prev2);
        prev2 = h1;
        x[(size_t) (at + i)] += level * 0.35f * h2 * (float) std::exp (-(i / sr) / 0.025);
    }
}

/** A drum loop of `bars` bars at `bpm`, pattern `style` (0: four on the
    floor + snare 2 & 4 + offbeat hats; 1: kicks 1 and 3, 16th hats; 2: kicks
    1, 2-and, 3 + snare 2 & 4 + 8th hats), humanised by ±3 ms. */
inline std::vector<float> drumLoop (double bpm, int bars, int style, unsigned seed, std::vector<Hit>* hits = nullptr, double extraSeconds = 0.0)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> jitter (-0.003, 0.003);
    const double beat = 60.0 / bpm;
    const auto n = (int64_t) std::llround (bars * 4 * beat * sr + extraSeconds * sr);
    std::vector<float> x ((size_t) n, 0.0f);
    auto put = [&] (double time, int kind, float level)
    {
        const double t = std::max (0.0, time + (time > 0.0 ? jitter (rng) : 0.0));
        const auto at = (int64_t) std::llround (t * sr);
        if (kind == 0)
            addKick (x, at, level);
        else if (kind == 1)
            addSnare (x, at, level, rng);
        else
            addHat (x, at, level, rng);
        if (hits != nullptr)
            hits->push_back ({ t, kind });
    };
    for (int b = 0; b < bars; ++b)
        for (int q = 0; q < 4; ++q)
        {
            const double t = (b * 4 + q) * beat;
            const bool kick = style == 0 || q == 0 || q == 2;
            if (kick)
                put (t, 0, q == 0 ? 0.8f : 0.7f);
            if (style == 2 && q == 1)
                put (t + beat / 2, 0, 0.6f);
            if (q == 1 || q == 3)
                put (t, 1, style == 1 ? 0.6f : 0.7f);
            if (style == 0)
                put (t + beat / 2, 2, 0.5f);
            else if (style == 1)
                for (int s = 0; s < 4; ++s)
                    put (t + s * beat / 4, 2, s == 0 ? 0.35f : 0.25f);
            else
            {
                put (t, 2, 0.3f);
                put (t + beat / 2, 2, 0.3f);
            }
        }
    return x;
}

} // namespace dctest
