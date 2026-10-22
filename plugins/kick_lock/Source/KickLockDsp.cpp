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
    liveLatency = latencyFor (sr, maxBlock);
    preciseLatency = preciseLatencyFor (sr, maxBlock);
    latency = precise ? preciseLatency : liveLatency;
    for (int k = 0; k < 2; ++k)
    {
        kernels[(size_t) k].assign ((size_t) firLength, 0.0f);
        firHistory[(size_t) k].assign ((size_t) (2 * firLength), 0.0f);
    }
    designKernel (kernels[0], subCutoff, sr);
    kernelCutoff = { subCutoff, -1.0f };
    activeKernel = 0;
    for (int m = 1; m < monoOptions; ++m)
    {
        monoKernels[(size_t) m].assign ((size_t) firLength, 0.0f);
        designKernel (monoKernels[(size_t) m], monoHz (m), sr);
    }
    sideHistory.assign ((size_t) (2 * firLength), 0.0f);
    monoFadeLength = juce::jmax (1, (int) std::round (0.03 * sr));
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
    sideLow.assign ((size_t) size, 0.0f);
    {
        std::int64_t n = 1;
        while (n < (std::int64_t) (2.0 * sr / KickTelemetry::scopeDecimation))
            n <<= 1;
        heardKick.assign ((size_t) n, 0.0f);
        heardMask = n - 1;
    }
    analysis.assign ((size_t) (0.25 * sr) + 4096, 0.0f);
    decimated.assign ((size_t) (0.25 * sr) / 8 + 512, 0.0f);
    lowFrom = lowTo = -1;
    decimation = juce::jmax (1, (int) std::round (sr / 3000.0));
    {
        // PRECISE: a Hilbert transformer at ~3 kHz, +-45 ms (Kaiser), and the
        // rings of the sub there (~1.3 s)
        const double fsd = sr / decimation;
        hilM = (int) std::round (hilbertSeconds * fsd) | 1;
        lowKernel.assign ((size_t) hilM + 1, 0.0f);
        hilbertKernel.assign ((size_t) hilM + 1, 0.0f);
        pairCutoff = -1.0f;
        designPair();
        std::int64_t n = 1;
        while (n < (std::int64_t) (1.3 * fsd))
            n <<= 1;
        dMask = n - 1;
        for (int c = 0; c < 2; ++c)
        {
            dSub[(size_t) c].assign ((size_t) n, 0.0f);
            dLow[(size_t) c].assign ((size_t) n, 0.0f);
            dHil[(size_t) c].assign ((size_t) n, 0.0f);
        }
    }
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
    std::fill (sideHistory.begin(), sideHistory.end(), 0.0f);
    std::fill (sideLow.begin(), sideLow.end(), 0.0f);
    monoFrom = monoTo = monoBelow;
    monoFade = 0;
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
    std::fill (heardKick.begin(), heardKick.end(), 0.0f);
    kickPhase = 0.0;
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
    trackPeriod = 0.0;
    bassRms = 0.0f;
    dWritten = 0;
    dPhase = 0;
    dAcc = {};
    for (int c = 0; c < 2; ++c)
    {
        std::fill (dSub[(size_t) c].begin(), dSub[(size_t) c].end(), 0.0f);
        std::fill (dLow[(size_t) c].begin(), dLow[(size_t) c].end(), 0.0f);
        std::fill (dHil[(size_t) c].begin(), dHil[(size_t) c].end(), 0.0f);
    }
    heldPeriod = 0.0;
    retrackPending = rampPending = false;
    yinAperiodicity = 1.0f;
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
    monoBelow = n > monoChoice ? juce::jlimit (0, monoOptions - 1, idx[monoChoice]) : 0;
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

void KickLockDsp::requestRetrack (std::int64_t from, double T) noexcept
{
    if (rotationIdle())
    {
        retrack (juce::jmax (from, clock - (std::int64_t) (0.2 * sampleRate)), T);
        return;
    }
    retrackPending = true;
    retrackFrom = from;
    retrackT = T;
    rampTo (0.0, clock - latency, 1.0, fadeLength);
}

void KickLockDsp::retrack (std::int64_t from, double T) noexcept
{
    // the fundamental's tracking, from the note's start on, redone at the
    // period measured at the kick: what is not heard yet is extracted at the
    // right frequency (YIN still held the note before or a guess, its window
    // was not yet in the note) — the kick's rotation turns the whole
    // fundamental, not part of it
    periodSamples = trackPeriod = T;
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
}

void KickLockDsp::measureForward (std::int64_t at, double T, int length, double& phaseDeg, double& amplitude, bool low) const noexcept
{
    // low: on the bass's low band (zero-phase, see lowBand) — a raw saw's
    // harmonics leaked into a window shorter than its period (36.7 Hz)
    // (the low band is at 1 in 4: its samples at at - before + 4 k + 1.5)
    const int before = low ? (int) std::round (2.0 * T) : 0;
    const int first4 = (before + lowStep - 1) / lowStep, count4 = (before + length) / lowStep - first4;
    if (low && (lowBand (at - before, at + length, T) < first4 + count4 || count4 < 8))
        low = false;
    const int count = low ? count4 : length;
    // the bass itself (the input ring, mono), from the kick on: its
    // fundamental and next harmonics fitted together (least squares) at 1 / T.
    // Over a whole period that is the plain projection on f0 (every harmonic
    // cancels); over less (a note under ~40 Hz, the room before the kick is
    // heard) the harmonics no longer cancel and would pull the phase
    constexpr int maxM = 16;
    const int H = low ? 2 : 3, M = 2 * H; // (the low band: f0 and its 2nd; the whole bass: its level only)
    double G[maxM][maxM] {}, r[maxM] {}, basis[maxM];
    const double w = juce::MathConstants<double>::twoPi / T * (low ? lowStep : 1), cw = std::cos (w), sw = std::sin (w);
    // (the first sample's time from `at`: 0, or on the low band 4 first4 + 1.5 - before)
    const double start = low ? juce::MathConstants<double>::twoPi / T * ((double) (first4 * lowStep) + 0.5 * (lowStep - 1) - before) : 0.0;
    double c1 = std::cos (start), s1 = std::sin (start);
    for (int k = 0; k < count; ++k)
    {
        float x = 0.0f;
        if (low)
            x = analysis[(size_t) (first4 + k)];
        else
        {
            for (int c = 0; c < numChannels; ++c)
                x += input[(size_t) c][(size_t) ((at + k) & mask)];
            x /= (float) numChannels;
        }
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
    double L[maxM][maxM] {}, y[maxM] {}, coef[maxM] {};
    for (int p = 0; p < M; ++p)
        for (int q = 0; q <= p; ++q)
        {
            double sum = G[p][q] + (p == q ? 1.0e-6 * count : 0.0);
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

double KickLockDsp::fitPeriod (const float* x, int n, double rate, double T0, int harmonics, double* residualOut) noexcept
{
    // least squares: x ~ sum of cos / sin of the first harmonics (up to
    // ~400 Hz, at most 6) at 1 / T; the T leaving the least residual, by a
    // golden-section search within 4 % of T0 (one minimum there: the window
    // is longer than a period)
    const int H = harmonics > 0 ? juce::jmin (6, harmonics) : juce::jlimit (1, 6, (int) std::floor (400.0 * T0 / rate));
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
    for (int it = 0; it < 18; ++it) // (8 % / 1.618^18: 0.002 %)
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
    if (residualOut != nullptr)
        *residualOut = residual (0.5 * (lo + hi));
    return 0.5 * (lo + hi);
}

int KickLockDsp::lowBand (std::int64_t from, std::int64_t to, double T) const noexcept
{
    // mono, with ~12 ms before (the filter settles) and a mirrored tail after
    // (no data past `to`: the backward pass starts on it, not on a step)
    const int warm = (int) (0.012 * sampleRate), tail = warm;
    const int n = (int) juce::jlimit<std::int64_t> (0, (std::int64_t) analysis.size() - warm - tail, to - from);
    if (n < 4)
        return 0;
    // the same stretch again (a period, then its steadiness): already there
    // (the tail continued at a period within 1 %: the same)
    if (from == lowFrom && to == lowTo && (T <= 1.0 ? lowT <= 1.0 : std::abs (T / juce::jmax (1.0, lowT) - 1.0) < 0.01))
        return n / lowStep;
    lowFrom = from;
    lowTo = to;
    lowT = T;
    // at 1 in 4 (~12 kHz: a 90 Hz band needs no more; a quarter of the cost
    // — a kick's analysis ran in one sample: clicks). analysis[k] is the low
    // band at from + 4 k + 1.5 (the 4 samples averaged, centred there)
    constexpr int L = lowStep;
    const int w4 = warm / L, n4 = n / L, t4 = tail / L;
    auto& a = analysis;
    for (int k = 0; k < w4 + n4; ++k)
    {
        float x = 0.0f;
        for (int j = 0; j < L; ++j)
            for (int c = 0; c < numChannels; ++c)
                x += input[(size_t) c][(size_t) ((from - (std::int64_t) w4 * L + (std::int64_t) k * L + j) & mask)];
        a[(size_t) k] = x / (float) (L * numChannels);
    }
    // after `to`: the note continued one period on (exact for a held note), or
    // mirrored when its period is not known yet (a mirror about the last value
    // added a step the filter rang on)
    // (the period in low-band samples is fractional: interpolated — rounded,
    // the continuation slipped up to 2 samples, 10 deg at 55 Hz)
    const double P = T / L;
    for (int k = 0; k < t4; ++k)
    {
        const double back = (double) (w4 + n4 + k) - P;
        if (P >= 2.0 && back >= 0.0)
        {
            const int b0 = (int) back;
            const float f = (float) (back - b0);
            a[(size_t) (w4 + n4 + k)] = a[(size_t) b0] + f * (a[(size_t) b0 + 1] - a[(size_t) b0]);
        }
        else
            a[(size_t) (w4 + n4 + k)] = a[(size_t) juce::jmax (0, w4 + n4 - 2 - k)];
    }
    const int total = w4 + n4 + t4;
    // two Butterworth low-pass biquads (90 Hz, or SUB CUT if lower), forth and back
    const double fc = juce::jmin (90.0, (double) subCutoff), w = juce::MathConstants<double>::twoPi * fc * L / sampleRate;
    const double alpha = std::sin (w) / (2.0 * juce::MathConstants<double>::sqrt2 * 0.5), cw = std::cos (w), a0 = 1.0 + alpha;
    const double b0 = (1.0 - cw) * 0.5 / a0, b1 = (1.0 - cw) / a0, b2 = b0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    auto pass = [&] (int begin, int end, int step)
    {
        for (int stage = 0; stage < 2; ++stage)
        {
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            for (int k = begin; k != end; k += step)
            {
                const double x = a[(size_t) k], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                a[(size_t) k] = (float) y;
            }
        }
    };
    pass (0, total, 1);
    pass (total - 1, -1, -1);
    std::memmove (a.data(), a.data() + w4, (size_t) n4 * sizeof (float));
    return n4;
}

double KickLockDsp::refinePeriod (std::int64_t from, std::int64_t to, double T0) const noexcept
{
    // the period of the low band around the kick, from T0 (within a few %):
    // its fundamental's phase on the first half against the second half —
    // how much further it turned than T0 says is the correction (3 times).
    // At 1 in 8 (~6 kHz, plenty under 160 Hz). (A least-squares fit by
    // golden-section search did the same for 30 times the cost: clicks.)
    constexpr int D = 2 * lowStep;
    const int all = lowBand (from, to, T0);
    const int n = juce::jmin (all / 2, (int) decimated.size());
    if (n * D < (int) (1.2 * T0) || n < 16)
        return 0.0;
    for (int i = 0; i < n; ++i)
        decimated[(size_t) i] = 0.5f * (analysis[(size_t) (2 * i)] + analysis[(size_t) (2 * i + 1)]);
    // the fundamental's phase on the first half against the second half, each
    // under a Hann window (no window: the 2nd harmonic and the edges leaked,
    // 1 % off): how much further it turned than the period says is the
    // correction (3 times). (A least-squares fit by golden-section search
    // did as well for 6 times the cost.)
    // (under 3 periods — a note just started, legato on the kick — the halves
    // are too short for that: the least-squares fit, cheap on so few samples)
    if ((double) n * D < 3.0 * T0)
        return fitPeriod (analysis.data(), juce::jmin (all, 2 * n), sampleRate / lowStep, T0 / lowStep, 2) * lowStep; // (at 1 in 4: few samples)
    const int half = n / 2;
    double w = juce::MathConstants<double>::twoPi * D / T0;
    for (int pass = 0; pass < 3; ++pass)
    {
        double z[2][2] {};
        for (int part = 0; part < 2; ++part)
        {
            const int offset = part * half;
            double c = std::cos (w * offset), sn = std::sin (w * offset);
            const double cw = std::cos (w), sw = std::sin (w), hw = juce::MathConstants<double>::twoPi / (half - 1);
            double hc = 1.0, hs = 0.0; // the window's own phasor
            const double hcw = std::cos (hw), hsw = std::sin (hw);
            for (int i = 0; i < half; ++i)
            {
                const double win = 0.5 - 0.5 * hc, x = win * decimated[(size_t) (offset + i)];
                z[part][0] += x * c;
                z[part][1] -= x * sn;
                double nc = c * cw - sn * sw;
                sn = sn * cw + c * sw;
                c = nc;
                nc = hc * hcw - hs * hsw;
                hs = hs * hcw + hc * hsw;
                hc = nc;
            }
        }
        // the second half against the first (both referred to the same time origin)
        const double dphi = std::atan2 (z[1][1] * z[0][0] - z[1][0] * z[0][1], z[1][0] * z[0][0] + z[1][1] * z[0][1]);
        w += dphi / half;
        if (w <= 0.0)
            return 0.0;
    }
    return juce::MathConstants<double>::twoPi * D / w;
}

double KickLockDsp::steadiness (std::int64_t from, std::int64_t to, double T) const noexcept
{
    // How far the fundamental's phase drifts over the stretch: its phase over
    // one period, every half period, against the first (all at 1 / T, from
    // one origin): a held note stays put — whatever its timbre does (a
    // filter closing over the note changed its shape: the old measure, the
    // low band against itself one period later, called it unsteady); a glide,
    // a wide FM, a noise sub drift. Returned as degrees / 600 (the limits:
    // 0.04 = 24 deg, 0.01 = 6 deg); -1 when too short to tell (1.5 periods)
    const int n = lowBand (from, to, T), W = (int) std::round (T / lowStep);
    if (W < 4 || n < W + W / 2)
        return -1.0;
    const double w = juce::MathConstants<double>::twoPi * lowStep / T, cw = std::cos (w), sw = std::sin (w);
    auto phaseAt = [&] (int offset)
    {
        double c = std::cos (w * offset), sn = std::sin (w * offset), I = 0.0, Q = 0.0;
        for (int k = 0; k < W; ++k)
        {
            const double x = analysis[(size_t) (offset + k)];
            I += x * c;
            Q += x * sn;
            const double nc = c * cw - sn * sw;
            sn = sn * cw + c * sw;
            c = nc;
        }
        return std::atan2 (I, Q) * 180.0 / juce::MathConstants<double>::pi;
    };
    const double first = phaseAt (0);
    double drift = 0.0;
    for (int offset = juce::jmax (1, W / 2); offset + W <= n; offset += juce::jmax (1, W / 2))
        drift = juce::jmax (drift, std::abs (wrap180 (phaseAt (offset) - first)));
    return drift / 600.0;
}

double KickLockDsp::periodBetween (std::int64_t from, std::int64_t to) const noexcept
{
    // on the low band (a saw's harmonics made the third one look like the note)
    // (the coarse search at ~3 kHz, plenty under 160 Hz: the refinement after
    // it gives the precision — a kick's analysis must stay cheap)
    constexpr int D = 4 * lowStep;
    std::array<float, 2048> x {};
    const int all = lowBand (from, to, 0.0);
    const int n = juce::jmin (all / 4, (int) x.size());
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = 0.25f * (analysis[(size_t) (4 * i)] + analysis[(size_t) (4 * i + 1)] + analysis[(size_t) (4 * i + 2)] + analysis[(size_t) (4 * i + 3)]);
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
    // (0.25: a real note's level moves under it; the fit below sets the period)
    for (int tau = tauMin; tau < tauMax - 1; ++tau)
        if (d[(size_t) tau] < 0.25f)
        {
            while (tau + 1 < tauMax - 1 && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            // then refined: the note fitted as a periodic wave (its first
            // harmonics) — a short window's lag estimate is 1-3 % short (the
            // note growing, less than two periods to compare)
            const double a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1], den = a - 2.0 * b + c;
            const double coarse = (tau + (std::abs (den) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0)) * D;
            const double fitted = refinePeriod (from, to, coarse); // (its tail continued at that period)
            const double T = fitted > 1.0 ? fitted : coarse;
            return (double) (to - from) >= 1.4 * T ? T : 0.0; // under 1.4 periods of the note: a guess (a wrong period passed 1.2 at 36.7 Hz)
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

double KickLockDsp::collisionPhase (double T, std::int64_t when) const noexcept
{
    // Where the sub and the kick meet: the kick (the last one, as heard: a
    // kick is not a sine — its pitch falls, its phase moves) weighted by how
    // much sub is back at each instant (the ducking curve), projected on the
    // note's frequency. In phase with that, the two add up where they collide
    // (Alan: "c'est au niveau de la collision des deux que c'est important").
    // The last kick's shape: the same sample every bar, no look-ahead needed.
    if (prevKick < 0 || heardKick.empty() || T <= 1.0)
        return 0.0;
    const auto heardUpTo = clock - latency; // the output's time: what has been heard of the kick
    const double releaseLength = releaseSamplesNow();
    // (the whole kick's tail, up to the next kick: after the release the sub
    // is back at full level while the kick still sounds — the collision
    // weighs most there; the window used to stop at the release's end)
    const auto beatLength = (std::int64_t) (60.0 / juce::jmax (1.0, bpm) * sampleRate);
    const auto window = juce::jmin<std::int64_t> (heardUpTo - prevKick, (std::int64_t) (collisionSeconds * sampleRate), when - prevKick > 0 ? when - prevKick : beatLength);
    if (window < (std::int64_t) (0.03 * sampleRate))
        return 0.0;
    const auto& table = shape == custom ? customTables[(size_t) customPublished.load (std::memory_order_acquire)] : presetTables[(size_t) shape];
    const int step = KickTelemetry::scopeDecimation;
    double kc = 0.0, ks = 0.0, total = 0.0, rc = 1.0, rs = 0.0;
    const double stepAngle = juce::MathConstants<double>::twoPi * step / T, stepCos = std::cos (stepAngle), stepSin = std::sin (stepAngle);
    for (std::int64_t u = 0; u < window; u += step)
    {
        const float k = heardKick[(size_t) (((prevKick + u) / step) & heardMask)];
        const float pos = (float) juce::jmin (1.0, (double) u / releaseLength) * (float) curveTableSize;
        const int i0 = juce::jmin ((int) pos, curveTableSize - 1);
        const float c = 1.0f - (table[(size_t) i0] + (pos - (float) i0) * (table[(size_t) i0 + 1] - table[(size_t) i0]));
        const double g = 1.0 - depth * c; // the sub heard (0 on the kick, back to 1 after the release)
        kc += k * g * rc;
        ks += k * g * rs;
        total += std::abs (k) * g;
        const double nc = rc * stepCos - rs * stepSin; // (the phasor turned, no trigonometry per step)
        rs = rs * stepCos + rc * stepSin;
        rc = nc;
    }
    // (a kick with nothing at the note's frequency where the sub is back: no aim)
    if (total <= 1.0e-9 || std::hypot (kc, ks) < 0.05 * total)
        return 0.0;
    return std::atan2 (kc, ks) * 180.0 / juce::MathConstants<double>::pi;
}

double KickLockDsp::targetPsiFor (double phaseDeg, double T) const noexcept
{
    // + = later. Lock: the rotation that brings the fundamental to the
    // target phase at the kick; WAVE START and OFFSET added on top (deliberate)
    // (the target: TARGET from the kick's own phase where the two meet)
    const double base = waveStart + offsetMs * 0.001 * sampleRate / T * 360.0;
    return base + (lock ? wrap180 (phaseDeg - targetPhase - kickPhase) : 0.0);
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
    const auto closeBy = (std::int64_t) (0.03 * sampleRate); // (not "near": a macro in windows.h)
    for (int n = 0; n < linkPendingCount; ++n)
        if (std::abs (linkPending[(size_t) n] - onset) < closeBy)
            return;
    if (std::abs (onset - lastLinkKick) < closeBy || linkPendingCount >= (int) linkPending.size())
        return;
    linkPending[(size_t) linkPendingCount++] = onset;
}

bool KickLockDsp::placeLinkKick (double ppq, std::int64_t& onset) const noexcept
{
    // the newest stretch of the song holding this position, near now (an
    // older loop pass holds it too, seconds ago: not that one); a stretch
    // left by a jump still reaches 0.3 s past its end (the kick's track not
    // yet jumped when its kick was found)
    const auto closeBy = (std::int64_t) (0.3 * sampleRate);
    const int n = juce::jmin (stretchCount, (int) stretches.size());
    for (int k = 0; k < n; ++k)
    {
        const auto& st = stretches[(size_t) ((stretchCount - 1 - k) % (int) stretches.size())];
        const double before = 0.01 * sampleRate * st.bps, after = (k == 0 ? 0.01 : 0.3) * sampleRate * st.bps;
        const double end = st.ppq0 + (double) st.length * st.bps;
        if (ppq < st.ppq0 - before || ppq > end + after)
            continue;
        const auto at = st.clock0 + (std::int64_t) std::llround ((ppq - st.ppq0) / st.bps);
        if (clock - at < closeBy && at - clock < closeBy)
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

void KickLockDsp::addTrigger (std::int64_t onset, int source)
{
    // the ducking knows it now; its phase is measured once the sub is known
    // there (8 ms later)
    kicks[(size_t) (kickCount % (int) kicks.size())] = onset;
    ++kickCount;
    lastKick = onset;
    if (pendingCount < (int) pendingKicks.size())
    {
        pendingExtra[(size_t) pendingCount] = 0;
        pendingSource[(size_t) pendingCount] = source;
        pendingKicks[(size_t) pendingCount++] = onset;
    }
}

void KickLockDsp::setSwitches (const bool* on, int n)
{
    lock = n > lockSwitch && on[lockSwitch];
    phaseOn = n <= phaseSwitch || on[phaseSwitch];
    const bool p = n > preciseSwitch && on[preciseSwitch];
    if (p != precise)
    {
        precise = p;
        applyLatency(); // (the processor has faded the output out for it)
    }
}

void KickLockDsp::applyLatency() noexcept
{
    latency = precise ? preciseLatency : liveLatency;
    lookahead = latency - 3;
    pendingCount = linkPendingCount = linkWaitingCount = 0;
    psiNow = psiTo = psiFrom = 0.0;
    fadeStart = -1;
    retrackPending = rampPending = false;
    dWritten = 0;
    dPhase = 0;
    dAcc = {};
}

void KickLockDsp::designPair() noexcept
{
    // everything under SUB CUT passes whole (the transition, ~30 Hz, above
    // it): the low-pass sin (w m) / (pi m), its Hilbert pair
    // (1 - cos (w m)) / (pi m), windowed alike (Kaiser, ~50 dB)
    if (juce::exactlyEqual (pairCutoff, subCutoff) || lowKernel.empty())
        return;
    pairCutoff = subCutoff;
    const double fsd = sampleRate / decimation, fc = (subCutoff + 16.0) / fsd, w = juce::MathConstants<double>::twoPi * fc;
    const double beta = 5.0;
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
    double dc = 2.0 * fc;
    lowKernel[0] = (float) (2.0 * fc);
    for (int m = 1; m <= hilM; ++m)
    {
        const double r = (double) m / (hilM + 1), win = i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta);
        lowKernel[(size_t) m] = (float) (std::sin (w * m) / (juce::MathConstants<double>::pi * m) * win);
        hilbertKernel[(size_t) m] = (float) ((1.0 - std::cos (w * m)) / (juce::MathConstants<double>::pi * m) * win);
        dc += 2.0 * lowKernel[(size_t) m];
    }
    for (auto& v : lowKernel)
        v = (float) (v / dc); // unity under the cut
    for (auto& v : hilbertKernel)
        v = (float) (v / dc);
}

float KickLockDsp::lowAt (int channel, std::int64_t t) const noexcept
{
    const double u = ((double) t - dFirst) / decimation;
    const auto k = (std::int64_t) std::floor (u);
    const auto ready = dWritten - 1 - hilM;
    if (k - 1 < juce::jmax<std::int64_t> (0, dWritten - dMask) || k + 2 > ready)
        return 0.0f;
    const float f = (float) (u - (double) k);
    const auto& h = dLow[(size_t) channel];
    const float p0 = h[(size_t) ((k - 1) & dMask)], p1 = h[(size_t) (k & dMask)], p2 = h[(size_t) ((k + 1) & dMask)], p3 = h[(size_t) ((k + 2) & dMask)];
    return p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0)));
}

float KickLockDsp::hilbertAt (int channel, std::int64_t t) const noexcept
{
    // the sub's Hilbert transform at a full-rate time (Catmull-Rom between
    // its ~3 kHz samples; it holds nothing above ~300 Hz)
    const double u = ((double) t - dFirst) / decimation;
    const auto k = (std::int64_t) std::floor (u);
    const auto ready = dWritten - 1 - hilM;
    if (k - 1 < juce::jmax<std::int64_t> (0, dWritten - dMask) || k + 2 > ready)
        return 0.0f;
    const float f = (float) (u - (double) k);
    const auto& h = dHil[(size_t) channel];
    const float p0 = h[(size_t) ((k - 1) & dMask)], p1 = h[(size_t) (k & dMask)], p2 = h[(size_t) ((k + 1) & dMask)], p3 = h[(size_t) ((k + 2) & dMask)];
    return p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0)));
}

void KickLockDsp::onKickPrecise (std::int64_t when, int source)
{
    const int count = telemetry.kickEvents.load (std::memory_order_relaxed) + 1;
    telemetry.recentKicks[(size_t) (count % 4)].store (when, std::memory_order_relaxed);
    telemetry.lastKickSample.store (when, std::memory_order_relaxed);
    telemetry.kickEvents.store (count, std::memory_order_release);
    const auto base = (double) waveStart + (periodSamples > 1.0 ? offsetMs * 0.001 * sampleRate / periodSamples * 360.0 : 0.0);
    // (no analysis: PHASE off, no kick heard, no bass — as in LIVE)
    const bool bass = subRms > 1.0e-5f && subRms >= 0.25f * bassRms;
    const int reason = ! phaseOn ? 1 : source == 2 ? 2 : ! bass ? 3 : 0;
    telemetry.analysisReason.store (reason, std::memory_order_relaxed);
    lastConfidence = 1.0;
    if (reason != 0)
    {
        rampTo (0.0, when + releaseFor (when), returnFor (when));
        prevKick = when;
        telemetry.phaseErrorDeg.store (0.0f, std::memory_order_relaxed);
        telemetry.kickPhaseDeg.store (0.0f, std::memory_order_relaxed);
        return;
    }
    // The collision, seen ahead: over ~90 ms after the kick (the release, at
    // most), the kick against the sub (the low band and its Hilbert transform)
    // weighted by how much sub the curve lets through. Turned by psi, the sub
    // is cos psi L + sin psi H: its correlation with the kick, cos psi A +
    // sin psi B, is largest at psi = atan2 (B, A) — no note, no period.
    // The kick: the one on the sidechain itself (seen ahead too), or, from
    // Kick Lock Send, the last one as heard (the same sample every beat).
    const double releaseLength = releaseSamplesNow();
    // (the whole kick's tail, up to the next kick — see collisionPhase)
    const double window = juce::jmin (collisionSeconds * sampleRate, prevKick >= 0 && when > prevKick ? (double) (when - prevKick) : 60.0 / juce::jmax (1.0, bpm) * sampleRate);
    const auto& table = shape == custom ? customTables[(size_t) customPublished.load (std::memory_order_acquire)] : presetTables[(size_t) shape];
    const int D = decimation;
    const auto kFirst = (std::int64_t) std::ceil (((double) when - dFirst) / D);
    const auto kLast = juce::jmin ((std::int64_t) std::floor (((double) when + window - dFirst) / D), dWritten - 1 - hilM);
    const bool current = source == 1;
    const auto heardUpTo = clock - latency;
    double A = 0.0, B = 0.0, kk = 0.0, ll = 0.0;
    for (auto k = juce::jmax (kFirst, dWritten - dMask); k <= kLast; ++k)
    {
        const double t = dFirst + (double) k * D, u = t - (double) when;
        const float pos = (float) juce::jmin (1.0, u / releaseLength) * (float) curveTableSize;
        const int i0 = juce::jlimit (0, curveTableSize - 1, (int) pos);
        const float c = 1.0f - (table[(size_t) i0] + (pos - (float) i0) * (table[(size_t) i0 + 1] - table[(size_t) i0]));
        const double g = 1.0 - depth * c;
        double kv = 0.0;
        if (current)
        {
            const auto t0 = (std::int64_t) std::llround (t) - D / 2;
            for (int j = 0; j < D; ++j)
                kv += kickRing[(size_t) ((t0 + j) & mask)];
            kv /= D;
        }
        else if (prevKick >= 0 && prevKick + (std::int64_t) u <= heardUpTo)
            kv = heardKick[(size_t) (((prevKick + (std::int64_t) u) / KickTelemetry::scopeDecimation) & heardMask)];
        double L = 0.0, H = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            L += dLow[(size_t) ch][(size_t) (k & dMask)];
            H += dHil[(size_t) ch][(size_t) (k & dMask)];
        }
        L /= numChannels;
        H /= numChannels;
        A += kv * g * L;
        B += kv * g * H;
        kk += kv * kv * g * g;
        ll += 0.5 * (L * L + H * H);
    }
    const double strength = std::hypot (A, B);
    const double psiStar = strength > 1.0e-12 && ll > 1.0e-12 ? std::atan2 (B, A) * 180.0 / juce::MathConstants<double>::pi : 0.0;
    const double coherence = kk > 0.0 && ll > 0.0 ? strength / std::sqrt (kk * ll) : 0.0;
    rampTo (lock ? psiStar + targetPhase + base : base, when + releaseFor (when), returnFor (when));
    prevKick = when;
    kickPhase = psiStar;
    telemetry.kickPhaseDeg.store ((float) psiStar, std::memory_order_relaxed);
    telemetry.phaseErrorDeg.store (lock ? 0.0f : (float) wrap180 (-psiStar), std::memory_order_relaxed);
    telemetry.phaseConfidence.store ((float) juce::jlimit (0.0, 1.0, coherence), std::memory_order_relaxed);
}

void KickLockDsp::onKick (std::int64_t when, int extra, int source)
{
    if (extra == 0)
    {
        const int count = telemetry.kickEvents.load (std::memory_order_relaxed) + 1;
        telemetry.recentKicks[(size_t) (count % 4)].store (when, std::memory_order_relaxed);
        telemetry.lastKickSample.store (when, std::memory_order_relaxed);
        telemetry.kickEvents.store (count, std::memory_order_release);
    }
    // No phase analysis (the ducking alone, nothing turned): PHASE off; no
    // kick heard (the tempo's beats: Auto without Kick Lock Send nor the
    // sidechain, or Beat — the kick's own time is not known); or no bass on
    // the track (a pad, a synth sidechained: under SUB CUT less than a
    // quarter of its level, or under -50 dB). Alan: "le but est de
    // sidechainer aussi d'autre chose que des subs"
    {
        const bool bass = subRms > 1.0e-5f && subRms >= 0.25f * bassRms;
        const int reason = ! phaseOn ? 1 : source == 2 ? 2 : ! bass ? 3 : 0;
        telemetry.analysisReason.store (reason, std::memory_order_relaxed);
        if (reason != 0)
        {
            rampTo (0.0, when + releaseFor (when), returnFor (when));
            rampPending = false;
            kickPhase = 0.0;
            prevKick = when;
            lastConfidence = 1.0;
            telemetry.phaseConfidence.store (1.0f, std::memory_order_relaxed);
            telemetry.phaseErrorDeg.store (0.0f, std::memory_order_relaxed);
            telemetry.kickPhaseDeg.store (0.0f, std::memory_order_relaxed);
            return;
        }
    }
    // the bass known after the kick: the room before it is heard (+ what was
    // waited for, see below)
    const int room = measureLength() + extra;

    // The phase of the fundamental at the kick, measured on the cycle that
    // follows it: what is heard with the kick, wherever the note started (the
    // 8 cycles before it used to be needed: a note starting 10-150 ms before
    // the kick was not locked, the same note sometimes locked, sometimes not)
    double Tm = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
    bool youngUnknown = false; // a note too young for its period to be known
    lastSource = 0;
    // The period of the note heard at the kick, from the bass itself: the
    // longest stretch ending with the cycle after the kick that holds one
    // steady note (80, 60, 45, 35 ms, on its low band). The pitch detector
    // lags ~50-100 ms: a note started just before the kick — after a gap, or
    // legato (no level jump: Alan's sub melody, 30-80 deg off) — was measured
    // at the note before's period.
    const auto to = when + room;
    const bool young = when - lastNoteStart < (std::int64_t) (0.1 * sampleRate);
    // (a note's attack left out: a growing note pulls the estimate 1-3 % short)
    const auto earliest = young ? lastNoteStart + (std::int64_t) (0.005 * sampleRate) : to - (std::int64_t) (0.08 * sampleRate);
    double local = 0.0;
    std::int64_t localFrom = to;
    const int waitMax = (int) (0.025 * sampleRate);
    const bool lastChance = extra >= waitMax;
    // 1. a note held across the kick: the long stretch before it (80, 60 ms)
    // (not again on a second look at a held note: it did not hold the first
    // time; a young note has more of itself by then)
    for (double span : { 0.08, 0.06 })
    {
        if (extra > 0 && ! young)
            break;
        const auto from = juce::jmax (earliest, to - (std::int64_t) (span * sampleRate));
        if (from == localFrom)
            continue;
        localFrom = from;
        const double T = periodBetween (from, to);
        // (as steady as a held note is — 0.0001-0.003; a stretch over two
        // legato notes passed 0.04; < 0: too short to tell — a young note, trusted)
        if (T > 1.0 && steadiness (from, to, T) < (young ? steadyLimit : 0.25 * steadyLimit))
        {
            local = T;
            break;
        }
    }
    // 2. a note changed near the kick: the note heard after the kick (what the
    // lock is for), at least 1.6 of its periods — waited for if need be, see
    // below — then as far back as the same note goes (40, 15 ms)
    if (local <= 1.0)
    {
        // (6 ms in: the low band's filter, both ways, smears a legato note
        // before into the first ms of this one — 0.5-1 % on its period)
        // (a young note: all of it — it started before the kick, after a gap)
        const auto postFrom = young ? earliest : juce::jmax (earliest, when + (std::int64_t) (0.006 * sampleRate));
        const double post = periodBetween (postFrom, to);
        if (post > 1.0 && (double) (to - postFrom) >= 1.6 * post) // (less: 1-4 % off, 100 deg at 41 Hz)
        {
            const double r = steadiness (postFrom, to, post);
            const bool held = ! young && postFrom > to - (std::int64_t) (0.075 * sampleRate);
            if (r < (held ? 0.25 * steadyLimit : steadyLimit))
            {
                local = post;
                localFrom = postFrom;
                for (double back : { 0.04, 0.015 })
                {
                    const auto from = juce::jmax (earliest, when - (std::int64_t) (back * sampleRate));
                    if (from >= postFrom)
                        continue;
                    const double T = periodBetween (from, to);
                    if (T > 1.0 && std::abs (T / post - 1.0) < 0.01 && steadiness (from, to, T) < 0.25 * steadyLimit)
                    {
                        local = T;
                        localFrom = from;
                        break;
                    }
                }
            }
        }
    }
    // not yet: wait for more of the bass — up to 25 ms: the sub is ducked
    // under the kick there, the rotation (5 ms) still comes before it is back
    if (local <= 1.0 && ! lastChance && pendingCount < (int) pendingKicks.size())
    {
        // (kept in order: the next kicks are later)
        for (int n = pendingCount; n > 0; --n)
        {
            pendingKicks[(size_t) n] = pendingKicks[(size_t) n - 1];
            pendingExtra[(size_t) n] = pendingExtra[(size_t) n - 1];
            pendingSource[(size_t) n] = pendingSource[(size_t) n - 1];
        }
        pendingKicks[0] = when;
        pendingSource[0] = source;
        pendingExtra[0] = waitMax; // (one more look, not one every 5 ms: each look costs)
        ++pendingCount;
        return;
    }
    // 3. still nothing (a note changed on the kick, too low to be measured in
    // the time there is, or no steady note at all): see below — left as it
    // is, or the pitch detector's period if the note is held
    const bool changedUnknown = local <= 1.0 && ! young && lastChance
                                && steadiness (when - (std::int64_t) (0.04 * sampleRate), when + (std::int64_t) (0.02 * sampleRate), Tm) > steadyLimit
                                && steadiness (when - (std::int64_t) (0.06 * sampleRate), when - (std::int64_t) (0.005 * sampleRate), Tm) < 0.25 * steadyLimit;
    // a short stretch (not cut by a note's start) is a new note only if the
    // one before held steady at another pitch: legato. A glide or a wide FM
    // is steady over 25-35 ms too, and unsteady before — not a note.
    if (local > 1.0 && ! young && localFrom > to - (std::int64_t) (0.075 * sampleRate))
    {
        const auto pEnd = localFrom - (std::int64_t) (0.015 * sampleRate), pFrom = pEnd - (std::int64_t) (0.06 * sampleRate);
        const double before = periodBetween (pFrom, pEnd);
        const double r = before > 1.0 ? steadiness (pFrom, pEnd, before) : 1.0;
        if (! (before > 1.0 && r >= 0.0 && r < 0.25 * steadyLimit && std::abs (before / local - 1.0) > 0.03)) // (held: ~0.0001-0.003; a glide's start ~0.03)
            local = 0.0;
    }
    lastSource = local > 1.0 ? 1 : young ? 2 : 0;
    if (local > 1.0)
    {
        // another note than the one followed (> 1 %): its fundamental
        // re-extracted at its period from where it was found, the pitch
        // detector waiting for it
        if (trackPeriod <= 1.0 || std::abs (local / trackPeriod - 1.0) > 0.01)
        {
            requestRetrack (localFrom, local);
            pitchHoldNote = lastNoteStart;
            pitchHoldUntil = clock + (std::int64_t) (0.15 * sampleRate);
        }
        Tm = local;
    }
    else if (young)
    {
        const auto from = juce::jmax (earliest, when - (std::int64_t) (0.1 * sampleRate));
        if (cleanPeriod > 1.0 && differenceAt (from, to, cleanPeriod) < 0.05)
        {
            Tm = cleanPeriod; // the last note held steadily, if this one repeats with it (the same note again)
            if (lastNoteStart <= when)
            {
                requestRetrack (lastNoteStart, Tm);
                pitchHoldNote = lastNoteStart;
                pitchHoldUntil = clock + (std::int64_t) (0.15 * sampleRate);
            }
        }
        else
            youngUnknown = true;
    }
    else if (changedUnknown)
        youngUnknown = true; // (the note held before the kick, another one after it: left as it is)
    // the tracking still on another note (it changes only with the rotation
    // at 0, and Hold never brings it back there by itself): change it now
    if (! youngUnknown && ! retrackPending && Tm > 1.0 && (trackPeriod <= 1.0 || std::abs (Tm / trackPeriod - 1.0) > 0.01))
        requestRetrack (clock - (std::int64_t) (0.2 * sampleRate), Tm);
    kickPhase = aimAtCollision ? collisionPhase (Tm, when) : 0.0;
    telemetry.kickPhaseDeg.store ((float) kickPhase, std::memory_order_relaxed);
    lastTm = Tm;
    lastNoteAgeMs = (double) (when - lastNoteStart) / sampleRate * 1000.0;
    double phase = 0.0, amplitude = 0.0, conf = 0.0;
    measureForward (when, Tm, juce::jlimit (16, juce::jmax (16, room), (int) std::round (Tm)), phase, amplitude);
    {
        // the phase itself on the low band (zero-phase: the same phase; a raw
        // saw's harmonics left out); its level stays the whole bass's
        double lowPhase = 0.0, lowAmplitude = 0.0;
        measureForward (when, Tm, juce::jlimit (16, juce::jmax (16, room), (int) std::round (Tm)), lowPhase, lowAmplitude, true);
        if (lowAmplitude > 1.0e-6)
            phase = lowPhase;
    }
    const double fundPower = 0.5 * amplitude * amplitude;
    // no note after the kick (a bass in short notes: the next one starts
    // later): nothing to put in phase — in place, not "unstable"
    // (the whole bass quiet, not just its fundamental: a noise sub's 55 Hz
    // part dips by chance)
    double rawPower = 0.0;
    {
        const int len = juce::jlimit (16, juce::jmax (16, room), (int) std::round (Tm));
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
        rampPending = false;
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
        // Steady enough to lock? The low band (the infra: a saw's harmonics,
        // its filter moving, don't count) against itself one period later,
        // over up to ~80 ms (at least 3 periods) before the kick and the one after: the same
        // cycle over and over, or a glide / wide FM / noise sub (not lockable).
        // (It replaced the pitch detector's own checks — aperiodicity, its
        // spread over 50 ms, the cycles before on the whole bass — fooled by
        // a raw saw and stale for a note just retracked: UNSTABLE on Alan's bass)
        conf = 1.0;
        // (from where this note was found steady, when it was: the stretch
        // before is another note — legato — not unsteadiness)
        const auto from = local > 1.0 ? localFrom
                                      : juce::jmax (lastNoteStart + (std::int64_t) (0.005 * sampleRate), when - (std::int64_t) std::llround (juce::jmax (3.0 * Tm, 0.08 * sampleRate)));
        const double r = steadiness (from, when + juce::jlimit (16, juce::jmax (16, room), (int) std::round (Tm)), Tm);
        lastAperiodicity = r;
        lastCoherence = r < 0.0 ? 1.0 : 1.0 - r;
        if (r > steadyLimit)
            conf = 0.5;
        lastPhase = phase;
        lastConfidence = conf;
    }
    else
        lastConfidence = 0.0;
    const double T = Tm; // the period the phase was measured with
    // an unsure measurement with LOCK: Continu keeps the correction where it
    // is (frozen), Transition makes none at this kick
    const bool sure = measured && (! lock || conf >= 0.7);
    // Hold, unsure, on another note than the one the correction was set for
    // (a bass melody: Alan saw it off): not that note's correction — none
    if (! sure && lock && lockMode == continuous && heldPeriod > 1.0 && T > 1.0 && std::abs (T / heldPeriod - 1.0) > 0.01)
    {
        rampPending = false;
        rampTo (0.0, when + releaseFor (when), returnFor (when));
        heldPeriod = 0.0;
    }
    if (sure && lock)
        heldPeriod = T;
    if (sure || ! lock || lockMode == transition)
    {
        const double psi = sure ? targetPsiFor (phase, T) : targetPsiFor (targetPhase + kickPhase, T);
        if (retrackPending)
        {
            // after the tracking has changed (the rotation at 0 first, see requestRetrack)
            rampPending = true;
            pendingPsi = psi;
            pendingHold = when + releaseFor (when);
            pendingReturn = returnFor (when);
        }
        else
            rampTo (psi, when + releaseFor (when), returnFor (when)); // 5 ms, over before the kick is heard
    }
    prevKick = when;
    noteHz = measured ? sampleRate / T : 0.0;
    // error left at the kick, relative to the intended phase
    if (measured)
    {
        const double intended = targetPhase + kickPhase - waveStart - offsetMs * 0.001 * sampleRate / T * 360.0;
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
        if (nch == 2)
        {
            const float side = 0.5f * (buffer.getSample (0, i) - buffer.getSample (1, i));
            sideHistory[(size_t) firPos] = side;
            sideHistory[(size_t) (firPos + firLength)] = side;
        }
        firPos = firPos + 1 == firLength ? 0 : firPos + 1;
        // MONO BELOW: the side's low band (aligned like the sub); a new
        // setting crossfades over 30 ms
        if (nch == 2)
        {
            if (monoBelow != monoTo && monoFade == 0)
            {
                monoFrom = monoTo;
                monoTo = monoBelow;
                monoFade = monoFadeLength;
            }
            auto low = [&] (int m)
            {
                if (m <= 0)
                    return 0.0f;
                const float* h = sideHistory.data() + firPos;
                const float* k = monoKernels[(size_t) m].data();
                float a0 = 0.0f, a1 = 0.0f;
                int j = 0;
                for (; j + 2 <= firLength; j += 2)
                {
                    a0 += h[j] * k[j];
                    a1 += h[j + 1] * k[j + 1];
                }
                for (; j < firLength; ++j)
                    a0 += h[j] * k[j];
                return a0 + a1;
            };
            float lowSide = monoTo > 0 || monoFade > 0 ? low (monoTo) : 0.0f;
            if (monoFade > 0)
            {
                const float a = (float) monoFade / (float) monoFadeLength;
                lowSide = a * low (monoFrom) + (1.0f - a) * lowSide;
                --monoFade;
            }
            sideLow[(size_t) (aligned & mask)] = lowSide;
        }
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
        // PRECISE: the sub at ~3 kHz, and its Hilbert transform hilM samples
        // behind (it needs that much of the future)
        if (precise)
        {
            for (int c = 0; c < nch; ++c)
                dAcc[(size_t) c] += sub[(size_t) c][(size_t) (aligned & mask)];
            if (++dPhase >= decimation)
            {
                dPhase = 0;
                if (dWritten == 0)
                    dFirst = (double) aligned - 0.5 * (decimation - 1);
                for (int c = 0; c < nch; ++c)
                {
                    dSub[(size_t) c][(size_t) (dWritten & dMask)] = dAcc[(size_t) c] / (float) decimation;
                    dAcc[(size_t) c] = 0.0f;
                }
                ++dWritten;
                const auto k = dWritten - 1 - hilM;
                designPair(); // (SUB CUT moved)
                if (k >= 0)
                    for (int c = 0; c < nch; ++c)
                    {
                        const auto& x = dSub[(size_t) c];
                        float low = lowKernel[0] * x[(size_t) (k & dMask)], hil = 0.0f;
                        for (int m = 1; m <= hilM; ++m)
                        {
                            const float before = x[(size_t) ((k - m) & dMask)], after = x[(size_t) ((k + m) & dMask)];
                            low += lowKernel[(size_t) m] * (before + after);
                            hil += hilbertKernel[(size_t) m] * (before - after);
                        }
                        dLow[(size_t) c][(size_t) (k & dMask)] = low;
                        dHil[(size_t) c][(size_t) (k & dMask)] = hil;
                    }
            }
        }
        // the fundamental (see the header): z = bass x e^-i theta summed, then
        // at tau = clock - lookahead the window [tau - T/2, tau + T/2]. Only
        // notes under SUB CUT, once YIN knows them (else it fades out: rest +
        // fundamental stays the bass exactly whatever it holds)
        // (the detector's new period followed only with the rotation at 0)
        if (periodSamples > 1.0 && ! juce::exactlyEqual (periodSamples, trackPeriod) && (trackPeriod <= 1.0 || rotationIdle()))
            trackPeriod = periodSamples;
        if (! precise) // (PRECISE turns the low band with its Hilbert transform: no tracking)
        {
            const double T = trackPeriod;
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
        {
            float raw = 0.0f; // the whole bass, aligned with the sub (its share under SUB CUT: is there a bass at all?)
            for (int c = 0; c < nch; ++c)
                raw += input[(size_t) c][(size_t) (aligned & mask)] / (float) nch;
            bassRms += (raw * raw - bassRms) * rmsCoeff;
        }
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
                if (phaseOn)
                    updatePitch(); // (PHASE off: no pitch to follow)
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
                addTrigger (onset, 1);
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
                    addTrigger (onset, 3);
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
                    addTrigger (clock, 2);
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
                    addTrigger (clock, 2);
                }
            }
        }
        if (pendingCount > 0 && precise && clock >= pendingKicks[0] + (latency - linkSlack - fadeLength - (std::int64_t) (0.0015 * sampleRate)))
        {
            const auto when = pendingKicks[0];
            const int source = pendingSource[0];
            for (int n = 1; n < pendingCount; ++n)
            {
                pendingKicks[(size_t) n - 1] = pendingKicks[(size_t) n];
                pendingExtra[(size_t) n - 1] = pendingExtra[(size_t) n];
                pendingSource[(size_t) n - 1] = pendingSource[(size_t) n];
            }
            --pendingCount;
            onKickPrecise (when, source);
        }
        else if (pendingCount > 0 && ! precise && clock >= pendingKicks[0] + measureLength() + pendingExtra[0] + (std::int64_t) (0.001 * sampleRate))
        {
            const auto when = pendingKicks[0];
            const int extra = pendingExtra[0], source = pendingSource[0];
            for (int n = 1; n < pendingCount; ++n)
            {
                pendingKicks[(size_t) n - 1] = pendingKicks[(size_t) n];
                pendingExtra[(size_t) n - 1] = pendingExtra[(size_t) n];
                pendingSource[(size_t) n - 1] = pendingSource[(size_t) n];
            }
            --pendingCount;
            onKick (when, extra, source);
        }
        // a retrack waiting for the rotation at 0 (see requestRetrack) — here,
        // after this sample's tracking: retrack recomputes up to it included
        // (before it, the sample was counted twice: a step in the sums, a click)
        if (retrackPending && rotationIdle())
        {
            retrackPending = false;
            retrack (juce::jmax (retrackFrom, clock - (std::int64_t) (0.2 * sampleRate)), retrackT);
            if (rampPending)
            {
                rampPending = false;
                rampTo (pendingPsi, pendingHold, pendingReturn);
            }
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
        float scopeSub = 0.0f, scopeLow = 0.0f;
        // MONO BELOW: the side's low band out (L - it, R + it); the
        // fundamental's rotation the same on both sides (it is under it)
        const bool monoLow = nch == 2 && (monoTo > 0 || monoFade > 0);
        const float lowSide = monoLow ? sideLow[(size_t) (t & mask)] : 0.0f;
        // the rotation's term per channel: LIVE, the fundamental (tracked);
        // PRECISE, the whole low band with its Hilbert transform
        float turn[2] {};
        if (rotate)
            for (int c = 0; c < nch; ++c)
            {
                const auto ti = (size_t) (t & mask);
                turn[c] = precise ? (cr - 1.0f) * lowAt (c, t) + sr * hilbertAt (c, t)
                                  : (cr - 1.0f) * fund[(size_t) c][ti] + sr * fundQ[(size_t) c][ti];
            }
        if (monoLow && rotate)
            turn[0] = turn[1] = 0.5f * (turn[0] + turn[1]);
        for (int c = 0; c < nch; ++c)
        {
            const auto ti = (size_t) (t & mask);
            const float x = input[(size_t) c][ti] + (monoLow ? (c == 0 ? -lowSide : lowSide) : 0.0f);
            const float e = fund[(size_t) c][ti], q = fundQ[(size_t) c][ti];
            // the fundamental (LIVE) or the low band (PRECISE) rotated by psi instead of itself
            outs[c] = duck * (rotate ? x + turn[c] : x);
            scopeSub += (cr * e + sr * q) / (float) nch;
            // the output's low band (under SUB CUT: the sub, its rotation included)
            scopeLow += duck * (sub[(size_t) c][ti] + (rotate ? (cr - 1.0f) * e + sr * q : 0.0f)) / (float) nch;
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

        // the kick as heard, 1 in 8 (its shape where the sub comes back: the
        // lock's aim, see collisionPhase)
        if ((t % KickTelemetry::scopeDecimation) == 0)
        {
            float kv = kickRing[(size_t) (t & mask)];
            if (trigger != triggerSidechain && trigger != triggerBeat && timing.isPlaying && telemetry.linkSlot.load (std::memory_order_relaxed) >= 0)
            {
                float lv = 0.0f;
                if (link.waveAt (timing.beatsAt (i) - (double) latency * timing.beatsPerSample, lv))
                    kv = lv;
            }
            heardKick[(size_t) ((t / KickTelemetry::scopeDecimation) & heardMask)] = kv;
        }
        // oscilloscope (1 in 8): the sub as heard, the bass's low band, the kick
        if (++scopeCounter >= KickTelemetry::scopeDecimation && telemetry.scopeSize > 0)
        {
            scopeCounter = 0;
            const int w = telemetry.scopeWrite.load (std::memory_order_relaxed);
            telemetry.scopeSub[(size_t) w].store (scopeSub, std::memory_order_relaxed);
            // the bass as heard, its lowest part only (Alan: a saw's highs hid
            // where its cycle is against the kick): its fundamental as heard
            // (rotated, ducked) while one is followed, else the band under SUB CUT
            telemetry.scopeBass[(size_t) w].store (trackGain > 0.5f ? duck * scopeSub : scopeLow, std::memory_order_relaxed);
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
    else if (telemetry.analysisReason.load (std::memory_order_relaxed) != 0)
        st = Status::ducking; // the sidechain alone (PHASE off, no kick heard, no bass)
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
