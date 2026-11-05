#include "BallroomDsp.h"

namespace bl
{
void BallroomDsp::prepare (double sampleRate, int maxBlockSize, int nch)
{
    numChannels = juce::jlimit (1, 2, nch);
    outL.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    outR.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    reverb.setParams (params);
    reverb.prepare (sampleRate, juce::jmax (1, maxBlockSize));
    levelCoeff = 1.0 - std::exp (-1.0 / (0.1 * sampleRate));
    level = 0.0;
}

void BallroomDsp::setKnobs (const float* v, int n)
{
    if (n < numKnobs)
        return;
    params.mix = v[mixKnob];
    params.predelayMs = v[predelayKnob];
    params.decaySeconds = v[decayKnob];
    params.highCutHz = v[highCutKnob];
    params.depth = v[depthKnob];
    params.earlySizeMs = v[earlySizeKnob];
    params.earlyCross = v[earlyCrossKnob];
    params.earlyModRate = v[earlyModRateKnob];
    params.earlyModDepth = v[earlyModDepthKnob];
    params.earlySend = v[earlySendKnob];
    params.diffusion = v[diffusionKnob];
    params.lateSize = v[lateSizeKnob];
    params.lateCross = v[lateCrossKnob];
    params.lateModRate = v[lateModRateKnob];
    params.lateModDepth = v[lateModDepthKnob];
    params.bassMult = v[bassMultKnob];
    params.bassXoverHz = v[bassXoverKnob];
    params.highMult = v[highMultKnob];
    params.highXoverHz = v[highXoverKnob];
}

void BallroomDsp::setChoices (const int* c, int n)
{
    if (n < numChoices)
        return;
    params.mode = (ballroom::Mode) juce::jlimit (0, (int) ballroom::Mode::count - 1, c[modeChoice]);
    params.freeze = c[freezeChoice] == 1;
}

void BallroomDsp::processWet (juce::AudioBuffer<float>& buffer, int n, const float*, const gk::BlockTiming&)
{
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    n = juce::jmin (n, (int) outL.size());
    reverb.setParams (params);
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (nch > 1 ? 1 : 0);
    reverb.process (inL, inR, outL.data(), outR.data(), n);
    // the level of what the reverb adds (for Mira's facets)
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 * ((double) outL[(size_t) i] - inL[i] + (double) outR[(size_t) i] - inR[i]);
        level += levelCoeff * (w * w - level);
    }
    if (nch > 1)
    {
        buffer.copyFrom (0, 0, outL.data(), n);
        buffer.copyFrom (1, 0, outR.data(), n);
    }
    else
        for (int i = 0; i < n; ++i)
            buffer.getWritePointer (0)[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
    telemetry.wetLevel.store ((float) std::sqrt (level), std::memory_order_relaxed);
    telemetry.frozen.store (params.freeze, std::memory_order_relaxed);
}
} // namespace bl
