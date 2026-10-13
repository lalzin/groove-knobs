#pragma once

namespace gk
{
/** N-stage first-order allpass phaser with feedback.

    Each stage is a TPT one-pole (stable under modulation) used in its allpass
    form ap = 2·lp − x. The phaser output is a crossfade between the input and
    the allpass chain: at mix 0.5 the notches are deepest. */
template <int NumStages = 4>
class AllpassPhaser
{
public:
    void reset() noexcept
    {
        for (auto& s : state)
            s = 0.0;
        lastOut = 0.0;
    }

    /** One-pole gain G = g / (1 + g) for an allpass break frequency (Hz). */
    static double gainForFrequency (double hz, double sampleRate) noexcept
    {
        const double g = std::tan (dspmath::pi * juce::jlimit (1.0, 0.45 * sampleRate, hz) / sampleRate);
        return g / (1.0 + g);
    }

    /** Set the allpass break frequency (Hz) for all stages. */
    void setFrequency (double hz, double sampleRate) noexcept { G = gainForFrequency (hz, sampleRate); }

    /** Set the one-pole gain directly (for per-sample interpolation). */
    void setGain (double newG) noexcept { G = newG; }

    void setFeedback (float fb) noexcept { feedback = juce::jlimit (-0.95f, 0.95f, fb); }

    inline float process (float x, float mix) noexcept
    {
        double v = (double) x + (double) feedback * lastOut;
        for (auto& s : state)
        {
            const double t = (v - s) * G;
            const double lp = t + s;
            s = lp + t;
            v = 2.0 * lp - v;
        }
        lastOut = v;
        return x + mix * ((float) v - x);
    }

private:
    std::array<double, NumStages> state {};
    double G = 0.5, lastOut = 0.0;
    float feedback = 0.0f;
};
} // namespace gk
