#include "SampleLoader.h"

namespace daftchunk
{
namespace
{
    juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager m;
        static const bool registered = [] { m.registerBasicFormats(); return true; }(); // WAV, AIFF, FLAC, Ogg, MP3 (JUCE_USE_MP3AUDIOFORMAT), CoreAudio on macOS
        juce::ignoreUnused (registered);
        return m;
    }
    std::mutex formatsLock; // AudioFormatManager::createReaderFor is not documented thread-safe
} // namespace

juce::String acceptedExtensions() { return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3"; }

bool isAcceptedFile (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}

std::unique_ptr<LoadedSample> decodeFile (const juce::File& file, juce::String& error, const std::atomic<bool>* cancel)
{
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        const std::lock_guard<std::mutex> g (formatsLock);
        reader.reset (formats().createReaderFor (file));
    }
    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "Unknown format: " + file.getFileName() : "File not found: " + file.getFileName();
        return nullptr;
    }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Empty file: " + file.getFileName();
        return nullptr;
    }
    auto s = std::make_unique<LoadedSample>();
    s->sampleRate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->path = file.getFullPathName();
    const auto maxLen = (juce::int64) (Analysis::maxSeconds * reader->sampleRate);
    s->truncated = reader->lengthInSamples > maxLen;
    const auto len = (int) std::min (reader->lengthInSamples, maxLen);
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    s->audio.setSize (chans, len);
    // in chunks of 1 s, so a cancel is quick
    const int chunk = (int) reader->sampleRate;
    for (int pos = 0; pos < len; pos += chunk)
    {
        if (cancel != nullptr && cancel->load())
            return nullptr;
        reader->read (&s->audio, pos, std::min (chunk, len - pos), pos, true, chans > 1);
    }
    return s;
}

SampleLoader::SampleLoader() : juce::Thread ("Daft Chunk analysis")
{
    done.signal(); // idle
    startThread (juce::Thread::Priority::low);
}

SampleLoader::~SampleLoader()
{
    cancel = true;
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void SampleLoader::load (const juce::File& file)
{
    auto r = std::make_unique<Request>();
    r->file = file;
    const std::lock_guard<std::mutex> g (lock);
    pending = std::move (r);
    cancel = true;
    busy = true;
    progress = 0.0f;
    done.reset();
    wake.signal();
}

void SampleLoader::load (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name)
{
    auto r = std::make_unique<Request>();
    r->audio = std::move (audio);
    r->sampleRate = sampleRate;
    r->name = name;
    const std::lock_guard<std::mutex> g (lock);
    pending = std::move (r);
    cancel = true;
    busy = true;
    progress = 0.0f;
    done.reset();
    wake.signal();
}

std::unique_ptr<LoadedSample> SampleLoader::takeResult (juce::String& error)
{
    const std::lock_guard<std::mutex> g (lock);
    if (! hasResult)
        return nullptr;
    hasResult = false;
    error = lastError;
    return std::move (result);
}

void SampleLoader::waitUntilDone()
{
    done.wait (-1);
}

void SampleLoader::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);
        for (;;)
        {
            std::unique_ptr<Request> r;
            {
                const std::lock_guard<std::mutex> g (lock);
                r = std::move (pending);
                if (r == nullptr)
                {
                    busy = false;
                    done.signal();
                    break;
                }
                cancel = false;
            }
            if (threadShouldExit())
                return;

            juce::String error;
            std::unique_ptr<LoadedSample> s;
            if (r->file != juce::File())
                s = decodeFile (r->file, error, &cancel);
            else
            {
                s = std::make_unique<LoadedSample>();
                s->sampleRate = r->sampleRate;
                s->name = r->name;
                const auto maxLen = (int) (Analysis::maxSeconds * r->sampleRate);
                s->truncated = r->audio.getNumSamples() > maxLen;
                const int len = std::min (r->audio.getNumSamples(), maxLen), chans = juce::jlimit (1, 2, r->audio.getNumChannels());
                s->audio.setSize (chans, len);
                for (int c = 0; c < chans; ++c)
                    s->audio.copyFrom (c, 0, r->audio, c, 0, len);
            }
            if (s != nullptr && ! cancel.load())
            {
                s->analysis = analyse (s->audio.getArrayOfReadPointers(), s->audio.getNumChannels(), s->audio.getNumSamples(), s->sampleRate, &progress, &cancel);
                if (s->analysis.numSamples == 0)
                {
                    if (! cancel.load())
                        error = "Empty sample";
                    s.reset();
                }
            }
            const std::lock_guard<std::mutex> g (lock);
            if (cancel.load() && pending != nullptr)
                continue; // replaced by a newer request: drop this one
            result = std::move (s);
            lastError = result == nullptr ? (error.isEmpty() ? juce::String ("Cancelled") : error) : juce::String();
            hasResult = true;
        }
    }
}
} // namespace daftchunk
