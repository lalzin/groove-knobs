// DAFT CHUNK - the analysis (spec 1 and 9): the tempo on 20 test loops from
// 90 to 130 BPM (right to x2 / /2), beat 1, the transients, the three slicing
// modes, the cuts on zero crossings with no click (sine and drum loop), the
// labels, the key, the limits (60 s, 64 slices), tap tempo, cancel.

#include "Analysis.h"
#include "DcSignals.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

using namespace daftchunk;

using namespace dctest;

namespace
{
Analysis analyseMono (const std::vector<float>& x, double rate = sr)
{
    const float* ch[] { x.data() };
    return analyse (ch, 1, (int64_t) x.size(), rate);
}

bool tempoRight (double found, double truth, double tolerance = 0.02)
{
    for (double m : { 1.0, 2.0, 0.5 })
        if (std::abs (found - truth * m) / (truth * m) < tolerance)
            return true;
    return false;
}

std::vector<float> sine (double seconds, double hz, float level = 0.5f)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = level * (float) std::sin (twoPi * hz * (double) i / sr);
    return x;
}
} // namespace

TEST_CASE ("DC 1 tempo on 20 loops from 90 to 130 BPM (to x2 / /2)", "[dc][analysis]")
{
    int exact = 0;
    for (int i = 0; i < 20; ++i)
    {
        const double bpm = 90.0 + 40.0 * i / 19.0;
        const int style = i % 3, bars = 2 + (i % 3) * 2; // 2, 4 or 6 bars
        const auto x = drumLoop (bpm, bars, style, 100u + (unsigned) i);
        const auto a = analyseMono (x);
        INFO ("loop " << i << ": " << bpm << " BPM, style " << style << ", " << bars << " bars -> " << a.bpm);
        CHECK (tempoRight (a.bpm, bpm));
        if (std::abs (a.bpm - bpm) < 0.05)
            ++exact;
    }
    CHECK (exact >= 18); // and not at x2 / /2: right on, for nearly all
}

TEST_CASE ("DC 2 a loop of whole bars: the exact tempo, beat 1 at the start", "[dc][analysis]")
{
    for (double bpm : { 98.0, 122.0, 127.5 })
    {
        const auto a = analyseMono (drumLoop (bpm, 4, 0, 7u));
        INFO (bpm << " -> " << a.bpm);
        CHECK (a.loopLocked);
        CHECK (a.bpm == Catch::Approx (bpm).margin (0.01));
        CHECK (a.firstBeat == Catch::Approx (0.0).margin (0.01 * sr));
    }
}

TEST_CASE ("DC 3 not a loop (a tail): the tempo still right", "[dc][analysis]")
{
    const auto a = analyseMono (drumLoop (118.0, 4, 0, 9u, nullptr, 0.37));
    CHECK_FALSE (a.loopLocked);
    CHECK (a.bpm == Catch::Approx (118.0).epsilon (0.01));
}

TEST_CASE ("DC 4 beat 1 when the file starts on beat 2", "[dc][analysis]")
{
    // 4 bars from beat 2: beat 1 comes 3 beats in (the kicks without the snare: 1 or 3, not 2 or 4)
    const double bpm = 120.0, beat = 60.0 / bpm;
    auto x = drumLoop (bpm, 5, 0, 11u);
    x.erase (x.begin(), x.begin() + (std::ptrdiff_t) std::llround (beat * sr));
    x.resize ((size_t) std::llround (16 * beat * sr));
    const auto a = analyseMono (x);
    const double inBeats = a.firstBeat / (beat * sr);
    INFO ("beat 1 at " << inBeats << " beats");
    const bool on1or3 = std::abs (inBeats - 3.0) < 0.05 || std::abs (inBeats - 1.0) < 0.05;
    CHECK (on1or3);
}

TEST_CASE ("DC 5 transients: every hit found, few extra", "[dc][analysis]")
{
    std::vector<Hit> hits;
    const auto x = drumLoop (120.0, 2, 0, 21u, &hits);
    const auto a = analyseMono (x);
    SliceSettings s;
    s.mode = SliceMode::transients;
    s.sensitivity = 0.6f;
    const auto b = cuts (a, s);
    // distinct hit times (a kick and a snare together count once)
    std::vector<double> times;
    for (const auto& h : hits)
        if (times.empty() || h.time - times.back() > 0.03)
            times.push_back (h.time);
    int found = 0;
    for (double t : times)
    {
        bool hit = false;
        for (auto c : b)
            hit = hit || std::abs ((double) c / sr - t) < 0.010;
        found += hit ? 1 : 0;
        if (! hit)
            UNSCOPED_INFO ("missed " << t);
    }
    CHECK (found >= (int) times.size() - 1);                       // the first, at 0, is the start anyway
    CHECK ((int) b.size() - 2 <= (int) times.size() + 2);          // no flood of false cuts
}

TEST_CASE ("DC 6 sensitivity: more cuts as it rises", "[dc][analysis]")
{
    const auto a = analyseMono (drumLoop (110.0, 2, 1, 31u));
    SliceSettings s;
    s.sensitivity = 0.0f;
    const auto low = cuts (a, s).size();
    s.sensitivity = 1.0f;
    const auto high = cuts (a, s).size();
    CHECK (high > low);
}

TEST_CASE ("DC 7 grid: the cuts on the grid, 1/4 1/8 1/16", "[dc][analysis]")
{
    const double bpm = 120.0;
    const auto a = analyseMono (drumLoop (bpm, 2, 0, 41u));
    for (int div : { 4, 8, 16 })
    {
        SliceSettings s;
        s.mode = SliceMode::grid;
        s.division = div;
        const auto b = cuts (a, s);
        const double step = sr * 60.0 / bpm * 4.0 / div;
        CHECK ((int) b.size() - 1 == (int) std::llround ((double) a.numSamples / step));
        for (size_t i = 1; i + 1 < b.size(); ++i)
            CHECK (std::abs ((double) b[i] - nearestGridPoint ((double) b[i], 0.0, step)) <= 0.0051 * sr * 2.0);
    }
}

TEST_CASE ("DC 8 mixed: grid points pulled onto a transient within 20 ms", "[dc][analysis]")
{
    // hits 12 ms late on purpose: the grid at 1/4 goes to them
    const double bpm = 120.0, beat = 0.5;
    std::vector<float> x ((size_t) (8 * beat * sr), 0.0f);
    for (int q = 0; q < 8; ++q)
        addKick (x, (int64_t) ((q * beat + (q > 0 ? 0.012 : 0.0)) * sr), 0.8f);
    const auto a = analyseMono (x);
    SliceSettings s;
    s.mode = SliceMode::mixed;
    s.division = 4;
    s.bpm = bpm;
    s.firstBeat = 0.0;
    const auto b = cuts (a, s);
    REQUIRE (b.size() == 9);
    for (int q = 1; q < 8; ++q)
        CHECK (std::abs ((double) b[(size_t) q] / sr - (q * beat + 0.012)) < 0.004);
    s.mode = SliceMode::grid;
    const auto g = cuts (a, s);
    for (int q = 1; q < 8; ++q)
        CHECK (std::abs ((double) g[(size_t) q] / sr - q * beat) < 0.006);
}

TEST_CASE ("DC 9 the cuts on zero crossings, 2 ms fades: no click (sine, drums)", "[dc][analysis][click]")
{
    auto maxStep = [] (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (size_t i = 1; i < v.size(); ++i)
            m = std::max (m, std::abs (v[i] - v[i - 1]));
        return m;
    };
    {
        const auto x = sine (4.0, 220.0);
        const auto a = analyseMono (x);
        SliceSettings s;
        s.mode = SliceMode::grid;
        s.division = 16;
        s.bpm = 123.0;
        s.firstBeat = 0.0;
        const auto slices = slice (a, s);
        REQUIRE (slices.size() > 10);
        std::vector<float> joined;
        for (const auto& sl : slices)
        {
            CHECK (std::abs (x[(size_t) sl.start]) < 0.0157f); // on a zero crossing (within one sample's step)
            const auto r = renderSlice (x.data(), (int64_t) x.size(), sl, sr);
            CHECK (std::abs (r.front()) < 1.0e-6f);
            CHECK (std::abs (r.back()) < 0.01f);
            joined.insert (joined.end(), r.begin(), r.end());
        }
        CHECK (maxStep (joined) <= maxStep (x) * 1.05f);
    }
    {
        const auto x = drumLoop (120.0, 2, 0, 51u);
        const auto a = analyseMono (x);
        for (auto mode : { SliceMode::transients, SliceMode::grid })
        {
            SliceSettings s;
            s.mode = mode;
            for (const auto& sl : slice (a, s))
            {
                const auto r = renderSlice (x.data(), (int64_t) x.size(), sl, sr);
                CHECK (std::abs (r.front()) < 1.0e-6f);
                CHECK (std::abs (r.back()) < 1.0e-3f);
                // the first and last 2 ms: no jump bigger than the source's own steepest
                for (size_t i = 1; i < std::min<size_t> (r.size(), 90); ++i)
                    CHECK (std::abs (r[i] - r[i - 1]) <= maxStep (x) + 1.0e-6f);
            }
        }
    }
}

TEST_CASE ("DC 10 labels: kick, snare, hat, voice, tonal, noise", "[dc][analysis][labels]")
{
    std::mt19937 rng (5u);
    auto label = [] (const std::vector<float>& v, float* root = nullptr) { return classify (v.data(), 0, (int64_t) v.size(), sr, root); };
    {
        std::vector<float> v ((size_t) (0.4 * sr), 0.0f);
        addKick (v, 0, 0.8f);
        CHECK (label (v) == SliceType::kick);
    }
    {
        std::vector<float> v ((size_t) (0.3 * sr), 0.0f);
        addSnare (v, 0, 0.8f, rng);
        CHECK (label (v) == SliceType::snare);
    }
    {
        std::vector<float> v ((size_t) (0.12 * sr), 0.0f);
        addHat (v, 0, 0.8f, rng);
        CHECK (label (v) == SliceType::hat);
    }
    {
        // a held saw at 110 Hz (A2): tonal, its root
        std::vector<float> v ((size_t) (0.5 * sr));
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double ph = std::fmod (110.0 * (double) i / sr, 1.0);
            v[i] = 0.4f * (float) (2.0 * ph - 1.0);
        }
        float root = -1.0f;
        CHECK (label (v, &root) == SliceType::tonal);
        CHECK (root == Catch::Approx (45.0f).margin (0.3f));
    }
    {
        // a sung "ah" at 220 Hz with vibrato (±0.5 semitone, 5.5 Hz): formants 800 / 1200 Hz
        std::vector<float> v ((size_t) (0.5 * sr));
        double ph = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / sr;
            const double f0 = 220.0 * std::pow (2.0, 0.5 / 12.0 * std::sin (twoPi * 5.5 * t));
            ph += twoPi * f0 / sr;
            double s = 0.0;
            for (int h = 1; h <= 20; ++h)
            {
                const double f = h * f0;
                const double formant = std::exp (-std::pow ((f - 800.0) / 250.0, 2.0)) + 0.7 * std::exp (-std::pow ((f - 1200.0) / 300.0, 2.0)) + 0.15 / h;
                s += formant * std::sin (h * ph);
            }
            v[i] = 0.25f * (float) s;
        }
        CHECK (label (v) == SliceType::voice);
    }
    {
        // held noise, low-passed (~3 kHz: a wash, not a hat): noise
        std::normal_distribution<float> g (0.0f, 0.4f);
        std::vector<float> v ((size_t) (0.5 * sr));
        float lp = 0.0f;
        for (auto& s : v)
            s = (lp += 0.35f * (g (rng) - lp));
        CHECK (label (v) == SliceType::noise);
        // held white noise (all treble): a hat (an open hat, a shaker)
        for (auto& s : v)
            s = 0.3f * g (rng);
        CHECK (label (v) == SliceType::hat);
    }
}

TEST_CASE ("DC 11 key: A minor, and E major", "[dc][analysis][key]")
{
    auto chordTrack = [] (const std::vector<std::vector<int>>& chords, double each)
    {
        std::vector<float> x ((size_t) (chords.size() * each * sr), 0.0f);
        for (size_t c = 0; c < chords.size(); ++c)
            for (int note : chords[c])
            {
                const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                for (int64_t i = 0; i < (int64_t) (each * sr); ++i)
                {
                    const double t = (double) i / sr;
                    double s = 0.0;
                    for (int h = 1; h <= 4; ++h)
                        s += std::sin (twoPi * f * h * t) / (h * h);
                    x[(size_t) ((int64_t) (c * each * sr) + i)] += 0.12f * (float) s;
                }
            }
        return x;
    };
    {
        // Am - F - C - G - Am - Dm - E - Am, a bass note under each
        const auto a = analyseMono (chordTrack ({ { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 },
                                                  { 45, 57, 60, 64 }, { 50, 57, 62, 65 }, { 40, 56, 59, 64 }, { 45, 57, 60, 64 } },
                                                1.0));
        INFO (a.key.name().toStdString());
        CHECK (a.key.tonic == 9);
        CHECK (a.key.minor);
        CHECK (a.key.name() == "A min");
    }
    {
        // E - A - B - E
        const auto a = analyseMono (chordTrack ({ { 40, 56, 59, 64 }, { 45, 57, 61, 64 }, { 47, 54, 59, 63 }, { 40, 56, 59, 64 } }, 1.5));
        INFO (a.key.name().toStdString());
        CHECK (a.key.tonic == 4);
        CHECK_FALSE (a.key.minor);
    }
}

TEST_CASE ("DC 12 limits: 60 s at most, 64 slices at most", "[dc][analysis]")
{
    const auto x = drumLoop (120.0, 36, 0, 61u); // 72 s
    const auto a = analyseMono (x);
    CHECK (a.numSamples == (int64_t) (60.0 * sr));
    SliceSettings s;
    s.mode = SliceMode::grid;
    s.division = 16;
    const auto slices = slice (a, s);
    CHECK (slices.size() == 64);
    CHECK (slices.back().end <= a.numSamples);
    for (size_t i = 1; i < slices.size(); ++i)
        CHECK (slices[i].start == slices[i - 1].end);
    s.mode = SliceMode::transients;
    s.sensitivity = 1.0f;
    CHECK (slice (a, s).size() <= 64);
}

TEST_CASE ("DC 13 tap tempo, the grid helper, cancel", "[dc][analysis]")
{
    CHECK (tapTempo ({ 0.0, 0.5, 1.0, 1.5, 2.0 }) == Catch::Approx (120.0));
    CHECK (tapTempo ({ 0.0, 0.48, 1.0, 1.52, 2.0 }) == Catch::Approx (120.0).epsilon (0.05));
    CHECK (tapTempo ({ 0.0, 0.5 }) == 0.0);
    CHECK (nearestGridPoint (1030.0, 10.0, 100.0) == Catch::Approx (1010.0));

    const auto x = drumLoop (120.0, 2, 0, 71u);
    const float* ch[] { x.data() };
    std::atomic<bool> cancel { true };
    std::atomic<float> progress { 0.0f };
    CHECK (analyse (ch, 1, (int64_t) x.size(), sr, &progress, &cancel).numSamples == 0);
    cancel = false;
    CHECK (analyse (ch, 1, (int64_t) x.size(), sr, &progress, &cancel).numSamples == (int64_t) x.size());
    CHECK (progress.load() == Catch::Approx (1.0f));
}

TEST_CASE ("DC 14 48 kHz and stereo", "[dc][analysis]")
{
    // the same loop at 48 kHz, L and R: the same tempo
    std::vector<float> l, r;
    {
        const auto x = drumLoop (124.0, 4, 0, 81u);
        // resample by linear interpolation to 48 kHz
        const double ratio = 44100.0 / 48000.0;
        const auto n = (size_t) ((double) x.size() / ratio);
        l.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double p = (double) i * ratio;
            const auto k = (size_t) p;
            const double f = p - (double) k;
            l[i] = k + 1 < x.size() ? (float) ((1.0 - f) * x[k] + f * x[k + 1]) : x.back();
        }
        r = l;
        for (auto& v : r)
            v *= 0.5f;
    }
    const float* ch[] { l.data(), r.data() };
    const auto a = analyse (ch, 2, (int64_t) l.size(), 48000.0);
    CHECK (a.bpm == Catch::Approx (124.0).epsilon (0.005));
}
