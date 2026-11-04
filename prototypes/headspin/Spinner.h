#pragma once
// HEAD SPIN — step 1: the trajectory engine and the "speakers" rendering.
//
//   in (mono or stereo) ─┬───────────────────────────────────────────── dry ─┐
//                        └─> mono ─> position on the path (phase, stretch,  │
//                            rotation) ─> azimuth θ, distance r ─┐           ├─> MIX (constant power) ─> LEVEL ─> out
//                                                                │           │
//      for each ear: constant-power gain (sin θ) ─> interaural  ─┤           │
//      delay (far ear, up to 0.6 ms, 3rd-order Lagrange) ─> head ─┤           │
//      shadow (far ear, 6 dB/oct low-pass 20 kHz → 3 kHz) ─>      │           │
//      behind the head: highs down a little ─> 1/r gain ──────────┴─> wet ───┘
//                                         └─> small room (send grows with r and behind)
//
// θ: 0 = front, +90° = right; r = 1 on the circle through the ears.
// No latency: the near ear is never delayed (a delay of 0 reads the input
// sample itself), MIX 0 gives the input back exactly.

#include "Shape.h"
#include <array>
#include <cmath>
#include <vector>

namespace headspin
{
/** The parameters of step 1 (the others come with step 2: shapes, sync,
    groove, retrigger). */
struct Params
{
    float mix = 1.0f;         // 0..1
    float width = 1.0f;       // 0.2..1, left / right stretch
    float depth = 1.0f;       // 0.2..1, front / back stretch
    float rotationDeg = 0.0f; // -180..180, clockwise
    bool leftToRight = true;  // direction (clockwise seen from above)
    float rateHz = 0.5f;      // 0.01..20
    float startDeg = 0.0f;    // 0..360, starting point on the path
    float distance = 0.5f;    // 0..1, how much distance attenuates and adds room
    float levelDb = 0.0f;     // -12..+6
};

/** Where the sound is (for the UI and the tests). */
struct Position
{
    double x = 0.0, y = 1.0; // after stretch and rotation
    double azimuthDeg = 0.0; // 0 front, 90 right, 180 back, -90 left
    double radius = 1.0;
};

class Spinner
{
public:
    static constexpr double maxItdSeconds = 0.0006; // a head ~ 20 cm across
    static constexpr double shadowMinHz = 3000.0, shadowMaxHz = 20000.0;
    static constexpr double smoothSeconds = 0.020;

    void prepare (double sampleRate);
    void reset();
    void setParams (const Params& p) { target = p; }

    /** Processes in place: left / right (right may be the same buffer as
        left for a mono input; the output is always two channels). */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    /** Where the source is now. */
    Position position() const;
    double getPhase() const noexcept { return phase; }

    /** The path's position at phase u with the current stretch / rotation. */
    Position positionAt (double u) const;

private:
    struct Smooth
    {
        double a = 0.0, s1 = 0.0, v = 0.0;
        bool init = false;
        void prepare (double sr, double seconds) { a = std::exp (-1.0 / (seconds / 4.74 * sr)); }
        double next (double t)
        {
            if (! init) { s1 = v = t; init = true; }
            s1 = t + (s1 - t) * a;
            v = s1 + (v - s1) * a;
            return v;
        }
    };

    /** A short delay line with 3rd-order Lagrange interpolation. */
    struct FracDelay
    {
        std::vector<float> buf;
        int w = 0, mask = 0;
        void prepare (int maxSamples)
        {
            int n = 1;
            while (n < maxSamples + 8)
                n <<= 1;
            buf.assign ((size_t) n, 0.0f);
            mask = n - 1;
            w = 0;
        }
        void push (float x) { buf[(size_t) (w = (w + 1) & mask)] = x; }
        float tap (int k) const { return buf[(size_t) ((w - k) & mask)]; }
        /** d >= 0 samples; d = 0 is the newest sample exactly. */
        float read (double d) const
        {
            // 4 taps around d, kept causal (k >= 0): for d < 1 the window is 0..3
            const int k0 = std::max (0, (int) std::floor (d) - 1);
            const double t = d - k0; // in [0, 3]
            const double x0 = tap (k0), x1 = tap (k0 + 1), x2 = tap (k0 + 2), x3 = tap (k0 + 3);
            const double c0 = -(t - 1) * (t - 2) * (t - 3) / 6.0, c1 = t * (t - 2) * (t - 3) / 2.0, c2 = -t * (t - 1) * (t - 3) / 2.0,
                         c3 = t * (t - 1) * (t - 2) / 6.0;
            return (float) (c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3);
        }
    };

    /** A small stereo room: 4-line feedback delay network, damped. */
    struct Room
    {
        std::array<std::vector<float>, 4> line;
        std::array<int, 4> pos {};
        std::array<float, 4> lp {};
        void prepare (double sr)
        {
            const double ms[4] { 23.1, 29.7, 34.3, 41.9 };
            for (int i = 0; i < 4; ++i)
            {
                line[(size_t) i].assign ((size_t) std::lround (ms[i] * 0.001 * sr), 0.0f);
                pos[(size_t) i] = 0;
                lp[(size_t) i] = 0.0f;
            }
        }
        void process (float in, float& l, float& r)
        {
            float o[4];
            for (int i = 0; i < 4; ++i)
                o[i] = line[(size_t) i][(size_t) pos[(size_t) i]];
            // Householder feedback (energy preserving) x 0.72, damped
            const float sum = 0.5f * (o[0] + o[1] + o[2] + o[3]);
            for (int i = 0; i < 4; ++i)
            {
                const float fb = 0.72f * (o[i] - sum);
                lp[(size_t) i] += 0.45f * (fb - lp[(size_t) i]);
                line[(size_t) i][(size_t) pos[(size_t) i]] = in + lp[(size_t) i];
                if (++pos[(size_t) i] >= (int) line[(size_t) i].size())
                    pos[(size_t) i] = 0;
            }
            l = 0.5f * (o[0] - o[2]) + 0.25f * o[1];
            r = 0.5f * (o[1] - o[3]) + 0.25f * o[2];
        }
    };

    double sr = 48000.0;
    Params target;
    Smooth sMix, sWidth, sDepth, sRot, sRate, sDist, sLevel;
    double phase = 0.0;
    bool started = false;
    ShapeTable shape;
    double width = 1.0, depth = 1.0, rotation = 0.0;
    FracDelay delayL, delayR;
    double shadowL = 0.0, shadowR = 0.0, backL = 0.0, backR = 0.0;
    Room room;
};
} // namespace headspin
