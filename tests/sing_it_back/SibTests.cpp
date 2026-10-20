// Sing It Back tests: transparency, lead voice, scales and snap, retuning,
// formants, anti-robot variety, voicing, automation, voice count, block
// sizes, latency, stability, state, CPU.

#include "SingItBackProcessor.h"
#include "SingItBackEditor.h"
#include "GkTestUtils.h"
#include "VocalSynth.h"
#include <catch2/catch_approx.hpp>
#include <numeric>

using namespace gktest;
using sib::Harmony;
using sib::SingItBackProcessor;

namespace
{
std::unique_ptr<SingItBackProcessor> makeProc (int nch, double sr, float crowd, int key = 0, int scale = 0, int block = 256)
{
    auto p = std::make_unique<SingItBackProcessor>();
    setLayout (*p, nch);
    p->setNonRealtime (true);
    setParam (p->getCrowdParameter(), crowd);
    setParam (p->getKeyParameter(), (float) key);
    setParam (p->getScaleParameter(), (float) scale);
    p->prepareToPlay (sr, block);
    return p;
}

Buffer run (SingItBackProcessor& p, const Buffer& in, int block = 256, std::function<void (int)> before = {})
{
    return processBlocks (p, in, { block }, std::move (before));
}

Buffer mono (const Buffer& stereo)
{
    Buffer m (1, stereo.getNumSamples());
    m.copyFrom (0, 0, stereo, 0, 0, stereo.getNumSamples());
    return m;
}

/** Median pitch (Hz) of a signal over [from, to), with the plugin's own YIN. */
double medianPitch (const float* x, int from, int to, double sr)
{
    sib::PitchTracker t;
    t.prepare (sr);
    std::vector<float> f;
    for (int i = 0; i < to; ++i)
        if (t.push (x[i]) && i >= from && t.voiced())
            f.push_back (t.frequency());
    if (f.empty())
        return 0.0;
    std::sort (f.begin(), f.end());
    return f[f.size() / 2];
}

double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

/** The crowd alone: output minus the lead (the input delayed by L, at the
    lead's level for `crowd`), channel 0 (+1). */
std::vector<float> crowdOf (const Buffer& out, const Buffer& in, int L, float crowd)
{
    const float g = juce::Decibels::decibelsToGain (sib::SingItBackDsp::leadDb (crowd));
    std::vector<float> w ((size_t) out.getNumSamples(), 0.0f);
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < out.getNumChannels(); ++c)
            s += out.getSample (c, i) - g * (i >= L ? in.getSample (c, i - L) : 0.0f);
        w[(size_t) i] = s;
    }
    return w;
}

/** Pitch of one crowd voice (soloed), singing over a sustained vowel. */
double soloVoicePitch (int voice, int midi, int key, int scale, bool snap, int harmony, int style = 0)
{
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 1.0f, key, scale);
    setParam (p->getStyleParameter(), (float) style);
    setParam (p->getSnapParameter(), snap ? 1.0f : 0.0f);
    setParam (p->getHarmonyParameter(), (float) harmony);
    setParam (p->getRoomParameter(), 0.0f);
    setParam (p->getHumanizeParameter(), 0.2f);
    p->getDsp().soloVoice (voice);
    const auto in = mono (vocalsynth::sustained (midi, 1.6, sr, 'a'));
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), p->getCrowdParameter().getValue());
    // the voice's own (intentional) detune taken out: what is checked is the note it aims at
    return medianPitch (w.data(), (int) (0.5 * sr), (int) w.size(), sr) / std::exp2 (p->getDsp().voiceInfo (voice).detuneCents / 1200.0);
}

double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
} // namespace

//==============================================================================
TEST_CASE ("SIB 1. Crowd 0 %: bit-exact with the input delayed by the latency (mono, stereo)", "[sib][null]")
{
    const double sr = 48000.0;
    for (int nch : { 1, 2 })
    {
        auto p = makeProc (nch, sr, 0.0f);
        const int L = p->getLatencySamples();
        auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
        in.setSize (nch, juce::jmin (in.getNumSamples(), (int) (4.0 * sr)), true);
        const auto out = run (*p, in);
        bool exact = true;
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                exact = exact && juce::exactlyEqual (out.getSample (c, i), i >= L ? in.getSample (c, i - L) : 0.0f);
        INFO ("channels " << nch << ", latency " << L);
        CHECK (exact);
    }
}

TEST_CASE ("SIB 2. The lead voice is the input (delayed): untouched up to 40 %, -10 dB at 100 %", "[sib][lead]")
{
    const double sr = 48000.0;
    for (float crowd : { 0.25f, 0.5f, 1.0f })
    {
        auto p = makeProc (2, sr, crowd);
        p->getDsp().muteCrowd (true);
        const int L = p->getLatencySamples();
        // after 0.2 s of silence (the engine's own start-up fade-in is over)
        const auto sung = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
        Buffer in (2, sung.getNumSamples() + (int) (0.2 * sr));
        in.clear();
        for (int c = 0; c < 2; ++c)
            in.copyFrom (c, (int) (0.2 * sr), sung, c, 0, sung.getNumSamples());
        const auto out = run (*p, in);
        const float g = juce::Decibels::decibelsToGain (sib::SingItBackDsp::leadDb (crowd));
        double err = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                err = juce::jmax (err, (double) std::abs (out.getSample (c, i) - g * (i >= L ? in.getSample (c, i - L) : 0.0f)));
        INFO ("crowd " << crowd << ": lead gain " << g << ", max error " << err);
        CHECK (err < 1.0e-6);
        if (crowd <= 0.4f)
            CHECK (juce::exactlyEqual (g, 1.0f));
    }
}

TEST_CASE ("SIB 2b. At 100 % the crowd is louder than the lead (wet)", "[sib][lead]")
{
    const double sr = 48000.0;
    const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 110.0);
    for (float crowd : { 0.5f, 1.0f })
    {
        auto p = makeProc (2, sr, crowd);
        auto q = makeProc (2, sr, crowd);
        q->getDsp().muteCrowd (true);
        const auto out = run (*p, in), leadOnly = run (*q, in);
        double eLead = 0.0, eCrowd = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const double l = leadOnly.getSample (c, i), x = out.getSample (c, i) - l;
                eLead += l * l;
                eCrowd += x * x;
            }
        const double ratio = 10.0 * std::log10 (eCrowd / eLead);
        INFO ("crowd " << crowd << ": crowd / lead " << ratio << " dB");
        if (crowd >= 1.0f)
            CHECK (ratio > 3.0);
        else
            CHECK (ratio < 0.0);
    }
}

TEST_CASE ("SIB 3. Scale, snap on: diatonic thirds", "[sib][scale]")
{
    // (the spec's "A 440 -> C 261.6" is the right note an octave low, and "E flat in
    // A minor" is not in A minor: DECISIONS.md) A4 -> C5, E4 -> G4 in C major;
    // C4 -> E4 in A minor
    CHECK (Harmony::target (69.0, 0, Harmony::major, true, Harmony::autoThirds, Harmony::harmonyUp) == Catch::Approx (72.0));
    CHECK (Harmony::target (64.0, 0, Harmony::major, true, Harmony::thirds, Harmony::harmonyUp) == Catch::Approx (67.0));
    CHECK (Harmony::target (60.0, 9, Harmony::minor, true, Harmony::autoThirds, Harmony::harmonyUp) == Catch::Approx (64.0));
    // a slightly flat singer is snapped first: 68.7 (A, 30 cents flat) -> C5
    CHECK (Harmony::target (68.7, 0, Harmony::major, true, Harmony::autoThirds, Harmony::harmonyUp) == Catch::Approx (72.0));
    // fifths, fourths, octaves (degrees)
    CHECK (Harmony::target (60.0, 0, Harmony::major, true, Harmony::fifths, Harmony::harmonyUp) == Catch::Approx (67.0));
    CHECK (Harmony::target (64.0, 0, Harmony::major, true, Harmony::fourths, Harmony::harmonyUp) == Catch::Approx (69.0));
    CHECK (Harmony::target (62.0, 0, Harmony::major, true, Harmony::octaves, Harmony::harmonyUp) == Catch::Approx (74.0));
    // rendered: the voice really sings it (±5 cents)
    const double c5 = soloVoicePitch (1, 69, 0, Harmony::major, true, Harmony::autoThirds);
    const double g4 = soloVoicePitch (1, 64, 0, Harmony::major, true, Harmony::autoThirds);
    const double e4 = soloVoicePitch (1, 60, 9, Harmony::minor, true, Harmony::autoThirds);
    INFO ("C5 " << c5 << " Hz (" << cents (c5, midiHz (72)) << " c), G4 " << g4 << " (" << cents (g4, midiHz (67)) << " c), E4 " << e4 << " ("
                << cents (e4, midiHz (64)) << " c)");
    CHECK (std::abs (cents (c5, midiHz (72))) < 5.0);
    CHECK (std::abs (cents (g4, midiHz (67))) < 5.0);
    CHECK (std::abs (cents (e4, midiHz (64))) < 5.0);
}

TEST_CASE ("SIB 4. Snap off: fixed intervals", "[sib][scale]")
{
    CHECK (Harmony::target (69.0, 0, Harmony::major, false, Harmony::thirds, Harmony::harmonyUp) == Catch::Approx (73.0));
    CHECK (Harmony::target (69.3, 0, Harmony::major, false, Harmony::fifths, Harmony::harmonyUp) == Catch::Approx (76.3)); // pitch kept
    const double cs5 = soloVoicePitch (1, 69, 0, Harmony::major, false, Harmony::thirds);
    INFO ("C#5 " << cs5 << " Hz (" << cents (cs5, midiHz (73)) << " c)");
    CHECK (std::abs (cents (cs5, midiHz (73))) < 5.0);
}

TEST_CASE ("SIB 5. Pentatonic: no crowd voice ever off the scale (snap on)", "[sib][scale]")
{
    for (int scale : { Harmony::pentaMajor, Harmony::pentaMinor, Harmony::major, Harmony::minor, Harmony::dorian, Harmony::mixolydian })
        for (int key = 0; key < 12; ++key)
            for (int harmony = 0; harmony < 5; ++harmony)
                for (int role = 0; role < 4; ++role)
                    for (double m = 40.0; m < 90.0; m += 0.37)
                    {
                        const double t = Harmony::target (m, key, scale, true, harmony, role);
                        if (! Harmony::inScale (t, key, scale) || std::abs (t - std::round (t)) > 1.0e-9)
                        {
                            FAIL ("scale " << scale << " key " << key << " harmony " << harmony << " role " << role << " pitch " << m << " -> " << t);
                        }
                    }
    SUCCEED();
}

TEST_CASE ("SIB 6. Key change while singing: the voices re-tune in 80 ms, no click", "[sib][retune]")
{
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 0.6f);
    setParam (p->getRoomParameter(), 0.0f);
    const auto in = mono (vocalsynth::sustained (64, 2.0, sr, 'o'));
    const int change = (int) (1.0 * sr);
    const auto out = run (*p, in, 256, [&] (int pos) {
        if (pos >= change && p->getKeyParameter().getIndex() == 0)
            setParam (p->getKeyParameter(), 2.0f); // C -> D major
    });
    const auto w = crowdOf (out, in, p->getLatencySamples(), p->getCrowdParameter().getValue());
    auto maxJump = [&] (int a, int b)
    {
        float m = 0.0f;
        for (int i = a + 1; i < b; ++i)
            m = juce::jmax (m, std::abs (w[(size_t) i] - w[(size_t) i - 1]));
        return m;
    };
    const float before = maxJump (change - (int) (0.4 * sr), change), during = maxJump (change, change + (int) (0.2 * sr));
    INFO ("largest sample step: before " << before << ", during the re-tune " << during);
    CHECK (during < 1.5f * before);
}

TEST_CASE ("SIB 7. Formants kept: the crowd voice's F1 / F2 within 3 dB of the same vowel sung at its pitch", "[sib][formants]")
{
    // A2 (110 Hz: harmonics dense enough to draw the envelope) -> a third up,
    // C3. Reference: the singer singing C3 with the same vowel; control: the
    // input resampled to C3 (formants 19 % up), which must fail the check.
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 1.0f);
    setParam (p->getRoomParameter(), 0.0f);
    setParam (p->getHumanizeParameter(), 0.2f);
    p->getDsp().soloVoice (1);
    const auto in = mono (vocalsynth::sustained (45, 2.0, sr, 'a'));
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), p->getCrowdParameter().getValue());
    const auto ref = mono (vocalsynth::sustained (48, 2.0, sr, 'a'));
    std::vector<float> resampled ((size_t) in.getNumSamples(), 0.0f);
    for (size_t i = 0; i < resampled.size(); ++i)
        if (const double t = (double) i * midiHz (48) / midiHz (45); t + 1 < in.getNumSamples())
            resampled[i] = in.getSample (0, (int) t) + (float) (t - std::floor (t)) * (in.getSample (0, (int) t + 1) - in.getSample (0, (int) t));

    // spectral envelope: the power spectrum smoothed across the harmonics
    // (Gaussian, sigma 120 Hz) at each formant, relative to the 200 Hz - 4 kHz energy
    auto envelope = [&] (const float* x, double f)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) (2 * size), 0.0f);
        const int from = (int) (0.5 * sr), len = (int) (1.2 * sr);
        for (int i = 0; i < len; ++i)
            buf[(size_t) i] = x[from + i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) len));
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        double band = 0.0, total = 0.0;
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * sr / size, e = (double) buf[(size_t) k] * buf[(size_t) k];
            if (hz > 200.0 && hz < 4000.0)
                total += e;
            band += e * std::exp (-0.5 * (hz - f) * (hz - f) / (120.0 * 120.0));
        }
        return 10.0 * std::log10 (band / total);
    };
    for (double f : { 730.0, 1090.0 })
    {
        const double a = envelope (ref.getReadPointer (0), f), b = envelope (w.data(), f), c = envelope (resampled.data(), f);
        INFO ("formant " << f << " Hz: sung at C3 " << a << " dB, crowd voice " << b << " dB, resampled " << c << " dB");
        CHECK (std::abs (a - b) < 3.0);
    }
    // the measure does tell formants that moved: over 400 Hz - 2.6 kHz the
    // resampled control strays far from the reference, the crowd voice does not
    double strayCrowd = 0.0, strayResampled = 0.0;
    for (double f = 400.0; f <= 2600.0; f += 100.0)
    {
        const double a = envelope (ref.getReadPointer (0), f);
        strayCrowd = juce::jmax (strayCrowd, std::abs (a - envelope (w.data(), f)));
        strayResampled = juce::jmax (strayResampled, std::abs (a - envelope (resampled.data(), f)));
    }
    INFO ("largest envelope difference: crowd voice " << strayCrowd << " dB, resampled " << strayResampled << " dB");
    CHECK (strayResampled > 6.0);
    CHECK (strayCrowd < 0.5 * strayResampled);
}

TEST_CASE ("SIB 8. Anti-robot: every voice different, never in lockstep", "[sib][human]")
{
    const double sr = 48000.0;
    // humanize 50 %, 8 voices: offsets spread (sd >= 8 ms)
    {
        auto p = makeProc (2, sr, 1.0f);
        setParam (p->getHumanizeParameter(), 0.5f);
        Buffer in (2, 512);
        in.clear();
        run (*p, in);
        std::vector<double> off;
        for (int k = 0; k < 8; ++k)
            off.push_back (p->getDsp().voiceInfo (k).offsetMs);
        double mean = 0.0, var = 0.0;
        for (auto o : off)
            mean += o / off.size();
        for (auto o : off)
            var += (o - mean) * (o - mean) / off.size();
        INFO ("offsets sd " << std::sqrt (var) << " ms");
        CHECK (std::sqrt (var) >= 8.0);
        // vibratos: 4 voices at least 0.3 Hz apart; 8 / 12 spread evenly
        for (int count : { 4, 8, 12 })
        {
            std::vector<double> rates;
            for (int k = 0; k < count; ++k)
                rates.push_back (p->getDsp().voiceInfo (k).vibratoHz);
            std::sort (rates.begin(), rates.end());
            double minGap = 1.0e9;
            for (size_t k = 1; k < rates.size(); ++k)
                minGap = juce::jmin (minGap, rates[k] - rates[k - 1]);
            INFO (count << " voices: smallest vibrato gap " << minGap << " Hz");
            CHECK (minGap >= (count == 4 ? 0.3 : 1.4 / (count - 1) * 0.5));
        }
    }
    // humanize 20 % (the minimum): the 3 ms minimum offset holds
    {
        auto p = makeProc (2, sr, 1.0f);
        setParam (p->getHumanizeParameter(), 0.0f); // clamped to 20 %
        for (int k = 0; k < 12; ++k)
            CHECK (p->getDsp().voiceInfo (k).offsetMs >= 3.0);
    }
    // dropouts: no regular pattern (intervals between dips vary)
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getDropoutsParameter(), 1.0f);
        const auto in = mono (vocalsynth::sustained (60, 30.0, sr, 'a'));
        std::vector<double> starts;
        int previous = 0;
        processBlocks (*p, in, { 512 }, [&] (int pos) {
            const int d = p->getDsp().getTelemetry().dropoutCount.load();
            if (d > previous)
                starts.push_back (pos / sr);
            previous = d;
        });
        REQUIRE (starts.size() > 5);
        std::vector<double> gaps;
        for (size_t k = 1; k < starts.size(); ++k)
            gaps.push_back (starts[k] - starts[k - 1]);
        double mean = 0.0, var = 0.0;
        for (auto g : gaps)
            mean += g / gaps.size();
        for (auto g : gaps)
            var += (g - mean) * (g - mean) / gaps.size();
        INFO (starts.size() << " dropouts, gap mean " << mean << " s, sd " << std::sqrt (var));
        CHECK (std::sqrt (var) / mean > 0.3);
    }
}

TEST_CASE ("SIB 9. Consonants: doubled at half level, not transposed, no buzz", "[sib][voicing]")
{
    // (DECISIONS.md: the crowd used to go silent on unvoiced sound; Alan found
    // the cut-outs robotic. Consonants are now doubled, untransposed, with
    // irregular grains: no pitched buzz.)
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 1.0f);
    setParam (p->getRoomParameter(), 0.0f);
    setParam (p->getDropoutsParameter(), 0.0f);
    // a sustained "sss": high-passed noise only
    Buffer in (1, (int) (1.5 * sr));
    gk::testsignals::Rng rng (5);
    gk::TptSvf hp;
    const auto c = gk::TptSvf::Coeffs::make (4500.0, sr, 0.7);
    for (int i = 0; i < in.getNumSamples(); ++i)
        in.setSample (0, i, 0.3f * (float) hp.processHighpass (rng.bipolar(), c));
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
    double e = 0.0, ein = 0.0;
    for (int i = (int) (0.3 * sr); i < in.getNumSamples(); ++i)
    {
        e += (double) w[(size_t) i] * w[(size_t) i];
        ein += (double) in.getSample (0, i) * in.getSample (0, i);
    }
    // no periodicity in the crowd's consonant (a regular grain train would buzz)
    sib::PitchTracker t;
    t.prepare (sr);
    int hops = 0, voiced = 0;
    for (int i = 0; i < (int) w.size(); ++i)
        if (t.push (w[(size_t) i]) && i > (int) (0.3 * sr))
        {
            ++hops;
            voiced += t.voiced() ? 1 : 0;
        }
    const double db = 10.0 * std::log10 (e / ein + 1.0e-30);
    INFO ("crowd / input energy on the consonant: " << db << " dB; crowd voiced " << voiced << " / " << hops << " hops");
    CHECK (db < 0.0);
    CHECK (db > -20.0);
    CHECK (voiced <= hops / 20);
}

TEST_CASE ("SIB 10. Crowd automation 0 -> 1 -> 0 in 100 ms: no click", "[sib][automation]")
{
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 0.0f);
    const auto in = mono (vocalsynth::sustained (62, 3.0, sr, 'o', 15.0));
    const int a = (int) (1.0 * sr), span = (int) (0.1 * sr);
    const auto out = processBlocks (*p, in, { 64 }, [&] (int pos) {
        float v = 0.0f;
        if (pos >= a && pos < a + span)
            v = (float) (pos - a) / span;
        else if (pos >= a + span && pos < a + 2 * span)
            v = 1.0f;
        else if (pos >= a + 2 * span && pos < a + 3 * span)
            v = 1.0f - (float) (pos - a - 2 * span) / span;
        setParam (p->getCrowdParameter(), v);
    });
    // DECISIONS.md: "no click" = no HF burst above -60 dB relative to the signal
    const double burst = zipperBurstDb (out.getReadPointer (0), a - (int) (0.2 * sr), a + 5 * span, sr);
    INFO ("burst " << burst << " dB");
    CHECK (burst < -60.0);
}

TEST_CASE ("SIB 11. Voice count never exceeds VOICES", "[sib][voices]")
{
    const double sr = 48000.0;
    for (int choice : { 0, 1, 2 })
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getVoicesParameter(), (float) choice);
        const int maxCount = choice == 0 ? 4 : choice == 1 ? 8 : 12;
        const auto in = mono (vocalsynth::sustained (60, 1.0, sr, 'a'));
        int most = 0;
        run (*p, in, 256, [&] (int) { most = juce::jmax (most, p->getDsp().getTelemetry().activeChorus.load()); });
        INFO ("VOICES " << maxCount << ": at most " << most << " heard");
        CHECK (most <= maxCount);
        CHECK (most >= maxCount - 1);
    }
}

TEST_CASE ("SIB 12. Block sizes: identical output for any partition", "[sib][blocks]")
{
    const double sr = 48000.0;
    const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 140.0, 0.2);
    Buffer cut (2, (int) (3.0 * sr));
    for (int c = 0; c < 2; ++c)
        cut.copyFrom (c, 0, in, c, 0, cut.getNumSamples());
    auto renderWith = [&] (const std::vector<int>& sizes)
    {
        auto p = makeProc (2, sr, 0.8f, 0, 0, 4096);
        return processBlocks (*p, cut, sizes);
    };
    const auto ref = renderWith ({ 512 });
    juce::Random rng (9);
    std::vector<int> randomSizes;
    for (int i = 0; i < 4000; ++i)
        randomSizes.push_back (1 + rng.nextInt (1500));
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 4096 }, randomSizes })
    {
        const auto out = renderWith (sizes);
        double err = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                err = juce::jmax (err, (double) std::abs (out.getSample (c, i) - ref.getSample (c, i)));
        INFO ("block " << sizes.front() << (sizes.size() > 1 ? " (random)" : "") << ": max difference " << err);
        CHECK (err < 1.0e-6);
    }
}

TEST_CASE ("SIB 13. Latency: reported = measured, <= 30 ms, constant", "[sib][latency]")
{
    for (double sr : sampleRates)
    {
        auto p = makeProc (1, sr, 0.0f);
        const int L = p->getLatencySamples();
        INFO ("sr " << sr << ": latency " << L << " samples = " << 1000.0 * L / sr << " ms");
        CHECK (1000.0 * L / sr <= 30.0);
        Buffer in (1, L + 4096);
        in.clear();
        in.setSample (0, 100, 0.5f);
        const auto out = run (*p, in);
        int first = -1;
        for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
            if (! juce::exactlyEqual (out.getSample (0, i), 0.0f))
                first = i;
        CHECK (first == 100 + L);
        setParam (p->getVoicesParameter(), 2.0f);
        setParam (p->getRoomSizeParameter(), 2.0f);
        setParam (p->getCrowdParameter(), 1.0f);
        run (*p, in);
        CHECK (p->getLatencySamples() == L);
    }
}

TEST_CASE ("SIB 14. Stability: no NaN / Inf, peak below 0 dBFS", "[sib][stability]")
{
    for (double sr : sampleRates)
    {
        const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 140.0, 0.5);
        for (int scale = 0; scale < 6; ++scale)
            for (int key : { 0, 5, 9, 11 })
            {
                if (sr > 50000.0 && (scale > 0 || key > 0))
                    continue;
                auto p = makeProc (2, sr, 1.0f, key, scale, 512);
                setParam (p->getVoicesParameter(), 2.0f);
                setParam (p->getRoomSizeParameter(), 2.0f);
                setParam (p->getRoomParameter(), 1.0f);
                const auto out = run (*p, in, 512);
                float peak = 0.0f;
                bool finite = true;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                    {
                        finite = finite && std::isfinite (out.getSample (c, i));
                        peak = juce::jmax (peak, std::abs (out.getSample (c, i)));
                    }
                INFO ("sr " << sr << " scale " << scale << " key " << key << ": peak " << peak);
                CHECK (finite);
                CHECK (peak < 1.0f);
            }
    }
}

TEST_CASE ("SIB 15. State round trip, parameters", "[sib][state]")
{
    SingItBackProcessor a;
    setParam (a.getCrowdParameter(), 0.7f);
    setParam (a.getKeyParameter(), 9.0f);
    setParam (a.getScaleParameter(), 4.0f);
    setParam (a.getSnapParameter(), 0.0f);
    setParam (a.getHarmonyParameter(), 3.0f);
    setParam (a.getVoicesParameter(), 2.0f);
    setParam (a.getSpreadParameter(), 0.2f);
    setParam (a.getHumanizeParameter(), 0.9f);
    setParam (a.getDropoutsParameter(), 0.1f);
    setParam (a.getRoomParameter(), 0.8f);
    setParam (a.getRoomSizeParameter(), 2.0f);
    setParam (a.getStyleParameter(), 5.0f);
    setParam (a.getResponseParameter(), 2.0f);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    SingItBackProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getCrowdParameter().getValue() == Catch::Approx (0.7f).margin (1.0e-6));
    CHECK (b.getKeyParameter().getIndex() == 9);
    CHECK (b.getScaleParameter().getIndex() == 4);
    CHECK_FALSE (b.getSnapParameter().get());
    CHECK (b.getHarmonyParameter().getIndex() == 3);
    CHECK (b.getVoicesParameter().getIndex() == 2);
    CHECK (b.getSpreadParameter().getValue() == Catch::Approx (0.2f).margin (1.0e-6));
    CHECK (b.getHumanizeParameter().getValue() == Catch::Approx (0.9f).margin (1.0e-6));
    CHECK (b.getDropoutsParameter().getValue() == Catch::Approx (0.1f).margin (1.0e-6));
    CHECK (b.getRoomParameter().getValue() == Catch::Approx (0.8f).margin (1.0e-6));
    CHECK (b.getRoomSizeParameter().getIndex() == 2);
    CHECK (b.getStyleParameter().getIndex() == 5);
    CHECK (b.getResponseParameter().getIndex() == 2);
    // defaults, and "crowd" first, no preset parameter
    SingItBackProcessor c;
    CHECK (c.getCrowdParameter().getValue() == Catch::Approx (0.25f));
    CHECK (c.getSnapParameter().get());
    CHECK (c.getVoicesParameter().getCurrentChoiceName() == "8");
    CHECK (c.getRoomSizeParameter().getCurrentChoiceName() == "Club");
    CHECK (c.getStyleParameter().getCurrentChoiceName() == "Club");
    CHECK (c.getResponseParameter().getIndex() == 0);
    CHECK (dynamic_cast<juce::RangedAudioParameter*> (c.getParameters()[0])->getParameterID() == "crowd");
    CHECK_FALSE (c.hasPresetParameter());
}

TEST_CASE ("SIB 16. CPU: 12 voices, stereo, 48 kHz", "[sib][cpu]")
{
    const double sr = 48000.0;
    auto p = makeProc (2, sr, 1.0f, 0, 0, 512);
    setParam (p->getVoicesParameter(), 2.0f);
    const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    run (*p, in, 512);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const double pct = 100.0 * ms / (1000.0 * in.getNumSamples() / sr);
    WARN ("12 voices: " << pct << " % of one core");
    if (timingChecksEnforced())
        CHECK (pct < 15.0);
}


TEST_CASE ("SIB 17. Styles: who sings what, how", "[sib][styles]")
{
    using D = sib::SingItBackDsp;
    // every style's roles land in the scale (snap on)
    for (int st = 0; st < D::numStyles; ++st)
        for (int k = 0; k < D::maxVoices; ++k)
            CHECK (D::styleSpec (st).roles[(size_t) k] <= Harmony::octaveUp);
    // Enfants: voice 0 an octave up, Géants: voice 0 an octave down; Gospel:
    // voice 2 a fifth up (A4 -> E5 in C major); exact pitch whatever the formants
    const double up = soloVoicePitch (0, 64, 0, Harmony::major, true, Harmony::autoThirds, D::kids);
    const double down = soloVoicePitch (0, 64, 0, Harmony::major, true, Harmony::autoThirds, D::giants);
    const double fifth = soloVoicePitch (2, 69, 0, Harmony::major, true, Harmony::autoThirds, D::gospel);
    INFO ("Enfants " << up << " Hz, Geants " << down << " Hz, Gospel fifth " << fifth << " Hz");
    CHECK (std::abs (cents (up, midiHz (76))) < 5.0);
    CHECK (std::abs (cents (down, midiHz (52))) < 5.0);
    CHECK (std::abs (cents (fifth, midiHz (76))) < 5.0);
    // Robots: no vibrato, no detune, nearly together; Stade: looser than Club
    auto p = makeProc (2, 48000.0, 1.0f);
    setParam (p->getStyleParameter(), (float) D::robots);
    Buffer in (2, 512);
    in.clear();
    run (*p, in);
    double maxRobot = 0.0;
    for (int k = 0; k < D::maxVoices; ++k)
    {
        const auto v = p->getDsp().voiceInfo (k);
        CHECK (juce::exactlyEqual (v.vibratoCents, 0.0));
        CHECK (juce::exactlyEqual (v.detuneCents, 0.0));
        maxRobot = juce::jmax (maxRobot, v.offsetMs);
    }
    CHECK (maxRobot < 10.0);
}

TEST_CASE ("SIB 18. RÉPONSE: the crowd sings the phrase back half a bar / a bar later", "[sib][response]")
{
    const double sr = 48000.0;
    for (int response : { 1, 2 })
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getRoomParameter(), 0.0f);
        setParam (p->getDropoutsParameter(), 0.0f);
        setParam (p->getResponseParameter(), (float) response);
        // a 0.6 s note at 0.2 s, then silence (120 BPM, 4/4: 1 s / 2 s later)
        const auto note = mono (vocalsynth::sustained (64, 0.6, sr, 'a'));
        Buffer in (1, (int) (3.5 * sr));
        in.clear();
        in.copyFrom (0, (int) (0.2 * sr), note, 0, 0, juce::jmin (note.getNumSamples(), in.getNumSamples() - (int) (0.2 * sr)));
        const auto out = run (*p, in);
        const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
        const double delay = response == 1 ? 1.0 : 2.0, L = p->getLatencySamples() / sr;
        auto energy = [&] (double a, double b)
        {
            double e = 0.0;
            for (int i = (int) (a * sr); i < (int) (b * sr) && i < (int) w.size(); ++i)
                e += (double) w[(size_t) i] * w[(size_t) i];
            return e;
        };
        const double during = energy (0.2 + L, 0.8 + L), answer = energy (0.2 + L + delay, 0.8 + L + delay + 0.05);
        double onset = -1.0;
        for (size_t i = 0; i < w.size() && onset < 0.0; ++i)
            if (std::abs (w[i]) > 0.01f)
                onset = (double) i / sr;
        INFO ("response " << response << ": crowd energy while singing " << during << ", in the answer " << answer << ", onset " << onset << " s");
        CHECK (during < 1.0e-3 * answer);
        CHECK (onset > 0.2 + L + delay);
        CHECK (onset < 0.2 + L + delay + 0.12);
        CHECK (p->getDsp().getTelemetry().responseSeconds.load() == Catch::Approx (delay).margin (0.001));
    }
}

TEST_CASE ("diag: pitch track of a file (SIB_DIAG_FILE)", "[.diagfile]")
{
    const char* path = std::getenv ("SIB_DIAG_FILE");
    REQUIRE (path != nullptr);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (path)));
    REQUIRE (r != nullptr);
    Buffer b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    sib::PitchTracker t;
    t.prepare (r->sampleRate);
    int hops = 0, voiced = 0, jumps = 0;
    float last = 0.0f;
    juce::String line;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float x = 0.5f * (b.getSample (0, i) + b.getSample (b.getNumChannels() - 1, i));
        if (t.push (x))
        {
            ++hops;
            if (t.voiced())
            {
                ++voiced;
                const float m = (float) (69.0 + 12.0 * std::log2 (t.frequency() / 440.0));
                if (last > 0.0f && std::abs (m - last) > 0.7f)
                    ++jumps;
                last = m;
            }
            else
                last = 0.0f;
            if (hops % 20 == 0)
                line << juce::String (i / r->sampleRate, 1) << "s:" << (t.voiced() ? juce::String (69.0 + 12.0 * std::log2 (t.frequency() / 440.0), 1) : juce::String ("-"))
                     << "(" << juce::String (t.aperiodicity(), 2) << ") ";
        }
    }
    WARN ("voiced " << voiced << " / " << hops << " hops, note jumps > 0.7 st: " << jumps << "\n" << line);
}

TEST_CASE ("SIB 19. VOL: 0 dB = untouched (bit-exact at 0 %), -6 dB halves, the bottom is silence; the dry path too", "[sib][volume]")
{
    const double sr = 48000.0;
    auto p = makeProc (2, sr, 0.0f);
    CHECK (p->getVolumeParameter().convertFrom0to1 (p->getVolumeParameter().getDefaultValue()) == Catch::Approx (0.0f).margin (0.001));
    const int L = p->getLatencySamples();
    const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
    for (float db : { -6.0206f, -60.0f, 6.0206f })
    {
        auto q = makeProc (2, sr, 0.0f);
        setParam (q->getVolumeParameter(), db);
        const float g = db <= -59.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
        const auto out = run (*q, in);
        double err = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = (int) (0.05 * sr); i < out.getNumSamples(); ++i)
            {
                const float x = g * (i >= L ? in.getSample (c, i - L) : 0.0f);
                if (std::abs (x) < 0.8f) // below the safety clip
                    err = juce::jmax (err, (double) std::abs (out.getSample (c, i) - x));
            }
        INFO ("VOL " << db << " dB: max error " << err);
        CHECK (err < 1.0e-6);
    }
}

TEST_CASE ("SIB 20. Natural: the crowd keeps the singer's own vibrato and slides (no auto-tune)", "[sib][natural]")
{
    // a sung A4 with a wide vibrato (35 cents): the unison voice follows it
    // (pitch spread close to the singer's), Robots flattens it (style pull)
    const double sr = 48000.0;
    const auto in = mono (vocalsynth::sustained (69, 2.0, sr, 'a', 35.0));
    auto spreadOf = [&] (const float* x, int n)
    {
        sib::PitchTracker t;
        t.prepare (sr);
        std::vector<double> c;
        for (int i = 0; i < n; ++i)
            if (t.push (x[i]) && i > (int) (0.7 * sr) && t.voiced())
                c.push_back (1200.0 * std::log2 (t.frequency() / 440.0));
        double mean = 0.0, var = 0.0;
        for (auto v : c)
            mean += v / (double) c.size();
        for (auto v : c)
            var += (v - mean) * (v - mean) / (double) c.size();
        return std::sqrt (var);
    };
    const double singer = spreadOf (in.getReadPointer (0), in.getNumSamples());
    double voices[2] {};
    for (int style : { 0, (int) sib::SingItBackDsp::robots })
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getStyleParameter(), (float) style);
        setParam (p->getHumanizeParameter(), 0.0f);
        setParam (p->getRoomParameter(), 0.0f);
        p->getDsp().soloVoice (0); // unison
        const auto out = run (*p, in);
        const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
        voices[style == 0 ? 0 : 1] = spreadOf (w.data(), (int) w.size());
    }
    INFO ("pitch spread: singer " << singer << " c, unison voice (Club) " << voices[0] << " c, Robots " << voices[1] << " c");
    CHECK (voices[0] > 0.7 * singer);
    CHECK (voices[1] < 0.4 * singer);
}

TEST_CASE ("diag: block load (average and worst block) at 128 samples", "[.diagcpu]")
{
    const double sr = 48000.0;
    for (int voicesChoice : { 1, 2 })
    {
        auto p = makeProc (2, sr, 1.0f, 0, 0, 128);
        setParam (p->getVoicesParameter(), (float) voicesChoice);
        const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
        Buffer buf (2, 128);
        juce::MidiBuffer midi;
        double total = 0.0, worst = 0.0;
        int blocks = 0, worstAt = 0;
        std::vector<double> times;
        for (int pos = 0; pos + 128 <= in.getNumSamples(); pos += 128)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, in, c, pos, 128);
            const auto t0 = juce::Time::getHighResolutionTicks();
            p->processBlock (buf, midi);
            const double us = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6;
            total += us;
            if (us > worst)
                worstAt = blocks;
            worst = juce::jmax (worst, us);
            times.push_back (us);
            ++blocks;
        }
        const double blockUs = 128.0 / sr * 1.0e6;
        std::sort (times.begin(), times.end());
        WARN ("worst at block " << worstAt << " (" << worstAt * 128 / sr << " s); 99.9th percentile " << 100.0 * times[times.size() * 999 / 1000] / (128.0 / sr * 1.0e6) << " %; 2nd worst " << 100.0 * times[times.size() - 2] / (128.0 / sr * 1.0e6) << " %");
        WARN ((voicesChoice == 1 ? 8 : 12) << " voices: average " << 100.0 * total / blocks / blockUs << " %, worst block " << 100.0 * worst / blockUs << " % of the block time");
    }
}

namespace
{
/** Crackle: clicks in a signal = samples whose 2nd difference stands far out
    of its local level (8 x the RMS of the 2nd difference over ±5 ms); per second. */
std::vector<double> clickTimes;
double clicksPerSecond (const float* x, int n, double sr)
{
    clickTimes.clear();
    std::vector<float> d2 ((size_t) n, 0.0f);
    for (int i = 1; i + 1 < n; ++i)
        d2[(size_t) i] = x[i + 1] - 2.0f * x[i] + x[i - 1];
    const int w = (int) (0.005 * sr);
    double acc = 0.0;
    int clicks = 0, lastClick = -1000000;
    for (int i = 0; i < n; ++i)
    {
        acc += (double) d2[(size_t) i] * d2[(size_t) i];
        if (i >= 2 * w)
            acc -= (double) d2[(size_t) (i - 2 * w)] * d2[(size_t) (i - 2 * w)];
        const int c = i - w;
        if (c < w)
            continue;
        const double rmsLocal = std::sqrt (juce::jmax (0.0, acc) / (2 * w));
        if (std::abs (d2[(size_t) c]) > 5.0 * rmsLocal + 1.0e-5 && c - lastClick > w)
        {
            ++clicks;
            lastClick = c;
            clickTimes.push_back (c / sr);
        }
    }
    return clicks / (n / sr);
}

/** A more realistic singer: jitter, shimmer, breath noise, a male low note phrase. */
Buffer realisticVoice (double sr)
{
    auto v = vocalsynth::sing (vocalsynth::phrase ({ 48, 50, 52, 53, 55, 57, 59 }), sr, 110.0, 0.5, 25.0, 11);
    gk::testsignals::Rng rng (17);
    gk::TptSvf hp;
    const auto c = gk::TptSvf::Coeffs::make (2500.0, sr, 0.7);
    for (int i = 0; i < v.getNumSamples(); ++i)
    {
        const float breath = 0.02f * (float) hp.processHighpass (rng.bipolar(), c);
        for (int ch = 0; ch < 2; ++ch)
            v.setSample (ch, i, v.getSample (ch, i) * (1.0f + 0.05f * (float) rng.bipolar()) + breath);
    }
    return v;
}
} // namespace

TEST_CASE ("diag: crackle of the crowd", "[.diagclick]")
{
    const double sr = 48000.0;
    const auto in = mono (realisticVoice (sr));
    WARN ("input: " << clicksPerSecond (in.getReadPointer (0), in.getNumSamples(), sr) << " clicks / s");
    for (int style = 0; style < 7; ++style)
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getStyleParameter(), (float) style);
        setParam (p->getRoomParameter(), 0.0f);
        const auto out = run (*p, in);
        const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
        // HF share (> 6 kHz) relative to the input's: crackle adds HF
        auto hf = [&] (const float* x, int n)
        {
            gk::TptSvf a, b;
            const auto cc = gk::TptSvf::Coeffs::make (6000.0, sr, 0.7);
            double eh = 0.0, e = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double y = b.processHighpass (a.processHighpass (x[i], cc), cc);
                eh += y * y;
                e += (double) x[i] * x[i];
            }
            return 10.0 * std::log10 (eh / (e + 1e-30));
        };
        WARN ("style " << style << ": crowd " << clicksPerSecond (w.data(), (int) w.size(), sr) << " clicks / s, HF share " << hf (w.data(), (int) w.size())
                       << " dB (input " << hf (in.getReadPointer (0), in.getNumSamples()) << " dB)");
        juce::String ts;
        for (auto t : clickTimes)
            ts << juce::String (t, 3) << " ";
        WARN ("  at " << ts);
        const double outClicks = clicksPerSecond (out.getReadPointer (0), out.getNumSamples(), sr);
        juce::String ts2;
        for (auto t : clickTimes)
            ts2 << juce::String (t, 3) << " ";
        WARN ("  output (what is heard): " << outClicks << " clicks / s at " << ts2);
    }
}

TEST_CASE ("diag: crackle per voice", "[.diagclick2]")
{
    const double sr = 48000.0;
    const auto in = mono (realisticVoice (sr));
    for (int style : { 0, 5 })
        for (int k = 0; k < 8; ++k)
        {
            auto p = makeProc (1, sr, 1.0f);
            setParam (p->getStyleParameter(), (float) style);
            setParam (p->getRoomParameter(), 0.0f);
            setParam (p->getDropoutsParameter(), 0.0f);
            p->getDsp().soloVoice (k);
            const auto out = run (*p, in);
            const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
            const double c = clicksPerSecond (w.data(), (int) w.size(), sr);
            juce::String ts;
            for (auto t : clickTimes)
                if (t > 0.2)
                    ts << juce::String (t, 3) << " ";
            WARN ("style " << style << " voice " << k << " (role " << p->getDsp().voiceInfo (k).role << "): " << c << " /s at " << ts);
        }
}

TEST_CASE ("diag: detector false positives at low pitch", "[.diagclick3]")
{
    const double sr = 48000.0;
    const auto in = mono (realisticVoice (sr));
    std::vector<float> down ((size_t) in.getNumSamples() * 2);
    for (size_t i = 0; i + 2 < down.size(); ++i)
    {
        const double t = (double) i * 0.5;
        const int k = (int) t;
        down[i] = k + 1 < in.getNumSamples() ? in.getSample (0, k) + (float) (t - k) * (in.getSample (0, k + 1) - in.getSample (0, k)) : 0.0f;
    }
    WARN ("input slowed an octave down: " << clicksPerSecond (down.data(), (int) down.size(), sr) << " clicks / s");
}

TEST_CASE ("diag: dump octave-down voice", "[.diagdump]")
{
    const double sr = 48000.0;
    const auto in = mono (realisticVoice (sr));
    auto p = makeProc (1, sr, 1.0f);
    setParam (p->getRoomParameter(), 0.0f);
    setParam (p->getDropoutsParameter(), 0.0f);
    p->getDsp().soloVoice (4);
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
    clicksPerSecond (w.data(), (int) w.size(), sr);
    juce::File f ("/Users/alan/Documents/Claude project/groove-knobs/.scratch/oct.txt");
    juce::String s;
    for (auto t : clickTimes)
        if (t > 1.3 && t < 1.5)
        {
            const int c = (int) (t * sr);
            for (int i = c - 60; i < c + 60; ++i)
                s << i << " " << juce::String (w[(size_t) i], 6) << "\n";
            break;
        }
    f.replaceWithText (s);
}

TEST_CASE ("SIB 21. No crackle: a breathy voice, every voice incl. an octave down, and the output", "[sib][crackle]")
{
    // clicks = samples whose 2nd difference stands > 5x out of its ±5 ms
    // level (the singer: 0 / s); an octave down used to rasp (~15 / s): the
    // grains no longer overlapped and chopped the breath
    const double sr = 48000.0;
    const auto in = mono (realisticVoice (sr));
    REQUIRE (clicksPerSecond (in.getReadPointer (0), in.getNumSamples(), sr) < 0.05);
    auto after = [] (double from) { int n = 0; for (auto t : clickTimes) n += t > from ? 1 : 0; return n; };
    for (int k : { 0, 1, 4, 7 }) // unison, third up, octave down, third down
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getRoomParameter(), 0.0f);
        setParam (p->getDropoutsParameter(), 0.0f);
        p->getDsp().soloVoice (k);
        const auto out = run (*p, in);
        const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
        clicksPerSecond (w.data(), (int) w.size(), sr);
        const double rate = after (0.2) / (in.getNumSamples() / sr);
        INFO ("voice " << k << " (role " << p->getDsp().voiceInfo (k).role << "): " << rate << " clicks / s");
        CHECK (rate < 1.0);
    }
    for (int style = 0; style < sib::SingItBackDsp::numStyles; ++style)
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getStyleParameter(), (float) style);
        const auto out = run (*p, in);
        clicksPerSecond (out.getReadPointer (0), out.getNumSamples(), sr);
        const double rate = after (0.2) / (in.getNumSamples() / sr);
        INFO ("style " << style << ", output: " << rate << " clicks / s");
        CHECK (rate < 0.3);
    }
}

TEST_CASE ("SIB 22. CPU: no block spike (128-sample blocks, 12 voices)", "[sib][cpu]")
{
    const double sr = 48000.0;
    auto p = makeProc (2, sr, 1.0f, 0, 0, 128);
    setParam (p->getVoicesParameter(), 2.0f);
    const auto in = vocalsynth::sing (vocalsynth::phrase ({ 60, 62, 64, 65, 67, 69, 71 }), sr, 120.0);
    Buffer buf (2, 128);
    juce::MidiBuffer midi;
    std::vector<double> times;
    for (int pos = 0; pos + 128 <= in.getNumSamples(); pos += 128)
    {
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in, c, pos, 128);
        const auto t0 = juce::Time::getHighResolutionTicks();
        p->processBlock (buf, midi);
        times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
    }
    std::sort (times.begin(), times.end());
    const double block = 128.0 / sr;
    const double p999 = 100.0 * times[times.size() * 999 / 1000] / block, mean = 100.0 * std::accumulate (times.begin(), times.end(), 0.0) / (double) times.size() / block;
    WARN ("128-sample blocks: average " << mean << " %, 99.9th percentile " << p999 << " % of the block time");
    if (timingChecksEnforced())
    {
        CHECK (mean < 6.0);
        CHECK (p999 < 25.0);
    }
}

TEST_CASE ("diag: Live conditions (512, 48 kHz, real time, a file) + UI cost", "[.diaglive]")
{
    const double sr = 48000.0;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (std::getenv ("SIB_DIAG_FILE"))));
    REQUIRE (r != nullptr);
    Buffer in (2, (int) r->lengthInSamples);
    r->read (&in, 0, in.getNumSamples(), 0, true, true);
    for (float crowd : { 0.25f, 1.0f })
    {
        SingItBackProcessor p;
        setLayout (p, 2);
        setParam (p.getCrowdParameter(), crowd);
        p.prepareToPlay (sr, 512);
        Buffer buf (2, 512);
        juce::MidiBuffer midi;
        double total = 0.0, worst = 0.0;
        int n = 0;
        for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, in, c, pos, 512);
            const auto t0 = juce::Time::getHighResolutionTicks();
            p.processBlock (buf, midi);
            const double s = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            total += s;
            worst = juce::jmax (worst, s);
            ++n;
        }
        WARN ("crowd " << crowd << ": audio average " << 100.0 * total / n / (512.0 / sr) << " %, worst block " << 100.0 * worst / (512.0 / sr) << " % of the block time");
    }
    // UI: Sugar singing at 60 fps (one frame = advance + paint into an image of the window size)
    juce::ScopedJuceInitialiser_GUI gui;
    SingItBackProcessor p;
    setParam (p.getCrowdParameter(), 1.0f);
    setParam (p.getVoicesParameter(), 2.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    juce::Image img (juce::Image::ARGB, ed->getWidth() * 2, ed->getHeight() * 2, true);
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int f = 0; f < 120; ++f)
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        ed->paintEntireComponent (g, true);
    }
    const double perFrame = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / 120.0;
    // per component
    std::function<void (juce::Component&, int)> each = [&] (juce::Component& c, int depth)
    {
        if (c.getWidth() > 30 && depth <= 3)
        {
            juce::Image im (juce::Image::ARGB, c.getWidth() * 2, c.getHeight() * 2, true);
            const auto t1 = juce::Time::getHighResolutionTicks();
            for (int f = 0; f < 60; ++f)
            {
                juce::Graphics g (im);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                c.paintEntireComponent (g, true);
            }
            const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t1) / 60.0 * 1000.0;
            WARN (juce::String::repeatedString ("  ", depth) << typeid (c).name() << " " << c.getWidth() << "x" << c.getHeight() << ": " << ms << " ms");
        }
        for (auto* ch : c.getChildren())
            each (*ch, depth + 1);
    };
    each (*ed, 0);
    // a typical frame: the stage and the knob areas repainted (background behind them included)
    auto* editor = dynamic_cast<sib::SingItBackEditor*> (ed.get());
    for (bool cached : { false, true })
    {
        editor->setBackgroundCached (cached);
        juce::Image im (juce::Image::ARGB, ed->getWidth() * 2, ed->getHeight() * 2, true);
        const auto t1 = juce::Time::getHighResolutionTicks();
        for (int f = 0; f < 60; ++f)
        {
            juce::Graphics g (im);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            g.reduceClipRegion (juce::Rectangle<int> (10, 58, 400, 172));
            g.excludeClipRegion ({ 0, 0, 0, 0 });
            ed->paintEntireComponent (g, true);
        }
        const double stage = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t1) / 60.0;
        const auto t2 = juce::Time::getHighResolutionTicks();
        for (int f = 0; f < 60; ++f)
        {
            juce::Graphics g (im);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            g.reduceClipRegion (juce::Rectangle<int> (262, 262).withCentre ({ 210, 345 }));
            ed->paintEntireComponent (g, true);
        }
        const double knob = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t2) / 60.0;
        WARN ((cached ? "cached background" : "no cache") << ": stage repaint " << stage * 1000.0 << " ms, knob repaint " << knob * 1000.0 << " ms");
    }
    WARN ("UI: " << perFrame * 1000.0 << " ms per full frame at 2x = " << 100.0 * perFrame * 60.0 << " % of a core at 60 fps (whole window; normally only the stage repaints)");
}

TEST_CASE ("SIB 23. Not auto-tuned: each voice's periods vary from one to the next (jitter), like a singer's", "[sib][natural]")
{
    // a perfectly steady 220 Hz tone in: the unison voice out must not be
    // perfectly steady (an exactly periodic copy is what sounds synthetic),
    // but stay a subtle jitter (< 1 %); Robots stays steady
    const double sr = 48000.0;
    Buffer in (1, (int) (2.0 * sr));
    for (int i = 0; i < in.getNumSamples(); ++i)
        in.setSample (0, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr));
    auto jitterOf = [&] (int style)
    {
        auto p = makeProc (1, sr, 1.0f);
        setParam (p->getStyleParameter(), (float) style);
        setParam (p->getRoomParameter(), 0.0f);
        p->getDsp().soloVoice (0);
        const auto out = run (*p, in);
        const auto w = crowdOf (out, in, p->getLatencySamples(), 1.0f);
        std::vector<double> zc;
        for (int i = (int) (0.8 * sr); i + 1 < (int) w.size() - (int) (0.2 * sr); ++i)
            if (w[(size_t) i] < 0.0f && w[(size_t) i + 1] >= 0.0f)
                zc.push_back (i + (double) w[(size_t) i] / (double) (w[(size_t) i] - w[(size_t) i + 1]));
        double sum = 0.0;
        int n = 0;
        for (size_t k = 2; k < zc.size(); ++k)
        {
            const double a = zc[k - 1] - zc[k - 2], b = zc[k] - zc[k - 1];
            sum += std::abs (b - a) / (0.5 * (a + b));
            ++n;
        }
        return 100.0 * sum / juce::jmax (1, n);
    };
    const double club = jitterOf (0), robots = jitterOf ((int) sib::SingItBackDsp::robots);
    INFO ("cycle-to-cycle period change: Club " << club << " %, Robots " << robots << " %");
    // (the measure itself shows ~0.15 % on a steady copy: Robots is the reference)
    CHECK (club > robots + 0.1);
    CHECK (club < 1.0);
}
