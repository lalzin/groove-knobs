#pragma once

#include <groove_core/groove_core.h>

namespace bw
{
/** Sound design of one Backwash preset (from its JSON). */
struct BackwashPreset
{
    float beats = 1.0f, riseSharpness = 4.0f, toneStartHz = 1500.0f, toneEndHz = 9000.0f;
    float focus = 0.9f, washHpfHz = 200.0f, maxWetDb = -3.0f;

    static BackwashPreset fromJson (const juce::var& root);
};

/** A reverse-reverb impulse response, ready for the convolver. */
struct BackwashIr
{
    std::unique_ptr<gk::PartitionedConvolver::Kernel> kernel;
    std::array<std::vector<float>, 2> taps; // the kernel in the time domain (LIVE rendering)
    int swellSamples = 0;
    bool clamped = false;
    int preset = -1;
    bool longSwell = false;
    double bpm = 0.0;
};

/** LIVE mode: one cycle (bar) of wet signal, rendered in the background from
    the memory, read by musical position. Channels: swell L, R, and the
    SHIMMER octave-up part L, R (mixed in real time, it follows the knob). */
struct BackwashWetCycle
{
    int length = 0;
    double cycleBeats = 4.0;
    std::array<std::vector<float>, 4> channels;
};

/** UI telemetry specific to Backwash (lock-free). */
struct BackwashTelemetry
{
    std::atomic<int> samplesToNextHit { -1 }; // -1: no hit coming within the lookahead
    std::atomic<float> nextHitStrength { 0.0f };
    std::atomic<int> swellSamples { 0 };
    std::atomic<bool> swellClamped { false };
    std::atomic<bool> liveMemoryReady { false }; // LIVE: something has been captured
};

/** Backwash: a real-time reverse reverb, locked to the tempo.

        in ─┬──────────────────── dry, delayed by L (2 s lookahead) ───────────────────┐
            └─> transient focus ─> HPF ─> [SHIMMER: +1 octave] ─> convolution with a ├─> out
                "reversed" IR (rising noise, darker at the start, cut dead on the    │
                hit) ─> tone LPF ─> width (M/S) ─> wet gain ──────────────────────────┘

    The IR has length L and its swell sits at its very end, so the swell of a
    hit ends exactly when the delayed dry plays that hit. L never changes
    (tempo, preset, chips): only the IR is regenerated — off the audio thread
    in real time, synchronously when rendering offline — and crossfaded over
    50 ms. */
class BackwashDsp final : public gk::OneKnobEngine
{
public:
    static constexpr double lookaheadSeconds = 2.0;
    static constexpr double irFadeSeconds = 0.050;
    static constexpr double tempoDebounceSeconds = 0.200;
    static constexpr int longToggle = 0, shimmerToggle = 1;
    static constexpr int liveSwitch = 0, lockSwitch = 1;
    /** LIVE memory: up to this long (one bar; half bars below ~30 BPM in 4/4). */
    static constexpr double memoryMaxSeconds = 8.0;

    BackwashDsp();
    ~BackwashDsp() override;

    /** Builds the IR for a preset / swell length / tempo (pure, allocates). */
    static BackwashIr buildIr (const BackwashPreset& p, int presetIndex, bool longSwell, double bpm, double sampleRate, int lookahead,
                               int partitionSize);

    /** Renders one cycle of LIVE wet signal: the memory (one cycle of the mono
        input, looped) through the same send chain and IR as the real-time
        path, aligned so the swell of a hit ends on that hit. Allocates. */
    static std::unique_ptr<BackwashWetCycle> renderWetCycle (const float* memory, int length, double cycleBeats, const BackwashIr& ir,
                                                             const BackwashPreset& p, double sampleRate, int lookahead, int partitionSize);

    /** Partition size used at a sample rate (latency of the convolver). */
    static int partitionSizeFor (double sampleRate);
    /** Shimmer grain (= send delay) at a sample rate. */
    static int grainSamplesFor (double sampleRate);

    // OneKnobEngine
    void setPresets (const gk::PresetManager& presets) override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return liveMode ? 0 : lookahead; }
    int getMaxLatencySamples() const override { return lookahead; }
    int latencyForSwitches (const bool* on, int n) const override { return n > liveSwitch && on[liveSwitch] ? 0 : lookahead; }
    void setSwitches (const bool* on, int n) override
    {
        const bool live = n > liveSwitch && on[liveSwitch];
        renderWanted = renderWanted || (live && ! liveMode);
        liveMode = live;
        locked = n > lockSwitch && on[lockSwitch];
    }
    bool isLiveMode() const noexcept { return liveMode; }

    /** Message thread: the LIVE memory (one cycle of the mono input, by
        musical position), empty if nothing was captured. */
    std::vector<float> copyMemory() const;
    /** Message thread: restores a saved memory (resampled to the current
        tempo on the audio thread). */
    void loadMemory (std::vector<float> data);
    void setPreset (int index) override { presetIndex = juce::jlimit (0, (int) presets.size() - 1, index); }
    void setToggleValues (const float* const* perSample, int numToggles) override;
    void setNonRealtime (bool isNonRealtime) noexcept override { nonRealtime = isNonRealtime; }
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    BackwashTelemetry& getTelemetry() noexcept { return telemetry; }
    const BackwashPreset& getPresetData (int index) const { return presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, index)]; }

private:
    class Builder;
    void requestIr (int preset, bool longSwell, double bpm);
    void installIr (std::unique_ptr<BackwashIr> ir, bool fade);
    void retire (std::unique_ptr<BackwashIr> ir);
    void updateHits (int numSamples);
    void updateMemoryLength (const gk::BlockTiming& timing);
    void takePendingMemory();
    void requestRender();
    void installCycle (std::unique_ptr<BackwashWetCycle> cycle);
    void retireCycle (std::unique_ptr<BackwashWetCycle> cycle);
    static int cycleIndex (double cyclePosition, int length) noexcept;
    static void resampleInto (const float* src, int srcLen, float* dst, int dstLen) noexcept;

    std::vector<BackwashPreset> presets;
    int presetIndex = 0;
    double sampleRate = 44100.0;
    int lookahead = 88200, partitionSize = 512, numChannels = 2;
    std::atomic<bool> nonRealtime { false };

    // IR management
    std::unique_ptr<BackwashIr> currentIr, previousIr, queuedIr;
    std::unique_ptr<Builder> builder;
    int wantedPreset = -1;
    bool wantedLong = false;
    double activeBpm = 120.0, candidateBpm = 120.0;
    std::int64_t candidateSince = 0, sampleClock = 0;

    // Toggles (per sample, from the processor)
    const float* toggleData[2] { nullptr, nullptr };
    int numToggleData = 0;

    // Signal path
    gk::PartitionedConvolver convolver;
    gk::IntegerDelay dryDelay;
    juce::AudioBuffer<float> dryBuffer;
    gk::EnvelopeFollower fastEnv, slowEnv;
    float rectified = 0.0f, rectifyCoeff = 0.01f;
    float limiterEnv = 0.0f, limiterAttack = 0.0f, limiterRelease = 0.0f;
    static constexpr float wetCeiling = 0.3981072f; // -8 dBFS
    gk::TptSvf hpf;
    gk::TptSvf::Coeffs hpfCoeffs;
    gk::ModDelay shimmerLine;
    double shimmerPhase = 0.0;
    int grainSamples = 1920;
    std::array<gk::TptSvf, 2> toneLpf;
    gk::SmoothParam focus, hpfLogHz, wetGain, toneLogHz, width, shimmerMix;
    std::uint32_t counter = 0;

    // Hit detection for the UI (input domain, heard L samples later)
    gk::EnvelopeFollower hitFast, hitSlow;
    int hitHoldoff = 0;
    std::array<std::pair<std::int64_t, float>, 64> hits {};
    int hitCount = 0;
    std::int64_t inputClock = 0;

    // LIVE mode (zero latency): the wet path is fed from a memory of the last
    // cycle (one bar), read `lookahead` ahead of the musical position, so each
    // swell lands on the same hit one cycle later. LOCK stops the recording.
    bool liveMode = false, locked = false, memoryHasContent = false;
    std::array<std::vector<float>, 2> memoryBuffers; // fixed capacity, swapped by index (never reallocated)
    std::atomic<int> memoryActive { 0 }, memoryLength { 0 };
    int memoryCapacity = 0;
    double memoryCycleBeats = 0.0;
    std::atomic<double> memoryCycleBeatsShared { 4.0 }; // for the builder thread
    std::atomic<std::vector<float>*> pendingMemory { nullptr }, spentMemory { nullptr };
    std::atomic<BackwashIr*> renderIr { nullptr }; // the IR the LIVE renders use (owned by currentIr)
    std::unique_ptr<BackwashWetCycle> cycle, previousCycle;
    float cycleFade = 1.0f, cycleFadeStep = 0.001f;
    gk::LinearSmoother liveGate; // LIVE: wet only while the transport plays
    int lastWrite = -1;
    bool renderWanted = false;

    BackwashTelemetry telemetry;
};
} // namespace bw
