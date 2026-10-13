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
        prepare (numChannels, delaySamples, delaySamples);
    }

    /** With room for delays up to `maxDelaySamples` (see setDelay). */
    void prepare (int numChannels, int delaySamples, int maxDelaySamples)
    {
        capacity = juce::jmax (0, maxDelaySamples, delaySamples);
        delay = juce::jmax (0, delaySamples);
        buffer.setSize (juce::jmax (1, numChannels), juce::jmax (1, capacity), false, true, false);
        reset();
    }

    /** Changes the delay (<= the prepared maximum) and clears the line.
        Real-time safe. */
    void setDelay (int delaySamples) noexcept
    {
        delay = juce::jlimit (0, capacity, delaySamples);
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
        if (delay != capacity)
        {
            // Line longer than the delay: separate read and write positions
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = in[i];
                int r = pos - delay;
                if (r < 0)
                    r += capacity;
                out[i] = d[r];
                d[pos] = x;
                if (++pos == capacity)
                    pos = 0;
            }
            return;
        }
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
            writePos = (writePos + numSamples) % capacity;
    }

private:
    juce::AudioBuffer<float> buffer;
    int delay = 0, capacity = 0, writePos = 0;
};
} // namespace gk
