#pragma once

namespace gk
{
/** ITU-R BS.1770 K-weighting pre-filter (high shelf + RLB high-pass), with
    coefficients derived for any sample rate. Used by the makeup calibration
    and the level tests — not in the plugin's audio path. */
class KWeighting
{
public:
    void prepare (double fs) noexcept
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (dspmath::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (dspmath::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        reset();
    }

    void reset() noexcept { z1 = {}; z2 = {}; }

    double process (double x) noexcept { return run (s2, z2, run (s1, z1, x)); }

    /** Mean-square of the K-weighted signal, summed over channels (BS.1770
        channel weights of 1 for L/R/mono), skipping `skipSamples`. */
    static double weightedPower (const juce::AudioBuffer<float>& buf, double fs, int skipSamples = 0)
    {
        double sum = 0.0;
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            KWeighting k;
            k.prepare (fs);
            const auto* d = buf.getReadPointer (ch);
            double acc = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double y = k.process (d[i]);
                if (i >= skipSamples)
                    acc += y * y;
            }
            sum += acc / juce::jmax (1, n - skipSamples);
        }
        return sum;
    }

    /** Loudness-like level in dB (−0.691 + 10·log10(power)), BS.1770 style
        but without gating. */
    static double levelDb (const juce::AudioBuffer<float>& buf, double fs, int skipSamples = 0)
    {
        return -0.691 + 10.0 * std::log10 (juce::jmax (1.0e-20, weightedPower (buf, fs, skipSamples)));
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    static double run (const Biquad& c, State& s, double x) noexcept
    {
        const double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }

    Biquad s1 {}, s2 {};
    State z1, z2;
};
} // namespace gk
