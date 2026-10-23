#pragma once

#include <groove_core/groove_core.h>
#include "CrackleChain.h"
#include "GrainMap.h"

namespace crackle
{
/** UI telemetry (lock-free). */
struct Telemetry
{
    std::atomic<int> crackles { 0 }, dropouts { 0 }; // counters since prepare
    std::atomic<float> noiseLevel { 0.0f };          // the added noises' envelope (the GRAIN ring)
    std::atomic<float> crush { 0.0f }, wow { 0.0f }; // the amounts in use
};

/** CRACKLE's engine: the chain (CrackleChain.h), each effect's amount from
    its knob or, while that knob is on AUTO, from GRAIN (GrainMap.h); MIX
    against the dry signal delayed like the chain; VOL.

        in ─┬─> band ─> wow / flutter ─> saturation (2x) ─> crush ─> radio
            │     ─> + hiss, vinyl, radio static (NOISE) ─> dropouts ──┐
            └─> delayed like the chain ──────────────────── MIX ───────┴─> VOL ─> out */
class CrackleDsp final : public gk::OneKnobEngine
{
public:
    // the small knobs, in OneKnobConfig order
    enum Knob { mixKnob, bandKnob, wowKnob, flutterKnob, hissKnob, saturationKnob, vinylKnob, dropoutKnob, crushKnob, radioKnob,
                noiseKnob, focusLowKnob, focusHighKnob, levelKnob, numKnobs };
    /** The effects that follow GRAIN while on AUTO (switch i = knob bandKnob + i). */
    static constexpr int firstEffect = bandKnob, numEffects = radioKnob - bandKnob + 1;
    static constexpr int typeChoice = 0;

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return Chain::latencySamples; }
    void setPreset (int) override {}
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override { satType = n > typeChoice ? (SatType) juce::jlimit (0, (int) SatType::count - 1, indices[typeChoice]) : SatType::tape; }
    void setSwitches (const bool* on, int n) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    /** The amount an effect plays at: its knob, or GRAIN's curve on AUTO. */
    static float effectAmount (int knob, float grain, float knobValue, bool automatic) noexcept;
    Telemetry& getTelemetry() noexcept { return telemetry; }

private:
    Chain chain;
    std::array<float, numKnobs> knobs {};
    std::array<bool, numEffects> autos {};
    SatType satType = SatType::tape;
    // the dry path, delayed like the chain (MIX)
    std::array<std::vector<float>, 2> dry;
    int dryPos = 0;
    std::vector<float> mono, scratchL, scratchR; // (a mono input's second channel; the input kept for MIX)
    gk::LinearSmoother mix, volume;
    double sr = 48000.0;
    float noiseEnv = 0.0f;
    Telemetry telemetry;
};
} // namespace crackle
