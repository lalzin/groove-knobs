// Crackle tests (spec 9): GRAIN 0 = the input (-120 dB), 60 s of noise and
// music without NaN, the output under -0.1 dBFS, WOW 1 swings a 440 Hz tone by
// less than 3 %, every knob followed / released (AUTO), presets distinct,
// state, latency, no crackle when knobs move, CPU.

#include "CrackleProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numeric>
#include <random>

using namespace gktest;
using crackle::CrackleDsp;
using crackle::CrackleProcessor;

namespace
{
constexpr double sr = 48000.0;
constexpr int numPresets = 12;

std::unique_ptr<CrackleProcessor> makeProc (int preset = 0)
{
    auto p = std::make_unique<CrackleProcessor>();
    setParam (p->getPresetParameter(), (float) preset);
    p->applyPreset (preset);
    setLayout (*p, 2);
    p->setNonRealtime (true);
    p->prepareToPlay (sr, 512);
    return p;
}

Buffer music (double seconds)
{
    auto b = gk::testsignals::grooveLoop (sr, 112.0, (int) std::ceil (seconds * 112.0 / 240.0), true);
    b.setSize (2, (int) (seconds * sr), true, true, false);
    return b;
}

// what a preset does to the signal: per third-octave band, the energy of the
// change (output minus the input, aligned on the latency) against the input's
// own (dB). Noise added, distortion, filtering, wobble all show here — the plain
// spectrum's shape hid most of them (a crackle and a wavefolder looked alike)
// the output's own spectral shape (third octaves, tilt removed)
std::vector<double> shape (const Buffer& b)
{
    std::vector<double> bands;
    for (double f = 40.0; f < 16000.0; f *= 1.26)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * 4.3), cw = std::cos (w), a0 = 1.0 + al;
        const double b0 = al / a0, b2 = -al / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double x = b.getSample (0, i), y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            e += y * y;
        }
        bands.push_back (10.0 * std::log10 (e + 1e-20));
    }
    const double mean = std::accumulate (bands.begin(), bands.end(), 0.0) / (double) bands.size();
    for (auto& v : bands)
        v -= mean;
    return bands;
}

std::vector<double> fingerprint (const Buffer& wet, const Buffer& dry, int latency)
{
    std::vector<double> bands;
    for (double f = 40.0; f < 16000.0; f *= 1.26)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * 4.3), cw = std::cos (w), a0 = 1.0 + al;
        const double b0 = al / a0, b2 = -al / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
        double dx1 = 0, dx2 = 0, dy1 = 0, dy2 = 0, rx1 = 0, rx2 = 0, ry1 = 0, ry2 = 0, eDry = 0, eRes = 0;
        for (int i = latency; i < wet.getNumSamples(); ++i)
        {
            const double d = dry.getSample (0, i - latency), r = wet.getSample (0, i) - d;
            const double yd = b0 * d + b2 * dx2 - a1 * dy1 - a2 * dy2;
            dx2 = dx1; dx1 = d; dy2 = dy1; dy1 = yd;
            const double yr = b0 * r + b2 * rx2 - a1 * ry1 - a2 * ry2;
            rx2 = rx1; rx1 = r; ry2 = ry1; ry1 = yr;
            if (i > (int) sr)
            {
                eDry += yd * yd;
                eRes += yr * yr;
            }
        }
        bands.push_back (10.0 * std::log10 ((eRes + 1e-12) / (eDry + 1e-12)));
    }
    return bands;
}
} // namespace

TEST_CASE ("GRAIN 0, MIX 1: the output is the input (within -120 dB)", "[crackle]")
{
    auto p = makeProc();
    setParam (p->getAmountParameter(), 0.0f);
    p->snapToTargets();
    const auto in = music (3.0);
    const auto out = processBlocks (*p, in, { 512, 97, 1024 });
    // (aligned on the reported latency)
    const int L = p->getLatencySamples();
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = L; i < in.getNumSamples(); ++i)
            worst = std::max (worst, (double) std::fabs (out.getSample (ch, i) - in.getSample (ch, i - L)));
    INFO ("latency " << L << ", worst difference " << juce::Decibels::gainToDecibels (worst + 1e-30) << " dB");
    CHECK (worst < 1e-6); // -120 dB
}

TEST_CASE ("60 s of white noise and music, every knob moving: no NaN, under -0.1 dBFS", "[crackle]")
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    auto p = makeProc();
    auto in = music (30.0);
    {
        // then 30 s of white noise at -3 dBFS
        auto noise = gk::testsignals::whiteNoise (2, (int) (30.0 * sr), 0.7f);
        const int n = in.getNumSamples();
        in.setSize (2, n + noise.getNumSamples(), true, true, false);
        for (int ch = 0; ch < 2; ++ch)
            in.copyFrom (ch, n, noise, ch, 0, noise.getNumSamples());
    }
    const auto out = processBlocks (*p, in, { 256, 512, 33 }, [&] (int pos)
    {
        if (pos % (int) (0.5 * sr) < 512)
        {
            setParam (p->getAmountParameter(), u (rng));
            for (int k = 0; k < p->getNumKnobs(); ++k)
                if (k != CrackleDsp::levelKnob)
                    p->getKnobParameter (k).setValueNotifyingHost (u (rng));
            setParam (p->getTypeParameter(), (float) (int) (u (rng) * 5.99f));
        }
    });
    REQUIRE (allFinite (out));
    CHECK (out.getMagnitude (0, out.getNumSamples()) <= 0.9886f); // -0.1 dBFS
}

TEST_CASE ("WOW 1: a 440 Hz tone swings by less than 3 %", "[crackle]")
{
    crackle::Chain c;
    c.prepare (sr);
    auto tone = gk::testsignals::sine (2, (int) (8.0 * sr), sr, 440.0, 0.5f);
    crackle::Amounts a;
    a.wow = 1.0f;
    a.flutter = 1.0f;
    c.process (tone.getWritePointer (0), tone.getWritePointer (1), tone.getNumSamples(), a);
    double last = -1.0, lo = 1e9, hi = 0.0;
    const float* y = tone.getReadPointer (0);
    for (int i = (int) sr; i < tone.getNumSamples(); ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double t = i - 1 + y[i - 1] / (y[i - 1] - y[i]);
            if (last > 0.0)
            {
                lo = std::min (lo, sr / (t - last));
                hi = std::max (hi, sr / (t - last));
            }
            last = t;
        }
    INFO (lo << " .. " << hi << " Hz");
    CHECK ((hi - lo) / 440.0 < 0.03);
    CHECK (hi - lo > 2.0); // (and it does move)
}

TEST_CASE ("AUTO: an effect follows GRAIN until its knob moves; a preset gives the others back", "[crackle]")
{
    auto p = makeProc (1); // Vinyl Sunday: WOW named (its own), CRUSH not (on AUTO)
    CHECK_FALSE (p->getAutoParameter (CrackleDsp::wowKnob).get());
    CHECK (p->getAutoParameter (CrackleDsp::crushKnob).get());
    // moving CRUSH (by hand or automation) takes it off AUTO
    p->getKnobParameter (CrackleDsp::crushKnob).setValueNotifyingHost (0.9f);
    CHECK_FALSE (p->getAutoParameter (CrackleDsp::crushKnob).get());
    // the amount: the knob's own, or GRAIN's curve
    CHECK (CrackleDsp::effectAmount (CrackleDsp::crushKnob, 0.5f, 0.8f, true) == Catch::Approx (0.0f));
    CHECK (CrackleDsp::effectAmount (CrackleDsp::crushKnob, 0.5f, 0.8f, false) == Catch::Approx (0.8f));
    CHECK (CrackleDsp::effectAmount (CrackleDsp::crushKnob, 1.0f, 0.0f, true) == Catch::Approx (1.0f));
    // another preset: its named knobs set, the rest back on AUTO
    setParam (p->getPresetParameter(), (float) p->getPresets().indexOf ("Broken Sampler"));
    CHECK (p->getAutoParameter (CrackleDsp::flutterKnob).get());
    CHECK_FALSE (p->getAutoParameter (CrackleDsp::crushKnob).get());
    CHECK (p->getTypeParameter().getIndex() == (int) crackle::SatType::hard);
}

TEST_CASE ("State: GRAIN, knobs and AUTOs come back as saved", "[crackle]")
{
    auto p = makeProc (3);
    setParam (p->getAmountParameter(), 0.37f);
    p->getKnobParameter (CrackleDsp::radioKnob).setValueNotifyingHost (0.6f); // (AUTO off)
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makeProc (0);
    q->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q->getPresetParameter().getIndex() == 3);
    CHECK (q->getAmountParameter().getValue() == Catch::Approx (0.37f).margin (0.002));
    CHECK (q->getKnobParameter (CrackleDsp::radioKnob).getValue() == Catch::Approx (0.6f).margin (0.002));
    CHECK_FALSE (q->getAutoParameter (CrackleDsp::radioKnob).get());
}

TEST_CASE ("Presets: twelve, each clearly different (3 dB in some band)", "[crackle]")
{
    auto probe = makeProc();
    REQUIRE (probe->getPresets().size() == numPresets);
    const auto in = music (6.0);
    // different = the change made to the signal OR the output's spectrum differs by 3 dB somewhere
    std::vector<std::vector<double>> prints, shapes;
    for (int i = 0; i < numPresets; ++i)
    {
        auto p = makeProc (i);
        const auto out = processBlocks (*p, in, { 512 });
        prints.push_back (fingerprint (out, in, p->getLatencySamples()));
        shapes.push_back (shape (out));
    }
    for (int i = 0; i < numPresets; ++i)
        for (int j = i + 1; j < numPresets; ++j)
        {
            double d = 0.0;
            for (size_t b = 0; b < prints[(size_t) i].size(); ++b)
                d = std::max ({ d, std::fabs (prints[(size_t) i][b] - prints[(size_t) j][b]), std::fabs (shapes[(size_t) i][b] - shapes[(size_t) j][b]) });
            INFO (probe->getPresets()[i].name << " vs " << probe->getPresets()[j].name << ": " << d << " dB");
            CHECK (d > 3.0);
        }
}

TEST_CASE ("Presets: about the input's loudness, K-weighted (calibrated makeup)", "[crackle]")
{
    // (K-weighted, as heard: the band and the radio take away lows that weigh in a
    // plain RMS but little to the ear — the calibration matches loudness)
    auto probe = makeProc();
    const auto in = music (6.0);
    const double dry = gk::KWeighting::levelDb (in, sr, (int) sr);
    for (int i = 0; i < numPresets; ++i)
    {
        auto p = makeProc (i);
        const double wet = gk::KWeighting::levelDb (processBlocks (*p, in, { 512 }), sr, (int) sr);
        INFO (probe->getPresets()[i].name << ": " << wet - dry << " dB from the input");
        CHECK (std::fabs (wet - dry) < 3.0);
    }
}

TEST_CASE ("Moving GRAIN and every knob while music plays: no crackle beyond the effect's own", "[crackle]")
{
    // each knob swept 0 -> 1 -> 0 over 2 s, set every 128-sample block, the noises
    // off (NOISE 0, no DROPOUT: they are crackles by design), against it held at 0
    const auto in = music (2.5);
    auto run = [&] (int knob, bool sweep) // -2: GRAIN
    {
        auto p = makeProc();
        setParam (p->getKnobParameter (CrackleDsp::noiseKnob), 0.0f);
        setParam (p->getKnobParameter (CrackleDsp::dropoutKnob), 0.0f);
        setParam (p->getKnobParameter (CrackleDsp::crushKnob), 0.0f);
        return processBlocks (*p, in, { 128 }, [&] (int pos)
        {
            const double phase = juce::jlimit (0.0, 1.0, (pos / sr - 0.25) / 2.0);
            const float v = sweep ? (float) (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase) : 0.0f;
            if (knob == -2)
                p->getAmountParameter().setValueNotifyingHost (v);
            else
                p->getKnobParameter (knob).setValueNotifyingHost (v);
        });
    };
    for (int k : { -2, (int) CrackleDsp::bandKnob, (int) CrackleDsp::wowKnob, (int) CrackleDsp::flutterKnob, (int) CrackleDsp::saturationKnob,
                   (int) CrackleDsp::radioKnob, (int) CrackleDsp::mixKnob, (int) CrackleDsp::focusLowKnob })
    {
        const auto still = run (k, false), moved = run (k, true);
        const double a = zipperBurstDb (still.getReadPointer (0), (int) (0.3 * sr), still.getNumSamples(), sr);
        const double b = zipperBurstDb (moved.getReadPointer (0), (int) (0.3 * sr), moved.getNumSamples(), sr);
        INFO ("knob " << k << ": HF bursts " << b << " dB, held " << a << " dB");
        CHECK (b < std::max (a + 6.0, -100.0));
    }
}

TEST_CASE ("CPU: stereo at 48 kHz", "[crackle]")
{
    auto p = makeProc (11); // Crushed Dub Tape: nearly everything on
    const auto in = music (20.0);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    processBlocks (*p, in, { 128 });
    const double share = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / 20.0;
    WARN ("Crackle: " << share * 100.0 << " % of one core");
    if (timingChecksEnforced())
        CHECK (share < 0.03);
}
