#pragma once

namespace gk
{
/** Fixed integer delay for the dry path (keeps dry and wet sample aligned).
    Memory is allocated in prepare(); process() never allocates. */
class IntegerDelay
{
public:
    void prepare (int numChannels, int delaySamples)
    {
        delay = juce::jmax (0, delaySamples);
        buffer.setSize (juce::jmax (1, numChannels), juce::jmax (1, delay), false, true, false);
        reset();
    }

    void reset() noexcept
    {
        buffer.clear();
        writePos = 0;
    }

    int getDelay() const noexcept { return delay; }

    /** Writes `in` and reads the delayed signal into `out` (may alias `in`). */
    void process (int channel, const float* in, float* out, int numSamples) noexcept
    {
        if (delay == 0)
        {
            if (out != in)
                std::copy (in, in + numSamples, out);
            return;
        }

        auto* d = buffer.getWritePointer (channel);
        int pos = writePos;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = in[i];
            out[i] = d[pos];
            d[pos] = x;
            if (++pos == delay)
                pos = 0;
        }
    }

    /** Call once per block after every channel has been processed. */
    void advance (int numSamples) noexcept
    {
        if (delay > 0)
            writePos = (writePos + numSamples) % delay;
    }

private:
    juce::AudioBuffer<float> buffer;
    int delay = 0, writePos = 0;
};
} // namespace gk
