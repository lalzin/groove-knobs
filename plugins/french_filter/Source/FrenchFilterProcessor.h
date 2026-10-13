#pragma once

#include "FrenchFilterDsp.h"

namespace ff
{
/** French Filter: the generic one-knob frame + the French Filter engine. */
class FrenchFilterProcessor final : public gk::OneKnobProcessor
{
public:
    FrenchFilterProcessor();

    /** Uses a custom preset list (calibration tool, tests). */
    explicit FrenchFilterProcessor (gk::PresetManager presets);

    juce::AudioProcessorEditor* createEditor() override;

    /** The built-in presets, in display order (from BinaryData). */
    static gk::PresetManager loadBuiltInPresets();

    FrenchFilterDsp& getFrenchFilterDsp() noexcept { return static_cast<FrenchFilterDsp&> (getEngine()); }
};
} // namespace ff
