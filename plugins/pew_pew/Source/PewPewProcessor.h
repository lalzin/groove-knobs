#pragma once

#include "PewPewDsp.h"

namespace pp
{
/** Pew Pew: the generic one-knob frame as an instrument (MIDI in, no audio
    input) + the drum-voice engine, with ROOM / ECHO knobs and the echo
    division. */
class PewPewProcessor final : public gk::OneKnobProcessor
{
public:
    PewPewProcessor();
    explicit PewPewProcessor (gk::PresetManager presets);

    juce::AudioProcessorEditor* createEditor() override;

    static gk::PresetManager loadBuiltInPresets();

    PewPewDsp& getPewDsp() noexcept { return static_cast<PewPewDsp&> (getEngine()); }
    juce::RangedAudioParameter& getRoomParameter() noexcept { return getKnobParameter (PewPewDsp::roomKnob); }
    juce::RangedAudioParameter& getEchoParameter() noexcept { return getKnobParameter (PewPewDsp::echoKnob); }
    juce::AudioParameterChoice& getDivisionParameter() noexcept { return getChoiceParameter (PewPewDsp::divChoice); }
    juce::AudioParameterBool& getUpParameter() noexcept { return getToggleParameter (PewPewDsp::upToggle); }
    juce::AudioParameterBool& getLongParameter() noexcept { return getToggleParameter (PewPewDsp::longToggle); }
    juce::AudioParameterBool& getRollParameter() noexcept { return getToggleParameter (PewPewDsp::rollToggle); }
};
} // namespace pp
