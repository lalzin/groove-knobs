#include "SlapperVoice.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace slapper
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// phase delay (samples) of a filter at w (radians per sample)
template <typename H> double phaseDelay (H response, double w)
{
    const auto z = response (std::polar (1.0, -w)); // z^-1 = e^{-jw}
    double ph = std::arg (z);
    if (ph > 0.0)
        ph -= 2.0 * pi; // (a delay: negative phase)
    return -ph / w;
}
} // namespace

void Voice::Line::init (int size)
{
    int n = 1;
    while (n < size)
        n <<= 1;
    buf.assign ((size_t) n, 0.0f);
    mask = n - 1;
    w = 0;
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    right.init ((int) (sr / 20.0) + 8);
    left.init ((int) (sr / 20.0) + 8);
    // the pickup: RBJ low-pass, 3 kHz, Q 1.1
    const double w = 2.0 * pi * 3000.0 / sr, al = std::sin (w) / (2.0 * 1.1), cw = std::cos (w), a0 = 1.0 + al;
    pb0 = (float) ((1.0 - cw) * 0.5 / a0); pb1 = (float) ((1.0 - cw) / a0); pb2 = pb0;
    pa1 = (float) (-2.0 * cw / a0); pa2 = (float) ((1.0 - al) / a0);
}

void Voice::tune (double hz)
{
    f0 = hz;
    const double N = sr / hz, w0 = 2.0 * pi * hz / sr;
    // loss: decay (T60) long and low, shorter up the neck; ÉTOUFFER cuts it
    const double t60 = (3.5 - 1.6 * std::log2 (std::max (1.0, hz / 41.2)) / 2.4) * (1.0 - 0.95 * mute);
    g = (float) std::pow (10.0, -3.0 / (std::max (0.05, t60) * hz));
    lpA = (float) (0.12 + 0.55 * mute + 0.1 * std::max (0.0, std::log2 (hz / 41.2)) / 2.4);
    // stiffness: the upper partials a little sharp (a first-order allpass, DC delay > 1)
    dispC = (float) (-0.25 * stringStiffness);
    const double a = lpA, c = dispC;
    const double dLoss = phaseDelay ([&] (std::complex<double> zi) { return (1.0 - a) / (1.0 - a * zi); }, w0);
    const double dDisp = phaseDelay ([&] (std::complex<double> zi) { return (c + zi) / (1.0 + c * zi); }, w0);
    // the rest: 2 M samples of the two lines and a Thiran fraction in [0.5, 2.5)
    const double rest = N - dLoss - dDisp;
    M = std::max (2, (int) std::floor ((rest - 0.5) / 2.0));
    double D = rest - 2.0 * M;
    // first-order Thiran, its phase delay at the note made exact (2 corrections)
    double target = D;
    for (int it = 0; it < 3; ++it)
    {
        const double ta = (1.0 - D) / (1.0 + D);
        const double actual = phaseDelay ([&] (std::complex<double> zi) { return (ta + zi) / (1.0 + ta * zi); }, w0);
        D += target - actual;
    }
    thA = (float) ((1.0 - D) / (1.0 + D));
    loopTotal = 2.0 * M + dLoss + dDisp + target;
    excitePoint = std::clamp (M - 1 - (int) std::lround (pickPosition * M), 1, M - 2);
    pickupPoint = std::clamp (M - 1 - (int) std::lround (0.22 * M), 1, M - 2); // the pickup, not at the pluck point
}

void Voice::displace (float A, int width)
{
    // the string held at the excitation point by A (a triangle), its corner
    // rounded over +-width samples, then let go (no initial speed): added on
    // what the string already does
    const int p = excitePoint;
    std::vector<float> shape ((size_t) M, 0.0f), smooth ((size_t) M, 0.0f);
    for (int d = 0; d < M; ++d)
        shape[(size_t) d] = d <= p ? A * (float) d / (float) p : A * (float) (M - 1 - d) / (float) (M - 1 - p);
    for (int d = 0; d < M; ++d)
    {
        float acc = 0.0f;
        int n = 0;
        for (int j = -width; j <= width; ++j)
            if (d + j >= 0 && d + j < M)
            {
                acc += shape[(size_t) (d + j)];
                ++n;
            }
        smooth[(size_t) d] = acc / (float) n;
    }
    for (int d = 0; d < M; ++d)
    {
        right.add (d, 0.5f * smooth[(size_t) d]);
        left.add (M - 1 - d, 0.5f * smooth[(size_t) d]);
    }
}

void Voice::noteOn (double hz, float velocity, Attack attack)
{
    // a new note on the same string: the old vibration is cut by the new
    // length (the finger frets elsewhere) — step 1: the lines are restarted
    right.init ((int) right.buf.size());
    left.init ((int) left.buf.size());
    lpZ = dispX = dispY = thX = thY = 0.0f;
    tune (hz);
    const float vel = std::clamp (velocity, 0.0f, 1.0f);
    // where the string is struck: the thumb slaps at the end of the neck (~1/3
    // of the string from the bridge: a strong fundamental — the sub), the pop
    // pulls a little closer to the bridge, the fingers play at PINCEMENT
    {
        const float from = attack == Attack::slap ? 0.33f + (pickPosition - 0.15f) : attack == Attack::pop ? 0.27f + (pickPosition - 0.15f) : pickPosition;
        excitePoint = std::clamp (M - 1 - (int) std::lround (std::clamp (from, 0.05f, 0.5f) * M), 1, M - 2);
    }
    attackGain = attack == Attack::finger ? 1.0f : attack == Attack::pop ? 1.1f : 1.0f;
    hammer = {};
    rebound = {};
    contactSamples = 0;
    peakForce = 0.0;
    reboundDelay = -1;
    switch (attack)
    {
        case Attack::slap:
            // the thumb drives the string down onto the frets — a wide push
            // (the fundamental: the sub) — and lets it go at once
            displace (-(0.02f + 0.06f * vel), std::max (2, (int) (0.0012 * sr)));
            // and its bite: light, very stiff, fast (~1 ms against the string)
            hammer = { true, 0.0, 0.0, 15.0, 3.0, 2.3, 0, (int) (0.008 * sr) };
            // its rebound on the fret: a second, weaker strike 6-12 ms later
            rebound = hammer;
            reboundDelay = (int) ((0.006 + 0.006 * (1.0 - vel)) * sr);
            clickLeft = (int) (0.0018 * sr);
            clickGain = 0.25f * vel;
            break;
        case Attack::finger:
            // the finger: pulls the string sideways and lets it slide off its
            // pad — a pluck with a wide, round corner (the pad's width)
            displace (0.02f + 0.06f * vel, std::max (2, (int) ((0.0025 - 0.001 * vel) * sr)));
            clickLeft = 0;
            break;
        case Attack::pop:
        {
            // pulled away from the board, then let go: a triangle at the pluck
            // point, its corner rounded by how fast the finger lets go
            displace (0.015f + 0.05f * vel, std::max (1, (int) ((0.0006 - 0.0004 * vel) * sr)));
            // the snap back against the frets
            clickLeft = (int) (0.0012 * sr);
            clickGain = 0.12f * vel;
            break;
        }
    }
    if (hammer.active)
    {
        // the impact speed: the velocity (the pop's is its pull, above)
        hammer.x = displacementAt (excitePoint);
        hammer.v = 0.004 + 0.012 * vel;
        rebound.v = 0.35 * hammer.v;
    }
}

void Voice::stepHammer (Hammer& h) noexcept
{
    if (! h.active)
        return;
    const double ys = displacementAt (excitePoint);
    const double d = h.x - ys;
    const double F = d > 0.0 ? h.k * std::pow (d, h.p) : 0.0;
    h.v -= F / h.m;
    h.x += h.v;
    if (F > 0.0)
    {
        inject (excitePoint, (float) (0.5 * F));
        ++contactSamples;
        peakForce = std::max (peakForce, F);
    }
    if (++h.age > h.life)
        h.active = false;
}

float Voice::process() noexcept
{
    // the string: the waves arriving at the bridge and the nut
    const float atBridge = right.at (M - 1), atNut = left.at (M - 1);
    // bridge: Thiran (tuning), allpass (stiffness), lowpass (loss), -g
    const float th = thA * atBridge + thX - thA * thY;
    thX = atBridge;
    thY = th;
    const float di = dispC * th + dispX - dispC * dispY;
    dispX = th;
    dispY = di;
    lpZ = (1.0f - lpA) * di + lpA * lpZ;
    const float toLeft = -g * lpZ;
    const float toRight = -atNut; // the nut: rigid
    right.push (toRight);
    left.push (toLeft);

    stepHammer (hammer);
    if (reboundDelay >= 0 && reboundDelay-- == 0)
    {
        rebound.active = true;
        rebound.age = 0;
        rebound.x = displacementAt (excitePoint);
    }
    stepHammer (rebound);

    // the pickup: the string's velocity there
    const float y = displacementAt (pickupPoint);
    float out = (y - lastPick) * 6.0f * attackGain;
    lastPick = y;
    // the click (thumb / snap on the frets): a short burst, high-passed
    if (clickLeft > 0)
    {
        --clickLeft;
        const float n = noise() * clickGain;
        clickHp = 0.6f * (clickHp + n - clickPrev);
        clickPrev = n;
        out += clickHp;
    }
    // the pickup's own low-pass
    const float py = pb0 * out + pb1 * px1 + pb2 * px2 - pa1 * py1 - pa2 * py2;
    px2 = px1; px1 = out; py2 = py1; py1 = py;
    // DC blocker
    dcY = py - dcX + 0.9995f * dcY;
    dcX = py;
    return dcY;
}
} // namespace slapper
