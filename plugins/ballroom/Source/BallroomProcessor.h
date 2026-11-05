#pragma once

#include "BallroomDsp.h"

namespace bl
{
/** BALLROOM: an algorithmic reverb, SPACE as the one knob.
    SPACE is a macro (SpaceMap.h): it sets MIX, DECAY, LATE SIZE, DEPTH,
    PREDELAY and the MOD DEPTHs, then the current preset's own values on top.
    Moving anything in the ROOM panel leaves the macro ("custom"); moving SPACE
    again brings it back. A preset sets the mode, FREEZE, SPACE (and so the
    macro), its own values, and every other setting back to its default. */
class BallroomProcessor final : public gk::OneKnobProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    BallroomProcessor();
    explicit BallroomProcessor (gk::PresetManager presets);
    ~BallroomProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    static gk::PresetManager loadBuiltInPresets();

    BallroomDsp& getDsp() noexcept { return static_cast<BallroomDsp&> (getEngine()); }
    juce::RangedAudioParameter& getKnob (BallroomDsp::Knob k) noexcept { return getKnobParameter (k); }
    juce::AudioParameterChoice& getChoice (BallroomDsp::Choice c) noexcept { return getChoiceParameter (c); }

    /** Sets everything from preset `index` (message thread). */
    void applyPreset (int index);
    /** SPACE's macro now (message thread; it also runs on its own when SPACE moves). */
    void applyMacro();
    /** A ROOM setting moved since SPACE last did: the editor shows "custom". */
    bool isCustom() const noexcept { return custom.load(); }

protected:
    void saveExtraState (juce::ValueTree& state) override;
    void loadExtraState (const juce::ValueTree& state) override;

private:
    void parameterChanged (const juce::String& id, float value) override;
    void handleUpdate();
    /** Runs handleUpdate on the message thread (the frame has its own AsyncUpdater). */
    struct Updater final : juce::AsyncUpdater
    {
        std::function<void()> fn;
        void handleAsyncUpdate() override { fn(); }
    };
    Updater updater;
    void set (juce::RangedAudioParameter& p, float plain);

    std::atomic<bool> custom { false }, macroPending { false }, presetPending { false };
    bool applying = false; // our own changes do not mark "custom"
    bool loadingState = false;
    int appliedPreset = -1;
};
} // namespace bl
