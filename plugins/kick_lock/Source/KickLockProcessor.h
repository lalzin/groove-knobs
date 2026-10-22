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
    juce::AudioParameterChoice& getLockModeParameter() noexcept { return getChoiceParameter (KickLockDsp::modeChoice); }
    juce::AudioParameterChoice& getTriggerParameter() noexcept { return getChoiceParameter (KickLockDsp::triggerChoice); }
    juce::AudioParameterChoice& getLinkChannelParameter() noexcept { return getChoiceParameter (KickLockDsp::linkChoice); }
    juce::AudioParameterChoice& getMonoBelowParameter() noexcept { return getChoiceParameter (KickLockDsp::monoChoice); }

    /** Every parameter back to its default, and the custom curve (the Reset button). */
    void resetAllToDefaults();

    /** The ducking curve of a SHAPE (a preset's, or the custom one). */
    std::vector<KickLockDsp::CurveNode> curveFor (int shape) const;
    const std::vector<KickLockDsp::CurveNode>& getCustomCurve() const noexcept { return customCurve; }
    /** The custom curve (message thread), saved with the session. */
    void setCustomCurve (std::vector<KickLockDsp::CurveNode> nodes);
    /** Bumped on every change of the custom curve (the editor redraws). */
    int getCurveVersion() const noexcept { return curveVersion; }

protected:
    void saveExtraState (juce::ValueTree& state) override;
    void loadExtraState (const juce::ValueTree& state) override;

private:
    std::vector<KickLockDsp::CurveNode> customCurve;
    int curveVersion = 0;
};
} // namespace kl
