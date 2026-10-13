#pragma once

namespace gk
{
/** Small, allocation-free math helpers shared by every Groove Knobs DSP. */
namespace dspmath
{
    constexpr double pi = 3.14159265358979323846;

    inline float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

    /** Classic cubic smoothstep, 0 below edge0, 1 above edge1. */
    inline float smoothstep (float edge0, float edge1, float x) noexcept
    {
        if (edge1 <= edge0)
            return x < edge0 ? 0.0f : 1.0f;
        const auto t = clamp01 ((x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g, float floorDb = -200.0f) noexcept
    {
        return g > 0.0f ? juce::jmax (floorDb, 20.0f * std::log10 (g)) : floorDb;
    }

    /** Log-domain interpolation: t = 0 -> a, t = 1 -> b (a, b > 0). */
    inline float logLerp (float a, float b, float t) noexcept { return a * std::pow (b / a, t); }

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline double frac (double x) noexcept { return x - std::floor (x); }
} // namespace dspmath
} // namespace gk
