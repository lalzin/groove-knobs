#include "BackwashDsp.h"

namespace bw
{
using namespace gk::dspmath;

BackwashPreset BackwashPreset::fromJson (const juce::var& root)
{
    BackwashPreset p;
    auto num = [] (const juce::var& section, const char* key, float fallback)
    {
        const auto v = section.getProperty (key, fallback);
        return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : fallback;
    };
    const auto swell = root.getProperty ("swell", {});
    p.beats = juce::jlimit (0.125f, 8.0f, num (swell, "beats", p.beats));
    p.riseSharpness = juce::jlimit (0.5f, 12.0f, num (swell, "riseSharpness", p.riseSharpness));
    p.toneStartHz = juce::jlimit (100.0f, 20000.0f, num (swell, "toneStartHz", p.toneStartHz));
    p.toneEndHz = juce::jlimit (100.0f, 20000.0f, num (swell, "toneEndHz", p.toneEndHz));
    const auto input = root.getProperty ("input", {});
    p.focus = juce::jlimit (0.0f, 1.0f, num (input, "focus", p.focus));
    p.washHpfHz = juce::jlimit (20.0f, 2000.0f, num (input, "washHpfHz", p.washHpfHz));
    const auto level = root.getProperty ("level", {});
    p.maxWetDb = juce::jlimit (-24.0f, 6.0f, num (level, "maxWetDb", p.maxWetDb));
    return p;
}

//==============================================================================
/** Background IR builder (real-time mode): builds the requested IR off the
    audio thread and deletes the retired ones. */
class BackwashDsp::Builder final : public juce::Thread
{
public:
    explicit Builder (BackwashDsp& d) : juce::Thread ("Backwash IR builder"), dsp (d) {}

    ~Builder() override
    {
        stopThread (2000);
        delete ready.exchange (nullptr);
        for (auto& g : graveyard)
            delete g.exchange (nullptr);
    }

    struct Request { int preset = -1; bool longSwell = false; double bpm = 120.0; };

    /** Audio thread: lock-free (atomics). A request superseded while being
        built is simply rebuilt: the serial number moves on. */
    void request (const Request& r) noexcept
    {
        pendingPreset.store (r.preset, std::memory_order_relaxed);
        pendingLong.store (r.longSwell, std::memory_order_relaxed);
        pendingBpm.store (r.bpm, std::memory_order_relaxed);
        requestSerial.fetch_add (1, std::memory_order_release);
        wake.signal();
    }

    /** Audio thread: takes a finished IR, if any. */
    std::unique_ptr<BackwashIr> takeReady() noexcept { return std::unique_ptr<BackwashIr> (ready.exchange (nullptr)); }

    /** Audio thread: hands an IR over for deletion. False if no slot is free. */
    bool retire (BackwashIr* ir) noexcept
    {
        for (auto& g : graveyard)
        {
            BackwashIr* expected = nullptr;
            if (g.compare_exchange_strong (expected, ir))
            {
                wake.signal();
                return true;
            }
        }
        return false;
    }

    void run() override
    {
        int built = 0;
        while (! threadShouldExit())
        {
            wake.wait (100);
            for (auto& g : graveyard)
                delete g.exchange (nullptr);

            const int serial = requestSerial.load (std::memory_order_acquire);
            Request r { pendingPreset.load (std::memory_order_relaxed), pendingLong.load (std::memory_order_relaxed),
                        pendingBpm.load (std::memory_order_relaxed) };
            if (serial == built || r.preset < 0)
                continue;
            auto ir = std::make_unique<BackwashIr> (BackwashDsp::buildIr (dsp.presets[(size_t) r.preset], r.preset, r.longSwell, r.bpm,
                                                                          dsp.sampleRate, dsp.lookahead, dsp.partitionSize));
            built = serial;
            delete ready.exchange (ir.release());
        }
    }

private:
    BackwashDsp& dsp;
    juce::WaitableEvent wake;
    std::atomic<int> pendingPreset { -1 }, requestSerial { 0 };
    std::atomic<bool> pendingLong { false };
    std::atomic<double> pendingBpm { 120.0 };
    std::atomic<BackwashIr*> ready { nullptr };
    std::array<std::atomic<BackwashIr*>, 8> graveyard {};
};

//==============================================================================
BackwashDsp::BackwashDsp() = default;

BackwashDsp::~BackwashDsp()
{
    builder.reset(); // stops the thread before the IRs it may reference go away
}

int BackwashDsp::grainSamplesFor (double sr)
{
    return juce::jmax (64, (int) (0.040 * sr));
}

int BackwashDsp::partitionSizeFor (double sr)
{
    // ~10 ms partitions: 512 at 44.1 / 48 kHz, 1024 at 88.2 / 96, 2048 at 176.4 / 192
    int p = 512;
    while (p < 8192 && sr / p > 100.0)
        p <<= 1;
    return p;
}

BackwashIr BackwashDsp::buildIr (const BackwashPreset& p, int presetIndex, bool longSwell, double bpm, double sr, int lookahead, int P)
{
    // The send is delayed by one shimmer grain (so the octave-up, which lags
    // by up to a grain, is never late): the IR moves that much earlier.
    const int grain = grainSamplesFor (sr);
    BackwashIr ir;
    ir.preset = presetIndex;
    ir.longSwell = longSwell;
    ir.bpm = bpm;

    const double seconds = (double) p.beats * (longSwell ? 2.0 : 1.0) * 60.0 / juce::jmax (20.0, bpm);
    int swell = (int) std::round (seconds * sr);
    const int maxSwell = lookahead - P - grain - 1; // the convolver latency and the send delay come out of the lookahead
    ir.clamped = swell > maxSwell;
    swell = juce::jlimit (16, maxSwell, swell);
    ir.swellSamples = swell;

    // IR taps 0..L-1; the swell occupies [L - swell, L). The convolver adds
    // P samples of latency and the send `grain` samples, so the kernel is the
    // IR shifted by P + grain.
    const int length = lookahead - P - grain;
    std::vector<float> irL ((size_t) length, 0.0f), irR ((size_t) length, 0.0f);
    const int start = length - swell;
    // The wash high-pass lives in the IR (same result as filtering the send,
    // the convolution is linear) so the final cut stays dead: nothing rings
    // after the hit.
    gk::TptSvf hpL, hpR;
    const auto hpc = gk::TptSvf::Coeffs::make (p.washHpfHz, sr, 0.7071);

    gk::testsignals::Rng noiseL (9001u), noiseR (4242u);
    gk::TptSvf lpL, lpR;
    const int segment = juce::jmax (1, (int) (0.002 * sr));
    const int fade = juce::jmax (1, (int) (0.001 * sr));
    const double k = p.riseSharpness, norm = 1.0 / (std::exp (k) - 1.0);
    const double nyquistGuard = 0.45 * sr;
    double energy = 0.0;

    for (int j = 0; j < swell; ++j)
    {
        const double pos = (double) j / (double) (swell - 1);
        const double env = (std::exp (k * pos) - 1.0) * norm;
        const double cut = j >= swell - fade ? (double) (swell - 1 - j) / (double) fade : 1.0; // 1 ms fade: the dead cut on the hit

        // Darker at the start, brighter towards the hit: per-2-ms segments,
        // cutoff interpolated in log frequency inside the segment
        const int seg = j / segment;
        const double p0 = juce::jmin (1.0, (double) (seg * segment) / swell), p1 = juce::jmin (1.0, (double) ((seg + 1) * segment) / swell);
        const double f0 = juce::jmin (nyquistGuard, (double) logLerp (p.toneStartHz, p.toneEndHz, (float) p0));
        const double f1 = juce::jmin (nyquistGuard, (double) logLerp (p.toneStartHz, p.toneEndHz, (float) p1));
        const double frac = (double) (j - seg * segment) / segment;
        const auto c = gk::TptSvf::Coeffs::make (f0 * std::pow (f1 / f0, frac), sr, 0.7071);

        const double l = hpL.processHighpass (lpL.processLowpass (noiseL.bipolar(), c) * env, hpc) * cut;
        const double r = hpR.processHighpass (lpR.processLowpass (noiseR.bipolar(), c) * env, hpc) * cut;
        irL[(size_t) (start + j)] = (float) l;
        irR[(size_t) (start + j)] = (float) r;
        energy += 0.5 * (l * l + r * r);
    }

    // Common energy reference: comparable level for every preset / length
    constexpr double referenceEnergy = 4.0; // +6 dB after listening (DECISIONS.md)
    const float scale = (float) std::sqrt (referenceEnergy / juce::jmax (1.0e-12, energy));
    for (int i = start; i < length; ++i)
    {
        irL[(size_t) i] *= scale;
        irR[(size_t) i] *= scale;
    }

    ir.kernel = gk::PartitionedConvolver::Kernel::build (irL.data(), irR.data(), length, P);
    return ir;
}

void BackwashDsp::setPresets (const gk::PresetManager& pm)
{
    presets.clear();
    for (int i = 0; i < pm.size(); ++i)
        presets.push_back (BackwashPreset::fromJson (pm[i].json));
    if (presets.empty())
        presets.emplace_back();
}

void BackwashDsp::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    builder.reset();
    sampleRate = newSampleRate;
    numChannels = juce::jlimit (1, 2, newNumChannels);
    lookahead = (int) std::round (lookaheadSeconds * sampleRate);
    partitionSize = partitionSizeFor (sampleRate);

    convolver.prepare (partitionSize, (lookahead - partitionSize) / partitionSize + 2);
    dryDelay.prepare (numChannels, lookahead);
    dryBuffer.setSize (numChannels, juce::jmax (1, maxBlockSize), false, true, false);

    // Transient designer: same release, different attacks -> they agree on
    // anything sustained, the fast one jumps ahead on attacks only.
    fastEnv.prepare (sampleRate, 0.001, 0.100);
    slowEnv.prepare (sampleRate, 0.060, 0.100);
    rectifyCoeff = (float) (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));
    limiterAttack = (float) std::exp (-1.0 / (0.0002 * sampleRate));
    limiterRelease = (float) std::exp (-1.0 / (0.120 * sampleRate));
    hitFast.prepare (sampleRate, 0.001, 0.030);
    hitSlow.prepare (sampleRate, 0.060, 0.300);
    grainSamples = grainSamplesFor (sampleRate);
    shimmerLine.prepare (sampleRate, 0.1);
    for (auto* s : { &focus, &hpfLogHz, &wetGain, &toneLogHz, &width, &shimmerMix })
        s->prepare (sampleRate, 0.030);

    // Initial IR, synchronously (prepare is not real time)
    wantedPreset = presetIndex;
    wantedLong = false;
    activeBpm = candidateBpm = gk::HostSync::fallbackBpm;
    currentIr = std::make_unique<BackwashIr> (buildIr (presets[(size_t) presetIndex], presetIndex, wantedLong, activeBpm, sampleRate, lookahead,
                                                       partitionSize));
    previousIr.reset();
    convolver.setKernel (currentIr->kernel.get());

    builder = std::make_unique<Builder> (*this);
    builder->startThread (juce::Thread::Priority::low);
    reset();
}

void BackwashDsp::reset()
{
    convolver.reset();
    if (currentIr != nullptr)
        convolver.setKernel (currentIr->kernel.get());
    dryDelay.reset();
    fastEnv.reset();
    slowEnv.reset();
    rectified = 0.0f;
    limiterEnv = 0.0f;
    hitFast.reset();
    hitSlow.reset();
    hpf.reset();
    for (auto& f : toneLpf)
        f.reset();
    shimmerLine.reset();
    shimmerPhase = 0.0;
    for (auto* s : { &focus, &hpfLogHz, &wetGain, &toneLogHz, &width, &shimmerMix })
        s->invalidate();
    counter = 0;
    hitCount = 0;
    hitHoldoff = 0;
    inputClock = 0;
    telemetry.samplesToNextHit.store (-1);
}

void BackwashDsp::setToggleValues (const float* const* perSample, int numToggles)
{
    numToggleData = juce::jmin (2, numToggles);
    for (int t = 0; t < numToggleData; ++t)
        toggleData[t] = perSample[t];
}

void BackwashDsp::retire (std::unique_ptr<BackwashIr> ir)
{
    if (ir == nullptr)
        return;
    if (nonRealtime.load() || builder == nullptr)
        return; // offline: plain delete is fine (unique_ptr goes out of scope)
    auto* raw = ir.release();
    if (! builder->retire (raw))
        delete raw; // no free slot (very unlikely): accept a delete here
}

void BackwashDsp::installIr (std::unique_ptr<BackwashIr> ir, bool fade)
{
    if (fade && convolver.isFading())
    {
        // A fade is still running (e.g. a preset change immediately followed
        // by its LONG default): queue this IR, it fades in when the current
        // fade is over. Jumping to the end of a fade would click.
        retire (std::move (queuedIr));
        queuedIr = std::move (ir);
        return;
    }
    if (previousIr != nullptr)
        retire (std::move (previousIr));
    previousIr = std::move (currentIr);
    currentIr = std::move (ir);
    if (fade)
        convolver.fadeTo (currentIr->kernel.get(), (int) (irFadeSeconds * sampleRate));
    else
    {
        convolver.setKernel (currentIr->kernel.get());
        retire (std::move (previousIr));
    }
}

void BackwashDsp::requestIr (int preset, bool longSwell, double bpm)
{
    if (nonRealtime.load() || builder == nullptr)
    {
        // Offline / tests: build now, deterministic
        installIr (std::make_unique<BackwashIr> (buildIr (presets[(size_t) preset], preset, longSwell, bpm, sampleRate, lookahead, partitionSize)),
                   true);
        return;
    }
    builder->request ({ preset, longSwell, bpm });
}

void BackwashDsp::processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());

    // --- IR management: preset, LONG, tempo (debounced) --------------------
    if (builder != nullptr)
        if (auto ready = builder->takeReady())
            installIr (std::move (ready), true);
    if (previousIr != nullptr && ! convolver.isFading())
        retire (std::move (previousIr));
    if (queuedIr != nullptr && ! convolver.isFading())
        installIr (std::move (queuedIr), true);

    const bool longNow = numToggleData > longToggle && toggleData[longToggle][numSamples - 1] >= 0.5f;
    if (std::abs (timing.bpm - candidateBpm) > 0.5)
    {
        candidateBpm = timing.bpm;
        candidateSince = sampleClock;
    }
    const bool tempoSettled = nonRealtime.load() || (double) (sampleClock - candidateSince) >= tempoDebounceSeconds * sampleRate;
    const bool tempoChanged = std::abs (candidateBpm - activeBpm) > 0.5 && tempoSettled;
    if (presetIndex != wantedPreset || longNow != wantedLong || tempoChanged)
    {
        wantedPreset = presetIndex;
        wantedLong = longNow;
        if (tempoChanged)
            activeBpm = candidateBpm;
        requestIr (wantedPreset, wantedLong, activeBpm);
    }
    sampleClock += numSamples;

    if (currentIr != nullptr)
    {
        telemetry.swellSamples.store (currentIr->swellSamples, std::memory_order_relaxed);
        telemetry.swellClamped.store (currentIr->clamped, std::memory_order_relaxed);
    }

    // --- Dry path, delayed by the lookahead ----------------------------------
    for (int c = 0; c < nch; ++c)
        dryDelay.process (c, buffer.getReadPointer (c), dryBuffer.getWritePointer (c), numSamples);
    dryDelay.advance (numSamples);

    const auto& p = presets[(size_t) presetIndex];
    const float shimmerOn = numToggleData > shimmerToggle ? 1.0f : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float a = clamp01 (amount[i]);
        if ((counter++ % 8u) == 0)
        {
            focus.setTarget (p.focus);
            hpfLogHz.setTarget (std::log (p.washHpfHz));
            const float ss = smoothstep (0.0f, 1.0f, a);
            wetGain.setTarget (dbToGain (p.maxWetDb) * ss * ss); // maxWetDb + 40·log10(smoothstep)
            toneLogHz.setTarget (std::log (logLerp (4000.0f, 18000.0f, std::pow (a, 0.7f))));
            width.setTarget (smoothstep (0.3f, 0.9f, a));
            shimmerMix.setTarget (0.5f * smoothstep (0.5f, 1.0f, a));
        }
        const float shim = shimmerOn > 0.0f ? toggleData[shimmerToggle][i] : 0.0f;

        float x = 0.0f;
        for (int c = 0; c < nch; ++c)
            x += buffer.getReadPointer (c)[i];
        x /= (float) nch;

        // UI: hits detected on the input, heard `lookahead` samples later
        {
            const float f = hitFast.process (x), sl = hitSlow.process (x);
            if (hitHoldoff > 0)
                --hitHoldoff;
            else if (f > 0.02f && f > 1.8f * sl)
            {
                hits[(size_t) (hitCount++ % (int) hits.size())] = { inputClock, juce::jmin (1.0f, f * 2.0f) };
                hitHoldoff = (int) (0.1 * sampleRate);
            }
        }
        ++inputClock;

        // Transient focus: keep the attacks only. Gate on how far the fast
        // envelope is above the slow one: nothing below 35 %, fully open at
        // 75 % (a held note, a slow fade-in or a sine's ripple stay closed).
        rectified += (std::abs (x) - rectified) * rectifyCoeff; // ~4 ms average: no waveform ripple
        const float fe = fastEnv.process (rectified), se = slowEnv.process (rectified);
        const float g = smoothstep (0.35f, 0.75f, (fe - se) / juce::jmax (1.0e-6f, fe));
        const float fo = focus.next();
        float prep = (1.0f - fo) * x + fo * (x * g);

        // (the wash high-pass is built into the IR, see buildIr)
        hpfLogHz.next();

        // Send delayed by one grain; SHIMMER (octave up: two crossed taps,
        // Hann windows, ~40 ms grains) reads earlier, so it is never late.
        shimmerLine.push (prep);
        const float sm = shimmerMix.next() * shim;
        prep = shimmerLine.read ((double) grainSamples);
        if (sm > 0.0f)
        {
            shimmerPhase = frac (shimmerPhase + 1.0 / grainSamples);
            float up = 0.0f;
            for (int t = 0; t < 2; ++t)
            {
                const double ph = frac (shimmerPhase + 0.5 * t);
                const double d = 1.0 + (grainSamples - 2) * (1.0 - ph); // delay shrinks 1 sample per sample: ×2 speed
                const float w = (float) std::sin (pi * ph);
                up += w * w * shimmerLine.read (d);
            }
            prep += sm * up;
        }

        float wl = 0.0f, wr = 0.0f;
        convolver.process (prep, wl, wr);

        // Tone (opens with the knob), width (M/S), gain
        const auto tc = gk::TptSvf::Coeffs::make (juce::jmin (0.45 * sampleRate, (double) std::exp (toneLogHz.next())), sampleRate, 0.7071);
        wl = (float) toneLpf[0].processLowpass (wl, tc);
        wr = (float) toneLpf[1].processLowpass (wr, tc);
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width.next();
        const float gain = wetGain.next();
        wl = gain * (mid + side);
        wr = gain * (mid - side);

        // Wet-only peak limiter (linked L/R, ceiling -8 dBFS): very loud
        // swells (dense vocals at 100 %) are tamed, the dry is untouched and
        // the output soft clip stays idle.
        const float wetPeak = juce::jmax (std::abs (wl), std::abs (wr));
        limiterEnv = wetPeak > limiterEnv ? wetPeak + (limiterEnv - wetPeak) * limiterAttack : wetPeak + (limiterEnv - wetPeak) * limiterRelease;
        if (limiterEnv > wetCeiling)
        {
            const float limitGain = wetCeiling / limiterEnv;
            wl *= limitGain;
            wr *= limitGain;
        }

        if (nch == 2)
        {
            buffer.getWritePointer (0)[i] = dryBuffer.getReadPointer (0)[i] + wl;
            buffer.getWritePointer (1)[i] = dryBuffer.getReadPointer (1)[i] + wr;
        }
        else
            buffer.getWritePointer (0)[i] = dryBuffer.getReadPointer (0)[i] + 0.5f * (wl + wr);
    }

    updateHits (numSamples);
}

void BackwashDsp::updateHits (int)
{
    // A hit that entered at input index h is heard when the input clock
    // reaches h + lookahead.
    std::int64_t best = -1;
    float strength = 0.0f;
    const int n = juce::jmin (hitCount, (int) hits.size());
    for (int k = 0; k < n; ++k)
    {
        const auto [h, s] = hits[(size_t) k];
        const std::int64_t due = h + lookahead - inputClock;
        if (due >= 0 && (best < 0 || due < best))
        {
            best = due;
            strength = s;
        }
    }
    telemetry.samplesToNextHit.store (best < 0 ? -1 : (int) best, std::memory_order_relaxed);
    telemetry.nextHitStrength.store (strength, std::memory_order_relaxed);
}
} // namespace bw
