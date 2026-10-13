#pragma once

namespace gk
{
/** Normalised asymmetric tanh waveshaper.

        y = (tanh (g·x + b) − tanh (b)) / g'      with  g' = g · (1 − tanh²(b))

    g' makes the small-signal slope exactly 1, so the drive amount only moves
    the point where the curve bends (no level change on quiet material) and
    y(0) = 0 (no static DC). The bias b adds even harmonics; the DC it creates
    on loud material is removed by a DcBlocker placed right after. */
class Saturator
{
public:
    void set (float gain, float bias) noexcept
    {
        g = juce::jmax (1.0e-4f, gain);
        b = bias;
        tb = std::tanh (b);
        invNorm = 1.0f / (g * (1.0f - tb * tb));
    }

    /** Cheaper update when only the gain moves (bias unchanged). */
    void setGain (float gain) noexcept
    {
        g = juce::jmax (1.0e-4f, gain);
        invNorm = 1.0f / (g * (1.0f - tb * tb));
    }

    inline float process (float x) const noexcept { return (std::tanh (g * x + b) - tb) * invNorm; }

private:
    float g = 1.0f, b = 0.0f, tb = 0.0f, invNorm = 1.0f;
};
} // namespace gk
