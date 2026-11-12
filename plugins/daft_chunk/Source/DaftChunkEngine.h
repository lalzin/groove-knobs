#pragma once
// DAFT CHUNK - the sound: 16 voices playing the slices (slice 1 on C1),
// each with its envelope (ATTACK, DECAY, SUSTAIN, RELEASE), its low-pass
// (FILTER, RESO), its pitch, and the slice's own settings (pitch, gain,
// filter, reverse, fine start / end). COUPE: a new note cuts what plays (an
// MPC pad); POLY: they overlap. ONE SHOT: the slice plays to its end; GATE:
// releasing the key starts the release.
//
// The kit (the sample, its slices and their settings) comes from the message
// thread through an atomic pointer; the processor keeps it alive (see
// DaftChunkProcessor). No allocation here.

#include "SampleLoader.h"
#include "Generator.h"
#include <groove_core/groove_core.h>

namespace daftchunk
{
class DaftChunkEngine final : public gk::OneKnobEngine
{
public:
    static constexpr int baseNote = 36; // slice 1 on C1 (MIDI 36)
    static constexpr int numVoices = 16;

    /** The host parameters (OneKnobConfig::knobs / choices, in this order). */
    enum Knob { attackKnob, decayKnob, sustainKnob, releaseKnob, pitchKnob, cutoffKnob, resoKnob, velocityKnob, volumeKnob, swingKnob, numKnobs };
    enum Choice { modeChoice, playChoice, syncChoice, styleChoice, captureChoice, numChoices };
    enum Mode { cut = 0, poly = 1 };
    enum Play { oneShot = 0, gate = 1 };
    /** LIBRE: the sample's own speed; TEMPO: at the host's tempo, same pitch
        (the slices stretched); VINYLE: at the host's tempo by speed (the pitch follows). */
    enum Sync { free = 0, tempo = 1, vinyl = 2 };

    /** The processor, before each block: whether the host gives a tempo (not the standalone app). */
    void setHostTempoKnown (bool known) noexcept { hostTempoKnown = known; }

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return 0; }
    void setPreset (int) override {}
    void setMidi (const juce::MidiBuffer& midi, int start, int numSamples) override;
    void setKnobs (const float* values, int n) override;
    void setChoices (const int* indices, int n) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    /** Message thread: the kit to play from the next block (null: silence). */
    void setKit (const Kit* k) noexcept { kit.store (k, std::memory_order_release); }
    /** The kit the audio thread is using (the processor frees retired kits only when safe). */
    const Kit* kitInUse() const noexcept { return inUse.load (std::memory_order_acquire); }

    /** Message thread: the idea to play, locked to the host's bars (null: none). */
    void setSequence (const Pattern* p) noexcept { sequence.store (p, std::memory_order_release); }
    const Pattern* sequenceInUse() const noexcept { return seqInUse.load (std::memory_order_acquire); }

    /** MIDI CAPTURE: the idea's notes of the last processBlock (positions in the host's block). */
    struct OutEvent { int sample; int note; float velocity; bool on; };
    int numOutEvents() const noexcept { return numOut; }
    const OutEvent& outEvent (int i) const noexcept { return outEvents[(size_t) i]; }
    void clearOutEvents() noexcept { numOut = 0; }

    /** The UI: play slice `index` now (a click on it), as a short note. */
    void preview (int index) noexcept { previewRequest.store (index, std::memory_order_relaxed); }

    /** The UI: the slice last started (-1: none playing) and where in it (0..1). */
    struct Telemetry
    {
        std::atomic<int> slice { -1 };
        std::atomic<float> position { 0.0f };
        std::atomic<double> hostBpm { 0.0 }; // 0: the host gives none
        std::atomic<int> step { -1 };         // the idea's step playing (-1: none)
    };
    const Telemetry& getTelemetry() const noexcept { return telemetry; }

private:
    enum class Stage { off, attack, decay, sustain, release };
    struct Voice
    {
        int slice = -1, note = -1;
        double pos = 0.0, first = 0.0, last = 0.0; // source samples: where, and the played range [first, last)
        bool reverse = false, held = false;
        float gain = 0.0f, env = 0.0f;
        Stage stage = Stage::off;
        int cutLeft = -1;      // >= 0: being cut (fading out over 3 ms)
        int previewLeft = -1;  // a preview or an idea's step: note-off after this many samples
        float extraPitch = 0.0f; // an idea's step: on top of the slice's own
        gk::TptSvf filter[2];
        unsigned age = 0;
        bool active() const noexcept { return stage != Stage::off; }
    };
    void noteOn (int note, float velocity, int previewSamples = -1);
    /** Starts slice `index` (an idea's step, a preview: `note` -1). */
    void startSlice (int index, float velocity, int note, int gateSamples, float extraPitch, bool flipReverse);
    void cutAll() noexcept;
    /** The idea's step events in this block, in time order (no rendering). */
    void collectSequence (int numSamples, const gk::BlockTiming& timing);
    struct SeqEvent { int at; const Step* step; int gate; };
    std::array<SeqEvent, 256> seqEvents {};
    int numSeqEvents = 0;
    // MIDI CAPTURE
    void emit (int sample, int note, float velocity, bool on) noexcept;
    void captureNote (int at, int slice, float velocity, int gate) noexcept;
    void advanceCapture (int numSamples) noexcept;
    std::array<OutEvent, 512> outEvents {};
    int numOut = 0;
    struct PendingOff { int note = -1; int left = 0; };
    std::array<PendingOff, 64> pendingOffs {};
    bool captureOn = false;
    void noteOff (int note);
    void releaseVoice (Voice& v) noexcept;
    void render (Voice& v, float* const* out, int numChannels, int from, int to);

    std::atomic<const Kit*> kit { nullptr }, inUse { nullptr };
    std::atomic<const Pattern*> sequence { nullptr }, seqInUse { nullptr };
    std::atomic<int> previewRequest { -1 };
    Telemetry telemetry;
    const Kit* current = nullptr;
    std::array<Voice, numVoices> voices {};
    int lead = -1;                  // the voice last started (the UI's playhead)
    unsigned ageCounter = 0;
    const juce::MidiBuffer* midi = nullptr; // the chunk's MIDI (setMidi), valid during processWet
    int midiStart = 0, midiCount = 0;
    double sampleRate = 44100.0;
    int cutFade = 132;              // 3 ms
    std::array<float, numKnobs> knobs { 0.0005f, 0.3f, 1.0f, 0.05f, 0.0f, 20000.0f, 0.0f, 0.7f, 0.0f, 0.0f };
    int mode = cut, play = gate, sync = tempo;
    bool hostTempoKnown = false;
    double rate = 1.0; // the host's tempo / the kit's (TEMPO, VINYLE), this block
    double cutoffSmoothed = 20000.0;
};
} // namespace daftchunk
