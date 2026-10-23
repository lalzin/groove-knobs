#include "SlapperVoice.h"
#include <algorithm>
#include <cmath>

namespace slapper
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double openStrings[] { 41.2034, 55.0, 73.4162, 97.9989 }; // E1 A1 D2 G2
constexpr double pickup = 0.19;        // the pickup's place, from the bridge (a P-bass's split coil)
constexpr double pickupWidth = 0.022;  // its magnetic window (~2 cm of an 86 cm scale)
constexpr int retuneEvery = 16;        // samples between pole updates while the pitch moves

double sinc (double x) { return std::fabs (x) < 1e-9 ? 1.0 : std::sin (x) / x; }

} // namespace

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    // the coil and its cable: a resonant low-pass, ~3.2 kHz
    {
        const double w = 2.0 * pi * std::min (3200.0, 0.4 * sr) / sr, al = std::sin (w) / (2.0 * 1.3), cw = std::cos (w), a0 = 1.0 + al;
        coil.b0 = (float) ((1.0 - cw) * 0.5 / a0); coil.b1 = (float) ((1.0 - cw) / a0); coil.b2 = coil.b0;
        coil.a1 = (float) (-2.0 * cw / a0); coil.a2 = (float) ((1.0 - al) / a0);
    }
    // the low cut: 2nd-order high-pass at 32 Hz (under the low E, over the rumble)
    {
        const double w = 2.0 * pi * 32.0 / sr, al = std::sin (w) / (2.0 * 0.7071), cw = std::cos (w), a0 = 1.0 + al;
        lowCut.b0 = (float) ((1.0 + cw) * 0.5 / a0); lowCut.b1 = (float) (-(1.0 + cw) / a0); lowCut.b2 = lowCut.b0;
        lowCut.a1 = (float) (-2.0 * cw / a0); lowCut.a2 = (float) ((1.0 - al) / a0);
    }
    subAttack = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
    reset();
}

void Voice::reset() noexcept
{
    for (auto& b : bank)
    {
        b.alive = false;
        b.K = 0;
        b.subEnv = b.subTarget = 0.0f;
        b.reboundIn = -1;
    }
    held = false;
    glidePos = 1.0;
    bend = appliedRatio = 1.0;
    ghostEnv = ghostLp = ghostBp = 0.0f;
    sRe.fill (0.0f);
    sIm.fill (0.0f);
    coil.clear();
    lowCut.clear();
}

void Voice::setup (Bank& b, double hz, float vel, const NoteParams& p) noexcept
{
    const bool ghost = vel < ghostVelocity;
    if (ghost)
        vel = 0.35f; // (a ghost is played lightly but heard: its level is that of a soft note)
    const double mute = ghost ? 1.0 : std::clamp ((double) p.mute, 0.0, 1.0);
    const double stiff = std::clamp ((double) p.string, 0.0, 1.0);

    // the string it is played on: the highest open string at or under the note
    // (a fretted string is shorter, so stiffer: inharmonicity grows with the fret)
    double open = openStrings[0];
    for (double o : openStrings)
        if (o <= hz * 1.001)
            open = o;
    const double B = (0.6 + 1.6 * stiff) * 1e-4 * std::pow (hz / open, 2.0);
    const double fMax = std::min (0.45 * sr, 6000.0);
    auto modeHz = [&] (int n) { return n * hz * std::sqrt (1.0 + B * n * n) / std::sqrt (1.0 + B); };
    int K = 0;
    while (K < maxModes && modeHz (K + 1) < fMax)
        ++K;
    K = std::max (K, 1);
    b.K = K;
    b.hz = hz;
    b.ratio = 1.0;

    // decay: sigma (f) = b1 + b2 f + b3 f^2 (amplitude, 1/s): the fundamental
    // rings ~5 s on a low E, the highs are gone in tenths of a second
    const double t60Fund = std::max (1.5, 5.0 - 0.8 * std::log2 (hz / 41.2));
    const double b1 = 6.91 / t60Fund * (1.0 + 30.0 * mute) * (ghost ? 3.0 : 1.0);
    const double b2 = 0.002 * (1.0 + 10.0 * mute);
    const double b3 = 0.7e-5 * (1.4 - 0.8 * stiff) * (1.0 + 4.0 * mute);

    // the three attacks and their weights (ATTAQUE: 0 DOIGT, 0.5 SLAP, 1 POP)
    const double a = ghost ? 0.5 : std::clamp ((double) p.attack, 0.0, 1.0);
    const double wF = std::max (0.0, 1.0 - 2.0 * a), wS = 1.0 - std::fabs (1.0 - 2.0 * a), wP = std::max (0.0, 2.0 * a - 1.0);
    const double hard = std::clamp ((double) p.hardness, 0.0, 1.0);
    const double xF = std::clamp ((double) p.pick, 0.05, 0.5);
    const double xS = std::clamp (0.30 + (p.pick - 0.15), 0.1, 0.5); // the thumb and the pop: the end of the neck
    const double tau = (1.4 - 0.8 * vel) * 1e-3 * (1.3 - 0.6 * hard); // the thumb's contact time
    const double fcF = (1500.0 + 2500.0 * vel) * (0.7 + 0.6 * hard);   // the finger's flesh

    // each attack's modal shape, then scaled to its displacement at its point (mm)
    double sumF = 0.0, sumS = 0.0, sumP = 0.0;
    std::array<double, maxModes> cF {}, cS {}, cP {};
    for (int k = 0; k < K; ++k)
    {
        const int n = k + 1;
        const double f = modeHz (n), w = 2.0 * pi * f;
        b.omega[(size_t) k] = (float) w;
        if (wF > 0.0)
        {
            // a pull and release: a triangle, its corner rounded by the fingertip, the flesh a low-pass
            const double s = std::sin (n * pi * xF);
            cF[(size_t) k] = 2.0 * s / (n * n * pi * pi * xF * (1.0 - xF)) * sinc (n * pi * 0.012) / std::sqrt (1.0 + std::pow (f / fcF, 4.0));
            sumF += cF[(size_t) k] * s;
        }
        if (wS > 0.0)
        {
            // a strike: a velocity bump under the thumb, the contact's duration a low-pass on it
            const double s = std::sin (n * pi * xS);
            cS[(size_t) k] = 2.0 * s * sinc (n * pi * 0.015) / std::sqrt (1.0 + std::pow (w * tau / pi, 4.0)) / w;
            sumS += cS[(size_t) k] * s;
        }
        if (wP > 0.0)
        {
            // pulled away from the board and let go: a sharp triangle
            const double s = std::sin (n * pi * xS);
            cP[(size_t) k] = 2.0 * s / (n * n * pi * pi * xS * (1.0 - xS)) * sinc (n * pi * 0.006) / std::sqrt (1.0 + std::pow (f / 5000.0, 4.0));
            sumP += cP[(size_t) k] * s;
        }
    }
    auto norm = [] (double A, double sum) { return A / (std::fabs (sum) > 1e-12 ? std::fabs (sum) : 1.0); };
    const double gF = wF * norm (0.5 + 1.1 * vel, sumF);
    const double gS = wS * norm ((2.0 + 4.0 * vel) * (0.85 + 0.3 * hard), sumS);
    const double gP = wP * norm (-(1.5 + 3.5 * vel), sumP); // (away from the frets)
    // the three near one level at the pickup (the finger moves the string least)
    const double level = wF * 2.4 + wS * 1.0 + wP * 1.5;
    const double rebound = std::clamp ((double) p.rebound, 0.0, 1.0);

    // the frets under the string: at the struck point, or at the last fret
    // (~0.3 from the bridge) when plucked between the pickups
    const double xf = std::max (0.3, wF * xF + (wS + wP) * xS);
    fretGap = 2.2 - 1.2 * std::clamp ((double) p.clank, 0.0, 1.0);
    b.reach = 0.0;
    for (int k = 0; k < K; ++k)
    {
        const int n = k + 1;
        const double w = b.omega[(size_t) k], f = w / (2.0 * pi);
        const double sigma = b1 + b2 * f + b3 * f * f;
        const double r = std::exp (-sigma / sr), th = w / sr;
        b.radius[(size_t) k] = (float) r;
        b.pr[(size_t) k] = (float) (r * std::cos (th));
        b.pi[(size_t) k] = (float) (r * std::sin (th));
        // the strike's pulse centred 2 tau after the start (not cut in half at it: no click)
        const double lag = -w * 2.0 * tau;
        const double strikeRe = gS * cS[(size_t) k] * std::cos (lag), strikeIm = gS * cS[(size_t) k] * std::sin (lag);
        b.re[(size_t) k] = (float) strikeRe;
        b.im[(size_t) k] = (float) (strikeIm + gF * cF[(size_t) k] + gP * cP[(size_t) k]);
        b.rebRe[(size_t) k] = (float) (rebound * strikeRe);
        b.rebIm[(size_t) k] = (float) (rebound * strikeIm);
        // the pickup: the string's velocity (omega Re z) under its window
        b.out[(size_t) k] = (float) (0.05 * level * w / (2.0 * pi * hz) * std::pow (hz / 41.2, 0.2) * std::sin (n * pi * pickup) * sinc (n * pi * pickupWidth));
        b.fret[(size_t) k] = (float) std::sin (n * pi * xf);
        b.fretInvW[(size_t) k] = (float) (2.0 * std::sin (n * pi * xf) / (w * sr));
        b.reach += (double) b.fret[(size_t) k] * b.pi[(size_t) k] * b.fretInvW[(size_t) k];
    }
    // the thumb's rebound on the fret: a second, weaker strike 6-12 ms later
    b.reboundIn = wS > 0.01 && rebound > 0.0 && ! ghost ? (int) ((0.006 + 0.006 * (1.0 - vel)) * sr) : -1;
    b.contact = ! ghost;
    b.alive = true;

    // the sub: on the fundamental, from the velocity, decays with it (none on a ghost)
    b.subEnv = 0.0f;
    b.subTarget = ghost ? 0.0f : 0.25f + 0.75f * vel;
    b.subDecay = (float) std::exp (-6.91 / (t60Fund / (1.0 + 30.0 * mute) * sr));
    subLevel = std::clamp (p.sub, 0.0f, 2.0f);

    // a ghost: the palm-muted thump, a short dull burst
    if (ghost)
    {
        ghostEnv = 0.05f;
        ghostDecay = (float) std::exp (-1.0 / (0.012 * sr));
    }

    // the sympathetic string: 0.2 % sharp, its first modes ringing ~1.2 s
    sympGain = 0.35f * std::clamp (p.body, 0.0f, 1.0f);
    for (int k = 0; k < sympModes; ++k)
    {
        const double w = 2.0 * pi * modeHz (k + 1) * 1.002 / sr;
        const double r = std::exp (-6.91 / (1.2 * sr) * (1.0 + 0.3 * k));
        sPr[(size_t) k] = (float) (r * std::cos (w));
        sPi[(size_t) k] = (float) (r * std::sin (w));
        sIn[(size_t) k] = (float) (1.0 - r);
    }
}

void Voice::damp (Bank& b, double t60) noexcept
{
    const float g = (float) std::exp (-6.91 / (t60 * sr));
    for (int k = 0; k < b.K; ++k)
    {
        b.pr[(size_t) k] *= g;
        b.pi[(size_t) k] *= g;
        b.radius[(size_t) k] *= g;
    }
    b.reboundIn = -1;
    b.contact = false;
    b.subDecay = std::min (b.subDecay, g); // (the sub goes with it)
}

void Voice::retune (Bank& b, double ratio) noexcept
{
    b.ratio = ratio;
    for (int k = 0; k < b.K; ++k)
    {
        const double th = b.omega[(size_t) k] * ratio / sr;
        b.pr[(size_t) k] = b.radius[(size_t) k] * (float) std::cos (th);
        b.pi[(size_t) k] = b.radius[(size_t) k] * (float) std::sin (th);
    }
}

void Voice::noteOn (double hz, float velocity, const NoteParams& p) noexcept
{
    const float vel = std::clamp (velocity, 0.0f, 1.0f);
    // the ringing string: the fretting hand lifts and the next finger lands — damped, ~15 ms
    if (bank[cur].alive)
        damp (bank[cur], 0.015);
    cur ^= 1;
    setup (bank[cur], hz, vel, p);
    targetHz = hz;
    glidePos = 1.0;
    glideFrom = glideTo_ = 0.0;
    appliedRatio = 1.0;
    retuneCountdown = 0;
    held = true;
    contactSamples = 0;
    if (bend != 1.0)
        retune (bank[cur], bend), appliedRatio = bend;
}

void Voice::glideTo (double hz, double seconds) noexcept
{
    Bank& b = bank[cur];
    if (! b.alive)
        return;
    targetHz = hz;
    held = true;
    glideFrom = std::log (b.ratio / bend);
    glideTo_ = std::log (hz / b.hz);
    glidePos = 0.0;
    glideStep = 1.0 / std::max (1.0, seconds * sr);
    retuneCountdown = 0;
}

void Voice::noteOff (double seconds) noexcept
{
    held = false;
    if (bank[cur].alive)
        damp (bank[cur], std::max (0.01, seconds));
}

float Voice::run (Bank& b) noexcept
{
    if (b.reboundIn >= 0 && b.reboundIn-- == 0)
        for (int k = 0; k < b.K; ++k)
        {
            b.re[(size_t) k] += b.rebRe[(size_t) k];
            b.im[(size_t) k] += b.rebIm[(size_t) k];
        }
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
    if (b.contact)
    {
        // the string at the contact point against the frets: a stiff, one-sided spring,
        // never pushing more than what puts it back on the fret by the next sample
        const float* fr = b.fret.data();
        float q = 0.0f;
        for (int k = 0; k < b.K; ++k)
            q += fr[k] * im[k];
        const double pen = q - fretGap;
        if (pen > 0.0)
        {
            const float F = (float) -std::min (4.0e8 * pen * std::sqrt (pen), pen / std::max (1e-30, b.reach));
            const float* fi = b.fretInvW.data();
            for (int k = 0; k < b.K; ++k)
                re[k] += F * fi[k];
            ++contactSamples;
        }
    }
    return acc;
}

float Voice::subOf (Bank& b) noexcept
{
    b.subEnv += subAttack * (b.subTarget - b.subEnv);
    b.subTarget *= b.subDecay;
    const float r = b.re[0], i = b.im[0], m = std::sqrt (r * r + i * i);
    return m > 1e-12f ? 0.03f * subLevel * b.subEnv * r / m : 0.0f;
}

float Voice::process() noexcept
{
    Bank& now = bank[cur];
    Bank& old = bank[cur ^ 1];
    if (! now.alive && ! old.alive && ghostEnv < 1e-6f)
        return 0.0f;

    // the pitch: a slide in progress and the bend, poles rebuilt every 16 samples
    if (now.alive && --retuneCountdown <= 0)
    {
        retuneCountdown = retuneEvery;
        double ratio = bend;
        if (glidePos < 1.0)
        {
            glidePos = std::min (1.0, glidePos + glideStep * retuneEvery);
            // (a smooth start and end: the finger accelerates, then lands)
            const double s = glidePos * glidePos * (3.0 - 2.0 * glidePos);
            ratio *= std::exp (glideFrom + (glideTo_ - glideFrom) * s);
        }
        else
            ratio *= std::exp (glideTo_);
        if (std::fabs (ratio - appliedRatio) > 1e-7)
        {
            retune (now, ratio);
            appliedRatio = ratio;
        }
    }

    float y = 0.0f;
    if (now.alive)
        y += run (now);
    if (old.alive)
        y += run (old);
    for (auto* b : { &now, &old })
        if (b->alive && std::fabs (b->re[0]) + std::fabs (b->im[0]) < 1e-6f && b->subEnv < 1e-5f && b->reboundIn < 0)
        {
            // gone: under -100 dB of its start (its fundamental, the last to die)
            bool quiet = true;
            for (int k = 1; k < std::min (b->K, 8); ++k)
                quiet = quiet && std::fabs (b->re[(size_t) k]) + std::fabs (b->im[(size_t) k]) < 1e-6f;
            if (quiet)
                b->alive = false;
        }

    // a ghost's thump: dull noise (band 150-900 Hz), 12 ms
    if (ghostEnv > 1e-6f)
    {
        const float n = noise() * ghostEnv;
        ghostEnv *= ghostDecay;
        ghostLp += 0.11f * (n - ghostLp);           // ~900 Hz
        ghostBp += 0.02f * (ghostLp - ghostBp);      // ~150 Hz
        y += 3.0f * (ghostLp - ghostBp);
    }

    // the sympathetic string, driven by this one
    if (sympGain > 0.0f)
    {
        float s = 0.0f;
        for (int k = 0; k < sympModes; ++k)
        {
            const float r = sRe[(size_t) k], i = sIm[(size_t) k];
            sRe[(size_t) k] = r * sPr[(size_t) k] - i * sPi[(size_t) k] + sIn[(size_t) k] * y;
            sIm[(size_t) k] = r * sPi[(size_t) k] + i * sPr[(size_t) k];
            s += sRe[(size_t) k];
        }
        y += sympGain * s;
    }

    y = coil.run (y);
    for (auto* b : { &now, &old })
        if (b->alive && b->K > 0)
            y += subOf (*b);
    return lowCut.run (y);
}
} // namespace slapper
