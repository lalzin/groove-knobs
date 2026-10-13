#pragma once

#include <groove_core/groove_core.h>

namespace ff
{
/** Sound design of one side of a French Filter preset. The knob is bipolar:
    turning left drives the "low" side (low-pass family), turning right the
    "high" side (high-pass family). Every side combines the same building
    blocks differently:
      filter  low-pass (12 or 24 dB) or high-pass riser, resonance that peaks
              then backs off, "growl" (saturating resonance)
      drive   asymmetric tanh before the filter
      env     envelope follower that opens the filter on each hit (squelch)
      grit    sampler bit / rate reduction
      phaser  4-stage, tempo-synced
      pump    kick-driven ducking (the French Touch compression breath)
      room    small room / long wash after the filter */
struct FrenchFilterSide
{
    enum class Mode { lowpass, highpass };

    Mode mode = Mode::lowpass;
    float slopeDb = 24.0f;
    float endHz = 180.0f;          // cutoff at 100 % (low-pass: lowest, high-pass: highest)
    float curve = 1.0f, qMax = 4.0f, resPeak = 0.72f, growl = 0.0f;
    float driveMaxDb = 9.0f, asymmetry = 0.10f;
    float envOctaves = 0.0f;
    float gritBits = 24.0f, gritRateHz = 0.0f, gritStart = 0.3f; // gritRateHz 0 = off
    float pumpDepthDb = 0.0f, pumpReleaseBeats = 0.5f;
    float roomMix = 0.0f, roomSize = 0.5f, roomDamping = 0.5f, roomStart = 0.3f, roomLevel = 0.45f;
    bool filterOnRoom = false;     // "filter.target": "room" — DJM "Space" style: dry untouched, only the reverb is filtered
    float phaserMaxMix = 0.35f, phaserBars = 2.0f, phaserFeedback = 0.40f;
    float phaserMinHz = 250.0f, phaserMaxHz = 2500.0f, stereoPhaseDeg = 0.0f;

    bool hasGrit() const noexcept { return gritRateHz > 0.0f; }
    bool hasRoom() const noexcept { return roomMix > 0.0f; }

    juce::StringArray tags;

    /** Reads one side ("low" or "high" section). `mode` is forced by the side. */
    static FrenchFilterSide fromJson (const juce::var& section, Mode mode);
};

/** A French Filter preset: its two sides. */
struct FrenchFilterPreset
{
    FrenchFilterSide low, high;

    const FrenchFilterSide& sideFor (float macro) const noexcept { return macro < 0.0f ? low : high; }

    static FrenchFilterPreset fromJson (const juce::var& root);
};

/** Values derived from the macro for one preset (before smoothing). */
struct FrenchFilterTargets
{
    float lowpassHz, highpassHz, resonance, q2, driveDb, phaserMix;
    float envOctaves, grit, pumpDb, room, roomLevel;
    float roomLowpassHz, roomHighpassHz; // send filter (open unless filterOnRoom)
    float lowpassWeight, slope24;
};

/** The French Filter wet path:
        2x oversampling → drive → grit → DC blocker → low-pass (12/24 dB) and/or
        high-pass (24 dB), TPT SVFs with saturating resonance, cutoff pushed by
        an envelope follower → phaser → pump → 2x downsampling → room.
    Filter coefficients are computed every 8 oversampled samples and
    interpolated per sample; gains are smoothed per sample (2-pole, 30 ms), so
    knob moves and preset changes never click. */
class FrenchFilterDsp final : public gk::OneKnobEngine
{
public:
    static constexpr float openLowpassHz = 20000.0f, openHighpassHz = 20.0f;
    static constexpr float q1 = 0.541f;          // 24 dB stage 1 (fixed)
    static constexpr float q2Base = 1.307f;      // 24 dB stage 2 without resonance (Butterworth pair)
    static constexpr float q12Base = 0.7071f;    // 12 dB without resonance
    static constexpr float resonanceRelease = 0.15f;
    static constexpr float phaserOnset = 0.8f;
    static constexpr float driveExponent = 1.5f;
    static constexpr float driveHeadroom = 0.5f; // see DECISIONS.md
    static constexpr float maxCutoffRatio = 0.45f;
    static constexpr int controlInterval = 8;    // oversampled samples per control update
    static constexpr int maxChannels = 2;

    /** Targets for a signed macro (-1 = low side fully on, +1 = high side). */
    static FrenchFilterTargets computeTargets (const FrenchFilterPreset& p, float macro) noexcept;

    // OneKnobEngine
    void setPresets (const gk::PresetManager& presets) override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return latency; }
    void setPreset (int index) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;
    float getModulationPhase() const noexcept override { return modPhase.load (std::memory_order_relaxed); }
    void setDiagnosticPassthrough (bool shouldPass) override { passthrough = shouldPass; }

    /** Tests only: disables the sampler grit (a deliberate aliasing
        generator, which would mask parameter clicks in the zipper test). */
    void setGritEnabledForTests (bool enabled) noexcept { gritEnabled = enabled; }

private:
    void controlTick (float macro, double beats) noexcept;
    void applyRoomSettings (const FrenchFilterSide& side) noexcept;

    struct Channel
    {
        gk::DcBlocker dc;
        gk::Grit grit;
        gk::TptSvf lp1, lp2, lp12, hp1, hp2, hp12;
        gk::AllpassPhaser<4> phaser;
        gk::SmoothParam phaserLogHz;
        gk::ControlRamp phaserGain;
        void reset()
        {
            dc.reset();
            grit.reset();
            for (auto* f : { &lp1, &lp2, &lp12, &hp1, &hp2, &hp12 })
                f->reset();
            phaser.reset();
            phaserLogHz.invalidate();
            phaserGain.invalidate();
        }
    };

    std::vector<FrenchFilterPreset> presets;
    int presetIndex = 0;
    const FrenchFilterSide* roomSide = nullptr;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double sampleRate = 44100.0, osRate = 88200.0;
    int latency = 0, numChannels = 2;
    std::uint32_t osCounter = 0;
    bool passthrough = false, gritEnabled = true;

    std::array<Channel, maxChannels> channels;

    // Control rate (smoothed per tick, coefficients interpolated per sample)
    gk::SmoothParam lpLogHz, hpLogHz, q24, q12, envDepth;
    gk::ControlRamp lpG, hpG, k24, k12;

    // Per-sample smoothed gains / mixes
    gk::SmoothParam driveGain, asymmetry, growl, phaserMix, phaserFeedback;
    gk::SmoothParam lowpassWeight, slope24, gritAmount, gritBits, gritLogRatio, pumpDb;
    gk::SmoothParam roomMix, roomLevel, roomLpLogHz, roomHpLogHz; // base rate
    std::array<gk::TptSvf, maxChannels> roomLp, roomHp;              // send filter
    std::vector<float> roomMixBuffer, roomLevelBuffer;
    std::uint32_t baseCounter = 0;
    gk::Saturator saturator;

    // Envelope (squelch) and pump detectors, stereo-linked, oversampled rate
    gk::EnvelopeFollower squelchEnv;
    gk::Pump pump;

    // Room (base rate, after downsampling)
    juce::Reverb room;
    juce::AudioBuffer<float> roomBuffer;

    std::atomic<float> modPhase { 0.0f };
};
} // namespace ff
