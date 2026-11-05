// BALLROOM tests (spec 6): MIX 0 = the input; RT60 within 15 % of DECAY
// (500 Hz - 4 kHz) at 0.5 / 2 / 8 s; the low and high bands follow BASS MULT
// and HIGH MULT within 20 %; no click on a mode, size or FREEZE change; FREEZE
// holds without growing; no allocation in processBlock; CPU; plus SPACE's
// macro and "custom", the presets, the state.

#include "BallroomProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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

TEST_CASE ("BL 1. MIX 0: the output is the input, sample for sample", "[bl][mix]")
{
    for (int preset = 0; preset < 10; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        setKnob (*p, BallroomDsp::mixKnob, 0.0f);
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
    auto p = makeProc (9); // Frozen Floor (FREEZE on): feed it first, then freeze
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

TEST_CASE ("BL 7. SPACE is a macro; a ROOM setting makes it custom, SPACE takes it back", "[bl][macro]")
{
    BallroomProcessor p;
    p.applyPreset (0); // Disco Ballroom
    CHECK_FALSE (p.isCustom());
    auto value = [&p] (BallroomDsp::Knob k) { return p.getKnob (k).convertFrom0to1 (p.getKnob (k).getValue()); };
    setParam (p.getAmountParameter(), 0.0f);
    CHECK (value (BallroomDsp::mixKnob) == Catch::Approx (0.10f).margin (0.005f));
    CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (0.3f).margin (0.01f));
    setParam (p.getAmountParameter(), 1.0f);
    CHECK (value (BallroomDsp::mixKnob) == Catch::Approx (0.45f).margin (0.005f));
    CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (8.0f).margin (0.05f));
    CHECK (value (BallroomDsp::predelayKnob) == Catch::Approx (40.0f).margin (0.2f));
    CHECK_FALSE (p.isCustom());
    setKnob (p, BallroomDsp::earlyCrossKnob, 0.9f);
    CHECK (p.isCustom());
    setParam (p.getAmountParameter(), 0.5f);
    CHECK_FALSE (p.isCustom());
    // a preset's own values stay over the macro (Afterparty: 20 s whatever SPACE)
    p.applyPreset (8);
    CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (20.0f).margin (0.1f));
    setParam (p.getAmountParameter(), 0.6f);
    CHECK (value (BallroomDsp::decayKnob) == Catch::Approx (20.0f).margin (0.1f));
}

TEST_CASE ("BL 8. Presets, parameters and state", "[bl][state]")
{
    BallroomProcessor p;
    CHECK (p.getNumPrograms() == 10);
    CHECK (p.getProgramName (0) == "Disco Ballroom");
    CHECK (p.getLatencySamples() == 0);
    // each preset its own mode / SPACE
    const char* modes[] { "Ballroom", "Club", "Mirror", "Plate", "Studio", "Ballroom", "Studio", "Space", "Space", "Space" };
    for (int i = 0; i < 10; ++i)
    {
        p.applyPreset (i);
        CHECK (p.getChoice (BallroomDsp::modeChoice).getCurrentChoiceName() == modes[i]);
    }
    CHECK (p.getChoice (BallroomDsp::freezeChoice).getIndex() == 1); // Frozen Floor
    // the state keeps everything, "custom" included
    p.applyPreset (5);
    setKnob (p, BallroomDsp::highXoverKnob, 3000.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    BallroomProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getPresetParameter().getIndex() == 5);
    CHECK (q.isCustom());
    CHECK (q.getKnob (BallroomDsp::highXoverKnob).convertFrom0to1 (q.getKnob (BallroomDsp::highXoverKnob).getValue()) == Catch::Approx (3000.0f).margin (5.0f));
    CHECK (q.getKnob (BallroomDsp::bassMultKnob).convertFrom0to1 (q.getKnob (BallroomDsp::bassMultKnob).getValue()) == Catch::Approx (1.8f).margin (0.01f));
}

TEST_CASE ("BL 9. Every preset sounds, finite, under the clip", "[bl][presets]")
{
    for (int preset = 0; preset < 10; ++preset)
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
