#pragma once
// BALLROOM — the late reverberation: an 8-line feedback delay network.
//
//   in L / R ─> input diffusion (2..4 allpasses per side) ─> injection (LATE
//   CROSS mixes the sides) ─> 8 delay lines ─┬─> out L (lines 0, 2, 4, 6, signs alternating)
//        ^                                  ├─> out R (lines 1, 3, 5, 7)
//        └── Householder or Hadamard <── an allpass <── absorption (3 bands) <─┘
//
// - The allpass in each loop (its length a fraction of the line's, its gain
//   the mode's loop diffusion x DIFFUSION): every pass through the matrix
//   multiplies the echoes, the tail is dense from its first tens of ms, no
//   flutter, no metal (lossless: the decay is that of line + allpass).
//
// - Lengths: the mode's 8 prime lengths, scaled by LATE SIZE. A network's
//   size is fixed (configure): the engine crossfades to a second network for
//   another size (no pitch bend, no click).
// - Absorption: each line has the gain that makes the mids fall 60 dB in
//   DECAY (g = 10^(-3 L / RT60), L the line's length in seconds), then a
//   low shelf (BASS XOVER) and a high shelf (HIGH XOVER), each two 2nd-order
//   state-variable shelves in a row (half the gain each: a steeper step),
//   set so the lows fall in DECAY x BASS MULT and the highs in DECAY x HIGH
//   MULT. Gains and coefficients at control rate (32 samples).
// - Modulation: every line's length moves with its own smoothed random
//   walk (MOD RATE, MOD DEPTH), read with first-order allpass interpolation
//   (flat in level at every frequency: no extra loss of highs at each pass).
// - FREEZE: the feedback at 1, no absorption, the input cut, the modulation
//   still and the lengths whole (nothing lost at each pass).
// No JUCE: the plugin uses this code as is.

#include "Modes.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ballroom
{
struct LateParams
{
    double decaySeconds = 2.0;  // RT60 of the mids, 0.1 .. 100
    double cross = 0.3;         // LATE CROSS 0..1
    double modRateHz = 0.5;     // 0.05 .. 5
    double modDepth = 0.25;     // 0..1
    double bassMult = 1.2;      // 0.25 .. 4
    double bassXoverHz = 500.0; // 100 .. 2000
    double highMult = 0.5;      // 0.1 .. 1
    double highXoverHz = 5000.0;// 1000 .. 12000
    double diffusion = 0.8;     // input diffusion 0..1
    bool freeze = false;
};

class LateFdn
{
public:
    static constexpr int numLines = 8;
    static constexpr double maxModSeconds = 0.0025, maxLoopFraction = 0.30;

    void prepare (double sampleRate);
    /** The mode and LATE SIZE (0..1) of this network; clears it. */
    void configure (Mode mode, double size);
    void reset();
    void setParams (const LateParams& p) { target = p; }
    /** The reverb of inL / inR into outL / outR (wet only, overwritten). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    /** A line's length in seconds (for the tests). */
    static double lineSeconds (Mode mode, int line, double size);

private:
    struct Delay
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void prepare (int maxSamples);
        void push (float x) { buf[(size_t) (w = (w + 1) & mask)] = x; }
        float tap (int k) const { return buf[(size_t) ((w - k) & mask)]; }
    };
    struct Allpass
    {
        std::vector<float> buf;
        int len = 1, pos = 0;
        float process (float x, float g)
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            if (++pos >= len)
                pos = 0;
            return d - g * v;
        }
    };
    struct Wander // a smoothed random walk in -1..1 (fixed seed)
    {
        std::uint32_t seed = 1;
        double value = 0.0, target = 0.0, phase = 1.0, current = 0.0, step = 0.0;
        int countdown = 0;
        double next (double rateHz, double sr);
    };
    struct Smooth
    {
        double a = 0.0, v = 0.0;
        bool init = false;
        double next (double t)
        {
            if (! init) { v = t; init = true; }
            v = t + (v - t) * a;
            return v;
        }
    };
    struct Shelf // TPT state-variable shelf (low or high), Q 0.707
    {
        double ic1 = 0.0, ic2 = 0.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
        void set (bool high, double fc, double gain, double sr);
        double process (double x)
        {
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return m0 * x + m1 * v1 + m2 * v2;
        }
    };

    double sr = 48000.0;
    Mode mode = Mode::club;
    const ModeShape* shape = &modeShape (Mode::club);
    std::array<double, numLines> lenSamples {}; // at this network's size
    LateParams target;
    std::array<Delay, numLines> lines;
    std::array<Wander, numLines> wander;
    std::array<std::array<Shelf, 2>, numLines> lowShelf, highShelf;
    std::array<double, numLines> apState {}; // allpass interpolation, per line
    std::array<double, numLines> midGain {};
    int controlCountdown = 0;
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<Allpass, numLines> loopAllpasses;
    Smooth sDecay, sBass, sHigh, sDepth, sCross, sFreeze;
    std::uint32_t denormal = 1;
};
} // namespace ballroom
