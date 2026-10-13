#pragma once

namespace gk
{
/** Vintage-sampler grit: sample-and-hold rate reduction + bit-depth
    reduction (Akai S950 / E-mu SP-1200 flavour). Both parameters are
    continuous, so they can be swept without clicks; at rate >= the processing
    rate and 24 bits it is transparent. */
class Grit
{
public:
    void reset() noexcept
    {
        phase = 1.0;
        held = 0.0f;
    }

    /** @param holdRatio  target rate / processing rate (0..1, 1 = no hold)
        @param bits       bit depth (continuous, e.g. 8 .. 24) */
    inline float process (float x, double holdRatio, float bits) noexcept
    {
        phase += holdRatio;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            const float steps = std::exp2 (bits - 1.0f);
            held = std::round (x * steps) / steps;
        }
        return held;
    }

private:
    double phase = 1.0;
    float held = 0.0f;
};

/** Peak envelope follower (attack / release one-poles). */
class EnvelopeFollower
{
public:
    void prepare (double rate, double attackSeconds, double releaseSeconds) noexcept
    {
        att = (float) std::exp (-1.0 / (attackSeconds * rate));
        rel = (float) std::exp (-1.0 / (releaseSeconds * rate));
    }
    void setRelease (double rate, double releaseSeconds) noexcept { rel = (float) std::exp (-1.0 / (releaseSeconds * rate)); }
    void reset() noexcept { env = 0.0f; }

    inline float process (float x) noexcept
    {
        const float a = std::abs (x);
        const float c = a > env ? att : rel;
        env = a + (env - a) * c;
        return env;
    }
    float get() const noexcept { return env; }

private:
    float att = 0.0f, rel = 0.0f, env = 0.0f;
};

/** Kick-driven "pump": the French Touch compression breath.

    The detector listens to the low end (< ~150 Hz) of the input, follows it
    with a fast attack and a tempo-related release, and normalises it against
    a slowly decaying peak, so the ducking depth does not depend on the input
    level. Returns a gain (<= 1) to apply to the signal. */
class Pump
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate = rate;
        lowpass = TptSvf::Coeffs::make (150.0, rate, 0.707);
        env.prepare (rate, 0.004, 0.18);
        peakRelease = (float) std::exp (-1.0 / (3.0 * rate));
        reset();
    }

    void reset() noexcept
    {
        detector.reset();
        env.reset();
        peak = 0.0f;
    }

    /** Release follows the groove: `beats` of a beat at `bpm`. */
    void setRelease (double bpm, double beats) noexcept
    {
        env.setRelease (sampleRate, juce::jlimit (0.06, 0.6, beats * 60.0 / juce::jmax (40.0, bpm)));
    }

    /** @param monoIn  detector input; @param depthDb  max ducking (>= 0). */
    inline float process (float monoIn, float depthDb) noexcept
    {
        const float low = (float) detector.processLowpass (monoIn, lowpass);
        const float e = env.process (low);
        peak = juce::jmax (e, peak * peakRelease);
        if (depthDb <= 0.0f || peak < 1.0e-5f)
            return 1.0f;
        const float r = e / peak;
        return dspmath::dbToGain (-depthDb * r * r);
    }

private:
    double sampleRate = 48000.0;
    TptSvf detector;
    TptSvf::Coeffs lowpass;
    EnvelopeFollower env;
    float peak = 0.0f, peakRelease = 0.9999f;
};
} // namespace gk
