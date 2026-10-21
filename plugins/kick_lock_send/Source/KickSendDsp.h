#pragma once

#include <groove_core/groove_core.h>
#include "KickLink.h"

namespace kls
{
/** Kick Lock Send: on the kick's track. The kick passes through, delayed by
    Kick Lock's latency (so the host keeps the two tracks in step), through a
    transient shaper: PUNCH (the big knob, centred: -1 softer attack .. +1
    more attack) and SUSTAIN (-1 shorter tail .. +1 longer); both at 0: the
    kick untouched, bit for bit. The shaper looks 1 ms ahead (the delay is
    there anyway). Each kick it detects (SENS) is published, with its position
    on the song's grid, to the Kick Lock(s) on its LINK number, with the kick's
    wave for their scope (kl::KickLink). */
class KickSendDsp final : public gk::OneKnobEngine
{
public:
    /** The detector's threshold for a SENSITIVITY (0..1): -6 dBFS .. -42 dBFS. */
    static float thresholdDbFor (float sensitivity) noexcept { return -6.0f - 36.0f * juce::jlimit (0.0f, 1.0f, sensitivity); }

    struct Telemetry
    {
        std::atomic<int> kicks { 0 };
        std::atomic<float> level { 0.0f };
    };

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return latency; }
    void setPreset (int) override {}
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;
    void setChoices (const int* idx, int n) override { sender.setChannel (n > 0 ? idx[0] : 1); }
    void setKnobs (const float* v, int n) override
    {
        sustain = n > sustainKnob ? juce::jlimit (-1.0f, 1.0f, v[sustainKnob]) : 0.0f;
        sensitivity = n > sensitivityKnob ? juce::jlimit (0.0f, 1.0f, v[sensitivityKnob]) : 0.5f;
    }
    static constexpr int sustainKnob = 0, sensitivityKnob = 1;
    /** The shaper's gain (dB) for an attack ratio (fast / slow envelope) and a tail ratio (slow / fast release). */
    static float shaperDb (float punch, float sustain, float attackRatio, float tailRatio) noexcept
    {
        if (juce::exactlyEqual (punch, 0.0f) && juce::exactlyEqual (sustain, 0.0f))
            return 0.0f;
        const float a = juce::jlimit (0.0f, 1.0f, (attackRatio - 1.0f) * 0.5f);
        const float t = juce::jlimit (0.0f, 1.0f, (tailRatio - 1.0f) / 1.5f);
        return juce::jlimit (-18.0f, 12.0f, punch * 12.0f * a + sustain * 12.0f * t);
    }

    kl::KickLinkSender& getSender() noexcept { return sender; }
    Telemetry& getTelemetry() noexcept { return telemetry; }

private:
    double sampleRate = 48000.0;
    int numChannels = 2, latency = 768, lead = 48;
    float sustain = 0.0f, sensitivity = 0.5f;
    // the shaper: envelopes, the gain ring (applied 1 ms ahead of the delayed kick), the delay rings
    float aFast = 0.0f, aSlow = 0.0f, sFast = 0.0f, sSlow = 0.0f, gainSmooth = 1.0f;
    float c05 = 0.0f, c20 = 0.0f, c25 = 0.0f, c40 = 0.0f, c250 = 0.0f, cSmooth = 0.0f;
    std::array<std::vector<float>, 2> ring;
    std::vector<float> gainRing;
    std::int64_t ringMask = 0;
    int waveCounter = 0;
    std::int64_t clock = 0, deadUntil = -1;
    float env = 0.0f, slowEnv = 0.0f, attack = 0.0f, releaseFast = 0.0f, releaseSlow = 0.0f;
    bool armed = true;
    std::vector<float> envRing;
    std::int64_t mask = 0;
    kl::KickLinkSender sender;
    Telemetry telemetry;
};
} // namespace kls
