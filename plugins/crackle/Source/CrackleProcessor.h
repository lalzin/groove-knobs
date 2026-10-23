#pragma once

#include "CrackleDsp.h"

namespace crackle
{
/** CRACKLE: the one-knob frame as an effect. GRAIN is the main knob; every
    effect has its own knob and an AUTO switch: on AUTO it follows GRAIN (its
    curve, GrainMap.h), moved by hand or automated it is its own (AUTO goes
    off; clicking the indicator gives it back to GRAIN). A preset sets GRAIN,
    its saturation TYPE and the knobs it names (the others back on AUTO). */
class CrackleProcessor final : public gk::OneKnobProcessor
{
public:
    CrackleProcessor();
    explicit CrackleProcessor (gk::PresetManager presets);
    ~CrackleProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static gk::PresetManager loadBuiltInPresets();

    CrackleDsp& getCrackleDsp() noexcept { return static_cast<CrackleDsp&> (getEngine()); }
    juce::AudioParameterChoice& getTypeParameter() noexcept { return getChoiceParameter (CrackleDsp::typeChoice); }
    /** An effect knob's AUTO switch (knob: CrackleDsp::bandKnob ... radioKnob). */
    juce::AudioParameterBool& getAutoParameter (int knob) noexcept { return getSwitchParameter (knob - CrackleDsp::firstEffect); }
    static bool isEffectKnob (int knob) noexcept { return knob >= CrackleDsp::firstEffect && knob < CrackleDsp::firstEffect + CrackleDsp::numEffects; }

    /** Sets GRAIN, TYPE and the knobs from preset `index` (message thread). */
    void applyPreset (int index);

private:
    class Watcher;
    std::unique_ptr<Watcher> watcher;
    int appliedPreset = -1;
    bool loadingState = false, applying = false;
    friend class Watcher;
};
} // namespace crackle
