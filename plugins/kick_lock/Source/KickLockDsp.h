#pragma once

#include <groove_core/groove_core.h>

namespace kl
{
/** Status of the phase indicator (computed on the audio thread). */
enum class Status { ok, shifted, unstable, weakSub, noKick, monoLoss };

/** UI telemetry: lock-free (atomics; the scope rings are read racily, which
    only ever shows a sample from the previous frame). */
struct KickTelemetry
{
    static constexpr int scopeDecimation = 8;
    std::atomic<float> phaseErrorDeg { 0.0f }, phaseConfidence { 0.0f }, phaseVariance { 1.0f };
    std::atomic<float> subLevelDb { -120.0f }, kickLevelDb { -120.0f }, monoSumLossDb { 0.0f };
    std::atomic<float> f0Hz { 0.0f }, shiftMs { 0.0f }, duckGain { 1.0f };
    std::atomic<int> kickEvents { 0 }, status { (int) Status::noKick };
    std::atomic<std::int64_t> lastKickSample { -1 }, outputClock { 0 };
    // oscilloscope: 2 s of the corrected sub and of the kick envelope (1 in 8)
    int scopeSize = 0;
    std::unique_ptr<std::atomic<float>[]> scopeSub, scopeKick;
    std::atomic<int> scopeWrite { 0 };
    std::atomic<std::int64_t> scopeTime { 0 }; // output time of the last scope sample written
};

/** Kick Lock.

        bass ─┬─> linear-phase FIR low-pass (SUB CUT, ±8 ms) ─> sub ─> YIN, phase at each kick
              ├─> fundamental = projection on f0 over one period ─> rotation (lock + WAVE START + OFFSET) ─┐
              └─> rest = bass − fundamental: rest + fundamental = bass exactly ─────────────────────────┤
                                                                            sum ─> ducking (× MIX) ─> out
        kick ─> 1 ms envelope > threshold (30 ms dead time) ─> ducking curve (shape, release, depth)
             └─> at each kick: the sub's phase there (demodulation at the YIN fundamental) ─> shift

    Latency L = 16 ms: the analysis runs 8 ms behind (the FIR), each kick is
    measured as soon as the sub is known just past it and its shift reached
    by a 5 ms crossfade that ends before the kick is heard; the fundamental's
    window needs half a period ahead (up to ~16 ms: notes down to 31 Hz).
    The rotation is only there around each kick: reached just before it,
    held over the release, then glided back to the original phase. */
class KickLockDsp final : public gk::OneKnobEngine
{
public:
    static constexpr int lockSwitch = 0;
    static constexpr int targetKnob = 0, offsetKnob = 1, subCutKnob = 2, depthKnob = 3, waveStartKnob = 4, thresholdKnob = 5;
    static constexpr int shapeChoice = 0, releaseChoice = 1, modeChoice = 2;
    enum LockMode { continuous, transition };
    enum Shape { exponential, linear, sCurve, bounce };

    static constexpr double firSeconds = 0.008, advanceSeconds = 0.008, crossfadeSeconds = 0.005, attackSeconds = 0.004,
                            deadTimeSeconds = 0.030, noKickSeconds = 2.0, maxDelaySeconds = 0.080, kernelFadeSeconds = 0.020,
                            noteLookSeconds = 0.0025; // how far past the kick the sub is looked at (a note starting there)
    static constexpr float shiftMixRamp = 0.25f; // MIX over which the shift comes in

    /** The sub's linear-phase low-pass: everything up to `cutoffHz` passes (the
        fundamental entirely), the transition above (Kaiser window). */
    static void designKernel (std::vector<float>& kernel, double cutoffHz, double sampleRate);

    KickLockDsp();

    /** Ducking curve, x = time since the kick / release (0..1): 1 at the kick, 0 at the end. */
    static float curve (int shape, float x) noexcept;
    /** Release length in beats for the RELEASE choice. */
    static double releaseBeats (int index) noexcept;
    /** Wraps degrees to [-180, 180). */
    static double wrap180 (double deg) noexcept;

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return latency; }
    void setPreset (int) override {}
    void setSwitches (const bool* on, int n) override { lock = n > lockSwitch && on[lockSwitch]; }
    void setKnobs (const float* v, int n) override;
    void setChoices (const int* idx, int n) override;
    void setSidechain (const float* const* channels, int numChannels, int numSamples) override;
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, const float* amount, const gk::BlockTiming& timing) override;

    KickTelemetry& getTelemetry() noexcept { return telemetry; }

    /** Tests: the shift of the fundamental heard now (ms, + = later). */
    double currentShiftMs() const noexcept
    {
        const double T = periodSamples > 1.0 ? periodSamples : sampleRate / 55.0;
        return psiNow / 360.0 * T / sampleRate * 1000.0;
    }
    /** Tests: the rotation of the fundamental heard now (degrees, + = later). */
    double currentRotationDeg() const noexcept { return psiNow; }
    /** Tests: the last phase measurement (degrees at the kick, before correction). */
    double lastMeasuredPhase() const noexcept { return lastPhase; }

private:
    void onKick (std::int64_t when);
    void updatePitch() noexcept;
    bool measurePhase (std::int64_t at, double periodSamples, double& phaseDeg, double& confidence) const noexcept;
    float monoSubAt (std::int64_t t) const noexcept { return mono[(size_t) (t & mask)]; }
    double targetPsiFor (double phaseDeg, double periodSamples) const noexcept;
    void rampTo (double psi, std::int64_t holdUntil, double returnSamples, int fadeSamples = -1) noexcept;
    double releaseSamplesNow() const noexcept;
    std::int64_t releaseFor (std::int64_t kick) const noexcept;
    double returnFor (std::int64_t kick) const noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2, latency = 480;
    std::int64_t clock = 0, mask = 0;

    // parameters
    bool lock = false;
    float targetPhase = 0.0f, offsetMs = 0.0f, subCutoff = 120.0f, depth = 1.0f, waveStart = 0.0f, thresholdDb = -18.0f;
    int shape = exponential, release = 2, lockMode = continuous;
    double bpm = 120.0;

    // signal paths: the FIR (two kernels, crossfaded when SUB CUT moves)
    int firDelay = 384, firLength = 769;
    std::array<std::vector<float>, 2> kernels;
    std::array<float, 2> kernelCutoff { -1.0f, -1.0f };
    int activeKernel = 0, kernelFade = 0, kernelFadeLength = 960;
    std::array<std::vector<float>, 2> firHistory; // 2 x length (contiguous window)
    int firPos = 0;
    float fir (int channel, int kernel) const noexcept;
    std::array<std::int64_t, 4> pendingKicks {};
    int pendingCount = 0;
    std::array<std::vector<float>, 2> input, sub; // rings (absolute time & mask)
    // the fundamental alone, the only part that is shifted: the bass projected
    // on f0 over exactly one period centred on each sample (every harmonic
    // cancels over a whole period, gain 1 at f0, no lag); prefix sums of
    // z = bass x e^-i theta give each window in O(1)
    std::array<std::vector<float>, 2> fund, fundQ; // the fundamental and its quadrature (+90°)
    std::array<std::vector<double>, 2> prefixRe, prefixIm;
    std::array<double, 2> sumRe {}, sumIm {};
    std::vector<double> oscRingRe, oscRingIm;
    int lookahead = 0; // the window's half-length, at most (samples)
    double trackHz = 0.0, oscRe = 1.0, oscIm = 0.0, rotRe = 1.0, rotIm = 0.0, noteHz = 0.0;
    float trackGain = 0.0f, trackGainCoeff = 0.001f;
    int oscRenorm = 0;
    void setTrackHz (double hz) noexcept;
    std::vector<float> mono;                      // mono sub, for the analysis
    std::vector<float> envRing;                   // the kick envelope (to find each kick's start)

    // sidechain (this chunk)
    std::array<const float*, 2> sc {};
    int scChannels = 0;
    float scEnv = 0.0f, scAttack = 0.0f, scRelease = 0.0f;
    float slowEnv = 0.0f, slowRelease = 0.0f; // re-arming: the kick's body must have gone (or a new attack)
    bool armed = true;
    std::int64_t lastKick = -1000000, deadUntil = -1;
    std::array<std::int64_t, 8> kicks {}; // recent kicks (input time), for the ducking
    int kickCount = 0;

    // pitch (YIN on the sub, decimated)
    int decimation = 16, decCounter = 0, yinHop = 0;
    std::vector<float> dec;
    std::int64_t decWrite = 0;
    double periodSamples = 0.0; // at the full rate, 0 = unknown
    std::array<double, 10> recentPeriods {}; // the last ~100 ms of YIN estimates
    int recentCount = 0;
    float yinAperiodicity = 1.0f;

    // the correction: a rotation of the fundamental (degrees, + = later),
    // reached before each kick (5 ms); MODE Continu: held until the next kick;
    // Transition: held over the ducking's release, then back to 0 (the
    // original bass) before the next kick
    double psiNow = 0.0, psiFrom = 0.0, psiTo = 0.0;
    std::int64_t fadeStart = -1, holdEnd = 0, prevKick = -1;
    int fadeNow = 240; // this ramp's length
    double returnLength = 1.0;
    int fadeLength = 240;
    double lastPhase = 0.0, lastConfidence = 0.0;
    float subRms = 0.0f, rmsCoeff = 0.001f, midRms = 0.0f, sideRms = 0.0f, lrRms = 0.0f;

    KickTelemetry telemetry;
    int scopeCounter = 0;
    float scopeKickPeak = 0.0f;
};
} // namespace kl
