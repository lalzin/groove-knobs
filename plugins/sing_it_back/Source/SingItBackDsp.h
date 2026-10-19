#pragma once

#include <groove_core/groove_core.h>

namespace sib
{
/** Scales, snapping and harmony intervals (pure functions, testable). */
struct Harmony
{
    enum Scale { major, minor, dorian, mixolydian, pentaMajor, pentaMinor, numScales };
    enum Interval { autoThirds, octaves, thirds, fifths, fourths };
    /** Voice roles: who sings what in the crowd. */
    enum Role { unison, harmonyUp, octaveDown, harmonyDown };

    static const std::vector<int>& degrees (int scale);

    /** Target pitch (MIDI, fractional) of a voice for a detected pitch.
        snap on: the detected pitch goes to the nearest scale note, then the
        interval is counted in scale degrees (heptatonic) or snapped into the
        scale (pentatonic); snap off: a fixed number of semitones (Auto: the
        diatonic third of the nearest degree, 3 or 4). */
    static double target (double detectedMidi, int key, int scale, bool snap, int interval, int role);

    /** Nearest scale note (MIDI) to a pitch. */
    static double snapToScale (double midi, int key, int scale);
    static bool inScale (double midi, int key, int scale);
    static juce::String noteName (int pitchClass); // Do, Ré, Mi... (French solfège)
};

/** YIN pitch detector on a decimated (~12 kHz) mono signal. */
class PitchTracker
{
public:
    void prepare (double sampleRate);
    void reset();
    /** Feeds one input sample; returns true when a new estimate is ready. */
    bool push (float x) noexcept;
    float frequency() const noexcept { return f0; }
    bool voiced() const noexcept { return isVoiced; }
    float aperiodicity() const noexcept { return aperiodic; }

private:
    void analyse() noexcept;
    int decimation = 4, counter = 0, write = 0, sinceHop = 0;
    double rate = 12000.0;
    gk::TptSvf aa1, aa2;
    gk::TptSvf::Coeffs c1, c2;
    std::vector<float> ring, frame, diff;
    int window = 384, tauMin = 11, tauMax = 172, hop = 64;
    float f0 = 220.0f, aperiodic = 1.0f;
    bool isVoiced = false;
    std::array<float, 3> recent { 220.0f, 220.0f, 220.0f };
    int recentIndex = 0;
};

/** UI telemetry (lock-free). */
struct SingTelemetry
{
    std::atomic<float> pitchHz { 0.0f }, inputRms { 0.0f };
    std::atomic<bool> voicedFlag { false };
    std::atomic<int> activeChorus { 0 }, dropoutCount { 0 };
    std::atomic<int> degreePitchClass { -1 }; // currentDegreeName: Harmony::noteName (this)
    std::array<std::atomic<float>, 12> voiceGain {};   // per voice, for the silhouettes
    std::array<std::atomic<float>, 12> voiceOffsetMs {};
};

/** Sing It Back: the lead voice untouched (delayed by the latency), plus up to
    12 crowd voices, each a pitch-shifted copy (formants kept) with its own
    timing, tuning, vibrato, level, dropouts, stereo place and reverb send.

        in ─┬──────────── lead, delayed by L ─────────────────────────────┐
            ├─> YIN pitch ─> pitch marks ─> per-voice harmony target       │
            └─> history ─> PSOLA, read 3-40 ms late ─> gain / pan ──────┬─┼─> out
                                                            crowd reverb ┘

    The pitch shifting is pitch-synchronous overlap-add (TD-PSOLA): two-period
    grains taken at the input's pitch marks and laid out at the target period.
    The pitch is exact by construction, and the grains carry the formants
    (DECISIONS.md: why not Signalsmith Stretch). The crowd is computed in
    whole 32-sample chunks on a global grid, so the output does not depend on
    the host's block size. */
class SingItBackDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int maxVoices = 12;
    static constexpr int chunk = 32; // control rate (samples), on a global grid
    // knobs / switches / choices (OneKnobConfig order)
    static constexpr int spreadKnob = 0, humanizeKnob = 1, dropoutsKnob = 2, roomKnob = 3;
    static constexpr int snapSwitch = 0;
    static constexpr int keyChoice = 0, scaleChoice = 1, harmonyChoice = 2, voicesChoice = 3, roomSizeChoice = 4;

    SingItBackDsp();

    /** Latency at a sample rate: ~21 ms (1024 at 44.1 / 48 kHz). */
    static int latencyFor (double sampleRate) noexcept;
    /** Crowd voices heard for a knob position (0..1) and a maximum. */
    static float audibleVoices (float crowd, int maxCount) noexcept;

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return latency; }
    void setPreset (int) override {}
    void setSwitches (const bool* on, int n) override { snap = n > snapSwitch ? on[snapSwitch] : true; }
    void setKnobs (const float* v, int n) override;
    void setChoices (const int* idx, int n) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    SingTelemetry& getTelemetry() noexcept { return telemetry; }

    // Tests: the character of each voice
    struct VoiceInfo { double offsetMs, detuneCents, vibratoHz, vibratoCents, gain, pan, send; int role; };
    VoiceInfo voiceInfo (int i) const;
    /** Tests: only voice `i` is heard (-1 = all), without dropouts. */
    void soloVoice (int i) noexcept { solo = i; }
    /** Tests: the crowd is muted (the lead alone). */
    void muteCrowd (bool m) noexcept { crowdMuted = m; }

private:
    struct Voice
    {
        std::vector<float> acc; // overlap-add ring (absolute output time & mask)
        double nextGrain = 0.0; // output time of the next grain centre
        bool running = false;
        Harmony::Role role = Harmony::unison;
        double offsetMs = 10.0, driftPhase = 0.0, driftRate = 0.2;
        double detune = 0.0, vibHz = 5.5, vibCents = 12.0, vibPhase = 0.0;
        float baseGain = 0.8f, pan = 0.0f, send = 0.8f;
        float semis = 0.0f;              // smoothed transposition
        float audible = 0.0f;            // crowd count fade (60 ms)
        float drop = 1.0f, dropTarget = 1.0f; int dropLeft = 0; // dropouts
    };
    struct Mark { std::int64_t pos; float period; }; // an analysis pitch mark (input time)
    void designVoices();
    void makeMarks (std::int64_t available);
    const Mark* findMark (double u, std::int64_t available) const noexcept;
    float historyAt (double t) const noexcept;
    void processChunk (std::int64_t chunkStart, float crowdNow);

    std::array<Voice, maxVoices> voices;
    double sampleRate = 48000.0;
    int numChannels = 2, latency = 0, halfMax = 512, gateLag = 960;
    std::int64_t clock = 0;

    // parameters
    float spread = 0.6f, humanize = 0.5f, dropouts = 0.3f, room = 0.4f;
    int key = 0, scale = 0, harmony = 0, voicesMax = 8, roomSize = 1;
    bool snap = true;
    int lastKey = -1, lastScale = -1, lastHarmony = -1, retuneLeft = 0;
    bool lastSnap = true;

    // signal
    PitchTracker tracker;
    float detectedMidi = 57.0f, gate = 0.0f, gateCoeff = 0.001f, rms = 0.0f, rmsCoeff = 0.01f;
    std::vector<float> history, gateHistory; // mono input and voicing gate (absolute time & mask)
    std::int64_t mask = 0;
    std::array<Mark, 256> marks {};
    std::int64_t markCount = 0;
    double nextMark = 0.0;
    std::array<float, chunk> outL {}, outR {}, revL {}, revR {}; // the crowd of the next chunk
    gk::IntegerDelay leadDelay;
    juce::AudioBuffer<float> lead;
    juce::Reverb reverb;
    int appliedRoomSize = -1;
    gk::SmoothParam crowdLevel, width, roomMix;
    juce::Random random { 2024 };
    int solo = -1;
    bool crowdMuted = false;
    float crowdSmoothed = 0.0f;

    SingTelemetry telemetry;
};
} // namespace sib
