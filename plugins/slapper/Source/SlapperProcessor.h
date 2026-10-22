#pragma once

#include "SlapperDsp.h"

namespace slapper
{
/** SLAPPER: the one-knob frame as an instrument (MIDI in, no audio input),
    FUNK as the main knob. Nine more parameters (all automatable): ATTACK,
    MUTE, TONE, DRIVE and VOL on the editor, PICK, STRING, BODY and HUMAN set
    by the presets. A preset sets FUNK, then its "params" (the ones it leaves
    out go back to their defaults); VOL stays the player's. */
class SlapperProcessor final : public gk::OneKnobProcessor
{
public:
    SlapperProcessor();
    explicit SlapperProcessor (gk::PresetManager presets);
    ~SlapperProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static gk::PresetManager loadBuiltInPresets();

    SlapperDsp& getSlapperDsp() noexcept { return static_cast<SlapperDsp&> (getEngine()); }
    juce::AudioParameterChoice& getModeParameter() noexcept { return getChoiceParameter (SlapperDsp::modeChoice); }
    juce::AudioParameterChoice& getEngineParameter() noexcept { return getChoiceParameter (SlapperDsp::engineChoice); }

    /** Sets FUNK and the knobs from preset `index` (message thread). */
    void applyPreset (int index);

private:
    class PresetWatcher;
    std::unique_ptr<PresetWatcher> watcher;
    int appliedPreset = -1;
    bool loadingState = false;
};
} // namespace slapper
