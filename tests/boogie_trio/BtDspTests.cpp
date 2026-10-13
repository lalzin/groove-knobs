// Boogie Trio DSP tests: null test, stability, anti-zipper, mono
// compatibility, hiss, block sizes, latency.

#include "BtTestHelpers.h"
#include <cstring>

using namespace bttest;
namespace ts = gk::testsignals;

namespace
{
bool equalRange (const float* a, const float* b, int n) { return std::memcmp (a, b, sizeof (float) * (size_t) n) == 0; }
} // namespace

//==============================================================================
TEST_CASE ("BT 1. Null test: amount 0 is bit-exact with the input", "[bt][dsp][null]")
{
    for (double sr : sampleRates)
        for (auto layout : { Layout::mono, Layout::stereo, Layout::monoToStereo })
            for (int preset = 0; preset < numPresets; ++preset)
                for (int combo = 0; combo < 4; ++combo)
                {
                    const int vintage = combo & 1, wide = (combo >> 1) & 1;
                    CAPTURE (sr, (int) layout, preset, vintage, wide);
                    auto proc = makeProc (layout, sr, preset, 0.0f, vintage, wide);
                    REQUIRE (proc->getLatencySamples() == 0);
                    const int inCh = layout == Layout::stereo ? 2 : 1, outCh = layout == Layout::mono ? 1 : 2;
                    const auto in = ts::whiteNoise (inCh, (int) (0.25 * sr), 0.5f, 3u + (std::uint32_t) preset);
                    const auto out = run (*proc, in, outCh, { 512 });
                    for (int ch = 0; ch < outCh; ++ch)
                        REQUIRE (equalRange (out.getReadPointer (ch), in.getReadPointer (juce::jmin (ch, inCh - 1)), in.getNumSamples()));
                }
}

TEST_CASE ("BT 1b. Null test: back to bit-exact after returning to 0", "[bt][dsp][null]")
{
    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        const double sr = 48000.0;
        auto proc = makeProc (Layout::stereo, sr, preset, 0.9f);
        const auto in = ts::whiteNoise (2, (int) (0.8 * sr), 0.5f, 9u);
        const int release = (int) (0.3 * sr);
        const auto out = run (*proc, in, 2, { 256 }, [&] (int pos) { setParam (proc->getAmountParameter(), pos >= release ? 0.0f : 0.9f); });
        const int from = release + (int) (0.035 * sr) + 256;
        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (equalRange (out.getReadPointer (ch, from), in.getReadPointer (ch, from), in.getNumSamples() - from));
    }
}

//==============================================================================
TEST_CASE ("BT 2. Stability: no NaN/Inf and output peak below 0 dBFS", "[bt][dsp][stability]")
{
    struct Signal { const char* name; std::function<Buffer (double sr, int n)> make; };
    const std::vector<Signal> signals {
        { "sine sweep", [] (double sr, int n) { return ts::sineSweep (2, n, sr, 20.0, juce::jmin (20000.0, 0.45 * sr), 0.89f); } },
        { "white noise", [] (double, int n) { return ts::whiteNoise (2, n, 0.7f, 3u); } },
        { "impulses", [] (double sr, int n) { return ts::impulses (2, n, (int) (0.05 * sr), 1.0f); } },
        { "noise +6 dBFS", [] (double, int n) { return ts::whiteNoise (2, n, 2.0f, 9u); } },
    };
    for (double sr : sampleRates)
        for (int preset = 0; preset < numPresets; ++preset)
            for (int combo : { 0, 3 })
                for (const auto& sig : signals)
                    for (double period : { 0.05, 0.5, 2.0 })
                    {
                        CAPTURE (sr, preset, combo, sig.name, period);
                        const int n = (int) ((period + 0.2) * sr);
                        const auto in = sig.make (sr, n);
                        auto proc = makeProc (Layout::stereo, sr, preset, 0.0f, combo & 1, combo >> 1);
                        const auto out = run (*proc, in, 2, { 64 }, [&] (int pos)
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
constexpr double zipperThresholdDb = -80.0; // same metric and threshold as French Filter (DECISIONS.md)
}

TEST_CASE ("BT 3. Anti-zipper: 10 ms automation 0 -> 1 -> 0", "[bt][dsp][zipper]")
{
    const double sr = 48000.0;
    const int n = (int) (0.8 * sr);
    const auto in = ts::sine (1, n, sr, 440.0, 0.25f);
    auto amountAt = [sr] (int pos)
    {
        const double t = pos / sr;
        if (t < 0.1) return 0.0f;
        if (t < 0.11) return (float) ((t - 0.1) / 0.01);
        if (t < 0.4) return 1.0f;
        if (t < 0.41) return (float) (1.0 - (t - 0.4) / 0.01);
        return 0.0f;
    };
    for (int preset = 0; preset < numPresets; ++preset)
        for (int combo = 0; combo < 4; ++combo)
        {
            CAPTURE (preset, combo);
            auto proc = makeProc (Layout::monoToStereo, sr, preset, 0.0f, combo & 1, combo >> 1, 32);
            proc->getBoogieTrioDsp().setHissEnabledForTests (false);
            const auto out = run (*proc, in, 2, { 32 }, [&] (int pos) { setParam (proc->getAmountParameter(), amountAt (pos)); });
            for (int ch = 0; ch < 2; ++ch)
            {
                const double burst = zipperBurstDb (out.getReadPointer (ch), (int) (0.05 * sr), n, sr);
                INFO ("burst " << burst << " dB");
                CHECK (burst < zipperThresholdDb);
            }
        }
}

TEST_CASE ("BT 3b. Anti-zipper: switching VINTAGE and WIDE while playing", "[bt][dsp][zipper]")
{
    const double sr = 48000.0;
    const int n = (int) (1.4 * sr);
    const auto in = ts::sine (1, n, sr, 440.0, 0.25f);
    for (int preset = 0; preset < numPresets; ++preset)
        for (float amount : { 0.5f, 1.0f })
        {
            CAPTURE (preset, amount);
            auto proc = makeProc (Layout::monoToStereo, sr, preset, amount, 0, 0, 128);
            proc->getBoogieTrioDsp().setHissEnabledForTests (false);
            const auto out = run (*proc, in, 2, { 128 }, [&] (int pos)
                                  {
                                      const int step = (int) (pos / (0.2 * sr));
                                      setParam (proc->getVintageParameter(), (step == 1 || step == 2 || step == 5) ? 1.0f : 0.0f);
                                      setParam (proc->getWideParameter(), (step == 2 || step == 3 || step == 5) ? 1.0f : 0.0f);
                                  });
            for (int ch = 0; ch < 2; ++ch)
            {
                const double burst = zipperBurstDb (out.getReadPointer (ch), (int) (0.1 * sr), n, sr);
                INFO ("burst " << burst << " dB");
                CHECK (burst < zipperThresholdDb);
            }
        }
}

//==============================================================================
TEST_CASE ("BT 4. Mono compatibility: the L+R sum stays the input (\"Wider\" style), within 1.5 dB", "[bt][dsp][mono]")
{
    const double sr = 48000.0;
    const auto pad = monoOf (ts::chordPad (sr, 3.0, -6.0f));
    const auto keys = monoOf (ts::electricPianoLoop (sr, 124.0, 2));
    for (int preset = 0; preset < numPresets; ++preset)
        for (int combo = 0; combo < 4; ++combo)
            for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                for (const auto* sig : { &pad, &keys })
                {
                    CAPTURE (preset, combo, amount);
                    auto proc = makeProc (Layout::monoToStereo, sr, preset, amount, combo & 1, combo >> 1);
                    proc->setMakeupEnabled (false);
                    const auto out = run (*proc, *sig, 2, { 512 });
                    double dry = 0.0, mono = 0.0;
                    for (int i = (int) (0.3 * sr); i < out.getNumSamples(); ++i)
                    {
                        const double l = out.getSample (0, i), r = out.getSample (1, i), x = sig->getSample (0, i);
                        dry += x * x;
                        mono += 0.25 * (l + r) * (l + r);
                    }
                    // Mono sum vs the dry input: the centre is untouched, only
                    // the tremolo / gate effects may change its level.
                    const double lossDb = 10.0 * std::log10 (dry / juce::jmax (1.0e-20, mono));
                    INFO ("mono loss " << lossDb << " dB");
                    CHECK (lossDb <= 1.5);
                }
}

//==============================================================================
TEST_CASE ("BT 5. Hiss: VINTAGE on silence stays below -100 dBFS", "[bt][dsp][hiss]")
{
    const double sr = 48000.0;
    Buffer silence (2, (int) (1.0 * sr));
    silence.clear();
    for (int preset = 0; preset < numPresets; ++preset)
        for (float amount : { 0.5f, 1.0f })
        {
            CAPTURE (preset, amount);
            auto proc = makeProc (Layout::stereo, sr, preset, amount, 1, 1);
            const auto out = run (*proc, silence, 2, { 512 });
            CHECK (gk::dspmath::gainToDb (out.getMagnitude (0, out.getNumSamples())) < -100.0f);
        }

    // And after music stops: the hiss follows the input envelope down.
    auto proc = makeProc (Layout::stereo, sr, 0, 1.0f, 1, 1);
    auto in = ts::chordPad (sr, 2.0, -6.0f);
    in.setSize (2, (int) (4.0 * sr), true, true);
    const auto out = run (*proc, in, 2, { 512 });
    const int tailStart = (int) (3.5 * sr);
    CHECK (gk::dspmath::gainToDb (out.getMagnitude (tailStart, out.getNumSamples() - tailStart)) < -100.0f);
}

TEST_CASE ("BT 5b. Pink noise generator is unit RMS (hiss calibrated to -72 dBFS)", "[bt][dsp][hiss]")
{
    gk::PinkNoise pink (7u);
    double acc = 0.0;
    const int n = 480000;
    for (int i = 0; i < n; ++i)
    {
        const double v = pink.next();
        acc += v * v;
    }
    const double rmsDb = 10.0 * std::log10 (acc / n);
    INFO ("pink RMS " << rmsDb << " dB");
    CHECK (std::abs (rmsDb) < 1.0);
}

//==============================================================================
TEST_CASE ("BT 6. Block sizes: identical output for any block partition", "[bt][dsp][blocks]")
{
    const double sr = 48000.0;
    const auto in = ts::chordPad (sr, 1.5, -6.0f);
    for (int preset : { 0, 2, 4 })
        for (float amount : { 0.3f, 1.0f })
        {
            CAPTURE (preset, amount);
            auto refProc = makeProc (Layout::stereo, sr, preset, amount, 1, 1, 512);
            const auto ref = run (*refProc, in, 2, { 512 });

            juce::Random rng (99);
            std::vector<int> randomSizes;
            for (int i = 0; i < 4000; ++i)
                randomSizes.push_back (1 + rng.nextInt (1000));
            const std::vector<std::pair<std::vector<int>, int>> partitions {
                { { 1 }, 1 }, { { 7 }, 7 }, { { 64 }, 64 }, { { 512 }, 512 }, { { 4096 }, 4096 }, { randomSizes, 1024 }, { { 4096 }, 512 },
            };
            for (const auto& [sizes, prepared] : partitions)
            {
                CAPTURE (sizes.front(), prepared);
                auto proc = makeProc (Layout::stereo, sr, preset, amount, 1, 1, prepared);
                const auto out = run (*proc, in, 2, sizes);
                float maxDiff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        maxDiff = juce::jmax (maxDiff, std::abs (out.getSample (ch, i) - ref.getSample (ch, i)));
                INFO ("max difference " << maxDiff);
                CHECK (maxDiff <= 1.0e-6f);
            }
        }
}

TEST_CASE ("BT 6b. Bass stays dry and mono below the wet high-pass", "[bt][dsp]")
{
    // A 60 Hz sine through the full effect must come out (almost) unchanged and centred.
    const double sr = 48000.0;
    const auto in = ts::sine (1, (int) (1.0 * sr), sr, 60.0, 0.5f);
    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        auto proc = makeProc (Layout::monoToStereo, sr, preset, 1.0f, 1, 1);
        proc->setMakeupEnabled (false);
        const auto out = run (*proc, in, 2, { 512 });
        const int s0 = (int) (0.3 * sr), len = out.getNumSamples() - s0;
        const float inRms = in.getRMSLevel (0, s0, len);
        for (int ch = 0; ch < 2; ++ch)
            CHECK (std::abs (gk::dspmath::gainToDb (out.getRMSLevel (ch, s0, len) / inRms)) < 1.0f);
        CHECK (std::abs (gk::dspmath::gainToDb (out.getRMSLevel (0, s0, len) / out.getRMSLevel (1, s0, len))) < 0.25f);
    }
}
