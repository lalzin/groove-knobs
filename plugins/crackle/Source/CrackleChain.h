// CRACKLE — the chain (no JUCE: also built by prototypes/crackle).
//   band (HP + LP closing in) -> wow & flutter (modulated delay, cubic) ->
//   saturation (tanh, 2x oversampled) -> + tape hiss
//   -> + vinyl (pink surface noise, sparse crackles) -> dropouts
//   (after the saturation: crush -> radio -> + hiss, vinyl, radio static -> dropouts)
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "Saturator.h"

namespace crackle
{
/** Xorshift32, a fixed seed at load: the same render every time. */
struct Rng
{
    std::uint32_t s = 0x6d2b79f5u;
    std::uint32_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uniform() noexcept { return (float) (next() >> 8) / 16777216.0f; }   // 0..1
    float bipolar() noexcept { return 2.0f * uniform() - 1.0f; }             // -1..1
};

/** The sections' amounts (0..1), from GRAIN (GrainMap.h) or the advanced knobs. */
struct Amounts
{
    float band = 0.0f, wow = 0.0f, hiss = 0.0f, saturation = 0.0f, vinyl = 0.0f, dropout = 0.0f;
    float flutter = 0.0f; // its own knob (WOW is the slow one)
    float crush = 0.0f, radio = 0.0f;
    SatType satType = SatType::tape;                    // TYPE (Saturator.h)
    float focusLow = 20.0f, focusHigh = 20000.0f;       // FOCUS, Hz: the band that is saturated
    /** NOISE: the level of every noise added (hiss now; vinyl crackle and radio
        static later), not of the processing: 0 none, 0.5 as GRAIN sets them
        (0 dB), 1 +6 dB. */
    float noise = 0.5f;
};

class Chain
{
public:
    void prepare (double sampleRate);
    void reset();
    /** Stereo, in place. The amounts are smoothed inside (20 ms). */
    void process (float* left, float* right, int numSamples, const Amounts& target) noexcept;
    /** The chain's delay at rest: the wow line's minimum and the 2x filters' (15 samples). */
    static constexpr int latencySamples = 2 + 15;

private:
    struct Svf // TPT state-variable filter: modulation-safe
    {
        double ic1 = 0, ic2 = 0;
        double lp (double x, double g, double k) noexcept;
        double hp (double x, double g, double k) noexcept;
    };
    double sr = 48000.0;
    Amounts now;
    float smooth = 0.0f;
    // band
    std::array<Svf, 2> hp, lp;
    // wow & flutter
    std::array<std::vector<float>, 2> delay;
    int mask = 0, write = 0;
    double wowPhase = 0.0, flutterPhase = 0.0, drift = 0.0, driftTarget = 0.0;
    int driftCountdown = 0;
    // saturation (2x)
    std::vector<float> halfBand;
    std::array<std::array<float, 64>, 2> upHist {}, downHist {};
    int upPos = 0, downPos = 0;
    std::array<Saturator, 2> saturator;
    // crush (sample-and-hold), radio (band, fading, static)
    std::array<double, 2> held {};
    int holdCount = 0;
    std::array<Svf, 2> radioHp, radioLp;
    double radioFadePhase = 0.0, staticEnv = 0.0, staticDecay = 0.0, staticHp = 0.0;
    // hiss
    std::array<Svf, 2> hissHp, hissLp;
    Rng rng;
    // vinyl: pink noise (Voss-McCartney, 5 rows) per channel, and the crackles
    struct Pink
    {
        std::array<float, 5> rows {};
        float sum = 0.0f;
        std::uint32_t counter = 0;
        float next (Rng& r) noexcept;
    };
    std::array<Pink, 2> pink;
    struct Crackle // the stylus on a speck of dust: a raw impulse and a short grainy tail
    {
        bool on = false;
        int age = 0, pulse = 1;         // the impulse: a tick (0.1-0.4 ms) or now and then a pop (1-3 ms)
        float amp = 0.0f, sign = 1.0f, tail = 0.0f, tailDecay = 0.0f, panL = 1.0f, panR = 1.0f;
    };
    std::array<Crackle, 8> crackles {};
    /** Playback de-emphasis (RIAA: 3180 / 318 / 75 us), 0 dB at 1 kHz: what makes
        crackle sound like a record — the highs rounded off, the pops given body. */
    struct Riaa
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double hpState = 0.0, hpOut = 0.0, hpCoef = 0.0; // and a 1-pole high-pass at 25 Hz (no rumble build-up)
        void design (double fs);
        double run (double x) noexcept;
    };
    std::array<Riaa, 2> riaa;
    // dropouts: the gain falls to 0 for 2-15 ms, 1 ms fades
    double dropGain = 1.0;
    int dropHold = 0, dropFade = 48;
    enum class Drop { none, falling, held, rising } drop = Drop::none;
public:
    /** (diagnosis / UI) crackles and dropouts since prepare */
    int crackleCount = 0, dropoutCount = 0;
private:
};
} // namespace crackle
