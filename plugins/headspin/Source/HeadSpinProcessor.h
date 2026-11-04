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

    /** The custom shape (SHAPE 6), drawn by hand in the view from above (in
        units: x right, y front): normalised (ShapeBank::normaliseDrawing),
        built at once, saved with the session. Fewer than 8 points: a circle.
        Message thread. */
    void setCustomShape (const std::vector<headspin::Point>& drawing);
    const std::vector<headspin::Point>& getCustomShape() const noexcept { return customShape; }
    /** +1 at every new custom shape (the editor redraws its path). */
    int getCustomVersion() const noexcept { return customVersion; }

    /** Builds the path for the current SHAPE now (offline renders, tests:
        there is no message loop to run the timer). */
    void updateShapeNow() { getDsp().updateShape (getKnob (HeadSpinDsp::shapeKnob).convertFrom0to1 (getKnob (HeadSpinDsp::shapeKnob).getValue())); }

protected:
    void saveExtraState (juce::ValueTree& state) override;
    void loadExtraState (const juce::ValueTree& state) override;

private:
    void timerCallback() override { updateShapeNow(); }
    void useCustomShape (std::vector<headspin::Point> normalised);

    std::vector<headspin::Point> customShape;
    int customVersion = 0;

    class PresetWatcher;
    std::unique_ptr<PresetWatcher> watcher;
    int appliedPreset = -1;
    bool loadingState = false;
};
} // namespace hs
