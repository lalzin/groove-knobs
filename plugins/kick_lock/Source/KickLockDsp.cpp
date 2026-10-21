#include "KickLockDsp.h"

namespace kl
{
using namespace gk::dspmath;

KickLockDsp::KickLockDsp()
{
    for (int sh = 0; sh < 4; ++sh)
    {
        const auto nodes = presetCurve (sh);
        for (int i = 0; i <= curveTableSize; ++i)
            presetTables[(size_t) sh][(size_t) i] = evalCurve (nodes, (float) i / (float) curveTableSize);
    }
    setCustomCurve (presetCurve (exponential));
}

std::vector<KickLockDsp::CurveNode> KickLockDsp::presetCurve (int s)
{
    switch (s)
    {
        case linear: return { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        case sCurve: return { { 0.0f, 0.0f, 2.2f }, { 0.5f, 0.5f, -2.2f }, { 1.0f, 1.0f, 0.0f } };
        // up to full at 55 %, a small dip (to 70 %) and back
        case bounce: return { { 0.0f, 0.0f, 2.2f }, { 0.275f, 0.5f, -2.2f }, { 0.55f, 1.0f, 1.2f }, { 0.775f, 0.7f, -1.2f }, { 1.0f, 1.0f, 0.0f } };
        // 1 - exp(-3x), normalised: one segment bent by -3 is exactly that
        case exponential:
        default: return { { 0.0f, 0.0f, -3.0f }, { 1.0f, 1.0f, 0.0f } };
    }
}

float KickLockDsp::evalCurve (const std::vector<CurveNode>& n, float x) noexcept
{
    if (n.empty())
        return 1.0f;
    if (x <= n.front().x)
        return n.front().y;
    for (size_t i = 0; i + 1 < n.size(); ++i)
        if (x <= n[i + 1].x)
        {
            const float w = n[i + 1].x - n[i].x;
            const float t = w > 1.0e-6f ? (x - n[i].x) / w : 1.0f;
            const float k = n[i].tension;
            const float f = std::abs (k) < 1.0e-3f ? t : (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
            return n[i].y + (n[i + 1].y - n[i].y) * f;
        }
    return n.back().y;
}

void KickLockDsp::setCustomCurve (const std::vector<CurveNode>& nodes)
{
    const int next = (customPublished.load (std::memory_order_acquire) + 1) % 3;
    auto& table = customTables[(size_t) next];
    for (int i = 0; i <= curveTableSize; ++i)
        table[(size_t) i] = juce::jlimit (0.0f, 1.0f, evalCurve (nodes, (float) i / (float) curveTableSize));
    customPublished.store (next, std::memory_order_release);
}

double KickLockDsp::releaseBeats (int index) noexcept
{
    static const double beats[] { 0.25, 0.75, 0.5, 1.0, 2.0 }; // 1/16, 1/8 dotted, 1/8, 1/4, 1/2
    return beats[juce::jlimit (0, 4, index)];
}

void KickLockDsp::designKernel (std::vector<float>& k, double cutoff, double sr)
{
    const int N = (int) k.size(), D = N / 2;
    // Kaiser, 50 dB stopband: transition (A - 7.95) / (2.285 (N - 1)) x sr / 2 pi
    // (~180 Hz at 48 kHz, Blackman's was ~340): the narrower, the fewer
    // harmonics split between the shifted sub and the mids (a split harmonic
    // combs with itself when the sub moves). Centred above the cutoff, so that
    // everything below it (the fundamental) passes entirely
    constexpr double attenuation = 50.0;
    const double transition = (attenuation - 7.95) / (2.285 * (N - 1)) * sr / (2.0 * juce::MathConstants<double>::pi);
    const double fc = (cutoff + 0.5 * transition) / sr;
    auto i0 = [] (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int j = 1; j < 30; ++j)
        {
            term *= (x / (2.0 * j)) * (x / (2.0 * j));
            sum += term;
        }
        return sum;
    };
    const double b = 0.5842 * std::pow (attenuation - 21.0, 0.4) + 0.07886 * (attenuation - 21.0); // Kaiser's beta
    double sum = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const int m = n - D;
        const double sinc = m == 0 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
        const double r = (double) m / D;
        const double w = i0 (b * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / i0 (b);
        k[(size_t) n] = (float) (sinc * w);
        sum += sinc * w;
    }
    for (auto& v : k)
        v = (float) (v / sum); // unity at DC
}

float KickLockDsp::fir (int c, int kernel) const noexcept
{
    const float* h = firHistory[(size_t) c].data() + firPos; // the last N inputs, oldest first
    const float* k = kernels[(size_t) kernel].data();
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    int j = 0;
    for (; j + 4 <= firLength; j += 4)
    {
        a0 += h[j] * k[j];
        a1 += h[j + 1] * k[j + 1];
        a2 += h[j + 2] * k[j + 2];
        a3 += h[j + 3] * k[j + 3];
    }
    for (; j < firLength; ++j)
        a0 += h[j] * k[j];
    return (a0 + a1) + (a2 + a3);
}

double KickLockDsp::wrap180 (double d) noexcept
{
    d = std::fmod (d + 180.0, 360.0);
    if (d < 0.0)
        d += 360.0;
    return d - 180.0;
}

void KickLockDsp::prepare (double sr, int maxBlock, int nch)
{
    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, nch);
    firDelay = (int) std::round (firSeconds * sr);
    firLength = 2 * firDelay + 1;
    linkSlack = linkSlackFor (sr, maxBlock);
    latency = latencyFor (sr, maxBlock);
    for (int k = 0; k < 2; ++k)
    {
        kernels[(size_t) k].assign ((size_t) firLength, 0.0f);
        firHistory[(size_t) k].assign ((size_t) (2 * firLength), 0.0f);
    }
    designKernel (kernels[0], subCutoff, sr);
    kernelCutoff = { subCutoff, -1.0f };
    activeKernel = 0;
    kernelFadeLength = juce::jmax (1, (int) std::round (kernelFadeSeconds * sr));
    std::int64_t size = 1;
    while (size < (std::int64_t) ((maxDelaySeconds + 0.6) * sr))
        size <<= 1;
    mask = size - 1;
    for (int c = 0; c < 2; ++c)
    {
        input[(size_t) c].assign ((size_t) size, 0.0f);
        sub[(size_t) c].assign ((size_t) size, 0.0f);
        fund[(size_t) c].assign ((size_t) size, 0.0f);
        fundQ[(size_t) c].assign ((size_t) size, 0.0f);
        prefixRe[(size_t) c].assign ((size_t) size, 0.0);
        prefixIm[(size_t) c].assign ((size_t) size, 0.0);
    }
    oscRingRe.assign ((size_t) size, 1.0);
    oscRingIm.assign ((size_t) size, 0.0);
    lookahead = latency - 3; // Hermite reads 2 samples past the output time
    trackGainCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    noteFastAtt = (float) (1.0 - std::exp (-1.0 / (0.0005 * sr)));
    noteFastRel = (float) (1.0 - std::exp (-1.0 / (0.04 * sr))); // holds over a cycle of a low bass (a saw at 44 Hz dips every 22 ms)
    noteLag = juce::jlimit (1, 4000, (int) std::round (0.025 * sr));
    notePeakDecay = (float) std::exp (-1.0 / (1.0 * sr));
    mono.assign ((size_t) size, 0.0f);
    envRing.assign ((size_t) size, 0.0f);
    kickRing.assign ((size_t) size, 0.0f);
    decimation = juce::jmax (1, (int) std::round (sr / 3000.0));
    dec.assign (1024, 0.0f);
    fadeLength = juce::jmax (1, (int) std::round (crossfadeSeconds * sr));
    scAttack = (float) (1.0 - std::exp (-1.0 / (0.0002 * sr)));
    scRelease = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
    slowRelease = (float) (1.0 - std::exp (-1.0 / (0.06 * sr)));
    rmsCoeff = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
    subPeakDecay = (float) std::exp (-1.0 / (1.0 * sr));
    telemetry.scopeSize = (int) (2.0 * sr) / KickTelemetry::scopeDecimation;
    telemetry.scopeSub = std::make_unique<std::atomic<float>[]> ((size_t) telemetry.scopeSize);
    telemetry.scopeBass = std::make_unique<std::atomic<float>[]> ((size_t) telemetry.scopeSize);
    telemetry.scopeKick = std::make_unique<std::atomic<float>[]> ((size_t) telemetry.scopeSize);
    reset();
}

void KickLockDsp::reset()
{
    for (auto& h : firHistory)
        std::fill (h.begin(), h.end(), 0.0f);
    firPos = 0;
    kernelFade = 0;
    pendingCount = 0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (input[(size_t) c].begin(), input[(size_t) c].end(), 0.0f);
        std::fill (sub[(size_t) c].begin(), sub[(size_t) c].end(), 0.0f);
        std::fill (fund[(size_t) c].begin(), fund[(size_t) c].end(), 0.0f);
        std::fill (fundQ[(size_t) c].begin(), fundQ[(size_t) c].end(), 0.0f);
        std::fill (prefixRe[(size_t) c].begin(), prefixRe[(size_t) c].end(), 0.0);
        std::fill (prefixIm[(size_t) c].begin(), prefixIm[(size_t) c].end(), 0.0);
        sumRe[(size_t) c] = sumIm[(size_t) c] = 0.0;
    }
    std::fill (oscRingRe.begin(), oscRingRe.end(), 1.0);
    std::fill (oscRingIm.begin(), oscRingIm.end(), 0.0);
    trackGain = 0.0f;
    oscRe = 1.0;
    oscIm = 0.0;
    noteHz = 0.0;
    setTrackHz (55.0);
    std::fill (mono.begin(), mono.end(), 0.0f);
    std::fill (envRing.begin(), envRing.end(), 0.0f);
    std::fill (kickRing.begin(), kickRing.end(), 0.0f);
    std::fill (dec.begin(), dec.end(), 0.0f);
    clock = 0;
    scEnv = slowEnv = 0.0f;
    armed = true;
    lastKick = -1000000;
    lastSidechainKick = -1000000;
    lastLinkKick = -1000000;
    linkPendingCount = 0;
    stretchCount = linkWaitingCount = 0;
    lateKicks = 0;
    noteFast = notePeak = 0.0f;
    std::fill (noteRing.begin(), noteRing.end(), 0.0f);
    noteDeadUntil = -1;
    lastNoteStart = -1000000;
    noteOnsets = 0;
    beatValid = false;
    deadUntil = -1;
    kickCount = 0;
    decCounter = 0;
    yinHop = 0;
    decWrite = 0;
    periodSamples = cleanPeriod = 0.0;
    pitchHoldUntil = pitchHoldNote = -1;
    yinAperiodicity = 1.0f;
    recentCount = 0;
    psiNow = psiFrom = psiTo = 0.0;
    fadeStart = -1;
    holdEnd = 0;
    prevKick = -1;
    returnLength = 1.0;
    lastPhase = lastConfidence = 0.0;
    subRms = midRms = sideRms = lrRms = 0.0f;
    subPeak = 0.0f;
    scopeCounter = 0;
    for (int i = 0; i < telemetry.scopeSize; ++i)
    {
        telemetry.scopeSub[(size_t) i].store (0.0f, std::memory_order_relaxed);
        telemetry.scopeBass[(size_t) i].store (0.0f, std::memory_order_relaxed);
        telemetry.scopeKick[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
}

void KickLockDsp::setKnobs (const float* v, int n)
{
    targetPhase = n > targetKnob ? juce::jlimit (0.0f, 360.0f, v[targetKnob]) : 0.0f;
    offsetMs = n > offsetKnob ? juce::jlimit (-20.0f, 20.0f, v[offsetKnob]) : 0.0f;
    subCutoff = n > subCutKnob ? juce::jlimit (80.0f, 150.0f, v[subCutKnob]) : 120.0f;
    depth = n > depthKnob ? clamp01 (v[depthKnob]) : 1.0f;
    waveStart = n > waveStartKnob ? juce::jlimit (0.0f, 360.0f, v[waveStartKnob]) : 0.0f;
    thresholdDb = n > thresholdKnob ? juce::jlimit (-40.0f, 0.0f, v[thresholdKnob]) : -18.0f;
}

void KickLockDsp::setChoices (const int* idx, int n)
{
    shape = n > shapeChoice ? juce::jlimit (0, (int) custom, idx[shapeChoice]) : exponential;
    release = n > releaseChoice ? juce::jlimit (0, 4, idx[releaseChoice]) : 2;
    lockMode = n > modeChoice ? juce::jlimit (0, 1, idx[modeChoice]) : continuous;
    trigger = n > triggerChoice ? juce::jlimit (0, 3, idx[triggerChoice]) : triggerAuto;
    linkChannel = n > linkChoice ? juce::jlimit (0, KickLinkMemory::channels - 1, idx[linkChoice]) : 1;
}

void KickLockDsp::setSidechain (const float* const* channels, int n, int)
{
    scChannels = juce::jlimit (0, 2, n);
    for (int c = 0; c < scChannels; ++c)
        sc[(size_t) c] = channels[c];
}

void KickLockDsp::setTrackHz (double hz) noexcept
{
    // the reference oscillator (its phase carries on)
    trackHz = hz;
    const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
    rotRe = std::cos (w);
    rotIm = std::sin (w);
}

void KickLockDsp::retrack (std::int64_t from, double T) noexcept
{
    // the fundamental's tracking, from the note's start on, redone at the
    // period measured at the kick: what is not heard yet is extracted at the
    // right frequency (YIN still held the note before or a guess, its window
    // was not yet in the note) — the kick's rotation turns the whole
    // fundamental, not part of it
    periodSamples = T;
    setTrackHz (sampleRate / T);
    const auto k0 = (size_t) ((from - 1) & mask);
    double re = oscRingRe[k0], im = oscRingIm[k0];
    std::array<double, 2> sRe {}, sIm {};
    for (int c = 0; c < numChannels; ++c)
    {
        sRe[(size_t) c] = prefixRe[(size_t) c][k0];
        sIm[(size_t) c] = prefixIm[(size_t) c][k0];
    }
    for (auto t = from; t <= clock; ++t)
    {
        const double nr = re * rotRe - im * rotIm;
        im = re * rotIm + im * rotRe;
        re = nr;
        const auto k = (size_t) (t & mask);
        oscRingRe[k] = re;
        oscRingIm[k] = im;
        for (int c = 0; c < numChannels; ++c)
        {
            const double x = input[(size_t) c][k];
            sRe[(size_t) c] += x * re;
            sIm[(size_t) c] -= x * im;
            prefixRe[(size_t) c][k] = sRe[(size_t) c];
            prefixIm[(size_t) c][k] = sIm[(size_t) c];
        }
    }
    const double m = 1.0 / std::sqrt (re * re + im * im);
    oscRe = re * m;
    oscIm = im * m;
    for (int c = 0; c < numChannels; ++c)
    {
        sumRe[(size_t) c] = sRe[(size_t) c];
        sumIm[(size_t) c] = sIm[(size_t) c];
    }
}

void KickLockDsp::updatePitch() noexcept
{
    if (clock < pitchHoldUntil && lastNoteStart == pitchHoldNote)
        return; // a young note's period, measured at its kick: YIN's window still holds the note before
    // YIN on the decimated sub (~3 kHz): 35-160 Hz, 128-sample window (~43 ms)
    const double rate = sampleRate / decimation;
    // (under ~48 Hz 256 at ~3 kHz = 85 ms: almost 4 periods at 44 Hz — with
    // 128, under two, a low bass looked aperiodic and unsteady: UNSTABLE on
    // Alan's bass; above, 128: a glide is followed sooner)
    const int window = periodSamples / decimation > 62.0 ? 256 : 128, tauMin = (int) std::floor (rate / 160.0), tauMax = juce::jmin (180, (int) std::ceil (rate / 35.0));
    std::array<float, 1024> frame {};
    const int len = window + tauMax;
    double energy = 0.0;
    for (int i = 0; i < len; ++i)
    {
        frame[(size_t) i] = dec[(size_t) ((decWrite - len + i) & 1023)];
        energy += (double) frame[(size_t) i] * frame[(size_t) i];
    }
    // the bass silent (between two notes, 20 dB under its recent level): keep
    // the last period — guessing in the silence moved it by up to 4.5 %, and
    // the next note's first cycle was measured at the wrong frequency
    if (subPeak > 1.0e-7 && energy / len < 0.01 * subPeak)
        return;
    std::array<float, 200> d {};
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double e = frame[(size_t) j] - frame[(size_t) (j + tau)];
            s += e * e;
        }
        running += s;
        d[(size_t) tau] = running > 0.0 ? (float) (s * tau / running) : 1.0f;
    }
    int best = -1;
    float bestValue = 1.0e9f;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (d[(size_t) tau] < 0.2f)
        {
            while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            best = tau;
            bestValue = d[(size_t) tau];
            break;
        }
        if (d[(size_t) tau] < bestValue)
        {
            bestValue = d[(size_t) tau];
            best = tau;
        }
    }
    yinAperiodicity = bestValue;
    if (best <= 1 || best >= tauMax || bestValue > 0.4f)
        return; // keep the last period, the confidence will say how good it is
    const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2.0 * b + c;
    const double fine = best + (std::abs (den) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0);
    periodSamples = fine * decimation;
    if (bestValue < 0.15f)
        cleanPeriod = periodSamples; // a note held steadily (not its end, not an attack)
    recentPeriods[(size_t) (recentCount % (int) recentPeriods.size())] = periodSamples;
    ++recentCount;
}

void KickLockDsp::measureForward (std::int64_t at, double T, int length, double& phaseDeg, double& amplitude) const noexcept
{
    // the bass itself (the input ring, mono), from the kick on: its
    // fundamental and next harmonics fitted together (least squares) at 1 / T.
    // Over a whole period that is the plain projection on f0 (every harmonic
    // cancels); over less (a note under ~40 Hz, the room before the kick is
    // heard) the harmonics no longer cancel and would pull the phase
    constexpr int H = 3, M = 2 * H;
    double G[M][M] {}, r[M] {}, basis[M];
    const double w = juce::MathConstants<double>::twoPi / T, cw = std::cos (w), sw = std::sin (w);
    double c1 = 1.0, s1 = 0.0;
    for (int k = 0; k < length; ++k)
    {
        float x = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            x += input[(size_t) c][(size_t) ((at + k) & mask)];
        x /= (float) numChannels;
        double ch = c1, sh = s1;
        for (int h = 0; h < H; ++h)
        {
            basis[2 * h] = ch;     // cos (h w k)
            basis[2 * h + 1] = sh; // sin (h w k)
            const double nc = ch * c1 - sh * s1;
            sh = sh * c1 + ch * s1;
            ch = nc;
        }
        for (int p = 0; p < M; ++p)
        {
            r[p] += basis[p] * x;
            for (int q = 0; q <= p; ++q)
                G[p][q] += basis[p] * basis[q];
        }
        const double nc = c1 * cw - s1 * sw;
        s1 = s1 * cw + c1 * sw;
        c1 = nc;
    }
    // Cholesky, then the coefficients (a little ridge: a short window makes
    // the harmonics nearly dependent)
    double L[M][M] {}, y[M] {}, coef[M] {};
    for (int p = 0; p < M; ++p)
        for (int q = 0; q <= p; ++q)
        {
            double sum = G[p][q] + (p == q ? 1.0e-6 * length : 0.0);
            for (int k = 0; k < q; ++k)
                sum -= L[p][k] * L[q][k];
            L[p][q] = p == q ? std::sqrt (juce::jmax (sum, 1.0e-12)) : sum / L[q][q];
        }
    for (int p = 0; p < M; ++p)
    {
        double sum = r[p];
        for (int k = 0; k < p; ++k)
            sum -= L[p][k] * y[k];
        y[p] = sum / L[p][p];
    }
    for (int p = M - 1; p >= 0; --p)
    {
        double sum = y[p];
        for (int k = p + 1; k < M; ++k)
            sum -= L[k][p] * coef[k];
        coef[p] = sum / L[p][p];
    }
    phaseDeg = std::atan2 (coef[0], coef[1]) * 180.0 / juce::MathConstants<double>::pi;
    amplitude = std::hypot (coef[0], coef[1]);
}

double KickLockDsp::differenceAt (std::int64_t from, std::int64_t to, double T) const noexcept
{
    // how far the note is from repeating itself after T (0 = exactly, 1 = no
    // relation): checks a period known from before on a note too young to
    // measure its own
    const int lag = (int) std::round (T), n = (int) (to - from) - lag;
    if (lag < 2 || n < 16)
        return 1.0;
    double diff = 0.0, energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f, y = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            x += input[(size_t) c][(size_t) ((from + i) & mask)];
            y += input[(size_t) c][(size_t) ((from + i + lag) & mask)];
        }
        diff += (double) (x - y) * (x - y);
        energy += (double) x * x + (double) y * y;
    }
    return energy > 0.0 ? diff / energy : 1.0;
}

double KickLockDsp::fitPeriod (const float* x, int n, double rate, double T0) noexcept
{
    // least squares: x ~ sum of cos / sin of the first harmonics (up to
    // ~400 Hz, at most 6) at 1 / T; the T leaving the least residual, by a
    // golden-section search within 4 % of T0 (one minimum there: the window
    // is longer than a period)
    const int H = juce::jlimit (1, 6, (int) std::floor (400.0 * T0 / rate));
    auto residual = [&] (double T)
    {
        constexpr int M = 12;
        const int m = 2 * H;
        double G[M][M] {}, r[M] {}, basis[M];
        double energy = 0.0;
        const double w = juce::MathConstants<double>::twoPi / T, cw = std::cos (w), sw = std::sin (w);
        double c1 = 1.0, s1 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double ch = c1, sh = s1;
            for (int h = 0; h < H; ++h)
            {
                basis[2 * h] = ch;
                basis[2 * h + 1] = sh;
                const double nc = ch * c1 - sh * s1;
                sh = sh * c1 + ch * s1;
                ch = nc;
            }
            const double v = x[i];
            energy += v * v;
            for (int p = 0; p < m; ++p)
            {
                r[p] += basis[p] * v;
                for (int q = 0; q <= p; ++q)
                    G[p][q] += basis[p] * basis[q];
            }
            const double nc = c1 * cw - s1 * sw;
            s1 = s1 * cw + c1 * sw;
            c1 = nc;
        }
        // Cholesky (a little ridge: harmonics nearly dependent on a short window)
        double L[M][M] {};
        for (int p = 0; p < m; ++p)
            for (int q = 0; q <= p; ++q)
            {
                double sum = G[p][q] + (p == q ? 1.0e-6 * n : 0.0);
                for (int k = 0; k < q; ++k)
                    sum -= L[p][k] * L[q][k];
                L[p][q] = p == q ? std::sqrt (juce::jmax (sum, 1.0e-12)) : sum / L[q][q];
            }
        double y[M] {}, projected = 0.0;
        for (int p = 0; p < m; ++p)
        {
            double sum = r[p];
            for (int k = 0; k < p; ++k)
                sum -= L[p][k] * y[k];
            y[p] = sum / L[p][p];
            projected += y[p] * y[p];
        }
        return energy - projected;
    };
    double lo = T0 * 0.96, hi = T0 * 1.04;
    constexpr double g = 0.6180339887498949;
    double p1 = hi - g * (hi - lo), p2 = lo + g * (hi - lo), r1 = residual (p1), r2 = residual (p2);
    for (int it = 0; it < 28; ++it)
    {
        if (r1 < r2)
        {
            hi = p2; p2 = p1; r2 = r1;
            p1 = hi - g * (hi - lo); r1 = residual (p1);
        }
        else
        {
            lo = p1; p1 = p2; r1 = r2;
            p2 = lo + g * (hi - lo); r2 = residual (p2);
        }
    }
    return 0.5 * (lo + hi);
}

double KickLockDsp::periodBetween (std::int64_t from, std::int64_t to) const noexcept
{
    constexpr int D = 4;
    std::array<float, 2048> x {};
    const int n = (int) juce::jmin<std::int64_t> ((to - from) / D, (std::int64_t) x.size());
    for (int i = 0; i < n; ++i)
    {
        float acc = 0.0f;
        for (int j = 0; j < D; ++j)
            for (int c = 0; c < numChannels; ++c)
                acc += input[(size_t) c][(size_t) ((from + i * D + j) & mask)];
        x[(size_t) i] = acc / (float) (D * numChannels);
    }
    const double rate = sampleRate / D;
    // a young note: as long a lag as the data allows (a fifth of it left to
    // compare: enough for a clean synth note, not a guess)
    const int tauMin = juce::jmax (2, (int) std::floor (rate / 160.0)), tauMax = juce::jmin (n - juce::jmax (8, n / 5), (int) std::ceil (rate / 35.0));
    const int window = n - tauMax;
    if (tauMax <= tauMin + 2 || window < 8)
        return 0.0;
    std::array<float, 1100> d {};
    double running = 0.0;
    for (int tau = 1; tau <= tauMax && tau < (int) d.size(); ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double e = x[(size_t) i] - x[(size_t) (i + tau)];
            sum += e * e;
        }
        running += sum;
        d[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
    }
    for (int tau = tauMin; tau < tauMax - 1; ++tau)
        if (d[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax - 1 && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            // then refined: the note fitted as a periodic wave (its first
            // harmonics) — a short window's lag estimate is 1-3 % short (the
            // note growing, less than two periods to compare)
            const double a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1], den = a - 2.0 * b + c;
            const double coarse = (tau + (std::abs (den) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0)) * D;
            return fitPeriod (x.data(), n, rate, coarse / D) * D;
        }
    return 0.0;
}

int KickLockDsp::measureLength() const noexcept
{
    // all the room left before the kick is heard (its crossfade done): the
    // cycle after the kick is measured on min (one period, this)
    const int room = latency - linkSlack - fadeLength - (int) std::round (0.0015 * sampleRate);
    return juce::jmax (16, room);
}

double KickLockDsp::targetPsiFor (double phaseDeg, double T) const noexcept
{
    // + = later. Lock: the rotation that brings the fundamental to the
    // target phase at the kick; WAVE START and OFFSET added on top (deliberate)
    const double base = waveStart + offsetMs * 0.001 * sampleRate / T * 360.0;
    return base + (lock ? wrap180 (phaseDeg - targetPhase) : 0.0);
}

void KickLockDsp::rampTo (double psi, std::int64_t holdUntil, double returnSamples, int fadeSamples) noexcept
{
    // the nearest equivalent turn (never more than half a turn to go)
    psi = psiNow + wrap180 (psi - psiNow);
    holdEnd = holdUntil;
    returnLength = juce::jmax (1.0, returnSamples);
    if (std::abs (psi - psiNow) < 0.05)
    {
        psiTo = psi;
        return;
    }
    psiFrom = psiNow;
    psiTo = psi;
    fadeStart = clock;
    fadeNow = fadeSamples > 0 ? fadeSamples : fadeLength;
}

double KickLockDsp::releaseSamplesNow() const noexcept
{
    return juce::jmax (1.0, releaseBeats (release) * 60.0 / bpm * sampleRate);
}

std::int64_t KickLockDsp::releaseFor (std::int64_t) const noexcept
{
    return (std::int64_t) releaseSamplesNow();
}

double KickLockDsp::returnFor (std::int64_t when) const noexcept
{
    // back to the original bass between the end of the release and the next
    // kick (expected one interval later), 30 ms ahead of it: 60..400 ms
    const double interval = prevKick >= 0 && when - prevKick < (std::int64_t) (2.0 * sampleRate) && when > prevKick
                                ? (double) (when - prevKick) : 60.0 / bpm * sampleRate;
    return juce::jlimit (0.06 * sampleRate, 0.4 * sampleRate, interval - releaseSamplesNow() - 0.03 * sampleRate);
}

void KickLockDsp::scheduleLink (std::int64_t onset)
{
    // once per kick (±30 ms)
    const auto near = (std::int64_t) (0.03 * sampleRate);
    for (int n = 0; n < linkPendingCount; ++n)
        if (std::abs (linkPending[(size_t) n] - onset) < near)
            return;
    if (std::abs (onset - lastLinkKick) < near || linkPendingCount >= (int) linkPending.size())
        return;
    linkPending[(size_t) linkPendingCount++] = onset;
}

bool KickLockDsp::placeLinkKick (double ppq, std::int64_t& onset) const noexcept
{
    // the newest stretch of the song holding this position, near now (an
    // older loop pass holds it too, seconds ago: not that one); a stretch
    // left by a jump still reaches 0.3 s past its end (the kick's track not
    // yet jumped when its kick was found)
    const auto near = (std::int64_t) (0.3 * sampleRate);
    const int n = juce::jmin (stretchCount, (int) stretches.size());
    for (int k = 0; k < n; ++k)
    {
        const auto& st = stretches[(size_t) ((stretchCount - 1 - k) % (int) stretches.size())];
        const double before = 0.01 * sampleRate * st.bps, after = (k == 0 ? 0.01 : 0.3) * sampleRate * st.bps;
        const double end = st.ppq0 + (double) st.length * st.bps;
        if (ppq < st.ppq0 - before || ppq > end + after)
            continue;
        const auto at = st.clock0 + (std::int64_t) std::llround ((ppq - st.ppq0) / st.bps);
        if (clock - at < near && at - clock < near)
        {
            onset = at;
            return true;
        }
    }
    return false;
}

void KickLockDsp::pollLink (const gk::BlockTiming& timing, int numSamples)
{
    telemetry.hostPlaying.store (timing.isPlaying, std::memory_order_relaxed);
    const bool wanted = trigger == triggerLink || trigger == triggerAuto;
    if (! wanted || ! timing.isPlaying || timing.beatsPerSample <= 0.0)
    {
        telemetry.linkSlot.store (wanted ? link.poll (linkChannel, [] (double, float) {}) : -1, std::memory_order_relaxed);
        stretchCount = linkWaitingCount = 0;
        return;
    }
    const double start = timing.beatsAtStart, bps = timing.beatsPerSample;
    // this block on the song: the same stretch as the last one if it follows
    // it (same tempo, no jump), else a new one
    if (stretchCount > 0)
    {
        auto& last = stretches[(size_t) ((stretchCount - 1) % (int) stretches.size())];
        if (last.clock0 + last.length == clock && juce::exactlyEqual (last.bps, bps)
            && std::abs (last.ppq0 + (double) last.length * last.bps - start) < 0.5 * bps)
            last.length += numSamples;
        else
            stretches[(size_t) (stretchCount++ % (int) stretches.size())] = { clock, numSamples, start, bps };
    }
    else
        stretches[(size_t) (stretchCount++ % (int) stretches.size())] = { clock, numSamples, start, bps };
    // a kick can come up to (latency - the crossfade) after its own time and
    // still be followed exactly (its cycle measured, the crossfade over before
    // it is heard); later (should not happen), it still ducks, late
    const auto budget = (std::int64_t) (latency - fadeLength - (int) std::round (0.0015 * sampleRate));
    auto take = [&] (std::int64_t onset)
    {
        if (clock - onset > budget)
            ++lateKicks;
        scheduleLink (onset);
    };
    // kicks not placed yet (the kick's track jumped back before this one): again
    for (int n = 0; n < linkWaitingCount;)
    {
        std::int64_t onset = 0;
        if (placeLinkKick (linkWaiting[(size_t) n].ppq, onset))
            take (onset);
        else if (clock < linkWaiting[(size_t) n].until)
        {
            ++n;
            continue;
        }
        linkWaiting[(size_t) n] = linkWaiting[(size_t) --linkWaitingCount];
    }
    const int slot = link.poll (linkChannel, [&] (double ppq, float)
    {
        telemetry.linkKicks.fetch_add (1, std::memory_order_relaxed);
        std::int64_t onset = 0;
        if (placeLinkKick (ppq, onset))
            take (onset);
        else if (linkWaitingCount < (int) linkWaiting.size())
            linkWaiting[(size_t) linkWaitingCount++] = { ppq, clock + (std::int64_t) (0.3 * sampleRate) };
    });
    telemetry.linkSlot.store (slot, std::memory_order_relaxed);
}

void KickLockDsp::addTrigger (std::int64_t onset)
{
    // the ducking knows it now; its phase is measured once the sub is known
    // there (8 ms later)
    kicks[(size_t) (kickCount % (int) kicks.size())] = onset;
    ++kickCount;
    lastKick = onset;
    if (pendingCount < (int) pendingKicks.size())
        pendingKicks[(size_t) pendingCount++] = onset;
}

void KickLockDsp::onKick (std::int64_t when)
{
    const int count = telemetry.kickEvents.load (std::memory_order_relaxed) + 1;
    telemetry.recentKicks[(size_t) (count % 4)].store (when, std::memory_order_relaxed);
    telemetry.lastKickSample.store (when, std::memory_order_relaxed);
    telemetry.kickEvents.store (count, std::memory_order_release);

    // The phase of the fundamental at the kick, measured on the cycle that
    // follows it: what is heard with the kick, wherever the note started (the
    // 8 cycles before it used to be needed: a note starting 10-150 ms before
    // the kick was not locked, the same note sometimes locked, sometimes not)
    double Tm = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
    bool youngUnknown = false; // a note too young for its period to be known
    lastSource = 0;
    // a note started since less than ~100 ms: the detector's estimate may be
    // the note before or a guess in the silence — its period from the note
    // itself (from its start to the end of the cycle after the kick)
    // (with at least ~45 ms of it — less, a period is a guess too: the
    // detector's last one then, the note before's, right for a repeated note)
    if (when - lastNoteStart < (std::int64_t) (0.1 * sampleRate))
    {
        // (its attack left out: a growing note pulls the estimate 1-3 % short)
        const auto from = juce::jmax (lastNoteStart + (std::int64_t) (0.005 * sampleRate), when - (std::int64_t) (0.1 * sampleRate)), to = when + measureLength();
        const double local = periodBetween (from, to);
        lastSource = local > 1.0 ? 1 : 2;
        if (local > 1.0)
            Tm = local;
        else if (cleanPeriod > 1.0 && differenceAt (from, to, cleanPeriod) < 0.05)
            Tm = cleanPeriod; // the last note held steadily, if this one repeats with it (the same note again)
        else
            youngUnknown = true;
        if (! youngUnknown && lastNoteStart <= when)
        {
            retrack (juce::jmax (lastNoteStart, clock - (std::int64_t) (0.2 * sampleRate)), Tm);
            pitchHoldNote = lastNoteStart;
            pitchHoldUntil = lastNoteStart + (std::int64_t) (0.15 * sampleRate);
        }
    }
    lastTm = Tm;
    lastNoteAgeMs = (double) (when - lastNoteStart) / sampleRate * 1000.0;
    double phase = 0.0, amplitude = 0.0, conf = 0.0;
    measureForward (when, Tm, juce::jlimit (16, juce::jmax (16, measureLength()), (int) std::round (Tm)), phase, amplitude);
    const double fundPower = 0.5 * amplitude * amplitude;
    // no note after the kick (a bass in short notes: the next one starts
    // later): nothing to put in phase — in place, not "unstable"
    // (the whole bass quiet, not just its fundamental: a noise sub's 55 Hz
    // part dips by chance)
    double rawPower = 0.0;
    {
        const int len = juce::jlimit (16, juce::jmax (16, measureLength()), (int) std::round (Tm));
        for (int k = 0; k < len; ++k)
            for (int c = 0; c < numChannels; ++c)
            {
                const float x = input[(size_t) c][(size_t) ((when + k) & mask)];
                rawPower += (double) x * x / (double) (len * numChannels);
            }
    }
    // no note after the kick, a note starting on it or after it (it starts in
    // place: left as it is, no click, the same every time), or one too young
    // to know its period (less than a period before the kick)
    const bool startsAtKick = when - lastNoteStart < (std::int64_t) (0.002 * sampleRate);
    if ((subPeak > 1.0e-7 && fundPower < 0.01 * subPeak && rawPower < 0.03 * subPeak) || startsAtKick || youngUnknown)
    {
        rampTo (0.0, when + releaseFor (when), returnFor (when));
        prevKick = when;
        lastConfidence = 1.0;
        telemetry.phaseConfidence.store (1.0f, std::memory_order_relaxed);
        telemetry.phaseErrorDeg.store (0.0f, std::memory_order_relaxed);
        return;
    }
    // too weak to be measured (-50 dBFS)
    const bool measured = fundPower > dbToGain (-50.0f) * dbToGain (-50.0f) * 0.5f && (periodSamples > 1.0 || when - lastNoteStart < (std::int64_t) (0.1 * sampleRate));
    if (measured)
    {
        // A note that has just started (in the cycle before the kick, or
        // after it): its cycle after the kick is all there is to know — the
        // detector's history still holds the silence or the note before
        const int len = measureLength();
        const auto noteAge = when - lastNoteStart; // samples since the last note started
        conf = 1.0;
        lastAperiodicity = yinAperiodicity;
        lastCoherence = 1.0;
        // a held note: the cycles before the kick say the same (as many as the
        // note has had, up to two): a noise sub's "phase" wanders instead
        int agree = 0, compared = 0;
        const int step = (int) std::round (Tm), backLength = juce::jmin (len, step);
        for (int back = 1; back <= 2; ++back)
            if (noteAge > (std::int64_t) back * step + (std::int64_t) (0.005 * sampleRate))
            {
                // whole periods back (with the fraction of one left over)
                double before = 0.0, ampBefore = 0.0;
                measureForward (when - back * step, Tm, backLength, before, ampBefore);
                const double drift = std::abs (wrap180 (phase - before - 360.0 * back * (step / Tm - 1.0)));
                ++compared;
                agree += drift <= 30.0 ? 1 : 0;
                lastCoherence = juce::jmin (lastCoherence, std::cos (juce::degreesToRadians (juce::jmin (drift, 90.0))));
            }
        if (compared > 0 && agree < compared)
            conf = juce::jmin (conf, 0.5);
        // the pitch detector's own checks, once its window (~100 ms) only
        // holds this note: a periodic sub, a fundamental that holds still (a
        // glide, a wide FM: not lockable) over the last ~50 ms
        if (noteAge > (std::int64_t) (0.1 * sampleRate))
        {
            if (yinAperiodicity > 0.3f)
                conf = juce::jmin (conf, 0.5);
            const int n = juce::jmin (recentCount, 5);
            if (n >= 3)
            {
                std::array<double, 5> last {};
                for (int j = 0; j < n; ++j)
                    last[(size_t) j] = recentPeriods[(size_t) ((recentCount - 1 - j) % (int) recentPeriods.size())];
                const auto [lo, hi] = std::minmax_element (last.begin(), last.begin() + n);
                lastPitchSpread = (*hi - *lo) / (0.5 * (*hi + *lo));
                if (lastPitchSpread > 0.03)
                    conf = juce::jmin (conf, 0.5);
            }
        }
        lastPhase = phase;
        lastConfidence = conf;
    }
    else
        lastConfidence = 0.0;
    const double T = Tm; // the period the phase was measured with
    // an unsure measurement with LOCK: Continu keeps the correction where it
    // is (frozen), Transition makes none at this kick
    const bool sure = measured && (! lock || conf >= 0.7);
    if (sure || ! lock || lockMode == transition)
        rampTo (sure ? targetPsiFor (phase, T) : targetPsiFor (targetPhase, T), when + releaseFor (when), returnFor (when)); // 5 ms, over before the kick is heard
    prevKick = when;
    noteHz = measured ? sampleRate / T : 0.0;
    // error left at the kick, relative to the intended phase
    if (measured)
    {
        const double intended = targetPhase - waveStart - offsetMs * 0.001 * sampleRate / T * 360.0;
        const double atKick = phase - psiTo;
        const double err = lock ? wrap180 (atKick - intended) : wrap180 (phase - targetPhase);
        telemetry.phaseErrorDeg.store ((float) err, std::memory_order_relaxed);
    }
    telemetry.phaseConfidence.store ((float) lastConfidence, std::memory_order_relaxed);
    telemetry.phaseVariance.store ((float) (1.0 - lastConfidence), std::memory_order_relaxed);
}

void KickLockDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    bpm = timing.bpm > 1.0 ? timing.bpm : 120.0;
    // SUB CUT moved: the new kernel is crossfaded in (20 ms)
    if (kernelFade == 0 && ! juce::exactlyEqual (kernelCutoff[(size_t) activeKernel], subCutoff))
    {
        const int other = 1 - activeKernel;
        designKernel (kernels[(size_t) other], subCutoff, sampleRate);
        kernelCutoff[(size_t) other] = subCutoff;
        kernelFade = kernelFadeLength;
    }

    const double releaseSamples = juce::jmax (1.0, releaseBeats (release) * 60.0 / bpm * sampleRate);
    const auto& table = shape == custom ? customTables[(size_t) customPublished.load (std::memory_order_acquire)] : presetTables[(size_t) shape];
    const double attack = attackSeconds * sampleRate;
    const float threshold = dbToGain (thresholdDb);
    const auto noKickSamples = (std::int64_t) (noKickSeconds * sampleRate);
    float kickPeak = 0.0f, gainMin = 1.0f;
    pollLink (timing, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        // --- input: the sub (FIR, aligned 8 ms back), rings ---------------
        const std::int64_t aligned = clock - firDelay; // the time the FIR output belongs to
        float monoSub = 0.0f;
        const float fadeOut = kernelFade > 0 ? (float) kernelFade / (float) kernelFadeLength : 1.0f;
        for (int c = 0; c < nch; ++c)
        {
            const float x = buffer.getSample (c, i);
            auto& h = firHistory[(size_t) c];
            h[(size_t) firPos] = x;
            h[(size_t) (firPos + firLength)] = x;
            input[(size_t) c][(size_t) (clock & mask)] = x;
        }
        firPos = firPos + 1 == firLength ? 0 : firPos + 1;
        // a bass note starting (a jump of the level against the last ~50 ms):
        // the rotation back to 0 before it reaches the output (L later; the
        // fundamental's window starts T/2 before it, so within L - T/2)
        {
            float level = 0.0f;
            for (int c = 0; c < nch; ++c)
                level = juce::jmax (level, std::abs (buffer.getSample (c, i)));
            noteFast += (level - noteFast) * (level > noteFast ? noteFastAtt : noteFastRel);
            // against the level 25 ms earlier (kept in the envelope ring of the
            // sub: here its own small ring)
            noteRing[(size_t) (clock & 4095)] = noteFast;
            const float before = noteRing[(size_t) ((clock - noteLag) & 4095)];
            notePeak = juce::jmax (noteFast, notePeak * notePeakDecay);
            // from a clearly lower level (a gap, a decay): a noise bass's own
            // ups and downs are not notes
            if (clock >= noteDeadUntil && noteFast > 0.003f && noteFast > 3.0f * before + 0.002f && before < 0.3f * notePeak)
            {
                noteDeadUntil = clock + (std::int64_t) (0.05 * sampleRate);
                lastNoteStart = clock;
                ++noteOnsets;
                if (! juce::exactlyEqual (psiTo, 0.0) || ! juce::exactlyEqual (psiNow, 0.0))
                {
                    const double T = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
                    const int room = juce::jmax (8, latency - (int) (0.5 * T) - 16);
                    rampTo (0.0, clock - latency, 1.0, juce::jmin (fadeLength, room));
                    noteHz = 0.0;
                }
            }
        }
        for (int c = 0; c < nch; ++c)
        {
            float s = fir (c, activeKernel);
            if (kernelFade > 0)
                s = fadeOut * s + (1.0f - fadeOut) * fir (c, 1 - activeKernel);
            sub[(size_t) c][(size_t) (aligned & mask)] = s;
            monoSub += s / (float) nch;
        }
        if (kernelFade > 0 && --kernelFade == 0)
            activeKernel = 1 - activeKernel;
        // the fundamental (see the header): z = bass x e^-i theta summed, then
        // at tau = clock - lookahead the window [tau - T/2, tau + T/2]. Only
        // notes under SUB CUT, once YIN knows them (else it fades out: rest +
        // fundamental stays the bass exactly whatever it holds)
        {
            const double T = periodSamples;
            const double hz = T > 1.0 ? sampleRate / T : 0.0;
            if (hz > 1.0 && std::abs (hz - trackHz) > 1.0e-6)
                setTrackHz (hz);
            const float want = hz > 1.0 && hz <= subCutoff * 1.05 && T <= 2.0 * lookahead ? 1.0f : 0.0f;
            trackGain += (want - trackGain) * trackGainCoeff;
            const double re = oscRe * rotRe - oscIm * rotIm, im = oscRe * rotIm + oscIm * rotRe;
            oscRe = re;
            oscIm = im;
            if (++oscRenorm >= 1024)
            {
                oscRenorm = 0;
                const double m = 1.0 / std::sqrt (oscRe * oscRe + oscIm * oscIm);
                oscRe *= m;
                oscIm *= m;
            }
            const auto now = (size_t) (clock & mask);
            oscRingRe[now] = oscRe;
            oscRingIm[now] = oscIm;
            const std::int64_t tau = clock - lookahead;
            const double N = juce::jlimit (2.0, 2.0 * lookahead, T > 1.0 ? T : sampleRate / 55.0);
            const double a = (double) tau - 0.5 * N, b = (double) tau + 0.5 * N;
            const auto ta = (size_t) (tau & mask);
            for (int c = 0; c < nch; ++c)
            {
                const double x = input[(size_t) c][now];
                sumRe[(size_t) c] += x * oscRe;
                sumIm[(size_t) c] -= x * oscIm;
                auto& pr = prefixRe[(size_t) c];
                auto& pi = prefixIm[(size_t) c];
                pr[now] = sumRe[(size_t) c];
                pi[now] = sumIm[(size_t) c];
                // the prefix sum at a fractional index (linear inside a sample)
                auto prefixAt = [&] (double r, double& outRe, double& outIm)
                {
                    const double f = std::floor (r);
                    const auto i0 = (std::int64_t) f;
                    const double fr = r - f;
                    const auto k0 = (size_t) (i0 & mask), k1 = (size_t) ((i0 + 1) & mask);
                    outRe = pr[k0] + fr * (pr[k1] - pr[k0]);
                    outIm = pi[k0] + fr * (pi[k1] - pi[k0]);
                };
                double aRe, aIm, bRe, bIm;
                prefixAt (a, aRe, aIm);
                prefixAt (b, bRe, bIm);
                const double sRe = (bRe - aRe) / N, sIm = (bIm - aIm) / N;
                fund[(size_t) c][ta] = (float) (2.0 * trackGain * (sRe * oscRingRe[ta] - sIm * oscRingIm[ta]));
                fundQ[(size_t) c][ta] = (float) (2.0 * trackGain * (sRe * oscRingIm[ta] + sIm * oscRingRe[ta]));
            }
        }
        mono[(size_t) (aligned & mask)] = monoSub;
        subRms += (monoSub * monoSub - subRms) * rmsCoeff;
        subPeak = juce::jmax (subRms, subPeak * subPeakDecay); // the bass's recent level (mean square), ~1 s memory
        // pitch: decimated (box average, the sub is far below its Nyquist)
        if (++decCounter >= decimation)
        {
            decCounter = 0;
            float acc = 0.0f;
            for (int k = 0; k < decimation; ++k)
                acc += mono[(size_t) ((aligned - k) & mask)];
            dec[(size_t) (decWrite & 1023)] = acc / (float) decimation;
            ++decWrite;
            if (++yinHop >= 32)
            {
                yinHop = 0;
                updatePitch();
            }
        }

        // --- the kick ------------------------------------------------------
        float k = 0.0f, kickMono = 0.0f;
        for (int c = 0; c < scChannels; ++c)
        {
            k = juce::jmax (k, std::abs (sc[(size_t) c][i]));
            kickMono += sc[(size_t) c][i] / (float) scChannels;
        }
        kickRing[(size_t) (clock & mask)] = kickMono;
        scEnv += (k - scEnv) * (k > scEnv ? scAttack : scRelease);
        envRing[(size_t) (clock & mask)] = scEnv;
        kickPeak = juce::jmax (kickPeak, scEnv);
        // the 1 ms envelope follows each cycle of the kick's body (it dips at
        // every zero crossing): a new kick only once the body has gone (slow
        // envelope under half the threshold), or on a new attack well above
        // what remains of it (a kick over a long 808 tail)
        const float slowBefore = slowEnv;
        slowEnv += (scEnv - slowEnv) * (scEnv > slowEnv ? scAttack : slowRelease);
        if (slowEnv < 0.5f * threshold && clock >= deadUntil) // not at the kick's very start: its slow envelope is still rising there
            armed = true;
        const bool newAttack = scEnv > 2.0f * slowBefore && clock >= deadUntil;
        if (scEnv >= threshold && (armed || newAttack) && clock >= deadUntil && scChannels > 0)
        {
            armed = false;
            deadUntil = clock + (std::int64_t) (deadTimeSeconds * sampleRate);
            // the kick starts a little before it crosses the threshold: back
            // to where its envelope rises from (at most 4 ms, so the shift's
            // crossfade still ends before it is heard)
            std::int64_t onset = clock;
            const auto earliest = clock - (std::int64_t) (0.004 * sampleRate);
            while (onset - 1 > earliest && envRing[(size_t) ((onset - 1) & mask)] > 0.02f * threshold
                   && envRing[(size_t) ((onset - 1) & mask)] <= envRing[(size_t) (onset & mask)])
                --onset;
            // the ducking knows it now; its phase is measured once the sub is
            // known there (8 ms later)
            if (trigger == triggerSidechain)
            {
                lastSidechainKick = onset;
                telemetry.triggerSource.store (1, std::memory_order_relaxed);
                addTrigger (onset);
            }
        }
        // the link's kicks, at their own time (the sidechain leads in Auto)
        for (int n = 0; n < linkPendingCount;)
            if (linkPending[(size_t) n] <= clock)
            {
                const auto onset = linkPending[(size_t) n];
                linkPending[(size_t) n] = linkPending[(size_t) --linkPendingCount];
                if (std::abs (onset - lastLinkKick) > (std::int64_t) (0.03 * sampleRate))
                {
                    lastLinkKick = onset;
                    telemetry.triggerSource.store (3, std::memory_order_relaxed);
                    addTrigger (onset);
                }
            }
            else
                ++n;

        // no kick (nothing on the sidechain nor from the link for 2 s, or
        // Beat): the host's quarter notes while it plays, like a tempo-synced ducker
        {
            const double b = timing.beatsAt (i);
            // (Auto: never while a Kick Lock Send is connected — no kick on its
            // track means no ducking, as with a sidechain; Alan: the beats
            // came in during an intro and after a break, "the link drops")
            const bool linked = telemetry.linkSlot.load (std::memory_order_relaxed) >= 0;
            const bool useBeats = trigger == triggerBeat
                                  || (trigger == triggerAuto && ! linked);
            if (! timing.isPlaying)
                beatValid = false;
            else if (! beatValid || b < lastBeatFloor - 0.001 || b > lastBeatFloor + 2.0)
            {
                // (re)synced (play, a loop, a locate): starting on a beat
                // triggers it at once (Alan: the first beat after play was
                // missing); starting between two beats waits for the next
                lastBeatFloor = std::floor (b);
                beatValid = true;
                if (useBeats && b - lastBeatFloor < 2.0 * timing.beatsPerSample + 1.0e-9)
                {
                    telemetry.triggerSource.store (2, std::memory_order_relaxed);
                    addTrigger (clock);
                }
            }
            else if (std::floor (b) > lastBeatFloor)
            {
                lastBeatFloor = std::floor (b);
                // (no dead time: a kick on the sidechain right after it must
                // still be seen, it takes over)
                if (useBeats)
                {
                    telemetry.triggerSource.store (2, std::memory_order_relaxed);
                    addTrigger (clock);
                }
            }
        }
        if (pendingCount > 0 && clock >= pendingKicks[0] + measureLength() + (std::int64_t) (0.001 * sampleRate))
        {
            const auto when = pendingKicks[0];
            for (int n = 1; n < pendingCount; ++n)
                pendingKicks[(size_t) n - 1] = pendingKicks[(size_t) n];
            --pendingCount;
            onKick (when);
        }

        // a new note between two kicks (the fundamental moved > 3 %): its
        // window would use the old f0 — back to the neutral shift until the
        // next kick measures it
        // (only a detector sure of itself: on a fresh note it still holds the
        // silence or the note before, and undid the kick's correction)
        if (noteHz > 0.0 && periodSamples > 1.0 && fadeStart < 0 && yinAperiodicity <= 0.3f
            && std::abs (sampleRate / periodSamples / noteHz - 1.0) > 0.03)
        {
            noteHz = 0.0;
            rampTo (0.0, clock - latency, 1.0, (int) (0.02 * sampleRate)); // a new note: 20 ms, no step
        }

        // --- output: L samples behind --------------------------------------
        const std::int64_t t = clock - latency; // output time (input time of what is heard now)
        // the rotation now: reaching the kick's, held over the release, back to 0
        if (fadeStart >= 0)
        {
            const double a = (double) (clock - fadeStart) / fadeNow;
            if (a >= 1.0)
            {
                fadeStart = -1;
                psiNow = psiTo;
            }
            else
                psiNow = psiFrom + (psiTo - psiFrom) * (double) smoothstep (0.0f, 1.0f, (float) a);
        }
        else if (lockMode == continuous || t < holdEnd)
            psiNow = psiTo;
        else if ((double) (t - holdEnd) < returnLength)
        {
            const double home = 360.0 * std::round (psiTo / 360.0);
            psiNow = psiTo + (home - psiTo) * (double) smoothstep (0.0f, 1.0f, (float) ((double) (t - holdEnd) / returnLength));
        }
        else if (! juce::exactlyEqual (psiNow, 0.0))
            psiNow = psiTo = 0.0;
        float g = 1.0f; // ducking
        if (clock - lastKick < noKickSamples)
            for (int n = 0; n < juce::jmin (kickCount, (int) kicks.size()); ++n)
            {
                const double d = (double) (t - kicks[(size_t) n]);
                float c = 0.0f;
                if (d < 0.0 && -d < attack)
                    c = smoothstep (0.0f, 1.0f, (float) (1.0 + d / attack)); // down to the kick
                else if (d >= 0.0)
                {
                    // the curve's gain over the release, its last value held after
                    const float pos = (float) juce::jmin (1.0, d / releaseSamples) * (float) curveTableSize;
                    const int i0 = juce::jmin ((int) pos, curveTableSize - 1);
                    c = 1.0f - (table[(size_t) i0] + (pos - (float) i0) * (table[(size_t) i0 + 1] - table[(size_t) i0]));
                }
                g = juce::jmin (g, 1.0f - depth * c);
            }
        gainMin = juce::jmin (gainMin, g);
        // MIX: no dry / wet crossfade of the sub — the dry sub and the shifted
        // one would comb (up to -14 dB on the fundamental half a cycle apart).
        // MIX scales the ducking, and the shift comes in over 0..25 % (full
        // above: the phase is locked whatever the MIX)
        const float mix = clamp01 (amount[i]);
        const double amt = (double) smoothstep (0.0f, shiftMixRamp, mix);
        const float duck = 1.0f - mix * (1.0f - g);
        const double psi = amt * psiNow * juce::MathConstants<double>::pi / 180.0;
        const bool rotate = ! juce::exactlyEqual (psi, 0.0);
        const float cr = rotate ? (float) std::cos (psi) : 1.0f, sr = rotate ? (float) std::sin (psi) : 0.0f;
        float outs[2] {};
        float scopeSub = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const auto ti = (size_t) (t & mask);
            const float x = input[(size_t) c][ti];
            const float e = fund[(size_t) c][ti], q = fundQ[(size_t) c][ti];
            // the fundamental rotated by psi (cos psi e + sin psi q) instead of itself
            outs[c] = duck * (rotate ? x + (cr - 1.0f) * e + sr * q : x);
            scopeSub += (cr * e + sr * q) / (float) nch;
        }
        for (int c = 0; c < nch; ++c)
            buffer.getWritePointer (c)[i] = outs[c];

        // mono sum (stereo only): level of (L + R) / 2 against L / R
        if (nch == 2)
        {
            const float m = 0.5f * (outs[0] + outs[1]);
            midRms += (m * m - midRms) * rmsCoeff * 0.2f;
            lrRms += (0.5f * (outs[0] * outs[0] + outs[1] * outs[1]) - lrRms) * rmsCoeff * 0.2f;
        }

        // oscilloscope (1 in 8): the sub as heard, the kick envelope
        if (++scopeCounter >= KickTelemetry::scopeDecimation && telemetry.scopeSize > 0)
        {
            scopeCounter = 0;
            const int w = telemetry.scopeWrite.load (std::memory_order_relaxed);
            telemetry.scopeSub[(size_t) w].store (scopeSub, std::memory_order_relaxed);
            telemetry.scopeBass[(size_t) w].store (nch == 2 ? 0.5f * (outs[0] + outs[1]) : outs[0], std::memory_order_relaxed);
            // the kick as heard (L later): Kick Lock Send's own wave when it leads, else the sidechain's
            float kv = kickRing[(size_t) (t & mask)];
            if (trigger != triggerSidechain && trigger != triggerBeat && timing.isPlaying && telemetry.linkSlot.load (std::memory_order_relaxed) >= 0)
            {
                float lv = 0.0f;
                if (link.waveAt (timing.beatsAt (i) - (double) latency * timing.beatsPerSample, lv))
                    kv = lv;
            }
            telemetry.scopeKick[(size_t) w].store (kv, std::memory_order_relaxed);
            telemetry.scopeTime.store (t, std::memory_order_relaxed);
            telemetry.scopeWrite.store ((w + 1) % telemetry.scopeSize, std::memory_order_release);
        }
        ++clock;
    }

    // --- status ---------------------------------------------------------------
    const float subDb = gainToDb (std::sqrt (subRms) + 1.0e-9f);
    const float lossDb = nch == 2 && lrRms > 1.0e-10f ? juce::jmax (0.0f, 10.0f * std::log10 (lrRms / juce::jmax (1.0e-12f, midRms))) : 0.0f;
    telemetry.subLevelDb.store (subDb, std::memory_order_relaxed);
    telemetry.kickLevelDb.store (gainToDb (kickPeak + 1.0e-9f), std::memory_order_relaxed);
    telemetry.monoSumLossDb.store (lossDb, std::memory_order_relaxed);
    telemetry.f0Hz.store (periodSamples > 1.0 ? (float) (sampleRate / periodSamples) : 0.0f, std::memory_order_relaxed);
    telemetry.shiftMs.store ((float) currentShiftMs(), std::memory_order_relaxed);
    telemetry.duckGain.store (gainMin, std::memory_order_relaxed);
    telemetry.outputClock.store (clock - latency, std::memory_order_relaxed);
    Status st = Status::ok;
    const float err = std::abs (telemetry.phaseErrorDeg.load (std::memory_order_relaxed));
    if (clock - lastKick > noKickSamples)
        st = Status::noKick; // no trigger at all: no kick, and no beat (the host is stopped)
    else if (lossDb > 3.0f)
        st = Status::monoLoss;
    else if (subDb < -50.0f)
        st = Status::weakSub;
    else if (lastConfidence < 0.7)
        st = Status::unstable;
    else if (err >= 5.0f || fadeStart >= 0)
        st = Status::shifted;
    telemetry.status.store ((int) st, std::memory_order_relaxed);
}
} // namespace kl
