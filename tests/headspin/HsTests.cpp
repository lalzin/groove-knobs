// HEAD SPIN tests (spec 7): MIX 0 = the input, the direction (left to right
// crosses in front), constant speed along the shapes (< 2 % on the triangle),
// no click when the shape, the direction or the mode changes, the phase locked
// to the tempo for 64 bars, no allocation in processBlock; plus stability,
// state and parameters, mono in -> stereo out, the presets.

#include "HeadSpinProcessor.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <new>

using namespace gktest;
using hs::HeadSpinDsp;
using hs::HeadSpinProcessor;

// --- allocation counter (only counts while `countAllocations` is set, on this thread)
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

std::unique_ptr<HeadSpinProcessor> makeProc (int preset = 0, int channels = 2)
{
    auto p = std::make_unique<HeadSpinProcessor>();
    setParam (p->getPresetParameter(), (float) preset);
    p->applyPreset (preset);
    setLayout (*p, channels);
    p->setNonRealtime (true);
    p->prepareToPlay (sr, 512);
    p->updateShapeNow();
    return p;
}

Buffer sine (double seconds, double hz = 1000.0, float level = 0.5f, int channels = 2)
{
    Buffer b (channels, (int) (seconds * sr));
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
    return b;
}

float maxStep (const Buffer& b, int from = 0)
{
    float m = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = juce::jmax (1, from); i < b.getNumSamples(); ++i)
            m = juce::jmax (m, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return m;
}
} // namespace

TEST_CASE ("HS 1. MIX 0: the output is the input, sample for sample", "[hs][mix]")
{
    for (int preset = 0; preset < 7; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        setParam (p->getAmountParameter(), 0.0f);
        Buffer in (2, (int) (2.0 * sr));
        juce::Random rng (preset + 1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (c, i, rng.nextFloat() - 0.5f);
        p->snapToTargets();
        const auto out = processBlocks (*p, in, { 512, 77, 1024 });
        float diff = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i)
                diff = juce::jmax (diff, std::abs (out.getSample (c, i) - in.getSample (c, i)));
        CHECK (diff == 0.0f);
    }
}

TEST_CASE ("HS 2. Direction: left to right goes left, front, right; right to left goes left, back, right", "[hs][direction]")
{
    for (bool leftToRight : { true, false })
    {
        CAPTURE (leftToRight);
        auto p = makeProc (0);
        setParam (p->getChoice (HeadSpinDsp::rateModeChoice), 0.0f); // Hz
        setParam (p->getKnob (HeadSpinDsp::rateHzKnob), 0.25f);
        setParam (p->getKnob (HeadSpinDsp::phaseKnob), 270.0f); // start on the left
        setParam (p->getChoice (HeadSpinDsp::directionChoice), leftToRight ? 0.0f : 1.0f);
        p->snapToTargets();
        std::vector<float> azimuths;
        const auto in = sine (4.0);
        processBlocks (*p, in, { 512 }, [&] (int) { azimuths.push_back (p->getDsp().getTelemetry().azimuthDeg.load()); });
        // a quarter turn in: in front (0) for left to right, behind (180) the other way
        const float atQuarter = azimuths[(size_t) (1.0 * sr / 512)];
        INFO ("azimuth after a quarter turn " << atQuarter);
        if (leftToRight)
            CHECK (std::abs (atQuarter) < 10.0f);
        else
            CHECK (std::abs (atQuarter) > 170.0f);
        // half a turn in: on the right either way
        CHECK (azimuths[(size_t) (2.0 * sr / 512)] == Catch::Approx (90.0f).margin (10.0f));
    }
    // the sound itself, from the left through the front: R - L from about -20 dB to about +20 dB
    auto p = makeProc (0);
    setParam (p->getChoice (HeadSpinDsp::rateModeChoice), 0.0f);
    setParam (p->getKnob (HeadSpinDsp::rateHzKnob), 0.25f);
    setParam (p->getKnob (HeadSpinDsp::phaseKnob), 270.0f);
    setParam (p->getKnob (HeadSpinDsp::distanceKnob), 0.0f);
    p->snapToTargets();
    const auto out = processBlocks (*p, sine (2.0), { 512 });
    auto balance = [&] (double t)
    {
        const int a = (int) (t * sr), n = (int) (0.05 * sr);
        return 10.0 * std::log10 ((double) out.getRMSLevel (1, a, n) * out.getRMSLevel (1, a, n) / ((double) out.getRMSLevel (0, a, n) * out.getRMSLevel (0, a, n)));
    };
    CHECK (balance (0.05) < -15.0);
    CHECK (std::abs (balance (0.97)) < 3.0);
    CHECK (balance (1.9) > 15.0);
}

TEST_CASE ("HS 3. Constant speed along every shape (spec: < 2 % on the triangle)", "[hs][shape]")
{
    headspin::ShapeBank bank;
    for (int step = 0; step <= 24; ++step) // the seven shapes (custom: a circle until drawn) and the morphs in between, every 0.25
    {
        const double s = step * 0.25;
        CAPTURE (s);
        headspin::ShapeTable t;
        bank.buildMorph (s, t);
        constexpr int steps = 2048;
        std::vector<double> len;
        double mean = 0.0;
        for (int k = 0; k < steps; ++k)
        {
            const auto a = t.at ((double) k / steps), b = t.at ((double) (k + 1) / steps);
            len.push_back (std::hypot (b.x - a.x, b.y - a.y));
            mean += len.back() / steps;
        }
        double dev = 0.0;
        for (double l : len)
            dev = std::max (dev, std::abs (l - mean) / mean);
        // The eight's left loop turns the other way: half way to its
        // neighbours the mix has to turn back once (a U-turn, rounded, one
        // table step short); everywhere else the speed is constant.
        const bool mixedWithEight = s > 3.0 && s < 5.0 && step % 4 != 0;
        CHECK (dev < (mixedWithEight ? 0.15 : 0.02));
    }
}

TEST_CASE ("HS 4. No click when the shape, the direction, the mode or the render changes", "[hs][click]")
{
    // a 1 kHz sine: its own steps are known; changes every 0.3 s must not add a bigger one
    auto steadyProc = makeProc (0);
    const auto steady = processBlocks (*steadyProc, sine (4.0), { 512 });
    const float base = maxStep (steady, (int) (0.1 * sr));
    struct Case { const char* name; std::function<void (HeadSpinProcessor&, int)> change; };
    const Case cases[] {
        { "shape", [] (HeadSpinProcessor& p, int pos) { setParam (p.getKnob (HeadSpinDsp::shapeKnob), (float) ((pos / (int) (0.3 * sr)) % 4)); p.updateShapeNow(); } },
        { "direction", [] (HeadSpinProcessor& p, int pos) { setParam (p.getChoice (HeadSpinDsp::directionChoice), (float) ((pos / (int) (0.3 * sr)) % 2)); } },
        { "mode", [] (HeadSpinProcessor& p, int pos) { setParam (p.getChoice (HeadSpinDsp::rateModeChoice), (float) ((pos / (int) (0.3 * sr)) % 2)); } },
        { "render", [] (HeadSpinProcessor& p, int pos) { setParam (p.getChoice (HeadSpinDsp::renderChoice), (float) ((pos / (int) (0.3 * sr)) % 2)); } },
    };
    for (const auto& c : cases)
    {
        CAPTURE (c.name);
        auto p = makeProc (0);
        const auto out = processBlocks (*p, sine (4.0), { 512 }, [&] (int pos) { c.change (*p, pos); });
        const float step = maxStep (out, (int) (0.1 * sr));
        INFO ("largest step " << step << " (steady " << base << ")");
        CHECK (step < base * 1.6f); // the star / square come closer to the head (louder): no jump beyond that
        CHECK (allFinite (out));
    }
}

TEST_CASE ("HS 5. Sync: the phase stays locked to the bar for 64 bars", "[hs][sync]")
{
    headspin::Spinner s;
    s.prepare (sr);
    headspin::Params p;
    p.sync = true;
    p.division = headspin::division::oneBar;
    s.setParams (p);
    s.updateShape();
    std::vector<float> z (512), out (512);
    const double bpm = 124.0;
    const int total = (int) std::llround (64 * 4 * 60.0 / bpm * sr);
    double worst = 0.0;
    for (int i = 0; i < total; i += 512)
    {
        headspin::Transport t;
        t.bpm = bpm;
        t.playing = true;
        t.ppq = i / sr * bpm / 60.0;
        s.process (z.data(), z.data(), out.data(), out.data(), 512, t);
        const double expected = (i + 512) / sr * bpm / 60.0 / 4.0;
        double err = std::fmod (std::abs (s.getPhase() - expected), 1.0);
        worst = std::max (worst, std::min (err, 1.0 - err));
    }
    INFO ("worst phase error " << worst * 360.0 << " deg");
    CHECK (worst * 360.0 < 0.5);
}

TEST_CASE ("HS 6. No allocation in processBlock", "[hs][alloc]")
{
    auto p = makeProc (5); // the figure eight, Sync
    Buffer in = sine (0.5);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 100);
    setParam (p->getChoice (HeadSpinDsp::retriggerChoice), 1.0f);
    processBlocks (*p, in, { 512 }); // warm up
    allocations = 0;
    countAllocations = true;
    for (int k = 0; k < 40; ++k)
    {
        Buffer view (in.getArrayOfWritePointers(), 2, 0, 512);
        p->processBlock (view, midi);
    }
    countAllocations = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("HS 7. Stability: loud noise, every preset, fast and slow, finite and under the safety clip", "[hs][stability]")
{
    for (int preset = 0; preset < 7; ++preset)
    {
        CAPTURE (preset);
        auto p = makeProc (preset);
        Buffer in (2, (int) (3.0 * sr));
        juce::Random rng (9);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (c, i, 1.6f * (rng.nextFloat() - 0.5f));
        const auto out = processBlocks (*p, in, { 512, 64 });
        CHECK (allFinite (out));
        CHECK (out.getMagnitude (0, out.getNumSamples()) <= 1.0f);
    }
}

TEST_CASE ("HS 8. Parameters, state, presets, mono in -> stereo out", "[hs][state]")
{
    HeadSpinProcessor p;
    const char* ids[] { "mix", "preset", "shape", "width", "depth", "rotation", "rateHz", "phase", "distance", "groove", "level", "focus",
                        "direction", "rateMode", "rateSync", "render", "retrigger", "bypass" };
    REQUIRE (p.getParameters().size() == 18);
    for (int i = 0; i < 18; ++i)
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (p.getParameters()[i])->getParameterID() == ids[i]);
    CHECK (p.getNumPrograms() == 7);
    CHECK (p.getAmountParameter().getValue() == Catch::Approx (1.0f));
    CHECK (p.getLatencySamples() == 0);
    CHECK (p.acceptsMidi());

    // a preset sets the motion, not LEVEL
    setParam (p.getKnob (HeadSpinDsp::levelKnob), -6.0f);
    p.applyPreset (4); // Headspin: 8 Hz
    CHECK (p.getChoice (HeadSpinDsp::rateModeChoice).getIndex() == 0);
    CHECK (p.getKnob (HeadSpinDsp::rateHzKnob).convertFrom0to1 (p.getKnob (HeadSpinDsp::rateHzKnob).getValue()) == Catch::Approx (8.0f).margin (0.01f));
    CHECK (p.getKnob (HeadSpinDsp::levelKnob).convertFrom0to1 (p.getKnob (HeadSpinDsp::levelKnob).getValue()) == Catch::Approx (-6.0f).margin (0.01f));
    p.applyPreset (6); // Reverse Ride
    CHECK (p.getChoice (HeadSpinDsp::directionChoice).getIndex() == 1);
    CHECK (p.getChoice (HeadSpinDsp::rateSyncChoice).getCurrentChoiceName() == "2 bars");

    // state round trip
    setParam (p.getKnob (HeadSpinDsp::shapeKnob), 2.5f);
    setParam (p.getAmountParameter(), 0.4f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    HeadSpinProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getKnob (HeadSpinDsp::shapeKnob).convertFrom0to1 (q.getKnob (HeadSpinDsp::shapeKnob).getValue()) == Catch::Approx (2.5f).margin (0.01f));
    CHECK (q.getAmountParameter().getValue() == Catch::Approx (0.4f).margin (1.0e-4));
    CHECK (q.getChoice (HeadSpinDsp::directionChoice).getIndex() == 1);

    // mono in, stereo out: the sound goes round (the two channels differ)
    auto m = std::make_unique<HeadSpinProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (m->setBusesLayout (layout));
    m->prepareToPlay (sr, 512);
    Buffer io (2, (int) sr);
    const auto s = sine (1.0, 500.0, 0.5f, 1);
    io.copyFrom (0, 0, s, 0, 0, s.getNumSamples());
    io.clear (1, 0, io.getNumSamples());
    juce::MidiBuffer midi;
    for (int pos = 0; pos < io.getNumSamples(); pos += 512)
    {
        Buffer view (io.getArrayOfWritePointers(), 2, pos, juce::jmin (512, io.getNumSamples() - pos));
        m->processBlock (view, midi);
    }
    double diff = 0.0;
    for (int i = 0; i < io.getNumSamples(); ++i)
        diff = std::max (diff, (double) std::abs (io.getSample (0, i) - io.getSample (1, i)));
    CHECK (diff > 0.05);
    CHECK (allFinite (io));
}

TEST_CASE ("HS 9. RETRIGGER MIDI: a note puts the sound back on its start point", "[hs][retrigger]")
{
    auto p = makeProc (0);
    setParam (p->getChoice (HeadSpinDsp::rateModeChoice), 0.0f);
    setParam (p->getKnob (HeadSpinDsp::rateHzKnob), 0.5f);
    setParam (p->getKnob (HeadSpinDsp::phaseKnob), 90.0f); // the start: on the right
    setParam (p->getChoice (HeadSpinDsp::retriggerChoice), 1.0f);
    p->snapToTargets();
    Buffer in = sine (1.4);
    juce::MidiBuffer none, note;
    note.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    int pos = 0;
    auto run = [&] (int blocks, juce::MidiBuffer& midi)
    {
        for (int k = 0; k < blocks; ++k, pos += 512)
        {
            Buffer view (in.getArrayOfWritePointers(), 2, pos, 512);
            p->processBlock (view, midi);
        }
    };
    run (60, none); // ~0.64 s at 0.5 Hz: about a third of a turn away from the start
    CHECK (std::abs (p->getDsp().getTelemetry().azimuthDeg.load() - 90.0f) > 60.0f);
    run (1, note);
    run (3, none); // the glide (12 ms) is over
    CHECK (p->getDsp().getTelemetry().azimuthDeg.load() == Catch::Approx (90.0f).margin (12.0f));
}

TEST_CASE ("HS 10. CUSTOM: a hand drawing becomes a clean path (clockwise, from the front, constant speed), saved with the session", "[hs][custom]")
{
    // a wobbly rounded square drawn anticlockwise, starting at the back,
    // cutting across the head once (pushed out to r 0.3)
    std::vector<headspin::Point> drawing;
    juce::Random rng (5);
    for (int k = 0; k < 140; ++k)
    {
        const double a = juce::MathConstants<double>::pi - 2.0 * juce::MathConstants<double>::pi * k / 140.0; // anticlockwise seen from above
        const double dent = 1.0 - 0.9 * std::exp (-std::pow ((k - 70) / 6.0, 2.0)); // the hand cuts across the head here
        const double r = dent / std::pow (std::pow (std::abs (std::cos (a)), 4.0) + std::pow (std::abs (std::sin (a)), 4.0), 0.25);
        drawing.push_back ({ r * std::sin (-a) + 0.02 * (rng.nextDouble() - 0.5), r * std::cos (-a) + 0.02 * (rng.nextDouble() - 0.5) });
    }
    const auto pts = headspin::ShapeBank::normaliseDrawing (drawing);
    REQUIRE (pts.size() == 256);
    // clockwise: the signed area is negative (x right, y front)
    double area = 0.0;
    for (size_t k = 0; k < pts.size(); ++k)
        area += pts[k].x * pts[(k + 1) % pts.size()].y - pts[(k + 1) % pts.size()].x * pts[k].y;
    CHECK (area < 0.0);
    // starts in front, never inside the head
    CHECK (std::abs (std::atan2 (pts[0].x, pts[0].y)) < 0.1);
    for (const auto& q : pts)
        CHECK (std::hypot (q.x, q.y) >= 0.29); // (evened out after the clamp)
    // the path at constant speed
    headspin::ShapeBank bank;
    bank.setCustom (pts);
    headspin::ShapeTable t;
    bank.buildMorph (6.0, t);
    constexpr int steps = 2048;
    std::vector<double> len;
    double mean = 0.0;
    for (int k = 0; k < steps; ++k)
    {
        const auto a = t.at ((double) k / steps), b = t.at ((double) (k + 1) / steps);
        len.push_back (std::hypot (b.x - a.x, b.y - a.y));
        mean += len.back() / steps;
    }
    double dev = 0.0;
    for (double l : len)
        dev = std::max (dev, std::abs (l - mean) / mean);
    INFO ("speed deviation " << dev);
    CHECK (dev < 0.02);

    // the processor: SHAPE 6 is it, the session keeps it
    HeadSpinProcessor p;
    p.setCustomShape (drawing);
    setParam (p.getKnob (HeadSpinDsp::shapeKnob), 6.0f);
    CHECK (p.getKnob (HeadSpinDsp::shapeKnob).getCurrentValueAsText() == "Custom");
    juce::MemoryBlock state;
    p.getStateInformation (state);
    HeadSpinProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (q.getCustomShape().size() == p.getCustomShape().size());
    double maxDiff = 0.0;
    for (size_t k = 0; k < p.getCustomShape().size(); ++k)
        maxDiff = std::max (maxDiff, std::hypot (p.getCustomShape()[k].x - q.getCustomShape()[k].x, p.getCustomShape()[k].y - q.getCustomShape()[k].y));
    CHECK (maxDiff < 0.01);
    // and it plays: the sound goes round it
    q.prepareToPlay (sr, 512);
    q.updateShapeNow();
    Buffer in = sine (1.0);
    const auto out = processBlocks (q, in, { 512 });
    CHECK (allFinite (out));
}
