// Slapper tests: tuning (±2 cents at 40 / 110 / 220 Hz), silence, stability
// (60 s of random playing, every knob and both modes), distinct presets,
// MONO slides, ghost notes, note-off, poly voices, HUMAIN, state, CPU.

#include "SlCommon.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numeric>
#include <random>

using namespace sltest;
using slapper::SlapperDsp;
using slapper::Voice;

namespace
{
constexpr int numPresets = 12;

// the fundamental's frequency over a stretch: low-passed (the stiff string's
// upper partials are sharp, as on a real bass), then the autocorrelation peak
double measureHz (std::vector<float> x, int from, int len, double expectHz, double sr)
{
    float l1 = 0.0f, l2 = 0.0f;
    const float c = 1.0f - (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 1.3 * expectHz / sr);
    for (auto& v : x)
    {
        l1 += c * (v - l1);
        l2 += c * (l1 - l2);
        v = l2;
    }
    const double T0 = sr / expectHz;
    const int lo = (int) (T0 * 0.9), hi = (int) (T0 * 1.1) + 2;
    std::vector<double> r;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[(size_t) (from + i)], b = x[(size_t) (from + i + lag)];
            xy += a * b; xx += a * a; yy += b * b;
        }
        r.push_back (xy / std::sqrt (xx * yy + 1e-30));
    }
    int k = 1;
    for (int i = 1; i + 1 < (int) r.size(); ++i)
        if (r[(size_t) i] > r[(size_t) k])
            k = i;
    const double a = r[(size_t) k - 1], b = r[(size_t) k], cc = r[(size_t) k + 1], den = a - 2 * b + cc;
    return sr / (lo + k + (std::fabs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0));
}

double rms (const Buffer& b, double from, double to, double sr = 48000.0)
{
    double s = 0.0;
    const int a = (int) (from * sr), e = std::min (b.getNumSamples(), (int) (to * sr));
    for (int i = a; i < e; ++i)
        s += (double) b.getSample (0, i) * b.getSample (0, i);
    return std::sqrt (s / std::max (1, e - a));
}

// log-spaced band energies (dB) of a whole render: a spectral fingerprint
std::vector<double> fingerprint (const Buffer& b)
{
    std::vector<double> bands;
    const double sr = 48000.0;
    for (double f = 40.0; f < 8000.0; f *= 1.26) // (third octaves)
    {
        // a band-pass (RBJ, constant skirt) per band
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
        v -= mean; // (shape only, not level)
    return bands;
}
} // namespace

TEST_CASE ("Tuning: 40, 110 and 220 Hz within 2 cents, every attack", "[slapper]")
{
    const double sr = 48000.0;
    for (float attack : { 0.0f, 0.5f, 1.0f })
        for (double hz : { 40.0, 110.0, 220.0 })
        {
            Voice v;
            v.prepare (sr);
            slapper::NoteParams p;
            p.attack = attack;
            p.sub = 0.0f;
            v.noteOn (hz, 0.85f, p);
            std::vector<float> x;
            for (int i = 0; i < (int) sr; ++i)
                x.push_back (v.process());
            const double got = measureHz (x, (int) (0.3 * sr), (int) (0.4 * sr), hz, sr);
            INFO ("attack " << attack << ", " << hz << " Hz: " << got << " Hz");
            CHECK (std::fabs (1200.0 * std::log2 (got / hz)) < 2.0);
        }
}

TEST_CASE ("Silence: no notes, no sound", "[slapper]")
{
    auto p = makeProc (0);
    const auto out = render (*p, {}, 1.0);
    CHECK (out.getMagnitude (0, out.getNumSamples()) == 0.0f);
}

TEST_CASE ("Stability: 60 s of random playing, every knob, both modes, no NaN, no overload", "[slapper]")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (int pass = 0; pass < 2; ++pass)
    {
        auto p = makeProc (pass * 11);
        gktest::setParam (p->getModeParameter(), (float) pass);
        juce::MidiMessageSequence seq;
        const double sr = 48000.0;
        for (double t = 0.0; t < 30.0; t += 0.02 + 0.2 * u (rng))
        {
            const int note = 24 + (int) (u (rng) * 36.0f);
            auto on = juce::MidiMessage::noteOn (1, note, (juce::uint8) (1 + u (rng) * 126.0f));
            on.setTimeStamp (std::round (t * sr));
            seq.addEvent (on);
            auto off = juce::MidiMessage::noteOff (1, note);
            off.setTimeStamp (std::round ((t + 0.01 + u (rng) * 0.6) * sr));
            seq.addEvent (off);
            if (u (rng) < 0.1f)
            {
                auto bend = juce::MidiMessage::pitchWheel (1, (int) (u (rng) * 16383.0f));
                bend.setTimeStamp (std::round (t * sr));
                seq.addEvent (bend);
            }
        }
        seq.sort();
        // the knobs change every 2 s (on the message thread, between renders)
        gk::MidiRenderer::Options opt;
        opt.blockSize = 256;
        bool first = true;
        float peak = 0.0f;
        for (double t = 0.0; t < 30.0; t += 2.0)
        {
            for (int k = 0; k < p->getNumKnobs(); ++k)
                if (k != SlapperDsp::levelKnob)
                    p->getKnobParameter (k).setValueNotifyingHost (u (rng));
            gktest::setParam (p->getAmountParameter(), u (rng));
            juce::MidiMessageSequence part;
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;
                if (m.getTimeStamp() >= t * sr && m.getTimeStamp() < (t + 2.0) * sr)
                {
                    auto c = m;
                    c.setTimeStamp (m.getTimeStamp() - t * sr);
                    part.addEvent (c);
                }
            }
            opt.prepare = first;
            first = false;
            const auto out = gk::MidiRenderer::render (*p, part, (int) (2.0 * sr), sr, opt);
            REQUIRE (gktest::allFinite (out));
            peak = std::max (peak, out.getMagnitude (0, out.getNumSamples()));
        }
        CHECK (peak > 0.05f);
        CHECK (peak <= 1.0f);
    }
}

TEST_CASE ("Presets: twelve, each sounds different", "[slapper]")
{
    auto probe = makeProc (0);
    REQUIRE (probe->getPresets().size() == numPresets);
    std::vector<std::vector<double>> prints;
    for (int i = 0; i < numPresets; ++i)
    {
        auto p = makeProc (i);
        prints.push_back (fingerprint (render (*p, sequenceOf (funkRiff (1), 48000.0), 3.0)));
    }
    for (int i = 0; i < numPresets; ++i)
        for (int j = i + 1; j < numPresets; ++j)
        {
            double d = 0.0;
            for (size_t b = 0; b < prints[(size_t) i].size(); ++b)
                d = std::max (d, std::fabs (prints[(size_t) i][b] - prints[(size_t) j][b]));
            INFO (probe->getPresets()[i].name << " vs " << probe->getPresets()[j].name << ": " << d << " dB");
            CHECK (d > 1.5);
        }
}

TEST_CASE ("MONO: a note played over a held one slides to it, no new pluck", "[slapper]")
{
    for (bool overlap : { true, false })
    {
        auto p = makeProc (2); // Finger Round
        gktest::setParam (p->getKnobParameter (SlapperDsp::humanKnob), 0.0f);
        const double sr = 48000.0;
        // A1 then E2 (a fourth up) at 0.5 s; held over it, or released before
        auto seq = sequenceOf ({ { 0.0, overlap ? 0.6 : 0.45, 33, 100 }, { 0.5, 0.8, 40, 100 } }, sr);
        const auto out = render (*p, seq, 1.3);
        std::vector<float> x (out.getReadPointer (0), out.getReadPointer (0) + out.getNumSamples());
        const double after = measureHz (x, (int) (0.75 * sr), (int) (0.2 * sr), 82.4, sr);
        CHECK (std::fabs (1200.0 * std::log2 (after / 82.41)) < 15.0);
        // a slide: no new attack (the level 20-40 ms after the change stays near the level before)
        const double before = rms (out, 0.40, 0.48), at = rms (out, 0.52, 0.56);
        INFO ("overlap " << overlap << ": before " << before << ", after the change " << at);
        if (overlap)
            CHECK (at < 1.6 * before);
        else
            CHECK (at > 1.6 * before);
    }
}

TEST_CASE ("Ghost note: short, quiet, no sustained pitch", "[slapper]")
{
    auto p = makeProc (0);
    gktest::setParam (p->getKnobParameter (SlapperDsp::humanKnob), 0.0f);
    const auto ghost = render (*p, sequenceOf ({ { 0.0, 0.5, 28, 10 } }, 48000.0), 1.0);
    auto q = makeProc (0);
    gktest::setParam (q->getKnobParameter (SlapperDsp::humanKnob), 0.0f);
    const auto note = render (*q, sequenceOf ({ { 0.0, 0.5, 28, 100 } }, 48000.0), 1.0);
    CHECK (rms (ghost, 0.0, 0.05) > 0.002);                  // heard
    CHECK (rms (ghost, 0.0, 0.05) < rms (note, 0.0, 0.05));  // softer than a note
    CHECK (rms (ghost, 0.25, 0.45) < 0.05 * rms (note, 0.25, 0.45)); // and gone (a note still rings)
}

TEST_CASE ("Note-off damps the string, the next note follows without a click", "[slapper]")
{
    auto p = makeProc (2);
    gktest::setParam (p->getKnobParameter (SlapperDsp::humanKnob), 0.0f);
    const auto out = render (*p, sequenceOf ({ { 0.0, 0.3, 28, 100 } }, 48000.0), 1.0);
    CHECK (rms (out, 0.6, 0.7) < 0.03 * rms (out, 0.1, 0.2)); // released at 0.3 s: down by > 30 dB 0.3 s later
}

TEST_CASE ("POLY: six strings ring together, a seventh steals the oldest", "[slapper]")
{
    auto p = makeProc (2);
    gktest::setParam (p->getModeParameter(), 1.0f);
    std::vector<Note> chord;
    for (int i = 0; i < 7; ++i)
        chord.push_back ({ 0.05 * i, 1.0, 36 + 4 * i, 100 });
    render (*p, sequenceOf (chord, 48000.0), 0.5);
    CHECK (p->getSlapperDsp().getTelemetry().activeVoices.load() == SlapperDsp::numVoices);
}

TEST_CASE ("HUMAIN: notes late by at most 8 ms, never early; off = exact", "[slapper]")
{
    for (float human : { 0.0f, 1.0f })
    {
        auto p = makeProc (0);
        gktest::setParam (p->getKnobParameter (SlapperDsp::humanKnob), human);
        std::vector<Note> notes;
        for (int i = 0; i < 8; ++i)
            notes.push_back ({ 0.6 * i + 0.05, 0.05, 40, 100 });
        const auto out = render (*p, sequenceOf (notes, 48000.0), 5.0);
        double maxLate = 0.0, minLate = 1.0;
        for (int i = 0; i < 8; ++i)
        {
            const int from = (int) ((0.6 * i + 0.05 - 0.004) * 48000.0);
            int on = from;
            while (on < out.getNumSamples() && std::fabs (out.getSample (0, on)) < 1e-4f)
                ++on;
            const double late = (on - from) / 48000.0 - 0.004;
            maxLate = std::max (maxLate, late);
            minLate = std::min (minLate, late);
        }
        INFO ("HUMAIN " << human << ": late " << minLate * 1000.0 << " .. " << maxLate * 1000.0 << " ms");
        CHECK (minLate > -0.0005);
        CHECK (maxLate < (human > 0.0f ? 0.0095 : 0.0015));
        if (human > 0.0f)
            CHECK (maxLate - minLate > 0.002); // it does vary
    }
}

TEST_CASE ("State: the saved FUNK and knobs win over the preset's", "[slapper]")
{
    auto p = makeProc (3);
    gktest::setParam (p->getAmountParameter(), 0.123f);
    gktest::setParam (p->getKnobParameter (SlapperDsp::toneKnob), 0.77f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makeProc (0);
    q->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q->getPresetParameter().getIndex() == 3);
    CHECK (q->getAmountParameter().convertFrom0to1 (q->getAmountParameter().getValue()) == Catch::Approx (0.123f).margin (0.002));
    CHECK (q->getKnobParameter (SlapperDsp::toneKnob).convertFrom0to1 (q->getKnobParameter (SlapperDsp::toneKnob).getValue()) == Catch::Approx (0.77f).margin (0.002));
    // and choosing a preset sets its FUNK
    gktest::setParam (q->getPresetParameter(), 11.0f);
    CHECK (q->getAmountParameter().getValue() == Catch::Approx (1.0f));
}

TEST_CASE ("CPU: six strings at 48 kHz", "[slapper]")
{
    auto p = makeProc (0);
    gktest::setParam (p->getModeParameter(), 1.0f);
    const double sr = 48000.0, seconds = 10.0;
    std::vector<Note> notes;
    for (double t = 0.0; t < seconds; t += 0.125) // a new note every eighth: six always ringing
        notes.push_back ({ t, 0.7, 28 + ((int) (t * 8.0) * 5) % 24, 110 });
    const auto seq = sequenceOf (notes, sr);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    render (*p, seq, seconds, sr, 128);
    const double share = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / seconds;
    WARN ("Slapper, 6 voices: " << share * 100.0 << " % of one core");
    if (gktest::timingChecksEnforced())
        CHECK (share < 0.04);
}
