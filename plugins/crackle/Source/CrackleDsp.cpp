#include "CrackleDsp.h"

namespace crackle
{
void CrackleDsp::prepare (double sampleRate, int maxBlockSize, int)
{
    sr = sampleRate;
    chain.prepare (sr);
    for (auto& d : dry)
        d.assign ((size_t) Chain::latencySamples + 1, 0.0f);
    for (auto* v : { &mono, &scratchL, &scratchR })
        v->assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    mix.prepare (sr, 0.020);
    volume.prepare (sr, 0.020);
    reset();
}

void CrackleDsp::reset()
{
    chain.reset();
    for (auto& d : dry)
        std::fill (d.begin(), d.end(), 0.0f);
    dryPos = 0;
    mix.snap (knobs[mixKnob]);
    volume.snap (gk::dspmath::dbToGain (knobs[levelKnob]));
    noiseEnv = 0.0f;
}

void CrackleDsp::setKnobs (const float* values, int n)
{
    for (int i = 0; i < juce::jmin (n, (int) numKnobs); ++i)
        knobs[(size_t) i] = values[i];
}

void CrackleDsp::setSwitches (const bool* on, int n)
{
    for (int i = 0; i < numEffects; ++i)
        autos[(size_t) i] = i < n ? on[i] : true;
}

float CrackleDsp::effectAmount (int knob, float g, float value, bool automatic) noexcept
{
    if (! automatic)
        return juce::jlimit (0.0f, 1.0f, value);
    switch (knob)
    {
        case bandKnob: return grain::band (g);
        case wowKnob: return grain::wow (g);
        case flutterKnob: return grain::flutter (g);
        case hissKnob: return grain::hiss (g);
        case saturationKnob: return grain::saturation (g);
        case vinylKnob: return grain::vinyl (g);
        case dropoutKnob: return grain::dropout (g);
        case crushKnob: return grain::crush (g);
        case radioKnob: return grain::radio (g);
        default: break;
    }
    return 0.0f;
}

void CrackleDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming&)
{
    juce::ScopedNoDenormals noDenormals;
    // GRAIN: once per block (the chain smooths every amount over 20 ms)
    const float g = amount != nullptr ? juce::jlimit (0.0f, 1.0f, amount[numSamples - 1]) : 0.0f;
    auto effect = [&] (int knob) { return effectAmount (knob, g, knobs[(size_t) knob], autos[(size_t) (knob - firstEffect)]); };
    Amounts a;
    a.band = effect (bandKnob);
    a.wow = effect (wowKnob);
    a.flutter = effect (flutterKnob);
    a.hiss = effect (hissKnob);
    a.saturation = effect (saturationKnob);
    a.vinyl = effect (vinylKnob);
    a.dropout = effect (dropoutKnob);
    a.crush = effect (crushKnob);
    a.radio = effect (radioKnob);
    a.noise = juce::jlimit (0.0f, 1.0f, knobs[noiseKnob]);
    a.satType = satType;
    a.focusLow = juce::jlimit (20.0f, 20000.0f, knobs[focusLowKnob]);
    a.focusHigh = juce::jlimit (a.focusLow, 20000.0f, knobs[focusHighKnob]);

    const int channels = buffer.getNumChannels();
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : mono.data();
    if (channels == 1)
        std::copy (left, left + numSamples, right);

    // MIX: the input kept aside (the chain runs in place), then delayed like the chain
    const int D = (int) dry[0].size();
    auto& dl = dry[0];
    auto& dr = dry[1];
    float* inL = scratchL.data();
    float* inR = scratchR.data();
    std::copy (left, left + numSamples, inL);
    std::copy (right, right + numSamples, inR);
    chain.process (left, right, numSamples, a);

    mix.setTarget (juce::jlimit (0.0f, 1.0f, knobs[mixKnob]));
    volume.setTarget (gk::dspmath::dbToGain (knobs[levelKnob]));
    for (int i = 0; i < numSamples; ++i)
    {
        // the dry sample from Chain::latencySamples ago
        const int readPos = (dryPos + 1) % D;
        const float dryL = dl[(size_t) readPos], dryR = dr[(size_t) readPos];
        dl[(size_t) dryPos] = inL[i];
        dr[(size_t) dryPos] = inR[i];
        dryPos = readPos;
        const float m = mix.next(), v = volume.next();
        left[i] = (dryL + m * (left[i] - dryL)) * v;
        right[i] = (dryR + m * (right[i] - dryR)) * v;
    }
    for (int ch = 2; ch < channels; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    // the UI: the noise that is being added, how much crush and wow
    const float noiseNow = a.noise * 2.0f * juce::jmax (a.hiss, a.vinyl, a.radio);
    noiseEnv += 0.2f * (noiseNow - noiseEnv);
    telemetry.noiseLevel.store (noiseEnv);
    telemetry.crush.store (a.crush);
    telemetry.wow.store (juce::jmax (a.wow, a.flutter));
    telemetry.crackles.store (chain.crackleCount);
    telemetry.dropouts.store (chain.dropoutCount);
}
} // namespace crackle
