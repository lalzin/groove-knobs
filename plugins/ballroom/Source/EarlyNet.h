#pragma once
// BALLROOM — the early reflections.
//
//   in L / R ─> 4..8 allpasses in series per side (DIFFUSION; their lengths
//   wander slowly: EARLY MOD RATE / DEPTH) ─> a delay line per side ─> the
//   multi-tap: taps spread over EARLY SIZE (the mode's pattern: how many,
//   how even, how fast they fall), each side reading its own line and, with
//   EARLY CROSS, the other one ─> two short allpasses per side (the mode's
//   smear x DIFFUSION): a cluster of reflections rather than distinct echoes.
// A network's size is fixed (configure); the engine crossfades for another.

#include "Modes.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ballroom
{
struct EarlyParams
{
    double cross = 0.2;      // EARLY CROSS 0..1
    double modRateHz = 0.4;  // 0.05 .. 5
    double modDepth = 0.15;  // 0..1
    double diffusion = 0.8;  // 0..1
};

class EarlyNet
{
public:
    static constexpr int maxAllpasses = 8, maxTaps = 24;
    static constexpr double maxSizeMs = 200.0, maxModMs = 0.4;

    void prepare (double sampleRate);
    /** The mode and EARLY SIZE (ms) of this network; clears it. */
    void configure (Mode mode, double sizeMs);
    void reset();
    void setParams (const EarlyParams& p) { target = p; }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);
    /** False for a mode without early reflections (Plate). */
    bool hasTaps() const noexcept { return numTaps > 0; }

private:
    struct Line
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void prepare (int maxSamples);
        void push (float x) { buf[(size_t) (w = (w + 1) & mask)] = x; }
        float tap (int k) const { return buf[(size_t) ((w - k) & mask)]; }
        float read (double d) const // linear (short, slowly moving delays)
        {
            const int i = (int) d;
            const float f = (float) (d - i);
            return tap (i) + f * (tap (i + 1) - tap (i));
        }
    };
    struct ModAllpass // an allpass whose length wanders a little
    {
        Line line;
        double base = 1.0;
        std::uint32_t seed = 1;
        double value = 0.0, aim = 0.0, phase = 1.0, current = 0.0, step = 0.0;
        int countdown = 0;
        float process (float x, float g, double modSamples, double rateHz, double sr);
    };
    struct Tap { double delay; float gainOwn, gainOther; };
    struct Smear // a plain allpass
    {
        std::vector<float> buf;
        int pos = 0;
        float process (float x, float g)
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size())
                pos = 0;
            return d - g * v;
        }
    };

    double sr = 48000.0;
    const ModeShape* shape = &modeShape (Mode::club);
    EarlyParams target;
    std::array<std::array<ModAllpass, maxAllpasses>, 2> allpasses;
    std::array<Line, 2> lines;
    std::array<std::array<Tap, maxTaps>, 2> taps {};
    std::array<std::array<Smear, 2>, 2> smears;
    int numTaps = 0;
    double cross = 0.2, crossCoeff = 0.0;
};
} // namespace ballroom
