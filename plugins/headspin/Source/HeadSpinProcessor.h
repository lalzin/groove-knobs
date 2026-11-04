#pragma once

#include "HeadSpinDsp.h"

namespace hs
{
/** HEAD SPIN: a circular panner round a head seen from above, on a shape.
    MIX (the main knob, "mix") is the constant-power dry / wet. A preset sets
    the motion (shape, stretch, rotation, direction, speed, start, distance,
    groove); LEVEL, RENDER and RETRIGGER stay the user's. A timer builds the
    shape's path on the message thread when SHAPE moves. */
class HeadSpinProcessor final : public gk::OneKnobProcessor, private juce::Timer
{
public:
    HeadSpinProcessor();
    explicit HeadSpinProcessor (gk::PresetManager presets);
    ~HeadSpinProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static gk::PresetManager loadBuiltInPresets();

    HeadSpinDsp& getDsp() noexcept { return static_cast<HeadSpinDsp&> (getEngine()); }
    juce::RangedAudioParameter& getKnob (HeadSpinDsp::Knob k) noexcept { return getKnobParameter (k); }
    juce::AudioParameterChoice& getChoice (HeadSpinDsp::Choice c) noexcept { return getChoiceParameter (c); }

    /** Sets the motion from preset `index` (message thread). */
    void applyPreset (int index);

    /** Builds the path for the current SHAPE now (offline renders, tests:
        there is no message loop to run the timer). */
    void updateShapeNow() { getDsp().updateShape (getKnob (HeadSpinDsp::shapeKnob).convertFrom0to1 (getKnob (HeadSpinDsp::shapeKnob).getValue())); }

private:
    void timerCallback() override { updateShapeNow(); }

    class PresetWatcher;
    std::unique_ptr<PresetWatcher> watcher;
    int appliedPreset = -1;
    bool loadingState = false;
};
} // namespace hs
