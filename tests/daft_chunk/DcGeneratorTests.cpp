// DAFT CHUNK — the idea generator (spec 3 and 9): the same seed gives the same
// loop bit for bit (the pattern and its sound), CHOP 0 is the sample in its own
// order, the musical rules (beat 1, the key), locked slices, ENCORE's 10-25 %,
// the 8 slots and their stars, ANNULER / RÉTABLIR, the session, and an idea
// played locked to the host's bars (with swing), without allocation.

#include "DaftChunkProcessor.h"
#include "DcSignals.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace daftchunk;
using gktest::setParam;

namespace
{
using dctest::sr;
using E = DaftChunkEngine;

std::unique_ptr<DaftChunkProcessor> loopKit (unsigned seed = 21u)
{
    auto x = dctest::drumLoop (120.0, 4, 2, seed);
    // a held tonal line on top, so some slices are tonal
    for (size_t i = 0; i < x.size(); ++i)
        if ((i / (size_t) (sr / 2)) % 4 == 3)
            x[i] += 0.3f * (float) std::sin (dctest::twoPi * 220.0 * (double) i / sr);
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int c = 0; c < 2; ++c)
        b.copyFrom (c, 0, x.data(), (int) x.size());
    auto p = std::make_unique<DaftChunkProcessor>();
    p->setNonRealtime (true);
    p->prepareToPlay (sr, 512);
    p->loadAudio (std::move (b), sr, "loop");
    p->waitForAnalysis();
    p->setMode (SliceMode::grid);
    p->setDivision (16);
    return p;
}

/** A host transport that moves on by each block. */
struct MovingPlayHead final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        p.setPpqPosition (ppq);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

juce::AudioBuffer<float> run (DaftChunkProcessor& p, MovingPlayHead& ph, int total, int block = 256)
{
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        p.processBlock (b, midi);
        ph.ppq += n * ph.bpm / 60.0 / sr;
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, b, c, 0, n);
    }
    return out;
}
} // namespace

TEST_CASE ("DC G1 the same seed: the same loop, bit for bit (pattern and sound)", "[dc][generator]")
{
    auto p = loopKit();
    const auto src = p->sourceInfo();
    for (int st = 0; st < numStyles; ++st)
        for (float chop : { 0.0f, 0.3f, 0.6f, 1.0f })
        {
            const auto a = generate (src, (Style) st, chop, 1234u), b = generate (src, (Style) st, chop, 1234u);
            CHECK (a == b);
            CHECK (generate (src, (Style) st, chop, 1235u).steps.size() > 0);
        }
    // two processors, the same idea: the same audio, sample for sample
    juce::AudioBuffer<float> outs[2];
    for (int i = 0; i < 2; ++i)
    {
        auto q = loopKit();
        MovingPlayHead ph;
        q->setPlayHead (&ph);
        setParam (q->getAmountParameter(), 0.7f);
        q->rollDice();
        // (rollDice picks a random seed: set the same idea in both)
        auto& idea = const_cast<DaftChunkProcessor::Idea&> (q->getIdea (q->getPlayingIdea()));
        idea.pattern = generate (q->sourceInfo(), Style::stutterHouse, 0.7f, 99u);
        q->togglePlay (q->getPlayingIdea());
        q->togglePlay (q->getSelectedIdea());
        outs[i] = run (*q, ph, 44100);
    }
    bool same = true;
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 44100; ++k)
            same = same && std::memcmp (outs[0].getReadPointer (c) + k, outs[1].getReadPointer (c) + k, sizeof (float)) == 0;
    CHECK (same);
    CHECK (outs[0].getMagnitude (0, 0, 44100) > 0.05f);
}

TEST_CASE ("DC G2 CHOP 0: the slices in their own order", "[dc][generator]")
{
    auto p = loopKit();
    const auto src = p->sourceInfo();
    const auto pt = generate (src, Style::discoLoop, 0.0f, 7u);
    REQUIRE (pt.bars == 4);
    for (int k = 0; k < pt.numSteps(); ++k)
    {
        const int own = src.original[(size_t) k];
        if (k == 0 || own >= 0)
            CHECK (pt.steps[(size_t) k].slice == (k == 0 && own < 0 ? pt.steps[0].slice : own));
        CHECK_FALSE (pt.steps[(size_t) k].reverse);
        CHECK (pt.steps[(size_t) k].repeat == 1);
    }
}

TEST_CASE ("DC G3 the rules: beat 1, the key, the bars", "[dc][generator]")
{
    auto p = loopKit();
    const auto src = p->sourceInfo();
    const auto scale = src.key.scale();
    for (uint32_t seed = 1; seed <= 60; ++seed)
        for (int st = 0; st < numStyles; ++st)
        {
            const float chop = (float) (seed % 11) / 10.0f;
            const auto pt = generate (src, (Style) st, chop, seed);
            CHECK ((pt.bars == 1 || pt.bars == 2 || pt.bars == 4));
            CHECK (pt.numSteps() == pt.bars * 16);
            for (int bar = 0; bar < pt.bars; ++bar)
            {
                const auto& s = pt.steps[(size_t) bar * 16];
                // the sample's own beat 1: of any of its bars (the loop may start further in, or take another bar's)
                bool own = false;
                for (size_t b = 0; b < src.original.size(); b += 16)
                    own = own || src.original[b] == s.slice;
                const bool kick = s.slice >= 0 && src.types[(size_t) s.slice] == SliceType::kick;
                INFO ("style " << st << " chop " << chop << " seed " << seed << " bar " << bar);
                CHECK (s.slice >= 0);
                CHECK_FALSE (s.cut);
                // (the vocal chop and the pitch party build on their own slices: their beat 1 is a kick only when there is one)
                if (st != (int) Style::vocalChop && st != (int) Style::pitchParty && st != (int) Style::filterTouch)
                    CHECK ((kick || own || chop < 0.03f));
            }
            for (const auto& s : pt.steps)
                if (s.slice >= 0 && s.pitch != 0 && src.roots[(size_t) s.slice] >= 0.0f
                    && (src.types[(size_t) s.slice] == SliceType::tonal || src.types[(size_t) s.slice] == SliceType::voice))
                {
                    const int note = (int) std::lround (src.roots[(size_t) s.slice]) + s.pitch;
                    CHECK (std::find (scale.begin(), scale.end(), ((note % 12) + 12) % 12) != scale.end());
                }
        }
}

TEST_CASE ("DC G4 locked slices stay where they are, and nowhere else", "[dc][generator]")
{
    auto p = loopKit();
    p->toggleLock (0);
    p->toggleLock (4);
    const auto src = p->sourceInfo();
    for (uint32_t seed = 1; seed <= 40; ++seed)
    {
        const auto pt = generate (src, Style::discoLoop, 1.0f, seed, 4);
        for (int k = 0; k < pt.numSteps(); ++k)
        {
            const int own = src.original[(size_t) k];
            const auto& s = pt.steps[(size_t) k];
            if (own == 0 || own == 4)
                CHECK (s.slice == own); // kept
            else if (s.slice == 0 || s.slice == 4)
                CHECK (false);          // never placed elsewhere
        }
    }
}

TEST_CASE ("DC G5 ENCORE: 4 close variants (10 to 25 % of the steps)", "[dc][generator]")
{
    auto p = loopKit();
    const auto src = p->sourceInfo();
    const auto base = generate (src, Style::stutterHouse, 0.5f, 5u);
    for (uint32_t seed = 1; seed <= 30; ++seed)
    {
        const auto v = mutate (base, src, seed);
        int diff = 0;
        for (int k = 0; k < base.numSteps(); ++k)
            diff += v.steps[(size_t) k] == base.steps[(size_t) k] ? 0 : 1;
        CHECK (diff >= 1);
        CHECK (diff <= (int) std::ceil (0.25 * base.numSteps()));
    }
    setParam (p->getAmountParameter(), 0.5f);
    REQUIRE (p->rollDice() == 0);
    CHECK (p->encore() == 4);
    int used = 0;
    for (int i = 0; i < 8; ++i)
        used += p->getIdea (i).used ? 1 : 0;
    CHECK (used == 5);
}

TEST_CASE ("DC G6 the 8 slots: the oldest without a star goes; all starred: none", "[dc][generator]")
{
    auto p = loopKit();
    for (int i = 0; i < 8; ++i)
        CHECK (p->rollDice() == i);
    p->toggleStar (0);
    CHECK (p->rollDice() == 1); // slot 0 is starred: the next oldest
    for (int i = 0; i < 8; ++i)
        if (! p->getIdea (i).starred)
            p->toggleStar (i);
    CHECK (p->rollDice() == -1);
    CHECK (p->getStatus().isNotEmpty());
    p->deleteIdea (3);
    CHECK (p->rollDice() == 3);
}

TEST_CASE ("DC G7 ANNULER / RETABLIR, and the session keeps the ideas", "[dc][generator][state]")
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 31u);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dc_test_ideas.wav");
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, (int) x.size());
        b.copyFrom (0, 0, x.data(), (int) x.size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    juce::MemoryBlock state;
    Pattern kept;
    {
        DaftChunkProcessor p;
        p.loadFile (f);
        p.waitForAnalysis();
        p.rollDice();
        const auto first = p.getIdea (0).pattern;
        p.rollDice();
        p.deleteIdea (0);
        CHECK_FALSE (p.getIdea (0).used);
        p.undo();
        REQUIRE (p.getIdea (0).used);
        CHECK (p.getIdea (0).pattern == first);
        p.redo();
        CHECK_FALSE (p.getIdea (0).used);
        p.undo();
        Step s;
        s.slice = 2;
        s.pitch = 3;
        s.reverse = true;
        p.setStep (1, 5, s);
        p.toggleStar (1);
        p.toggleLock (3);
        kept = p.getIdea (1).pattern;
        p.getStateInformation (state);
    }
    DaftChunkProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    q.waitForAnalysis();
    REQUIRE (q.getIdea (1).used);
    CHECK (q.getIdea (1).starred);
    CHECK (q.getIdea (1).pattern == kept);
    CHECK (q.getIdea (1).pattern.steps[5].reverse);
    CHECK (q.isLocked (3));
    f.deleteFile();
}

TEST_CASE ("DC G8 an idea plays on the host's bars, swung, without allocation", "[dc][generator][rt]")
{
    auto p = loopKit();
    MovingPlayHead ph;
    p->setPlayHead (&ph);
    setParam (p->getChoiceParameter (E::syncChoice), (float) E::free);
    REQUIRE (p->rollDice() == 0);
    // an idea of one bar: slice 0 on every 8th, nothing else
    auto& idea = const_cast<DaftChunkProcessor::Idea&> (p->getIdea (0));
    Pattern pt;
    pt.bars = 1;
    pt.steps.assign (16, Step {});
    pt.steps[0].slice = 0;
    pt.steps[1].slice = 0; // an odd 16th: it swings
    pt.steps[8].slice = 0;
    idea.pattern = pt;
    p->togglePlay (0);
    p->togglePlay (0); // (off and on: the new pattern)
    REQUIRE (p->getPlayingIdea() == 0);
    setParam (p->getKnobParameter (E::swingKnob), 0.5f);
    setParam (p->getKnobParameter (E::releaseKnob), 0.003f); // GATE: each step lasts its 16th, then silence
    ph.ppq = 0.0;
    const auto out = run (*p, ph, (int) (2.5 * sr));
    // onsets: sound after at least 300 samples of silence
    std::vector<int> onsets;
    int quiet = 1000;
    bool armed = true;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const float a = std::abs (out.getSample (0, i));
        quiet = a < 0.0005f ? quiet + 1 : 0;
        if (quiet >= 300)
            armed = true;
        if (a > 0.01f && armed)
        {
            onsets.push_back (i);
            armed = false;
        }
    }
    const double sixteenth = sr * 0.125; // 120 BPM
    INFO ("peak " << out.getMagnitude (0, 0, out.getNumSamples()) << " playing " << p->getPlayingIdea() << " slices " << p->getKit()->slices.size()
          << " step " << p->getDsp().getTelemetry().step.load() << " host " << p->getHostBpm());
    REQUIRE (onsets.size() >= 4);
    CHECK (std::abs (onsets[0] - 0) < 150);
    CHECK (std::abs (onsets[1] - (sixteenth + 0.5 * 0.125 * sr * 0.5)) < 150); // SWING 50 %: 1/16 of a beat late
    CHECK (std::abs (onsets[2] - 8.0 * sixteenth) < 150);
    CHECK (std::abs (onsets[3] - 16.0 * sixteenth) < 150); // the next bar: again on beat 1

    // no allocation while it plays
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (512);
    p->rollDice();
    for (int k = 0; k < 4; ++k)
        p->processBlock (b, midi);
    int allocs = 0;
    {
        // (counted in DcTests.cpp's operator new: the same binary)
        extern std::atomic<int> dcAllocations;
        extern thread_local bool dcCountAllocations;
        dcCountAllocations = true;
        dcAllocations = 0;
        for (int k = 0; k < 300; ++k)
        {
            b.clear();
            p->processBlock (b, midi);
            ph.ppq += 512 * 2.0 / sr;
        }
        dcCountAllocations = false;
        allocs = dcAllocations.load();
    }
    CHECK (allocs == 0);
}

TEST_CASE ("DC G9 variety: 20 seeds, at least 16 different loops in every style", "[dc][generator]")
{
    auto p = loopKit();
    const auto src = p->sourceInfo();
    for (int st = 0; st < numStyles; ++st)
        for (float chop : { 0.3f, 0.6f, 0.9f })
        {
            std::vector<Pattern> seen;
            for (uint32_t seed = 100; seed < 120; ++seed)
            {
                const auto pt = generate (src, (Style) st, chop, seed);
                bool known = false;
                for (const auto& q : seen)
                    known = known || q.steps == pt.steps;
                if (! known)
                    seen.push_back (pt);
            }
            INFO (styleName ((Style) st) << " chop " << chop);
            CHECK (seen.size() >= 16);
        }
}

TEST_CASE ("DC G10 CHOP live: the idea follows the knob (same seed and style), ANNULER", "[dc][generator]")
{
    auto p = loopKit();
    setParam (p->getAmountParameter(), 0.1f);
    REQUIRE (p->rollDice() == 0);
    const auto before = p->getIdea (0).pattern;
    setParam (p->getAmountParameter(), 0.9f);
    p->applyChop();
    const auto after = p->getIdea (0).pattern;
    CHECK (after.seed == before.seed);
    CHECK (after.style == before.style);
    CHECK (after.bars == before.bars);
    CHECK (after.chop == Catch::Approx (0.9f));
    CHECK (after == generate (p->sourceInfo(), before.style, 0.9f, before.seed, before.bars));
    CHECK_FALSE (after.steps == before.steps);
    // back to where it was: the same loop again
    setParam (p->getAmountParameter(), 0.1f);
    p->applyChop();
    CHECK (p->getIdea (0).pattern.steps == before.steps);
    // ANNULER: one for the whole turn of the knob (both moves came within 1.5 s): back to before it
    p->undo();
    CHECK (p->getIdea (0).pattern == before);
    p->redo();
    CHECK (p->getIdea (0).pattern.chop == Catch::Approx (0.1f));
}

// A dump of the generated patterns for a real file (not run by default):
//   DC_DUMP_FILE=/path/sample.mp3 dc_tests "[.dump]"
TEST_CASE ("DC dump patterns", "[.dump]")
{
    const char* path = std::getenv ("DC_DUMP_FILE");
    if (path == nullptr)
        return;
    DaftChunkProcessor p;
    p.loadFile (juce::File (path));
    p.waitForAnalysis();
    if (const char* mode = std::getenv ("DC_DUMP_GRID"); mode != nullptr)
    {
        p.setMode (SliceMode::grid);
        p.setDivision (juce::String (mode).getIntValue());
    }
    const auto src = p.sourceInfo();
    std::string types;
    for (size_t i = 0; i < src.types.size(); ++i)
        types += std::to_string (i + 1) + typeName (src.types[i])[0] + " ";
    std::printf ("%s  %d slices, %d bars, key %s\n%s\n", path, (int) src.types.size(), src.bars, src.key.name().toRawUTF8(), types.c_str());
    for (int st = 0; st < numStyles; ++st)
        for (float chop : { 0.2f, 0.5f, 0.8f })
            for (uint32_t seed : { 11u, 12u })
            {
                const auto pt = generate (src, (Style) st, chop, seed);
                std::string line;
                for (int k = 0; k < pt.numSteps(); ++k)
                {
                    const auto& s = pt.steps[(size_t) k];
                    if (k % 16 == 0)
                        line += "| ";
                    line += s.cut ? "X" : s.slice < 0 ? "." : std::to_string (s.slice + 1);
                    if (s.repeat > 1)
                        line += "*";
                    if (s.reverse)
                        line += "r";
                    if (s.pitch != 0)
                        line += (s.pitch > 0 ? "+" : "") + std::to_string (s.pitch);
                    line += " ";
                }
                std::printf ("%-14s %.1f #%u: %s\n", styleName ((Style) st), chop, seed, line.c_str());
            }
}
