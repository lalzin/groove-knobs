// Kick Lock tests: phase accuracy and convergence, glide, confidence, freeze,
// null, ducking, no sidechain, mono sum, crossover, latency, automation,
// block sizes, stability, state, CPU.

#include "KickLockProcessor.h"
#include "GkTestUtils.h"
#include "KickBass.h"
#include "KickSendProcessor.h"
#include <catch2/catch_approx.hpp>
#include <numeric>

using namespace gktest;
using namespace kickbass;
using kl::KickLockDsp;
using kl::KickLockProcessor;
using kl::Status;

namespace
{
// the link's shared block: one of the tests' own (a Live running on this
// machine with Kick Lock Send must never be seen by the tests, nor disturbed)
const bool testLinkBlock = []
{
    kl::KickLink::blockName() = "gk_kl_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30));
    return true;
}();

std::unique_ptr<KickLockProcessor> makeKl (double sr, float mix, bool lock, float depth = 1.0f, int block = 512)
{
    auto p = std::make_unique<KickLockProcessor>();
    p->setNonRealtime (true);
    setParam (p->getMixParameter(), mix);
    setParam (p->getLockParameter(), lock ? 1.0f : 0.0f);
    setParam (p->getDepthParameter(), depth);
    p->prepareToPlay (sr, block);
    return p;
}

/** Phase errors (degrees) of the output's fundamental at each kick heard after `from` seconds. */
std::vector<double> errorsAtKicks (const Buffer& out, int latency, double hz, double sr, const std::vector<double>& kicks, double from, double to = 1.0e9,
                                   double target = 0.0)
{
    std::vector<double> e;
    for (double k : kicks)
        if (k >= from && k < to)
        {
            const int at = (int) std::round (k * sr) + latency;
            if (at + (int) (2.0 * sr / hz) < out.getNumSamples())
                e.push_back (KickLockDsp::wrap180 (phaseAt (out.getReadPointer (0), out.getNumSamples(), at, hz, sr) - target));
        }
    return e;
}

double maxAbs (const std::vector<double>& v)
{
    double m = 0.0;
    for (auto x : v)
        m = juce::jmax (m, std::abs (x));
    return m;
}
} // namespace

//==============================================================================
TEST_CASE ("KL 1. Phase accuracy: every note 40-110 Hz, the kick -10..+10 ms off, error < 5 deg after convergence", "[kl][phase]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0;
    juce::String table;
    double worst = 0.0;
    for (double hz : { 40.0, 45.0, 49.0, 55.0, 61.7, 65.4, 73.4, 82.4, 98.0, 110.0 })
        for (double off : { -0.010, -0.004, 0.0, 0.003, 0.010 })
        {
            auto p = makeKl (sr, 1.0f, true, 0.0f); // depth 0: the phase is measured on the full bass
            const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
            const auto k = kicks (seconds, sr, bpm, 0.25 + off);
            const auto out = run (*p, b, &k, { 512 }, bpm);
            const auto e = errorsAtKicks (out, p->getLatencySamples(), hz, sr, kickTimes (seconds, bpm, 0.25 + off), 1.0, seconds - 0.2);
            const double m = maxAbs (e);
            worst = juce::jmax (worst, m);
            if (juce::exactlyEqual (off, 0.0))
                table << juce::String (hz, 1) << " Hz: " << juce::String (m, 2) << " deg; ";
            INFO (hz << " Hz, kick " << off * 1000.0 << " ms: worst error " << m << " deg over " << e.size() << " kicks");
            CHECK (e.size() >= 4);
            CHECK (m < 5.0);
        }
    WARN ("per note (kick on the grid): " << table << "worst of all: " << worst << " deg");
}

TEST_CASE ("KL 2. Convergence: after a note change, under 5 deg within 1 s", "[kl][phase]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 6.0;
    auto p = makeKl (sr, 1.0f, true, 0.0f);
    const auto b = bass ({ { 0.0, 3.0, 55.0 }, { 3.0, 3.0, 73.4 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.1);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const auto times = kickTimes (seconds, bpm, 0.1);
    // every kick from 1 s after the change on
    const auto e = errorsAtKicks (out, p->getLatencySamples(), 73.4, sr, times, 3.0 + 1.0, seconds - 0.2);
    // and the first kick after the change (convergence time)
    double firstGood = -1.0;
    for (double t : times)
        if (t > 3.05 && t < seconds - 0.2)
        {
            const auto one = errorsAtKicks (out, p->getLatencySamples(), 73.4, sr, { t }, 0.0);
            if (! one.empty() && std::abs (one[0]) < 5.0)
            {
                firstGood = t - 3.0;
                break;
            }
        }
    INFO ("worst error from 1 s after the change: " << maxAbs (e) << " deg; first kick under 5 deg " << firstGood << " s after the change");
    CHECK (maxAbs (e) < 5.0);
    CHECK (firstGood >= 0.0);
    CHECK (firstGood < 1.0);
}

TEST_CASE ("KL 3. Glide 55 -> 110 Hz in 200 ms: no jump in the output", "[kl][phase]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    Buffer b (2, (int) (seconds * sr));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double f = t < 1.5 ? 55.0 : t < 1.7 ? 55.0 * std::pow (2.0, (t - 1.5) / 0.2) : 110.0;
        ph += f / sr;
        const float v = (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * ph));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 1.0f, true, 0.0f);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    // largest sample-to-sample step: the input's (110 Hz, 0.4) is ~0.0058
    float step = 0.0f;
    for (int i = (int) (0.5 * sr); i + 1 < out.getNumSamples(); ++i)
        step = juce::jmax (step, std::abs (out.getSample (0, i + 1) - out.getSample (0, i)));
    INFO ("largest step " << step);
    CHECK (step < 0.012f);
}

TEST_CASE ("KL 4. Confidence: a noise or FM sub is never OK", "[kl][confidence]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0;
    for (int kind : { 0, 1 })
    {
        Buffer b (2, (int) (seconds * sr));
        if (kind == 0)
        {
            gk::testsignals::Rng rng (3);
            gk::TptSvf lp;
            const auto c = gk::TptSvf::Coeffs::make (90.0, sr, 0.7);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = (float) (2.0 * lp.processLowpass (rng.bipolar(), c));
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        }
        else
            b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr, true, 0.25); // deep FM
        const auto k = kicks (seconds, sr, bpm, 0.1);
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        int ok = 0, unstable = 0;
        run (*p, b, &k, { 512 }, bpm, [&] (int pos)
             {
                 if (pos < (int) (1.0 * sr))
                     return;
                 const auto st = (Status) p->getDsp().getTelemetry().status.load();
                 ok += st == Status::ok ? 1 : 0;
                 unstable += st == Status::unstable ? 1 : 0;
             });
        INFO ((kind == 0 ? "noise" : "FM") << ": OK " << ok << " blocks, unstable " << unstable);
        CHECK (ok == 0);
        CHECK (unstable > 0);
    }
}

TEST_CASE ("KL 5. Freeze: below 0.7 confidence the correction does not move (< 2 deg, 0.1 ms at 61.7 Hz)", "[kl][confidence]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 5.0;
    // a steady note (locked), then noise: the shift stays where it was
    auto steady = bass ({ { 0.0, 2.5, 61.7 } }, seconds, sr, true);
    gk::testsignals::Rng rng (8);
    gk::TptSvf lp;
    const auto c = gk::TptSvf::Coeffs::make (80.0, sr, 0.7);
    for (int i = (int) (2.5 * sr); i < steady.getNumSamples(); ++i)
    {
        const float v = (float) (2.0 * lp.processLowpass (rng.bipolar(), c));
        steady.setSample (0, i, v);
        steady.setSample (1, i, v);
    }
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 1.0f, true, 0.0f);
    std::vector<double> shifts;
    run (*p, steady, &k, { 512 }, bpm, [&] (int pos)
         {
             if (pos > (int) (2.8 * sr))
                 shifts.push_back (p->getDsp().currentRotationDeg());
         });
    const auto [lo, hi] = std::minmax_element (shifts.begin(), shifts.end());
    INFO ("rotation during the noise: " << *lo << " .. " << *hi << " deg");
    CHECK (*hi - *lo < 2.0);
}

TEST_CASE ("KL 6. Null: MIX 0 is bit-exact with the input delayed by the latency, LOCK on or off", "[kl][null]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 2.0;
    for (bool lock : { false, true })
    {
        auto p = makeKl (sr, 0.0f, lock);
        const int L = p->getLatencySamples();
        const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
        const auto k = kicks (seconds, sr, bpm);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        bool exact = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                exact = exact && juce::exactlyEqual (out.getSample (c, i), i >= L ? b.getSample (c, i - L) : 0.0f);
        INFO ("lock " << lock);
        CHECK (exact);
    }
}

TEST_CASE ("KL 7. Ducking: the gain is lowest at the kick (+-1 ms) and comes back over the release", "[kl][duck]")
{
    // (the kicks every 2 beats, so a 1/4 release ends before the next one)
    const double sr = 48000.0, bpm = 124.0, seconds = 3.5;
    Buffer b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b.setSample (c, i, 0.25f); // DC: the gain itself (the sub filter passes DC, no shift with LOCK off)
    for (int rel : { 0, 2, 3 })
    {
        auto p = makeKl (sr, 1.0f, false, 1.0f);
        setParam (p->getReleaseParameter(), (float) rel);
        const auto k = kicks (seconds, sr, bpm / 2.0, 0.5);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        const int L = p->getLatencySamples();
        const int kick = (int) std::round ((0.5 + 2.0 * 120.0 / bpm) * sr) + L; // the third kick
        int argMin = kick;
        for (int i = kick - (int) (0.01 * sr); i < kick + (int) (0.01 * sr); ++i)
            if (out.getSample (0, i) < out.getSample (0, argMin))
                argMin = i;
        const double releaseMs = KickLockDsp::releaseBeats (rel) * 60000.0 / bpm;
        const int back = kick + (int) std::round (releaseMs * 0.001 * sr);
        INFO ("release " << releaseMs << " ms: minimum at " << (argMin - kick) / sr * 1000.0 << " ms from the kick, gain " << out.getSample (0, argMin) / 0.25f
                         << ", back to " << out.getSample (0, back + 48) / 0.25f << " after the release");
        CHECK (std::abs (argMin - kick) <= (int) (0.001 * sr));
        CHECK (out.getSample (0, argMin) < 0.01f);
        CHECK (out.getSample (0, back + 48) == Catch::Approx (0.25f).margin (0.002f));
        CHECK (out.getSample (0, kick + (int) (0.5 * releaseMs * 0.001 * sr)) < 0.24f);
    }
}

TEST_CASE ("KL 8. No kick (TRIGGER Sidechain, or Auto with the host stopped): the output is the bass, status 'no kick'", "[kl][sidechain]")
{
    const double sr = 48000.0, seconds = 3.0;
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    for (int pass = 0; pass < 2; ++pass)
    {
        auto p = makeKl (sr, 0.6f, false);
        const bool playing = pass == 0;
        if (playing)
        {
            setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerSidechain);
            p->prepareToPlay (sr, 512);
        }
        const int L = p->getLatencySamples();
        const auto out = run (*p, b, nullptr, { 512 }, 124.0, {}, playing);
        // (after the generic 3 ms fade-in of the engine's input at start-up)
        double err = 0.0;
        for (int i = L + (int) (0.05 * sr); i < out.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - b.getSample (0, i - L)));
        INFO ((playing ? "Sidechain, playing" : "Auto, stopped") << ": max difference " << err);
        CHECK (err < 1.0e-5);
        CHECK ((Status) p->getDsp().getTelemetry().status.load() == Status::noKick);
    }
    // a connected sidechain that stays silent (TRIGGER Sidechain): the same after 2 s
    Buffer silent (2, b.getNumSamples());
    silent.clear();
    auto q = makeKl (sr, 0.6f, true);
    setParam (q->getTriggerParameter(), (float) KickLockDsp::triggerSidechain);
    q->prepareToPlay (sr, 512);
    run (*q, b, &silent);
    CHECK ((Status) q->getDsp().getTelemetry().status.load() == Status::noKick);
}

TEST_CASE ("KL 9. Mono sum: with LOCK on, L + R loses less than 3 dB", "[kl][mono]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0;
    auto b = bass ({ { 0.0, 2.0, 55.0 }, { 2.0, 2.0, 82.4 } }, seconds, sr);
    // a little width in the mids (different harmonics on the right)
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (1, i, b.getSample (1, i) + 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / sr));
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 1.0f, true, 0.6f);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    double eL = 0.0, eR = 0.0, eM = 0.0;
    for (int i = (int) (0.5 * sr); i < out.getNumSamples(); ++i)
    {
        const double l = out.getSample (0, i), r = out.getSample (1, i), m = 0.5 * (l + r);
        eL += l * l;
        eR += r * r;
        eM += m * m;
    }
    const double loss = 10.0 * std::log10 (0.5 * (eL + eR) / eM);
    INFO ("mono sum loss " << loss << " dB; telemetry " << p->getDsp().getTelemetry().monoSumLossDb.load() << " dB");
    CHECK (loss < 3.0);
    CHECK (p->getDsp().getTelemetry().monoSumLossDb.load() < 3.0f);
}

TEST_CASE ("KL 10. Crossover: sub + mids rebuild the bass exactly (no shift, no ducking)", "[kl][crossover]")
{
    const double sr = 48000.0, seconds = 2.0;
    auto p = makeKl (sr, 1.0f, false, 0.0f);
    const int L = p->getLatencySamples();
    const auto b = bass ({ { 0.0, 1.0, 55.0 }, { 1.0, 1.0, 110.0 } }, seconds, sr);
    const auto k = kicks (seconds, sr, 124.0);
    const auto out = run (*p, b, &k);
    double err = 0.0, peak = 0.0;
    for (int i = L + (int) (0.05 * sr); i < out.getNumSamples(); ++i)
    {
        err = juce::jmax (err, (double) std::abs (out.getSample (0, i) - b.getSample (0, i - L)));
        peak = juce::jmax (peak, (double) std::abs (b.getSample (0, i - L)));
    }
    INFO ("max difference " << err << " (" << 20.0 * std::log10 (1.0 + err / peak) << " dB)");
    CHECK (20.0 * std::log10 (1.0 + err / peak) < 0.1);
}

TEST_CASE ("KL 11. Latency: reported = measured, constant whatever the settings", "[kl][latency]")
{
    for (double sr : sampleRates)
    {
        auto p = makeKl (sr, 1.0f, false, 0.0f);
        const int L = p->getLatencySamples();
        Buffer in (2, L + 4096);
        in.clear();
        in.setSample (0, 100, 0.5f);
        in.setSample (1, 100, 0.5f);
        const auto out = run (*p, in, nullptr);
        int first = -1;
        for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
            if (std::abs (out.getSample (0, i)) > 0.25f)
                first = i;
        INFO ("sr " << sr << ": latency " << L << " samples (" << 1000.0 * L / sr << " ms), impulse at " << first);
        CHECK (first == 100 + L);
        setParam (p->getLockParameter(), 1.0f);
        setParam (p->getSubCutoffParameter(), 150.0f);
        setParam (p->getOffsetParameter(), 15.0f);
        run (*p, in, nullptr);
        CHECK (p->getLatencySamples() == L);
    }
}

TEST_CASE ("KL 12. Automation: MIX and LOCK swept, no click", "[kl][automation]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0;
    const auto b = bass ({ { 0.0, seconds, 61.7 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 0.0f, false, 0.5f);
    const auto out = run (*p, b, &k, { 64 }, bpm, [&] (int pos)
                          {
                              const double t = pos / sr;
                              setParam (p->getMixParameter(), (float) (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1.3 * t)));
                              setParam (p->getLockParameter(), std::fmod (t, 1.0) < 0.5 ? 1.0f : 0.0f);
                          });
    // DECISIONS.md: "no click" = no sample step above twice the bass's own largest step (+ the kicks' ducking slope)
    float bassStep = 0.0f, step = 0.0f;
    for (int i = 1; i < b.getNumSamples(); ++i)
        bassStep = juce::jmax (bassStep, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
    for (int i = (int) (0.3 * sr); i < out.getNumSamples(); ++i)
        step = juce::jmax (step, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
    INFO ("largest step " << step << ", the bass's " << bassStep);
    CHECK (step < 2.0f * bassStep);
}

TEST_CASE ("KL 13. Block sizes: identical output for any partition", "[kl][blocks]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    const auto b = bass ({ { 0.0, 1.5, 55.0 }, { 1.5, 1.5, 73.4 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.05);
    auto render = [&] (const std::vector<int>& sizes)
    {
        auto p = makeKl (sr, 0.7f, true, 0.8f, 4096);
        setParam (p->getWaveStartParameter(), 45.0f);
        return run (*p, b, &k, sizes, bpm);
    };
    const auto ref = render ({ 512 });
    juce::Random rng (4);
    std::vector<int> randomSizes;
    for (int i = 0; i < 2000; ++i)
        randomSizes.push_back (1 + rng.nextInt (1500));
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 4096 }, randomSizes })
    {
        const auto out = render (sizes);
        double err = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                err = juce::jmax (err, (double) std::abs (out.getSample (c, i) - ref.getSample (c, i)));
        INFO ("block " << sizes.front() << (sizes.size() > 1 ? " (random)" : "") << ": max difference " << err);
        CHECK (err < 1.0e-6);
    }
}

TEST_CASE ("KL 14. Stability: no NaN / Inf, peak below 0 dBFS, every rate, mono or stereo sidechain", "[kl][stability]")
{
    for (double sr : sampleRates)
        for (int scCh : { 1, 2 })
        {
            const double seconds = 2.0;
            auto p = std::make_unique<KickLockProcessor>();
            auto layout = p->getBusesLayout();
            layout.inputBuses.getReference (1) = scCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            REQUIRE (p->setBusesLayout (layout));
            p->setNonRealtime (true);
            setParam (p->getLockParameter(), 1.0f);
            setParam (p->getMixParameter(), 1.0f);
            p->prepareToPlay (sr, 512);
            const auto b = bass ({ { 0.0, 1.0, 41.2 }, { 1.0, 1.0, 110.0 } }, seconds, sr);
            auto k = kicks (seconds, sr, 128.0);
            k.setSize (scCh, k.getNumSamples(), true);
            const auto out = run (*p, b, &k, { 512 }, 128.0);
            bool finite = true;
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite (out.getSample (c, i));
                    peak = juce::jmax (peak, std::abs (out.getSample (c, i)));
                }
            INFO ("sr " << sr << ", sidechain " << scCh << " ch: peak " << peak << ", kicks " << p->getDsp().getTelemetry().kickEvents.load());
            CHECK (finite);
            CHECK (peak < 1.0f);
            CHECK (p->getDsp().getTelemetry().kickEvents.load() > 2);
        }
}

TEST_CASE ("KL 15. State round trip, the defaults, the reset button", "[kl][state]")
{
    KickLockProcessor a;
    setParam (a.getMixParameter(), 0.3f);
    setParam (a.getLockParameter(), 1.0f);
    setParam (a.getTargetPhaseParameter(), 90.0f);
    setParam (a.getOffsetParameter(), -5.0f);
    setParam (a.getSubCutoffParameter(), 100.0f);
    setParam (a.getDepthParameter(), 0.4f);
    setParam (a.getWaveStartParameter(), 180.0f);
    setParam (a.getThresholdParameter(), -24.0f);
    setParam (a.getShapeParameter(), 3.0f);
    setParam (a.getReleaseParameter(), 4.0f);
    setParam (a.getLockModeParameter(), 1.0f);
    a.editorScale.store (1.5f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    KickLockProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    auto plain = [] (juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); };
    CHECK (plain (b.getMixParameter()) == Catch::Approx (0.3f).margin (1.0e-4));
    CHECK (b.getLockParameter().get());
    CHECK (plain (b.getTargetPhaseParameter()) == Catch::Approx (90.0f).margin (0.01));
    CHECK (plain (b.getOffsetParameter()) == Catch::Approx (-5.0f).margin (0.01));
    CHECK (plain (b.getSubCutoffParameter()) == Catch::Approx (100.0f).margin (0.01));
    CHECK (plain (b.getDepthParameter()) == Catch::Approx (0.4f).margin (1.0e-4));
    CHECK (plain (b.getWaveStartParameter()) == Catch::Approx (180.0f).margin (0.01));
    CHECK (plain (b.getThresholdParameter()) == Catch::Approx (-24.0f).margin (0.01));
    CHECK (b.getShapeParameter().getIndex() == 3);
    CHECK (b.getReleaseParameter().getIndex() == 4);
    CHECK (b.getLockModeParameter().getIndex() == 1);
    CHECK (b.editorScale.load() == Catch::Approx (1.5f));
    // defaults: MIX 60 % first, LOCK off, release 1/8, 120 Hz, -18 dB
    KickLockProcessor c;
    CHECK (dynamic_cast<juce::RangedAudioParameter*> (c.getParameters()[0])->getParameterID() == "mix");
    CHECK (plain (c.getMixParameter()) == Catch::Approx (0.6f));
    CHECK_FALSE (c.getLockParameter().get());
    CHECK (c.getReleaseParameter().getCurrentChoiceName() == "1/8");
    CHECK (c.getLockModeParameter().getCurrentChoiceName() == "Hold");
    CHECK (plain (c.getSubCutoffParameter()) == Catch::Approx (120.0f));
    CHECK (plain (c.getThresholdParameter()) == Catch::Approx (-18.0f));
    // the reset button
    b.resetAllToDefaults();
    CHECK (plain (b.getMixParameter()) == Catch::Approx (0.6f).margin (1.0e-4));
    CHECK_FALSE (b.getLockParameter().get());
    CHECK (b.getShapeParameter().getIndex() == 0);
    // the sidechain bus exists and is called so (hosts route a track to it)
    CHECK (c.getBusCount (true) == 2);
    CHECK (c.getBus (true, 1)->getName() == "Sidechain");
}

TEST_CASE ("KL 18. Level: whatever MIX and the shift, the fundamental keeps its level (< 0.5 dB)", "[kl][level]")
{
    // the dry sub crossfaded with the shifted one would comb (up to -14 dB
    // half a cycle apart): MIX scales the ducking, never the sub's phase
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0, hz = 55.0, T = 1.0 / hz;
    // magnitude at f0 over 2-period windows, averaged: insensitive to the
    // phase moving between kicks (the rotation glides back to 0)
    auto amplitude2 = [&] (const float* x, int from, int to)
    {
        double I = 0.0, Q = 0.0, wsum = 0.0;
        const int W = to - from;
        for (int k = 0; k < W; ++k)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * k / W);
            const double ph = 2.0 * juce::MathConstants<double>::pi * hz * k / sr;
            I += w * x[from + k] * std::cos (ph);
            Q += w * x[from + k] * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::hypot (I, Q) / wsum;
    };
    auto amplitude = [&] (const float* x, int from, int to)
    {
        const int W = (int) std::round (2.0 * sr / hz);
        double sum = 0.0;
        int n = 0;
        for (int at = from; at + W <= to; at += W / 2, ++n)
            sum += amplitude2 (x, at, at + W);
        return sum / juce::jmax (1, n);
    };
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    double worst = 0.0;
    for (float mix : { 0.1f, 0.3f, 0.6f, 1.0f })
        for (double frac : { 0.0, 0.25, 0.5, 0.75 })
        {
            auto p = makeKl (sr, mix, true, 0.0f); // depth 0: the level of the bass itself
            const auto k = kicks (seconds, sr, bpm, 0.25 + frac * T);
            const auto out = run (*p, b, &k, { 512 }, bpm);
            const int L = p->getLatencySamples();
            // between two kicks (the phase moves at each kick: a window across one would blur it)
            const auto kt = kickTimes (seconds, bpm, 0.25 + frac * T);
            for (size_t n = 3; n + 1 < kt.size(); ++n)
            {
                const int from = (int) ((kt[n] + 0.02) * sr), to = (int) ((kt[n + 1] - 0.02) * sr);
                const double a = amplitude (out.getReadPointer (0), from + L, to + L);
                const double r = amplitude (b.getReadPointer (0), from, to);
                const double db = juce::Decibels::gainToDecibels (a / r);
                worst = juce::jmax (worst, std::abs (db));
                INFO ("MIX " << mix << ", kick " << frac << " cycle off, kick " << n << ": " << db << " dB");
                CHECK (std::abs (db) < 0.5);
            }
        }
    WARN ("worst level change of the fundamental: " << worst << " dB");
}

TEST_CASE ("KL 19. MODE Transition: locked at the kick, the original bass again before the next one", "[kl][mode]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    const double off = 0.25 + 0.4 / hz; // the kick 0.4 cycle off: a large correction
    const auto k = kicks (seconds, sr, bpm, off);
    auto p = makeKl (sr, 1.0f, true, 0.0f);
    setParam (p->getLockModeParameter(), 1.0f);
    p->prepareToPlay (sr, 512);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    const auto kt = kickTimes (seconds, bpm, off);
    double worstLock = 0.0, worstBack = 0.0;
    for (size_t n = 2; n + 1 < kt.size(); ++n)
    {
        // at the kick: the target phase (0)
        const int at = (int) std::round (kt[n] * sr);
        worstLock = juce::jmax (worstLock, std::abs (kl::KickLockDsp::wrap180 (phaseAt (out.getReadPointer (0), out.getNumSamples(), at + L, hz, sr))));
        // just before the next kick: the original bass (sample by sample)
        const int from = (int) std::round ((kt[n + 1] - 0.025) * sr), to = (int) std::round ((kt[n + 1] - 0.012) * sr);
        for (int i = from; i < to; ++i)
            worstBack = juce::jmax (worstBack, (double) std::abs (out.getSample (0, i + L) - b.getSample (0, i)));
    }
    INFO ("phase at the kicks within " << worstLock << " deg; largest difference from the bass before the next kick " << worstBack);
    CHECK (worstLock < 5.0);
    CHECK (worstBack < 1.0e-3);
}

TEST_CASE ("KL 20. Custom curve (LFOTool style): the gain follows the points, saved with the session", "[kl][curve]")
{
    // the exponential preset is exactly the old curve: 1 - exp(-3x), normalised
    for (float x : { 0.0f, 0.1f, 0.37f, 0.8f, 1.0f })
        CHECK (KickLockDsp::evalCurve (KickLockDsp::presetCurve (KickLockDsp::exponential), x)
               == Catch::Approx ((1.0f - std::exp (-3.0f * x)) / (1.0f - std::exp (-3.0f))).margin (1.0e-5));

    // a step: 0 for the first half of the release, then 0.5, held after
    const double sr = 48000.0, bpm = 120.0, seconds = 2.0;
    auto p = makeKl (sr, 1.0f, false, 1.0f);
    setParam (p->getShapeParameter(), (float) KickLockDsp::custom);
    setParam (p->getReleaseParameter(), 3.0f); // 1/4 = 0.5 s at 120 BPM
    p->setCustomCurve ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.52f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } });
    Buffer dc (2, (int) (seconds * sr));
    for (int c = 0; c < 2; ++c)
        juce::FloatVectorOperations::fill (dc.getWritePointer (c), 0.5f, dc.getNumSamples());
    Buffer k (2, dc.getNumSamples());
    k.clear();
    for (int i = (int) (0.5 * sr); i < (int) (0.52 * sr); ++i)
        for (int c = 0; c < 2; ++c)
            k.setSample (c, i, 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * (i / sr)));
    const auto out = run (*p, dc, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    auto gainAt = [&] (double t) { return out.getSample (0, (int) (t * sr) + L) / 0.5f; };
    INFO ("gain at +0.1 s " << gainAt (0.6) << ", +0.4 s " << gainAt (0.9) << ", +1 s " << gainAt (1.5));
    CHECK (gainAt (0.6) == Catch::Approx (0.0f).margin (0.02));  // first half of the release
    CHECK (gainAt (0.9) == Catch::Approx (0.5f).margin (0.02));  // second half
    CHECK (gainAt (1.5) == Catch::Approx (0.5f).margin (0.02));  // the last value held after the release

    // saved and restored with the session; the reset button brings the exponential back
    juce::MemoryBlock state;
    p->getStateInformation (state);
    KickLockProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (b.getCustomCurve().size() == 4);
    CHECK (b.getCustomCurve()[2].x == Catch::Approx (0.52f).margin (1.0e-3));
    CHECK (b.getCustomCurve()[2].y == Catch::Approx (0.5f).margin (1.0e-3));
    CHECK (b.getShapeParameter().getIndex() == KickLockDsp::custom);
    b.resetAllToDefaults();
    CHECK (b.getCustomCurve().size() == 2);
    CHECK (b.getShapeParameter().getIndex() == KickLockDsp::exponential);
}

TEST_CASE ("KL 21. No kick, the host playing: ducking and lock on the beats (TRIGGER Auto / Beat); a kick takes over", "[kl][trigger]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    const auto beats = kickTimes (seconds, bpm, 0.0);
    // the ducking's lowest point on each beat (beat 0, at the very start, is not a transition)
    auto lowestNear = [&] (const Buffer& out, int L, double t)
    {
        int at = 0;
        float lo = 1.0e9f;
        for (int i = (int) ((t - 0.02) * sr); i < (int) ((t + 0.03) * sr); ++i)
        {
            // the envelope: the bass is a 55 Hz wave, its own peaks over a cycle
            float pk = 0.0f;
            for (int j = 0; j < (int) (sr / hz); j += 8)
                pk = juce::jmax (pk, std::abs (out.getSample (0, i + L + j - (int) (sr / hz / 2))));
            if (pk < lo)
            {
                lo = pk;
                at = i;
            }
        }
        return at / sr;
    };
    for (int trig : { (int) KickLockDsp::triggerAuto, (int) KickLockDsp::triggerBeat })
    {
        auto p = makeKl (sr, 1.0f, true, 1.0f);
        setParam (p->getTriggerParameter(), (float) trig);
        p->prepareToPlay (sr, 512);
        const auto out = run (*p, b, nullptr, { 512 }, bpm);
        const int L = p->getLatencySamples();
        for (size_t n = 2; n + 1 < beats.size(); ++n)
        {
            const double at = lowestNear (out, L, beats[n]);
            INFO ("trigger " << trig << ", beat " << n << " at " << beats[n] << " s: ducking lowest at " << at << " s");
            CHECK (std::abs (at - beats[n]) < 0.012);
        }
        // and the lock on them (depth 0: the phase of the bass itself)
        auto q = makeKl (sr, 1.0f, true, 0.0f);
        setParam (q->getTriggerParameter(), (float) trig);
        q->prepareToPlay (sr, 512);
        const auto o2 = run (*q, b, nullptr, { 512 }, bpm);
        const auto e = errorsAtKicks (o2, q->getLatencySamples(), hz, sr, beats, 1.0, seconds - 0.2);
        INFO ("phase at the beats: worst " << maxAbs (e) << " deg");
        CHECK (maxAbs (e) < 5.0);
        CHECK ((Status) q->getDsp().getTelemetry().status.load() != Status::noKick);
        CHECK (q->getDsp().getTelemetry().triggerSource.load() == 2);
    }
    // play from the top: the first beat (the downbeat, where play starts) triggers at once
    {
        auto p = makeKl (sr, 1.0f, true, 1.0f);
        setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerBeat);
        p->prepareToPlay (sr, 512);
        Buffer shortBass (2, (int) (0.2 * sr));
        shortBass.clear();
        run (*p, shortBass, nullptr, { 512 }, bpm);
        CHECK (p->getDsp().getTelemetry().kickEvents.load() == 1);
        CHECK (p->getDsp().getTelemetry().lastKickSample.load() == 0);
    }
    // Auto with a kick on the sidechain (off the beat by 0.1 s): the kicks lead, not the beats
    {
        auto p = makeKl (sr, 1.0f, false, 1.0f);
        const auto k = kicks (seconds, sr, bpm, 0.1);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        const int L = p->getLatencySamples();
        for (double t : kickTimes (seconds, bpm, 0.1))
            if (t > 1.0 && t < seconds - 0.3)
            {
                INFO ("kick at " << t << " s");
                CHECK (std::abs (lowestNear (out, L, t) - t) < 0.012);
            }
        CHECK (p->getDsp().getTelemetry().triggerSource.load() == 1);
    }
}

TEST_CASE ("KL 22. Link: Kick Lock Send on the kick's track drives Kick Lock, on time or a block late", "[kl][link]")
{
    kl::KickLink::removeBlock(); // a fresh block
    const double sr = 48000.0, bpm = 124.0, seconds = 6.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    const auto kickTrack = kicks (seconds, sr, bpm, 0.0);
    const auto times = kickTimes (seconds, bpm, 0.0);
    for (int order : { 0, 1 }) // 0: the kick's track first (on time); 1: the bass first, 1024-sample blocks (a block late)
    {
        kls::KickSendProcessor send;
        send.setNonRealtime (true);
        send.prepareToPlay (sr, 1024);
        auto p = makeKl (sr, 1.0f, true, 0.0f, 1024); // depth 0: the phase of the bass itself
        setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerLink);
        p->prepareToPlay (sr, 1024);
        REQUIRE (send.getLatencySamples() == p->getLatencySamples()); // the host keeps both tracks in step
        gk::OfflinePlayHead ph;
        ph.bpm = bpm;
        ph.sampleRate = sr;
        ph.playing = true;
        send.setPlayHead (&ph);
        p->setPlayHead (&ph);
        Buffer kickIo (kickTrack), bassIo (b);
        juce::MidiBuffer midi;
        const int block = order == 0 ? 512 : 1024;
        for (int pos = 0; pos < b.getNumSamples(); pos += block)
        {
            const int len = juce::jmin (block, b.getNumSamples() - pos);
            ph.samplePosition = pos;
            Buffer kv (kickIo.getArrayOfWritePointers(), 2, pos, len), bv (bassIo.getArrayOfWritePointers(), 2, pos, len);
            if (order == 0)
            {
                send.processBlock (kv, midi);
                p->processBlock (bv, midi);
            }
            else
            {
                p->processBlock (bv, midi);
                send.processBlock (kv, midi);
            }
        }
        send.setPlayHead (nullptr);
        p->setPlayHead (nullptr);
        const auto e = errorsAtKicks (bassIo, p->getLatencySamples(), hz, sr, times, order == 0 ? 0.7 : 2.5, seconds - 0.2);
        INFO ((order == 0 ? "on time" : "a block late (predicted)") << ": phase at the kicks, worst " << maxAbs (e) << " deg over " << e.size()
              << " kicks; predicting " << p->getDsp().linkPredicting());
        {
            const auto last = p->getDsp().getTelemetry().lastKickSample.load();
            double nearest = 1.0e9;
            for (double t : times)
                if (std::abs (t * sr - (double) last) < std::abs (nearest - (double) last))
                    nearest = t * sr;
            INFO ("last link kick at sample " << last << ", the kick is at " << nearest << "; Send detected " << send.getDsp().getTelemetry().kicks.load()
                  << " kicks for " << times.size());
            CHECK (std::abs ((double) last - nearest) < 0.002 * sr);
        }
        CHECK (! e.empty());
        CHECK (maxAbs (e) < 5.0);
        CHECK (p->getDsp().getTelemetry().triggerSource.load() == 3);
        CHECK (p->getDsp().linkPredicting() == (order == 1));
        CHECK (p->getDsp().linkName().isNotEmpty());
    }
    // Auto with Kick Lock Send connected but no kick (an intro, a break): no
    // ducking on the beats (that looked like "the link drops")
    {
        kls::KickSendProcessor send;
        send.setNonRealtime (true);
        send.prepareToPlay (sr, 512);
        auto p = makeKl (sr, 1.0f, true, 1.0f);
        gk::OfflinePlayHead ph;
        ph.bpm = bpm;
        ph.sampleRate = sr;
        ph.playing = true;
        send.setPlayHead (&ph);
        p->setPlayHead (&ph);
        Buffer silent (2, (int) (3.0 * sr)), bassIo (2, silent.getNumSamples());
        silent.clear();
        for (int c = 0; c < 2; ++c)
            bassIo.copyFrom (c, 0, b, c, 0, bassIo.getNumSamples());
        juce::MidiBuffer midi;
        for (int pos = 0; pos < silent.getNumSamples(); pos += 512)
        {
            ph.samplePosition = pos;
            Buffer kv (silent.getArrayOfWritePointers(), 2, pos, 512), bv (bassIo.getArrayOfWritePointers(), 2, pos, 512);
            send.processBlock (kv, midi);
            p->processBlock (bv, midi);
        }
        send.setPlayHead (nullptr);
        p->setPlayHead (nullptr);
        CHECK (p->getDsp().getTelemetry().linkSlot.load() >= 0);
        CHECK (p->getDsp().getTelemetry().kickEvents.load() == 0);
    }
    kl::KickLink::removeBlock();
}

TEST_CASE ("KL 24. A bass note starting on each kick: in place, not 'unstable'", "[kl][status]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    const auto beat = 60.0 / bpm;
    std::vector<kickbass::Note> notes;
    for (double t = 0.1; t < seconds; t += beat)
        notes.push_back ({ t, beat * 0.8, 55.0 }); // retriggered with every kick
    const auto b = bass (notes, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 1.0f, true);
    int blocks = 0, unstable = 0;
    run (*p, b, &k, { 512 }, bpm, [&] (int pos)
         {
             if (pos > (int) (0.8 * sr))
             {
                 ++blocks;
                 unstable += (Status) p->getDsp().getTelemetry().status.load() == Status::unstable ? 1 : 0;
             }
         });
    INFO ("unstable in " << unstable << " of " << blocks << " blocks");
    CHECK (unstable == 0);
}

TEST_CASE ("KL 17. CPU: stereo bass + stereo sidechain, LOCK on, 48 kHz", "[kl][cpu]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 8.0;
    auto p = makeKl (sr, 1.0f, true, 0.8f);
    const auto b = bass ({ { 0.0, 4.0, 55.0 }, { 4.0, 4.0, 73.4 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    run (*p, b, &k, { 512 }, bpm);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const double pct = 100.0 * ms / (seconds * 1000.0);
    WARN ("Kick Lock: " << pct << " % of one core");
    if (timingChecksEnforced())
        CHECK (pct < 10.0);
}

TEST_CASE ("diag: f0 estimate", "[.kldiag]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 2.0;
    for (double hz : { 40.0, 55.0, 61.7, 73.4, 98.0, 110.0 })
    {
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
        const auto k = kicks (seconds, sr, bpm, 0.25);
        run (*p, b, &k, { 512 }, bpm);
        const double f = p->getDsp().getTelemetry().f0Hz.load();
        WARN (hz << " Hz: f0 " << f << " (" << 1200.0 * std::log2 (f / hz) << " cents), confidence " << p->getDsp().getTelemetry().phaseConfidence.load());
    }
}

TEST_CASE ("diag: one kick", "[.kldiag2]")
{
    const double sr = 48000.0, seconds = 2.0;
    for (double hz : { 40.0, 55.0 })
    {
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr, true);
        Buffer k (2, b.getNumSamples());
        k.clear();
        const double tk = 1.0;
        const auto all = kicks (seconds, sr, 60.0, tk); // kicks at 1 s only (60 BPM: next at 2 s)
        k.makeCopyOf (all);
        const auto out = run (*p, b, &k, { 512 }, 120.0);
        const int L = p->getLatencySamples();
        const double truth = KickLockDsp::wrap180 (360.0 * hz * tk);
        const double meas = p->getDsp().lastMeasuredPhase();
        const double shift = p->getDsp().currentShiftMs();
        const double outPh = phaseAt (out.getReadPointer (0), out.getNumSamples(), (int) (tk * sr) + L, hz, sr);
        const double inPh = phaseAt (b.getReadPointer (0), b.getNumSamples(), (int) (tk * sr), hz, sr);
        WARN ("onset " << p->getDsp().getTelemetry().lastKickSample.load() << " (true " << (int) (tk * sr) << "), f0 " << p->getDsp().getTelemetry().f0Hz.load());
        WARN (hz << " Hz: truth " << truth << ", input measured by the test " << inPh << ", plugin measured " << meas << ", shift " << shift << " ms (expected "
                 << KickLockDsp::wrap180 (meas) / 360.0 / hz * 1000.0 << "), output phase at the kick " << outPh);
    }
}

TEST_CASE ("diag: ducking trace", "[.kldiag3]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    Buffer b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < 2; ++c)
            b.setSample (c, i, 0.25f);
    auto p = makeKl (sr, 1.0f, false, 1.0f);
    setParam (p->getReleaseParameter(), 0.0f);
    const auto k = kicks (seconds, sr, bpm, 0.5);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    const int kick = (int) std::round (1.467741935 * sr) + L;
    juce::String s;
    for (int ms = -5; ms < 480; ms += 20)
        s << ms << ":" << juce::String (out.getSample (0, kick + ms * 48) / 0.25f, 3) << " ";
    WARN (s << " kicks " << p->getDsp().getTelemetry().kickEvents.load());
}

TEST_CASE ("diag: output envelope around a kick", "[.kldiag4]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.2;
    auto p = makeKl (sr, 0.6f, true, 1.0f);
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.1);
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    const double kt = 0.1 + 4 * 60.0 / bpm;
    juce::String line;
    for (int ms = -20; ms <= 480; ms += 20)
    {
        const int at = (int) ((kt + ms / 1000.0) * sr) + L;
        float pk = 0.0f;
        for (int i = at - 480; i < at + 480; ++i)
            pk = juce::jmax (pk, std::abs (out.getSample (0, i)));
        line << ms << ":" << juce::String (pk, 2) << " ";
    }
    WARN (line);
}

TEST_CASE ("KL 23. One detection per kick, whatever the threshold (Kick Lock and Kick Lock Send)", "[kl][kick]")
{
    // (the re-arming used to fire again at the end of the dead time when the
    // threshold was low: the slow envelope had not risen yet at the kick's start)
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.1);
    const auto n = (int) kickTimes (seconds, bpm, 0.1).size();
    for (float db : { -40.0f, -30.0f, -24.0f, -18.0f, -9.0f })
    {
        auto p = makeKl (sr, 1.0f, false);
        setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerSidechain);
        setParam (p->getThresholdParameter(), db);
        p->prepareToPlay (sr, 512);
        run (*p, b, &k, { 512 }, bpm);
        INFO ("Kick Lock, threshold " << db << " dB");
        CHECK (p->getDsp().getTelemetry().kickEvents.load() == n);
    }
    for (float sens : { 0.0f, 0.5f, 0.9f })
    {
        kls::KickSendDsp d;
        d.prepare (sr, 512, 2);
        Buffer io (k);
        std::vector<float> amount (512, sens);
        gk::BlockTiming t;
        for (int pos = 0; pos < io.getNumSamples(); pos += 512)
        {
            const int len = juce::jmin (512, io.getNumSamples() - pos);
            Buffer v (io.getArrayOfWritePointers(), 2, pos, len);
            d.processWet (v, len, amount.data(), t);
        }
        INFO ("Kick Lock Send, sensitivity " << sens);
        CHECK (d.getTelemetry().kicks.load() == n);
    }
}
