#pragma once

namespace gk
{
/** Musical timing for one processed block. */
struct BlockTiming
{
    double beatsAtStart = 0.0;   // musical position (quarter notes) of the first sample
    double beatsPerSample = 0.0; // advance per sample at the current tempo
    double bpm = 120.0;
    double beatsPerBar = 4.0;    // quarter notes per bar (host time signature, 4/4 if unknown)
    bool isPlaying = false;

    double beatsAt (int sampleOffset) const noexcept { return beatsAtStart + sampleOffset * beatsPerSample; }

    BlockTiming offsetBy (int samples) const noexcept
    {
        auto t = *this;
        t.beatsAtStart = beatsAt (samples);
        return t;
    }
};

/** Reads the host transport once per block and produces a continuous
    musical position: locked to the host PPQ while it plays, free-running at
    the same tempo when it is stopped (120 BPM if the host gives no tempo). */
class HostSync
{
public:
    static constexpr double fallbackBpm = 120.0;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        freeBeats = 0.0;
        lastBpm = fallbackBpm;
        lastBeatsPerBar = 4.0;
    }

    /** Must be called once per host block, before processing it. */
    BlockTiming beginBlock (juce::AudioPlayHead* playHead, int numSamples) noexcept;

private:
    double sr = 44100.0, freeBeats = 0.0, lastBpm = fallbackBpm, lastBeatsPerBar = 4.0;
};
} // namespace gk
