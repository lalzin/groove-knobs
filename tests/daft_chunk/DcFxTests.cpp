// DAFT CHUNK — step 5: the output effects (PUMP on the beats, SWEEP opening
// over the bars, CRUSH, MIX), the 10 presets, and the export with them.

#include "DaftChunkProcessor.h"
#include "DcSignals.h"
#include "Exporter.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace daftchunk;
using gktest::setParam;

extern thread_local bool dcCountAllocations;
extern std::atomic<int> dcAllocations;

namespace
{
using dctest::sr;
using E = DaftChunkEngine;

struct Clock final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

/** A held, bright saw (4 s), sliced in 4 on the grid; the effects neutral; the transport running. */
std::unique_ptr<DaftChunkProcessor> sawKit (Clock& c)
{
    std::vector<float> x ((size_t) (4.0 * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.3f * (float) (2.0 * std::fmod (110.0 * (double) i / sr, 1.0) - 1.0);
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int ch = 0; ch < 2; ++ch)
        b.copyFrom (ch, 0, x.data(), (int) x.size());
    auto p = std::make_unique<DaftChunkProcessor>();
    p->setNonRealtime (true);
    p->prepareToPlay (sr, 512);
    p->setPlayHead (&c);
    setParam (p->getKnobParameter (E::pumpKnob), 0.0f);
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweepOff);
    setParam (p->getChoiceParameter (E::syncChoice), (float) E::free);
    setParam (p->getChoiceParameter (E::playChoice), (float) E::oneShot);
    p->loadAudio (std::move (b), sr, "saw");
    p->waitForAnalysis();
    p->setBpm (120.0);
    p->setFirstBeat (0.0);
    p->setMode (SliceMode::grid);
    p->setDivision (4);
    return p;
}

/** Renders `seconds`, the clock moving; a held note on slice 1 at the start (C#1). */
juce::AudioBuffer<float> run (DaftChunkProcessor& p, Clock& c, double seconds, bool note = true)
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::MidiBuffer m;
        if (note && pos == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
        p.processBlock (b, m);
        c.ppq += n * c.bpm / 60.0 / sr;
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, b, ch, 0, n);
    }
    return out;
}

double highs (const juce::AudioBuffer<float>& b, int from, int len)
{
    double e = 0.0;
    for (int i = from + 1; i < from + len; ++i)
        e += std::pow (b.getSample (0, i) - b.getSample (0, i - 1), 2.0);
    return e;
}
} // namespace

TEST_CASE ("DC F1 the 10 presets: names, settings, a loop in their style at once", "[dc][fx][presets]")
{
    DaftChunkProcessor p;
    const char* names[] { "One More Chop", "Disco Loop", "Vocal Chop", "Stutter House", "Reverse Party",
                          "Pitch Party", "MPC Mode", "Chunky Funk", "Robot Glitch", "Filter Sunrise" };
    REQUIRE (p.getPresets().size() == 10);
    for (int i = 0; i < 10; ++i)
        CHECK (p.getPresets()[i].name == names[i]);

    Clock c;
    auto q = sawKit (c);
    auto chop = [&] { return q->getAmountParameter().convertFrom0to1 (q->getAmountParameter().getValue()); };
    auto knob = [&] (int k) { return q->getKnobParameter (k).convertFrom0to1 (q->getKnobParameter (k).getValue()); };
    auto choice = [&] (int ch) { return q->getChoiceParameter (ch).getIndex(); };
    struct Expect { int preset; float chop; Style style; };
    const Expect ex[] { { 1, 0.1f, Style::discoLoop }, { 2, 0.5f, Style::vocalChop }, { 3, 0.6f, Style::stutterHouse },
                        { 4, 0.5f, Style::reverseParty }, { 5, 0.75f, Style::pitchParty }, { 9, 0.2f, Style::filterTouch } };
    for (const auto& e : ex)
    {
        q->applyPreset (e.preset);
        CHECK (chop() == Catch::Approx (e.chop).margin (0.001));
        CHECK (choice (E::styleChoice) == (int) e.style);
        REQUIRE (q->getPlayingIdea() >= 0);
        CHECK (q->getIdea (q->getPlayingIdea()).pattern.style == e.style); // heard at once
    }
    CHECK (choice (E::sweepChoice) == E::sweep16);                       // Filter Sunrise: 16 bars
    q->applyPreset (6);                                                  // MPC Mode
    CHECK (chop() == Catch::Approx (0.0f).margin (0.001));
    CHECK (choice (E::modeChoice) == E::cut);
    CHECK (q->getSettings().mode == SliceMode::grid);
    CHECK (q->getSettings().division == 16);
    q->applyPreset (7);                                                  // Chunky Funk
    CHECK (knob (E::swingKnob) == Catch::Approx (0.55f).margin (0.01));
    q->applyPreset (8);                                                  // Robot Glitch
    CHECK (chop() == Catch::Approx (1.0f).margin (0.001));
    CHECK (knob (E::crushKnob) > 0.3f);
    q->applyPreset (0);                                                  // One More Chop
    CHECK (choice (E::styleChoice) == (int) Style::filterTouch);
    CHECK (knob (E::pumpKnob) > 0.2f);
}

TEST_CASE ("DC F2 PUMP: ducked on every beat, back by the next", "[dc][fx]")
{
    Clock c;
    auto p = sawKit (c);
    p->setBpm (30.0); // slices of 2 s: the note holds over the beats (the pump follows the host, at 120)
    setParam (p->getKnobParameter (E::pumpKnob), 1.0f);
    const auto out = run (*p, c, 1.6);
    const int beat = (int) (sr * 0.5);
    for (int b = 1; b < 3; ++b)
    {
        const float early = out.getRMSLevel (0, b * beat + 200, 1000), late = out.getRMSLevel (0, b * beat + beat - 2000, 1000);
        INFO ("beat " << b);
        CHECK (early < 0.4f * late);
    }
}

TEST_CASE ("DC F3 SWEEP: the low-pass opens over the bars (IDEA: the loop's own)", "[dc][fx]")
{
    Clock c;
    auto p = sawKit (c);
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweep4);
    const auto out = run (*p, c, 1.9); // (the first slice held: 0.5 s; bar 1 of 4 sweeps from 300 Hz)
    // early in the 4 bars: dark
    const double early = highs (out, 2000, 4000);
    // with the IDEA's own: a FILTER TOUCH loop sweeps over 8 bars, the idea's
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweepOff);
    c.ppq = 0.0;
    const auto open = run (*p, c, 0.4);
    CHECK (early < 0.2 * highs (open, 2000, 4000));
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweepIdea);
    setParam (p->getChoiceParameter (E::styleChoice), (float) Style::filterTouch);
    REQUIRE (p->rollDice() >= 0);
    CHECK (p->getIdea (p->getPlayingIdea()).pattern.filterBars == 8);
    c.ppq = 0.0;
    run (*p, c, 0.2, false);
    CHECK (p->getDsp().getTelemetry().sweep.load() >= 0.0f);
    p->stopIdea();
    run (*p, c, 0.05, false);
    CHECK (p->getDsp().getTelemetry().sweep.load() < 0.0f); // nothing playing: no sweep
}

TEST_CASE ("DC F4 CRUSH, and MIX 0 is the dry slices exactly", "[dc][fx]")
{
    Clock c;
    auto p = sawKit (c);
    c.ppq = 0.0;
    const auto dry = run (*p, c, 0.3);
    setParam (p->getKnobParameter (E::crushKnob), 1.0f);
    run (*p, c, 0.8, false); // (the last note ends)
    c.ppq = 0.0;
    const auto crushed = run (*p, c, 0.3);
    // 6 bits held over 8 samples: runs of equal values
    int runs = 0;
    for (int i = 4000; i < 8000; ++i)
        runs += std::abs (crushed.getSample (0, i) - crushed.getSample (0, i - 1)) < 1.0e-9f ? 1 : 0; // (held)
    CHECK (runs > 3000);
    setParam (p->getKnobParameter (E::mixKnob), 0.0f);
    setParam (p->getKnobParameter (E::pumpKnob), 1.0f);
    c.ppq = 0.0;
    run (*p, c, 0.8, false); // (MIX glides there, and the last note ends)
    c.ppq = 0.0;
    const auto mixed = run (*p, c, 0.3);
    float diff = 0.0f;
    for (int i = 0; i < dry.getNumSamples(); ++i)
        diff = std::max (diff, std::abs (mixed.getSample (0, i) - dry.getSample (0, i)));
    CHECK (diff < 1.0e-5f);
}

TEST_CASE ("DC F5 the effects: no allocation; the WAV export has them", "[dc][fx][rt]")
{
    Clock c;
    auto p = sawKit (c);
    setParam (p->getKnobParameter (E::pumpKnob), 0.6f);
    setParam (p->getKnobParameter (E::crushKnob), 0.4f);
    setParam (p->getKnobParameter (E::mixKnob), 0.7f);
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweep8);
    REQUIRE (p->rollDice() >= 0);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    m.ensureSize (256);
    for (int k = 0; k < 4; ++k)
        p->processBlock (b, m);
    dcCountAllocations = true;
    dcAllocations = 0;
    for (int k = 0; k < 200; ++k)
    {
        b.clear();
        p->processBlock (b, m);
        c.ppq += 512 * 2.0 / sr;
    }
    dcCountAllocations = false;
    CHECK (dcAllocations.load() == 0);

    const int idea = p->getPlayingIdea();
    const auto with = exporter::render (*p, idea, 120.0, sr);
    setParam (p->getKnobParameter (E::crushKnob), 0.0f);
    setParam (p->getKnobParameter (E::pumpKnob), 0.0f);
    setParam (p->getChoiceParameter (E::sweepChoice), (float) E::sweepOff);
    const auto without = exporter::render (*p, idea, 120.0, sr);
    REQUIRE (with.getNumSamples() == without.getNumSamples());
    double d = 0.0;
    for (int i = 0; i < with.getNumSamples(); ++i)
        d += std::abs (with.getSample (0, i) - without.getSample (0, i));
    CHECK (d > 100.0);
}

TEST_CASE ("DC F6 a session keeps its preset and settings (no loop made on load)", "[dc][fx][state]")
{
    juce::MemoryBlock state;
    {
        DaftChunkProcessor p;
        setParam (p.getPresetParameter(), 8.0f); // Robot Glitch
        p.applyPreset (8);
        setParam (p.getKnobParameter (E::crushKnob), 0.2f); // then changed by hand
        p.getStateInformation (state);
    }
    DaftChunkProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.getPresetParameter().getIndex() == 8);
    CHECK (q.getKnobParameter (E::crushKnob).convertFrom0to1 (q.getKnobParameter (E::crushKnob).getValue()) == Catch::Approx (0.2f).margin (0.01));
    CHECK (q.getPlayingIdea() == -1);
}
