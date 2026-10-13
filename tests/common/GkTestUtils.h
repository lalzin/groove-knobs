#pragma once

// Test utilities shared by every Groove Knobs plugin's test suite.

#include <groove_core/groove_core.h>
#include <catch2/catch_test_macros.hpp>

namespace gktest
{
using Buffer = juce::AudioBuffer<float>;

inline const std::array<double, 4> sampleRates { 44100.0, 48000.0, 96000.0, 192000.0 };

inline void setParam (juce::RangedAudioParameter& p, float plain) { p.setValueNotifyingHost (p.convertTo0to1 (plain)); }

inline void setLayout (juce::AudioProcessor& proc, int numChannels)
{
    const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    REQUIRE (proc.setBusesLayout (layout));
}

/** Processes `input` with a fixed block size sequence (no latency compensation). */
inline Buffer processBlocks (juce::AudioProcessor& proc, const Buffer& input, const std::vector<int>& blockSizes,
                             std::function<void (int samplePos)> beforeBlock = {})
{
    Buffer out (input);
    juce::MidiBuffer midi;
    size_t k = 0;
    for (int pos = 0; pos < out.getNumSamples();)
    {
        const int n = juce::jmin (blockSizes[k++ % blockSizes.size()], out.getNumSamples() - pos);
        if (beforeBlock)
            beforeBlock (pos);
        Buffer view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        proc.processBlock (view, midi);
        pos += n;
    }
    return out;
}

inline bool allFinite (const Buffer& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

/** HF energy (> 8 kHz) of the second difference of a mono signal: the
    "zipper / click" detector used by the anti-zipper and preset tests. */
inline double zipperEnergy (const float* y, int start, int end, double sr)
{
    gk::TptSvf h1, h2;
    const auto c1 = gk::TptSvf::Coeffs::make (8000.0, sr, 0.541);
    const auto c2 = gk::TptSvf::Coeffs::make (8000.0, sr, 1.307);
    double e = 0.0;
    for (int i = 2; i < end; ++i)
    {
        const double d2 = (double) y[i] - 2.0 * y[i - 1] + y[i - 2];
        const double hp = h2.processHighpass (h1.processHighpass (d2, c1), c2);
        if (i >= start)
            e += hp * hp;
    }
    return e;
}

/** Loudest short-term (5 ms window) HF level, in dB, of the second
    difference of a mono signal, high-passed above 8 kHz. A click shows up
    as a burst; smooth parameter changes do not. */
inline double zipperBurstDb (const float* y, int start, int end, double sr)
{
    gk::TptSvf h1, h2;
    const auto c1 = gk::TptSvf::Coeffs::make (8000.0, sr, 0.541);
    const auto c2 = gk::TptSvf::Coeffs::make (8000.0, sr, 1.307);
    const int win = juce::jmax (8, (int) (0.005 * sr));
    std::vector<double> e ((size_t) juce::jmax (0, end), 0.0);
    for (int i = 2; i < end; ++i)
    {
        const double d2 = (double) y[i] - 2.0 * y[i - 1] + y[i - 2];
        const double hp = h2.processHighpass (h1.processHighpass (d2, c1), c2);
        e[(size_t) i] = hp * hp;
    }
    double acc = 0.0, best = 0.0;
    for (int i = start; i < end; ++i)
    {
        acc += e[(size_t) i];
        if (i - win >= start)
            acc -= e[(size_t) (i - win)];
        best = juce::jmax (best, acc / win);
    }
    return 10.0 * std::log10 (juce::jmax (1.0e-30, best));
}

/** Energy of the second difference of the input (normalisation). */
inline double secondDiffEnergy (const float* x, int start, int end)
{
    double e = 0.0;
    for (int i = juce::jmax (2, start); i < end; ++i)
    {
        const double d2 = (double) x[i] - 2.0 * x[i - 1] + x[i - 2];
        e += d2 * d2;
    }
    return e;
}
} // namespace gktest
