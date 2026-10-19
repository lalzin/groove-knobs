#pragma once

namespace gk
{
/** Play head for offline renders: a transport playing from bar 1 at a fixed tempo. */
class OfflinePlayHead final : public juce::AudioPlayHead
{
public:
    double bpm = 124.0, sampleRate = 48000.0;
    juce::int64 samplePosition = 0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeInSamples (samplePosition);
        info.setTimeInSeconds ((double) samplePosition / sampleRate);
        info.setPpqPosition ((double) samplePosition / sampleRate * bpm / 60.0);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

/** Drives a OneKnobProcessor offline, like a host would: fixed block size,
    "amount" automation sent every `automationStep` samples, optional tempo. */
struct OfflineRenderer
{
    struct Options
    {
        int blockSize = 256;
        int automationStep = 32;             // host automation resolution
        double bpm = 124.0;
        bool transportPlaying = true;
        bool usePlayHead = true;
        bool compensateLatency = true;       // output aligned with input, same length
        int preset = -1;                     // -1 = keep
        std::function<float (double seconds)> amount; // null = keep current value
        std::function<int (double seconds)> presetAt; // optional preset automation
    };

    static void setParameter (juce::RangedAudioParameter& p, float plainValue)
    {
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    }

    /** Prepares the processor for the buffer's channel count and renders it. */
    static juce::AudioBuffer<float> render (OneKnobProcessor& proc, const juce::AudioBuffer<float>& input, double sampleRate, const Options& opt)
    {
        const int numChannels = input.getNumChannels();
        const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        proc.setBusesLayout (layout);
        proc.setNonRealtime (true); // offline: engines build their data synchronously (reproducible)

        if (opt.preset >= 0)
            setParameter (proc.getPresetParameter(), (float) opt.preset);
        if (opt.amount)
            setParameter (proc.getAmountParameter(), opt.amount (0.0));

        proc.prepareToPlay (sampleRate, opt.blockSize);
        const int latency = opt.compensateLatency ? proc.getLatencySamples() : 0;

        OfflinePlayHead playHead;
        playHead.bpm = opt.bpm;
        playHead.sampleRate = sampleRate;
        playHead.playing = opt.transportPlaying;
        proc.setPlayHead (opt.usePlayHead ? &playHead : nullptr);

        const int total = input.getNumSamples() + latency;
        juce::AudioBuffer<float> out (numChannels, total);
        juce::AudioBuffer<float> block (numChannels, opt.blockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total;)
        {
            const int n = juce::jmin (opt.blockSize, total - pos);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                block.clear (ch, 0, opt.blockSize);
                const int avail = juce::jlimit (0, n, input.getNumSamples() - pos);
                if (avail > 0)
                    block.copyFrom (ch, 0, input, ch, pos, avail);
            }

            // Host-style automation: parameters change between sub-blocks.
            for (int sub = 0; sub < n; sub += opt.automationStep)
            {
                const int m = juce::jmin (opt.automationStep, n - sub);
                const double t = (double) (pos + sub) / sampleRate;
                if (opt.amount)
                    setParameter (proc.getAmountParameter(), juce::jlimit (0.0f, 1.0f, opt.amount (t)));
                if (opt.presetAt)
                    setParameter (proc.getPresetParameter(), (float) opt.presetAt (t));

                playHead.samplePosition = pos + sub;
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), numChannels, sub, m);
                proc.processBlock (view, midi);
            }

            for (int ch = 0; ch < numChannels; ++ch)
                out.copyFrom (ch, pos, block, ch, 0, n);
            pos += n;
        }

        proc.setPlayHead (nullptr);
        juce::AudioBuffer<float> result (numChannels, input.getNumSamples());
        for (int ch = 0; ch < numChannels; ++ch)
            result.copyFrom (ch, 0, out, ch, latency, input.getNumSamples());
        return result;
    }
};

/** Instruments: plays a MIDI sequence (time stamps in samples) through an
    instrument, like a host: no input bus, stereo (or mono) output, fixed
    block size, every event at its exact offset inside its block. */
struct MidiRenderer
{
    struct Options
    {
        int blockSize = 512;
        int numChannels = 2;
        double bpm = 124.0;
        bool transportPlaying = true;
        bool prepare = true;           // false: keep the processor's state (rendering in several passes)
        std::int64_t startSample = 0;  // play head position of the first sample
    };

    static juce::AudioBuffer<float> render (juce::AudioProcessor& proc, const juce::MidiMessageSequence& events, int numSamples,
                                            double sampleRate, const Options& opt)
    {
        if (opt.prepare)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.outputBuses.add (opt.numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            proc.setBusesLayout (layout);
            proc.setNonRealtime (true);
            proc.prepareToPlay (sampleRate, opt.blockSize);
        }
        OfflinePlayHead playHead;
        playHead.bpm = opt.bpm;
        playHead.sampleRate = sampleRate;
        playHead.playing = opt.transportPlaying;
        proc.setPlayHead (&playHead);

        juce::AudioBuffer<float> out (opt.numChannels, numSamples);
        out.clear();
        juce::MidiBuffer midi;
        int next = 0;
        for (int pos = 0; pos < numSamples; pos += opt.blockSize)
        {
            const int n = juce::jmin (opt.blockSize, numSamples - pos);
            midi.clear();
            while (next < events.getNumEvents())
            {
                const auto& m = events.getEventPointer (next)->message;
                const auto at = (std::int64_t) std::llround (m.getTimeStamp());
                if (at >= pos + n)
                    break;
                if (at >= pos)
                    midi.addEvent (m, (int) (at - pos));
                ++next;
            }
            playHead.samplePosition = opt.startSample + pos;
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), opt.numChannels, pos, n);
            proc.processBlock (view, midi);
        }
        proc.setPlayHead (nullptr);
        return out;
    }
};
} // namespace gk
