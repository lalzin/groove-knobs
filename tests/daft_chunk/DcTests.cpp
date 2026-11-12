// DAFT CHUNK - the plug-in (spec 1, 2 and 9): loading (WAV, FLAC, missing,
// longer than 60 s), the slices played from C1 exactly as cut (with their
// fades), no allocation in processBlock, the cuts moved by hand, the session
// saved and restored (settings and hand cuts), the kit swapped while the
// audio runs.

#include "DaftChunkProcessor.h"
#include "DcSignals.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <new>
#include <thread>

using namespace daftchunk;
using gktest::setParam;

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
using dctest::sr;

juce::AudioBuffer<float> toBuffer (const std::vector<float>& x, int channels = 2)
{
    juce::AudioBuffer<float> b (channels, (int) x.size());
    for (int c = 0; c < channels; ++c)
        b.copyFrom (c, 0, x.data(), (int) x.size());
    return b;
}

std::unique_ptr<DaftChunkProcessor> loaded (const std::vector<float>& x, double hostRate = sr)
{
    auto p = std::make_unique<DaftChunkProcessor>();
    p->setNonRealtime (true);
    p->prepareToPlay (hostRate, 512);
    p->loadAudio (toBuffer (x), sr, "loop");
    p->waitForAnalysis();
    return p;
}

juce::File writeWav (const std::vector<float>& x, const juce::String& name, bool flac = false)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name + (flac ? ".flac" : ".wav"));
    f.deleteFile();
    std::unique_ptr<juce::AudioFormat> fmt;
    if (flac)
        fmt = std::make_unique<juce::FlacAudioFormat>();
    else
        fmt = std::make_unique<juce::WavAudioFormat>();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    auto w = fmt->createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    REQUIRE (w != nullptr);
    const auto b = toBuffer (x);
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

/** Renders `seconds` with note-ons at given sample positions. */
juce::AudioBuffer<float> play (DaftChunkProcessor& p, const std::vector<std::pair<int, int>>& notes, int total, int block = 256)
{
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::MidiBuffer midi;
        for (const auto& [at, note] : notes)
            if (at >= pos && at < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), at - pos);
        p.processBlock (b, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, b, c, 0, n);
    }
    return out;
}
} // namespace

TEST_CASE ("DC P1 a loop loaded: analysed, sliced, the tempo right", "[dc][plugin]")
{
    const auto x = dctest::drumLoop (122.0, 4, 0, 1u);
    auto p = loaded (x);
    REQUIRE (p->getSample() != nullptr);
    CHECK_FALSE (p->isAnalysing());
    CHECK (p->getBpm() == Catch::Approx (122.0).margin (0.01));
    CHECK (p->getKit()->slices.size() >= 16);
    CHECK (p->getKit()->slices.size() <= 64);
    CHECK (p->getStatus().isEmpty());
}

TEST_CASE ("DC P2 files: WAV and FLAC read, a missing file says so, longer than 60 s is cut", "[dc][plugin]")
{
    const auto x = dctest::drumLoop (118.0, 4, 1, 2u);
    for (bool flac : { false, true })
    {
        const auto f = writeWav (x, "dc_test_loop", flac);
        DaftChunkProcessor p;
        p.loadFile (f);
        p.waitForAnalysis();
        REQUIRE (p.getSample() != nullptr);
        CHECK (p.getSample()->audio.getNumSamples() == (int) x.size());
        CHECK (p.getSample()->name == "dc_test_loop");
        CHECK (p.getBpm() == Catch::Approx (118.0).margin (0.05));
        f.deleteFile();
    }
    {
        DaftChunkProcessor p;
        p.loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no_such_sample.wav"));
        p.waitForAnalysis();
        CHECK (p.getSample() == nullptr);
        CHECK (p.getStatus().contains ("no_such_sample.wav"));
    }
    {
        const auto f = writeWav (dctest::drumLoop (120.0, 33, 0, 3u), "dc_test_long"); // 66 s
        DaftChunkProcessor p;
        p.loadFile (f);
        p.waitForAnalysis();
        REQUIRE (p.getSample() != nullptr);
        CHECK (p.getSample()->audio.getNumSamples() == (int) (60.0 * sr));
        CHECK (p.getStatus() == juce::String::fromUTF8 ("Coup\xc3\xa9 \xc3\xa0 60 s"));
        f.deleteFile();
    }
}

TEST_CASE ("DC P3 a note plays its slice, exactly as cut (C1 = slice 1)", "[dc][plugin]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 4u);
    auto p = loaded (x);
    const auto kit = p->getKit();
    REQUIRE (kit->slices.size() > 4);
    for (int i : { 0, 3 })
    {
        const auto& s = kit->slices[(size_t) i];
        const auto expected = renderSlice (x.data(), (int64_t) x.size(), s, sr);
        const auto out = play (*p, { { 100, DaftChunkEngine::baseNote + i } }, (int) expected.size() + 400);
        double err = 0.0, ref = 0.0;
        for (size_t k = 0; k + 1 < expected.size(); ++k)
        {
            const float y = out.getSample (0, 100 + (int) k);
            const float e = expected[k] * 1.0f; // velocity 127 = 1
            err = std::max (err, (double) std::abs (y - e));
            ref = std::max (ref, (double) std::abs (e));
        }
        INFO ("slice " << i << " max error " << err << " of " << ref);
        CHECK (err < 0.02 * ref + 1.0e-4);
        // silent after it ends
        CHECK (out.getMagnitude (0, 100 + (int) expected.size() + 10, 200) < 1.0e-6f);
    }
}

TEST_CASE ("DC P4 a new note cuts the last one without a click; host at 48 kHz", "[dc][plugin][click]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 5u);
    auto p = loaded (x, 48000.0);
    const auto out = play (*p, { { 0, 36 }, { 3000, 37 }, { 5000, 36 }, { 5100, 38 } }, 30000);
    // the steepest step of the output: no more than the source's own (resampled)
    float src = 0.0f, mine = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        src = std::max (src, std::abs (x[i] - x[i - 1]));
    for (int i = 1; i < out.getNumSamples(); ++i)
        mine = std::max (mine, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
    CHECK (mine <= src * 1.1f);
    CHECK (out.getMagnitude (0, 0, 30000) > 0.1f);
}

TEST_CASE ("DC P5 no allocation in processBlock", "[dc][plugin][rt]")
{
    const auto x = dctest::drumLoop (124.0, 2, 2, 6u);
    auto p = loaded (x);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    p->getDsp().preview (2);
    countAllocations = true;
    allocations = 0;
    for (int k = 0; k < 200; ++k)
    {
        midi.clear();
        if (k % 7 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + k % 12, (juce::uint8) 100), k % 300);
        b.clear();
        p->processBlock (b, midi);
    }
    countAllocations = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("DC P6 the cuts by hand: move, add, remove; the settings re-slice", "[dc][plugin]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 7u);
    auto p = loaded (x);
    const auto n0 = p->getKit()->slices.size();
    const auto s2 = p->getKit()->slices[2];
    p->moveCut (2, s2.start + (int64_t) (0.05 * sr));
    CHECK (p->hasUserCuts());
    CHECK (std::abs (p->getKit()->slices[2].start - (s2.start + (int64_t) (0.05 * sr))) < (int64_t) (0.006 * sr));
    CHECK (p->getKit()->slices.size() == n0);
    // never past its neighbours
    p->moveCut (2, 0);
    CHECK (p->getKit()->slices[2].start > p->getKit()->slices[1].start);
    p->addCut ((p->getKit()->slices[4].start + p->getKit()->slices[4].end) / 2);
    CHECK (p->getKit()->slices.size() == n0 + 1);
    p->removeCut (1);
    CHECK (p->getKit()->slices.size() == n0);
    p->setMode (SliceMode::grid);
    CHECK_FALSE (p->hasUserCuts());
    p->setDivision (8);
    CHECK (p->getKit()->slices.size() == 16); // 2 bars of 1/8
    p->halveBpm();
    CHECK (p->getBpm() == Catch::Approx (60.0).margin (0.01));
    CHECK (p->getKit()->slices.size() == 8);
    p->setBpm (-1.0);
    CHECK (p->getBpm() == Catch::Approx (120.0).margin (0.01));
}

TEST_CASE ("DC P7 the session: the sample, the settings and the hand cuts come back", "[dc][plugin][state]")
{
    const auto x = dctest::drumLoop (126.0, 2, 1, 8u);
    const auto f = writeWav (x, "dc_test_state");
    juce::MemoryBlock state;
    std::vector<int64_t> starts;
    {
        DaftChunkProcessor p;
        p.loadFile (f);
        p.waitForAnalysis();
        p.setMode (SliceMode::mixed);
        p.setDivision (8);
        p.setSensitivity (0.7f);
        p.moveCut (3, p.getKit()->slices[3].start + 900);
        for (const auto& s : p.getKit()->slices)
            starts.push_back (s.start);
        setParam (p.getAmountParameter(), 0.66f);
        p.getStateInformation (state);
    }
    DaftChunkProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    q.waitForAnalysis();
    REQUIRE (q.getSample() != nullptr);
    CHECK (q.getSettings().mode == SliceMode::mixed);
    CHECK (q.getSettings().division == 8);
    CHECK (q.getSettings().sensitivity == Catch::Approx (0.7f));
    CHECK (q.hasUserCuts());
    REQUIRE (q.getKit()->slices.size() == starts.size());
    for (size_t i = 0; i < starts.size(); ++i)
        CHECK (q.getKit()->slices[i].start == starts[i]);
    CHECK (q.getAmountParameter().convertFrom0to1 (q.getAmountParameter().getValue()) == Catch::Approx (0.66f).margin (0.001));
    f.deleteFile();
}

TEST_CASE ("DC P8 the kit changes while the audio runs (no crash, no stale slice)", "[dc][plugin][rt]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 9u);
    auto p = loaded (x);
    std::atomic<bool> stop { false };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> b (2, 128);
        juce::MidiBuffer midi;
        int k = 0;
        while (! stop.load())
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (k++ % 20), (juce::uint8) 100), 0);
            b.clear();
            p->processBlock (b, midi);
        }
    });
    for (int i = 0; i < 300; ++i)
    {
        const auto& sl = p->getKit()->slices;
        if (i % 3 == 0)
            p->setSensitivity ((float) (i % 10) / 10.0f);
        else if (sl.size() > 3)
            p->moveCut (1 + i % ((int) sl.size() - 2), sl[1].start + (i % 50) * 10);
    }
    stop = true;
    audio.join();
    CHECK (p->getKit() != nullptr);
}

namespace
{
using E = DaftChunkEngine;

/** Renders note-ons / note-offs: {sample, note, on}. */
juce::AudioBuffer<float> playEvents (DaftChunkProcessor& p, const std::vector<std::tuple<int, int, bool>>& events, int total, int block = 256)
{
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::MidiBuffer midi;
        for (const auto& [at, note, on] : events)
            if (at >= pos && at < pos + n)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 127) : juce::MidiMessage::noteOff (1, note), at - pos);
        p.processBlock (b, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, b, c, 0, n);
    }
    return out;
}

float rmsOf (const juce::AudioBuffer<float>& b, int from, int len) { return b.getRMSLevel (0, from, len); }

/** A held tone (a slice that lasts): 1 s of a 220 Hz saw, as one slice per 0.5 s on the grid. */
std::unique_ptr<DaftChunkProcessor> toneKit()
{
    std::vector<float> x ((size_t) (2.0 * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.4f * (float) (2.0 * std::fmod (220.0 * (double) i / sr, 1.0) - 1.0);
    auto p = loaded (x);
    p->setMode (SliceMode::grid);
    p->setBpm (120.0);
    p->setFirstBeat (0.0);
    p->setDivision (4); // 4 slices of 0.5 s
    REQUIRE (p->getKit()->slices.size() == 4);
    return p;
}
} // namespace

TEST_CASE ("DC S1 envelope: GATE releases on note-off, ONE SHOT plays on", "[dc][plugin][sound]")
{
    auto p = toneKit();
    setParam (p->getKnobParameter (E::releaseKnob), 0.05f);
    // GATE: released at 0.1 s -> silent ~0.1 s later
    auto out = playEvents (*p, { { 0, 36, true }, { 4410, 36, false } }, 22050);
    CHECK (rmsOf (out, 1000, 2000) > 0.1f);
    CHECK (rmsOf (out, 4410 + 4410, 2000) < 0.003f);
    // ONE SHOT: the note-off changes nothing
    setParam (p->getChoiceParameter (E::playChoice), (float) E::oneShot);
    out = playEvents (*p, { { 0, 37, true }, { 4410, 37, false } }, 22050);
    CHECK (rmsOf (out, 4410 + 4410, 2000) > 0.1f);
}

TEST_CASE ("DC S2 attack, decay, sustain", "[dc][plugin][sound]")
{
    auto p = toneKit();
    setParam (p->getKnobParameter (E::attackKnob), 0.1f);
    setParam (p->getKnobParameter (E::decayKnob), 0.05f);
    setParam (p->getKnobParameter (E::sustainKnob), 0.25f);
    const auto out = playEvents (*p, { { 0, 36, true } }, 20000);
    const float early = rmsOf (out, 400, 400), peak = rmsOf (out, 4200, 400), held = rmsOf (out, 15000, 1000);
    CHECK (early < 0.2f * peak);               // still rising
    CHECK (held == Catch::Approx (0.25f * peak).epsilon (0.15)); // down to the sustain
}

TEST_CASE ("DC S3 POLY overlaps, COUPE cuts", "[dc][plugin][sound]")
{
    auto p = toneKit();
    setParam (p->getChoiceParameter (E::modeChoice), (float) E::poly);
    auto out = playEvents (*p, { { 0, 36, true }, { 2000, 37, true } }, 8000);
    const float both = rmsOf (out, 5000, 2000);
    setParam (p->getChoiceParameter (E::modeChoice), (float) E::cut);
    out = playEvents (*p, { { 0, 36, true }, { 2000, 37, true } }, 8000);
    const float one = rmsOf (out, 5000, 2000);
    CHECK (both > 1.2f * one);
}

TEST_CASE ("DC S4 a slice's own: reverse, pitch +12, fine start and end, filter", "[dc][plugin][sound]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 13u);
    auto p = loaded (x);
    p->setMode (SliceMode::grid);
    p->setDivision (4);
    const auto s = p->getKit()->slices[1];
    const int len = (int) (s.end - s.start);
    auto lengthOf = [&] (const juce::AudioBuffer<float>& b)
    {
        int last = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > 1.0e-5f)
                last = i;
        return last;
    };
    setParam (p->getChoiceParameter (E::playChoice), (float) E::oneShot);
    const auto plain = playEvents (*p, { { 0, 37, true } }, len + 2000);
    {
        // the lengths on a held tone (a drum slice ends in silence)
        auto t = toneKit();
        setParam (t->getChoiceParameter (E::playChoice), (float) E::oneShot);
        const int tl = (int) (t->getKit()->slices[1].end - t->getKit()->slices[1].start);
        CHECK (std::abs (lengthOf (playEvents (*t, { { 0, 37, true } }, tl + 2000)) - tl) < 200);
        SliceParams tp;
        tp.pitch = 12.0f;
        t->setSliceParams (1, tp);
        CHECK (std::abs (lengthOf (playEvents (*t, { { 0, 37, true } }, tl + 2000)) - tl / 2) < 200);
        tp = {};
        tp.start = 0.25f;
        tp.end = 0.25f;
        t->setSliceParams (1, tp);
        CHECK (std::abs (lengthOf (playEvents (*t, { { 0, 37, true } }, tl + 2000)) - tl / 2) < 200);
    }
    SliceParams sp;
    sp = {};
    sp.reverse = true;
    p->setSliceParams (1, sp);
    const auto rev = playEvents (*p, { { 0, 37, true } }, len + 2000);
    // reversed: the loud attack comes at the end
    CHECK (rmsOf (rev, len - 3000, 2500) > 2.0f * rmsOf (rev, 200, 2500));

    sp = {};
    sp.filter = -6.0f; // 20 kHz -> ~300 Hz
    p->setSliceParams (1, sp);
    const auto dull = playEvents (*p, { { 0, 37, true } }, len + 2000);
    auto treble = [] (const juce::AudioBuffer<float>& b)
    {
        double e = 0.0;
        for (int i = 1; i < b.getNumSamples(); ++i)
            e += std::pow (b.getSample (0, i) - b.getSample (0, i - 1), 2.0);
        return e;
    };
    CHECK (treble (dull) < 0.1 * treble (plain));
    CHECK (p->getSliceParams (0).isDefault());
    p->applyToAllSlices (1);
    CHECK (p->getSliceParams (0).filter == Catch::Approx (-6.0f));
}

TEST_CASE ("DC S5 the session keeps the slices' own settings", "[dc][plugin][state]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 14u);
    const auto f = writeWav (x, "dc_test_params");
    juce::MemoryBlock state;
    {
        DaftChunkProcessor p;
        p.loadFile (f);
        p.waitForAnalysis();
        SliceParams sp;
        sp.pitch = -5.0f;
        sp.gainDb = 3.0f;
        sp.reverse = true;
        sp.end = 0.2f;
        p.setSliceParams (2, sp);
        p.getStateInformation (state);
    }
    DaftChunkProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    q.waitForAnalysis();
    const auto sp = q.getSliceParams (2);
    CHECK (sp.pitch == Catch::Approx (-5.0f));
    CHECK (sp.gainDb == Catch::Approx (3.0f));
    CHECK (sp.reverse);
    CHECK (sp.end == Catch::Approx (0.2f));
    CHECK (q.getSliceParams (1).isDefault());
    f.deleteFile();
}
