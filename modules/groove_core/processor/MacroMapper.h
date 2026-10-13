#pragma once

namespace gk
{
/** Building blocks to map the single macro (0..1) onto DSP parameters.
    Every plugin of the suite composes its own mapping from these curves so
    the "feel" of the knob stays consistent across the suite. */
namespace macro
{
    /** Exponential sweep from `startValue` (amount 0) to `endValue` (amount 1),
        log-spaced, with a curve exponent on the amount (curve < 1 spends more
        of the travel near the end value, > 1 near the start value). */
    inline float logSweep (float startValue, float endValue, float amount, float curve = 1.0f) noexcept
    {
        const float t = std::pow (dspmath::clamp01 (amount), juce::jmax (0.05f, curve));
        return dspmath::logLerp (startValue, endValue, t);
    }

    /** Rises from 0 to 1 at `peakAt` (smoothstep), then backs off by
        `releaseDepth` towards amount 1. */
    inline float peakThenRelease (float amount, float peakAt, float releaseDepth) noexcept
    {
        if (amount < peakAt)
            return dspmath::smoothstep (0.0f, peakAt, amount);
        return 1.0f - releaseDepth * dspmath::smoothstep (peakAt, 1.0f, amount);
    }

    /** 0 below `start`, smoothstep to 1 at amount 1. */
    inline float lateOnset (float amount, float start) noexcept { return dspmath::smoothstep (start, 1.0f, amount); }

    /** Power curve scaled to a maximum: maxValue · amount^exponent. */
    inline float power (float amount, float maxValue, float exponent) noexcept
    {
        return maxValue * std::pow (dspmath::clamp01 (amount), exponent);
    }

    /** Dry/wet weight inside the transparency zone [0, zone]: linear 0..1. */
    inline float transparencyWeight (float amount, float zone) noexcept
    {
        return zone > 0.0f ? juce::jmin (1.0f, amount / zone) : (amount > 0.0f ? 1.0f : 0.0f);
    }
} // namespace macro
} // namespace gk
