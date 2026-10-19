// Pew Pew tests: silence, flat at 0 %, distinct presets, glide, polyphony and
// stealing, retrigger, sample accuracy, velocity, note-off, delay sync,
// reverb tail, block sizes, stability, state, all-notes-off, CPU.

#include "PewPewProcessor.h"
#include "PewPatterns.h"
#include "GkTestUtils.h"
#include <catch2/catch_approx.hpp>

using namespace gktest;
using pp::PewPewProcessor;

namespace
{
constexpr int numPresets = 5;

struct Hit
{
    int sample, note;
    int velocity = 127;
    int offAfter = -1; // note-off this many samples later (-1: none)
};

std::unique_ptr<PewPewProcessor> makeProc (int preset, float amount, float room = 0.0f, float echo = 0.0f, int div = 1)
{
    auto p = std::make_unique<PewPewProcessor>();
    setParam (p->getPresetParameter(), (float) preset);
    setParam (p->getAmountParameter(), amount);
    setParam (p->getRoomParameter(), room);
    setParam (p->getEchoParameter(), echo);
    setParam (p->getDivisionParameter(), (float) div);
    return p;
}

juce::MidiMessageSequence sequenceOf (const std::vector<Hit>& hits)
{
    juce::MidiMessageSequence seq;
    for (const auto& h : hits)
    {
        auto on = juce::MidiMessage::noteOn (1, h.note, (juce::uint8) h.velocity);
        on.setTimeStamp (h.sample);
        seq.addEvent (on);
        if (h.offAfter >= 0)
        {
            auto off = juce::MidiMessage::noteOff (1, h.note);
            off.setTimeStamp (h.sample + h.offAfter);
            seq.addEvent (off);
        }
    }
    seq.sort();
    return seq;
}

Buffer render (PewPewProcessor& p, const std::vector<Hit>& hits, int length, double sr = 48000.0, int block = 512, double bpm = 120.0)
{
    gk::MidiRenderer::Options opt;
    opt.blockSize = block;
    opt.bpm = bpm;
    return gk::MidiRenderer::render (p, sequenceOf (hits), length, sr, opt);
}

/** Instantaneous frequency from interpolated rising zero crossings:
    (time of the period centre, frequency). */
std::vector<std::pair<double, double>> zeroCrossingFrequency (const float* x, int from, int to, double sr)
{
    std::vector<double> crossings;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            crossings.push_back ((i - 1) + (double) x[i - 1] / (double) (x[i - 1] - x[i]));
    std::vector<std::pair<double, double>> f;
    for (size_t k = 1; k < crossings.size(); ++k)
        f.push_back ({ 0.5 * (crossings[k] + crossings[k - 1]) / sr, sr / (crossings[k] - crossings[k - 1]) });
    return f;
}

double rms (const Buffer& b, int ch, int from, int to)
{
    double e = 0.0;
    for (int i = from; i < to; ++i)
        e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return std::sqrt (e / juce::jmax (1, to - from));
}

/** Largest sample-to-sample jump in [from, to). */
float maxJump (const float* x, int from, int to)
{
    float m = 0.0f;
    for (int i = juce::jmax (1, from); i < to; ++i)
        m = juce::jmax (m, std::abs (x[i] - x[i - 1]));
    return m;
}

/** Spectral envelope: log energy in 20 log-spaced bands (60 Hz – 12 kHz) for
    12 frames of 50 ms. */
std::vector<double> spectralEnvelope (const float* x, double sr)
{
    constexpr int order = 11, size = 1 << order, bands = 20, frames = 12;
    juce::dsp::FFT fft (order);
    std::vector<double> feature;
    std::vector<float> buf ((size_t) (2 * size));
    const int hop = (int) (0.05 * sr);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / size);
            buf[(size_t) i] = x[f * hop + i] * (float) w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int b = 0; b < bands; ++b)
        {
            const double lo = 60.0 * std::pow (200.0, (double) b / bands), hi = 60.0 * std::pow (200.0, (double) (b + 1) / bands);
            double e = 1.0e-12;
            for (int k = (int) (lo * size / sr); k <= (int) (hi * size / sr) && k < size / 2; ++k)
                e += (double) buf[(size_t) k] * buf[(size_t) k];
            feature.push_back (10.0 * std::log10 (e));
        }
    }
    // Only the 60 dB under the loudest cell count: the silence above the
    // toms (common to all of them) must not make them look alike
    const double top = *std::max_element (feature.begin(), feature.end());
    for (auto& v : feature)
        v = juce::jmax (v, top - 60.0);
    return feature;
}

/** Similarity of two spectral envelopes: correlation of the dB values (1 = same). */
double similarity (const std::vector<double>& a, const std::vector<double>& b)
{
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        ma += a[i];
        mb += b[i];
    }
    ma /= (double) a.size();
    mb /= (double) b.size();
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt (saa * sbb + 1.0e-30);
}
} // namespace

//==============================================================================
TEST_CASE ("PP 1. Silence: no MIDI, the output is exactly 0", "[pp][silence]")
{
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto p = makeProc (preset, 1.0f, 1.0f, 1.0f);
        const auto out = render (*p, {}, (int) (5.0 * 48000.0));
        bool allZero = true;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                allZero = allZero && juce::exactlyEqual (out.getSample (ch, i), 0.0f);
        CHECK (allZero);
    }
}

TEST_CASE ("PP 2. Flat at 0 %: constant pitch, no click", "[pp][flat]")
{
    const double sr = 48000.0;
    for (int preset : { 0, 4 }) // the sine presets (a clean pitch reading)
    {
        auto p = makeProc (preset, 0.0f);
        const auto out = render (*p, { { 0, 60 } }, (int) (0.3 * sr), sr);
        const auto f = zeroCrossingFrequency (out.getReadPointer (0), 0, out.getNumSamples(), sr);
        REQUIRE (f.size() > 5);
        double lo = 1.0e9, hi = 0.0;
        for (const auto& [t, hz] : f)
            if (t > 0.005 && t < 0.25)
            {
                lo = juce::jmin (lo, hz);
                hi = juce::jmax (hi, hz);
            }
        INFO ("preset " << preset << ": " << lo << " .. " << hi << " Hz");
        CHECK ((hi - lo) / lo < 0.01);
        const auto& pd = p->getPewDsp().getPresetData (preset);
        CHECK (lo == Catch::Approx (pp::PewVoice::baseFrequency (pd, 60)).epsilon (0.01));
    }
    // no click at 0 %: identical to the same preset without any click
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto pm = PewPewProcessor::loadBuiltInPresets();
        gk::PresetManager silentClick;
        for (int i = 0; i < pm.size(); ++i)
        {
            auto json = pm[i].json;
            json.getDynamicObject()->setProperty ("clickDb", -80.0);
            silentClick.add (juce::JSON::toString (json));
        }
        auto withClick = makeProc (preset, 0.0f);
        PewPewProcessor noClick (silentClick);
        setParam (noClick.getPresetParameter(), (float) preset);
        setParam (noClick.getAmountParameter(), 0.0f);
        const auto a = render (*withClick, { { 0, 72 } }, (int) (0.05 * sr), sr), b = render (noClick, { { 0, 72 } }, (int) (0.05 * sr), sr);
        bool same = true;
        for (int i = 0; i < a.getNumSamples(); ++i)
            same = same && juce::exactlyEqual (a.getSample (0, i), b.getSample (0, i));
        CHECK (same);
        // and at 100 % the click is there
        auto loud = makeProc (preset, 1.0f);
        PewPewProcessor loudNoClick (silentClick);
        setParam (loudNoClick.getPresetParameter(), (float) preset);
        setParam (loudNoClick.getAmountParameter(), 1.0f);
        const auto c = render (*loud, { { 0, 72 } }, (int) (0.05 * sr), sr), d = render (loudNoClick, { { 0, 72 } }, (int) (0.05 * sr), sr);
        double diff = 0.0;
        for (int i = 0; i < (int) (0.004 * sr); ++i)
            diff = juce::jmax (diff, (double) std::abs (c.getSample (0, i) - d.getSample (0, i)));
        INFO ("preset " << preset << ": click at 100 % peaks at " << diff);
        CHECK (diff > 1.0e-4);
    }
}

TEST_CASE ("PP 3. Presets: five distinct spectral envelopes, none close to Laser Pew", "[pp][presets]")
{
    // Distance = 1 - correlation of the spectral envelopes (20 bands x 12
    // frames of 50 ms, dB). Minimum 0.2: "not alike at 80 %" (DECISIONS.md).
    const double sr = 48000.0;
    std::vector<std::vector<double>> env;
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto p = makeProc (preset, 1.0f);
        const auto out = render (*p, { { 0, 60 } }, (int) (0.7 * sr), sr);
        env.push_back (spectralEnvelope (out.getReadPointer (0), sr));
    }
    double minDistance = 1.0;
    for (int a = 0; a < numPresets; ++a)
        for (int b = a + 1; b < numPresets; ++b)
        {
            const double d = 1.0 - similarity (env[(size_t) a], env[(size_t) b]);
            WARN ("distance " << a << "-" << b << ": " << d);
            minDistance = juce::jmin (minDistance, d);
            CHECK (d > 0.2);
        }
    WARN ("smallest distance between presets: " << minDistance);
}

TEST_CASE ("PP 4. Glide at 100 %: from fStart to fEnd, exponential (pitchTauMs) or steady (glideMs)", "[pp][glide]")
{
    const double sr = 48000.0;
    for (int preset : { 0, 1, 4 }) // sine presets: exponential (Disco Tom), steady (Laser Pew, Deep Drop)
    {
        auto p = makeProc (preset, 1.0f);
        const auto& pd = p->getPewDsp().getPresetData (preset);
        const double fBase = pp::PewVoice::baseFrequency (pd, 60);
        const double fStart = fBase * std::pow (2.0, pd.sweepOct), fEnd = fBase * std::pow (2.0, pd.endOct);
        const auto out = render (*p, { { 0, 60 } }, (int) (0.4 * sr), sr);
        const auto f = zeroCrossingFrequency (out.getReadPointer (0), (int) (0.006 * sr), out.getNumSamples(), sr);
        double worst = 0.0;
        int checked = 0;
        for (const auto& [t, hz] : f)
        {
            if (t > (pd.linearGlide ? 0.9 * pd.glideMs * 0.001 : 0.3))
                break;
            const double expected = pd.linearGlide ? fStart * std::pow (fEnd / fStart, t / (pd.glideMs * 0.001))
                                                   : fEnd + (fStart - fEnd) * std::exp (-t / (pd.pitchTauMs * 0.001));
            worst = juce::jmax (worst, std::abs (hz - expected) / expected);
            ++checked;
        }
        INFO ("preset " << preset << ": " << checked << " periods, worst error " << worst * 100.0 << " %");
        CHECK (checked > 10);
        CHECK (worst < 0.05);
    }
}

TEST_CASE ("PP 5. Polyphony: 8 voices clean, the 9th steals the oldest without a click", "[pp][poly]")
{
    const double sr = 48000.0;
    // amount 0: no noise click, every voice deterministic
    std::vector<Hit> eight;
    for (int k = 0; k < 8; ++k)
        eight.push_back ({ k * 480, 48 + 3 * k });
    const int stealAt = 8 * 480 + 1000;
    auto withSteal = eight;
    withSteal.push_back ({ stealAt, 72 });
    auto withoutOldest = std::vector<Hit> (eight.begin() + 1, eight.end());
    withoutOldest.push_back ({ stealAt, 72 });

    auto a = makeProc (0, 0.0f), b = makeProc (0, 0.0f);
    const auto outA = render (*a, withSteal, (int) (0.5 * sr), sr);
    const auto outB = render (*b, withoutOldest, (int) (0.5 * sr), sr);
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < outA.getNumSamples(); ++i)
    {
        finite = finite && std::isfinite (outA.getSample (0, i));
        peak = juce::jmax (peak, std::abs (outA.getSample (0, i)));
    }
    CHECK (finite);
    CHECK (peak < 1.0f);
    // A - B = the stolen voice alone, faded out over 2 ms: no jump bigger than its own slope
    std::vector<float> stolen ((size_t) outA.getNumSamples());
    for (int i = 0; i < outA.getNumSamples(); ++i)
        stolen[(size_t) i] = outA.getSample (0, i) - outB.getSample (0, i);
    const float before = maxJump (stolen.data(), stealAt - 480, stealAt);
    const float during = maxJump (stolen.data(), stealAt, stealAt + (int) (0.004 * sr));
    float after = 0.0f;
    for (int i = stealAt + (int) (0.0025 * sr); i < outA.getNumSamples(); ++i)
        after = juce::jmax (after, std::abs (stolen[(size_t) i]));
    INFO ("stolen voice: max step before " << before << ", during the fade " << during << ", level after " << after);
    // a 2 ms fade adds a little slope; a click would be a step of the order
    // of the voice's level
    float level = 0.0f;
    for (int i = stealAt - 480; i < stealAt; ++i)
        level = juce::jmax (level, std::abs (stolen[(size_t) i]));
    CHECK (during <= 1.5f * before);
    CHECK (during < 0.1f * level);
    CHECK (after < 1.0e-6f);
}

TEST_CASE ("PP 6. Retrigger: the same note again very fast, no click", "[pp][retrigger]")
{
    const double sr = 48000.0;
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto both = makeProc (preset, 0.0f), first = makeProc (preset, 0.0f), second = makeProc (preset, 0.0f);
        const int again = (int) (0.012 * sr);
        const auto ob = render (*both, { { 0, 60 }, { again, 60 } }, (int) (0.1 * sr), sr);
        const auto o1 = render (*first, { { 0, 60 } }, (int) (0.1 * sr), sr);
        const auto o2 = render (*second, { { again, 60 } }, (int) (0.1 * sr), sr);
        // a new voice: the sum of the two hits, the first one is not cut
        double err = 0.0;
        for (int i = 0; i < ob.getNumSamples(); ++i)
            err = juce::jmax (err, (double) std::abs (ob.getSample (0, i) - o1.getSample (0, i) - o2.getSample (0, i)));
        INFO ("preset " << preset << ": max deviation from the sum " << err);
        CHECK (err < 1.0e-5);
        const float jump = maxJump (ob.getReadPointer (0), again - 48, again + 96);
        CHECK (jump <= maxJump (o1.getReadPointer (0), again - 48, again + 96) + maxJump (o2.getReadPointer (0), again - 48, again + 96) + 1.0e-6f);
    }
}

TEST_CASE ("PP 7. Sample accurate: a note at offset k starts exactly at k", "[pp][timing]")
{
    for (int block : { 64, 512, 1000 })
        for (int k : { 0, 1, 137, 511, 999, 1500 })
            for (int preset = 0; preset < numPresets; ++preset)
            {
                auto p = makeProc (preset, 0.0f);
                const auto out = render (*p, { { k, 60 } }, 3000, 48000.0, block);
                int first = -1;
                for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
                    if (! juce::exactlyEqual (out.getSample (0, i), 0.0f))
                        first = i;
                INFO ("block " << block << ", k " << k << ", preset " << preset);
                CHECK (first == k);
            }
}

TEST_CASE ("PP 8. Velocity: louder with velocity, following velCurve", "[pp][velocity]")
{
    const double sr = 48000.0;
    for (int preset = 0; preset < numPresets; ++preset)
    {
        double previous = 0.0, full = 0.0;
        std::vector<double> levels;
        for (int vel : { 16, 32, 64, 96, 127 })
        {
            auto p = makeProc (preset, 0.6f);
            const auto out = render (*p, { { 0, 60, vel } }, (int) (0.3 * sr), sr);
            const double r = rms (out, 0, 0, out.getNumSamples());
            CHECK (r > previous);
            previous = r;
            levels.push_back (r);
            if (vel == 127)
                full = r;
        }
        const auto ref = makeProc (preset, 0.6f);
        const auto& pd = ref->getPewDsp().getPresetData (preset);
        const double expected = std::pow (64.0 / 127.0, (double) pd.velCurve);
        INFO ("preset " << preset << ": level at 64 / level at 127 = " << levels[2] / full << ", expected " << expected);
        CHECK (levels[2] / full == Catch::Approx (expected).epsilon (0.02));
    }
}

TEST_CASE ("PP 9. Note-off is ignored: one-shot", "[pp][noteoff]")
{
    const double sr = 48000.0;
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto a = makeProc (preset, 1.0f, 0.3f, 0.3f), b = makeProc (preset, 1.0f, 0.3f, 0.3f);
        const auto oa = render (*a, { { 100, 60, 127, 50 } }, (int) (0.8 * sr), sr);
        const auto ob = render (*b, { { 100, 60, 127, -1 } }, (int) (0.8 * sr), sr);
        bool same = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < oa.getNumSamples(); ++i)
                same = same && juce::exactlyEqual (oa.getSample (ch, i), ob.getSample (ch, i));
        CHECK (same);
    }
}

TEST_CASE ("PP 10. Echo locked to the tempo: 1/8 = 250 ms, dotted 1/8 = 375 ms at 120 BPM", "[pp][delay]")
{
    const double sr = 48000.0;
    for (const auto& [div, ms] : std::vector<std::pair<int, double>> { { 2, 250.0 }, { 1, 375.0 }, { 0, 125.0 }, { 3, 500.0 } })
    {
        auto dryP = makeProc (1, 1.0f, 0.0f, 0.0f, div), wetP = makeProc (1, 1.0f, 0.0f, 1.0f, div);
        const int at = 1000;
        const auto od = render (*dryP, { { at, 60 } }, (int) (0.8 * sr), sr), ow = render (*wetP, { { at, 60 } }, (int) (0.8 * sr), sr);
        int first = -1;
        for (int i = 0; i < od.getNumSamples() && first < 0; ++i)
            if (std::abs (ow.getSample (0, i) - od.getSample (0, i)) > 1.0e-7f)
                first = i;
        const double measured = (first - at) * 1000.0 / sr;
        INFO ("division " << div << ": first echo after " << measured << " ms");
        CHECK (std::abs (measured - ms) <= 1.0);
    }
}

TEST_CASE ("PP 11. Reverb tail: gone (-60 dB) within 3 s after the hit ends", "[pp][reverb]")
{
    const double sr = 48000.0;
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto dryP = makeProc (preset, 1.0f, 0.0f), wetP = makeProc (preset, 1.0f, 1.0f);
        const auto od = render (*dryP, { { 0, 60 } }, (int) (10.0 * sr), sr), ow = render (*wetP, { { 0, 60 } }, (int) (10.0 * sr), sr);
        float dryPeak = 0.0f, revPeak = 0.0f;
        for (int i = 0; i < od.getNumSamples(); ++i)
        {
            dryPeak = juce::jmax (dryPeak, std::abs (od.getSample (0, i)));
            revPeak = juce::jmax (revPeak, std::abs (ow.getSample (0, i) - od.getSample (0, i)));
        }
        int dryEnd = 0, revEnd = 0;
        for (int i = 0; i < od.getNumSamples(); ++i)
        {
            if (std::abs (od.getSample (0, i)) > 1.0e-3f * dryPeak)
                dryEnd = i;
            if (std::abs (ow.getSample (0, i) - od.getSample (0, i)) > 1.0e-3f * revPeak)
                revEnd = i;
        }
        const double tail = (revEnd - dryEnd) / sr;
        INFO ("preset " << preset << ": hit ends at " << dryEnd / sr << " s, reverb tail " << tail << " s after");
        CHECK (tail <= 3.0);
    }
}

TEST_CASE ("PP 12. Block sizes: identical output for any block partition", "[pp][blocks]")
{
    const double sr = 48000.0;
    const auto events = pp::beatsToSamples (pp::testPatternBeats(), 124.0, sr);
    const int length = (int) (4.0 * sr);
    auto renderWith = [&] (const std::vector<int>& sizes)
    {
        auto p = makeProc (3, 0.8f, 0.5f, 0.5f);
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p->setBusesLayout (layout);
        p->setNonRealtime (true);
        p->prepareToPlay (sr, 4096);
        gk::OfflinePlayHead ph;
        ph.bpm = 124.0;
        ph.sampleRate = sr;
        p->setPlayHead (&ph);
        Buffer out (2, length);
        out.clear();
        juce::MidiBuffer midi;
        int next = 0;
        size_t k = 0;
        for (int pos = 0; pos < length;)
        {
            const int n = juce::jmin (sizes[k++ % sizes.size()], length - pos);
            midi.clear();
            while (next < events.getNumEvents())
            {
                const auto& m = events.getEventPointer (next)->message;
                const int at = (int) m.getTimeStamp();
                if (at >= pos + n)
                    break;
                midi.addEvent (m, at - pos);
                ++next;
            }
            ph.samplePosition = pos;
            Buffer view (out.getArrayOfWritePointers(), 2, pos, n);
            p->processBlock (view, midi);
            pos += n;
        }
        p->setPlayHead (nullptr);
        return out;
    };
    const auto ref = renderWith ({ 512 });
    juce::Random rng (11);
    std::vector<int> randomSizes;
    for (int i = 0; i < 3000; ++i)
        randomSizes.push_back (1 + rng.nextInt (1500));
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 4096 }, randomSizes })
    {
        const auto out = renderWith (sizes);
        double err = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i)
                err = juce::jmax (err, (double) std::abs (out.getSample (ch, i) - ref.getSample (ch, i)));
        INFO ("block " << sizes.front() << (sizes.size() > 1 ? " (random)" : "") << ": max difference " << err);
        CHECK (err < 1.0e-6);
    }
}

TEST_CASE ("PP 13. Stability: no NaN / Inf, peak below 0 dBFS", "[pp][stability]")
{
    const auto beats = pp::testPatternBeats();
    for (double sr : { 44100.0, 96000.0, 192000.0 })
        for (int preset = 0; preset < numPresets; ++preset)
            for (float amount : { 0.0f, 0.5f, 1.0f })
                for (float fx : { 0.0f, 1.0f })
                {
                    if (sr > 100000.0 && (amount != 1.0f || fx != 1.0f))
                        continue;
                    auto p = makeProc (preset, amount, fx, fx);
                    gk::MidiRenderer::Options opt;
                    opt.bpm = 124.0;
                    const auto out = gk::MidiRenderer::render (*p, pp::beatsToSamples (beats, 124.0, sr), (int) (8.0 * sr), sr, opt);
                    float peak = 0.0f;
                    bool finite = true;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < out.getNumSamples(); ++i)
                        {
                            finite = finite && std::isfinite (out.getSample (ch, i));
                            peak = juce::jmax (peak, std::abs (out.getSample (ch, i)));
                        }
                    INFO ("sr " << sr << ", preset " << preset << ", amount " << amount << ", fx " << fx << ": peak " << peak);
                    CHECK (finite);
                    CHECK (peak < 1.0f);
                }
}

TEST_CASE ("PP 14. State round trip; presets keep ROOM / ECHO / division", "[pp][state]")
{
    PewPewProcessor a;
    setParam (a.getAmountParameter(), 0.33f);
    setParam (a.getPresetParameter(), 3.0f);
    setParam (a.getRoomParameter(), 0.4f);
    setParam (a.getEchoParameter(), 0.7f);
    setParam (a.getDivisionParameter(), 3.0f);
    setParam (a.getBypassParameterObject(), 1.0f);
    a.editorScale.store (1.5f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    PewPewProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getAmountParameter().getValue() == Catch::Approx (0.33f).margin (1.0e-6));
    CHECK (b.getPresetParameter().getIndex() == 3);
    CHECK (b.getRoomParameter().getValue() == Catch::Approx (0.4f).margin (1.0e-6));
    CHECK (b.getEchoParameter().getValue() == Catch::Approx (0.7f).margin (1.0e-6));
    CHECK (b.getDivisionParameter().getIndex() == 3);
    CHECK (b.getBypassParameterObject().get());
    CHECK (b.editorScale.load() == Catch::Approx (1.5f));

    PewPewProcessor c;
    CHECK (c.getAmountParameter().getValue() == Catch::Approx (0.6f));
    CHECK (c.getDivisionParameter().getCurrentChoiceName() == "1/8 D");
    CHECK (c.acceptsMidi());
    CHECK (c.getLatencySamples() == 0);
    CHECK (c.getTailLengthSeconds() == Catch::Approx (3.0));
    setParam (c.getRoomParameter(), 0.5f);
    setParam (c.getEchoParameter(), 0.25f);
    c.setCurrentProgram (2);
    CHECK (c.getRoomParameter().getValue() == Catch::Approx (0.5f)); // a preset does not touch the user's sends
    CHECK (c.getEchoParameter().getValue() == Catch::Approx (0.25f));
    const char* ids[] { "amount", "preset", "reverb", "delay", "delayDiv", "bypass" };
    for (int i = 0; i < 6; ++i)
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (c.getParameters()[i])->getParameterID() == ids[i]);
    // instrument bus: no input, stereo or mono output
    juce::AudioProcessor::BusesLayout stereo, mono, withInput;
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    withInput.inputBuses.add (juce::AudioChannelSet::stereo());
    withInput.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (c.checkBusesLayoutSupported (stereo));
    CHECK (c.checkBusesLayoutSupported (mono));
    CHECK_FALSE (c.checkBusesLayoutSupported (withInput));
}

TEST_CASE ("PP 15. All notes off / reset: clean 5 ms cut", "[pp][panic]")
{
    const double sr = 48000.0;
    auto p = makeProc (3, 1.0f, 1.0f, 1.0f);
    std::vector<Hit> hits;
    for (int k = 0; k < 6; ++k)
        hits.push_back ({ k * 2000, 50 + 4 * k });
    auto seq = sequenceOf (hits);
    const int cutAt = 20000;
    auto off = juce::MidiMessage::allNotesOff (1);
    off.setTimeStamp (cutAt);
    seq.addEvent (off);
    seq.sort();
    gk::MidiRenderer::Options opt;
    const auto out = gk::MidiRenderer::render (*p, seq, (int) (0.6 * sr), sr, opt);
    // the voices are gone 5 ms later (only the effects' tails remain), no jump at the cut
    auto ref = makeProc (3, 1.0f, 1.0f, 1.0f);
    const auto withoutCut = gk::MidiRenderer::render (*ref, sequenceOf (hits), (int) (0.6 * sr), sr, opt);
    const float jumpCut = maxJump (out.getReadPointer (0), cutAt, cutAt + (int) (0.006 * sr));
    const float jumpRef = maxJump (withoutCut.getReadPointer (0), cutAt, cutAt + (int) (0.006 * sr));
    INFO ("max step during the cut " << jumpCut << " (without the cut " << jumpRef << ")");
    CHECK (jumpCut <= 1.1f * jumpRef + 1.0e-4f);
    CHECK (p->getPewDsp().getTelemetry().activeVoices.load() == 0);

    // reset with sound ringing: faded, then silence
    p->reset();
    gk::MidiRenderer::Options keep;
    keep.prepare = false;
    const auto after = gk::MidiRenderer::render (*p, {}, (int) (0.05 * sr), sr, keep);
    const float stepBefore = maxJump (out.getReadPointer (0), out.getNumSamples() - (int) (0.006 * sr), out.getNumSamples());
    const float stepFade = maxJump (after.getReadPointer (0), 0, (int) (0.006 * sr));
    INFO ("reset: max step " << stepFade << " (ringing before: " << stepBefore << ")");
    CHECK (stepFade <= 1.1f * stepBefore + 1.0e-4f);
    float late = 0.0f;
    for (int i = (int) (0.006 * sr); i < after.getNumSamples(); ++i)
        late = juce::jmax (late, std::abs (after.getSample (0, i)));
    CHECK (juce::exactlyEqual (late, 0.0f));
}

TEST_CASE ("PP 16. CPU: 8 voices + reverb + delay, stereo 48 kHz", "[pp][cpu]")
{
    const double sr = 48000.0;
    std::vector<Hit> hits;
    for (int k = 0; k < 160; ++k) // sixteenths at 124 BPM for 20 s: the pool stays full
        hits.push_back ({ (int) (k * 0.121 * sr), 48 + (k * 5) % 24 });
    for (int preset = 0; preset < numPresets; ++preset)
    {
        auto p = makeProc (preset, 1.0f, 1.0f, 1.0f);
        const int length = (int) (20.0 * sr);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto out = render (*p, hits, length, sr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double pct = 100.0 * ms / (1000.0 * length / sr);
        WARN ("preset " << preset << ": " << pct << " % of one core");
        if (timingChecksEnforced())
            CHECK (pct < 5.0);
        CHECK (out.getMagnitude (0, length) > 0.0f);
    }
}
