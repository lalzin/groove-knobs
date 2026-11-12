#include "Stretcher.h"

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wshadow", "-Wshadow-field", "-Wconversion", "-Wsign-conversion", "-Wfloat-equal", "-Wimplicit-float-conversion",
                                     "-Wfloat-conversion", "-Wshorten-64-to-32", "-Wunused-parameter", "-Wextra-semi", "-Wzero-as-null-pointer-constant",
                                     "-Wcast-align", "-Wdeprecated", "-Wunused-variable", "-Wsign-compare", "-Wimplicit-int-conversion")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4244 4267 4305 4100 4456 4457 4458 4459 4127 4389 4018)
#include <signalsmith-stretch/signalsmith-stretch.h>
JUCE_END_IGNORE_WARNINGS_MSVC
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

namespace daftchunk
{
std::shared_ptr<const juce::AudioBuffer<float>> stretchBuffer (const juce::AudioBuffer<float>& in, double sampleRate, double factor)
{
    factor = juce::jlimit (0.25, 4.0, factor);
    const int chans = in.getNumChannels(), inLen = in.getNumSamples();
    const int outLen = std::max (1, (int) std::llround (inLen * factor));
    auto out = std::make_shared<juce::AudioBuffer<float>> (chans, outLen);
    out->clear();
    if (inLen < 64)
        return out;
    signalsmith::stretch::SignalsmithStretch<float> s;
    s.presetDefault (chans, (float) sampleRate);
    if (! s.exact (in.getArrayOfReadPointers(), inLen, out->getArrayOfWritePointers(), outLen))
    {
        // too short for the stretcher's windows: plain resampling (the pitch moves, a few ms only)
        for (int c = 0; c < chans; ++c)
            for (int i = 0; i < outLen; ++i)
            {
                const double p = (double) i / factor;
                const int k = std::min ((int) p, inLen - 2);
                const float f = (float) (p - k);
                out->setSample (c, i, in.getSample (c, k) + f * (in.getSample (c, k + 1) - in.getSample (c, k)));
            }
    }
    return out;
}

StretchWorker::StretchWorker() : juce::Thread ("Daft Chunk stretch")
{
    done.signal(); // idle
    startThread (juce::Thread::Priority::low);
}

StretchWorker::~StretchWorker()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void StretchWorker::request (Job job)
{
    const std::lock_guard<std::mutex> g (lock);
    pending = std::make_unique<Job> (std::move (job));
    busy = true;
    done.reset();
    wake.signal();
}

StretchWorker::Result StretchWorker::takeResult()
{
    const std::lock_guard<std::mutex> g (lock);
    auto r = std::move (result);
    result = {};
    return r;
}

void StretchWorker::waitUntilDone()
{
    done.wait (-1);
}

void StretchWorker::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);
        for (;;)
        {
            std::unique_ptr<Job> job;
            {
                const std::lock_guard<std::mutex> g (lock);
                job = std::move (pending);
                if (job == nullptr)
                {
                    busy = false;
                    done.signal();
                    break;
                }
            }
            if (threadShouldExit())
                return;
            auto audio = stretchBuffer (*job->source, job->sampleRate, job->factor);
            const std::lock_guard<std::mutex> g (lock);
            if (pending != nullptr)
                continue; // already outdated
            result = { std::move (audio), job->factor, job->id };
        }
    }
}
} // namespace daftchunk
