#pragma once

#include "KickSendDsp.h"

namespace kls
{
/** Kick Lock Send: the one-knob frame (SENSITIVITY, no presets) and the engine. */
class KickSendProcessor final : public gk::OneKnobProcessor
{
public:
    KickSendProcessor();

    juce::AudioProcessorEditor* createEditor() override;
    void updateTrackProperties (const TrackProperties& properties) override;

    KickSendDsp& getDsp() noexcept { return static_cast<KickSendDsp&> (getEngine()); }
    juce::RangedAudioParameter& getPunchParameter() noexcept { return getAmountParameter(); }
    juce::RangedAudioParameter& getSustainParameter() noexcept { return getKnobParameter (KickSendDsp::sustainKnob); }
    juce::RangedAudioParameter& getSensitivityParameter() noexcept { return getKnobParameter (KickSendDsp::sensitivityKnob); }
    juce::AudioParameterChoice& getLinkChannelParameter() noexcept { return getChoiceParameter (0); }
    juce::String getTrackName() const { return trackName; }

private:
    juce::String trackName { "Kick" };
};
} // namespace kls
