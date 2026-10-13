// Backwash tests: convolver, null test, alignment, latency, swell length,
// stability, clicks, block sizes, state, focus, UI data, CPU.

#include "BackwashProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <cstring>

using namespace gktest;
using bw::BackwashProcessor;
namespace ts = gk::testsignals;

namespace
{
constexpr int numPresets = 5;

std::unique_ptr<BackwashProcessor> makeProc (int nch, double sr, int preset, float amount, int lg = -1, int sh = -1, int block = 512)
{
    auto p = std::make_unique<BackwashProcessor>();
    setLayout (*p, nch);
    p->setNonRealtime (true);
    setParam (p->getPresetParameter(), (float) preset);
    if (lg >= 0)
        setParam (p->getLongParameter(), (float) lg);
    if (sh >= 0)
        setParam (p->getShimmerParameter(), (float) sh);
    setParam (p->getAmountParameter(), amount);
    p->prepareToPlay (sr, block);
    return p;
}

/** Processes with a play head at `bpm` (or none), fixed block size. */
Buffer runAt (BackwashProcessor& p, const Buffer& in, double sr, double bpm, int block, std::function<void (int)> before = {})
{
    gk::OfflinePlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    ph.playing = true;
    if (bpm > 0.0)
        p.setPlayHead (&ph);
    Buffer out (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, out.getNumSamples() - pos);
        if (before)
            before (pos);
        ph.samplePosition = pos;
        Buffer view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        p.processBlock (view, midi);
    }
    p.setPlayHead (nullptr);
    return out;
}

Buffer withSilence (const Buffer& b, int before, int after)
{
    Buffer out (b.getNumChannels(), before + b.getNumSamples() + after);
    out.clear();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        out.copyFrom (ch, before, b, ch, 0, b.getNumSamples());
    return out;
}

/** wet = out - delayed dry, channel 0 */
std::vector<float> wetOf (const Buffer& out, const Buffer& in, int L)
{
    std::vector<float> w ((size_t) out.getNumSamples(), 0.0f);
    for (int i = 0; i < out.getNumSamples(); ++i)
        w[(size_t) i] = out.getSample (0, i) - (i >= L ? in.getSample (0, i - L) : 0.0f);
    return w;
}
} // namespace

//==============================================================================
TEST_CASE ("BW 0. Partitioned convolver matches direct convolution", "[bw][dsp]")
{
    const int P = 64, len = 1000;
    std::vector<float> irL ((size_t) len, 0.0f), irR ((size_t) len, 0.0f);
    ts::Rng rng (5);
    for (int i = 300; i < len; ++i)
    {
        irL[(size_t) i] = rng.bipolar();
        irR[(size_t) i] = rng.bipolar() * 0.5f;
    }
    auto k = gk::PartitionedConvolver::Kernel::build (irL.data(), irR.data(), len, P);
    gk::PartitionedConvolver conv;
    conv.prepare (P, len / P + 2);
    conv.setKernel (k.get());
    const int n = 3000;
    std::vector<float> x ((size_t) n), yl ((size_t) n), yr ((size_t) n);
    for (auto& v : x)
        v = rng.bipolar();
    for (int i = 0; i < n; ++i)
        conv.process (x[(size_t) i], yl[(size_t) i], yr[(size_t) i]);
    double maxErr = 0.0;
    for (int i = P; i < n; ++i)
    {
        double refL = 0.0, refR = 0.0;
        const int t = i - P; // latency P
        for (int j = 0; j < len && j <= t; ++j)
        {
            refL += (double) irL[(size_t) j] * x[(size_t) (t - j)];
            refR += (double) irR[(size_t) j] * x[(size_t) (t - j)];
        }
        maxErr = juce::jmax (maxErr, std::abs (refL - yl[(size_t) i]), std::abs (refR - yr[(size_t) i]));
    }
    INFO ("max error " << maxErr);
    CHECK (maxErr < 1.0e-3);
}

//==============================================================================
TEST_CASE ("BW 1. Null test: amount 0 is bit-exact with the input delayed by L", "[bw][null]")
{
    for (double sr : sampleRates)
        for (int nch : { 1, 2 })
            for (int preset = 0; preset < numPresets; ++preset)
                for (int combo : { 0, 1, 2, 3 })
                {
                    if (sr > 100000.0 && combo != 3 && preset != 0)
                        continue; // keep the 192 / 96 kHz runs short: one full set per rate
                    CAPTURE (sr, nch, preset, combo);
                    auto p = makeProc (nch, sr, preset, 0.0f, combo & 1, combo >> 1);
                    const int L = p->getLatencySamples();
                    REQUIRE (L == (int) std::round (2.0 * sr));
                    const auto in = ts::whiteNoise (nch, L + (int) (0.3 * sr), 0.5f, 7u);
                    const auto out = runAt (*p, in, sr, 0.0, 512);
                    for (int ch = 0; ch < nch; ++ch)
                        REQUIRE (std::memcmp (out.getReadPointer (ch, L), in.getReadPointer (ch), sizeof (float) * (size_t) (in.getNumSamples() - L)) == 0);
                }
}

//==============================================================================
TEST_CASE ("BW 2. Alignment: the swell stops at the hit (at most 1 ms after), never later", "[bw][align]")
{
    const double sr = 48000.0;
    for (double bpm : { 90.0, 124.0, 170.0 })
        for (int preset = 0; preset < numPresets; ++preset)
        {
            CAPTURE (bpm, preset);
            auto p = makeProc (2, sr, preset, 1.0f, 0, 0);
            const int L = p->getLatencySamples();
            const int at = (int) (3.0 * sr);
            Buffer in (2, at + L + (int) (1.0 * sr));
            in.clear();
            in.setSample (0, at, 0.8f);
            in.setSample (1, at, 0.8f);
            const auto out = runAt (*p, in, sr, bpm, 512);
            const auto wet = wetOf (out, in, L);
            float peak = 0.0f;
            for (auto v : wet)
                peak = juce::jmax (peak, std::abs (v));
            REQUIRE (peak > 1.0e-4f);
            // Energy right before the hit, nothing after hit + 1 ms
            const int hit = at + L, after = hit + (int) (0.001 * sr);
            float before = 0.0f, late = 0.0f;
            for (int i = hit - (int) (0.02 * sr); i < hit; ++i)
                before = juce::jmax (before, std::abs (wet[(size_t) i]));
            for (int i = after; i < (int) wet.size(); ++i)
                late = juce::jmax (late, std::abs (wet[(size_t) i]));
            INFO ("peak " << peak << ", last 20 ms " << before << ", after hit+1ms " << late);
            CHECK (before > 0.1f * peak);   // the swell is at its loudest just before the hit
            // and stops by hit + 1 ms: below -40 dB of the peak (what remains is
            // the few-ms ringing of the 2nd-order wash high-pass, DECISIONS.md)
            CHECK (late < 1.0e-2f * peak);
        }
}

//==============================================================================
TEST_CASE ("BW 3. Latency is exactly L and never changes", "[bw][latency]")
{
    for (double sr : sampleRates)
    {
        auto p = makeProc (2, sr, 0, 0.5f);
        const int L = (int) std::round (2.0 * sr);
        CHECK (p->getLatencySamples() == L);
        const auto in = ts::backbeatLoop (sr, 124.0, 2);
        runAt (*p, in, sr, 97.0, 256, [&] (int pos)
               {
                   setParam (p->getPresetParameter(), (float) ((pos / 20000) % numPresets));
                   setParam (p->getLongParameter(), (float) ((pos / 30000) % 2));
                   setParam (p->getShimmerParameter(), (float) ((pos / 25000) % 2));
               });
        CHECK (p->getLatencySamples() == L);
        p->prepareToPlay (sr, 64);
        CHECK (p->getLatencySamples() == L);
    }
}

//==============================================================================
TEST_CASE ("BW 4. Swell length follows the tempo, and is clamped at slow tempos", "[bw][length]")
{
    const double sr = 48000.0;
    for (int preset = 0; preset < numPresets; ++preset)
        for (int lg : { 0, 1 })
        {
            CAPTURE (preset, lg);
            auto p = makeProc (1, sr, preset, 1.0f, lg, 0);
            const int L = p->getLatencySamples();
            const int at = (int) (3.0 * sr);
            Buffer in (1, at + L + (int) (0.5 * sr));
            in.clear();
            in.setSample (0, at, 0.8f);
            const auto out = runAt (*p, in, sr, 124.0, 512);
            const auto wet = wetOf (out, in, L);
            float peak = 0.0f;
            for (auto v : wet)
                peak = juce::jmax (peak, std::abs (v));
            // The swell is exactly silent before it starts (the IR is zero there),
            // so its true length is from the first sample above -100 dB of the
            // peak to the last one above -40 dB. A -40 dB onset would miss the
            // start: with the spec's exponential rise, the first 11 % (k = 4) to
            // 22 % (k = 6) of the swell is below -40 dB by design (DECISIONS.md).
            int first = -1, last = -1;
            for (int i = 0; i < (int) wet.size(); ++i)
            {
                const float v = std::abs (wet[(size_t) i]);
                if (first < 0 && v > 1.0e-5f * peak)
                    first = i;
                if (v > 1.0e-2f * peak)
                    last = i;
            }
            const auto& pd = p->getBackwashDsp().getPresetData (preset);
            const double expected = pd.beats * (lg ? 2.0 : 1.0) * 60.0 / 124.0 * sr;
            const double measured = last - first;
            INFO ("measured " << measured / sr << " s, expected " << expected / sr << " s");
            CHECK (measured == Catch::Approx (expected).epsilon (0.05));
            CHECK_FALSE (p->getBackwashDsp().getTelemetry().swellClamped.load());
        }

    // 40 BPM, Ghost Pad + LONG = 4 beats = 6 s > 2 s lookahead: clamped
    auto p = makeProc (1, sr, 3, 1.0f, 1, 0);
    const auto in = ts::sine (1, (int) (0.5 * sr), sr, 440.0, 0.1f);
    runAt (*p, in, sr, 40.0, 512);
    CHECK (p->getBackwashDsp().getTelemetry().swellClamped.load());
    CHECK (p->getBackwashDsp().getTelemetry().swellSamples.load() < p->getLatencySamples());
}

//==============================================================================
TEST_CASE ("BW 5. Stability: no NaN/Inf, output peak below 0 dBFS", "[bw][stability]")
{
    for (double sr : { 44100.0, 96000.0 })
        for (int preset = 0; preset < numPresets; ++preset)
        {
            CAPTURE (sr, preset);
            const int L = (int) std::round (2.0 * sr);
            const int n = L + (int) (2.5 * sr);
            std::vector<std::pair<const char*, Buffer>> signals;
            signals.push_back ({ "noise +6 dBFS", ts::whiteNoise (2, n, 2.0f, 3u) });
            signals.push_back ({ "impulses", ts::impulses (2, n, (int) (0.1 * sr), 1.0f) });
            signals.push_back ({ "sweep", ts::sineSweep (2, n, sr, 20.0, 20000.0, 0.89f) });
            auto loop = ts::backbeatLoop (sr, 124.0, 6, 0.0f);
            loop.setSize (2, n, true, true);
            signals.push_back ({ "drums 0 dBFS", loop });
            for (const auto& [name, in] : signals)
            {
                CAPTURE (name);
                auto p = makeProc (2, sr, preset, 0.0f, 1, 1);
                const auto out = runAt (*p, in, sr, 124.0, 256, [&] (int pos)
                                        {
                                            const double u = std::fmod ((double) pos / sr, 1.5) / 1.5;
                                            setParam (p->getAmountParameter(), (float) (1.0 - std::abs (1.0 - 2.0 * u)));
                                        });
                REQUIRE (allFinite (out));
                REQUIRE (out.getMagnitude (0, out.getNumSamples()) < 1.0f);
            }
        }
}

//==============================================================================
TEST_CASE ("BW 6. No clicks when changing preset, LONG, SHIMMER or tempo while playing", "[bw][zipper]")
{
    // Ghost Pad (focus 0) on a sustained sine: the wet is a steady sine, so any
    // discontinuity in an IR change would show up as a burst.
    const double sr = 48000.0;
    const int L = (int) std::round (2.0 * sr);
    const auto in = ts::sine (1, L + (int) (4.0 * sr), sr, 440.0, 0.25f);
    struct Change { const char* name; std::function<void (BackwashProcessor&, int)> apply; double bpm; };
    const std::vector<Change> changes {
        { "preset", [] (BackwashProcessor& p, int step) { setParam (p.getPresetParameter(), step % 2 ? 1.0f : 3.0f); }, 124.0 },
        { "long", [] (BackwashProcessor& p, int step) { setParam (p.getLongParameter(), (float) (step % 2)); }, 124.0 },
        { "shimmer", [] (BackwashProcessor& p, int step) { setParam (p.getShimmerParameter(), (float) (step % 2)); }, 124.0 },
    };
    for (const auto& c : changes)
    {
        CAPTURE (c.name);
        auto p = makeProc (1, sr, 3, 1.0f, 0, 0, 256);
        const auto out = runAt (*p, in, sr, c.bpm, 256, [&] (int pos) { if (pos > L + (int) (0.5 * sr)) c.apply (*p, (pos - L) / (int) (0.6 * sr)); });
        const double burst = zipperBurstDb (out.getReadPointer (0), L + (int) (0.6 * sr), out.getNumSamples(), sr);
        INFO ("burst " << burst << " dB");
        CHECK (burst < -80.0);
    }

    // Tempo changes (play head tempo moves every 0.6 s)
    auto p = makeProc (1, sr, 3, 1.0f, 0, 0, 256);
    gk::OfflinePlayHead ph;
    ph.sampleRate = sr;
    p->setPlayHead (&ph);
    Buffer out (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += 256)
    {
        ph.bpm = pos > L ? (((pos - L) / (int) (0.6 * sr)) % 2 ? 128.0 : 118.0) : 118.0;
        ph.samplePosition = pos;
        Buffer view (out.getArrayOfWritePointers(), 1, pos, juce::jmin (256, out.getNumSamples() - pos));
        p->processBlock (view, midi);
    }
    p->setPlayHead (nullptr);
    const double burst = zipperBurstDb (out.getReadPointer (0), L + (int) (0.6 * sr), out.getNumSamples(), sr);
    INFO ("tempo burst " << burst << " dB");
    CHECK (burst < -80.0);
}

//==============================================================================
TEST_CASE ("BW 7. Block sizes: identical output for any block partition", "[bw][blocks]")
{
    const double sr = 48000.0;
    auto in = ts::backbeatLoop (sr, 124.0, 3);
    in.setSize (2, in.getNumSamples() + (int) (2.2 * sr), true, true);
    for (int preset : { 0, 4 })
    {
        CAPTURE (preset);
        auto ref = makeProc (2, sr, preset, 1.0f, 1, 1, 512);
        const auto r = runAt (*ref, in, sr, 0.0, 512);
        juce::Random rng (3);
        std::vector<int> sizes;
        for (int i = 0; i < 4000; ++i)
            sizes.push_back (1 + rng.nextInt (1000));
        const std::vector<std::pair<std::vector<int>, int>> partitions {
            { { 1 }, 1 }, { { 7 }, 7 }, { { 64 }, 64 }, { { 4096 }, 4096 }, { sizes, 1024 }, { { 4096 }, 512 }
        };
        for (const auto& [blocks, prepared] : partitions)
        {
            CAPTURE (blocks.front(), prepared);
            auto p = makeProc (2, sr, preset, 1.0f, 1, 1, prepared);
            const auto out = processBlocks (*p, in, blocks);
            float maxDiff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    maxDiff = juce::jmax (maxDiff, std::abs (out.getSample (ch, i) - r.getSample (ch, i)));
            INFO ("max difference " << maxDiff);
            CHECK (maxDiff <= 1.0e-6f);
        }
    }
}

//==============================================================================
TEST_CASE ("BW 8. State round trip, preset chip defaults", "[bw][state]")
{
    BackwashProcessor a;
    setParam (a.getAmountParameter(), 0.66f);
    setParam (a.getPresetParameter(), 2.0f);
    setParam (a.getShimmerParameter(), 1.0f); // user override (Clap Rewind: shimmer off by default)
    setParam (a.getBypassParameterObject(), 1.0f);
    a.editorScale.store (1.5f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    BackwashProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getAmountParameter().getValue() == Catch::Approx (0.66f).margin (1.0e-6));
    CHECK (b.getPresetParameter().getIndex() == 2);
    CHECK (b.getShimmerParameter().get());
    CHECK_FALSE (b.getLongParameter().get());
    CHECK (b.getBypassParameterObject().get());
    CHECK (b.editorScale.load() == Catch::Approx (1.5f));

    BackwashProcessor c;
    c.setCurrentProgram (3); // Ghost Pad: long on
    CHECK (c.getLongParameter().get());
    CHECK_FALSE (c.getShimmerParameter().get());
    c.setCurrentProgram (4); // Big Inhale: shimmer on
    CHECK_FALSE (c.getLongParameter().get());
    CHECK (c.getShimmerParameter().get());
    CHECK (c.getPresetParameter().isMetaParameter());
    const char* ids[] { "amount", "preset", "long", "shimmer", "bypass" };
    for (int i = 0; i < 5; ++i)
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (c.getParameters()[i])->getParameterID() == ids[i]);
}

//==============================================================================
TEST_CASE ("BW 9. Focus 1: a held note without attack makes almost no swell", "[bw][focus]")
{
    // A held note (already sounding: we measure the swell its sustain makes,
    // not its onset — any onset from silence looks like an attack) against a
    // snare, both through Clap Rewind (focus 1).
    const double sr = 48000.0;
    const int L = (int) std::round (2.0 * sr), pre = (int) (2.5 * sr);
    auto wetEnergy = [&] (const Buffer& sig, double fromS, double toS)
    {
        auto p = makeProc (1, sr, 2, 1.0f, 0, 0);
        const auto padded = withSilence (sig, pre, L + (int) (0.5 * sr));
        const auto out = runAt (*p, padded, sr, 124.0, 512);
        const auto wet = wetOf (out, padded, L);
        double e = 0.0;
        for (int i = pre + L + (int) (fromS * sr); i < juce::jmin ((int) wet.size(), pre + L + (int) (toS * sr)); ++i)
            e += (double) wet[(size_t) i] * wet[(size_t) i];
        return e;
    };
    // Held note: 600 ms fade-in, then sustained; the swells of its sustain
    // land in [1.2 s, 3.0 s] (input time + L)
    auto held = ts::sine (1, (int) (3.0 * sr), sr, 330.0, 0.5f);
    for (int i = 0; i < (int) (0.6 * sr); ++i)
        held.setSample (0, i, held.getSample (0, i) * (float) i / (float) (0.6 * sr));
    // Snare (from the drum loop, beat 2): its whole swell
    const auto loop = ts::backbeatLoop (sr, 124.0, 1);
    Buffer snare (1, (int) (0.3 * sr));
    snare.copyFrom (0, 0, loop, 0, (int) (60.0 / 124.0 * sr), snare.getNumSamples());
    const double ratioDb = 10.0 * std::log10 (wetEnergy (held, 1.2, 3.0) / wetEnergy (snare, -1.0, 0.3));
    INFO ("held note (sustain) vs snare: " << ratioDb << " dB");
    CHECK (ratioDb < -30.0);
}

//==============================================================================
TEST_CASE ("BW 10. UI data: samplesToNextHit announces the hits (< 5 ms error)", "[bw][ui]")
{
    const double sr = 48000.0, bpm = 124.0;
    auto p = makeProc (2, sr, 0, 0.8f);
    const int L = p->getLatencySamples();
    const auto in = ts::backbeatLoop (sr, bpm, 4);
    const int beat = (int) std::round (60.0 / bpm * sr);
    gk::OfflinePlayHead ph;
    ph.sampleRate = sr;
    ph.bpm = bpm;
    p->setPlayHead (&ph);
    Buffer io (in);
    io.setSize (2, in.getNumSamples() + L, true, true);
    juce::MidiBuffer midi;
    int checked = 0;
    double worstMs = 0.0;
    for (int pos = 0; pos < io.getNumSamples(); pos += 256)
    {
        const int n = juce::jmin (256, io.getNumSamples() - pos);
        ph.samplePosition = pos;
        Buffer view (io.getArrayOfWritePointers(), 2, pos, n);
        p->processBlock (view, midi);
        const int toNext = p->getBackwashDsp().getTelemetry().samplesToNextHit.load();
        if (toNext < 0)
            continue;
        // The kicks are on every beat: input hit k at k·beat is heard at k·beat + L
        const int heardAt = pos + n + toNext;
        const int k = (int) std::round ((double) (heardAt - L) / beat);
        if (k >= 0 && k < 16)
        {
            worstMs = juce::jmax (worstMs, std::abs ((double) (heardAt - (k * beat + L))) / sr * 1000.0);
            ++checked;
        }
    }
    p->setPlayHead (nullptr);
    INFO ("checked " << checked << ", worst error " << worstMs << " ms");
    CHECK (checked > 100);
    CHECK (worstMs < 5.0);
}

//==============================================================================
TEST_CASE ("BW 13. CPU: cost of the convolution, stereo 48 kHz", "[bw][cpu]")
{
    const double sr = 48000.0;
    for (int preset : { 0, 3, 4 })
    {
        auto p = makeProc (2, sr, preset, 1.0f, 1, preset == 4 ? 1 : 0);
        const auto in = ts::backbeatLoop (sr, 124.0, 10);
        Buffer io (in);
        juce::MidiBuffer midi;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < io.getNumSamples(); pos += 256)
        {
            Buffer view (io.getArrayOfWritePointers(), 2, pos, juce::jmin (256, io.getNumSamples() - pos));
            p->processBlock (view, midi);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        const double audioMs = 1000.0 * in.getNumSamples() / sr;
        WARN ("preset " << preset << ": " << juce::String (100.0 * ms / audioMs, 2) << " % of one core (" << ms << " ms for "
                        << audioMs << " ms of audio)");
        CHECK (ms < audioMs * 0.2);
    }
}

