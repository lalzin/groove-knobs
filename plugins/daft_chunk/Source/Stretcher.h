#pragma once
// DAFT CHUNK - the sample stretched to the host's tempo without changing its
// pitch (Signalsmith Stretch, offline, the whole sample in one go), on a
// background thread: the audio thread only plays the finished buffer.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace daftchunk
{
/** `in` stretched to `factor` times its length (factor 0.5 .. 2: twice as
    fast .. twice as slow), same channels, same sample rate. */
std::shared_ptr<const juce::AudioBuffer<float>> stretchBuffer (const juce::AudioBuffer<float>& in, double sampleRate, double factor);

/** One stretch at a time, in the background. A new request replaces a
    pending one; the result waits in takeResult() (message thread). */
class StretchWorker final : private juce::Thread
{
public:
    StretchWorker();
    ~StretchWorker() override;

    struct Job
    {
        std::shared_ptr<const juce::AudioBuffer<float>> source; // kept alive while stretching
        double sampleRate = 44100.0, factor = 1.0;
        int id = 0;
    };
    void request (Job job);
    bool isBusy() const noexcept { return busy.load(); }

    struct Result
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double factor = 1.0;
        int id = 0;
    };
    /** The last finished stretch (audio null: none since the last call). */
    Result takeResult();
    /** Tests: blocks until idle. */
    void waitUntilDone();

private:
    void run() override;
    std::mutex lock;
    std::unique_ptr<Job> pending;
    Result result;
    std::atomic<bool> busy { false };
    juce::WaitableEvent wake, done { true };
};
} // namespace daftchunk
