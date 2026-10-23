// CRACKLE — step 1 prototype: the chain without UI, without JUCE.
//   band (HP + LP closing in) -> wow & flutter (modulated delay, cubic) ->
//   saturation (tanh, 2x oversampled) -> + tape hiss
// Vinyl crackle, dropouts, bitcrush and radio come in steps 2 and 3.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

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
    float band = 0.0f, wow = 0.0f, hiss = 0.0f, saturation = 0.0f;
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
    // hiss
    std::array<Svf, 2> hissHp, hissLp;
    Rng rng;
};
} // namespace crackle
