// DSP tests: null test, stability, anti-zipper, preset change, block sizes, latency.

#include "TestHelpers.h"
#include <catch2/catch_approx.hpp>
#include <cstring>

using namespace fftest;
using ff::FrenchFilterProcessor;
namespace ts = gk::testsignals;

namespace
{
const int numPresets = FrenchFilterProcessor::loadBuiltInPresets().size();

std::unique_ptr<FrenchFilterProcessor> makeProc (int numChannels, double sr, int preset, float amount, int block = 512)
{
    auto p = std::make_unique<FrenchFilterProcessor>();
    setLayout (*p, numChannels);
    setParam (p->getPresetParameter(), (float) preset);
    setParam (p->getAmountParameter(), amount);
    p->prepareToPlay (sr, block);
    return p;
}

bool equalRange (const float* a, const float* b, int n)
{
    return std::memcmp (a, b, sizeof (float) * (size_t) n) == 0;
}
} // namespace

//==============================================================================
TEST_CASE ("1. Null test: knob at the centre is bit-exact with the latency-delayed input", "[dsp][null]")
{
    // 0.5 = centre; 64/127 and 63/127 are what a MIDI controller sends for
    // "the middle" and must fall in the dead zone.
    const float centres[] { 0.5f, 64.0f / 127.0f, 63.0f / 127.0f };
    for (double sr : sampleRates)
        for (int nch : { 1, 2 })
            for (int preset = 0; preset < numPresets; ++preset)
            {
                const float centre = centres[(size_t) preset % 3];
                CAPTURE (sr, nch, preset, centre);
                auto proc = makeProc (nch, sr, preset, centre);
                const int L = proc->getLatencySamples();
                REQUIRE (L > 0);

                const auto in = ts::whiteNoise (nch, (int) (0.5 * sr), 0.5f, 11u + (std::uint32_t) preset);
                const auto out = processBlocks (*proc, in, { 512 });
                const int n = in.getNumSamples();

                for (int ch = 0; ch < nch; ++ch)
                {
                    REQUIRE (juce::exactlyEqual (out.getMagnitude (ch, 0, L), 0.0f));
                    REQUIRE (equalRange (out.getReadPointer (ch, L), in.getReadPointer (ch), n - L));
                }
            }
}

TEST_CASE ("1b. Null test: back to bit-exact once the knob returns to the centre", "[dsp][null]")
{
    for (double sr : { 44100.0, 96000.0 })
        for (int preset = 0; preset < numPresets; ++preset)
            for (float knob : { 0.2f, 0.8f })
        {
            CAPTURE (sr, preset, knob);
            auto proc = makeProc (2, sr, preset, knob);
            const int L = proc->getLatencySamples();
            const auto in = ts::whiteNoise (2, (int) (0.8 * sr), 0.5f, 5u);
            const int release = (int) (0.3 * sr);
            const auto out = processBlocks (*proc, in, { 256 }, [&] (int pos)
                                            { setParam (proc->getAmountParameter(), pos >= release ? 0.5f : knob); });

            // 20 ms ramp + 10 ms crossfade + one block of slack, then the
            // output must be the dry signal again.
            const int from = release + (int) (0.035 * sr) + 256;
            for (int ch = 0; ch < 2; ++ch)
                REQUIRE (equalRange (out.getReadPointer (ch, from + L), in.getReadPointer (ch, from), in.getNumSamples() - from - L));
        }
}

//==============================================================================
TEST_CASE ("2. Stability: no NaN/Inf and output peak below 0 dBFS", "[dsp][stability]")
{
    struct Signal { const char* name; std::function<Buffer (double sr, int n)> make; };
    const std::vector<Signal> signals {
        { "sine sweep", [] (double sr, int n) { return ts::sineSweep (2, n, sr, 20.0, juce::jmin (20000.0, 0.45 * sr), 0.89f); } },
        { "white noise", [] (double, int n) { return ts::whiteNoise (2, n, 0.7f, 3u); } },
        { "impulses", [] (double sr, int n) { return ts::impulses (2, n, (int) (0.05 * sr), 1.0f); } },
        { "noise +6 dBFS", [] (double, int n) { return ts::whiteNoise (2, n, 2.0f, 9u); } },
    };
    const double sweepPeriods[] { 0.05, 0.5, 2.0 };

    for (double sr : sampleRates)
        for (int preset = 0; preset < numPresets; ++preset)
            for (const auto& sig : signals)
                for (double period : sweepPeriods)
                {
                    CAPTURE (sr, preset, sig.name, period);
                    const int n = (int) ((period + 0.2) * sr);
                    const auto in = sig.make (sr, n);
                    auto proc = makeProc (2, sr, preset, 0.5f);
                    // Triangle 0 -> 1 -> 0: both sides, crossing the centre
                    const auto out = processBlocks (*proc, in, { 64 }, [&] (int pos)
                                                    {
                                                        const double u = std::fmod ((double) pos / sr, period) / period;
                                                        setParam (proc->getAmountParameter(), (float) (1.0 - std::abs (1.0 - 2.0 * u)));
                                                    });
                    REQUIRE (allFinite (out));
                    REQUIRE (out.getMagnitude (0, n) < 1.0f);
                }
}

//==============================================================================
namespace
{
// Anti-zipper metric (see DECISIONS.md): loudest 5 ms burst of HF (> 8 kHz)
// energy of the output's second difference. A 0.1 dB instantaneous gain step
// on the sine (barely audible tick) scores about -72.5 dB; the threshold sits
// 7.5 dB below it.
constexpr double zipperThresholdDb = -80.0;

std::unique_ptr<FrenchFilterProcessor> makeZipperProc (int numChannels, double sr, int preset, float amount, int block)
{
    auto p = makeProc (numChannels, sr, preset, amount, block);
    p->getFrenchFilterDsp().setGritEnabledForTests (false);
    return p;
}
} // namespace

TEST_CASE ("3. Anti-zipper: 10 ms automation centre -> each end -> centre, and end to end", "[dsp][zipper]")
{
    const double sr = 48000.0;
    const int n = (int) (1.6 * sr);
    const auto in = ts::sine (1, n, sr, 220.0, 0.25f); // −12 dBFS

    // Knob moves (10 ms each): centre → full left → centre → full right →
    // centre → full left → full right (across the centre) → centre.
    auto amountAt = [sr] (int pos)
    {
        const double t = pos / sr;
        struct Seg { double t0; float from, to; };
        const Seg segs[] { { 0.10, 0.5f, 0.0f }, { 0.40, 0.0f, 0.5f }, { 0.60, 0.5f, 1.0f }, { 0.90, 1.0f, 0.5f },
                           { 1.10, 0.5f, 0.0f }, { 1.30, 0.0f, 1.0f }, { 1.45, 1.0f, 0.5f } };
        float v = 0.5f;
        for (const auto& sgm : segs)
            if (t >= sgm.t0)
                v = t < sgm.t0 + 0.01 ? sgm.from + (float) ((t - sgm.t0) / 0.01) * (sgm.to - sgm.from) : sgm.to;
        return v;
    };

    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        auto proc = makeZipperProc (1, sr, preset, 0.5f, 32);
        // Host automation resolution: 32 samples.
        const auto out = processBlocks (*proc, in, { 32 }, [&] (int pos) { setParam (proc->getAmountParameter(), amountAt (pos)); });
        const double burst = zipperBurstDb (out.getReadPointer (0), (int) (0.05 * sr), n, sr);
        INFO ("burst " << burst << " dB");
        CHECK (burst < zipperThresholdDb);
    }

    // Negative control: the metric must catch a 0.1 dB gain step on a peak.
    auto stepped = in;
    const int period = (int) (sr / 220.0), at = (int) (0.3 * sr) / period * period + period / 4;
    stepped.applyGain (0, at, n - at, gk::dspmath::dbToGain (0.1f));
    const double control = zipperBurstDb (stepped.getReadPointer (0), (int) (0.05 * sr), n, sr);
    INFO ("0.1 dB step control " << control << " dB");
    CHECK (control > zipperThresholdDb + 5.0);
}

TEST_CASE ("4. Preset change during playback: no discontinuity", "[dsp][zipper]")
{
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr);
    // −20 dBFS: at 100 % some presets resonate a 220 Hz sine by ~+11 dB; keep
    // the safety clipper out of a test that looks for discontinuities.
    const auto in = ts::sine (2, n, sr, 220.0, 0.1f);

    for (float amount : { 0.0f, 0.14f, 0.25f, 0.75f, 0.86f, 1.0f })
    {
        CAPTURE (amount);
        auto proc = makeZipperProc (2, sr, 0, amount, 128);
        const int sequence[] { 0, 3, 1, 2, 4, 5, 6, 7, 8, 0 };
        const auto out = processBlocks (*proc, in, { 128 }, [&] (int pos)
                                        { setParam (proc->getPresetParameter(), (float) sequence[juce::jmin (9, (int) (pos / (0.2 * sr)))]); });
        REQUIRE (allFinite (out));
        const double burst = zipperBurstDb (out.getReadPointer (0), (int) (0.1 * sr), n, sr);
        INFO ("burst " << burst << " dB");
        CHECK (burst < zipperThresholdDb);
    }
}

//==============================================================================
TEST_CASE ("5. Block sizes: identical output for any block partition", "[dsp][blocks]")
{
    const double sr = 48000.0;
    const int n = (int) (1.5 * sr);
    auto in = ts::whiteNoise (2, n, 0.3f, 21u);
    const auto tone = ts::sine (2, n, sr, 110.0, 0.4f);
    for (int ch = 0; ch < 2; ++ch)
        in.addFrom (ch, 0, tone, ch, 0, n);

    for (int preset : { 0, 3, 4 })
        for (float amount : { 0.08f, 0.3f, 0.92f })
        {
            CAPTURE (preset, amount);
            auto refProc = makeProc (2, sr, preset, amount, 512);
            const auto ref = processBlocks (*refProc, in, { 512 });

            juce::Random rng (1234);
            std::vector<int> randomSizes;
            for (int i = 0; i < 4000; ++i)
                randomSizes.push_back (1 + rng.nextInt (1000));

            const std::vector<std::pair<std::vector<int>, int>> partitions {
                { { 1 }, 1 }, { { 7 }, 7 }, { { 64 }, 64 }, { { 512 }, 512 }, { { 4096 }, 4096 }, { randomSizes, 1024 },
                { { 4096 }, 512 }, // host sending more than announced
            };

            for (const auto& [sizes, prepared] : partitions)
            {
                CAPTURE (sizes.front(), prepared);
                auto proc = makeProc (2, sr, preset, amount, prepared);
                const auto out = processBlocks (*proc, in, sizes);
                float maxDiff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        maxDiff = juce::jmax (maxDiff, std::abs (out.getSample (ch, i) - ref.getSample (ch, i)));
                INFO ("max difference " << maxDiff);
                CHECK (maxDiff <= 1.0e-6f);
            }
        }
}

//==============================================================================
namespace
{
int correlationPeakLag (const Buffer& in, const Buffer& out, int maxLag)
{
    int best = 0;
    double bestValue = -1.0;
    const int n = in.getNumSamples();
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < n; ++i)
            acc += (double) in.getSample (0, i) * out.getSample (0, i + lag);
        if (acc > bestValue)
        {
            bestValue = acc;
            best = lag;
        }
    }
    return best;
}
} // namespace

TEST_CASE ("8. Latency: reported latency matches the measured latency", "[dsp][latency]")
{
    for (double sr : sampleRates)
    {
        CAPTURE (sr);
        // Wet path only (knob at an end), processing disabled: pure 2x resampling.
        auto proc = makeProc (1, sr, 0, 1.0f);
        proc->getFrenchFilterDsp().setDiagnosticPassthrough (true);
        proc->setMakeupEnabled (false);
        const int L = proc->getLatencySamples();

        const int n = (int) (0.25 * sr);
        Buffer impulse (1, n);
        impulse.clear();
        const int at = n / 2;
        impulse.setSample (0, at, 0.5f);
        const auto out = processBlocks (*proc, impulse, { 256 });
        int peak = 0;
        for (int i = 0; i < n; ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak)))
                peak = i;
        CHECK (peak - at == L);

        auto proc2 = makeProc (1, sr, 0, 1.0f);
        proc2->getFrenchFilterDsp().setDiagnosticPassthrough (true);
        proc2->setMakeupEnabled (false);
        const auto noise = ts::whiteNoise (1, n, 0.5f, 77u);
        const auto out2 = processBlocks (*proc2, noise, { 256 });
        CHECK (correlationPeakLag (noise, out2, 4 * L) == L);

        // Full chain at the end of the transparency zone on the low side
        // (wet only, cutoff ~17.4 kHz): dry and wet are aligned up to the
        // low-pass filter's own (minimum-phase) group delay,
        // ~2.613 / (2·pi·fc) = 24 us for a 4th-order Butterworth — not
        // latency, and constant in time, so more samples at higher rates.
        FrenchFilterProcessor probe;
        auto proc3 = makeProc (1, sr, 0, probe.amountFor (-0.03f));
        const auto out3 = processBlocks (*proc3, noise, { 256 });
        const int lag = correlationPeakLag (noise, out3, 4 * L);
        const int filterDelay = (int) std::ceil (2.613 / (2.0 * gk::dspmath::pi * 17400.0) * sr);
        CAPTURE (lag, L, filterDelay);
        CHECK (lag >= L);
        CHECK (lag <= L + filterDelay);
    }
}
