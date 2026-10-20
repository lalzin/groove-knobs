// Sing It Back tests: transparency, lead voice, scales and snap, retuning,
// formants, anti-robot variety, voicing, automation, voice count, block
// sizes, latency, stability, state, CPU.

#include "SingItBackProcessor.h"
#include "GkTestUtils.h"
#include "VocalSynth.h"
#include <catch2/catch_approx.hpp>

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
double soloVoicePitch (int voice, int midi, int key, int scale, bool snap, int harmony)
{
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 1.0f, key, scale);
    setParam (p->getSnapParameter(), snap ? 1.0f : 0.0f);
    setParam (p->getHarmonyParameter(), (float) harmony);
    setParam (p->getRoomParameter(), 0.0f);
    setParam (p->getHumanizeParameter(), 0.2f);
    p->getDsp().soloVoice (voice);
    const auto in = mono (vocalsynth::sustained (midi, 1.6, sr, 'a'));
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), p->getCrowdParameter().getValue());
    return medianPitch (w.data(), (int) (0.5 * sr), (int) w.size(), sr);
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

TEST_CASE ("SIB 9. Voicing: no crowd on an unvoiced consonant, no click", "[sib][voicing]")
{
    const double sr = 48000.0;
    auto p = makeProc (1, sr, 1.0f);
    setParam (p->getRoomParameter(), 0.0f);
    // a sustained "sss": high-passed noise only
    Buffer in (1, (int) (1.5 * sr));
    gk::testsignals::Rng rng (5);
    gk::TptSvf hp;
    const auto c = gk::TptSvf::Coeffs::make (4500.0, sr, 0.7);
    for (int i = 0; i < in.getNumSamples(); ++i)
        in.setSample (0, i, 0.3f * (float) hp.processHighpass (rng.bipolar(), c));
    const auto out = run (*p, in);
    const auto w = crowdOf (out, in, p->getLatencySamples(), p->getCrowdParameter().getValue());
    double e = 0.0, ein = 0.0;
    for (int i = (int) (0.3 * sr); i < in.getNumSamples(); ++i)
    {
        e += (double) w[(size_t) i] * w[(size_t) i];
        ein += (double) in.getSample (0, i) * in.getSample (0, i);
    }
    INFO ("crowd / input energy on the consonant: " << 10.0 * std::log10 (e / ein + 1.0e-30) << " dB");
    CHECK (e < 1.0e-4 * ein);
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
    // defaults, and "crowd" first, no preset parameter
    SingItBackProcessor c;
    CHECK (c.getCrowdParameter().getValue() == Catch::Approx (0.25f));
    CHECK (c.getSnapParameter().get());
    CHECK (c.getVoicesParameter().getCurrentChoiceName() == "8");
    CHECK (c.getRoomSizeParameter().getCurrentChoiceName() == "Club");
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

