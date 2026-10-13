#pragma once

#include "BoogieTrioDsp.h"

namespace bt
{
/** Boogie Trio: the generic one-knob frame + the triple-chorus engine, with
    two extra on/off parameters (VINTAGE, WIDE) and mono → stereo support. */
class BoogieTrioProcessor final : public gk::OneKnobProcessor
{
public:
    BoogieTrioProcessor();
    explicit BoogieTrioProcessor (gk::PresetManager presets);

    juce::AudioProcessorEditor* createEditor() override;

    static gk::PresetManager loadBuiltInPresets();

    BoogieTrioDsp& getBoogieTrioDsp() noexcept { return static_cast<BoogieTrioDsp&> (getEngine()); }
    juce::AudioParameterBool& getVintageParameter() noexcept { return getToggleParameter (BoogieTrioDsp::vintageToggle); }
    juce::AudioParameterBool& getWideParameter() noexcept { return getToggleParameter (BoogieTrioDsp::wideToggle); }
};
} // namespace bt
