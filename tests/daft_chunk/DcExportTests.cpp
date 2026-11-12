// DAFT CHUNK — the export (spec 4 and 9): the WAV is exactly N bars at the
// host's tempo to the sample (24 bits, the right name), the MIDI replays the
// idea (C1 = slice 1, the tempo), the KIT holds every slice and the loop's
// MIDI, and MIDI CAPTURE sends the idea's notes out on their samples (the
// keys played in are not echoed).

#include "DaftChunkProcessor.h"
#include "DcSignals.h"
#include "Exporter.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace daftchunk;
using gktest::setParam;

namespace
{
using dctest::sr;
using E = DaftChunkEngine;

struct Transport final : juce::AudioPlayHead
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

std::unique_ptr<DaftChunkProcessor> withIdea (Transport& t, double hostRate = 48000.0)
{
    const auto x = dctest::drumLoop (120.0, 2, 0, 41u);
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int c = 0; c < 2; ++c)
        b.copyFrom (c, 0, x.data(), (int) x.size());
    auto p = std::make_unique<DaftChunkProcessor>();
    p->setNonRealtime (true);
    p->prepareToPlay (hostRate, 512);
    p->setPlayHead (&t);
    p->loadAudio (std::move (b), sr, "Test Loop");
    p->waitForAnalysis();
    p->setMode (SliceMode::grid);
    p->setDivision (16);
    juce::AudioBuffer<float> blk (2, 512);
    juce::MidiBuffer m;
    p->processBlock (blk, m); // (the host's tempo is known)
    p->stretchNow();
    REQUIRE (p->rollDice() == 0);
    return p;
}

juce::AudioBuffer<float> readWav (const juce::File& f, int& bits, double& rate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
    REQUIRE (r != nullptr);
    bits = (int) r->bitsPerSample;
    rate = r->sampleRate;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}
} // namespace

TEST_CASE ("DC E1 WAV: exactly N bars at the host's tempo, 24 bits, its name", "[dc][export]")
{
    Transport t;
    t.bpm = 126.0;
    auto p = withIdea (t);
    const auto f = exporter::writeWav (*p, 0);
    REQUIRE (f.existsAsFile());
    CHECK (f.getFileName().startsWith ("DaftChunk_Test_Loop_126bpm_loop1"));
    CHECK (f.getParentDirectory() == exporter::folder());
    int bits = 0;
    double rate = 0.0;
    const auto b = readWav (f, bits, rate);
    const int bars = p->getIdea (0).pattern.bars;
    CHECK (bits == 24);
    CHECK (rate == Catch::Approx (48000.0));
    CHECK (b.getNumSamples() == (int) std::llround (bars * 4.0 * 60.0 / 126.0 * 48000.0));
    CHECK (b.getMagnitude (0, 0, b.getNumSamples()) > 0.05f);
    // the same idea, rendered again: the same samples
    const auto again = exporter::render (*p, 0, 126.0, 48000.0);
    float diff = 0.0f;
    for (int i = 0; i < again.getNumSamples(); ++i)
        diff = std::max (diff, std::abs (again.getSample (0, i) - b.getSample (0, i)));
    CHECK (diff < 1.0e-6f + 1.0f / 8388608.0f * 2.0f); // (24-bit rounding)
    f.deleteFile();
    // at another tempo: still exact
    for (double bpm : { 97.0, 133.3 })
        CHECK (exporter::render (*p, 0, bpm, 44100.0).getNumSamples() == (int) std::llround (bars * 4.0 * 60.0 / bpm * 44100.0));
}

TEST_CASE ("DC E2 MIDI: the notes that replay the idea, at its tempo", "[dc][export]")
{
    Transport t;
    t.bpm = 124.0;
    auto p = withIdea (t);
    const auto f = exporter::writeMidi (*p, 0);
    REQUIRE (f.existsAsFile());
    CHECK (f.getFileExtension() == ".mid");
    juce::MidiFile mf;
    juce::FileInputStream in (f);
    REQUIRE (mf.readFrom (in));
    REQUIRE (mf.getNumTracks() == 1);
    const auto& seq = *mf.getTrack (0);
    double tempo = 0.0;
    std::vector<std::pair<double, int>> ons;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (m.isTempoMetaEvent())
            tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
        if (m.isNoteOn())
            ons.push_back ({ m.getTimeStamp() / mf.getTimeFormat(), m.getNoteNumber() });
    }
    CHECK (tempo == Catch::Approx (124.0).epsilon (0.001));
    const auto notes = ideaNotes (p->getIdea (0).pattern, 0.0f);
    REQUIRE (ons.size() == notes.size());
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK (ons[i].first == Catch::Approx (notes[i].start).margin (0.002));
        CHECK (ons[i].second == E::baseNote + notes[i].slice);
    }
    CHECK (ons.front().first == Catch::Approx (0.0));
    f.deleteFile();
}

TEST_CASE ("DC E3 KIT: every slice as a WAV, and the loop's MIDI in the folder", "[dc][export]")
{
    Transport t;
    auto p = withIdea (t);
    const auto files = exporter::writeKit (*p, 0);
    REQUIRE (files.size() == (int) p->getKit()->slices.size());
    CHECK (files[0].getFileName().startsWith ("01_"));
    const auto dir = files[0].getParentDirectory();
    CHECK (dir.getFileName().endsWith ("_KIT"));
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.mid").size() == 1);
    dir.deleteRecursively();
}

TEST_CASE ("DC E4 MIDI CAPTURE: the idea's notes out, on their samples; nothing else", "[dc][export][capture]")
{
    Transport t;
    t.bpm = 120.0;
    auto p = withIdea (t, sr);
    // an idea of one bar: slice 2 on beat 1, slice 5 on beat 3
    auto& idea = const_cast<DaftChunkProcessor::Idea&> (p->getIdea (0));
    Pattern pt;
    pt.bars = 1;
    pt.steps.assign (16, Step {});
    pt.steps[0].slice = 2;
    pt.steps[8].slice = 5;
    idea.pattern = pt;
    p->togglePlay (0);
    p->togglePlay (0);
    auto runFor = [&] (int samples, bool keyIn)
    {
        std::vector<std::tuple<int, int, bool>> out;
        juce::AudioBuffer<float> b (2, 256);
        for (int pos = 0; pos < samples; pos += 256)
        {
            juce::MidiBuffer m;
            if (keyIn && pos == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 10);
            b.clear();
            p->processBlock (b, m);
            for (const auto meta : m)
                if (meta.getMessage().isNoteOnOrOff())
                    out.push_back ({ pos + meta.samplePosition, meta.getMessage().getNoteNumber(), meta.getMessage().isNoteOn() });
            t.ppq += 256 * t.bpm / 60.0 / sr;
        }
        return out;
    };
    t.ppq = 0.0;
    auto off = runFor ((int) sr, true);
    CHECK (off.empty()); // capture off: nothing out (not even the key played in)

    setParam (p->getChoiceParameter (E::captureChoice), 1.0f);
    t.ppq = 0.0;
    auto ev = runFor ((int) (2.2 * sr), false);
    // beat 1 (sample 0): 38 on; beat 3 (1 s): 38 off then 41 on; the next bar (2 s): 41 off, 38 on
    REQUIRE (ev.size() >= 4);
    CHECK (std::get<1> (ev[0]) == 38);
    CHECK (std::get<2> (ev[0]));
    CHECK (std::get<0> (ev[0]) < 4);
    bool on41 = false;
    for (const auto& [at, note, on] : ev)
        if (note == 41 && on)
        {
            on41 = true;
            CHECK (std::abs (at - (int) sr) < 4);
        }
    CHECK (on41);
    // every note-on has its note-off
    int balance = 0;
    for (const auto& e : ev)
        balance += std::get<2> (e) ? 1 : -1;
    CHECK (balance <= 1); // (the last one may still be held at the end)
    // stopping the idea: the held note ends
    p->stopIdea();
    auto after = runFor (512, false);
    int offs = 0;
    for (const auto& e : after)
        offs += std::get<2> (e) ? 0 : 1;
    CHECK (offs == balance);
}
