#pragma once

#include <groove_core/groove_core.h>

namespace pp
{
/** Sound design of one Pew Pew preset (from its JSON). */
struct PewPreset
{
    enum class Osc { sine, triangle, square, fm };
    Osc osc = Osc::sine;
    float baseHz = 130.8f, tuneOffset = 0.0f;              // note 60 = baseHz (+ tuneOffset semitones)
    float sweepOct = 1.2f, endOct = -0.4f, pitchTauMs = 35.0f; // the "pew"
    float decayMs = 340.0f, clickDb = -26.0f, velCurve = 1.3f, level = 1.0f;
    float lpHz = 2200.0f;                                   // square: 2nd-order low-pass
    float fmRatio = 1.41f, fmIndex = 3.0f, fmTauMs = 120.0f;  // FM
    float roomSize = 0.55f, damping = 0.4f, reverbMax = 0.45f, delayFeedback = 0.40f;

    static PewPreset fromJson (const juce::var& root);
};

/** UI telemetry (lock-free). */
struct PewTelemetry
{
    std::atomic<int> triggerCounter { 0 };      // +1 per note-on
    std::atomic<float> lastTriggerVelocity { 0.0f };
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> pitchPosition { 0.0f };  // newest voice: 0 = fEnd .. 1 = fStart
    std::atomic<std::int64_t> lastTriggerSample { -1 };
    std::atomic<std::int64_t> sampleClock { 0 };
};

/** One drum voice: oscillator + pitch glide ("pew") + exponential decay +
    "laser" click. One-shot: plays to the end of its envelope. */
class PewVoice
{
public:
    struct Start
    {
        const PewPreset* preset = nullptr;
        int note = 60;
        float velocity = 1.0f; // 0..1
        float amount = 0.6f;   // knob, smoothed
        double sampleRate = 48000.0;
        std::uint32_t seed = 1;
        std::int64_t order = 0;
    };

    void start (const Start& s) noexcept;
    /** Fades out over `samples` (stolen voice, panic) then frees itself. */
    void fadeOut (int samples) noexcept;
    float render() noexcept;
    bool isActive() const noexcept { return active; }
    bool isFading() const noexcept { return fadeLeft >= 0; }
    std::int64_t getOrder() const noexcept { return order; }
    float pitchPosition() const noexcept;
    double frequency() const noexcept { return fEnd + glide; }

    static double baseFrequency (const PewPreset& p, int note) noexcept;

private:
    PewPreset::Osc osc = PewPreset::Osc::sine;
    bool active = false;
    std::int64_t order = 0;
    double sr = 48000.0, phase = 0.0, modPhase = 0.0;
    double fStart = 0.0, fEnd = 0.0, glide = 0.0, glideCoeff = 0.0;
    double env = 0.0, decayCoeff = 0.0, fmIndex = 0.0, fmCoeff = 0.0, fmRatio = 1.41;
    int attackLen = 48, n = 0, clickLen = 192, releaseLeft = -1, releaseLen = 240, fadeLeft = -1, fadeLen = 1;
    float gain = 0.0f, clickGain = 0.0f;
    gk::TptSvf lp, clickHp;
    gk::TptSvf::Coeffs lpCoeffs, clickCoeffs;
    gk::testsignals::Rng rng;
};

/** Pew Pew engine: 8-voice pool, ROOM (reverb) and ECHO (tempo delay) sends.

        MIDI ─> 8 voices ─> dry ─┬──────────────────────────────────┐
                                 ├─> 15 ms ─> reverb × room ────────┤
                                 └─> stereo ping-pong delay × echo ─┴─> out (safety clip in the processor)
*/
class PewPewDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int numVoices = 8, numTails = 4;
    static constexpr int roomKnob = 0, echoKnob = 1, divChoice = 0;
    static constexpr double stealFadeSeconds = 0.002, panicFadeSeconds = 0.005, preDelaySeconds = 0.015,
                            maxDelaySeconds = 2.0, delayCrossfadeSeconds = 0.030, smoothSeconds = 0.030;

    /** Delay division (index of the delayDiv choice) in beats. */
    static double divisionBeats (int index) noexcept;

    void setPresets (const gk::PresetManager& presets) override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int index) override { presetIndex = juce::jlimit (0, (int) presets.size() - 1, index); }
    void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) override;
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override { division = n > divChoice ? indices[divChoice] : 1; }
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    PewTelemetry& getTelemetry() noexcept { return telemetry; }
    const PewPreset& getPresetData (int index) const { return presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, index)]; }

private:
    void noteOn (int note, float velocity, float amountNow);
    void panic();
    void clearEffects();
    void renderVoices (float* out, int from, int to);

    std::vector<PewPreset> presets;
    int presetIndex = 0, appliedReverbPreset = -1, division = 1;
    double sampleRate = 48000.0;
    int numChannels = 2;

    std::array<PewVoice, numVoices> voices;
    std::array<PewVoice, numTails> tails; // stolen voices, fading out
    std::int64_t noteOrder = 0;
    std::uint32_t seed = 1;

    const juce::MidiBuffer* midi = nullptr;
    int midiStart = 0, midiCount = 0;
    float room = 0.0f, echo = 0.0f;

    // Effects
    juce::Reverb reverb;
    gk::IntegerDelay preDelay;
    juce::AudioBuffer<float> dry, revBuf;
    std::array<std::vector<float>, 2> delayLine;
    int delayWrite = 0, delayLen = 0;
    double delayTime = 0.0, delayTimeOld = 0.0;
    float delayFade = 1.0f, delayFadeStep = 0.001f;
    gk::TptSvf echoLpL, echoLpR;
    gk::TptSvf::Coeffs echoLp;
    gk::SmoothParam roomMix, echoMix, amountForVoices, feedback;
    std::uint32_t counter = 0;
    bool amountReady = false;

    // Panic / reset with sound: everything fades out, then is cleared
    int panicLeft = -1, panicLen = 240;
    bool soundRecently = false;
    std::int64_t silentSamples = std::int64_t (1) << 40;

    PewTelemetry telemetry;
};
} // namespace pp
