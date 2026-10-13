#pragma once

namespace gk
{
/** Deterministic test / calibration signals (offline tools and tests only). */
namespace testsignals
{
    /** Small deterministic PRNG (xorshift32) so renders are reproducible. */
    struct Rng
    {
        std::uint32_t s;
        explicit Rng (std::uint32_t seed = 0x1234567u) : s (seed ? seed : 1u) {}
        std::uint32_t next() noexcept
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return s;
        }
        /** Uniform in [-1, 1). */
        float bipolar() noexcept { return (float) ((double) next() / 2147483648.0 - 1.0); }
    };

    inline juce::AudioBuffer<float> whiteNoise (int numChannels, int numSamples, float peak, std::uint32_t seed = 1)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        Rng rng (seed);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                b.setSample (ch, i, peak * rng.bipolar());
        return b;
    }

    /** Pink noise (Paul Kellet's refined filter), scaled to an RMS level in dBFS. */
    inline juce::AudioBuffer<float> pinkNoise (int numChannels, int numSamples, float rmsDb, std::uint32_t seed = 7)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            Rng rng (seed + 101u * (std::uint32_t) ch);
            double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                const double w = rng.bipolar();
                b0 = 0.99886 * b0 + w * 0.0555179;
                b1 = 0.99332 * b1 + w * 0.0750759;
                b2 = 0.96900 * b2 + w * 0.1538520;
                b3 = 0.86650 * b3 + w * 0.3104856;
                b4 = 0.55000 * b4 + w * 0.5329522;
                b5 = -0.7616 * b5 - w * 0.0168980;
                const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
                b6 = w * 0.115926;
                b.setSample (ch, i, (float) pink);
            }
            const float rms = b.getRMSLevel (ch, 0, numSamples);
            b.applyGain (ch, 0, numSamples, rms > 0.0f ? dspmath::dbToGain (rmsDb) / rms : 0.0f);
        }
        return b;
    }

    inline juce::AudioBuffer<float> sine (int numChannels, int numSamples, double sampleRate, double hz, float peak)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = peak * (float) std::sin (2.0 * dspmath::pi * hz * i / sampleRate);
            for (int ch = 0; ch < numChannels; ++ch)
                b.setSample (ch, i, v);
        }
        return b;
    }

    /** Exponential sine sweep from f0 to f1. */
    inline juce::AudioBuffer<float> sineSweep (int numChannels, int numSamples, double sampleRate, double f0, double f1, float peak)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        const double T = numSamples / sampleRate, k = std::log (f1 / f0);
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / sampleRate;
            const double ph = 2.0 * dspmath::pi * f0 * T / k * (std::exp (t / T * k) - 1.0);
            for (int ch = 0; ch < numChannels; ++ch)
                b.setSample (ch, i, peak * (float) std::sin (ph));
        }
        return b;
    }

    inline juce::AudioBuffer<float> impulses (int numChannels, int numSamples, int period, float peak)
    {
        juce::AudioBuffer<float> b (numChannels, numSamples);
        b.clear();
        for (int i = period / 2; i < numSamples; i += period)
            for (int ch = 0; ch < numChannels; ++ch)
                b.setSample (ch, i, (i / period) % 2 ? -peak : peak);
        return b;
    }

    //==========================================================================
    /** Synthetic drum loop (kick on every beat, clap on 2 & 4, 8th/16th hats,
        open hat on the off-beats), optionally with a disco bass line and
        electric-piano chord stabs. Stereo, normalised to `peakDb`. */
    inline juce::AudioBuffer<float> grooveLoop (double sr, double bpm, int bars, bool withMusic, float peakDb = -3.0f)
    {
        const double beatSec = 60.0 / bpm;
        const int beats = bars * 4;
        const int n = (int) std::ceil (beats * beatSec * sr);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        Rng rng (2024);

        auto add = [&] (int ch, int i, float v)
        {
            if (i >= 0 && i < n)
                b.getWritePointer (ch)[i] += v;
        };

        auto kick = [&] (double t0)
        {
            const int s0 = (int) (t0 * sr);
            double phase = 0.0;
            for (int i = 0; i < (int) (0.45 * sr); ++i)
            {
                const double t = i / sr;
                const double f = 48.0 + 120.0 * std::exp (-t / 0.035);
                phase += 2.0 * dspmath::pi * f / sr;
                const float env = (float) (std::exp (-t / 0.22) * std::min (1.0, t / 0.001));
                const float click = (float) (std::exp (-t / 0.002) * 0.3);
                const float v = 0.9f * env * (float) std::sin (phase) + click * rng.bipolar() * 0.3f;
                add (0, s0 + i, v);
                add (1, s0 + i, v);
            }
        };

        auto clap = [&] (double t0)
        {
            const int s0 = (int) (t0 * sr);
            TptSvf bp;
            const auto c = TptSvf::Coeffs::make (1400.0, sr, 1.2);
            for (int i = 0; i < (int) (0.25 * sr); ++i)
            {
                const double t = i / sr;
                double env = std::exp (-t / 0.11) * 0.6;
                for (double burst : { 0.0, 0.011, 0.022 })
                    if (t >= burst)
                        env += std::exp (-(t - burst) / 0.004);
                const double hp = bp.processHighpass (rng.bipolar(), c);
                const float v = 0.50f * (float) (env * hp);
                add (0, s0 + i, v * 0.9f);
                add (1, s0 + i, v);
            }
        };

        auto hat = [&] (double t0, double decay, float level, float pan)
        {
            const int s0 = (int) (t0 * sr);
            TptSvf hp;
            const auto c = TptSvf::Coeffs::make (juce::jmin (7500.0, 0.45 * sr), sr, 0.7);
            for (int i = 0; i < (int) (5.0 * decay * sr); ++i)
            {
                const double t = i / sr;
                const float v = level * (float) (std::exp (-t / decay) * hp.processHighpass (rng.bipolar(), c));
                add (0, s0 + i, v * (1.0f - pan));
                add (1, s0 + i, v * (1.0f + pan));
            }
        };

        auto bassNote = [&] (double t0, double dur, double hz)
        {
            const int s0 = (int) (t0 * sr);
            TptSvf lp;
            double ph = 0.0;
            for (int i = 0; i < (int) (dur * sr); ++i)
            {
                const double t = i / sr;
                ph = dspmath::frac (ph + hz / sr);
                const double saw = 2.0 * ph - 1.0;
                const double cutoff = 300.0 + 1500.0 * std::exp (-t / 0.06);
                const double y = lp.processLowpass (saw, TptSvf::Coeffs::make (cutoff, sr, 1.1));
                const double env = std::min (1.0, t / 0.003) * std::min (1.0, (dur - t) / 0.01);
                const float v = 0.26f * (float) (env * y);
                add (0, s0 + i, v);
                add (1, s0 + i, v);
            }
        };

        auto chordStab = [&] (double t0, double dur, std::initializer_list<double> notes)
        {
            const int s0 = (int) (t0 * sr);
            for (int i = 0; i < (int) (dur * sr); ++i)
            {
                const double t = i / sr;
                const double env = std::exp (-t / 0.18) * std::min (1.0, t / 0.002) * std::min (1.0, (dur - t) / 0.02);
                double v = 0.0;
                int k = 0;
                for (double hz : notes)
                {
                    const double ph = 2.0 * dspmath::pi * hz * t;
                    v += std::sin (ph + 0.6 * std::exp (-t / 0.05) * std::sin (2.0 * ph)) * (k % 2 ? 0.9 : 1.0);
                    ++k;
                }
                const float s = 0.15f * (float) (env * v);
                add (0, s0 + i, s * 1.1f);
                add (1, s0 + i, s * 0.9f);
            }
        };

        for (int beat = 0; beat < beats; ++beat)
        {
            const double t = beat * beatSec;
            kick (t);
            if (beat % 2 == 1)
                clap (t);
            hat (t + 0.5 * beatSec, 0.09, 0.32f, 0.15f); // open-ish hat on the off-beat
            for (int s = 0; s < 4; ++s)
                if (s != 2)
                    hat (t + s * 0.25 * beatSec, 0.018, s == 0 ? 0.22f : 0.16f, -0.2f);

            if (withMusic)
            {
                // Octave disco bass: A1 / A2 on 8ths, moving to D and E.
                const int bar = beat / 4;
                const double root = (bar % 4 == 2) ? 73.42 : (bar % 4 == 3 ? 82.41 : 55.0);
                bassNote (t, 0.45 * beatSec, root);
                bassNote (t + 0.5 * beatSec, 0.45 * beatSec, root * 2.0);
                if (beat % 2 == 1)
                {
                    if (bar % 4 == 2)
                        chordStab (t + 0.5 * beatSec, 0.4 * beatSec, { 293.66, 349.23, 440.0, 523.25 });
                    else if (bar % 4 == 3)
                        chordStab (t + 0.5 * beatSec, 0.4 * beatSec, { 329.63, 415.30, 493.88, 587.33 });
                    else
                        chordStab (t + 0.5 * beatSec, 0.4 * beatSec, { 220.0, 261.63, 329.63, 392.0 });
                }
            }
        }

        const float peak = b.getMagnitude (0, n);
        if (peak > 0.0f)
            b.applyGain (dspmath::dbToGain (peakDb) / peak);
        return b;
    }
} // namespace testsignals
} // namespace gk
