#pragma once

#include "BoogieTrioProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>

namespace bttest
{
using namespace gktest;
using bt::BoogieTrioProcessor;

constexpr int numPresets = 8;

/** Bus layouts under test: mono → mono, stereo → stereo, mono → stereo. */
enum class Layout { mono, stereo, monoToStereo };

inline void setLayout (juce::AudioProcessor& proc, Layout l)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (l == Layout::stereo ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono());
    layout.outputBuses.add (l == Layout::mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    REQUIRE (proc.setBusesLayout (layout));
}

inline std::unique_ptr<BoogieTrioProcessor> makeProc (Layout layout, double sr, int preset, float amount, int vintage = -1, int wide = -1,
                                                      int block = 512)
{
    auto p = std::make_unique<BoogieTrioProcessor>();
    setLayout (*p, layout);
    setParam (p->getPresetParameter(), (float) preset);
    if (vintage >= 0)
        setParam (p->getVintageParameter(), (float) vintage);
    if (wide >= 0)
        setParam (p->getWideParameter(), (float) wide);
    setParam (p->getAmountParameter(), amount);
    p->prepareToPlay (sr, block);
    return p;
}

/** Runs `in` (1 or 2 channels) through a processor whose output may have
    more channels (mono → stereo): returns the output buffer. */
inline Buffer run (juce::AudioProcessor& proc, const Buffer& in, int outChannels, const std::vector<int>& blocks,
                   std::function<void (int)> beforeBlock = {})
{
    Buffer io (outChannels, in.getNumSamples());
    io.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        io.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
    juce::MidiBuffer midi;
    size_t k = 0;
    for (int pos = 0; pos < io.getNumSamples();)
    {
        const int n = juce::jmin (blocks[k++ % blocks.size()], io.getNumSamples() - pos);
        if (beforeBlock)
            beforeBlock (pos);
        Buffer view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, n);
        proc.processBlock (view, midi);
        pos += n;
    }
    return io;
}

inline Buffer monoOf (const Buffer& b)
{
    Buffer m (1, b.getNumSamples());
    m.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
    if (b.getNumChannels() > 1)
    {
        m.addFrom (0, 0, b, 1, 0, b.getNumSamples());
        m.applyGain (0.5f);
    }
    return m;
}
} // namespace bttest
