#pragma once

#include "BallroomDsp.h"

namespace bl
{
/** BALLROOM: an algorithmic reverb, SPACE as the one knob: the dry / wet,
    0 .. 100 % (SpaceMap.h). SPACE and MIX are one: moving either moves the
    other. The room is the preset's: its mode and every setting (the JSON
    "room"), all of it in the ROOM panel; moving one of them there lights
    CUSTOM (the room is no longer the preset's) until a preset is loaded. */
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
    /** SPACE -> MIX, or MIX -> SPACE when MIX moved last (message thread; it also runs on its own). */
    void applyMacro();
    /** A ROOM setting (other than MIX) moved since the preset was loaded: the editor shows "custom". */
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
    std::atomic<int> mixFollows { 1 }; // 1: MIX follows SPACE, 2: SPACE follows MIX
    bool applying = false; // our own changes do not mark "custom"
    bool loadingState = false;
    int appliedPreset = -1;
};
} // namespace bl
