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
    // most tests feed the kick through the sidechain bus: TRIGGER Sidechain
    // (Auto follows Kick Lock Send, else the tempo; tested on their own)
    setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerSidechain);
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
    int at = 0;
    for (int i = (int) (0.5 * sr); i + 1 < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i + 1) - out.getSample (0, i)) > step)
        {
            step = std::abs (out.getSample (0, i + 1) - out.getSample (0, i));
            at = i;
        }
    INFO ("largest step " << step << " at " << (at - p->getLatencySamples()) / sr << " s (input time)");
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
        int ok = 0, unstable = 0, seenK = 0;
        juce::String dbg;
        run (*p, b, &k, { 512 }, bpm, [&] (int pos)
             {
                 auto& d = p->getDsp();
                 if (d.getTelemetry().kickEvents.load() != seenK)
                 {
                     seenK = d.getTelemetry().kickEvents.load();
                     dbg << " [age " << juce::String (d.lastNoteAgeMs, 0) << " conf " << juce::String (d.getTelemetry().phaseConfidence.load(), 2) << " coh "
                         << juce::String (d.lastCoherence, 2) << " ap " << juce::String (d.lastAperiodicity, 2) << " st " << d.getTelemetry().status.load() << "]";
                 }
                 if (pos < (int) (1.0 * sr))
                     return;
                 const auto st = (Status) p->getDsp().getTelemetry().status.load();
                 ok += st == Status::ok ? 1 : 0;
                 unstable += st == Status::unstable ? 1 : 0;
             });
        INFO ((kind == 0 ? "noise" : "FM") << ": OK " << ok << " blocks, unstable " << unstable << dbg);
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
        // (the next kick's crossfade starts ~12 ms before it is heard: before that)
        const int from = (int) std::round ((kt[n + 1] - 0.030) * sr), to = (int) std::round ((kt[n + 1] - 0.015) * sr);
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
    // a kick on the sidechain (off the beat by 0.1 s): Sidechain follows it;
    // Auto does not (the host's sidechain only when chosen): the beats lead
    for (int trig : { (int) KickLockDsp::triggerSidechain, (int) KickLockDsp::triggerAuto })
    {
        auto p = makeKl (sr, 1.0f, false, 1.0f);
        setParam (p->getTriggerParameter(), (float) trig);
        p->prepareToPlay (sr, 512);
        const auto k = kicks (seconds, sr, bpm, 0.1);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        const int L = p->getLatencySamples();
        const double offset = trig == (int) KickLockDsp::triggerSidechain ? 0.1 : 0.0;
        for (double t : kickTimes (seconds, bpm, offset))
            if (t > 1.0 && t < seconds - 0.3)
            {
                INFO ("trigger " << trig << ", expected at " << t << " s");
                CHECK (std::abs (lowestNear (out, L, t) - t) < 0.012);
            }
        CHECK (p->getDsp().getTelemetry().triggerSource.load() == (trig == (int) KickLockDsp::triggerSidechain ? 1 : 2));
    }
}

TEST_CASE ("KL 22. Link: Kick Lock Send drives Kick Lock, on time or a buffer late; a removed kick triggers nothing", "[kl][link]")
{
    kl::KickLink::removeBlock(); // a fresh block
    const double sr = 48000.0, bpm = 124.0, seconds = 6.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    auto kickTrack = kicks (seconds, sr, bpm, 0.0);
    auto times = kickTimes (seconds, bpm, 0.0);
    // the end of bar 2: its last kick removed (a fill, a break)
    const double removed = times[7];
    kickTrack.clear ((int) std::round (removed * sr), (int) (0.3 * sr));
    times.erase (times.begin() + 7);
    for (int order : { 0, 1 }) // 0: the kick's track first, 512; 1: the bass first, 1024-sample buffers (its kicks a buffer late)
    {
        const int block = order == 0 ? 512 : 1024;
        kls::KickSendProcessor send;
        send.setNonRealtime (true);
        send.prepareToPlay (sr, block);
        auto p = makeKl (sr, 1.0f, true, 0.0f, block); // depth 0: the phase of the bass itself
        setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerLink);
        p->prepareToPlay (sr, block);
        REQUIRE (send.getLatencySamples() == p->getLatencySamples()); // the host treats both tracks alike
        INFO ("latency " << p->getLatencySamples() << " samples at " << block);
        gk::OfflinePlayHead ph;
        ph.bpm = bpm;
        ph.sampleRate = sr;
        ph.playing = true;
        send.setPlayHead (&ph);
        p->setPlayHead (&ph);
        Buffer kickIo (kickTrack), bassIo (b);
        juce::MidiBuffer midi;
        std::vector<std::int64_t> triggers;
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
            const auto last = p->getDsp().getTelemetry().lastKickSample.load();
            if (triggers.empty() || triggers.back() != last)
                triggers.push_back (last);
        }
        send.setPlayHead (nullptr);
        p->setPlayHead (nullptr);
        const auto e = errorsAtKicks (bassIo, p->getLatencySamples(), hz, sr, times, 0.7, seconds - 0.2);
        INFO ((order == 0 ? "on time" : "a buffer late") << ": phase at the kicks, worst " << maxAbs (e) << " deg over " << e.size() << " kicks");
        CHECK (! e.empty());
        CHECK (maxAbs (e) < 5.0);
        CHECK (p->getDsp().linkLateKicks() == 0);
        CHECK (p->getDsp().getTelemetry().triggerSource.load() == 3);
        CHECK (p->getDsp().linkName().isNotEmpty());
        // nothing where the kick was removed
        for (auto t : triggers)
        {
            INFO ("trigger at " << t / sr << " s, the removed kick was at " << removed << " s");
            CHECK (std::abs ((double) t / sr - removed) > 0.05);
        }
        // and every other kick placed within 2 ms
        for (double t : times)
            if (t > 0.3 && t < seconds - 0.3)
            {
                bool found = false;
                for (auto tr : triggers)
                    found = found || std::abs ((double) tr / sr - t) < 0.002;
                INFO ("kick at " << t << " s");
                CHECK (found);
            }
    }
    // Auto with Kick Lock Send connected but no kick (an intro, a break): no
    // ducking on the beats (that looked like "the link drops")
    {
        kls::KickSendProcessor send;
        send.setNonRealtime (true);
        send.prepareToPlay (sr, 512);
        auto p = makeKl (sr, 1.0f, true, 1.0f);
        setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerAuto);
        p->prepareToPlay (sr, 512);
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

TEST_CASE ("KL 25. The ducking never moves the phase: LOCK with any curve = LOCK without ducking", "[kl][curve][phase]")
{
    // the lock rotates the fundamental at the kick; the curve only multiplies
    // the bass by a gain: once the bass is back, the sub's phase must be the
    // same whatever the curve (presets, a hand-drawn one, either MODE)
    const double sr = 48000.0, bpm = 124.0, seconds = 4.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.25 + 0.4 / hz); // 0.4 cycle off: a big correction
    const auto kt = kickTimes (seconds, bpm, 0.25 + 0.4 / hz);
    for (int mode : { 0, 1 })
    {
        auto runWith = [&] (float depth, int shape, bool custom)
        {
            auto p = makeKl (sr, 1.0f, true, depth);
            setParam (p->getLockModeParameter(), (float) mode);
            setParam (p->getReleaseParameter(), 0.0f); // 1/16: the bass is back well before the next kick
            if (custom)
            {
                p->setCustomCurve ({ { 0.0f, 0.0f, 0.0f }, { 0.4f, 0.05f, -4.0f }, { 0.8f, 0.9f, 3.0f }, { 1.0f, 1.0f, 0.0f } });
                setParam (p->getShapeParameter(), (float) KickLockDsp::custom);
            }
            else
                setParam (p->getShapeParameter(), (float) shape);
            p->prepareToPlay (sr, 512);
            auto out = run (*p, b, &k, { 512 }, bpm);
            return std::make_pair (std::move (out), p->getLatencySamples());
        };
        const auto [ref, L] = runWith (0.0f, 0, false); // the lock alone
        for (int variant = 0; variant < 5; ++variant)
        {
            const auto [out, L2] = runWith (1.0f, juce::jmin (variant, 3), variant == 4);
            double worst = 0.0;
            for (size_t n = 2; n + 1 < kt.size(); ++n)
            {
                // just after the release (1/16 = 121 ms), before the next kick
                const int at = (int) std::round ((kt[n] + 0.14) * sr) + L;
                const double a = phaseAt (ref.getReadPointer (0), ref.getNumSamples(), at, hz, sr);
                const double c = phaseAt (out.getReadPointer (0), out.getNumSamples(), at, hz, sr);
                worst = juce::jmax (worst, std::abs (KickLockDsp::wrap180 (c - a)));
            }
            INFO ("MODE " << mode << ", curve " << (variant == 4 ? "custom" : juce::String (variant)) << ": phase difference with / without ducking " << worst << " deg");
            CHECK (worst < 1.0);
        }
    }
}

TEST_CASE ("KL 26. A bass note starting between two kicks starts untouched (no rotated attack: no click)", "[kl][notes]")
{
    // a raw saw bass (all the highs), notes starting 90 ms after each kick,
    // held over the next one; LOCK on, MODE Hold: the rotation of the last
    // kick must be back to 0 when a note starts (Alan: a click in the highs)
    const double sr = 48000.0, bpm = 145.0, seconds = 4.0, beat = 60.0 / bpm;
    Buffer b (2, (int) (seconds * sr));
    b.clear();
    std::vector<double> starts;
    for (double t0 = 0.09 + 0.1; t0 < seconds - beat; t0 += beat)
    {
        starts.push_back (t0);
        const double hz = 55.0 * (starts.size() % 2 == 0 ? 1.0 : 1.189);
        double ph = 0.0;
        for (int i = (int) (t0 * sr); i < (int) ((t0 + beat * 0.8) * sr); ++i)
        {
            ph += hz / sr;
            const float v = (float) (0.4 * (2.0 * (ph - std::floor (ph)) - 1.0));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto p = makeKl (sr, 1.0f, true, 0.0f); // depth 0: only the lock touches the bass
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    double worst = 0.0;
    for (size_t n = 2; n < starts.size(); ++n)
    {
        const int s0 = (int) std::round (starts[n] * sr);
        for (int i = s0 - (int) (0.002 * sr); i < s0 + (int) (0.002 * sr); ++i)
            worst = juce::jmax (worst, (double) std::abs (out.getSample (0, i + L) - b.getSample (0, i)));
    }
    INFO ("largest difference from the bass around the note starts: " << worst << "; starts seen " << p->getDsp().noteStartsSeen()
          << ", rotation now " << p->getDsp().currentRotationDeg());
    CHECK (worst < 1.0e-3);
    CHECK (p->getDsp().noteStartsSeen() >= (int) starts.size() - 1);
}

TEST_CASE ("KL 27. LINK channels: each Kick Lock follows its own Send; two Sends on one channel are a conflict", "[kl][link]")
{
    kl::KickLink::removeBlock();
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    // two kick tracks: on the beats, and off them by 1/8
    const auto kickA = kicks (seconds, sr, bpm, 0.0), kickB = kicks (seconds, sr, bpm, 30.0 / bpm);
    kls::KickSendProcessor sendA, sendB;
    for (auto* sp : { &sendA, &sendB })
    {
        sp->setNonRealtime (true);
        sp->prepareToPlay (sr, 512);
    }
    setParam (sendA.getLinkChannelParameter(), 1.0f); // LINK 1
    setParam (sendB.getLinkChannelParameter(), 2.0f); // LINK 2
    auto lockA = makeKl (sr, 1.0f, true), lockB = makeKl (sr, 1.0f, true);
    setParam (lockA->getLinkChannelParameter(), 1.0f);
    setParam (lockB->getLinkChannelParameter(), 2.0f);
    for (auto* lp : { lockA.get(), lockB.get() })
    {
        setParam (lp->getTriggerParameter(), (float) KickLockDsp::triggerLink);
        lp->prepareToPlay (sr, 512);
    }
    gk::OfflinePlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    ph.playing = true;
    for (juce::AudioProcessor* ap : { (juce::AudioProcessor*) &sendA, (juce::AudioProcessor*) &sendB, (juce::AudioProcessor*) lockA.get(), (juce::AudioProcessor*) lockB.get() })
        ap->setPlayHead (&ph);
    Buffer ka (kickA), kb (kickB), ba (b), bb (b);
    juce::MidiBuffer midi;
    std::vector<std::int64_t> trigA, trigB;
    for (int pos = 0; pos < b.getNumSamples(); pos += 512)
    {
        ph.samplePosition = pos;
        Buffer v1 (ka.getArrayOfWritePointers(), 2, pos, 512), v2 (kb.getArrayOfWritePointers(), 2, pos, 512), v3 (ba.getArrayOfWritePointers(), 2, pos, 512),
            v4 (bb.getArrayOfWritePointers(), 2, pos, 512);
        sendA.processBlock (v1, midi);
        sendB.processBlock (v2, midi);
        lockA->processBlock (v3, midi);
        lockB->processBlock (v4, midi);
        for (auto [lp, tr] : { std::pair { lockA.get(), &trigA }, std::pair { lockB.get(), &trigB } })
        {
            const auto last = lp->getDsp().getTelemetry().lastKickSample.load();
            if (last >= 0 && (tr->empty() || tr->back() != last))
                tr->push_back (last);
        }
    }
    auto onGrid = [&] (const std::vector<std::int64_t>& tr, double offset)
    {
        int ok = 0;
        for (auto t : tr)
            for (double k : kickTimes (seconds, bpm, offset))
                ok += std::abs ((double) t / sr - k) < 0.002 ? 1 : 0;
        return ok;
    };
    INFO ("Kick Lock 1: " << trigA.size() << " kicks, Kick Lock 2: " << trigB.size());
    CHECK (trigA.size() >= 5);
    CHECK (trigB.size() >= 5);
    CHECK (onGrid (trigA, 0.0) == (int) trigA.size());          // only Send 1's kicks
    CHECK (onGrid (trigB, 30.0 / bpm) == (int) trigB.size());  // only Send 2's
    CHECK (lockA->getDsp().linkSendersOnChannel() == 1);
    // Send 2 moved to LINK 1 as well: a conflict, seen on both sides
    setParam (sendB.getLinkChannelParameter(), 1.0f);
    for (int pos = b.getNumSamples(); pos < b.getNumSamples() + 48000; pos += 512)
    {
        ph.samplePosition = pos;
        Buffer silent (2, 512);
        silent.clear();
        Buffer s2 (2, 512), s3 (2, 512);
        s2.clear();
        s3.clear();
        sendA.processBlock (silent, midi);
        sendB.processBlock (s2, midi);
        lockA->processBlock (s3, midi);
        juce::Thread::sleep (1);
    }
    CHECK (lockA->getDsp().linkSendersOnChannel() == 2);
    CHECK (sendA.getDsp().getSender().getSharedWith() == 1);
    for (juce::AudioProcessor* ap : { (juce::AudioProcessor*) &sendA, (juce::AudioProcessor*) &sendB, (juce::AudioProcessor*) lockA.get(), (juce::AudioProcessor*) lockB.get() })
        ap->setPlayHead (nullptr);
    kl::KickLink::removeBlock();
}

TEST_CASE ("KL 28. A host's reset at play: the kick on the very first sample is ducked (no warm-up)", "[kl][start]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 1.0;
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    const auto k = kicks (seconds, sr, bpm, 0.0); // a kick at sample 0
    auto p = makeKl (sr, 1.0f, false, 1.0f);
    p->reset(); // what some hosts do at play
    const auto out = run (*p, b, &k, { 512 }, bpm);
    const int L = p->getLatencySamples();
    // at the kick the bass is ducked to (almost) nothing, like on every other kick
    float atFirst = 0.0f;
    for (int i = L; i < L + (int) (0.003 * sr); ++i)
        atFirst = juce::jmax (atFirst, std::abs (out.getSample (0, i)));
    INFO ("bass level at the first kick: " << atFirst);
    CHECK (atFirst < 0.05f);
}

TEST_CASE ("KL 29. Kick Lock Send's shaper: untouched at rest, PUNCH and SUSTAIN shape the kick", "[kl][send]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 2.0;
    const auto k = kicks (seconds, sr, bpm, 0.1);
    auto render = [&] (float punch, float sustain)
    {
        kls::KickSendProcessor send;
        send.setNonRealtime (true);
        setParam (send.getPunchParameter(), 0.5f + 0.5f * punch); // the knob: 0 .. 1, its centre at rest
        setParam (send.getSustainParameter(), sustain);
        send.prepareToPlay (sr, 512);
        auto out = run (send, k, nullptr, { 512 }, bpm);
        return std::make_pair (std::move (out), send.getLatencySamples());
    };
    // at rest: the kick, delayed, bit for bit
    {
        const auto [out, L] = render (0.0f, 0.0f);
        bool exact = true;
        int first = -1;
        for (int i = L + (int) (0.01 * sr); i < out.getNumSamples(); ++i)
            if (! juce::exactlyEqual (out.getSample (0, i), k.getSample (0, i - L)))
            {
                if (first < 0)
                    first = i;
                exact = false;
            }
        INFO ("first difference at " << first << ": " << (first >= 0 ? out.getSample (0, first) : 0.0f) << " vs " << (first >= 0 ? k.getSample (0, first - L) : 0.0f));
        CHECK (exact);
    }
    // the attack (first 8 ms of each kick) and the tail (120..250 ms) against the original
    auto energy = [&] (const Buffer& x, int L, double from, double to)
    {
        double e = 0.0;
        for (double t : kickTimes (seconds, bpm, 0.1))
            if (t + to < seconds)
                for (int i = (int) ((t + from) * sr); i < (int) ((t + to) * sr); ++i)
                    e += (double) x.getSample (0, i + L) * x.getSample (0, i + L);
        return e;
    };
    const auto attackRef = energy (k, 0, 0.0, 0.008), tailRef = energy (k, 0, 0.12, 0.25);
    auto db = [] (double a, double b) { return 10.0 * std::log10 (a / b); };
    {
        const auto [up, L] = render (1.0f, 0.0f);
        const auto [down, L2] = render (-1.0f, 0.0f);
        INFO ("PUNCH +1: attack " << db (energy (up, L, 0.0, 0.008), attackRef) << " dB; -1: " << db (energy (down, L2, 0.0, 0.008), attackRef) << " dB");
        CHECK (db (energy (up, L, 0.0, 0.008), attackRef) > 3.0);
        CHECK (db (energy (down, L2, 0.0, 0.008), attackRef) < -3.0);
    }
    {
        const auto [longer, L] = render (0.0f, 1.0f);
        const auto [shorter, L2] = render (0.0f, -1.0f);
        INFO ("SUSTAIN +1: tail " << db (energy (longer, L, 0.12, 0.25), tailRef) << " dB; -1: " << db (energy (shorter, L2, 0.12, 0.25), tailRef) << " dB");
        CHECK (db (energy (longer, L, 0.12, 0.25), tailRef) > 3.0);
        CHECK (db (energy (shorter, L2, 0.12, 0.25), tailRef) < -3.0);
    }
}

TEST_CASE ("KL 30. Link in Auto over 10 s: kicks to the end, Send's kick in Kick Lock's scope", "[kl][link]")
{
    kl::KickLink::removeBlock();
    const double sr = 48000.0, bpm = 124.0, seconds = 10.0;
    const auto b = bass ({ { 0.0, seconds, 55.0 } }, seconds, sr);
    const auto kickTrack = kicks (seconds, sr, bpm, 0.0);
    kls::KickSendProcessor send;
    send.setNonRealtime (true);
    send.prepareToPlay (sr, 512);
    auto p = makeKl (sr, 1.0f, true, 1.0f);
    setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerAuto);
    p->prepareToPlay (sr, 512);
    gk::OfflinePlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    ph.playing = true;
    send.setPlayHead (&ph);
    p->setPlayHead (&ph);
    Buffer kickIo (kickTrack), bassIo (b);
    juce::MidiBuffer midi;
    float scopeKickPeak = 0.0f;
    for (int pos = 0; pos + 512 <= b.getNumSamples(); pos += 512)
    {
        ph.samplePosition = pos;
        Buffer kv (kickIo.getArrayOfWritePointers(), 2, pos, 512), bv (bassIo.getArrayOfWritePointers(), 2, pos, 512);
        send.processBlock (kv, midi);
        p->processBlock (bv, midi);
    }
    auto& tel = p->getDsp().getTelemetry();
    for (int i = 0; i < tel.scopeSize; ++i)
        scopeKickPeak = juce::jmax (scopeKickPeak, std::abs (tel.scopeKick[(size_t) i].load()));
    send.setPlayHead (nullptr);
    p->setPlayHead (nullptr);
    const int expected = (int) kickTimes (seconds - 0.05, bpm, 0.0).size();
    INFO ("kicks followed " << tel.kickEvents.load() << " of " << expected << ", received " << tel.linkKicks.load() << ", status " << tel.status.load()
          << ", last at " << tel.lastKickSample.load() / sr << " s; the kick in the scope peaks at " << scopeKickPeak);
    CHECK (tel.kickEvents.load() >= expected - 1);
    CHECK (tel.lastKickSample.load() / sr > seconds - 0.6);
    CHECK (tel.triggerSource.load() == 3);
    CHECK ((Status) tel.status.load() != Status::noKick);
    CHECK (scopeKickPeak > 0.3f); // Send's kick (0.8 peak), not a flat line
    kl::KickLink::removeBlock();
}

TEST_CASE ("KL 31. A low bass (44 Hz, many harmonics) held over the kicks locks; one cut before each kick is never 'unstable'", "[kl][status]")
{
    const double sr = 48000.0, bpm = 145.0, seconds = 4.0, beat = 60.0 / bpm, hz = 44.0;
    auto saw = [&] (std::vector<std::pair<double, double>> notes)
    {
        Buffer b (2, (int) (seconds * sr));
        b.clear();
        for (auto [t0, len] : notes)
        {
            double ph = 0.0;
            for (int i = (int) (t0 * sr); i < juce::jmin (b.getNumSamples(), (int) ((t0 + len) * sr)); ++i)
            {
                const double t = i / sr - t0;
                ph += hz * (1.0 + 0.003 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / sr; // a little drift
                const double env = juce::jmin (1.0, t / 0.004) * juce::jmin (1.0, (t0 + len - i / sr) / 0.01);
                const float v = (float) (0.35 * env * (2.0 * (ph - std::floor (ph)) - 1.0));
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        }
        return b;
    };
    const auto k = kicks (seconds, sr, bpm, 0.1);
    const auto kt = kickTimes (seconds, bpm, 0.1);
    // held: one long note over every kick
    {
        const auto b = saw ({ { 0.0, seconds } });
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        int blocks = 0, unstable = 0;
        const auto out = run (*p, b, &k, { 512 }, bpm, [&] (int pos)
        {
            if (pos > (int) (1.0 * sr))
            {
                ++blocks;
                unstable += (Status) p->getDsp().getTelemetry().status.load() == Status::unstable ? 1 : 0;
            }
        });
        const auto e = errorsAtKicks (out, p->getLatencySamples(), hz, sr, kt, 1.0, seconds - 0.3);
        INFO ("held: unstable in " << unstable << " of " << blocks << " blocks; phase worst " << maxAbs (e) << " deg");
        CHECK (unstable == 0);
        CHECK (maxAbs (e) < 5.0);
    }
    // short notes: each ends 50 ms before a kick and starts 80 ms after it
    {
        std::vector<std::pair<double, double>> notes;
        for (double t : kt)
            notes.push_back ({ t + 0.08, beat - 0.13 });
        const auto b = saw (notes);
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        int blocks = 0, unstable = 0;
        run (*p, b, &k, { 512 }, bpm, [&] (int pos)
        {
            if (pos > (int) (1.0 * sr))
            {
                ++blocks;
                unstable += (Status) p->getDsp().getTelemetry().status.load() == Status::unstable ? 1 : 0;
            }
        });
        INFO ("short notes: unstable in " << unstable << " of " << blocks << " blocks");
        CHECK (unstable == 0);
    }
}

TEST_CASE ("KL 32. The same note every time: locked every time, wherever it starts against the kick", "[kl][phase]")
{
    // a synth restarting its wave on every note, the same note before each
    // kick; the note starts 120 ms .. 10 ms before the kick, on it, or 10 ms
    // after (Alan: "parfois parfait, parfois non" — only notes started 150 ms
    // before were locked: the phase was read on the 8 cycles before the kick)
    const double sr = 48000.0, bpm = 124.0, beat = 60.0 / bpm, hz = 55.0, seconds = 8.0;
    for (double lead : { 0.12, 0.06, 0.03, 0.01, 0.0, -0.01 })
    {
        Buffer b (2, (int) (seconds * sr));
        b.clear();
        std::vector<double> kt;
        for (double t = 0.5; t < seconds - 1.0; t += 2.0 * beat)
        {
            kt.push_back (t);
            const double t0 = t - lead;
            for (int i = (int) (t0 * sr); i < (int) ((t0 + 1.5 * beat) * sr); ++i)
            {
                const double tt = i / sr - t0, ph = hz * tt;
                const double env = juce::jmin (1.0, tt / 0.003) * juce::jmin (1.0, (t0 + 1.5 * beat - i / sr) / 0.01);
                const double y = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * ph) + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * ph);
                b.setSample (0, i, (float) (env * y));
                b.setSample (1, i, (float) (env * y));
            }
        }
        Buffer k (2, b.getNumSamples());
        k.clear();
        for (double t : kt)
            for (int i = (int) (t * sr); i < juce::jmin (k.getNumSamples(), (int) ((t + 0.25) * sr)); ++i)
            {
                const double tt = i / sr - t;
                const float v = (float) (0.8 * std::exp (-tt / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tt + 3.0 * (1.0 - std::exp (-tt / 0.03)))));
                k.setSample (0, i, v);
                k.setSample (1, i, v);
            }
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        auto e = errorsAtKicks (out, p->getLatencySamples(), hz, sr, kt, 0.0, seconds);
        e.erase (e.begin()); // the very first note: too little of it to measure
        const auto [lo, hi] = std::minmax_element (e.begin(), e.end());
        INFO ("note " << lead * 1000.0 << " ms before the kick: phase from " << *lo << " to " << *hi << " deg");
        // locked; a note starting on the kick or after it is left as it is
        // (it starts in place: no click), the same each time
        if (lead >= 0.0)
            CHECK (maxAbs (e) < 12.0);
        CHECK (*hi - *lo < 8.0); // and the same each time
    }
}

TEST_CASE ("KL 33. A sub melody: each note locked, wherever it starts against the kick", "[kl][phase]")
{
    // a different note before each kick (36.7-73.4 Hz), the synth restarting
    // its wave on every note (Alan: "ça change le résultat d'une mélodie ?");
    // a young note's period is measured on the note itself and its fundamental
    // re-extracted at it — the detector still held the note before
    const double sr = 48000.0, bpm = 124.0, beat = 60.0 / bpm, seconds = 12.0;
    const double notes[] { 41.2, 55.0, 49.0, 61.7, 36.7, 73.4, 46.2, 65.4 };
    for (double lead : { 0.12, 0.06, 0.03, 0.01, 0.0, -0.01 })
    {
        Buffer b (2, (int) (seconds * sr));
        b.clear();
        std::vector<double> kt, hzs;
        int n = 0;
        for (double t = 0.5; t < seconds - 1.0; t += 2.0 * beat, ++n)
        {
            const double hz = notes[n % 8];
            kt.push_back (t);
            hzs.push_back (hz);
            const double t0 = t - lead;
            for (int i = (int) (t0 * sr); i < (int) ((t0 + 1.5 * beat) * sr); ++i)
            {
                const double tt = i / sr - t0, ph = hz * tt;
                const double env = juce::jmin (1.0, tt / 0.003) * juce::jmin (1.0, (t0 + 1.5 * beat - i / sr) / 0.01);
                const double y = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * ph) + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * ph);
                b.setSample (0, i, (float) (env * y));
                b.setSample (1, i, (float) (env * y));
            }
        }
        Buffer k (2, b.getNumSamples());
        k.clear();
        for (double t : kt)
            for (int i = (int) (t * sr); i < juce::jmin (k.getNumSamples(), (int) ((t + 0.25) * sr)); ++i)
            {
                const double tt = i / sr - t;
                const float v = (float) (0.8 * std::exp (-tt / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tt + 3.0 * (1.0 - std::exp (-tt / 0.03)))));
                k.setSample (0, i, v);
                k.setSample (1, i, v);
            }
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        const auto out = run (*p, b, &k, { 512 }, bpm);
        const int latency = p->getLatencySamples();
        double worst = 0.0, worstIntact = 0.0;
        for (size_t j = 1; j < kt.size(); ++j)
        {
            const bool tooYoung = lead > 0.0 && lead < 0.02 && juce::exactlyEqual (hzs[j], 36.7); // under 1.2 periods before the end of the measured cycle
            if (lead >= 0.0 && ! tooYoung)
            {
                const auto e = errorsAtKicks (out, latency, hzs[j], sr, { kt[j] }, 0.0, seconds);
                if (! e.empty())
                    worst = juce::jmax (worst, std::abs (e[0]));
            }
            // a note started on / after the kick (or too young to measure):
            // left as it is, from its start
            if (lead <= 0.01)
            {
                const int from = (int) ((kt[j] - lead) * sr);
                for (int i = from; i < from + (int) (0.03 * sr) && i + latency < out.getNumSamples(); ++i)
                    if (lead <= 0.0 || tooYoung)
                        worstIntact = juce::jmax (worstIntact, (double) std::abs (out.getSample (0, i + latency) - b.getSample (0, i)));
            }
        }
        INFO ("note " << lead * 1000.0 << " ms before the kick: worst phase " << worst << " deg, worst change of a note left in place " << worstIntact);
        CHECK (worst < 5.0);
        CHECK (worstIntact < 0.01);
    }
}

namespace
{
/** A host looping `loopBeats` (the position jumps back to 0); `shift`: this
    track wraps that many samples early (late if < 0) — the same timeline, a
    host compensating its tracks' delays may not jump them at the same block. */
struct LoopPlayHead final : public juce::AudioPlayHead
{
    double bpm = 124.0, sampleRate = 48000.0, loopBeats = 8.0;
    juce::int64 samplePosition = 0, shift = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        const double beats = (double) samplePosition / sampleRate * bpm / 60.0, early = (double) shift / sampleRate * bpm / 60.0;
        info.setPpqPosition (beats - loopBeats * std::floor ((beats + early) / loopBeats + 1.0e-9));
        info.setTimeInSamples (samplePosition);
        info.setIsLooping (true);
        info.setLoopPoints (juce::AudioPlayHead::LoopPoints { 0.0, loopBeats });
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};
} // namespace

TEST_CASE ("KL 34. Link in a host loop: every kick followed after each loop restart", "[kl][link]")
{
    // Alan: in an Ableton loop, after the restart "the plug-in forgets the
    // link". A 2-bar loop with a kick on each beat; both orders of the
    // tracks; the kick's track wrapping a block before / after the bass's
    const double sr = 48000.0, bpm = 124.0, seconds = 12.0, hz = 55.0;
    const auto b = bass ({ { 0.0, seconds, hz } }, seconds, sr);
    const auto kickTrack = kicks (seconds, sr, bpm, 0.0);
    const auto times = kickTimes (seconds, bpm, 0.0);
    for (int order : { 0, 1 })
        for (int shiftBlocks : { 0, 1, -1 })
        {
            kl::KickLink::removeBlock();
            const int block = 512;
            kls::KickSendProcessor send;
            send.setNonRealtime (true);
            send.prepareToPlay (sr, block);
            auto p = makeKl (sr, 1.0f, true, 0.0f, block);
            setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerAuto);
            p->prepareToPlay (sr, block);
            LoopPlayHead kickHead, bassHead;
            kickHead.bpm = bassHead.bpm = bpm;
            kickHead.sampleRate = bassHead.sampleRate = sr;
            kickHead.shift = shiftBlocks * block;
            send.setPlayHead (&kickHead);
            p->setPlayHead (&bassHead);
            Buffer kickIo (kickTrack), bassIo (b);
            juce::MidiBuffer midi;
            std::vector<std::int64_t> triggers;
            int seenEvents = 0;
            for (int pos = 0; pos < b.getNumSamples(); pos += block)
            {
                const int len = juce::jmin (block, b.getNumSamples() - pos);
                kickHead.samplePosition = bassHead.samplePosition = pos;
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
                auto& tel = p->getDsp().getTelemetry();
                const int events = tel.kickEvents.load();
                for (int n = juce::jmax (seenEvents + 1, events - 3); n <= events; ++n)
                    triggers.push_back (tel.recentKicks[(size_t) (n % 4)].load());
                seenEvents = events;
            }
            send.setPlayHead (nullptr);
            p->setPlayHead (nullptr);
            INFO ("order " << order << ", the kick's track wraps " << shiftBlocks << " block(s) early");
            int missing = 0, extra = 0;
            for (double t : times)
                if (t > 0.3 && t < seconds - 0.3)
                {
                    bool found = false;
                    for (auto tr : triggers)
                        found = found || std::abs ((double) tr / sr - t) < 0.002;
                    missing += found ? 0 : 1;
                }
            for (auto tr : triggers)
            {
                bool onKick = false;
                for (double t : times)
                    onKick = onKick || std::abs ((double) tr / sr - t) < 0.002;
                extra += onKick ? 0 : 1;
            }
            INFO (missing << " kicks missed, " << extra << " triggers off the kicks, of " << times.size());
            CHECK (missing == 0);
            CHECK (extra == 0);
            // and locked on every kick, the ones after a restart too
            const auto e = errorsAtKicks (bassIo, p->getLatencySamples(), hz, sr, times, 0.7, seconds - 0.2);
            INFO ("phase at the kicks, worst " << maxAbs (e) << " deg");
            CHECK (maxAbs (e) < 5.0);
        }
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

TEST_CASE ("diag: high frequencies added by the lock on a raw saw bass", "[.kldiag7]")
{
    const double sr = 48000.0, bpm = 124.0, seconds = 3.0;
    Buffer b (2, (int) (seconds * sr));
    b.clear();
    const double beat = 60.0 / bpm;
    gk::testsignals::Rng rng (7);
    for (double t0 = 0.1; t0 < seconds - 0.05; t0 += 2 * beat)
    {
        const double hz = 55.0 * (std::fmod (t0, 4 * beat) < 2 * beat ? 1.0 : 1.122);
        double ph = 0.0;
        for (int i = (int) (t0 * sr); i < juce::jmin (b.getNumSamples(), (int) ((t0 + beat * 1.9) * sr)); ++i)
        {
            const double t = i / sr - t0;
            ph += hz * (1.0 + 0.3 * std::exp (-t / 0.02)) / sr; // a pitch drop at the attack
            const float v = (float) (0.4 * std::exp (-t / 0.4) * (2.0 * (ph - std::floor (ph)) - 1.0) + 0.01 * rng.bipolar());
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    }
    const auto k = kicks (seconds, sr, bpm, 0.1 + 0.3 * beat); // in the middle of the notes
    for (bool lock : { false, true })
    {
        auto p = makeKl (sr, 1.0f, lock, 0.0f);
        int seen = 0;
        juce::String perKick;
        const auto out = run (*p, b, &k, { 512 }, bpm, [&] (int)
        {
            auto& tl = p->getDsp().getTelemetry();
            if (tl.kickEvents.load() != seen)
            {
                seen = tl.kickEvents.load();
                perKick << "[conf " << juce::String (tl.phaseConfidence.load(), 2) << " err " << juce::String (tl.phaseErrorDeg.load(), 0) << " meas "
                        << juce::String (p->getDsp().lastMeasuredPhase(), 0) << "] ";
            }
        });
        WARN (perKick);
        const int L = p->getLatencySamples();
        // the difference out - in, high-passed (3rd difference), against the input's
        double ed = 0.0, ei = 0.0;
        for (int i = L + 4; i < out.getNumSamples(); ++i)
        {
            auto hp = [&] (const float* x, int n) { return x[n] - 3.0f * x[n - 1] + 3.0f * x[n - 2] - x[n - 3]; };
            std::vector<float> d (4);
            float dd[4];
            for (int j = 0; j < 4; ++j)
                dd[j] = out.getSample (0, i - j) - b.getSample (0, i - j - L);
            const float hd = dd[0] - 3.0f * dd[1] + 3.0f * dd[2] - dd[3];
            const float hi = hp (b.getReadPointer (0), i - L);
            ed += (double) hd * hd;
            ei += (double) hi * hi;
        }
        auto& tel = p->getDsp().getTelemetry();
        WARN ("LOCK " << (lock ? "on" : "off") << ": highs added " << 10.0 * std::log10 (ed / ei + 1.0e-30) << " dB under the bass's own highs; status "
              << tel.status.load() << " confidence " << tel.phaseConfidence.load() << " f0 " << tel.f0Hz.load() << " rotation " << p->getDsp().currentRotationDeg()
              << " kicks " << tel.kickEvents.load());
    }
}

TEST_CASE ("diag: a bass file (GK_KL_FILE, GK_KL_BPM)", "[.kldiag8]")
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (std::getenv ("GK_KL_FILE"))));
    REQUIRE (r != nullptr);
    Buffer b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    const double sr = r->sampleRate, bpm = std::atof (std::getenv ("GK_KL_BPM"));
    auto p = makeKl (sr, 1.0f, true, 0.0f);
    setParam (p->getTriggerParameter(), (float) KickLockDsp::triggerBeat);
    p->prepareToPlay (sr, 512);
    int seen = 0;
    juce::String log;
    std::vector<std::pair<std::int64_t, double>> kicksSeen;
    const auto out = run (*p, b, nullptr, { 512 }, bpm, [&] (int)
    {
        auto& d = p->getDsp();
        auto& t = d.getTelemetry();
        if (t.kickEvents.load() != seen)
        {
            seen = t.kickEvents.load();
            kicksSeen.push_back ({ t.lastKickSample.load(), d.lastTm });
            log << "\n kick " << seen << ": status " << t.status.load() << " conf " << juce::String (t.phaseConfidence.load(), 2) << " coherence "
                << juce::String (d.lastCoherence, 2) << " aperiodic " << juce::String (d.lastAperiodicity, 2) << " spread " << juce::String (d.lastPitchSpread * 100.0, 1)
                << "% f0 " << juce::String (t.f0Hz.load(), 1) << " err " << juce::String (t.phaseErrorDeg.load(), 1)
                << " age " << juce::String (d.lastNoteAgeMs, 0) << " Tm " << juce::String (d.lastTm, 1) << " src " << d.lastSource << " ph " << juce::String (d.lastMeasuredPhase(), 0);
        }
    });
    juce::String errs;
    for (auto [when, T] : kicksSeen)
        if (T > 1.0 && when + p->getLatencySamples() + (int) (2.0 * T) < out.getNumSamples())
            errs << juce::String (KickLockDsp::wrap180 (phaseAt (out.getReadPointer (0), out.getNumSamples(), (int) when + p->getLatencySamples(), sr / T, sr)), 0) << " ";
    WARN (log << "\n output phase at the kicks: " << errs);
}

TEST_CASE ("diag: same note every bar, phase-reset synth, note start vs kick", "[.kldiag9]")
{
    const double sr = 48000.0, bpm = 124.0, beat = 60.0 / bpm, hz = 55.0;
    for (double lead : { 0.12, 0.06, 0.03, 0.01, 0.0, -0.01 }) // note-on this long before the kick
    {
        const double seconds = 8.0;
        Buffer b (2, (int) (seconds * sr));
        b.clear();
        std::vector<double> kt;
        for (double t = 0.5; t < seconds - 1.0; t += 2.0 * beat) // a kick every 2 beats, the note before it
        {
            kt.push_back (t);
            const double t0 = t - lead;
            for (int i = (int) (t0 * sr); i < (int) ((t0 + 1.5 * beat) * sr); ++i)
            {
                const double tt = i / sr - t0;
                const double ph = hz * tt; // the synth restarts its wave at 0 on every note
                const double env = juce::jmin (1.0, tt / 0.003) * juce::jmin (1.0, (t0 + 1.5 * beat - i / sr) / 0.01);
                const double y = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * ph) + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * ph);
                b.setSample (0, i, (float) (env * y));
                b.setSample (1, i, (float) (env * y));
            }
        }
        Buffer k (2, b.getNumSamples());
        k.clear();
        for (double t : kt)
            for (int i = (int) (t * sr); i < juce::jmin (k.getNumSamples(), (int) ((t + 0.25) * sr)); ++i)
            {
                const double tt = i / sr - t;
                const float v = (float) (0.8 * std::exp (-tt / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tt + 100.0 * 0.03 * (1.0 - std::exp (-tt / 0.03)))));
                k.setSample (0, i, v);
                k.setSample (1, i, v);
            }
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        int seen = 0;
        juce::String dbg;
        const auto out = run (*p, b, &k, { 512 }, bpm, [&] (int)
        {
            auto& d = p->getDsp();
            if (d.getTelemetry().kickEvents.load() != seen)
            {
                seen = d.getTelemetry().kickEvents.load();
                dbg << " [age " << juce::String (d.lastNoteAgeMs, 0) << " T " << juce::String (d.lastTm, 1) << " conf " << juce::String (d.getTelemetry().phaseConfidence.load(), 2)
                    << " coh " << juce::String (d.lastCoherence, 2) << " ap " << juce::String (d.lastAperiodicity, 2) << " sp " << juce::String (d.lastPitchSpread * 100.0, 1) << "]";
            }
        });
        if (std::abs (lead - 0.01) < 1.0e-6)
            WARN (dbg);
        const auto e = errorsAtKicks (out, p->getLatencySamples(), hz, sr, kt, 0.0, seconds);
        juce::String line;
        for (double v : e)
            line << juce::String (v, 0) << " ";
        WARN ("note " << lead * 1000.0 << " ms before the kick: phase at each kick " << line);
    }
}

TEST_CASE ("KL diag10. A sub melody: a different note before each kick", "[.kldiag10]")
{
    const double sr = 48000.0, bpm = 124.0, beat = 60.0 / bpm, seconds = 12.0;
    const double notes[] { 41.2, 55.0, 49.0, 61.7, 36.7, 73.4, 46.2, 65.4 };
    for (double lead : { 0.12, 0.06, 0.03, 0.01, 0.0, -0.01 })
    {
        Buffer b (2, (int) (seconds * sr));
        b.clear();
        std::vector<double> kt, hzs;
        int n = 0;
        for (double t = 0.5; t < seconds - 1.0; t += 2.0 * beat, ++n)
        {
            const double hz = notes[n % 8];
            kt.push_back (t);
            hzs.push_back (hz);
            const double t0 = t - lead;
            for (int i = (int) (t0 * sr); i < (int) ((t0 + 1.5 * beat) * sr); ++i)
            {
                const double tt = i / sr - t0, ph = hz * tt;
                const double env = juce::jmin (1.0, tt / 0.003) * juce::jmin (1.0, (t0 + 1.5 * beat - i / sr) / 0.01);
                const double y = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * ph) + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * ph);
                b.setSample (0, i, (float) (env * y));
                b.setSample (1, i, (float) (env * y));
            }
        }
        Buffer k (2, b.getNumSamples());
        k.clear();
        for (double t : kt)
            for (int i = (int) (t * sr); i < juce::jmin (k.getNumSamples(), (int) ((t + 0.25) * sr)); ++i)
            {
                const double tt = i / sr - t;
                const float v = (float) (0.8 * std::exp (-tt / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tt + 3.0 * (1.0 - std::exp (-tt / 0.03)))));
                k.setSample (0, i, v);
                k.setSample (1, i, v);
            }
        auto p = makeKl (sr, 1.0f, true, 0.0f);
        int seen = 0;
        juce::String dbg;
        const auto out = run (*p, b, &k, { 512 }, bpm, [&] (int)
        {
            auto& d = p->getDsp();
            if (d.getTelemetry().kickEvents.load() != seen)
            {
                seen = d.getTelemetry().kickEvents.load();
                const double hz = notes[(seen - 1) % 8];
                dbg << " [age " << juce::String (d.lastNoteAgeMs, 0) << " T " << juce::String (d.lastTm, 1) << "/" << juce::String (sr / hz, 1) << " s" << d.lastSource << " ph " << juce::String (d.lastMeasuredPhase(), 1) << "/" << juce::String (KickLockDsp::wrap180 (360.0 * hz * lead), 1) << " conf " << juce::String (d.getTelemetry().phaseConfidence.load(), 2) << "]";
            }
        });
        WARN (dbg);
        std::string line;
        double worst = 0.0;
        for (size_t j = 1; j < kt.size(); ++j)
        {
            const auto e = errorsAtKicks (out, p->getLatencySamples(), hzs[j], sr, { kt[j] }, 0.0, seconds);
            if (! e.empty())
            {
                line += juce::String (e[0], 1).toStdString() + " ";
                worst = juce::jmax (worst, std::abs (e[0]));
            }
        }
        WARN ("lead " << lead * 1000.0 << " ms: worst " << worst << " | " << line);
    }
}


TEST_CASE ("KL 35. A raw saw bass (every harmonic to 18 kHz): held, repeated, a melody — locked, measured on its low band", "[kl][phase]")
{
    const double sr = 48000.0, bpm = 124.0, beat = 60.0 / bpm, seconds = 12.0;
    auto sawAt = [&] (double ph, double hz)
    {
        double y = 0.0; // band-limited saw (every harmonic to 18 kHz, 1/n)
        for (int h = 1; h * hz < 18000.0; ++h)
            y += std::sin (juce::MathConstants<double>::twoPi * h * ph) / h;
        return 0.35 * y;
    };
    for (int mode : { 0, 1, 2 }) // 0 held note, 1 the same note repeated, 2 a melody
        for (double lead : { 0.12, 0.03, 0.0 })
        {
            if (mode == 0 && lead < 0.1)
                continue;
            const double notes[] { 41.2, 55.0, 49.0, 61.7, 36.7, 73.4, 46.2, 65.4 };
            Buffer b (2, (int) (seconds * sr));
            b.clear();
            std::vector<double> kt, hzs;
            int n = 0;
            for (double t = 0.5; t < seconds - 1.0; t += 2.0 * beat, ++n)
            {
                kt.push_back (t);
                hzs.push_back (mode == 2 ? notes[n % 8] : 55.0);
            }
            if (mode == 0)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = (float) (juce::jmin (1.0, i / sr / 0.005) * sawAt (55.0 * i / sr, 55.0));
                    b.setSample (0, i, v);
                    b.setSample (1, i, v);
                }
            }
            else
                for (size_t j = 0; j < kt.size(); ++j)
                {
                    const double t0 = kt[j] - lead, hz = hzs[j];
                    for (int i = (int) (t0 * sr); i < (int) ((t0 + 1.5 * beat) * sr); ++i)
                    {
                        const double tt = i / sr - t0;
                        const double env = juce::jmin (1.0, tt / 0.003) * juce::jmin (1.0, (t0 + 1.5 * beat - i / sr) / 0.01);
                        const float v = (float) (env * sawAt (hz * tt, hz));
                        b.setSample (0, i, v);
                        b.setSample (1, i, v);
                    }
                }
            Buffer k (2, b.getNumSamples());
            k.clear();
            for (double t : kt)
                for (int i = (int) (t * sr); i < juce::jmin (k.getNumSamples(), (int) ((t + 0.25) * sr)); ++i)
                {
                    const double tt = i / sr - t;
                    const float v = (float) (0.8 * std::exp (-tt / 0.18) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tt + 3.0 * (1.0 - std::exp (-tt / 0.03)))));
                    k.setSample (0, i, v);
                    k.setSample (1, i, v);
                }
            auto p = makeKl (sr, 1.0f, true, 0.0f);
            int seen = 0, unstable = 0, blocks = 0;
            juce::String dbg;
            const auto out = run (*p, b, &k, { 512 }, bpm, [&] (int)
            {
                auto& d = p->getDsp();
                ++blocks;
                unstable += d.getTelemetry().status.load() == (int) Status::unstable ? 1 : 0;
                if (d.getTelemetry().kickEvents.load() != seen)
                {
                    seen = d.getTelemetry().kickEvents.load();
                    dbg << " [T " << juce::String (d.lastTm, 1) << " ph " << juce::String (d.lastMeasuredPhase(), 0) << " c " << juce::String (d.getTelemetry().phaseConfidence.load(), 2) << "]";
                }
            });
            std::string line;
            double worst = 0.0;
            for (size_t j = 1; j < kt.size(); ++j)
            {
                const auto e = errorsAtKicks (out, p->getLatencySamples(), hzs[j], sr, { kt[j] }, 0.0, seconds);
                if (! e.empty())
                {
                    line += juce::String (e[0], 0).toStdString() + " ";
                    worst = juce::jmax (worst, std::abs (e[0]));
                }
            }
            INFO ("mode " << mode << " lead " << lead * 1000.0 << " ms: worst " << worst << " | unstable " << unstable << "/" << blocks << " | " << line << "\n" << dbg.substring (0, 600));
            CHECK (worst < 6.0);
            CHECK (unstable < 10); // (the very start only)
        }
}
