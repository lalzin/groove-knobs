#pragma once

#include <groove_core/groove_core.h>

namespace bt
{
/** Sound design of one Boogie Trio preset (from its JSON). */
struct BoogieTrioPreset
{
    float baseDelayMs = 8.0f, chorusHz = 0.6f, chorusDepthMs = 2.5f, vibratoHz = 6.0f, vibratoDepthMs = 0.15f;
    float maxMix = 0.6f, wetHpfHz = 150.0f;

    // Extra effects (all 0 = off), growing with the knob from fxStart
    float fxStart = 0.3f;
    float feedback = 0.0f;                             // delay feedback (jet / flanger colour)
    float driftMs = 0.0f;                              // tape wow: slow random delay drift
    float tremoloDepth = 0.0f, tremoloBeats = 0.5f, tremoloStereoDeg = 180.0f; // tempo-synced tremolo / autopan
    float gateDepth = 0.0f, gateStepsPerBeat = 4.0f, gateDuty = 0.55f;         // tempo-synced chop
    float roomMix = 0.0f, roomSize = 0.6f, roomDamping = 0.5f;                  // reverb on the wet
    float toneDb = 0.0f;                               // high-shelf tilt of the wet (+ bright / - dark)
    float phaserMix = 0.0f, phaserBars = 2.0f, phaserFeedback = 0.4f;           // tempo-synced phaser on the wet

    static BoogieTrioPreset fromJson (const juce::var& root);
};

/** Values derived from the knob (before smoothing). */
struct BoogieTrioTargets
{
    float chorusDepthMs, vibratoDepthMs, mix, width;
    float fx; // 0..1 amount of the extra effects
};

/** The Boogie Trio wet path: a 70s string-machine triple chorus.

    Stereo output is "Wider" style (Alan, 2026-10-13): the centre stays the
    dry signal (phase and level untouched), the chorus voices go to the side
    channel, so the mono sum gives back the input exactly.

        in ─┬──────────────────────────────── dry (low end stays here, mono and clean) ─┐
            └─> 4th-order HPF ─> mono ─> 3 BBD-like voices (Hermite delays, chorus +    ├─> out
                vibrato LFOs at 0° / 120° / 240°) ─> [VINTAGE: 7 kHz LPF, soft tanh,     │
                input-gated hiss] ─> L / C / R panning ──────────────────────────────────┘

    Extra effects per preset (feedback, tape drift, tempo tremolo / gate, wet
    reverb, tone, wet phaser) grow with the knob from the preset's fxStart.

    out = x + mix · (voices − hpf(x)): with mix 0 the output is exactly the
    input, and the part of the spectrum below the HPF never leaves the dry
    path. No oversampling, no latency. */
class BoogieTrioDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int numVoices = 3;
    static constexpr int maxChannels = 2;
    static constexpr int controlInterval = 8;
    static constexpr int vintageToggle = 0, wideToggle = 1;
    static constexpr float voiceOffsetMs[numVoices] { -0.3f, 0.0f, 0.3f };
    static constexpr float widePan = 0.85f, narrowPan = 0.35f; // mono output only (legacy panning constants)
    static constexpr float sideGain = 1.6f;    // "Wider"-style side level at full knob (pronounced chorus)
    static constexpr float narrowSide = 0.55f; // side level without WIDE
    static constexpr float vintageLowpassHz = 7000.0f;
    static constexpr float vintageDriveDb = 3.0f;
    static constexpr float hissDbfs = -72.0f;

    static BoogieTrioTargets computeTargets (const BoogieTrioPreset& p, float amount) noexcept;

    // OneKnobEngine
    void setPresets (const gk::PresetManager& presets) override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int index) override { presetIndex = juce::jlimit (0, (int) presets.size() - 1, index); }
    void setToggleValues (const float* const* perSample, int numToggles) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;
    float getModulationPhase() const noexcept override { return modPhase.load (std::memory_order_relaxed); }

    /** Tests only: mutes the VINTAGE hiss (noise would mask clicks in the
        anti-zipper test; the hiss has its own test). */
    void setHissEnabledForTests (bool enabled) noexcept { hissEnabled = enabled; }

    /** Vibrato LFO phase (0..1), for the UI (left hand on the neck). */
    float getVibratoPhase() const noexcept { return vibPhaseUi.load (std::memory_order_relaxed); }

private:
    void controlTick (float amount) noexcept;
    void applyRoomSettings() noexcept;

    std::vector<BoogieTrioPreset> presets;
    int presetIndex = 0;
    double sampleRate = 44100.0;
    int numChannels = 2;
    std::uint32_t counter = 0;

    const float* toggleData[2] { nullptr, nullptr };
    int numToggleData = 0;

    gk::ModDelay line;
    std::array<gk::TptSvf, maxChannels> wetHpf, wetHpf2, vintageLpf;
    std::array<gk::PinkNoise, maxChannels> hiss { gk::PinkNoise (101u), gk::PinkNoise (202u) };
    gk::EnvelopeFollower inputEnv;
    gk::Saturator saturator;

    double chorusPhase = 0.0, vibratoPhase = 0.0;
    gk::SmoothParam baseDelayMs, chorusDepthMs, vibratoDepthMs, chorusHz, vibratoHz, mix, width, hpfLogHz;

    // Extra effects
    gk::SmoothParam fxAmount, feedback, drift, tremolo, gate, roomMix, toneDb, phaserMix;
    float feedbackSample = 0.0f;
    std::array<float, numVoices> driftState {}, driftTarget {};
    int driftCountdown = 0;
    gk::testsignals::Rng driftRng { 4242u };
    std::array<gk::TptSvf, maxChannels> toneSplit;
    gk::TptSvf::Coeffs toneCoeffs;
    std::array<gk::AllpassPhaser<4>, maxChannels> phaser;
    juce::Reverb room;
    int roomPreset = -1;
    juce::AudioBuffer<float> wetBuffer, highBuffer, roomBuffer;
    gk::TptSvf::Coeffs hpfCoeffs, hpfCoeffs2, lpfCoeffs;
    float hissGain = 0.0f;
    bool hissEnabled = true;

    std::atomic<float> modPhase { 0.0f }, vibPhaseUi { 0.0f };
};
} // namespace bt
