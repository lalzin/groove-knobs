#pragma once

#include <groove_core/groove_core.h>
#include "Spinner.h"

namespace hs
{
/** HEAD SPIN's engine for the Groove Knobs frame: MIX is the main knob
    (OneKnobProcessor's "amount", here "mix"), the rest are small knobs and
    choices (OneKnobConfig order below). The work is headspin::Spinner. */
class HeadSpinDsp final : public gk::OneKnobEngine
{
public:
    enum Knob { shapeKnob, widthKnob, depthKnob, rotationKnob, rateHzKnob, phaseKnob, distanceKnob, grooveKnob, levelKnob, numKnobs };
    enum Choice { directionChoice, rateModeChoice, rateSyncChoice, renderChoice, retriggerChoice, numChoices };

    /** What the editor shows (written by the audio thread once per block). */
    struct Telemetry
    {
        std::atomic<float> x { 0.0f }, y { 1.0f }; // after stretch / rotation (r = 1: the ears' circle)
        std::atomic<float> azimuthDeg { 0.0f }, radius { 1.0f };
        std::atomic<float> turnsPerSecond { 0.0f };
        std::atomic<float> monoLossDb { 0.0f };     // measured: the mono sum against the stereo level
        std::atomic<float> panLawLossDb { 0.0f };   // what the pan law alone loses at this position
        std::atomic<int> blocks { 0 };
    };

    // OneKnobEngine
    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int) override {}
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override;
    void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    /** Message thread (a timer in the processor, the render tools): builds the
        path for SHAPE `shape` when it changed. */
    void updateShape (float shape);
    /** Message thread: the custom shape (normalised), built at once. */
    void setCustomShape (const std::vector<headspin::Point>& points, float shape)
    {
        spinner.setCustomShape (points);
        updateShape (shape);
    }

    Telemetry& getTelemetry() noexcept { return telemetry; }

private:
    headspin::Spinner spinner;
    headspin::Params params;
    std::vector<float> outL, outR;
    int numChannels = 2;
    double sampleRate = 48000.0;
    std::array<int, 128> noteOns {};
    int numNoteOns = 0;
    double monoE = 0.0, stereoE = 0.0, monoCoeff = 0.0;
    Telemetry telemetry;
};
} // namespace hs
