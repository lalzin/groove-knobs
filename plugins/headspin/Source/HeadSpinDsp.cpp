#include "HeadSpinDsp.h"

namespace hs
{
void HeadSpinDsp::prepare (double sr, int maxBlockSize, int nch)
{
    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, nch);
    outL.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    outR.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    monoCoeff = 1.0 - std::exp (-1.0 / (0.3 * sr)); // the mono check over ~300 ms
    spinner.prepare (sr);
    spinner.setParams (params);
    spinner.updateShape();
}

void HeadSpinDsp::reset()
{
    spinner.reset();
    monoE = stereoE = 0.0;
    numNoteOns = 0;
}

void HeadSpinDsp::updateShape (float shape)
{
    spinner.setShapeForBuild (shape);
    spinner.updateShape();
}

void HeadSpinDsp::setKnobs (const float* v, int n)
{
    if (n < numKnobs)
        return;
    params.shape = v[shapeKnob];
    params.width = v[widthKnob];
    params.depth = v[depthKnob];
    params.rotationDeg = v[rotationKnob];
    params.rateHz = v[rateHzKnob];
    params.startDeg = v[phaseKnob];
    params.distance = v[distanceKnob];
    params.groove = v[grooveKnob];
    params.levelDb = v[levelKnob];
}

void HeadSpinDsp::setChoices (const int* c, int n)
{
    if (n < numChoices)
        return;
    params.leftToRight = c[directionChoice] == 0;
    params.sync = c[rateModeChoice] == 1;
    params.division = c[rateSyncChoice];
    // (render: Speakers only until the headphone mode, step 4)
    params.retrigger = c[retriggerChoice] == 1 ? headspin::Motion::Retrigger::midi
                     : c[retriggerChoice] == 2 ? headspin::Motion::Retrigger::transient
                                               : headspin::Motion::Retrigger::off;
}

void HeadSpinDsp::setMidi (const juce::MidiBuffer& midi, int start, int count)
{
    numNoteOns = 0;
    for (const auto meta : midi)
    {
        const int at = meta.samplePosition - start;
        if (at < 0 || at >= count)
            continue;
        if (meta.getMessage().isNoteOn() && numNoteOns < (int) noteOns.size())
            noteOns[(size_t) numNoteOns++] = at;
    }
}

void HeadSpinDsp::processWet (juce::AudioBuffer<float>& buffer, int n, const float* amount, const gk::BlockTiming& timing)
{
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    if (n > (int) outL.size())
        n = (int) outL.size();
    spinner.setParams (params);
    headspin::Transport t;
    t.bpm = timing.bpm;
    t.playing = timing.isPlaying;
    t.ppq = timing.beatsAtStart;
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (nch > 1 ? 1 : 0);
    spinner.process (inL, inR, outL.data(), outR.data(), n, t, noteOns.data(), numNoteOns, amount);
    numNoteOns = 0;

    // Mono check: the mono sum against the stereo level, ~300 ms
    for (int i = 0; i < n; ++i)
    {
        const double l = outL[(size_t) i], r = outR[(size_t) i], m = 0.5 * (l + r);
        monoE += monoCoeff * (m * m - monoE);
        stereoE += monoCoeff * (0.5 * (l * l + r * r) - stereoE);
    }
    if (nch > 1)
    {
        buffer.copyFrom (0, 0, outL.data(), n);
        buffer.copyFrom (1, 0, outR.data(), n);
    }
    else
        for (int i = 0; i < n; ++i)
            buffer.getWritePointer (0)[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);

    const auto pos = spinner.position();
    telemetry.x.store ((float) pos.x, std::memory_order_relaxed);
    telemetry.y.store ((float) pos.y, std::memory_order_relaxed);
    telemetry.azimuthDeg.store ((float) pos.azimuthDeg, std::memory_order_relaxed);
    telemetry.radius.store ((float) pos.radius, std::memory_order_relaxed);
    telemetry.turnsPerSecond.store ((float) spinner.turnsPerSecond(), std::memory_order_relaxed);
    telemetry.monoLossDb.store (stereoE > 1.0e-9 ? (float) (10.0 * std::log10 (monoE / stereoE + 1.0e-12)) : 0.0f, std::memory_order_relaxed);
    // the pan law alone at this azimuth (constant power, 0.85 deep): (gL + gR)^2 / 2 against gL^2 + gR^2 = 1
    const double pan = 0.25 * juce::MathConstants<double>::pi * (1.0 + 0.85 * std::sin (pos.azimuthDeg * juce::MathConstants<double>::pi / 180.0));
    const double g = std::cos (pan) + std::sin (pan);
    telemetry.panLawLossDb.store ((float) (10.0 * std::log10 (0.5 * g * g)), std::memory_order_relaxed);
    telemetry.blocks.fetch_add (1, std::memory_order_relaxed);
}
} // namespace hs
