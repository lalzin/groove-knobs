#include "BackwashBounce.h"

namespace bw::bounce
{
namespace
{
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

juce::AudioBuffer<float> renderLoop (BackwashProcessor& renderer, const Request& request, const std::vector<float>& memory,
                                     double cycleBeats, double sampleRate)
{
    const int len = (int) memory.size();
    if (len <= 0 || cycleBeats <= 0.0)
        return {};
    // The tempo the memory was recorded at: one cycle is exactly `len` samples
    Request r = request;
    r.bpm = cycleBeats * 60.0 * sampleRate / (double) len;
    // The cycle kept, with enough cycles before it for its longest swell (2 s)
    // and one after it: the swell at the end of the bar leads to the next bar's
    // first beat
    const int cycles = (int) std::ceil (BackwashDsp::lookaheadSeconds * sampleRate / (double) len - 1.0e-9) + 2;
    juce::AudioBuffer<float> in (1, cycles * len);
    for (int c = 0; c < cycles; ++c)
        in.copyFrom (0, c * len, memory.data(), len);
    int bars = 0;
    const auto out = renderBuffer (renderer, r, in, sampleRate, bars);
    if (out.getNumSamples() < 2 * len)
        return {};
    const int last = out.getNumSamples() - 2 * len; // the next-to-last cycle (the output ends with the input)
    juce::AudioBuffer<float> wet (2, len);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < len; ++i)
            wet.setSample (ch, i, out.getSample (ch, last + i) - memory[(size_t) i]);
    return wet;
}

juce::File writeLoop (BackwashProcessor& live, const juce::File& folder)
{
    const auto memory = live.getBackwashDsp().copyMemory();
    if (memory.empty() || ! live.getBackwashDsp().getTelemetry().liveMemoryComplete.load())
        return {};
    const auto request = requestFrom (live);
    auto renderer = makeRenderer (request);
    const double cycleBeats = live.getBackwashDsp().getMemoryCycleBeats();
    const double sr = live.getBackwashDsp().getSampleRate();
    const auto wet = renderLoop (*renderer, request, memory, cycleBeats, sr);
    if (wet.getNumSamples() == 0)
        return {};

    // e.g. "Backwash_Snare_Swell_124bpm_1bar.wav"
    const double bpm = cycleBeats * 60.0 * sr / (double) memory.size();
    const int bars = juce::jmax (1, juce::roundToInt (cycleBeats / juce::jmax (0.25, request.beatsPerBar)));
    const auto preset = clean (renderer->getConfig().presets[renderer->getPresetParameter().getIndex()].name);
    const auto name = "Backwash_" + preset + "_" + tempoText (bpm) + "bpm_" + juce::String (bars) + (bars == 1 ? "bar" : "bars");
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav").getNonexistentSibling (false);
    return writeWav (file, wet, sr) ? file : juce::File();
}
} // namespace bw::bounce
