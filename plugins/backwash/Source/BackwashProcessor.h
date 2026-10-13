#pragma once

#include "BackwashDsp.h"

namespace bw
{
/** Backwash: the generic one-knob frame + the reverse-reverb engine, with two
    chips (LONG, SHIMMER), 2 s of latency or none in LIVE mode (LIVE, LOCK). */
class BackwashProcessor final : public gk::OneKnobProcessor
{
public:
    BackwashProcessor();
    explicit BackwashProcessor (gk::PresetManager presets);

    juce::AudioProcessorEditor* createEditor() override;

    static gk::PresetManager loadBuiltInPresets();

    BackwashDsp& getBackwashDsp() noexcept { return static_cast<BackwashDsp&> (getEngine()); }
    juce::AudioParameterBool& getLongParameter() noexcept { return getToggleParameter (BackwashDsp::longToggle); }
    juce::AudioParameterBool& getShimmerParameter() noexcept { return getToggleParameter (BackwashDsp::shimmerToggle); }
    juce::AudioParameterBool& getLiveParameter() noexcept { return getSwitchParameter (BackwashDsp::liveSwitch); }
    juce::AudioParameterBool& getLockParameter() noexcept { return getSwitchParameter (BackwashDsp::lockSwitch); }

    void saveExtraState (juce::ValueTree& state) override;
    void loadExtraState (const juce::ValueTree& state) override;
};
} // namespace bw
