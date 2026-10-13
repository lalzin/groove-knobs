#pragma once

#include "BackwashDsp.h"

namespace bw
{
/** Backwash: the generic one-knob frame + the reverse-reverb engine, with two
    chips (LONG, SHIMMER) and a fixed 2 s latency. */
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
};
} // namespace bw
