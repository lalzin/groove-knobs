#pragma once
// DAFT CHUNK - a sample: its audio and its analysis (immutable once made),
// and the background thread that decodes and analyses it.

#include "Analysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <memory>
#include <mutex>

namespace daftchunk
{
/** A decoded, analysed sample (never changes once made: shared with the audio thread). */
struct LoadedSample
{
    juce::AudioBuffer<float> audio; // 1 or 2 channels, at most 60 s
    double sampleRate = 44100.0;
    juce::String name, path;        // the file's name (no extension) and full path (empty: not from a file)
    bool truncated = false;         // the file was longer than 60 s
    Analysis analysis;
};

/** A slice's own settings (the TRANCHE page), on top of the global ones. */
struct SliceParams
{
    float pitch = 0.0f;     // semitones, -24..24
    float gainDb = 0.0f;    // -24..+12
    float filter = 0.0f;    // -1..1: left a low-pass, right a high-pass, 0 open (on top of FILTER)
    float start = 0.0f;     // fine start: 0..0.9 of the slice, from its start
    float end = 0.0f;       // fine end: 0..0.9 of the slice, from its end
    bool reverse = false;
    bool isDefault() const noexcept
    {
        const auto zero = [] (float v) { return std::abs (v) < 1.0e-6f; };
        return zero (pitch) && zero (gainDb) && zero (filter) && zero (start) && zero (end) && ! reverse;
    }
};

/** The slices of a sample, as played: what the audio thread reads. */
struct Kit
{
    std::shared_ptr<const LoadedSample> sample;
    std::vector<Slice> slices;
    std::vector<SliceParams> params; // one per slice
    /** The audio played: the sample's, or the sample stretched to the host's
        tempo (TEMPO). Slice positions x timeScale = positions in it. */
    std::shared_ptr<const juce::AudioBuffer<float>> stretched;
    double timeScale = 1.0;
    double playBpm = 120.0; // the tempo of the audio played
    int version = 0;

    const juce::AudioBuffer<float>& audio() const noexcept { return stretched != nullptr ? *stretched : sample->audio; }
};

/** Decodes WAV, AIFF, FLAC, MP3 (and what the system reads). Error: null and `error` set. */
std::unique_ptr<LoadedSample> decodeFile (const juce::File& file, juce::String& error, const std::atomic<bool>* cancel = nullptr);

/** File extensions accepted by drag and drop / the chooser. */
juce::String acceptedExtensions(); // "*.wav;*.aif;..."
bool isAcceptedFile (const juce::String& path);

/** One job at a time on a background thread: decode (if a file) then analyse.
    A new request cancels the one running. The finished sample waits in
    takeResult() (message thread). */
class SampleLoader final : private juce::Thread
{
public:
    SampleLoader();
    ~SampleLoader() override;

    void load (const juce::File& file);
    /** Already decoded audio (tests, state with embedded audio). */
    void load (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name);

    bool isBusy() const noexcept { return busy.load(); }
    float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

    /** The finished sample (null: none since the last call). `error` is set
        when the last job failed (the result is then null). */
    std::unique_ptr<LoadedSample> takeResult (juce::String& error);

    /** Blocks until the current job is done (tests, offline). */
    void waitUntilDone();

private:
    void run() override;

    struct Request
    {
        juce::File file;
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;
        juce::String name;
    };
    std::mutex lock;
    std::unique_ptr<Request> pending;
    std::unique_ptr<LoadedSample> result;
    juce::String lastError;
    bool hasResult = false;
    std::atomic<bool> busy { false }, cancel { false };
    std::atomic<float> progress { 0.0f };
    juce::WaitableEvent wake, done { true };
};
} // namespace daftchunk
