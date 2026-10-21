#pragma once

#include "KickLockDsp.h"

namespace kl
{
/** Kick Lock: the generic one-knob frame (MIX = dry / wet of the sidechain
    processing, no presets) with a sidechain input bus, and the engine. */
class KickLockProcessor final : public gk::OneKnobProcessor
{
public:
    KickLockProcessor();

    juce::AudioProcessorEditor* createEditor() override;

    KickLockDsp& getDsp() noexcept { return static_cast<KickLockDsp&> (getEngine()); }
    juce::RangedAudioParameter& getMixParameter() noexcept { return getAmountParameter(); }
    juce::AudioParameterBool& getLockParameter() noexcept { return getSwitchParameter (KickLockDsp::lockSwitch); }
    juce::RangedAudioParameter& getTargetPhaseParameter() noexcept { return getKnobParameter (KickLockDsp::targetKnob); }
    juce::RangedAudioParameter& getOffsetParameter() noexcept { return getKnobParameter (KickLockDsp::offsetKnob); }
    juce::RangedAudioParameter& getSubCutoffParameter() noexcept { return getKnobParameter (KickLockDsp::subCutKnob); }
    juce::RangedAudioParameter& getDepthParameter() noexcept { return getKnobParameter (KickLockDsp::depthKnob); }
    juce::RangedAudioParameter& getWaveStartParameter() noexcept { return getKnobParameter (KickLockDsp::waveStartKnob); }
    juce::RangedAudioParameter& getThresholdParameter() noexcept { return getKnobParameter (KickLockDsp::thresholdKnob); }
    juce::AudioParameterChoice& getShapeParameter() noexcept { return getChoiceParameter (KickLockDsp::shapeChoice); }
    juce::AudioParameterChoice& getReleaseParameter() noexcept { return getChoiceParameter (KickLockDsp::releaseChoice); }

    /** Every parameter back to its default (the "Réinitialiser" button). */
    void resetAllToDefaults();
};
} // namespace kl
