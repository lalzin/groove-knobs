#pragma once
// DAFT CHUNK - the analysis of a sample (spec 1): the tempo, beat 1, the key,
// the transients; then the slices (TRANSIENTS, GRID, MIXED), each cut moved
// to the nearest zero crossing, each slice labelled (KICK, SNARE, HAT, VOICE,
// TONAL, NOISE).
//
// Two passes:
//   analyse()  the slow one (STFT, onsets, tempo, key: ~100 ms for a minute
//              of audio), on a background thread, with a progress value and
//              a cancel flag. Its result does not depend on the slicing
//              settings.
//   slice()    the fast one (a few ms): the cuts for the settings (mode,
//              division, sensitivity, the user's tempo / beat 1), then the
//              labels. Run again whenever a setting moves.
// Nothing here touches the audio thread. JUCE only for the FFT.

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace daftchunk
{
enum class SliceType { kick, snare, hat, voice, tonal, noise };
inline constexpr int numSliceTypes = 6;
const char* typeName (SliceType t) noexcept; // "KICK", "SNARE", "HAT", "VOIX", "TONAL", "BRUIT"

enum class SliceMode { transients, grid, mixed };

struct Key
{
    int tonic = 0;        // 0 = C .. 11 = B
    bool minor = false;
    float confidence = 0; // correlation with the profile, 0..1
    juce::String name() const; // "A min", "F# maj"
    /** The scale's pitch classes (natural minor / major), tonic first. */
    std::array<int, 7> scale() const noexcept;
};

/** The slicing settings: what the user chooses. bpm / firstBeat < 0: the analysis's. */
struct SliceSettings
{
    SliceMode mode = SliceMode::transients;
    int division = 16;          // GRID / MIXED: 4, 8 or 16 (1/4, 1/8, 1/16)
    float sensitivity = 0.5f;   // TRANSIENTS / MIXED: 0 = only the strongest hits .. 1 = every little one
    double bpm = -1.0;
    double firstBeat = -1.0;    // in samples
};

struct Slice
{
    int64_t start = 0, end = 0; // [start, end) in samples
    SliceType type = SliceType::noise;
    float rootMidi = -1.0f;     // tonal / voice: the pitch at the start (MIDI, fractional), else -1
    float peak = 0.0f;          // the slice's peak (0..1+)
};

struct Analysis
{
    static constexpr double maxSeconds = 60.0;
    static constexpr int maxSlices = 64;

    double sampleRate = 44100.0;
    int64_t numSamples = 0;
    std::vector<float> mono;      // (L + R) / 2: the cuts, the labels

    // onsets (spectral flux), one value per hop
    int hop = 512;
    double frameRate = 0.0;       // frames per second
    std::vector<float> flux, lowFlux, highFlux; // normalised (0..~1); low: below 150 Hz, high: above 1.5 kHz
    struct Onset { int64_t sample; float strength; }; // every candidate, with its prominence (0..1)
    std::vector<Onset> onsets;

    // tempo and beat 1
    double bpm = 120.0;           // the estimate (80..160 unless the file says otherwise)
    double tempoConfidence = 0.0;
    bool loopLocked = false;      // the tempo was snapped so the file is a whole number of beats
    double firstBeat = 0.0;       // samples: beat 1 of a bar (the first one at or after the start)

    Key key;
    std::array<float, 12> chroma {};

    // the waveform, for the display: min / max per bucket
    static constexpr int numPeaks = 4096;
    std::vector<float> peakMin, peakMax;

    double seconds() const noexcept { return (double) numSamples / sampleRate; }
    double beatSamples (double tempo) const noexcept { return sampleRate * 60.0 / tempo; }
};

/** The slow pass. `channels`: 1 or 2. Cut to 60 s. `progress` (0..1) and
    `cancel` may be null; cancelled: returns an empty Analysis (numSamples 0). */
Analysis analyse (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate,
                  std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

/** The fast pass: the slices for these settings (1 to 64, covering the start
    of the file to its end, or to the 64th cut). */
std::vector<Slice> slice (const Analysis& a, const SliceSettings& s);

/** The slices' boundaries before the labels: sorted, the first 0, the last
    the end (the file's, or the 64th slice's). */
std::vector<int64_t> cuts (const Analysis& a, const SliceSettings& s);

/** Slices from given boundaries (the user's, moved by hand; the first 0, the
    last the end): sorted, zero-crossing snapped, too-close ones merged,
    labelled. */
std::vector<Slice> slicesFromCuts (const Analysis& a, std::vector<int64_t> cutPositions);

/** The attack at a slice's start, from the analysis's flux (0..~1): in a
    full mix everything sustains, so the decay alone cannot tell a kick. */
struct AttackCues
{
    float total = 0.0f, low = 0.0f, high = 0.0f;
};
AttackCues attackAt (const Analysis& a, int64_t sample);

/** The label of mono[start, end) (`cues`: the attack there, when known). */
SliceType classify (const float* mono, int64_t start, int64_t end, double sampleRate, float* rootMidi = nullptr, const AttackCues* cues = nullptr);

/** The nearest zero crossing of `x` around `pos` (within maxDistance samples;
    `preferBefore`: an earlier one wins up to twice as far, so a transient's
    attack is not cut). None: pos. */
int64_t nearestZeroCrossing (const float* x, int64_t n, int64_t pos, int64_t maxDistance, bool preferBefore = false);

/** Tempo from tapped times (seconds, in order): the median interval. <= 0: not enough taps. */
double tapTempo (const std::vector<double>& tapTimes);

/** The sample of the beat-1 grid nearest `pos`, the grid being every `beat`
    samples from `firstBeat`. */
double nearestGridPoint (double pos, double firstBeat, double step);

/** A slice's audio with the 2 ms fades (raised cosine) in and out: for the
    tests and the export. */
std::vector<float> renderSlice (const float* x, int64_t n, const Slice& s, double sampleRate, double fadeSeconds = 0.002);
} // namespace daftchunk
