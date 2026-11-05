#pragma once

#include <groove_core/groove_core.h>
#include "Ballroom.h"

namespace bl
{
/** BALLROOM's engine for the Groove Knobs frame: SPACE is the main knob (the
    processor turns it into the macro's parameters on the message thread);
    the reverb reads every parameter (small knobs and choices, in the order
    below). The work is ballroom::Ballroom. */
class BallroomDsp final : public gk::OneKnobEngine
{
public:
    enum Knob
    {
        mixKnob, predelayKnob, decayKnob, highCutKnob, depthKnob,                                       // main
        earlySizeKnob, earlyCrossKnob, earlyModRateKnob, earlyModDepthKnob, earlySendKnob, diffusionKnob, // early
        lateSizeKnob, lateCrossKnob, lateModRateKnob, lateModDepthKnob,                                  // late
        bassMultKnob, bassXoverKnob, highMultKnob, highXoverKnob,
        clearKnob,                                                                                       // main (added after)
        numKnobs
    };
    enum Choice { modeChoice, freezeChoice, numChoices };

    /** What the editor shows (audio thread, once per block). */
    struct Telemetry
    {
        std::atomic<float> wetLevel { 0.0f }; // the reverb's level (RMS, ~100 ms), linear
        std::atomic<bool> frozen { false };
    };

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override { reverb.reset(); level = 0.0; }
    int getLatencySamples() const override { return 0; }
    void setPreset (int) override {}
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    Telemetry& getTelemetry() noexcept { return telemetry; }

private:
    ballroom::Ballroom reverb;
    ballroom::Params params;
    std::vector<float> outL, outR;
    int numChannels = 2;
    double level = 0.0, levelCoeff = 0.0;
    Telemetry telemetry;
};
} // namespace bl
