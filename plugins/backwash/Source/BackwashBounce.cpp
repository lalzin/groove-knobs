#include "BackwashBounce.h"

namespace bw::bounce
{
namespace
{
    juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager m;
        static const bool registered = [] { m.registerBasicFormats(); return true; }(); // WAV, AIFF, FLAC, Ogg, MP3, CoreAudio on macOS
        juce::ignoreUnused (registered);
        return m;
    }
    std::mutex formatsLock;

    /** The host transport, as the offline render sees it: playing from 0 at a
        fixed tempo and time signature. */
    class BouncePlayHead final : public juce::AudioPlayHead
    {
    public:
        double bpm = 120.0, sampleRate = 48000.0, beatsPerBar = 4.0;
        juce::int64 samplePosition = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (true);
            info.setTimeInSamples (samplePosition);
            info.setTimeInSeconds ((double) samplePosition / sampleRate);
            info.setPpqPosition ((double) samplePosition / sampleRate * bpm / 60.0);
            // beats per bar in quarter notes: 4 -> 4/4, 3 -> 3/4, 1.5 -> 3/8, 3.5 -> 7/8
            const bool quarters = std::abs (beatsPerBar - std::round (beatsPerBar)) < 1.0e-6;
            info.setTimeSignature (quarters ? TimeSignature { juce::roundToInt (beatsPerBar), 4 } : TimeSignature { juce::roundToInt (beatsPerBar * 2.0), 8 });
            info.setPpqPositionOfLastBarStart (std::floor (*info.getPpqPosition() / beatsPerBar) * beatsPerBar);
            return info;
        }
    };

    juce::String clean (const juce::String& s)
    {
        auto t = juce::File::createLegalFileName (s).replaceCharacters (" ", "_");
        return t.isEmpty() ? juce::String ("sound") : t.substring (0, 40);
    }

    juce::String tempoText (double bpm)
    {
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.005;
        return whole ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 2).replaceCharacter ('.', '_');
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sampleRate)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
        if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
            return false;
        juce::WavAudioFormat wav;
        // 32-bit float: the swells may go over 0 dBFS, nothing is clipped
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate (sampleRate)
                                                  .withNumChannels (b.getNumChannels())
                                                  .withBitsPerSample (32)
                                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
} // namespace

bool isAcceptedFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;wave;aif;aiff;flac;mp3;ogg");
}

juce::File defaultFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Backwash");
    d.createDirectory();
    return d;
}

Request requestFrom (BackwashProcessor& live)
{
    Request r;
    live.getStateInformation (r.state);
    const double bpm = (double) live.getFeatures().bpm.load();
    r.bpm = bpm > 1.0 ? bpm : 120.0;
    const double bpb = live.getBackwashDsp().getBeatsPerBar();
    r.beatsPerBar = bpb > 0.0 ? bpb : 4.0;
    return r;
}

std::unique_ptr<BackwashProcessor> makeRenderer (const Request& request)
{
    auto p = std::make_unique<BackwashProcessor>();
    // The state without the LIVE memory, in the 2 s mode (exact on any sound)
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (request.state.getData(), (int) request.state.getSize()))
    {
        xml->removeAttribute ("liveMemory");
        xml->setAttribute ("live", 0);
        xml->setAttribute ("lock", 0);
        xml->setAttribute ("bypass", 0);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*xml, block);
        p->setStateInformation (block.getData(), (int) block.getSize());
    }
    auto set = [] (juce::AudioParameterBool& b, bool on)
    {
        if (b.get() != on)
            b.setValueNotifyingHost (on ? 1.0f : 0.0f);
    };
    set (p->getLiveParameter(), false);
    set (p->getLockParameter(), false);
    set (p->getBypassParameterObject(), false);

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    p->setBusesLayout (layout);
    p->setNonRealtime (true);
    return p;
}

juce::AudioBuffer<float> renderBuffer (BackwashProcessor& renderer, const Request& request, const juce::AudioBuffer<float>& input,
                                       double sampleRate, int& preRollBars, const std::function<bool (float)>& progress)
{
    constexpr int block = 512;
    renderer.prepareToPlay (sampleRate, block);
    const int latency = renderer.getLatencySamples();

    const double samplesPerBeat = sampleRate * 60.0 / request.bpm;
    const double barSamples = samplesPerBeat * request.beatsPerBar;
    // Room for the swell of a hit at the very start: the lookahead (the
    // longest swell possible), in whole bars
    const int maxBars = juce::jmax (1, (int) std::ceil (BackwashDsp::lookaheadSeconds * sampleRate / barSamples - 1.0e-9));
    const int pre = (int) std::llround (maxBars * barSamples);
    const int n = input.getNumSamples();
    const int total = pre + n + latency;

    juce::AudioBuffer<float> work (2, total);
    work.clear();
    for (int ch = 0; ch < 2; ++ch)
        work.copyFrom (ch, pre, input, juce::jmin (ch, input.getNumChannels() - 1), 0, n);

    BouncePlayHead head;
    head.bpm = request.bpm;
    head.sampleRate = sampleRate;
    head.beatsPerBar = request.beatsPerBar;
    renderer.setPlayHead (&head);
    juce::MidiBuffer midi;
    bool cancelled = false;
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = juce::jmin (block, total - pos);
        head.samplePosition = pos;
        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, pos, len);
        renderer.processBlock (view, midi);
        if (progress && ! progress ((float) (pos + len) / (float) total))
        {
            cancelled = true;
            break;
        }
    }
    renderer.setPlayHead (nullptr);
    renderer.releaseResources();
    if (cancelled)
        return {};

    // The output, latency removed: [pre bars of room][the file]. Keep the
    // bars that hold a swell (whole bars, so the result stays on the grid).
    int first = pre;
    for (int i = 0; i < pre && first == pre; ++i)
        for (int ch = 0; ch < 2; ++ch)
            if (std::abs (work.getSample (ch, latency + i)) > 1.0e-4f) // -80 dB
            {
                first = i;
                break;
            }
    const int emptyBars = juce::jlimit (0, maxBars, (int) std::floor ((double) first / barSamples + 1.0e-9));
    preRollBars = maxBars - emptyBars;
    const int start = pre - (int) std::llround (preRollBars * barSamples);

    juce::AudioBuffer<float> out (2, pre - start + n);
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, work, ch, latency + start, out.getNumSamples());
    return out;
}

Result render (BackwashProcessor& renderer, const Request& request, const juce::File& input, const juce::File& folder,
               const std::function<bool (float)>& progress)
{
    Result r;
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        const std::lock_guard<std::mutex> lock (formatsLock);
        reader.reset (formats().createReaderFor (input));
    }
    if (reader == nullptr)
    {
        r.error = juce::String::fromUTF8 ("Format non lu : ") + input.getFileName();
        return r;
    }
    const double sr = reader->sampleRate;
    if (sr < 8000.0 || reader->lengthInSamples <= 0)
    {
        r.error = juce::String::fromUTF8 ("Fichier vide : ") + input.getFileName();
        return r;
    }
    if ((double) reader->lengthInSamples > maxInputSeconds * sr)
    {
        r.error = juce::String::fromUTF8 ("Trop long (10 min max) : ") + input.getFileName();
        return r;
    }
    const int n = (int) reader->lengthInSamples;
    const int nch = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> in (nch, n);
    reader->read (&in, 0, n, 0, true, nch > 1);
    reader.reset();

    int bars = 0;
    const auto out = renderBuffer (renderer, request, in, sr, bars, progress);
    if (out.getNumSamples() == 0)
    {
        r.error = juce::String::fromUTF8 ("Annul\xc3\xa9");
        return r;
    }

    // e.g. "Clap_Backwash_Snare_Swell_124bpm_1bar.wav", never over an earlier one (it may be in a set)
    const auto preset = clean (renderer.getConfig().presets[renderer.getPresetParameter().getIndex()].name);
    const auto name = clean (input.getFileNameWithoutExtension()) + "_Backwash_" + preset + "_" + tempoText (request.bpm) + "bpm_"
                      + juce::String (bars) + (bars == 1 ? "bar" : "bars");
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav").getNonexistentSibling (false);
    if (! writeWav (file, out, sr))
    {
        r.error = juce::String::fromUTF8 ("\xc3\x89""criture impossible : ") + file.getFullPathName();
        return r;
    }
    r.file = file;
    r.preRollBars = bars;
    return r;
}
} // namespace bw::bounce
