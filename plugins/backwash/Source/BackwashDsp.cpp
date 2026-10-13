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
        delete readyCycle.exchange (nullptr);
        for (auto& g : graveyard)
            delete g.exchange (nullptr);
        for (auto& g : cycleGraveyard)
            delete g.exchange (nullptr);
    }

    /** Audio thread: asks for a new LIVE render (latest memory and IR). */
    void requestRender() noexcept
    {
        renderSerial.fetch_add (1, std::memory_order_release);
        wake.signal();
    }

    /** Audio thread: takes a finished LIVE render, if any. */
    std::unique_ptr<BackwashWetCycle> takeReadyCycle() noexcept { return std::unique_ptr<BackwashWetCycle> (readyCycle.exchange (nullptr)); }

    /** Audio thread: hands a cycle over for deletion. False if no slot is free. */
    bool retire (BackwashWetCycle* c) noexcept
    {
        for (auto& g : cycleGraveyard)
        {
            BackwashWetCycle* expected = nullptr;
            if (g.compare_exchange_strong (expected, c))
            {
                wake.signal();
                return true;
            }
        }
        return false;
    }

    struct Request { int preset = -1; bool longSwell = false; double bpm = 120.0; double beats = 1.0; };

    /** Audio thread: lock-free (atomics). A request superseded while being
        built is simply rebuilt: the serial number moves on. */
    void request (const Request& r) noexcept
    {
        pendingPreset.store (r.preset, std::memory_order_relaxed);
        pendingLong.store (r.longSwell, std::memory_order_relaxed);
        pendingBpm.store (r.bpm, std::memory_order_relaxed);
        pendingBeats.store (r.beats, std::memory_order_relaxed);
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
            for (auto& g : cycleGraveyard)
                delete g.exchange (nullptr);
            delete dsp.spentMemory.exchange (nullptr);

            // LIVE render. The IR is loaded after the graveyard sweep: a
            // retired IR is only deleted by this thread, at the next sweep.
            if (const int rs = renderSerial.load (std::memory_order_acquire); rs != rendered)
            {
                rendered = rs;
                const int len = dsp.memoryLength.load();
                const auto* ir = dsp.renderIr.load();
                if (len > 0 && ir != nullptr)
                {
                    const auto& mem = dsp.memoryBuffers[(size_t) dsp.memoryActive.load()];
                    auto c = BackwashDsp::renderWetCycle (mem.data(), len, dsp.memoryCycleBeatsShared.load(), *ir,
                                                          dsp.presets[(size_t) juce::jmax (0, ir->preset)], dsp.sampleRate, dsp.lookahead,
                                                          dsp.partitionSize, dsp.targetShared.load(), dsp.beatsPerBarShared.load());
                    delete readyCycle.exchange (c.release());
                }
            }

            const int serial = requestSerial.load (std::memory_order_acquire);
            Request r { pendingPreset.load (std::memory_order_relaxed), pendingLong.load (std::memory_order_relaxed),
                        pendingBpm.load (std::memory_order_relaxed), pendingBeats.load (std::memory_order_relaxed) };
            if (serial == built || r.preset < 0)
                continue;
            auto preset = dsp.presets[(size_t) r.preset];
            preset.beats = (float) r.beats;
            auto ir = std::make_unique<BackwashIr> (BackwashDsp::buildIr (preset, r.preset, r.longSwell, r.bpm,
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
    std::atomic<double> pendingBpm { 120.0 }, pendingBeats { 1.0 };
    std::atomic<BackwashIr*> ready { nullptr };
    std::array<std::atomic<BackwashIr*>, 8> graveyard {};
    std::atomic<int> renderSerial { 0 };
    int rendered = 0;
    std::atomic<BackwashWetCycle*> readyCycle { nullptr };
    std::array<std::atomic<BackwashWetCycle*>, 4> cycleGraveyard {};
};

//==============================================================================
BackwashDsp::BackwashDsp() = default;

BackwashDsp::~BackwashDsp()
{
    builder.reset(); // stops the thread before the IRs it may reference go away
    delete pendingMemory.exchange (nullptr);
    delete spentMemory.exchange (nullptr);
}

//==============================================================================
void BackwashDsp::resampleInto (const float* src, int srcLen, float* dst, int dstLen) noexcept
{
    // Linear, by position in the cycle (a tempo change keeps every hit on
    // its beat)
    if (srcLen <= 0)
    {
        std::fill (dst, dst + dstLen, 0.0f);
        return;
    }
    const double step = (double) srcLen / (double) juce::jmax (1, dstLen);
    for (int j = 0; j < dstLen; ++j)
    {
        const double pos = j * step;
        const int i0 = (int) pos;
        const float f = (float) (pos - i0);
        const float a = src[i0 % srcLen], b = src[(i0 + 1) % srcLen];
        dst[j] = a + f * (b - a);
    }
}

std::vector<float> BackwashDsp::copyMemory() const
{
    const int len = memoryLength.load();
    const int active = memoryActive.load();
    if (! telemetry.liveMemoryReady.load() || len <= 0 || memoryBuffers[(size_t) active].size() < (size_t) len)
        return {};
    const auto& m = memoryBuffers[(size_t) active];
    return std::vector<float> (m.begin(), m.begin() + len);
}

void BackwashDsp::loadMemory (std::vector<float> data)
{
    auto* fresh = data.empty() ? nullptr : new std::vector<float> (std::move (data));
    delete pendingMemory.exchange (fresh);
}

void BackwashDsp::takePendingMemory()
{
    auto* m = pendingMemory.exchange (nullptr);
    if (m == nullptr)
        return;
    const int len = memoryLength.load();
    if (len > 0)
    {
        auto& dst = memoryBuffers[(size_t) memoryActive.load()];
        resampleInto (m->data(), (int) m->size(), dst.data(), len);
        memoryHasContent = true;
        memoryComplete = true;
        telemetry.liveMemoryComplete.store (true);
        renderWanted = true;
        telemetry.liveMemoryReady.store (true);
    }
    else
    {
        // Not sized yet (no block seen): keep it for later
        delete pendingMemory.exchange (m);
        return;
    }
    if (nonRealtime.load() || builder == nullptr)
        delete m;
    else if (auto* old = spentMemory.exchange (m))
        delete old; // the builder had not collected the previous one (unlikely)
}

int BackwashDsp::cycleIndex (double cyclePosition, int length) noexcept
{
    // Rounded, not truncated: at tempos where a cycle is a whole number of
    // samples, positions fall exactly on sample boundaries and truncation
    // would jitter by one sample
    const int q = (int) ((cyclePosition - std::floor (cyclePosition)) * length + 0.5);
    return q >= length ? q - length : q;
}

double BackwashDsp::swellBeatsFor (const BackwashPreset& p, int lengthIndex, double beatsPerBar) noexcept
{
    switch (lengthIndex)
    {
        case 1: return 0.25;
        case 2: return 0.5;
        case 3: return 1.0;
        case 4: return 2.0;
        case 5: return juce::jmax (0.25, beatsPerBar);
        default: return p.beats; // PRESET
    }
}

void BackwashDsp::retireCycle (std::unique_ptr<BackwashWetCycle> c)
{
    if (c == nullptr || nonRealtime.load() || builder == nullptr)
        return; // offline: plain delete
    auto* raw = c.release();
    if (! builder->retire (raw))
        delete raw;
}

void BackwashDsp::installCycle (std::unique_ptr<BackwashWetCycle> c)
{
    // Crossfade from the previous render (its content may differ: a new bar,
    // a new IR)
    if (previousCycle != nullptr)
        retireCycle (std::move (previousCycle));
    previousCycle = std::move (cycle);
    cycle = std::move (c);
    cycleFade = previousCycle != nullptr ? 0.0f : 1.0f;
}

void BackwashDsp::requestRender()
{
    renderWanted = false;
    if (! memoryHasContent)
        return;
    if (nonRealtime.load() || builder == nullptr)
    {
        // Offline / tests: render now, deterministic
        if (currentIr != nullptr && memoryLength.load() > 0)
            installCycle (renderWetCycle (memoryBuffers[(size_t) memoryActive.load()].data(), memoryLength.load(), memoryCycleBeats, *currentIr,
                                          presets[(size_t) juce::jmax (0, currentIr->preset)], sampleRate, lookahead, partitionSize,
                                          targetIndex, beatsPerBarShared.load()));
        return;
    }
    builder->requestRender();
}

void BackwashDsp::updateMemoryLength (const gk::BlockTiming& timing)
{
    // One bar (halved while longer than the capacity), at the settled tempo
    double cycleBeats = juce::jmax (0.25, timing.beatsPerBar) * (targetIndex == targetEveryTwoBars ? 2.0 : 1.0);
    const double bpm = juce::jmax (20.0, activeBpm);
    auto lengthFor = [&] (double beats) { return (int) std::round (beats * 60.0 / bpm * sampleRate); };
    while (lengthFor (cycleBeats) > memoryCapacity && cycleBeats > 0.25)
        cycleBeats *= 0.5;
    const int len = juce::jlimit (1, memoryCapacity, lengthFor (cycleBeats));
    const int oldLen = memoryLength.load();
    if (len == oldLen && juce::exactlyEqual (cycleBeats, memoryCycleBeats))
        return;

    const int active = memoryActive.load();
    auto& src = memoryBuffers[(size_t) active];
    auto& dst = memoryBuffers[(size_t) (1 - active)];
    if (memoryHasContent && oldLen > 0)
        resampleInto (src.data(), oldLen, dst.data(), len);
    else
        std::fill (dst.begin(), dst.begin() + len, 0.0f);
    memoryCycleBeats = cycleBeats;
    memoryCycleBeatsShared.store (cycleBeats);
    memoryLength.store (len);
    memoryActive.store (1 - active);
    lastWrite = -1;
    renderWanted = true;
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
    ir.taps = { std::move (irL), std::move (irR) };
    return ir;
}

float BackwashDsp::targetGate (double beats, double beatsPerBar, int target) noexcept
{
    // Which hits get a swell, by musical position: a window from 1/8 beat
    // before the target beat (early claps, swing) to 3/8 after (the attack and
    // its body), soft edges
    if (target <= targetAll)
        return 1.0f;
    const double bar = juce::jmax (0.25, beatsPerBar);
    const double cycle = target == targetEveryTwoBars ? 2.0 * bar : bar;
    double pos = std::fmod (beats, cycle);
    if (pos < 0.0)
        pos += cycle;
    auto window = [&] (double beat)
    {
        double d = pos - beat;
        if (d > 0.5 * cycle)
            d -= cycle;
        else if (d < -0.5 * cycle)
            d += cycle;
        return smoothstep (-0.16f, -0.125f, (float) d) * (1.0f - smoothstep (0.375f, 0.41f, (float) d));
    };
    switch (target)
    {
        case targetOne:
        case targetEveryTwoBars: return window (0.0);
        case targetTwoFour: return juce::jmax (window (1.0), bar > 3.0 ? window (3.0) : 0.0f);
        case targetLast: return window (bar - 1.0);
        default: return 1.0f;
    }
}

std::unique_ptr<BackwashWetCycle> BackwashDsp::renderWetCycle (const float* memory, int M, double cycleBeats, const BackwashIr& ir,
                                                               const BackwashPreset& p, double sr, int lookahead, int P, int target,
                                                               double beatsPerBar)
{
    auto out = std::make_unique<BackwashWetCycle>();
    out->length = M;
    out->cycleBeats = cycleBeats;
    for (auto& c : out->channels)
        c.assign ((size_t) M, 0.0f);
    if (M <= 0 || ir.taps[0].empty())
        return out;

    // 1. The send chain (focus, one-grain delay, octave up) over the looped
    //    memory, exactly as in processWet. Two passes: the first one settles
    //    the envelopes and the shimmer line, as on a loop that keeps playing.
    const int grain = grainSamplesFor (sr);
    std::vector<float> prep ((size_t) M), up ((size_t) M);
    {
        gk::EnvelopeFollower fe, se;
        fe.prepare (sr, 0.001, 0.100);
        se.prepare (sr, 0.060, 0.100);
        const float rc = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
        float rect = 0.0f;
        gk::ModDelay line;
        line.prepare (sr, 0.1);
        double phase = 0.0;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < M; ++i)
            {
                const float x = memory[i] * targetGate ((double) i / M * cycleBeats, beatsPerBar, target);
                rect += (std::abs (x) - rect) * rc;
                const float a = fe.process (rect), b = se.process (rect);
                const float g = smoothstep (0.35f, 0.75f, (a - b) / juce::jmax (1.0e-6f, a));
                line.push ((1.0f - p.focus) * x + p.focus * (x * g));
                phase = frac (phase + 1.0 / grain);
                float u = 0.0f;
                for (int t = 0; t < 2; ++t)
                {
                    const double ph = frac (phase + 0.5 * t);
                    const float w = (float) std::sin (pi * ph);
                    u += w * w * line.read (1.0 + (grain - 2) * (1.0 - ph));
                }
                if (pass == 1)
                {
                    prep[(size_t) i] = line.read ((double) grain);
                    up[(size_t) i] = u;
                }
            }
    }

    // 2. The convolver gives y(t) = sum_j taps[j] * prep(t - P - j), heard
    //    with the dry delayed by L. Here the dry is not delayed, so the wet at
    //    position q is that sum at t = q + L: a circular correlation with the
    //    IR folded on the cycle, h[(L - P - j) mod M] += taps[j]:
    //        wet[q] = sum_m h[m] * prep[(q + m) mod M]
    //    computed as a linear convolution of h reversed with prep looped
    //    twice (outputs M-1 .. 2M-2), by FFT.
    int order = 1;
    while ((1 << order) < 2 * M)
        ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    auto spectrum = [&] (std::vector<float>& buf) { fft.performRealOnlyForwardTransform (buf.data(), true); };
    auto looped = [&] (const std::vector<float>& sig)
    {
        std::vector<float> b ((size_t) (2 * N), 0.0f);
        for (int i = 0; i < 2 * M; ++i)
            b[(size_t) i] = sig[(size_t) (i % M)];
        spectrum (b);
        return b;
    };
    const auto prepSpec = looped (prep), upSpec = looped (up);
    const int D = lookahead - P;

    for (int c = 0; c < 2; ++c)
    {
        std::vector<float> h ((size_t) (2 * N), 0.0f);
        const auto& taps = ir.taps[(size_t) c];
        for (int j = 0; j < (int) taps.size(); ++j)
        {
            int m = (D - j) % M;
            if (m < 0)
                m += M;
            h[(size_t) (M - 1 - m)] += taps[(size_t) j]; // reversed
        }
        spectrum (h);
        for (int k = 0; k < 2; ++k)
        {
            const auto& sigSpec = k == 0 ? prepSpec : upSpec;
            std::vector<float> prod ((size_t) (2 * N), 0.0f);
            for (int bin = 0; bin <= N / 2; ++bin)
            {
                const float ar = h[(size_t) (2 * bin)], ai = h[(size_t) (2 * bin + 1)];
                const float br = sigSpec[(size_t) (2 * bin)], bi = sigSpec[(size_t) (2 * bin + 1)];
                prod[(size_t) (2 * bin)] = ar * br - ai * bi;
                prod[(size_t) (2 * bin + 1)] = ar * bi + ai * br;
            }
            fft.performRealOnlyInverseTransform (prod.data()); // normalised by 1/N
            auto& dst = out->channels[(size_t) (2 * k + c)];
            for (int q = 0; q < M; ++q)
                dst[(size_t) q] = prod[(size_t) (q + M - 1)];
        }
    }
    return out;
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

    // LIVE memory: keep what was captured across a re-prepare
    {
        auto kept = copyMemory();
        memoryCapacity = (int) std::ceil (memoryMaxSeconds * sampleRate) + 2;
        for (auto& m : memoryBuffers)
            m.assign ((size_t) memoryCapacity, 0.0f);
        memoryLength.store (0);
        memoryActive.store (0);
        memoryCycleBeats = 0.0;
        memoryHasContent = false;
        memoryComplete = cycleStarted = false;
        telemetry.liveMemoryReady.store (false);
        telemetry.liveMemoryComplete.store (false);
        if (! kept.empty() && pendingMemory.load() == nullptr)
            loadMemory (std::move (kept));
    }

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
    wantedBeats = swellBeatsFor (presets[(size_t) presetIndex], lengthIndex, 4.0);
    {
        auto p0 = presets[(size_t) presetIndex];
        p0.beats = (float) wantedBeats;
        currentIr = std::make_unique<BackwashIr> (buildIr (p0, presetIndex, wantedLong, activeBpm, sampleRate, lookahead, partitionSize));
    }
    previousIr.reset();
    convolver.setKernel (currentIr->kernel.get());
    renderIr.store (currentIr.get());
    cycle.reset();
    previousCycle.reset();
    cycleFade = 1.0f;
    cycleFadeStep = (float) (1.0 / (irFadeSeconds * sampleRate));
    liveGate.prepare (sampleRate, 0.010);
    liveGate.snap (0.0f);
    lastWrite = -1;
    renderWanted = true;

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
    renderIr.store (currentIr.get()); // before the old one can be retired
    renderWanted = true;
    if (fade)
        convolver.fadeTo (currentIr->kernel.get(), (int) (irFadeSeconds * sampleRate));
    else
    {
        convolver.setKernel (currentIr->kernel.get());
        retire (std::move (previousIr));
    }
}

void BackwashDsp::requestIr (int preset, bool longSwell, double bpm, double beats)
{
    if (nonRealtime.load() || builder == nullptr)
    {
        // Offline / tests: build now, deterministic
        auto p = presets[(size_t) preset];
        p.beats = (float) beats;
        installIr (std::make_unique<BackwashIr> (buildIr (p, preset, longSwell, bpm, sampleRate, lookahead, partitionSize)), true);
        return;
    }
    builder->request ({ preset, longSwell, bpm, beats });
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
    const double beatsNow = swellBeatsFor (presets[(size_t) presetIndex], lengthIndex, timing.beatsPerBar);
    if (presetIndex != wantedPreset || longNow != wantedLong || tempoChanged || std::abs (beatsNow - wantedBeats) > 1.0e-6)
    {
        wantedPreset = presetIndex;
        wantedLong = longNow;
        wantedBeats = beatsNow;
        if (tempoChanged)
            activeBpm = candidateBpm;
        requestIr (wantedPreset, wantedLong, activeBpm, wantedBeats);
    }
    if (std::abs (timing.beatsPerBar - beatsPerBarShared.load()) > 1.0e-9 || targetIndex != renderedTarget)
    {
        beatsPerBarShared.store (timing.beatsPerBar);
        targetShared.store (targetIndex);
        renderedTarget = targetIndex;
        renderWanted = true;
    }
    sampleClock += numSamples;

    if (currentIr != nullptr)
    {
        telemetry.swellSamples.store (currentIr->swellSamples, std::memory_order_relaxed);
        telemetry.swellClamped.store (currentIr->clamped, std::memory_order_relaxed);
    }

    // --- LIVE memory: sized to one cycle at the settled tempo ---------------
    updateMemoryLength (timing);
    takePendingMemory();
    const int memLen = memoryLength.load();
    float* memory = memoryBuffers[(size_t) memoryActive.load()].data();
    // LOCK freezes the memory, but an empty memory (LOCK on from the start)
    // first records one whole cycle: the "listen" pass
    const bool recording = timing.isPlaying && (! locked || ! memoryComplete);
    const double lookaheadBeats = lookahead * timing.beatsPerSample;

    // --- Dry path: delayed by the lookahead (LIVE: as is) -------------------
    if (liveMode)
    {
        for (int c = 0; c < nch; ++c)
            dryBuffer.copyFrom (c, 0, buffer, c, 0, numSamples);
    }
    else
    {
        for (int c = 0; c < nch; ++c)
            dryDelay.process (c, buffer.getReadPointer (c), dryBuffer.getWritePointer (c), numSamples);
        dryDelay.advance (numSamples);
    }

    // --- LIVE: rendered cycles -------------------------------------------
    if (builder != nullptr)
        if (auto ready = builder->takeReadyCycle())
            installCycle (std::move (ready));
    if (previousCycle != nullptr && cycleFade >= 1.0f)
        retireCycle (std::move (previousCycle));
    if (liveMode && renderWanted)
        requestRender();
    liveGate.setTarget (timing.isPlaying ? 1.0f : 0.0f);

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

        // Memory: one cycle of the input, by musical position (recorded in
        // both modes, so LIVE is ready when switched on; LOCK freezes it).
        const double beats = timing.beatsAt (i);
        {
            const int w = cycleIndex (beats / memoryCycleBeats, memLen);
            if (liveMode)
            {
                // What will be heard `lookahead` from now: drives the UI breath
                const double ahead = beats + lookaheadBeats;
                const float remembered = memory[cycleIndex (ahead / memoryCycleBeats, memLen)];
                if (recording)
                    memory[w] = x;
                x = timing.isPlaying ? remembered * targetGate (ahead, timing.beatsPerBar, targetIndex) : 0.0f;
            }
            else
            {
                if (recording)
                    memory[w] = x;
                x *= targetGate (beats, timing.beatsPerBar, targetIndex); // only the targeted hits get a swell
            }
            if (recording)
            {
                if (! memoryHasContent && std::abs (memory[w]) > 1.0e-5f)
                {
                    memoryHasContent = true;
                    telemetry.liveMemoryReady.store (true, std::memory_order_relaxed);
                }
                if (w < lastWrite - memLen / 2)
                {
                    renderWanted = true; // a whole cycle was recorded: render it
                    if (memoryHasContent && cycleStarted && ! memoryComplete)
                    {
                        memoryComplete = true;
                        telemetry.liveMemoryComplete.store (true, std::memory_order_relaxed);
                    }
                    cycleStarted = true;
                }
                lastWrite = w;
            }
        }

        // UI: hits detected on what will be heard `lookahead` samples later
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

        float wl = 0.0f, wr = 0.0f;
        if (liveMode)
        {
            // LIVE: the rendered cycle, read at this musical position (the
            // send chain and the convolution were done in the background)
            focus.next();
            hpfLogHz.next();
            const float sm = shimmerMix.next() * shim;
            auto read = [&] (const BackwashWetCycle& c, float& l, float& r)
            {
                const auto q = (size_t) cycleIndex (beats / c.cycleBeats, c.length);
                l = c.channels[0][q] + sm * c.channels[2][q];
                r = c.channels[1][q] + sm * c.channels[3][q];
            };
            if (cycle != nullptr)
                read (*cycle, wl, wr);
            if (previousCycle != nullptr && cycleFade < 1.0f)
            {
                float pl = 0.0f, pr = 0.0f;
                read (*previousCycle, pl, pr);
                const float f = smoothstep (0.0f, 1.0f, cycleFade);
                wl = pl + f * (wl - pl);
                wr = pr + f * (wr - pr);
                cycleFade = juce::jmin (1.0f, cycleFade + cycleFadeStep);
            }
            const float gate = liveGate.next(); // transport stopped: no phantom swells
            wl *= gate;
            wr *= gate;
        }
        else
        {
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

            convolver.process (prep, wl, wr);
        }

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
