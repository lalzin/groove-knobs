// BALLROOM tests (spec 6): MIX 0 = the input; RT60 within 15 % of DECAY
// (500 Hz - 4 kHz) at 0.5 / 2 / 8 s; the low and high bands follow BASS MULT
// and HIGH MULT within 20 %; no click on a mode, size or FREEZE change; FREEZE
// holds without growing; no allocation in processBlock; CPU; plus SPACE as
// the dry / wet (one with MIX, 100 % = the reverb alone at the dry's level),
// "custom", the presets, the state.

#include "BallroomProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <new>

using namespace gktest;
using bl::BallroomDsp;
using bl::BallroomProcessor;

namespace
{
thread_local bool countAllocations = false;
std::atomic<int> allocations { 0 };
} // namespace
void* operator new (std::size_t n)
{
    if (countAllocations)
        ++allocations;
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double sr = 48000.0;

constexpr int numPresets = 22;

std::unique_ptr<BallroomProcessor> makeProc (int preset = 0)
{
    auto p = std::make_unique<BallroomProcessor>();
    setParam (p->getPresetParameter(), (float) preset);
    p->applyPreset (preset);
    setLayout (*p, 2);
    p->setNonRealtime (true);
    p->prepareToPlay (sr, 512);
    return p;
}

void setKnob (BallroomProcessor& p, BallroomDsp::Knob k, float v) { setParam (p.getKnob (k), v); }

/** The wet response to an impulse (MIX 1). */
Buffer impulseResponse (BallroomProcessor& p, double seconds)
{
    // (at 50 ms: the frame fades the engine's input in over its first 3 ms)
    const int at = (int) (0.05 * sr);
    Buffer in (2, at + (int) (seconds * sr));
    in.clear();
    in.setSample (0, at, 1.0f);
    in.setSample (1, at, 1.0f);
    const auto out = processBlocks (p, in, { 512 });
    Buffer ir (2, out.getNumSamples() - at);
    for (int c = 0; c < 2; ++c)
        ir.copyFrom (c, 0, out, c, at, ir.getNumSamples());
    return ir;
}

/** RT60 of a band (8-pole filters, Schroeder integration, T30). */
double rt60 (const Buffer& ir, double lo, double hi)
{
    std::vector<double> e ((size_t) ir.getNumSamples(), 0.0);
    const double aH = std::exp (-2.0 * juce::MathConstants<double>::pi * lo / sr), aL = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * hi / sr);
    for (int c = 0; c < 2; ++c)
    {
        double h[8] {}, pv[8] {}, lp[8] {};
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            double v = ir.getSample (c, i);
            for (int k = 0; k < 8; ++k)
            {
                h[k] = aH * (h[k] + v - pv[k]);
                pv[k] = v;
                v = h[k];
            }
            for (int k = 0; k < 8; ++k)
            {
                lp[k] += aL * (v - lp[k]);
                v = lp[k];
            }
            e[(size_t) i] += v * v;
        }
    }
    std::vector<double> edc (e.size());
    double acc = 0.0;
    for (size_t i = e.size(); i-- > 0;)
        edc[i] = (acc += e[i]);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / edc[0] + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = (double) i / sr;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++count;
        }
    }
    return count < 10 ? 0.0 : -60.0 / ((count * sxy - sx * sy) / (count * sxx - sx * sx));
}

/** A ROOM set for a measurement: Ballroom mode, the late only, wet only. */
std::unique_ptr<BallroomProcessor> measuringProc (double decay, double bass, double high)
{
    auto p = makeProc (0);
    setParam (p->getChoice (BallroomDsp::modeChoice), (float) ballroom::Mode::studio); // (no warmth of its own on the lows)
    setKnob (*p, BallroomDsp::mixKnob, 1.0f);
    setKnob (*p, BallroomDsp::depthKnob, 1.0f);
    setKnob (*p, BallroomDsp::decayKnob, (float) decay);
    setKnob (*p, BallroomDsp::bassMultKnob, (float) bass);
    setKnob (*p, BallroomDsp::highMultKnob, (float) high);
    setKnob (*p, BallroomDsp::highCutKnob, 20000.0f);
    setKnob (*p, BallroomDsp::predelayKnob, 0.0f);
    setKnob (*p, BallroomDsp::bassXoverKnob, 500.0f); // (the crossovers the bands are chosen for)
    setKnob (*p, BallroomDsp::highXoverKnob, 5000.0f);
    setKnob (*p, BallroomDsp::earlySendKnob, 0.0f); // the late fed by the impulse itself (not spread by the early first)
    setKnob (*p, BallroomDsp::clearKnob, 0.0f);
    p->prepareToPlay (sr, 512); // the networks set for this mode at once
    return p;
}

float maxStep (const Buffer& b, int from = 0)
{
    float m = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = juce::jmax (1, from); i < b.getNumSamples(); ++i)
            m = juce::jmax (m, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return m;
}

Buffer sine (double seconds, float level = 0.4f)
{
    Buffer b (2, (int) (seconds * sr));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
    return b;
}
} // namespace

TEST_CASE ("BL 1. SPACE 0 (MIX 0) on every preset: the output is the input, sample for sample", "[bl][mix]")
{
    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        setParam (p->getAmountParameter(), 0.0f); // the one knob at 0 %: no reverb at all
        CHECK (p->getKnob (BallroomDsp::mixKnob).getValue() == 0.0f);
        p->snapToTargets();
        Buffer in (2, (int) (1.5 * sr));
        juce::Random rng (preset + 1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (c, i, rng.nextFloat() - 0.5f);
        const auto out = processBlocks (*p, in, { 512, 77, 1024 });
        float diff = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = (int) (0.1 * sr); i < in.getNumSamples(); ++i) // (after the MIX smoothing settles)
                diff = juce::jmax (diff, std::abs (out.getSample (c, i) - in.getSample (c, i)));
        CHECK (diff == 0.0f);
    }
}

TEST_CASE ("BL 2. RT60 (500 Hz - 4 kHz) within 15 % of DECAY at 0.5, 2 and 8 s", "[bl][rt60]")
{
    for (double decay : { 0.5, 2.0, 8.0 })
    {
        CAPTURE (decay);
        auto p = measuringProc (decay, 1.0, 1.0);
        const double m = rt60 (impulseResponse (*p, decay * 0.9 + 0.6), 500, 4000);
        INFO ("measured " << m << " s");
        CHECK (std::abs (m / decay - 1.0) < 0.15);
    }
}

TEST_CASE ("BL 3. BASS MULT and HIGH MULT: the bands' RT60 follow within 20 %", "[bl][rt60]")
{
    for (auto [bass, high] : { std::pair { 1.2, 0.5 }, { 2.0, 0.3 }, { 0.5, 1.0 }, { 4.0, 0.1 } })
    {
        CAPTURE (bass, high);
        auto p = measuringProc (2.0, bass, high);
        const auto ir = impulseResponse (*p, 2.0 * std::max (1.0, bass) * 0.9 + 0.6);
        // (the highs an octave and more above HIGH XOVER: the shelf's own transition
        // decays in between and would leak into a band starting closer)
        const double lo = rt60 (ir, 80, 200), mid = rt60 (ir, 700, 2500), hi = rt60 (ir, 10000, 16000);
        INFO ("low " << lo << " s, mid " << mid << " s, high " << hi << " s");
        CHECK (std::abs (lo / (2.0 * bass) - 1.0) < 0.20);
        CHECK (std::abs (hi / (2.0 * high) - 1.0) < 0.20);
    }
}

TEST_CASE ("BL 4. No click on a mode, size or FREEZE change", "[bl][click]")
{
    auto steadyProc = makeProc (0);
    const float base = maxStep (processBlocks (*steadyProc, sine (4.0), { 512 }), (int) (0.2 * sr));
    struct Case { const char* name; std::function<void (BallroomProcessor&, int)> change; };
    const Case cases[] {
        { "mode", [] (BallroomProcessor& p, int pos) { setParam (p.getChoice (BallroomDsp::modeChoice), (float) ((pos / (int) (0.5 * sr)) % 6)); } },
        { "late size", [] (BallroomProcessor& p, int pos) { setKnob (p, BallroomDsp::lateSizeKnob, (pos / (int) (0.5 * sr)) % 2 ? 0.9f : 0.2f); } },
        { "early size", [] (BallroomProcessor& p, int pos) { setKnob (p, BallroomDsp::earlySizeKnob, (pos / (int) (0.4 * sr)) % 2 ? 150.0f : 10.0f); } },
        { "freeze", [] (BallroomProcessor& p, int pos) { setParam (p.getChoice (BallroomDsp::freezeChoice), (float) ((pos / (int) (0.7 * sr)) % 2)); } },
    };
    for (const auto& c : cases)
    {
        CAPTURE (c.name);
        auto p = makeProc (0);
        const auto out = processBlocks (*p, sine (4.0), { 512 }, [&] (int pos) { c.change (*p, pos); });
        const float step = maxStep (out, (int) (0.2 * sr));
        INFO ("largest step " << step << " (steady " << base << ")");
        CHECK (allFinite (out));
        CHECK (step < base * 2.2f); // (crossfades between networks: no jump beyond a quick blend)
    }
}

TEST_CASE ("BL 5. FREEZE holds: a minute without the level growing", "[bl][freeze]")
{
    auto p = makeProc (11); // Frozen Floor (Space mode): feed it, then freeze
    setParam (p->getChoice (BallroomDsp::freezeChoice), 0.0f);
    Buffer noise (2, (int) sr);
    juce::Random rng (4);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (c, i, 0.3f * (rng.nextFloat() - 0.5f));
    processBlocks (*p, noise, { 512 });
    setParam (p->getChoice (BallroomDsp::freezeChoice), 1.0f);
    Buffer silence (2, (int) (61.0 * sr));
    silence.clear();
    const auto out = processBlocks (*p, silence, { 512 });
    const double first = out.getRMSLevel (0, (int) sr, (int) (10 * sr)), last = out.getRMSLevel (0, (int) (51 * sr), (int) (10 * sr));
    INFO ("first " << juce::Decibels::gainToDecibels (first) << " dB, last " << juce::Decibels::gainToDecibels (last) << " dB");
    CHECK (first > 1.0e-3);
    CHECK (last <= first * 1.02);
    CHECK (last >= first * 0.9);
}

TEST_CASE ("BL 6. No allocation in processBlock (mode changes included)", "[bl][alloc]")
{
    auto p = makeProc (0);
    Buffer in = sine (0.5);
    juce::MidiBuffer midi;
    processBlocks (*p, in, { 512 });
    allocations = 0;
    countAllocations = true;
    for (int k = 0; k < 60; ++k)
    {
        if (k == 20) // (the host side of the change is not processBlock's: not counted)
        {
            countAllocations = false;
            p->getChoice (BallroomDsp::modeChoice).setValueNotifyingHost (p->getChoice (BallroomDsp::modeChoice).convertTo0to1 (5.0f));
            countAllocations = true;
        }
        Buffer view (in.getArrayOfWritePointers(), 2, 0, 512);
        p->processBlock (view, midi);
    }
    countAllocations = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("BL 7. SPACE is the dry / wet, one with MIX; a ROOM setting makes it custom", "[bl][macro]")
{
    BallroomProcessor p;
    p.applyPreset (0); // Disco Ballroom
    CHECK_FALSE (p.isCustom());
    auto value = [&p] (BallroomDsp::Knob k) { return p.getKnob (k).convertFrom0to1 (p.getKnob (k).getValue()); };
    const float decay = value (BallroomDsp::decayKnob);
    for (float s : { 0.0f, 0.5f, 1.0f })
    {
        setParam (p.getAmountParameter(), s);
        CHECK (value (BallroomDsp::mixKnob) == Catch::Approx (s).margin (0.005f));
        CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (decay)); // the room is the preset's
    }
    setKnob (p, BallroomDsp::mixKnob, 0.25f); // MIX moves SPACE
    CHECK (p.getAmountParameter().getValue() == Catch::Approx (0.25f).margin (0.005f));
    CHECK_FALSE (p.isCustom());
    setKnob (p, BallroomDsp::earlyCrossKnob, 0.9f);
    CHECK (p.isCustom());
    setParam (p.getAmountParameter(), 0.5f);
    CHECK (p.isCustom()); // (SPACE does not undo the room's change)
    p.applyPreset (10); // Afterparty
    CHECK_FALSE (p.isCustom());
    CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (28.0f).margin (0.1f));
    CHECK (value (BallroomDsp::mixKnob) == Catch::Approx (0.55f).margin (0.005f));
}

TEST_CASE ("BL 7b. SPACE 100 %: the reverb alone, about as loud as the dry", "[bl][macro]")
{
    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        setParam (p->getAmountParameter(), 1.0f);
        setKnob (*p, BallroomDsp::clearKnob, 0.0f); // (the level itself: CLEAR lowers the tail under the notes, on purpose)
        p->snapToTargets();
        // pink noise notes (0.25 s every 0.75 s; render.cpp's calibration signal): the dry must be gone
        Buffer in (2, (int) (6.0 * sr));
        juce::Random rng (preset + 7);
        double b[7] {};
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double t = std::fmod ((double) i / sr, 0.75), w = 2.0 * rng.nextFloat() - 1.0; // (Kellet's pink filter)
            b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.96900 * b[2] + w * 0.1538520;
            b[3] = 0.86650 * b[3] + w * 0.3104856; b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
            const double pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362;
            b[6] = w * 0.115926;
            const float v = t < 0.25 ? (float) (0.05 * pink * std::min (1.0, t / 0.005) * std::min (1.0, (0.25 - t) / 0.01)) : 0.0f;
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
        const auto out = processBlocks (*p, in, { 512 });
        const double dryDb = juce::Decibels::gainToDecibels (in.getRMSLevel (0, 0, in.getNumSamples()));
        const double wetDb = juce::Decibels::gainToDecibels (0.5 * (out.getRMSLevel (0, 0, out.getNumSamples()) + out.getRMSLevel (1, 0, out.getNumSamples())));
        INFO ("wet " << wetDb - dryDb << " dB against the dry");
        CHECK (std::abs (wetDb - dryDb) < 4.5);
        // no dry left: the output against the input, correlated sample for sample, is far from 1
        double xy = 0, xx = 0, yy = 0;
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double x = in.getSample (0, i), y = out.getSample (0, i);
            xy += x * y; xx += x * x; yy += y * y;
        }
        CHECK (std::abs (xy / std::sqrt (xx * yy + 1e-30)) < 0.3);
    }
}

TEST_CASE ("BL 8. Presets, parameters and state", "[bl][state]")
{
    BallroomProcessor p;
    CHECK (p.getNumPrograms() == numPresets);
    CHECK (p.getProgramName (0) == "Disco Ballroom");
    CHECK (p.getLatencySamples() == 0);
    // each preset its own room
    const char* modes[] { "Ballroom", "Club", "Studio", "Studio", "Gate", "Plate", "Mirror", "Ballroom", "Ballroom", "Space", "Space", "Space",
                         "Ballroom", "Gate", "Space", "Club", "Plate", "Club", "Studio", "Mirror", "Space", "Studio" };
    std::vector<float> decays;
    for (int i = 0; i < numPresets; ++i)
    {
        p.applyPreset (i);
        CHECK (p.getChoice (BallroomDsp::modeChoice).getCurrentChoiceName() == modes[i]);
        CHECK (p.getChoice (BallroomDsp::freezeChoice).getIndex() == 0); // (FREEZE: the player's, a preset never loads silent)
        decays.push_back (p.getKnob (BallroomDsp::decayKnob).convertFrom0to1 (p.getKnob (BallroomDsp::decayKnob).getValue()));
    }
    // far apart: the shortest under 0.5 s, the longest over 20 s
    CHECK (*std::min_element (decays.begin(), decays.end()) < 0.5f);
    CHECK (*std::max_element (decays.begin(), decays.end()) > 20.0f);
    // the state keeps everything, "custom" included
    p.applyPreset (7); // Warm Hall
    setKnob (p, BallroomDsp::highXoverKnob, 4200.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    BallroomProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getPresetParameter().getIndex() == 7);
    CHECK (q.isCustom());
    CHECK (q.getKnob (BallroomDsp::highXoverKnob).convertFrom0to1 (q.getKnob (BallroomDsp::highXoverKnob).getValue()) == Catch::Approx (4200.0f).margin (5.0f));
    CHECK (q.getKnob (BallroomDsp::bassMultKnob).convertFrom0to1 (q.getKnob (BallroomDsp::bassMultKnob).getValue()) == Catch::Approx (2.0f).margin (0.01f));
}

TEST_CASE ("BL 9. Every preset sounds, finite, under the clip", "[bl][presets]")
{
    for (int preset = 0; preset < numPresets; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        setParam (p->getChoice (BallroomDsp::freezeChoice), 0.0f);
        Buffer in (2, (int) (3.0 * sr));
        juce::Random rng (preset);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (c, i, i < (int) sr ? 0.9f * (rng.nextFloat() - 0.5f) : 0.0f);
        const auto out = processBlocks (*p, in, { 512 });
        CHECK (allFinite (out));
        CHECK (out.getMagnitude (0, out.getNumSamples()) <= 1.0f);
        CHECK (out.getRMSLevel (0, (int) (1.05 * sr), (int) (0.1 * sr)) > 1.0e-4); // a tail after the input stops
    }
}

TEST_CASE ("BL 11. CLEAR: the tail steps back where the dry plays, band by band, and comes back in the gaps", "[bl][clear]")
{
    // a 4 s tail; 2 s of a low tone (100 Hz) and a high one (6 kHz), then only the low one 1 s, then silence
    auto run = [] (float clear)
    {
        auto p = makeProc (7); // Warm Hall
        setKnob (*p, BallroomDsp::highCutKnob, 20000.0f);
        setKnob (*p, BallroomDsp::highMultKnob, 1.0f);
        setKnob (*p, BallroomDsp::clearKnob, clear);
        setParam (p->getAmountParameter(), 1.0f); // the wet alone
        p->snapToTargets();
        Buffer in (2, (int) (5.0 * sr));
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double t = (double) i / sr;
            const double lo = t < 3.0 ? 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * t) : 0.0;
            const double hi = t < 2.0 ? 0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 6000.0 * t) : 0.0;
            in.setSample (0, i, (float) (lo + hi));
            in.setSample (1, i, (float) (lo + hi));
        }
        return processBlocks (*p, in, { 512 });
    };
    const auto off = run (0.0f), on = run (1.0f);
    // the level of one band of the output over [a, b] s
    auto band = [] (const Buffer& b, double f0, double f1, double a, double e)
    {
        juce::IIRFilter hp, lp;
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, f0));
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, f1));
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = lp.processSingleSampleRaw (hp.processSingleSampleRaw (b.getSample (0, i)));
            if (i >= (int) (a * sr) && i < (int) (e * sr)) { acc += (double) v * v; ++n; }
        }
        return 10.0 * std::log10 (acc / juce::jmax (1, n) + 1.0e-30);
    };
    // while both tones play: the tail steps back in both bands
    const double lowWhile = band (on, 60, 160, 1.0, 2.0) - band (off, 60, 160, 1.0, 2.0);
    const double highWhile = band (on, 4500, 8000, 1.0, 2.0) - band (off, 4500, 8000, 1.0, 2.0);
    // the high tone stopped, the low one goes on: the high tail comes back, the low one stays back
    const double highGap = band (on, 4500, 8000, 2.5, 3.0) - band (off, 4500, 8000, 2.5, 3.0);
    const double lowStill = band (on, 60, 160, 2.5, 3.0) - band (off, 60, 160, 2.5, 3.0);
    // everything stopped: the whole tail back
    const double lowAfter = band (on, 60, 160, 3.5, 4.5) - band (off, 60, 160, 3.5, 4.5);
    INFO ("low while " << lowWhile << " dB, high while " << highWhile << " dB, high in the gap " << highGap << " dB, low still " << lowStill
                       << " dB, low after " << lowAfter << " dB");
    CHECK (lowWhile < -6.0);
    CHECK (highWhile < -6.0);
    CHECK (highGap > -2.0);
    CHECK (lowStill < -6.0);
    CHECK (lowAfter > -2.0);
}

TEST_CASE ("BL 10. CPU: stereo, 48 kHz, blocks of 512", "[bl][cpu]")
{
    auto p = makeProc (0);
    Buffer in (2, 512);
    juce::Random rng (2);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 512; ++i)
            in.setSample (c, i, 0.2f * (rng.nextFloat() - 0.5f));
    juce::MidiBuffer midi;
    const int blocks = (int) (10.0 * sr / 512);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < blocks; ++k)
    {
        Buffer view (in.getArrayOfWritePointers(), 2, 0, 512);
        p->processBlock (view, midi);
    }
    const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    WARN ("CPU " << 100.0 * s / 10.0 << " % of one core");
    if (timingChecksEnforced())
        CHECK (s / 10.0 < 0.05);
}
