#pragma once
// HEAD SPIN — step 1: the trajectory engine and the "speakers" rendering.
//
// The source moves on the path around a spherical head (the structural model
// of Brown & Duda, 1998, revised by later work), each ear rendered on its own:
//
//   in ─> mono ─┬─> per-ear fractional delay: Doppler (the source's distance,
//               │   one line, read twice) + Woodworth interaural delay
//               │   (the far ear, up to ~0.65 ms), 3rd-order Lagrange
//               ├─> per-ear head shadow: Brown-Duda one-pole / one-zero filter,
//               │   its zero set by the angle between the source and that ear
//               │   (the ear facing it gets brighter, the shadowed one darker)
//               ├─> pinna front / back cue (parametric, after Iida): a little
//               │   "concha" peak around 4 kHz in front, a dip there and less
//               │   air above 8 kHz behind
//               ├─> constant-power amplitude panning (sin θ), stronger
//               │   level difference when the source comes close (near field)
//               └─> distance: direct level 1/r, direct-to-reverberant ratio
//                   falling with distance (a small room fed at a near-constant
//                   level), Doppler from the same distance
//   MIX (constant power, MIX 0 = the input exactly) ─> LEVEL ─> out
//
// θ: 0 = front, +90° = right; r = 1 on the circle through the ears. Zero
// latency: only the difference between the ears is delayed (the earlier ear
// reads the input sample itself), and the Doppler delay is 0 at the path's
// nearest point.

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
    static constexpr double headRadius = 0.0875;     // m (the usual spherical head)
    static constexpr double speedOfSound = 343.0;    // m/s
    static constexpr double pathScale = 1.0;         // m: the ears' circle (r = 1) is 1 m away, for the Doppler
    static constexpr double maxDopplerShift = 0.02;  // +-2 % of pitch at most (musical, at any speed)
    static constexpr double maxDopplerSeconds = 0.012;
    static constexpr double shadowAmount = 0.7;      // the head model's level difference, scaled (loudspeakers: the
                                                     // listener's own head shadows too)
    static constexpr double smoothSeconds = 0.020;

    /** Woodworth / Brown-Duda: the delay at an ear for a source at angle
        phi (radians) from that ear's axis, relative to the head's centre. */
    static double earDelaySeconds (double phi) noexcept;
    /** Brown-Duda head shadow: the zero's coefficient alpha (0.1 .. 2) for a
        source at angle phi (radians, 0 = facing the ear). */
    static double shadowAlpha (double phi) noexcept;

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
    FracDelay delay; // the mono source: Doppler + interaural delays, read once per ear
    // head shadow (one-pole / one-zero per ear): previous input and output
    double hsXL = 0.0, hsYL = 0.0, hsXR = 0.0, hsYR = 0.0, hsG = 1.0;
    // pinna cue: a TPT state-variable band-pass at 4 kHz and a one-pole above 8 kHz, per ear
    struct Svf { double ic1 = 0.0, ic2 = 0.0; };
    Svf pinnaL, pinnaR;
    double svfG = 0.0, svfK = 1.0, svfA1 = 1.0, svfA2 = 0.0, svfA3 = 0.0;
    double airL = 0.0, airR = 0.0, airCoeff = 0.0;
    double nearestRadius = 1.0; // of the current path (the Doppler delay is 0 there)
    Room room;
};
} // namespace headspin
