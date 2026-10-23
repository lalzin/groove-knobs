#pragma once

#include <groove_core/groove_core.h>
#include "FunkMap.h"
#include "SlapperAmp.h"
#include "SlapperVoice.h"
#include "SynthVoice.h"

namespace slapper
{
/** UI telemetry (lock-free). */
struct Telemetry
{
    std::atomic<int> hitCounter { 0 };       // +1 per played note
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<int> lastKind { 0 };         // 0 doigt, 1 slap, 2 pop, 3 ghost
    std::atomic<float> level { 0.0f };       // output envelope
    std::atomic<int> activeVoices { 0 };
};

/** The playing engine: MIDI -> 6 voices (mono legato or poly) -> the amp.
    ENGINE (set by each preset, and by hand): STRING (the modelled bass) or a
    synth (ANALOG, FM, PULSE, SynthVoice.h); a voice slot holds one of each,
    the one not playing is let go when the engine changes (new notes only:
    a ringing note keeps its engine).

    MONO (default): one string. With SLIDE up, a note played while another is
    held slides to it (no new pluck; the time grows with SLIDE and the
    interval) and releasing it slides back to the note still held; SLIDE 0:
    every note is plucked. A key pressed again while still held is always
    plucked again (each press counted: its first release no longer cuts the
    second — MIDI clips overlap the same note often). A note under GHOST's
    velocity is a ghost (a muted, pitchless thump). POLY: up to 6 strings,
    each note plucks its own.
    HUMAIN: each note a little late (up to 8 ms), its velocity (±6 %) and its
    pluck point (±3 %) varied, drawn from each voice's own seed. */
class SlapperDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int numVoices = 6;
    // the small knobs, in OneKnobConfig order
    enum Knob { attackKnob, pickKnob, stringKnob, bodyKnob, muteKnob, driveKnob, toneKnob, humanKnob, levelKnob, slideKnob, ghostKnob, numKnobs };
    static constexpr int modeChoice = 0;   // 0 mono, 1 poly
    static constexpr int engineChoice = 1; // Engine order: string, analog, fm, pulse
    static constexpr double maxHumanSeconds = 0.008;

    /** Each preset's level trim ("trimDb" in its JSON, calibrated: every
        preset at the same loudness, K-weighted, at VOL 0 dB). */
    void setPresets (const gk::PresetManager& presets) override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int index) override { trimDb = trims.empty() ? 0.0f : trims[(size_t) juce::jlimit (0, (int) trims.size() - 1, index)]; }
    /** A preset's engine ("engine" in its JSON: string, analog, fm, pulse), as the ENGINE choice index. */
    static int engineIndexOf (const juce::var& presetJson);
    void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) override;
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override
    {
        poly = n > modeChoice && indices[modeChoice] == 1;
        engine = n > engineChoice ? (Engine) juce::jlimit (0, 3, indices[engineChoice]) : Engine::string;
    }
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    Telemetry& getTelemetry() noexcept { return telemetry; }

    /** Glide time for an interval (semitones): 30 ms for a step, 80 ms an octave and more. */
    static double glideSeconds (double semitones) noexcept { return 0.030 + 0.050 * std::min (1.0, std::fabs (semitones) / 12.0); }

private:
    struct Event
    {
        std::int64_t due = 0;
        int note = 0;
        float velocity = 0.0f; // 0: note off
        bool used = false;
    };
    void handle (const juce::MidiMessage& m, float funkNow);
    void schedule (int note, float velocity, double delaySeconds);
    void play (int note, float velocity);
    void release (int note);
    NoteParams paramsFor (int voice, float velocity) noexcept;
    SynthParams synthParamsFor (int voice) noexcept;
    void start (int slot, double hz, float velocity);
    void slide (int slot, double hz);
    void letGo (int slot);
    bool slotActive (int slot) const noexcept { return voices[(size_t) slot].isActive() || synths[(size_t) slot].isActive(); }
    bool slotHeld (int slot) const noexcept { return voices[(size_t) slot].isHeld() || synths[(size_t) slot].isHeld(); }
    double slotHz (int slot) const noexcept { return slotEngine[(size_t) slot] == Engine::string ? voices[(size_t) slot].currentHz() : synths[(size_t) slot].currentHz(); }
    float humanRandom (int voice) noexcept; // -1..1, the voice's own sequence

    double sr = 48000.0;
    std::array<Voice, numVoices> voices;
    std::array<SynthVoice, numVoices> synths;
    std::array<Engine, numVoices> slotEngine {};
    Engine engine = Engine::string;
    std::array<int, numVoices> voiceNote {};
    std::array<std::int64_t, numVoices> voiceOrder {};
    std::array<std::uint32_t, numVoices> voiceSeed {};
    std::int64_t order = 0, clock = 0;
    // mono: the held keys, latest last
    std::array<int, 16> heldKeys {};
    int numHeld = 0;
    std::array<Event, 64> pending {};
    std::array<double, 128> noteDelay {}; // the delay a note was played with (its note-off gets the same)
    std::array<int, 128> pressed {};      // presses of each key not yet released
    std::uint32_t timingSeed = 0x1234567u;
    const juce::MidiBuffer* midi = nullptr;
    int midiStart = 0, midiCount = 0;
    bool poly = false;
    float knobs[numKnobs] { 0.5f, 0.15f, 0.4f, 0.3f, 0.0f, 0.3f, 0.0f, 0.2f, 0.0f, 0.0f, 0.5f };
    float funk = 0.5f, trimDb = 0.0f;
    std::vector<float> trims;
    double bendRatio = 1.0;
    Amp amp;
    /** Two one-poles in a row (~20 ms): follows a host's block-by-block
        automation steps without a kink in the slope (a kink is heard on a
        bright synth as a faint crackle). */
    struct Smooth2
    {
        double c = 0.0, a = 0.0, b = 0.0, target = 0.0;
        void prepare (double rate, double seconds) { c = 1.0 - std::exp (-1.0 / (seconds * 0.5 * rate)); }
        void snap (double v) { a = b = target = v; }
        void setTarget (double v) { target = v; }
        float next() noexcept
        {
            a += c * (target - a);
            b += c * (a - b);
            return (float) b;
        }
    } driveSmooth, toneSmooth, volume;
    float envelope = 0.0f;
    Telemetry telemetry;
};
} // namespace slapper
