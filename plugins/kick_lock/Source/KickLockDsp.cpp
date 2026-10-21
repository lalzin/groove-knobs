#include "KickLockDsp.h"

namespace kl
{
using namespace gk::dspmath;

KickLockDsp::KickLockDsp() = default;

float KickLockDsp::curve (int s, float x) noexcept
{
    if (x <= 0.0f)
        return 1.0f;
    if (x >= 1.0f)
        return 0.0f;
    switch (s)
    {
        case linear: return 1.0f - x;
        case sCurve: return 1.0f - smoothstep (0.0f, 1.0f, x);
        case bounce:
            // two falling arcs: down to 0 at 55 %, a small return (0.3) and down again
            if (x < 0.55f)
                return 1.0f - smoothstep (0.0f, 1.0f, x / 0.55f);
            return 0.3f * std::sin (juce::MathConstants<float>::pi * (x - 0.55f) / 0.45f);
        default:
        {
            // exp(-3x), normalised so it lands on 0 at the end (no step)
            static const float e3 = std::exp (-3.0f);
            return (std::exp (-3.0f * x) - e3) / (1.0f - e3);
        }
    }
}

double KickLockDsp::releaseBeats (int index) noexcept
{
    static const double beats[] { 0.25, 0.75, 0.5, 1.0, 2.0 }; // 1/16, 1/8 dotted, 1/8, 1/4, 1/2
    return beats[juce::jlimit (0, 4, index)];
}

void KickLockDsp::designKernel (std::vector<float>& k, double cutoff, double sr)
{
    const int N = (int) k.size(), D = N / 2;
    // Blackman: transition ~5.5 sr / N; centred above the cutoff, so that
    // everything below it (the fundamental) passes entirely
    const double transition = 5.5 * sr / N;
    const double fc = (cutoff + 0.5 * transition) / sr;
    double sum = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const int m = n - D;
        const double sinc = m == 0 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
        const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / (N - 1)) + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * n / (N - 1));
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
    latency = firDelay + (int) std::round (advanceSeconds * sr);
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
    }
    mono.assign ((size_t) size, 0.0f);
    envRing.assign ((size_t) size, 0.0f);
    decimation = juce::jmax (1, (int) std::round (sr / 3000.0));
    dec.assign (512, 0.0f);
    fadeLength = juce::jmax (1, (int) std::round (crossfadeSeconds * sr));
    scAttack = (float) (1.0 - std::exp (-1.0 / (0.0002 * sr)));
    scRelease = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
    slowRelease = (float) (1.0 - std::exp (-1.0 / (0.06 * sr)));
    rmsCoeff = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
    dryDelay.prepare (numChannels, latency);
    dry.setSize (numChannels, juce::jmax (1, maxBlock), false, true, false);
    telemetry.scopeSize = (int) (2.0 * sr) / KickTelemetry::scopeDecimation;
    telemetry.scopeSub = std::make_unique<std::atomic<float>[]> ((size_t) telemetry.scopeSize);
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
    }
    std::fill (mono.begin(), mono.end(), 0.0f);
    std::fill (envRing.begin(), envRing.end(), 0.0f);
    std::fill (dec.begin(), dec.end(), 0.0f);
    dryDelay.reset();
    clock = 0;
    scEnv = slowEnv = 0.0f;
    armed = true;
    lastKick = -1000000;
    deadUntil = -1;
    kickCount = 0;
    decCounter = 0;
    yinHop = 0;
    decWrite = 0;
    periodSamples = 0.0;
    yinAperiodicity = 1.0f;
    recentCount = 0;
    shiftNow = shiftFrom = shiftTo = lockShift = 0.0;
    fadeStart = -1;
    lastPhase = lastConfidence = 0.0;
    subRms = midRms = sideRms = lrRms = 0.0f;
    scopeCounter = 0;
    scopeKickPeak = 0.0f;
    for (int i = 0; i < telemetry.scopeSize; ++i)
    {
        telemetry.scopeSub[(size_t) i].store (0.0f, std::memory_order_relaxed);
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
    shape = n > shapeChoice ? juce::jlimit (0, 3, idx[shapeChoice]) : exponential;
    release = n > releaseChoice ? juce::jlimit (0, 4, idx[releaseChoice]) : 2;
}

void KickLockDsp::setSidechain (const float* const* channels, int n, int)
{
    scChannels = juce::jlimit (0, 2, n);
    for (int c = 0; c < scChannels; ++c)
        sc[(size_t) c] = channels[c];
}

float KickLockDsp::subAt (int c, double pos) const noexcept
{
    // Hermite (4 points) at absolute (aligned) time `pos`
    const double f = std::floor (pos);
    const auto i = (std::int64_t) f;
    const auto& r = sub[(size_t) c];
    const float x0 = r[(size_t) ((i - 1) & mask)], x1 = r[(size_t) (i & mask)], x2 = r[(size_t) ((i + 1) & mask)], x3 = r[(size_t) ((i + 2) & mask)];
    const float t = (float) (pos - f);
    const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

void KickLockDsp::updatePitch() noexcept
{
    // YIN on the decimated sub (~3 kHz): 35-160 Hz, 128-sample window (~43 ms)
    const double rate = sampleRate / decimation;
    const int window = 128, tauMin = (int) std::floor (rate / 160.0), tauMax = juce::jmin (180, (int) std::ceil (rate / 35.0));
    std::array<float, 512> frame {};
    const int len = window + tauMax;
    for (int i = 0; i < len; ++i)
        frame[(size_t) i] = dec[(size_t) ((decWrite - len + i) & 511)];
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
    recentPeriods[(size_t) (recentCount % (int) recentPeriods.size())] = periodSamples;
    ++recentCount;
}

bool KickLockDsp::measurePhase (std::int64_t at, double T, double& phaseDeg, double& confidence) const noexcept
{
    // The fundamental's phase at `at`: demodulation over two periods (Hann),
    // i.e. the analytic signal at f0 (harmonics in the sub band do not bias
    // it as zero crossings would). Confidence: the same phase measured at the
    // 8 previous cycles (8 x T earlier) — a steady sub gives the same phase
    if (T < 2.0)
        return false;
    const double w = juce::MathConstants<double>::twoPi / T;
    const int W = (int) std::round (2.0 * T);
    auto phaseAt = [&] (std::int64_t end)
    {
        double I = 0.0, Q = 0.0;
        for (int k = 0; k < W; ++k)
        {
            const double n = -(double) k; // relative to `end`
            const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) k / (double) W);
            const double s = win * monoSubAt (end - k);
            I += s * std::cos (w * n);
            Q += s * std::sin (w * n);
        }
        return std::atan2 (I, Q);
    };
    double re = 0.0, im = 0.0;
    for (int j = 0; j < 8; ++j)
    {
        // `at - j*T` is j whole cycles earlier: the same phase if the sub is steady
        const double p = phaseAt (at - (std::int64_t) std::llround (j * T)) + w * (std::llround (j * T) - j * T);
        re += std::cos (p);
        im += std::sin (p);
    }
    confidence = std::sqrt (re * re + im * im) / 8.0;
    phaseDeg = phaseAt (at) * 180.0 / juce::MathConstants<double>::pi;
    return true;
}

double KickLockDsp::targetShiftFor (double phaseDeg, double T) const noexcept
{
    // + = the sub later. Lock: the shortest move (< half a cycle) that brings
    // the fundamental to the target phase at the kick; WAVE START and OFFSET
    // are added on top (deliberate)
    const double base = waveStart / 360.0 * T + offsetMs * 0.001 * sampleRate;
    double s = base + (lock ? wrap180 (phaseDeg - targetPhase) / 360.0 * T : 0.0);
    // the sub can be advanced by the latency at most: below that, a cycle later
    const double minShift = -(double) (latency - firDelay) + 4.0, maxShift = maxDelaySeconds * sampleRate - latency - 4.0;
    while (s < minShift && T > 1.0)
        s += T;
    while (s > maxShift && T > 1.0 && s - T >= minShift)
        s -= T;
    return juce::jlimit (minShift, maxShift, s);
}

void KickLockDsp::onKick (std::int64_t when)
{
    telemetry.kickEvents.fetch_add (1, std::memory_order_relaxed);
    telemetry.lastKickSample.store (when, std::memory_order_relaxed);

    // A note starting at the kick (the sub rising sharply between just before
    // and just after it): what was measured before belongs to the previous
    // note or to silence, and a synth note starting on the kick starts in
    // place — back to the neutral shift (WAVE START / OFFSET only)
    {
        auto rms = [&] (std::int64_t from, std::int64_t to)
        {
            double e = 0.0;
            for (auto n = from; n < to; ++n)
                e += (double) monoSubAt (n) * monoSubAt (n);
            return std::sqrt (e / (double) juce::jmax<std::int64_t> (1, to - from));
        };
        const auto ms = [&] (double m) { return (std::int64_t) (m * 0.001 * sampleRate); };
        const double before = rms (when - ms (40.0), when - ms (15.0)), after = rms (when - ms (2.0), when + ms (noteLookSeconds * 1000.0));
        // from silence; or after a periodic sub (a noise sub's level jumps
        // about by itself: no note there, the freeze below applies)
        const bool fromSilence = before < dbToGain (-50.0f);
        if (after > 1.0e-4 && (fromSilence ? after > 2.0 * before : (after > 3.0 * before && yinAperiodicity <= 0.3f)))
        {
            const double T = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
            const double neutral = targetShiftFor (targetPhase, T); // lock part 0: phase = target
            if (std::abs (neutral - shiftTo) > 0.25)
            {
                shiftFrom = shiftNow;
                shiftTo = neutral;
                fadeStart = clock;
            }
            lastConfidence = 0.0; // no measurement for this kick
            telemetry.phaseConfidence.store (0.0f, std::memory_order_relaxed);
            return;
        }
    }

    // The sub's phase at this kick (heard in L samples), and the shift for it
    double phase = 0.0, conf = 0.0;
    const bool weak = subRms < dbToGain (-50.0f);
    const bool measured = ! weak && measurePhase (when, periodSamples, phase, conf);
    if (measured)
    {
        // a steady phase over 8 cycles, and a periodic sub (YIN): a noise
        // sub can line up its phases by chance, never both
        if (yinAperiodicity > 0.3f)
            conf = juce::jmin (conf, 0.5);
        // and a fundamental that holds still (a glide, a wide FM: not lockable)
        const int n = juce::jmin (recentCount, (int) recentPeriods.size());
        if (n >= 3)
        {
            const auto [lo, hi] = std::minmax_element (recentPeriods.begin(), recentPeriods.begin() + n);
            if ((*hi - *lo) / (0.5 * (*hi + *lo)) > 0.03)
                conf = juce::jmin (conf, 0.5);
        }
        lastPhase = phase;
        lastConfidence = conf;
    }
    else
        lastConfidence = 0.0;
    const double T = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
    double next = shiftTo;
    if (measured && (! lock || conf >= 0.7))
        next = targetShiftFor (phase, T);
    else if (! lock)
        next = targetShiftFor (0.0, T); // no lock: only WAVE START / OFFSET
    // (unsure measurement with LOCK: the shift stays where it is — frozen)
    if (std::abs (next - shiftTo) > 0.25)
    {
        shiftFrom = shiftNow;
        shiftTo = next;
        fadeStart = clock; // 5 ms crossfade, over before the kick is heard
    }
    // error left at the kick, relative to the intended phase
    if (measured)
    {
        const double intended = targetPhase - waveStart - offsetMs * 0.001 * sampleRate / T * 360.0;
        const double atKick = phase - shiftTo / T * 360.0;
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
    for (int c = 0; c < nch; ++c)
        dryDelay.process (c, buffer.getReadPointer (c), dry.getWritePointer (c), numSamples);
    dryDelay.advance (numSamples);

    const double releaseSamples = juce::jmax (1.0, releaseBeats (release) * 60.0 / bpm * sampleRate);
    const double attack = attackSeconds * sampleRate;
    const float threshold = dbToGain (thresholdDb);
    const auto noKickSamples = (std::int64_t) (noKickSeconds * sampleRate);
    float kickPeak = 0.0f, gainMin = 1.0f;

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
        mono[(size_t) (aligned & mask)] = monoSub;
        subRms += (monoSub * monoSub - subRms) * rmsCoeff;
        // pitch: decimated (box average, the sub is far below its Nyquist)
        if (++decCounter >= decimation)
        {
            decCounter = 0;
            float acc = 0.0f;
            for (int k = 0; k < decimation; ++k)
                acc += mono[(size_t) ((aligned - k) & mask)];
            dec[(size_t) (decWrite & 511)] = acc / (float) decimation;
            ++decWrite;
            if (++yinHop >= 32)
            {
                yinHop = 0;
                updatePitch();
            }
        }

        // --- the kick ------------------------------------------------------
        float k = 0.0f;
        for (int c = 0; c < scChannels; ++c)
            k = juce::jmax (k, std::abs (sc[(size_t) c][i]));
        scEnv += (k - scEnv) * (k > scEnv ? scAttack : scRelease);
        envRing[(size_t) (clock & mask)] = scEnv;
        kickPeak = juce::jmax (kickPeak, scEnv);
        // the 1 ms envelope follows each cycle of the kick's body (it dips at
        // every zero crossing): a new kick only once the body has gone (slow
        // envelope under half the threshold), or on a new attack well above
        // what remains of it (a kick over a long 808 tail)
        const float slowBefore = slowEnv;
        slowEnv += (scEnv - slowEnv) * (scEnv > slowEnv ? scAttack : slowRelease);
        if (slowEnv < 0.5f * threshold)
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
            kicks[(size_t) (kickCount % (int) kicks.size())] = onset;
            ++kickCount;
            lastKick = onset;
            if (pendingCount < (int) pendingKicks.size())
                pendingKicks[(size_t) pendingCount++] = onset;
        }
        if (pendingCount > 0 && aligned >= pendingKicks[0] + (std::int64_t) (noteLookSeconds * sampleRate))
        {
            const auto when = pendingKicks[0];
            for (int n = 1; n < pendingCount; ++n)
                pendingKicks[(size_t) n - 1] = pendingKicks[(size_t) n];
            --pendingCount;
            onKick (when);
        }

        // --- output: L samples behind --------------------------------------
        const std::int64_t t = clock - latency; // output time (input time of what is heard now)
        if (fadeStart >= 0)
        {
            const double a = (double) (clock - fadeStart) / fadeLength;
            if (a >= 1.0)
            {
                fadeStart = -1;
                shiftNow = shiftTo;
            }
        }
        float g = 1.0f; // ducking
        if (clock - lastKick < noKickSamples)
            for (int n = 0; n < juce::jmin (kickCount, (int) kicks.size()); ++n)
            {
                const double d = (double) (t - kicks[(size_t) n]);
                float c = 0.0f;
                if (d < 0.0 && -d < attack)
                    c = smoothstep (0.0f, 1.0f, (float) (1.0 + d / attack)); // down to the kick
                else if (d >= 0.0 && d < releaseSamples)
                    c = curve (shape, (float) (d / releaseSamples));
                g = juce::jmin (g, 1.0f - depth * c);
            }
        gainMin = juce::jmin (gainMin, g);
        const float mix = clamp01 (amount[i]);
        float outs[2] {};
        float scopeSub = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const float x = input[(size_t) c][(size_t) (t & mask)];
            const float s0 = sub[(size_t) c][(size_t) (t & mask)];
            float s;
            if (fadeStart >= 0)
            {
                const float a = (float) (clock - fadeStart) / (float) fadeLength;
                s = (1.0f - a) * subAt (c, (double) t - shiftFrom) + a * subAt (c, (double) t - shiftTo);
            }
            else
                s = juce::exactlyEqual (shiftNow, 0.0) ? s0 : subAt (c, (double) t - shiftNow);
            const float wet = g * (x - s0 + s); // mids + the shifted sub, ducked
            outs[c] = dry.getSample (c, i) + mix * (wet - dry.getSample (c, i));
            scopeSub += s / (float) nch;
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
        scopeKickPeak = juce::jmax (scopeKickPeak, scEnv);
        if (++scopeCounter >= KickTelemetry::scopeDecimation && telemetry.scopeSize > 0)
        {
            scopeCounter = 0;
            const int w = telemetry.scopeWrite.load (std::memory_order_relaxed);
            telemetry.scopeSub[(size_t) w].store (scopeSub, std::memory_order_relaxed);
            telemetry.scopeKick[(size_t) w].store (scopeKickPeak, std::memory_order_relaxed);
            telemetry.scopeWrite.store ((w + 1) % telemetry.scopeSize, std::memory_order_relaxed);
            scopeKickPeak = 0.0f;
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
    telemetry.shiftMs.store ((float) (shiftTo / sampleRate * 1000.0), std::memory_order_relaxed);
    telemetry.duckGain.store (gainMin, std::memory_order_relaxed);
    telemetry.outputClock.store (clock - latency, std::memory_order_relaxed);
    Status st = Status::ok;
    const float err = std::abs (telemetry.phaseErrorDeg.load (std::memory_order_relaxed));
    if (scChannels == 0 || clock - lastKick > noKickSamples)
        st = Status::noKick;
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
