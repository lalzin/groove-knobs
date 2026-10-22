#pragma once

#include <groove_core/groove_core.h>
#include "KickLink.h"

namespace kl
{
/** Status of the phase indicator (computed on the audio thread). */
enum class Status { ok, shifted, unstable, weakSub, noKick, monoLoss, ducking }; // ducking: the sidechain alone, no phase analysis (see analysisReason)

/** UI telemetry: lock-free (atomics; the scope rings are read racily, which
    only ever shows a sample from the previous frame). */
struct KickTelemetry
{
    static constexpr int scopeDecimation = 8;
    std::atomic<float> phaseErrorDeg { 0.0f }, phaseConfidence { 0.0f }, phaseVariance { 1.0f };
    std::atomic<float> kickPhaseDeg { 0.0f }; // the kick's own phase where it meets the sub (the lock's aim); 0 = unknown
    // why no phase analysis at the last kick: 0 analysed, 1 PHASE off, 2 no kick heard (the tempo's beats),
    // 3 no bass on the track (nothing under SUB CUT: a pad, a synth — sidechained, not phased)
    std::atomic<int> analysisReason { 0 };
    std::atomic<float> subLevelDb { -120.0f }, kickLevelDb { -120.0f }, monoSumLossDb { 0.0f };
    std::atomic<float> f0Hz { 0.0f }, shiftMs { 0.0f }, duckGain { 1.0f };
    std::atomic<int> kickEvents { 0 }, status { (int) Status::noKick };
    std::atomic<int> triggerSource { 0 }; // what triggered the last event: 0 nothing, 1 the sidechain's kick, 2 the host's beat, 3 the link
    std::atomic<int> linkSlot { -1 };     // the Kick Lock Send followed (-1: none)
    std::atomic<int> linkKicks { 0 };     // kicks received from it (diagnosis)
    std::atomic<bool> hostPlaying { false };
    std::atomic<std::int64_t> lastKickSample { -1 }, outputClock { 0 };
    std::array<std::atomic<std::int64_t>, 4> recentKicks { { -1, -1, -1, -1 } }; // the last kicks (slot = count % 4)
    // oscilloscope: 2 s of the corrected sub, the output and the kick (1 in 8)
    int scopeSize = 0;
    // scopeSub: the fundamental as heard; scopeBass: the output's low band (mono, under SUB CUT); scopeKick: the kick (L later, as heard)
    std::unique_ptr<std::atomic<float>[]> scopeSub, scopeKick, scopeBass;
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
    static constexpr int lockSwitch = 0, phaseSwitch = 1, preciseSwitch = 2;
    static constexpr int targetKnob = 0, offsetKnob = 1, subCutKnob = 2, depthKnob = 3, waveStartKnob = 4, thresholdKnob = 5;
    static constexpr int shapeChoice = 0, releaseChoice = 1, modeChoice = 2, triggerChoice = 3, linkChoice = 4, monoChoice = 5;
    // MONO BELOW: Off, then these frequencies (the low band made mono)
    static constexpr int monoOptions = 7;
    static float monoHz (int index) noexcept { constexpr float hz[monoOptions] { 0.0f, 60.0f, 80.0f, 100.0f, 120.0f, 150.0f, 200.0f }; return hz[juce::jlimit (0, monoOptions - 1, index)]; }
    /** TRIGGER: Auto = Kick Lock Send's kicks (the link) when a Send is on its LINK number, else the
        host's beats; Link (only); Beat (only); Sidechain (the host's sidechain routing, only when chosen:
        Alan's Live did not route it, and anything on it must not hijack the link). */
    enum Trigger { triggerAuto, triggerLink, triggerBeat, triggerSidechain };
    enum LockMode { continuous, transition };
    enum Shape { exponential, linear, sCurve, bounce, custom };

    /** The ducking curve (LFOTool style): points of gain over the release
        (x 0..1, gain 0..1), each segment bent by its `tension` (0 = straight,
        < 0 rises fast then slows, > 0 the opposite). The last gain holds after
        the release, until the next kick. */
    struct CurveNode { float x = 0.0f, y = 0.0f, tension = 0.0f; };
    static constexpr int maxCurveNodes = 32, curveTableSize = 512;
    static std::vector<CurveNode> presetCurve (int shape);
    static float evalCurve (const std::vector<CurveNode>& nodes, float x) noexcept;
    /** The custom curve (SHAPE = Custom), from the message thread (triple-buffered table). */
    void setCustomCurve (const std::vector<CurveNode>& nodes);

    static constexpr double firSeconds = 0.008, advanceSeconds = 0.008, crossfadeSeconds = 0.005, attackSeconds = 0.004,
                            deadTimeSeconds = 0.030, noKickSeconds = 2.0, maxDelaySeconds = 0.080, kernelFadeSeconds = 0.020,
                            noteLookSeconds = 0.0025; // how far past the kick the sub is looked at (a note starting there)
    static constexpr float shiftMixRamp = 0.25f; // MIX over which the shift comes in

    /** The sub's linear-phase low-pass: everything up to `cutoffHz` passes (the
        fundamental entirely), the transition above (Kaiser window). */
    static void designKernel (std::vector<float>& kernel, double cutoffHz, double sampleRate);

    /** Kick Lock Send's kicks arrive up to one host buffer late (tracks
        computed in parallel); up to ~11 ms is followed live (the rotation and
        the ducking's descent still have time). Beyond: this much more latency
        (0 for buffers up to 512 at 48 kHz) — never a guess (a predicted kick
        would duck where a kick was removed at the end of a bar). */
    static int linkSlackFor (double sampleRate, int maxBlock) noexcept
    {
        const int budget = baseLatencyFor (sampleRate) - (int) std::round ((crossfadeSeconds + 0.0015) * sampleRate);
        return maxBlock > budget ? maxBlock - budget + 32 : 0;
    }
    /** Each kick's phase is measured on the cycle that follows it (what is
        heard with the kick, wherever the note started): one period of the
        lowest note measured (40 Hz), then the 5 ms crossfade, before the kick
        is heard. */
    static constexpr double measureFloorHz = 40.0;
    static constexpr double steadyLimit = 0.04; // the fundamental's phase drift (degrees / 600: 24 deg): further, not lockable
    static int baseLatencyFor (double sampleRate) noexcept
    {
        const int legacy = (int) std::round (firSeconds * sampleRate) + (int) std::round (advanceSeconds * sampleRate);
        return juce::jmax (legacy, (int) std::round (sampleRate / measureFloorHz + (crossfadeSeconds + 0.0015) * sampleRate));
    }
    /** The latency for a rate and a host buffer (Kick Lock Send reports the same). */
    static int latencyFor (double sampleRate, int maxBlock) noexcept
    {
        return baseLatencyFor (sampleRate) + linkSlackFor (sampleRate, maxBlock);
    }
    KickLockDsp();

    /** Release length in beats for the RELEASE choice. */
    static double releaseBeats (int index) noexcept;
    /** Wraps degrees to [-180, 180). */
    static double wrap180 (double deg) noexcept;

    void setPresets (const gk::PresetManager&) override {}
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    int getLatencySamples() const override { return latency; }
    void setPreset (int) override {}
    void setSwitches (const bool* on, int n) override;
    int latencyForSwitches (const bool* on, int n) const override { return n > preciseSwitch && on[preciseSwitch] ? preciseLatency : liveLatency; }
    int getMaxLatencySamples() const override { return juce::jmax (liveLatency, preciseLatency); }
    /** PRECISE: no note detection at all — the low band and its Hilbert
        transform (the same band turned 90 deg, whatever it holds: legato,
        glides, a saw), and the rotation chosen on the collision itself: the
        kick against the sub coming back, weighted by the ducking curve, over
        ~90 ms after the kick, both seen ahead. It needs to see them: ~150 ms
        of latency (the host compensates it; for mixing, not for playing). */
    static constexpr double hilbertSeconds = 0.045, collisionSeconds = 0.3; // PRECISE sees this much of the kick's tail ahead (a long kick's: 0.15 lost 4 %)
    static constexpr double templateSeconds = 0.3; // LIVE: the last kick's tail it weighs (no latency: it is past)
    static int preciseLatencyFor (double sampleRate, int maxBlock) noexcept
    {
        const int D = juce::jmax (1, (int) std::round (sampleRate / 3000.0));
        return (int) std::round ((firSeconds + hilbertSeconds + collisionSeconds + crossfadeSeconds + 0.0015) * sampleRate) + 4 * D
               + linkSlackFor (sampleRate, maxBlock);
    }
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
    /** The lock's aim: the kick's phase where the sub comes back under it (default), or a fixed
        phase at the kick's start (TARGET alone: tests of the lock's accuracy). */
    void setAimAtCollision (bool on) noexcept { aimAtCollision = on; }
    static double fitPeriod (const float* x, int n, double rate, double T0, int harmonics = 0, double* residualOut = nullptr) noexcept;

private:
    void onKick (std::int64_t when, int extra = 0, int source = 1);
    void updatePitch() noexcept;
    /** The phase (degrees, 0 = rising zero crossing) and amplitude of the
        fundamental at `at`, from the `length` samples of the bass that follow
        it (projection on f0: the harmonics cancel over a whole period). */
    void measureForward (std::int64_t at, double periodSamples, int length, double& phaseDeg, double& amplitude, bool low = false) const noexcept;
    int measureLength() const noexcept;
    /** The period (samples) of the note between `from` and `to`, from the bass
        itself (YIN, decimated by 4); 0 if none clear. */
    double periodBetween (std::int64_t from, std::int64_t to) const noexcept;
    // the bass's lowest band (its fundamental, the infra), zero-phase (filtered
    // forth and back) over [from, to), mono, into `analysis`: a saw's
    // harmonics no longer pull the period or the steadiness checks
    int lowBand (std::int64_t from, std::int64_t to, double T) const noexcept; // T: the note's period (0 = unknown)
    double refinePeriod (std::int64_t from, std::int64_t to, double T0) const noexcept;
    double steadiness (std::int64_t from, std::int64_t to, double T) const noexcept;
    static constexpr int lowStep = 4; // the low band at 1 in 4
    mutable std::vector<float> analysis, decimated;
    mutable std::int64_t lowFrom = -1, lowTo = -1; // the stretch `analysis` holds (see lowBand)
    mutable double lowT = 0.0;
    double differenceAt (std::int64_t from, std::int64_t to, double T) const noexcept;
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
    bool lock = false, phaseOn = true, precise = false;
    float targetPhase = 0.0f, offsetMs = 0.0f, subCutoff = 120.0f, depth = 1.0f, waveStart = 0.0f, thresholdDb = -18.0f;
    int shape = exponential, release = 2, lockMode = continuous, trigger = triggerAuto, linkChannel = 1;
    std::int64_t lastSidechainKick = -1000000; // the last kick on the sidechain (Auto: the beats take over 2 s after)
    double lastBeatFloor = 0.0;
    bool beatValid = false;
    void addTrigger (std::int64_t onset, int source);

    // the link (Kick Lock Send): its kicks on the song's grid, placed on ours
    KickLinkReceiver link;
    std::array<std::int64_t, 32> linkPending {};
    int linkPendingCount = 0;
    std::int64_t lastLinkKick = -1000000;
    // a bass note starting (seen L ahead of the output): the rotation back to
    // 0 before it is heard, so its attack is never rotated (a click in the
    // highs: Alan)
    float noteFast = 0.0f, noteFastAtt = 0.0f, noteFastRel = 0.0f, notePeak = 0.0f, notePeakDecay = 0.99999f;
    std::array<float, 4096> noteRing {};
    int noteLag = 480;
    std::int64_t noteDeadUntil = -1, lastNoteStart = -1000000;
    int noteOnsets = 0;
    int linkSlack = 0, lateKicks = 0;
    void pollLink (const gk::BlockTiming& timing, int numSamples);
    // where the song's positions fell on this plug-in's own clock lately: a
    // loop restart (or a locate) jumps them back; a kick of the link is placed
    // by the stretch it belongs to (the kick's track may jump a block before
    // or after this one, its kick come in a block later)
    struct Stretch { std::int64_t clock0 = 0, length = 0; double ppq0 = 0.0, bps = 0.0; };
    std::array<Stretch, 32> stretches {};
    int stretchCount = 0;
    struct WaitingKick { double ppq = 0.0; std::int64_t until = 0; };
    std::array<WaitingKick, 16> linkWaiting {};
    int linkWaitingCount = 0;
    bool placeLinkKick (double ppq, std::int64_t& onset) const noexcept;
    void scheduleLink (std::int64_t onset);

public:
    /** Tests: the name of the Kick Lock Send followed (empty = none). */
    juce::String linkName() const { return link.followedName(); }
    /** Tests / UI: kicks of the link that came later than the latency allows (should stay 0). */
    int linkLateKicks() const noexcept { return lateKicks; }
    /** UI: live Kick Lock Sends on this channel (> 1: a conflict). */
    int linkSendersOnChannel() const noexcept { return link.getSendersOnChannel(); }
    /** Tests / diagnosis: at the last kick measured, the phase coherence over
        8 cycles (before the caps), YIN's aperiodicity, the pitch spread (0.03 = 3 %). */
    double lastCoherence = 0.0, lastAperiodicity = 0.0, lastTm = 0.0, lastNoteAgeMs = 0.0;
    int lastSource = 0; // diagnosis: young note period from 1 = itself, 2 = the note before
    /** Tests: bass note starts seen (each brings the rotation back to 0 before it is heard). */
    int noteStartsSeen() const noexcept { return noteOnsets; }

private:
    double bpm = 120.0;

    // signal paths: the FIR (two kernels, crossfaded when SUB CUT moves)
    int firDelay = 384, firLength = 769;
    std::array<std::vector<float>, 2> kernels;
    std::array<float, 2> kernelCutoff { -1.0f, -1.0f };
    int activeKernel = 0, kernelFade = 0, kernelFadeLength = 960;
    std::array<std::vector<float>, 2> firHistory; // 2 x length (contiguous window)
    // MONO BELOW: the side (L - R) / 2 through a linear-phase low-pass (the
    // sub's kind), taken out of L and put into R at the output
    std::array<std::vector<float>, monoOptions> monoKernels;
    std::vector<float> sideHistory, sideLow;
    int monoBelow = 0, monoFrom = 0, monoTo = 0, monoFade = 0, monoFadeLength = 1;
    int firPos = 0;
    float fir (int channel, int kernel) const noexcept;
    std::array<std::int64_t, 4> pendingKicks {};
    std::array<int, 4> pendingExtra {}; // samples more of the bass waited for (a note changed on the kick)
    std::array<int, 4> pendingSource {}; // what triggered it (telemetry.triggerSource's codes)
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
    void retrack (std::int64_t from, double T) noexcept;
    // The fundamental's tracking changes period (and its window) only while
    // the rotation is at 0: a change under a rotation made the corrected
    // fundamental jump — a click (Alan, in playback and in the render). A
    // retrack asked meanwhile waits: the rotation goes to 0 (2 ms), the
    // tracking changes, then the rotation goes where the kick wants it
    void requestRetrack (std::int64_t from, double T) noexcept;
    bool rotationIdle() const noexcept { return fadeStart < 0 && std::abs (wrap180 (psiNow)) < 1.0e-3 && std::abs (wrap180 (psiTo)) < 1.0e-3; } // (whole turns: none)
    double trackPeriod = 0.0, heldPeriod = 0.0; // heldPeriod: the note the held correction was set for
    bool retrackPending = false, rampPending = false;
    std::int64_t retrackFrom = 0, pendingHold = 0;
    double retrackT = 0.0, pendingPsi = 0.0, pendingReturn = 1.0;
    std::int64_t pitchHoldUntil = -1, pitchHoldNote = -1; // a young note's period, measured at its kick: YIN waits for it
    std::vector<float> mono;                      // mono sub, for the analysis
    std::vector<float> envRing;                   // the kick envelope (to find each kick's start)
    std::vector<float> kickRing;                  // the kick itself (mono), for the scope
    // the kick as heard (the sidechain's, or Kick Lock Send's own wave), 1 in
    // 8, on the input's time line: the last kick's shape where the sub comes
    // back under it
    std::vector<float> heardKick;
    std::int64_t heardMask = 0;
    double kickPhase = 0.0; // degrees: the kick's phase at the note's frequency, weighted by the sub's return (0 = unknown)
    double collisionPhase (double T, std::int64_t when) const noexcept;
    // PRECISE: the sub at 1 in D (~3 kHz) and its Hilbert transform there,
    // per channel; index k is the time dFirst + k D (the input's time line)
    std::array<std::vector<float>, 2> dSub, dLow, dHil;
    // a sharp low-pass at SUB CUT and its Hilbert pair (the same band turned
    // 90 deg), +-45 ms at ~3 kHz, the same Kaiser window: m = 0..hilM
    std::vector<float> lowKernel, hilbertKernel;
    float pairCutoff = -1.0f;
    void designPair() noexcept;
    float lowAt (int channel, std::int64_t t) const noexcept;
    std::array<float, 2> dAcc {};
    std::int64_t dWritten = 0, dMask = 0;
    double dFirst = 0.0;
    int dPhase = 0, hilM = 0, liveLatency = 0, preciseLatency = 0;
    float hilbertAt (int channel, std::int64_t t) const noexcept;
    void onKickPrecise (std::int64_t when, int source);
    void applyLatency() noexcept;
    bool aimAtCollision = true;
    using CurveTable = std::array<float, curveTableSize + 1>;
    std::array<CurveTable, 4> presetTables {};
    std::array<CurveTable, 3> customTables {};
    std::atomic<int> customPublished { 0 };

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
    double periodSamples = 0.0, cleanPeriod = 0.0; // at the full rate, 0 = unknown; the last one from a steadily held note
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
    float subPeak = 0.0f, subPeakDecay = 0.99999f;
    float subRms = 0.0f, bassRms = 0.0f, rmsCoeff = 0.001f, midRms = 0.0f, sideRms = 0.0f, lrRms = 0.0f;

    KickTelemetry telemetry;
    int scopeCounter = 0;
};
} // namespace kl
