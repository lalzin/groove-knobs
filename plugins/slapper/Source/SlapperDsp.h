#pragma once

#include <groove_core/groove_core.h>
#include "FunkMap.h"
#include "SlapperAmp.h"
#include "SlapperVoice.h"

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

/** The playing engine: MIDI -> 6 strings (mono legato or poly) -> the amp.

    MONO (default): one string. A note played while another is held slides
    to it (no new pluck, 30-80 ms by the interval); releasing it slides back
    to the note still held. A note with velocity < 0.15 is a ghost (a muted,
    pitchless thump). POLY: up to 6 strings, each note plucks its own.
    HUMAIN: each note a little late (up to 8 ms), its velocity (±6 %) and its
    pluck point (±3 %) varied, drawn from each voice's own seed. */
class SlapperDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int numVoices = 6;
    // the small knobs, in OneKnobConfig order
    enum Knob { attackKnob, pickKnob, stringKnob, bodyKnob, muteKnob, driveKnob, toneKnob, humanKnob, levelKnob, numKnobs };
    static constexpr int modeChoice = 0; // 0 mono, 1 poly
    static constexpr double maxHumanSeconds = 0.008;

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int) override {}
    void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) override;
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override { poly = n > modeChoice && indices[modeChoice] == 1; }
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
    float humanRandom (int voice) noexcept; // -1..1, the voice's own sequence

    double sr = 48000.0;
    std::array<Voice, numVoices> voices;
    std::array<int, numVoices> voiceNote {};
    std::array<std::int64_t, numVoices> voiceOrder {};
    std::array<std::uint32_t, numVoices> voiceSeed {};
    std::int64_t order = 0, clock = 0;
    // mono: the held keys, latest last
    std::array<int, 16> heldKeys {};
    int numHeld = 0;
    std::array<Event, 64> pending {};
    std::array<double, 128> noteDelay {}; // the delay a note was played with (its note-off gets the same)
    std::uint32_t timingSeed = 0x1234567u;
    const juce::MidiBuffer* midi = nullptr;
    int midiStart = 0, midiCount = 0;
    bool poly = false;
    float knobs[numKnobs] { 0.5f, 0.15f, 0.4f, 0.3f, 0.0f, 0.3f, 0.0f, 0.2f, 0.0f };
    float funk = 0.5f;
    double bendRatio = 1.0;
    Amp amp;
    gk::LinearSmoother driveSmooth, toneSmooth, volume;
    float envelope = 0.0f;
    Telemetry telemetry;
};
} // namespace slapper
