#include "KickSendDsp.h"
#include "KickLockDsp.h"

namespace kls
{
void KickSendDsp::prepare (double sr, int maxBlock, int nch)
{
    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, nch);
    // Kick Lock's own latency (same rate, same buffer): the host then treats both tracks alike
    latency = kl::KickLockDsp::latencyFor (sr, maxBlock);
    lead = juce::jmin (latency - 1, (int) std::round (0.001 * sr));
    auto coeff = [sr] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); };
    c05 = coeff (0.0005);
    c20 = coeff (0.020);
    c25 = coeff (0.025);
    c40 = coeff (0.040);
    c250 = coeff (0.250);
    cSmooth = coeff (0.0003);
    std::int64_t rs = 1;
    while (rs < (std::int64_t) latency + 64)
        rs <<= 1;
    ringMask = rs - 1;
    for (auto& r : ring)
        r.assign ((size_t) rs, 0.0f);
    gainRing.assign ((size_t) rs, 1.0f);
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
    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    std::fill (gainRing.begin(), gainRing.end(), 1.0f);
    aFast = aSlow = sFast = sSlow = 0.0f;
    gainSmooth = 1.0f;
    waveCounter = 0;
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
    const float threshold = gk::dspmath::dbToGain (thresholdDbFor (sensitivity));
    const bool playing = timing.isPlaying && timing.beatsPerSample > 0.0;
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float k = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const float x = buffer.getSample (c, i);
            ring[(size_t) c][(size_t) (clock & ringMask)] = x;
            k = juce::jmax (k, std::abs (x));
        }

        // --- the detector (the same as Kick Lock's): a fast envelope over
        // the threshold, re-armed once the kick's body has gone (or on a new
        // attack), its start searched up to 4 ms back
        env += (k - env) * (k > env ? attack : releaseFast);
        envRing[(size_t) (clock & mask)] = env;
        const float slowBefore = slowEnv;
        slowEnv += (env - slowEnv) * (env > slowEnv ? attack : releaseSlow);
        if (slowEnv < 0.5f * threshold && clock >= deadUntil) // not at the kick's very start: its slow envelope is still rising
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
            // its place on the song's grid (only while the host plays)
            if (playing)
                sender.publish (timing.beatsAt (i) - (double) (clock - onset) * timing.beatsPerSample, env);
        }
        peak = juce::jmax (peak, env);

        // --- the shaper's gain, from the kick as it comes in: attack = the
        // fast envelope over the slow one; tail = a slow release over a fast one
        aFast += (k - aFast) * (k > aFast ? c05 : c40);
        aSlow += (k - aSlow) * (k > aSlow ? c20 : c40);
        sFast += (k - sFast) * (k > sFast ? c05 : c25);
        sSlow += (k - sSlow) * (k > sSlow ? c05 : c250);
        const float punch = juce::jlimit (-1.0f, 1.0f, amount[i]);
        const float db = shaperDb (punch, sustain, aFast / (aSlow + 1.0e-6f), sSlow / (sFast + 1.0e-6f));
        const float target = juce::exactlyEqual (db, 0.0f) ? 1.0f : gk::dspmath::dbToGain (db);
        gainSmooth = juce::exactlyEqual (target, 1.0f) && std::abs (gainSmooth - 1.0f) < 1.0e-6f ? 1.0f : gainSmooth + (target - gainSmooth) * cSmooth;
        gainRing[(size_t) (clock & ringMask)] = gainSmooth;

        // --- out: the kick L samples ago, with the gain computed 1 ms after
        // it (the shaper anticipates its attack)
        const auto t = clock - latency;
        const float g = gainRing[(size_t) ((t + lead) & ringMask)];
        float mono = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const float y = juce::exactlyEqual (g, 1.0f) ? ring[(size_t) c][(size_t) (t & ringMask)] : ring[(size_t) c][(size_t) (t & ringMask)] * g;
            buffer.getWritePointer (c)[i] = y;
            mono += y / (float) nch;
        }
        // its wave for Kick Lock's scope (1 in 8, at the output's own position)
        if (playing && ++waveCounter >= kl::KickLinkMemory::waveDecimation)
        {
            waveCounter = 0;
            sender.pushWave (mono, timing.beatsAt (i) - (double) latency * timing.beatsPerSample,
                             (double) kl::KickLinkMemory::waveDecimation * timing.beatsPerSample);
        }
        ++clock;
    }
    telemetry.level.store (peak, std::memory_order_relaxed);
}
} // namespace kls
