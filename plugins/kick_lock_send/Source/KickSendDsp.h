#pragma once

#include <groove_core/groove_core.h>
#include "KickLink.h"

namespace kls
{
/** Kick Lock Send: on the kick's track. The kick passes through untouched,
    delayed by Kick Lock's latency (so the host keeps the two tracks in step);
    each kick it detects is published, with its position on the song's grid,
    to the Kick Lock(s) on the bass (kl::KickLink). The knob is the
    detector's sensitivity. */
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

    kl::KickLinkSender& getSender() noexcept { return sender; }
    Telemetry& getTelemetry() noexcept { return telemetry; }

private:
    double sampleRate = 48000.0;
    int numChannels = 2, latency = 768;
    gk::IntegerDelay delay;
    std::int64_t clock = 0, deadUntil = -1;
    float env = 0.0f, slowEnv = 0.0f, attack = 0.0f, releaseFast = 0.0f, releaseSlow = 0.0f;
    bool armed = true;
    std::vector<float> envRing;
    std::int64_t mask = 0;
    kl::KickLinkSender sender;
    Telemetry telemetry;
};
} // namespace kls
