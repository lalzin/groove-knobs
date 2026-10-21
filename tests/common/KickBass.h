#pragma once

// Test signals for Kick Lock: a bass (sine sub + harmonics above the
// crossover), a 4-on-the-floor kick, and a runner feeding the sidechain bus.

#include <groove_core/groove_core.h>

namespace kickbass
{
using Buffer = juce::AudioBuffer<float>;

/** A bass note: the fundamental (the sub) and a few harmonics (the mids). */
struct Note { double start, length, hz; };

/** Renders notes (stereo, the same on both channels); `subOnly`: no harmonics. */
inline Buffer bass (const std::vector<Note>& notes, double seconds, double sr, bool subOnly = false, double fmDepth = 0.0, std::uint32_t seed = 1)
{
    Buffer b (2, (int) (seconds * sr));
    b.clear();
    juce::ignoreUnused (seed);
    for (const auto& n : notes)
    {
        const int s0 = (int) (n.start * sr), s1 = juce::jmin (b.getNumSamples(), (int) ((n.start + n.length) * sr));
        double phase = 0.0;
        for (int i = s0; i < s1; ++i)
        {
            const double t = (i - s0) / sr, rest = (s1 - i) / sr;
            const double env = juce::jmin (1.0, t / 0.005) * juce::jmin (1.0, rest / 0.01);
            // fmDepth: a wide frequency modulation (7.3 Hz), the sub never steady
            const double f = n.hz * (1.0 + fmDepth * std::sin (2.0 * juce::MathConstants<double>::pi * 7.3 * t));
            phase += f / sr;
            const double p = 2.0 * juce::MathConstants<double>::pi * phase;
            double y = 0.5 * std::sin (p);
            if (! subOnly)
                y += 0.18 * std::sin (3.0 * p) + 0.12 * std::sin (4.0 * p + 0.3) + 0.08 * std::sin (6.0 * p + 1.1);
            const float v = (float) (0.8 * env * y);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }
    return b;
}

/** Four-on-the-floor kicks: a 150 -> 50 Hz sine sweep with a 180 ms decay. */
inline Buffer kicks (double seconds, double sr, double bpm, double offsetSeconds = 0.0, double level = 0.8)
{
    Buffer k (2, (int) (seconds * sr));
    k.clear();
    const double beat = 60.0 / bpm;
    for (double at = offsetSeconds; at < seconds; at += beat)
    {
        if (at < 0.0)
            continue;
        const int s0 = (int) std::round (at * sr);
        double phase = 0.0;
        for (int i = s0; i < juce::jmin (k.getNumSamples(), s0 + (int) (0.3 * sr)); ++i)
        {
            const double t = (i - s0) / sr;
            const double f = 50.0 + 100.0 * std::exp (-t / 0.03);
            phase += f / sr;
            const float v = (float) (level * std::exp (-t / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * phase));
            k.addSample (0, i, v);
            k.addSample (1, i, v);
        }
    }
    return k;
}

/** Kick times (seconds) of kicks(). */
inline std::vector<double> kickTimes (double seconds, double bpm, double offsetSeconds = 0.0)
{
    std::vector<double> t;
    for (double at = offsetSeconds; at < seconds; at += 60.0 / bpm)
        if (at >= 0.0)
            t.push_back (at);
    return t;
}

/** Runs a processor with a main input and a sidechain (null = not connected),
    in blocks; returns the main output. */
template <typename Processor>
Buffer run (Processor& p, const Buffer& main, const Buffer* side, std::vector<int> blockSizes = { 512 }, double bpm = 124.0,
            std::function<void (int)> before = {})
{
    const int n = main.getNumSamples();
    const int mainCh = main.getNumChannels(), sideCh = side != nullptr ? side->getNumChannels() : 0;
    Buffer io (mainCh + sideCh, n);
    for (int c = 0; c < mainCh; ++c)
        io.copyFrom (c, 0, main, c, 0, n);
    for (int c = 0; c < sideCh; ++c)
        io.copyFrom (mainCh + c, 0, *side, c, 0, n);
    gk::OfflinePlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = p.getSampleRate();
    ph.playing = true;
    p.setPlayHead (&ph);
    juce::MidiBuffer midi;
    size_t k = 0;
    for (int pos = 0; pos < n;)
    {
        const int len = juce::jmin (blockSizes[k++ % blockSizes.size()], n - pos);
        if (before)
            before (pos);
        ph.samplePosition = pos;
        Buffer view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, len);
        p.processBlock (view, midi);
        pos += len;
    }
    p.setPlayHead (nullptr);
    Buffer out (mainCh, n);
    for (int c = 0; c < mainCh; ++c)
        out.copyFrom (c, 0, io, c, 0, n);
    return out;
}

/** Phase (degrees, 0 = rising zero crossing) at sample `at` of the component at
    `hz` of x, measured over the 3 periods that follow it (Hann): the sub that
    sounds with the kick (before it, the previous kick's shift is still there). */
inline double phaseAt (const float* x, int n, int at, double hz, double sr)
{
    const double T = sr / hz, w = 2.0 * juce::MathConstants<double>::pi / T;
    const int W = (int) std::round (3.0 * T);
    double I = 0.0, Q = 0.0;
    for (int k = 0; k <= W; ++k)
    {
        const int i = at + k;
        if (i < 0 || i >= n)
            continue;
        const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * k / W);
        I += win * x[i] * std::cos (w * k);
        Q += win * x[i] * std::sin (w * k);
    }
    return std::atan2 (I, Q) * 180.0 / juce::MathConstants<double>::pi;
}
} // namespace kickbass
