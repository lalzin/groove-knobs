#pragma once

namespace gk
{
/** Lock-free audio → UI telemetry (written by the audio thread once per
    block, read by the editor at frame rate). */
struct AudioFeatures
{
    std::atomic<float> rms { 0.0f };               // smoothed RMS of the input (linear)
    std::atomic<std::uint32_t> transientCount { 0 }; // incremented on each detected transient
    std::atomic<double> beats { 0.0 };             // musical position at the end of the last block
    std::atomic<float> bpm { 120.0f };
    std::atomic<bool> playing { false };
    std::atomic<float> modulationPhase { 0.0f };   // engine LFO phase
    std::atomic<float> amount { 0.0f };            // smoothed macro as heard
    std::atomic<float> bypassMix { 0.0f };         // 0 = active, 1 = fully bypassed

    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free);
};

/** Envelope-based RMS + transient detector, real-time safe. */
class FeatureExtractor
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        auto coeff = [rate = sampleRate] (double millis) { return (float) std::exp (-1.0 / (0.001 * millis * rate)); };
        fastAtt = coeff (1.0);
        fastRel = coeff (25.0);
        slowAtt = coeff (40.0);
        slowRel = coeff (250.0);
        rmsCoeff = coeff (60.0);
        refractory = (int) (0.09 * sampleRate);
        reset();
    }

    void reset() noexcept
    {
        fast = slow = ms = 0.0f;
        holdoff = 0;
    }

    void process (const juce::AudioBuffer<float>& buf, int start, int numSamples, AudioFeatures& out) noexcept
    {
        const int nch = buf.getNumChannels();
        const float norm = nch > 0 ? 1.0f / (float) nch : 0.0f;
        std::uint32_t newTransients = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                x += buf.getReadPointer (ch)[start + i];
            x *= norm;

            ms = rmsCoeff * ms + (1.0f - rmsCoeff) * x * x;

            const float a = std::abs (x);
            fast = a > fast ? fastAtt * fast + (1.0f - fastAtt) * a : fastRel * fast + (1.0f - fastRel) * a;
            slow = a > slow ? slowAtt * slow + (1.0f - slowAtt) * a : slowRel * slow + (1.0f - slowRel) * a;

            if (holdoff > 0)
                --holdoff;
            else if (fast > 0.01f && fast > 2.0f * slow)
            {
                ++newTransients;
                holdoff = refractory;
            }
        }

        out.rms.store (std::sqrt (ms), std::memory_order_relaxed);
        if (newTransients > 0)
            out.transientCount.fetch_add (newTransients, std::memory_order_relaxed);
    }

private:
    double sr = 44100.0;
    float fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0, rmsCoeff = 0;
    float fast = 0, slow = 0, ms = 0;
    int refractory = 0, holdoff = 0;
};
} // namespace gk
