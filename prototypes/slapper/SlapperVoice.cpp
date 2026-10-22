#include "SlapperVoice.h"
#include <algorithm>
#include <cmath>

namespace slapper
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double openStrings[] { 41.2034, 55.0, 73.4162, 97.9989 }; // E1 A1 D2 G2
constexpr double pickup = 0.19;   // the pickup's place, from the bridge (a P-bass's split coil)
constexpr double pickupWidth = 0.022; // its magnetic window (~2 cm of an 86 cm scale)

double sinc (double x) { return std::fabs (x) < 1e-9 ? 1.0 : std::sin (x) / x; }
} // namespace

void Voice::Bank::resize (int n)
{
    K = n;
    for (auto* v : { &re, &im, &pr, &pi, &out, &fret, &fretInvW })
        v->assign ((size_t) n, 0.0f);
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& b : bank)
    {
        b.resize (0);
        b.alive = false;
    }
    // the coil and its cable: a resonant low-pass, ~2.8 kHz
    const double w = 2.0 * pi * 3200.0 / sr, al = std::sin (w) / (2.0 * 1.3), cw = std::cos (w), a0 = 1.0 + al;
    coil.b0 = (float) ((1.0 - cw) * 0.5 / a0); coil.b1 = (float) ((1.0 - cw) / a0); coil.b2 = coil.b0;
    coil.a1 = (float) (-2.0 * cw / a0); coil.a2 = (float) ((1.0 - al) / a0);
    designedLowCut = -1.0f;
}

void Voice::setup (Bank& b, double hz, Attack attack, float vel)
{
    // the string it is played on: the highest open string at or under the note
    // (a fretted string is shorter, so stiffer: inharmonicity grows with the fret)
    double open = openStrings[0];
    for (double o : openStrings)
        if (o <= hz * 1.001)
            open = o;
    const double B = (0.6 + 1.6 * stringStiffness) * 1e-4 * std::pow (hz / open, 2.0);
    const double fMax = std::min (0.45 * sr, 6000.0);
    int K = 0;
    while (K < 400 && (K + 1) * hz * std::sqrt (1.0 + B * (K + 1) * (K + 1)) / std::sqrt (1.0 + B) < fMax)
        ++K;
    K = std::max (K, 1);
    b.resize (K);
    b.alive = true;

    // decay: sigma (f) = b1 + b2 f + b3 f^2 (amplitude, 1/s): the fundamental
    // rings ~5 s on a low E, the highs are gone in tenths of a second
    const double t60Fund = std::max (1.5, 5.0 - 0.8 * std::log2 (hz / 41.2));
    const double b1 = 6.91 / t60Fund * (1.0 + 30.0 * mute);
    const double b2 = 0.002 * (1.0 + 10.0 * mute);
    const double b3 = 0.7e-5 * (1.4 - 0.8 * stringStiffness) * (1.0 + 4.0 * mute);

    // the excitation: where, how wide, how hard
    const double xe = attack == Attack::finger ? std::clamp ((double) pickPosition, 0.05, 0.5)
                                               : std::clamp (0.30 + (pickPosition - 0.15), 0.1, 0.5);
    std::vector<double> w ((size_t) K), q0 ((size_t) K), u0 ((size_t) K);
    for (int k = 0; k < K; ++k)
    {
        const int n = k + 1;
        const double f = n * hz * std::sqrt (1.0 + B * n * n) / std::sqrt (1.0 + B);
        w[(size_t) k] = 2.0 * pi * f;
        const double s = std::sin (n * pi * xe);
        if (attack == Attack::slap)
        {
            // a strike: a velocity bump under the thumb (~2.5 cm wide), the
            // contact's duration (shorter when harder) a low-pass on it
            const double tau = (1.4 - 0.8 * vel) * 1e-3;
            const double c = 1.0 / std::sqrt (1.0 + std::pow (w[(size_t) k] * tau / pi, 4.0));
            u0[(size_t) k] = 2.0 * s * sinc (n * pi * 0.015) * c / w[(size_t) k];
        }
        else
        {
            // a pull and release: a triangle (peak 1 at xe), its corner rounded
            // by the fingertip, the flesh a gentler low-pass on top
            const double width = attack == Attack::pop ? 0.006 : 0.012;
            const double fc = attack == Attack::pop ? 5000.0 : 1500.0 + 2500.0 * vel;
            const double flesh = 1.0 / std::sqrt (1.0 + std::pow (f / fc, 4.0));
            q0[(size_t) k] = 2.0 * s / (n * n * pi * pi * xe * (1.0 - xe)) * sinc (n * pi * width) * flesh;
        }
    }
    // the amplitude at the struck point (mm): what decides whether the string reaches the frets
    double atPoint = 0.0;
    for (int k = 0; k < K; ++k)
        atPoint += (attack == Attack::slap ? u0[(size_t) k] : q0[(size_t) k]) * std::sin ((k + 1) * pi * xe);
    const double A = attack == Attack::slap ? 2.0 + 4.0 * vel : attack == Attack::pop ? -(1.5 + 3.5 * vel) : 0.5 + 1.1 * vel;
    const double scale = A / (std::fabs (atPoint) > 1e-12 ? std::fabs (atPoint) : 1.0);

    // the frets under the string: at the struck point, or — plucked between
    // the pickups, where there are none — at the last fret (~0.3 from the bridge)
    const double xf = std::max (xe, 0.3);
    // (the fingers and the pop played a little harder than they move the string: the three near one level)
    const double level = attack == Attack::finger ? 2.4 : attack == Attack::pop ? 1.5 : 1.0;
    for (int k = 0; k < K; ++k)
    {
        const double f = w[(size_t) k] / (2.0 * pi);
        const double sigma = b1 + b2 * f + b3 * f * f;
        const double r = std::exp (-sigma / sr), th = w[(size_t) k] / sr;
        b.pr[(size_t) k] = (float) (r * std::cos (th));
        b.pi[(size_t) k] = (float) (r * std::sin (th));
        // (the strike's pulse centred 2 tau after the start, not cut in half at it: no click)
        const double lag = attack == Attack::slap ? -w[(size_t) k] * 2.0 * (1.4 - 0.8 * vel) * 1e-3 : 0.0;
        b.re[(size_t) k] = (float) (scale * (u0[(size_t) k] * std::cos (lag) - q0[(size_t) k] * std::sin (lag)));
        b.im[(size_t) k] = (float) (scale * (u0[(size_t) k] * std::sin (lag) + q0[(size_t) k] * std::cos (lag)));
        // the pickup: the string's velocity (omega Re z) under its window
        const int n = k + 1;
        b.out[(size_t) k] = (float) (0.05 * level * w[(size_t) k] / (2.0 * pi * hz) * std::pow (hz / 41.2, 0.2) * std::sin (n * pi * pickup) * sinc (n * pi * pickupWidth));
        // the frets under the struck point
        b.fret[(size_t) k] = (float) std::sin (n * pi * xf);
        b.fretInvW[(size_t) k] = (float) (2.0 * std::sin (n * pi * xf) / (w[(size_t) k] * sr));
    }
    b.fundW = (float) w[0];
    b.reach = 0.0;
    for (int k = 0; k < K; ++k)
        b.reach += (double) b.fret[(size_t) k] * b.pi[(size_t) k] * b.fretInvW[(size_t) k];
    fretGap = 1.5; // mm, the action at the end of the neck
    fretK = 4.0e8;
    contactOn = true;

    // the sub: on the fundamental, level from the velocity, decays with it
    b.subEnv = 0.0f;
    b.subTarget = 0.25f + 0.75f * vel;
    subAttack = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
    b.subDecay = (float) std::exp (-6.91 / (t60Fund / (1.0 + 30.0 * mute) * sr));
}

void Voice::damp (Bank& b, double t60)
{
    const float g = (float) std::exp (-6.91 / (t60 * sr));
    for (int k = 0; k < b.K; ++k)
    {
        b.pr[(size_t) k] *= g;
        b.pi[(size_t) k] *= g;
    }
    b.subTarget = 0.0f; // (its sub follows in ~4 ms)
}

float Voice::sub (Bank& b) noexcept
{
    b.subEnv += subAttack * (b.subTarget - b.subEnv);
    b.subTarget *= b.subDecay;
    const float r = b.re[0], i = b.im[0], m = std::sqrt (r * r + i * i);
    return m > 1e-12f ? 0.03f * subLevel * b.subEnv * r / m : 0.0f;
}

void Voice::noteOn (double hz, float velocity, Attack attack)
{
    const float vel = std::clamp (velocity, 0.0f, 1.0f);
    // the ringing string: the fretting hand lifts and the next finger lands — damped, ~15 ms
    if (bank[cur].alive)
        damp (bank[cur], 0.015);
    cur ^= 1;
    setup (bank[cur], hz, attack, vel);
    contactSamples = 0;
    peakForce = 0.0;
}

float Voice::run (Bank& b, bool contact) noexcept
{
    float acc = 0.0f;
    float* re = b.re.data();
    float* im = b.im.data();
    const float* pr = b.pr.data();
    const float* pim = b.pi.data();
    const float* out = b.out.data();
    for (int k = 0; k < b.K; ++k)
    {
        const float r = re[k], i = im[k];
        re[k] = r * pr[k] - i * pim[k];
        im[k] = r * pim[k] + i * pr[k];
        acc += out[k] * re[k];
    }
    if (contact)
    {
        // the string under the struck point against the frets: a stiff, one-sided spring
        const float* fr = b.fret.data();
        float q = 0.0f;
        for (int k = 0; k < b.K; ++k)
            q += fr[k] * im[k];
        const double pen = q - fretGap;
        if (pen > 0.0)
        {
            // (never more than what puts the string back on the fret by the next
            // sample: an explicit stiff spring would otherwise blow up)
            const float F = (float) -std::min (fretK * std::pow (pen, 1.5), pen / std::max (1e-30, b.reach));
            const float* fi = b.fretInvW.data();
            for (int k = 0; k < b.K; ++k)
                re[k] += F * fi[k];
            ++contactSamples;
            peakForce = std::max (peakForce, (double) -F);
        }
    }
    return acc;
}

float Voice::process() noexcept
{
    if (designedLowCut != lowCutHz)
    {
        designedLowCut = lowCutHz;
        const double w = 2.0 * pi * lowCutHz / sr, al = std::sin (w) / (2.0 * 0.7071), cw = std::cos (w), a0 = 1.0 + al;
        lowCut.b0 = (float) ((1.0 + cw) * 0.5 / a0); lowCut.b1 = (float) (-(1.0 + cw) / a0); lowCut.b2 = lowCut.b0;
        lowCut.a1 = (float) (-2.0 * cw / a0); lowCut.a2 = (float) ((1.0 - al) / a0);
    }
    float y = 0.0f;
    Bank& now = bank[cur];
    Bank& old = bank[cur ^ 1];
    if (now.alive)
        y += run (now, contactOn);
    if (old.alive)
    {
        y += run (old, false);
        // gone once its fundamental is (-80 dB of a damped string: ~60 ms)
        if (std::fabs (old.re[0]) + std::fabs (old.im[0]) < 1e-6f && old.subEnv < 1e-5f)
            old.alive = false;
    }
    y = coil.run (y);
    // the subs: sines on the fundamentals' phase (the pickup's: Re z)
    for (auto* b : { &now, &old })
        if (b->alive && b->K > 0)
            y += sub (*b);
    return lowCut.run (y);
}
} // namespace slapper
