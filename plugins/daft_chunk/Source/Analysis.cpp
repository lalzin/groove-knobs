#include "Analysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace daftchunk
{
namespace
{
    constexpr double pi = 3.14159265358979323846;

    bool cancelled (const std::atomic<bool>* c) noexcept { return c != nullptr && c->load (std::memory_order_relaxed); }
    void report (std::atomic<float>* p, float v) noexcept
    {
        if (p != nullptr)
            p->store (v, std::memory_order_relaxed);
    }

    int orderFor (int size) noexcept
    {
        int o = 0;
        while ((1 << o) < size)
            ++o;
        return o;
    }

    std::vector<float> hann (int n)
    {
        std::vector<float> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / n));
        return w;
    }

    /** Linear interpolation of v at a fractional index (0 outside). */
    float at (const std::vector<float>& v, double i) noexcept
    {
        if (i < 0.0 || i > (double) v.size() - 1.0)
            return 0.0f;
        const auto k = (size_t) i;
        const float f = (float) (i - (double) k);
        return k + 1 < v.size() ? v[k] + f * (v[k + 1] - v[k]) : v[k];
    }

    float percentile (std::vector<float> v, double q)
    {
        if (v.empty())
            return 0.0f;
        const auto k = (size_t) std::clamp (q * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) k, v.end());
        return v[k];
    }

    /** Prefix sums of x^2 (double): the energy of any range in O(1). */
    std::vector<double> energyPrefix (const std::vector<float>& x)
    {
        std::vector<double> s (x.size() + 1, 0.0);
        for (size_t i = 0; i < x.size(); ++i)
            s[i + 1] = s[i] + (double) x[i] * x[i];
        return s;
    }

    double meanEnergy (const std::vector<double>& s, int64_t a, int64_t b) noexcept
    {
        a = std::clamp<int64_t> (a, 0, (int64_t) s.size() - 1);
        b = std::clamp<int64_t> (b, 0, (int64_t) s.size() - 1);
        return b > a ? (s[(size_t) b] - s[(size_t) a]) / (double) (b - a) : 0.0;
    }

    /** The attack inside [from, to): where the next 4 ms most outweigh the 10 ms before. */
    int64_t refineAttack (const std::vector<double>& e, double sr, int64_t from, int64_t to)
    {
        const auto post = (int64_t) (0.004 * sr), pre = (int64_t) (0.010 * sr);
        int64_t best = from;
        double bestRatio = -1.0;
        for (int64_t i = from; i < to; i += 8)
        {
            const double r = meanEnergy (e, i, i + post) / (meanEnergy (e, i - pre, i) + 1.0e-9);
            if (r > bestRatio)
            {
                bestRatio = r;
                best = i;
            }
        }
        return std::max<int64_t> (0, best - (int64_t) (0.0005 * sr));
    }

    // Krumhansl-Kessler key profiles (major, minor), C first
    constexpr float majorProfile[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlation (const float* a, const float* b, int rot)
    {
        float ma = 0.0f, mb = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= 12.0f;
        mb /= 12.0f;
        float num = 0.0f, da = 0.0f, db = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const float x = a[(i + rot) % 12] - ma, y = b[i] - mb;
            num += x * y;
            da += x * x;
            db += y * y;
        }
        return num / std::sqrt (da * db + 1.0e-12f);
    }

    /** YIN: the pitch (Hz) of x[0, w) and how periodic it is (the CMNDF minimum, 0 = perfectly). */
    std::pair<double, double> yin (const float* x, int w, double sr)
    {
        const int tauMin = std::max (2, (int) (sr / 1000.0)), tauMax = std::min (w / 2, (int) (sr / 55.0));
        if (tauMax <= tauMin + 2)
            return { 0.0, 1.0 };
        std::vector<double> d ((size_t) tauMax + 1, 0.0);
        const int len = w - tauMax;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const double v = (double) x[i] - x[i + tau];
                s += v * v;
            }
            d[(size_t) tau] = s;
        }
        // cumulative mean normalised difference
        std::vector<double> c ((size_t) tauMax + 1, 1.0);
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            c[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (c[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && c[(size_t) tau + 1] < c[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (c[(size_t) tau] < c[(size_t) best])
                    best = tau;
        }
        // parabolic interpolation
        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1];
            const double den = a - 2.0 * b + cc;
            if (std::abs (den) > 1.0e-12)
                t += 0.5 * (a - cc) / den;
        }
        return { sr / t, c[(size_t) best] };
    }
} // namespace

const char* typeName (SliceType t) noexcept
{
    switch (t)
    {
        case SliceType::kick: return "KICK";
        case SliceType::snare: return "SNARE";
        case SliceType::hat: return "HAT";
        case SliceType::voice: return "VOIX";
        case SliceType::tonal: return "TONAL";
        case SliceType::noise: break;
    }
    return "BRUIT";
}

juce::String Key::name() const
{
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[tonic % 12]) + (minor ? " min" : " maj");
}

std::array<int, 7> Key::scale() const noexcept
{
    static constexpr int maj[7] { 0, 2, 4, 5, 7, 9, 11 }, min[7] { 0, 2, 3, 5, 7, 8, 10 };
    std::array<int, 7> s {};
    for (int i = 0; i < 7; ++i)
        s[(size_t) i] = (tonic + (minor ? min[i] : maj[i])) % 12;
    return s;
}

//==============================================================================
Analysis analyse (const float* const* channels, int numChannels, int64_t numSamples, double sampleRate,
                  std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    Analysis a;
    a.sampleRate = sampleRate;
    a.numSamples = std::min<int64_t> (numSamples, (int64_t) (Analysis::maxSeconds * sampleRate));
    const auto n = a.numSamples;
    if (n <= 0 || numChannels <= 0 || sampleRate <= 0.0)
        return {};

    // the mono sum
    a.mono.assign ((size_t) n, 0.0f);
    const int chans = std::min (numChannels, 2);
    for (int c = 0; c < chans; ++c)
        for (int64_t i = 0; i < n; ++i)
            a.mono[(size_t) i] += channels[c][i] / (float) chans;

    // the waveform peaks
    a.peakMin.assign (Analysis::numPeaks, 0.0f);
    a.peakMax.assign (Analysis::numPeaks, 0.0f);
    for (int b = 0; b < Analysis::numPeaks; ++b)
    {
        const auto from = n * b / Analysis::numPeaks, to = std::max (from + 1, n * (b + 1) / Analysis::numPeaks);
        float lo = 0.0f, hi = 0.0f;
        for (int64_t i = from; i < std::min (to, n); ++i)
            for (int c = 0; c < chans; ++c)
            {
                lo = std::min (lo, channels[c][i]);
                hi = std::max (hi, channels[c][i]);
            }
        a.peakMin[(size_t) b] = lo;
        a.peakMax[(size_t) b] = hi;
    }

    // --- STFT: spectral flux (log magnitudes, positive differences)
    const int fftSize = sampleRate <= 50000.0 ? 2048 : 4096;
    a.hop = fftSize / 4;
    a.frameRate = sampleRate / a.hop;
    const auto numFrames = (int) (n / a.hop) + 1;
    juce::dsp::FFT fft (orderFor (fftSize));
    const auto window = hann (fftSize);
    std::vector<float> buf ((size_t) fftSize * 2), prev ((size_t) fftSize / 2 + 1, 0.0f), cur (prev.size());
    const double binHz = sampleRate / fftSize;
    const int lowBin = std::max (1, (int) (30.0 / binHz)), highBin = std::min (fftSize / 2, (int) (16000.0 / binHz));
    const int bassBin = std::max (2, (int) std::ceil (150.0 / binHz)), trebleBin = (int) (1500.0 / binHz);
    a.flux.assign ((size_t) numFrames, 0.0f);
    a.lowFlux.assign ((size_t) numFrames, 0.0f);
    a.highFlux.assign ((size_t) numFrames, 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        if ((f & 63) == 0)
        {
            if (cancelled (cancel))
                return {};
            report (progress, 0.6f * (float) f / (float) numFrames);
        }
        std::fill (buf.begin(), buf.end(), 0.0f);
        const int64_t start = (int64_t) f * a.hop - fftSize / 2;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto j = start + i;
            if (j >= 0 && j < n)
                buf[(size_t) i] = a.mono[(size_t) j] * window[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float total = 0.0f, low = 0.0f, high = 0.0f;
        for (int k = 1; k <= fftSize / 2; ++k)
        {
            cur[(size_t) k] = std::log1p (1000.0f * buf[(size_t) k] * 2.0f / (float) fftSize);
            const float d = std::max (0.0f, cur[(size_t) k] - prev[(size_t) k]);
            if (k >= lowBin && k <= highBin)
                total += d;
            if (k < bassBin)
                low += d;
            else if (k >= trebleBin && k <= highBin)
                high += d;
        }
        // (frame 0 against silence: a hit right at the start counts, beat 1 of most loops)
        a.flux[(size_t) f] = total;
        a.lowFlux[(size_t) f] = low;
        a.highFlux[(size_t) f] = high;
        std::swap (cur, prev);
    }
    // normalise (the 98th percentile -> ~0.8)
    for (auto* v : { &a.flux, &a.lowFlux, &a.highFlux })
    {
        const float ref = std::max (1.0e-6f, percentile (*v, 0.98));
        for (auto& x : *v)
            x = std::min (1.5f, 0.8f * x / ref);
    }

    // --- onset candidates: local maxima (±30 ms) with their prominence over the local median (±250 ms)
    const auto energy = energyPrefix (a.mono);
    {
        const int w = std::max (1, (int) std::lround (0.03 * a.frameRate)), m = std::max (2, (int) std::lround (0.25 * a.frameRate));
        std::vector<float> win;
        for (int f = 1; f < numFrames; ++f)
        {
            const float v = a.flux[(size_t) f];
            if (v <= 0.01f)
                continue;
            bool isMax = true;
            for (int k = std::max (0, f - w); k <= std::min (numFrames - 1, f + w) && isMax; ++k)
                if (a.flux[(size_t) k] > v || (k < f && a.flux[(size_t) k] >= v))
                    isMax = false;
            if (! isMax)
                continue;
            win.assign (a.flux.begin() + std::max (0, f - m), a.flux.begin() + std::min (numFrames, f + m + 1));
            const float prom = v - percentile (win, 0.5);
            if (prom <= 0.005f)
                continue;
            const int64_t centre = (int64_t) f * a.hop;
            const auto attack = refineAttack (energy, sampleRate, std::max<int64_t> (0, centre - fftSize / 2), std::min<int64_t> (n, centre + a.hop));
            a.onsets.push_back ({ attack, std::min (1.0f, prom) });
        }
    }
    report (progress, 0.65f);

    // --- tempo: autocorrelation of the detrended flux, then a fine comb (period and phase)
    {
        std::vector<float> e ((size_t) numFrames, 0.0f);
        const int half = std::max (1, (int) (0.5 * a.frameRate));
        double run = 0.0;
        int count = 0;
        for (int f = 0; f < std::min (numFrames, half); ++f)
            run += a.flux[(size_t) f], ++count;
        for (int f = 0; f < numFrames; ++f)
        {
            if (f + half < numFrames)
                run += a.flux[(size_t) (f + half)], ++count;
            if (f - half - 1 >= 0)
                run -= a.flux[(size_t) (f - half - 1)], --count;
            e[(size_t) f] = std::max (0.0f, a.flux[(size_t) f] - (float) (run / std::max (1, count)));
        }
        const int maxLag = std::min (numFrames - 1, (int) (a.frameRate * 60.0 / 40.0 * 4.0) + 2);
        std::vector<float> acf ((size_t) maxLag + 1, 0.0f);
        for (int l = 1; l <= maxLag; ++l)
        {
            double s = 0.0;
            for (int t = 0; t + l < numFrames; ++t)
                s += (double) e[(size_t) t] * e[(size_t) (t + l)];
            acf[(size_t) l] = (float) (s / (numFrames - l));
        }
        if (cancelled (cancel))
            return {};
        report (progress, 0.7f);

        double best = 120.0, bestScore = -1.0;
        for (double b = 80.0; b <= 160.0; b += 0.05)
        {
            const double L = a.frameRate * 60.0 / b;
            double s = 0.0;
            for (int k = 1; k <= 4; ++k)
                s += at (acf, k * L);
            const double lb = std::log2 (b / 120.0);
            s *= 0.8 + 0.2 * std::exp (-0.5 * lb * lb / 0.25);
            if (s > bestScore)
            {
                bestScore = s;
                best = b;
            }
        }

        // fine: the period and the phase that put the most flux on the beats
        auto comb = [&] (double bpm, double& phaseOut)
        {
            const double P = a.frameRate * 60.0 / bpm;
            double bestS = -1.0;
            for (double ph = 0.0; ph < P; ph += 0.25)
            {
                double s = 0.0;
                int k = 0;
                for (double t = ph; t < numFrames - 1; t += P, ++k)
                    s += at (a.flux, t);
                s /= std::max (1, k);
                if (s > bestS)
                {
                    bestS = s;
                    phaseOut = ph;
                }
            }
            return bestS;
        };
        double phase = 0.0, combBest = -1.0;
        for (double b = best * 0.985; b <= best * 1.015; b += best * 0.0005)
        {
            double ph = 0.0;
            const double s = comb (b, ph);
            if (s > combBest)
            {
                combBest = s;
                a.bpm = b;
                phase = ph;
            }
        }
        const double meanFlux = std::accumulate (a.flux.begin(), a.flux.end(), 0.0) / std::max (1, numFrames);
        a.tempoConfidence = std::clamp ((combBest / (meanFlux + 1.0e-9) - 1.0) / 3.0, 0.0, 1.0);

        // a loop: a whole number of bars -> the exact tempo, the grid from the start
        const double seconds = a.seconds(), beats = seconds * a.bpm / 60.0;
        const double bars4 = std::round (beats / 4.0) * 4.0;
        if (bars4 >= 4.0)
        {
            const double exact = 60.0 * bars4 / seconds;
            const double startDistance = std::min (phase, a.frameRate * 60.0 / a.bpm - phase) / a.frameRate; // s from the start to a beat
            if (std::abs (exact - a.bpm) / a.bpm < 0.02 && startDistance < 0.06)
            {
                a.bpm = exact;
                a.loopLocked = true;
                phase = 0.0;
            }
        }
        if (! a.loopLocked)
            a.bpm = std::round (a.bpm * 100.0) / 100.0;
        report (progress, 0.75f);

        // beat 1: the beat class (of 4) with the most low-end attack and the least of the rest
        // (a kick without the snare: in house the kick is on every beat, the snare on 2 and 4)
        const double P = a.frameRate * 60.0 / a.bpm;
        double classScore[4] {};
        int classCount[4] {};
        int j = 0;
        for (double t = phase; t < numFrames - 1; t += P, ++j)
        {
            double s = 0.0;
            for (int d = -1; d <= 1; ++d) // ±1 frame: the onset may sit just off the grid
                s = std::max (s, 2.0 * at (a.lowFlux, t + d) - 1.0 * at (a.highFlux, t + d));
            classScore[j % 4] += s;
            ++classCount[j % 4];
        }
        double score[4] {}, hi = -1.0e30, lo = 1.0e30;
        int bestClass = 0;
        for (int c = 0; c < 4; ++c)
        {
            score[c] = classCount[c] > 0 ? classScore[c] / classCount[c] : -1.0e30;
            if (score[c] > hi)
            {
                hi = score[c];
                bestClass = c;
            }
            lo = std::min (lo, score[c]);
        }
        // a beat right at the start is most often the one: it wins unless clearly behind
        if ((a.loopLocked || phase / a.frameRate < 0.06) && score[0] >= hi - 0.2 * (hi - lo))
            bestClass = 0;
        a.firstBeat = (phase + bestClass * P) * a.hop;
        if (a.loopLocked && bestClass == 0)
            a.firstBeat = 0.0;
        else
        {
            // onto the nearest attack (±40 ms)
            double nearest = 1.0e30;
            for (const auto& o : a.onsets)
                if (std::abs ((double) o.sample - a.firstBeat) < std::abs (nearest - a.firstBeat) && o.strength > 0.05f)
                    nearest = (double) o.sample;
            if (std::abs (nearest - a.firstBeat) < 0.04 * sampleRate)
                a.firstBeat = nearest;
        }
    }
    report (progress, 0.8f);

    // --- key: a long-window chromagram (80 Hz .. 5 kHz), against the major / minor profiles
    {
        const int cs = sampleRate <= 50000.0 ? 16384 : 32768, chop = cs / 2;
        juce::dsp::FFT cfft (orderFor (cs));
        const auto cw = hann (cs);
        std::vector<float> cb ((size_t) cs * 2);
        const double cbin = sampleRate / cs;
        const int frames = (int) (n / chop) + 1;
        for (int f = 0; f < frames; ++f)
        {
            if (cancelled (cancel))
                return {};
            std::fill (cb.begin(), cb.end(), 0.0f);
            const int64_t start = (int64_t) f * chop - cs / 2;
            for (int i = 0; i < cs; ++i)
                if (const auto jj = start + i; jj >= 0 && jj < n)
                    cb[(size_t) i] = a.mono[(size_t) jj] * cw[(size_t) i];
            cfft.performFrequencyOnlyForwardTransform (cb.data(), true);
            for (int k = (int) (80.0 / cbin); k <= (int) (5000.0 / cbin) && k < cs / 2; ++k)
            {
                const double midi = 69.0 + 12.0 * std::log2 (k * cbin / 440.0);
                const double r = std::round (midi);
                if (std::abs (midi - r) > 0.35) // between two semitones: neither
                    continue;
                a.chroma[(size_t) (((int) r % 12 + 12) % 12)] += cb[(size_t) k];
            }
            report (progress, 0.8f + 0.18f * (float) f / (float) frames);
        }
        const float peak = *std::max_element (a.chroma.begin(), a.chroma.end());
        if (peak > 0.0f)
            for (auto& c : a.chroma)
                c /= peak;
        float bestR = -2.0f;
        for (int t = 0; t < 12; ++t)
            for (int minor = 0; minor < 2; ++minor)
            {
                const float r = correlation (a.chroma.data(), minor ? minorProfile : majorProfile, t);
                if (r > bestR)
                {
                    bestR = r;
                    a.key.tonic = t;
                    a.key.minor = minor == 1;
                }
            }
        a.key.confidence = std::max (0.0f, bestR);
    }
    report (progress, 1.0f);
    return a;
}

//==============================================================================
int64_t nearestZeroCrossing (const float* x, int64_t n, int64_t pos, int64_t maxDistance, bool preferBefore)
{
    if (pos <= 0 || pos >= n)
        return std::clamp<int64_t> (pos, 0, n);
    auto crossing = [&] (int64_t i) { return i > 0 && i < n && ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f) || std::abs (x[i]) < 1.0e-9f); };
    int64_t before = -1, after = -1;
    for (int64_t d = 0; d <= maxDistance && before < 0; ++d)
        if (crossing (pos - d))
            before = pos - d;
    for (int64_t d = 0; d <= maxDistance && after < 0; ++d)
        if (crossing (pos + d))
            after = pos + d;
    if (before < 0 && after < 0)
        return pos;
    if (before < 0)
        return after;
    if (after < 0)
        return before;
    const auto db = pos - before, da = after - pos;
    return (preferBefore ? db <= 2 * da : db <= da) ? before : after;
}

double nearestGridPoint (double pos, double firstBeat, double step)
{
    return step > 0.0 ? firstBeat + std::round ((pos - firstBeat) / step) * step : pos;
}

double tapTempo (const std::vector<double>& t)
{
    if (t.size() < 3)
        return 0.0;
    std::vector<double> d;
    for (size_t i = 1; i < t.size(); ++i)
        if (t[i] > t[i - 1])
            d.push_back (t[i] - t[i - 1]);
    if (d.size() < 2)
        return 0.0;
    std::nth_element (d.begin(), d.begin() + (std::ptrdiff_t) (d.size() / 2), d.end());
    const double m = d[d.size() / 2];
    return m > 0.0 ? 60.0 / m : 0.0;
}

namespace
{
    float thresholdFor (float sensitivity) noexcept
    {
        const float s = std::clamp (sensitivity, 0.0f, 1.0f);
        return 0.02f + 0.5f * (1.0f - s) * (1.0f - s);
    }

    /** The onsets kept at this sensitivity: above the threshold, 50 ms apart (the stronger wins). */
    std::vector<Analysis::Onset> pickOnsets (const Analysis& a, float sensitivity)
    {
        std::vector<Analysis::Onset> kept;
        const float thr = thresholdFor (sensitivity);
        std::vector<Analysis::Onset> c;
        for (const auto& o : a.onsets)
            if (o.strength >= thr)
                c.push_back (o);
        std::sort (c.begin(), c.end(), [] (auto& x, auto& y) { return x.strength > y.strength; });
        const auto gap = (int64_t) (0.05 * a.sampleRate);
        for (const auto& o : c)
        {
            bool free = true;
            for (const auto& k : kept)
                if (std::abs (k.sample - o.sample) < gap)
                {
                    free = false;
                    break;
                }
            if (free)
                kept.push_back (o);
        }
        std::sort (kept.begin(), kept.end(), [] (auto& x, auto& y) { return x.sample < y.sample; });
        return kept;
    }

    /** Sorted, snapped, merged (20 ms apart), at most maxSlices; boundaries [0, ..., end]. */
    std::vector<int64_t> finish (const Analysis& a, std::vector<int64_t> c, int64_t end)
    {
        const auto n = a.numSamples;
        end = std::clamp<int64_t> (end, 1, n);
        for (auto& p : c)
            p = nearestZeroCrossing (a.mono.data(), n, p, (int64_t) (0.005 * a.sampleRate), true);
        std::sort (c.begin(), c.end());
        const auto minGap = (int64_t) (0.02 * a.sampleRate);
        std::vector<int64_t> b { 0 };
        for (auto p : c)
            if (p - b.back() >= minGap && end - p >= minGap)
                b.push_back (p);
        if ((int) b.size() > Analysis::maxSlices)
        {
            end = b[(size_t) Analysis::maxSlices];
            b.resize ((size_t) Analysis::maxSlices);
        }
        b.push_back (end);
        return b;
    }
} // namespace

std::vector<int64_t> cuts (const Analysis& a, const SliceSettings& s)
{
    const auto n = a.numSamples;
    if (n <= 0)
        return { 0 };
    const double bpm = s.bpm > 0.0 ? s.bpm : a.bpm;
    const double fb = s.firstBeat >= 0.0 ? s.firstBeat : a.firstBeat;
    const int division = s.division == 4 || s.division == 8 ? s.division : 16;
    const double step = a.beatSamples (bpm) * 4.0 / division;

    std::vector<int64_t> c;
    if (s.mode == SliceMode::transients)
    {
        auto onsets = pickOnsets (a, s.sensitivity);
        if ((int) onsets.size() > Analysis::maxSlices - 1) // the strongest 63
        {
            std::sort (onsets.begin(), onsets.end(), [] (auto& x, auto& y) { return x.strength > y.strength; });
            onsets.resize ((size_t) Analysis::maxSlices - 1);
        }
        for (const auto& o : onsets)
            c.push_back (o.sample);
        return finish (a, c, n);
    }

    // the grid, from beat 1 both ways
    std::vector<double> grid;
    for (double p = fb - std::floor (fb / step) * step; p < (double) n; p += step)
        grid.push_back (p);
    const auto onsets = s.mode == SliceMode::mixed ? pickOnsets (a, s.sensitivity) : std::vector<Analysis::Onset> {};
    const auto window = 0.02 * a.sampleRate;
    for (double p : grid)
    {
        double q = p;
        if (s.mode == SliceMode::mixed)
        {
            double bestD = window;
            for (const auto& o : onsets)
                if (const double d = std::abs ((double) o.sample - p); d <= bestD)
                {
                    bestD = d;
                    q = (double) o.sample;
                }
        }
        c.push_back ((int64_t) std::llround (q));
    }
    return finish (a, c, n); // (at most the first 64 slices from the start)
}

std::vector<Slice> slicesFromCuts (const Analysis& a, std::vector<int64_t> b)
{
    std::vector<Slice> out;
    if (a.numSamples <= 0 || b.size() < 2)
        return out;
    const auto end = std::clamp<int64_t> (b.back(), 1, a.numSamples);
    b.pop_back();
    b.erase (b.begin()); // 0
    b = finish (a, b, end);
    for (size_t i = 0; i + 1 < b.size(); ++i)
    {
        Slice s;
        s.start = b[i];
        s.end = b[i + 1];
        const auto cues = attackAt (a, s.start);
        s.type = classify (a.mono.data(), s.start, s.end, a.sampleRate, &s.rootMidi, &cues);
        for (auto j = s.start; j < s.end; ++j)
            s.peak = std::max (s.peak, std::abs (a.mono[(size_t) j]));
        out.push_back (s);
    }
    return out;
}

std::vector<Slice> slice (const Analysis& a, const SliceSettings& s)
{
    return slicesFromCuts (a, cuts (a, s));
}

//==============================================================================
AttackCues attackAt (const Analysis& a, int64_t sample)
{
    AttackCues c;
    if (a.flux.empty() || a.hop <= 0)
        return c;
    const auto f = (int) std::lround ((double) sample / a.hop);
    for (int k = std::max (0, f - 1); k <= std::min ((int) a.flux.size() - 1, f + 3); ++k)
    {
        c.total = std::max (c.total, a.flux[(size_t) k]);
        c.low = std::max (c.low, a.lowFlux[(size_t) k]);
        c.high = std::max (c.high, a.highFlux[(size_t) k]);
    }
    return c;
}

SliceType classify (const float* x, int64_t start, int64_t end, double sr, float* rootMidi, const AttackCues* cues)
{
    if (rootMidi != nullptr)
        *rootMidi = -1.0f;
    const auto sliceLen = end - start;
    const auto len = std::min<int64_t> (sliceLen, (int64_t) (0.3 * sr));
    if (len < 64)
        return SliceType::noise;
    float peak = 0.0f;
    for (auto i = start; i < start + len; ++i)
        peak = std::max (peak, std::abs (x[i]));
    if (peak < 1.0e-4f)
        return SliceType::noise;

    // the average power spectrum of the first 300 ms
    const int fs = sr <= 50000.0 ? 1024 : 2048, fh = fs / 2;
    juce::dsp::FFT fft (orderFor (fs));
    const auto w = hann (fs);
    std::vector<float> buf ((size_t) fs * 2);
    std::vector<double> P ((size_t) fs / 2 + 1, 0.0);
    int frames = 0;
    for (int64_t f0 = start; f0 < start + std::max<int64_t> (len - fs, 0) + 1; f0 += fh, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < fs && f0 + i < start + len; ++i)
            buf[(size_t) i] = x[f0 + i] * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= fs / 2; ++k)
            P[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    const double bin = sr / fs;
    double total = 0.0, weighted = 0.0, low = 0.0, mid = 0.0, logSum = 0.0, linSum = 0.0;
    int flatBins = 0;
    for (int k = 1; k <= fs / 2; ++k)
    {
        const double f = k * bin, p = P[(size_t) k] + 1.0e-12;
        total += p;
        weighted += p * f;
        if (f < 150.0)
            low += p;
        if (f >= 300.0 && f <= 3400.0)
            mid += p;
        if (f >= 200.0 && f <= std::min (10000.0, sr * 0.45))
        {
            logSum += std::log (p);
            linSum += p;
            ++flatBins;
        }
    }
    const double centroid = weighted / total, lowRatio = low / total, midRatio = mid / total;
    const double flatness = flatBins > 0 ? std::exp (logSum / flatBins) / (linSum / flatBins) : 1.0;

    // decay: the 50 ms after the peak against 80..200 ms later (short slices: their last third)
    int64_t pk = start;
    for (auto i = start; i < start + std::min<int64_t> (sliceLen, (int64_t) (0.03 * sr)); ++i)
        if (std::abs (x[i]) > std::abs (x[pk]))
            pk = i;
    auto rms = [&] (int64_t a, int64_t b)
    {
        a = std::max (a, start);
        b = std::min (b, end);
        double s = 0.0;
        for (auto i = a; i < b; ++i)
            s += (double) x[i] * x[i];
        return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
    };
    const double e1 = rms (pk, pk + (int64_t) (0.05 * sr));
    double e2 = rms (pk + (int64_t) (0.08 * sr), pk + (int64_t) (0.2 * sr));
    if (pk + (int64_t) (0.12 * sr) > end)
        e2 = rms (end - sliceLen / 3, end);
    const double decay = e2 / (e1 + 1.0e-9);
    // a hard attack in a full mix counts as a hit even if the rest sustains
    const bool hit = cues != nullptr && cues->total > 0.4f;
    const bool percussive = decay < 0.45 || hit;

    // pitch: YIN at 3 points (how stable, and the root)
    const int yw = (int) std::min<int64_t> ((int64_t) (0.05 * sr), sliceLen);
    double pitches[3] {}, aper[3] { 1, 1, 1 };
    int np = 0;
    for (double t : { 0.03, 0.09, 0.15 })
    {
        const auto at0 = start + (int64_t) (t * sr);
        if (at0 + yw > end || yw < (int) (sr / 55.0) * 2)
            break;
        const auto [f, ap] = yin (x + at0, yw, sr);
        pitches[np] = f;
        aper[np] = ap;
        ++np;
    }
    if (np == 0 && yw >= (int) (sr / 55.0) * 2)
    {
        const auto [f, ap] = yin (x + start, yw, sr);
        pitches[0] = f;
        aper[0] = ap;
        np = 1;
    }
    double meanAp = 0.0;
    for (int i = 0; i < np; ++i)
        meanAp += aper[i];
    meanAp = np > 0 ? meanAp / np : 1.0;
    const bool harmonic = np > 0 && meanAp < 0.2;
    double spread = 0.0; // semitones, max - min
    if (np >= 2)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (int i = 0; i < np; ++i)
        {
            const double m = 12.0 * std::log2 (std::max (pitches[i], 1.0) / 440.0);
            lo = std::min (lo, m);
            hi = std::max (hi, m);
        }
        spread = hi - lo;
    }
    auto setRoot = [&]
    {
        if (rootMidi != nullptr && np > 0 && pitches[0] > 20.0)
            *rootMidi = (float) (69.0 + 12.0 * std::log2 (pitches[0] / 440.0));
    };

    if (percussive)
    {
        if ((lowRatio > 0.35 && centroid < 1500.0) || (hit && cues->low > 0.5f))
            return SliceType::kick;
        if ((centroid > 5000.0 && lowRatio < 0.08) || (hit && cues->high > 0.45f && cues->low < 0.25f && cues->high > 1.6f * cues->total - 0.3f))
            return SliceType::hat;
        if (flatness > 0.1 || (hit && cues->high > 0.45f))
            return SliceType::snare;
        if (harmonic)
        {
            setRoot();
            return SliceType::tonal;
        }
        return SliceType::snare;
    }
    if (harmonic || flatness < 0.06)
    {
        setRoot();
        const double f0 = pitches[0];
        if (harmonic && f0 > 80.0 && f0 < 1000.0 && midRatio > 0.45 && spread > 0.15 && spread < 6.0)
            return SliceType::voice;
        return SliceType::tonal;
    }
    if (centroid > 6000.0 && lowRatio < 0.05)
        return SliceType::hat;
    return SliceType::noise;
}

std::vector<float> renderSlice (const float* x, int64_t n, const Slice& s, double sampleRate, double fadeSeconds)
{
    const auto a = std::clamp<int64_t> (s.start, 0, n), b = std::clamp<int64_t> (s.end, a, n);
    std::vector<float> out (x + a, x + b);
    const auto len = (int64_t) out.size();
    const auto fade = std::min<int64_t> ((int64_t) std::lround (fadeSeconds * sampleRate), len / 2);
    for (int64_t i = 0; i < fade; ++i)
    {
        const float g = (float) (0.5 - 0.5 * std::cos (pi * (double) i / (double) fade));
        out[(size_t) i] *= g;
        out[(size_t) (len - 1 - i)] *= g;
    }
    return out;
}
} // namespace daftchunk
