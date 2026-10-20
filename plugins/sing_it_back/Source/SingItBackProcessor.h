#pragma once

#include "SingItBackDsp.h"

namespace sib
{
/** Sing It Back: the generic one-knob frame (CROWD, no presets) + the crowd
    engine, with the scale bar (key, scale, snap) and the advanced panel. */
class SingItBackProcessor final : public gk::OneKnobProcessor
{
public:
    SingItBackProcessor();

    juce::AudioProcessorEditor* createEditor() override;

    SingItBackDsp& getDsp() noexcept { return static_cast<SingItBackDsp&> (getEngine()); }
    juce::RangedAudioParameter& getCrowdParameter() noexcept { return getAmountParameter(); }
    juce::AudioParameterBool& getSnapParameter() noexcept { return getSwitchParameter (SingItBackDsp::snapSwitch); }
    juce::RangedAudioParameter& getSpreadParameter() noexcept { return getKnobParameter (SingItBackDsp::spreadKnob); }
    juce::RangedAudioParameter& getHumanizeParameter() noexcept { return getKnobParameter (SingItBackDsp::humanizeKnob); }
    juce::RangedAudioParameter& getDropoutsParameter() noexcept { return getKnobParameter (SingItBackDsp::dropoutsKnob); }
    juce::RangedAudioParameter& getRoomParameter() noexcept { return getKnobParameter (SingItBackDsp::roomKnob); }
    juce::AudioParameterChoice& getKeyParameter() noexcept { return getChoiceParameter (SingItBackDsp::keyChoice); }
    juce::AudioParameterChoice& getScaleParameter() noexcept { return getChoiceParameter (SingItBackDsp::scaleChoice); }
    juce::AudioParameterChoice& getHarmonyParameter() noexcept { return getChoiceParameter (SingItBackDsp::harmonyChoice); }
    juce::AudioParameterChoice& getVoicesParameter() noexcept { return getChoiceParameter (SingItBackDsp::voicesChoice); }
    juce::AudioParameterChoice& getRoomSizeParameter() noexcept { return getChoiceParameter (SingItBackDsp::roomSizeChoice); }
    juce::AudioParameterChoice& getStyleParameter() noexcept { return getChoiceParameter (SingItBackDsp::styleChoice); }
    juce::AudioParameterChoice& getResponseParameter() noexcept { return getChoiceParameter (SingItBackDsp::responseChoice); }
};
} // namespace sib
