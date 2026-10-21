#include "KickSendDsp.h"
#include "KickLockDsp.h"

namespace kls
{
void KickSendDsp::prepare (double sr, int, int nch)
{
    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, nch);
    // Kick Lock's own latency: the host then delays both tracks alike
    latency = (int) std::round (kl::KickLockDsp::firSeconds * sr) + (int) std::round (kl::KickLockDsp::advanceSeconds * sr);
    delay.prepare (numChannels, latency);
    attack = (float) (1.0 - std::exp (-1.0 / (0.0002 * sr)));
    releaseFast = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
    releaseSlow = (float) (1.0 - std::exp (-1.0 / (0.06 * sr)));
    std::int64_t size = 1;
    while (size < (std::int64_t) (0.05 * sr))
        size <<= 1;
    mask = size - 1;
    envRing.assign ((size_t) size, 0.0f);
    reset();
}

void KickSendDsp::reset()
{
    delay.reset();
    clock = 0;
    deadUntil = -1;
    env = slowEnv = 0.0f;
    armed = true;
    std::fill (envRing.begin(), envRing.end(), 0.0f);
}

void KickSendDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    sender.heartbeat();
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        // the same detector as Kick Lock's: a fast envelope over the
        // threshold, re-armed once the kick's body has gone (or on a new
        // attack), its start searched up to 4 ms back
        float k = 0.0f;
        for (int c = 0; c < nch; ++c)
            k = juce::jmax (k, std::abs (buffer.getSample (c, i)));
        const float threshold = gk::dspmath::dbToGain (thresholdDbFor (amount[i]));
        env += (k - env) * (k > env ? attack : releaseFast);
        envRing[(size_t) (clock & mask)] = env;
        const float slowBefore = slowEnv;
        slowEnv += (env - slowEnv) * (env > slowEnv ? attack : releaseSlow);
        if (slowEnv < 0.5f * threshold && clock >= deadUntil) // not at the kick's very start: its slow envelope is still rising there
            armed = true;
        const bool newAttack = env > 2.0f * slowBefore && clock >= deadUntil;
        if (env >= threshold && (armed || newAttack) && clock >= deadUntil)
        {
            armed = false;
            deadUntil = clock + (std::int64_t) (kl::KickLockDsp::deadTimeSeconds * sampleRate);
            std::int64_t onset = clock;
            const auto earliest = clock - (std::int64_t) (0.004 * sampleRate);
            while (onset - 1 > earliest && envRing[(size_t) ((onset - 1) & mask)] > 0.02f * threshold
                   && envRing[(size_t) ((onset - 1) & mask)] <= envRing[(size_t) (onset & mask)])
                --onset;
            telemetry.kicks.fetch_add (1, std::memory_order_relaxed);
            // its place on the song's grid (only while the host plays: the
            // grid means nothing when stopped)
            if (timing.isPlaying && timing.beatsPerSample > 0.0)
                sender.publish (timing.beatsAt (i) - (double) (clock - onset) * timing.beatsPerSample, env);
        }
        peak = juce::jmax (peak, env);
        ++clock;
    }
    telemetry.level.store (peak, std::memory_order_relaxed);
    // the kick itself, untouched (delayed like the bass)
    for (int c = 0; c < nch; ++c)
        delay.process (c, buffer.getReadPointer (c), buffer.getWritePointer (c), numSamples);
    delay.advance (numSamples);
}
} // namespace kls
