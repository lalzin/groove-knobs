#include "SynthVoice.h"
#include <algorithm>
#include <cmath>

namespace slapper
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;

// polyBLEP: the step's residual around a discontinuity at phase 0 (phase t, increment dt)
double blep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}
double frac (double x) noexcept { return x - std::floor (x); }
double saw (double t, double dt) noexcept { return 2.0 * t - 1.0 - blep (t, dt); }
double pulse (double t, double dt, double w) noexcept { return (t < w ? 1.0 : -1.0) + blep (t, dt) - blep (frac (t - w + 1.0), dt); }
double advance (double& ph, double inc) noexcept
{
    ph += inc;
    if (ph >= 1.0)
        ph -= 1.0;
    return ph;
}
} // namespace

void SynthVoice::prepare (double sampleRate)
{
    sr = sampleRate;
    fmIndexCoef = 1.0 - std::exp (-1.0 / (0.0007 * sr));
    reset();
}

void SynthVoice::reset() noexcept
{
    stage = Stage::idle;
    held = false;
    amp = fenv = 0.0;
    s1 = s2 = s3 = s4 = 0.0;
    dcX = dcY = 0.0;
    fmPrev = fmIndex = 0.0;
    bend = 1.0;
}

void SynthVoice::noteOn (double noteHz, float vel, const SynthParams& params) noexcept
{
    p = params;
    ghost = vel < 0.15f;
    velocity = ghost ? 0.35f : std::clamp (vel, 0.0f, 1.0f);
    targetHz = noteHz;
    logHz = logTo = std::log (noteHz);
    glidePos = 1.0;
    pitchCountdown = 0;
    const double mute = std::clamp ((double) p.mute, 0.0, 1.0), open = 1.0 - mute;
    const double attackShape = std::clamp ((double) p.attack, 0.0, 1.0);
    switch (p.engine)
    {
        case Engine::fm: sustain = 0.45 * open; break;
        case Engine::pulse: sustain = 0.8 * std::pow (open, 1.5); break;
        case Engine::analog:
        case Engine::string: sustain = 0.85 * std::pow (open, 1.5); break;
    }
    // the amp: 1.5 ms up (from wherever it is: no click on a retrigger), to the sustain, the release
    const double decayTau = ghost ? 0.03 : 0.15 + 1.2 * open;
    const double releaseTau = ghost ? 0.01 : 0.02 + 0.12 * open;
    attackStep = 1.0 / (0.0015 * sr);
    decayCoef = std::exp (-1.0 / (decayTau * sr));
    releaseCoef = std::exp (-1.0 / (releaseTau * sr));
    // the bright attack: how fast it closes (ATTACK 1: a short pluck)
    const double fTau = ghost ? 0.012 : 0.04 + 0.6 * (1.0 - attackShape) * (1.0 - attackShape);
    fenv = 1.0;
    fenvCoef = std::exp (-1.0 / (fTau * sr));
    stage = Stage::attack;
    held = true;
}

void SynthVoice::glideTo (double noteHz, double seconds) noexcept
{
    if (stage == Stage::idle)
        return;
    targetHz = noteHz;
    held = true;
    logFrom = logHz;
    logTo = std::log (noteHz);
    glidePos = 0.0;
    glideStep = 1.0 / std::max (1.0, seconds * sr);
}

void SynthVoice::noteOff() noexcept
{
    held = false;
    if (stage != Stage::idle)
        stage = Stage::release;
}

double SynthVoice::ladder (double x, double fc, double k) noexcept
{
    // zero-delay-feedback 4-pole ladder (Zavalishin): four TPT one-poles, the
    // feedback solved, the input saturated (the resonance growls, never blows up)
    const double g = std::tan (3.14159265358979323846 * fc / sr), G = g / (1.0 + g), inv = 1.0 / (1.0 + g);
    const double S = (G * G * G * s1 + G * G * s2 + G * s3 + s4) * inv;
    double u = (x - k * S) / (1.0 + k * G * G * G * G);
    u = std::tanh (u);
    auto stagePole = [G] (double in, double& s)
    {
        const double v = (in - s) * G, y = v + s;
        s = y + v;
        return y;
    };
    const double y1 = stagePole (u, s1), y2 = stagePole (y1, s2), y3 = stagePole (y2, s3);
    return stagePole (y3, s4);
}

float SynthVoice::process() noexcept
{
    if (stage == Stage::idle)
        return 0.0f;

    // pitch: the slide and the bend, every 16 samples
    if (--pitchCountdown <= 0)
    {
        pitchCountdown = 16;
        if (glidePos < 1.0)
        {
            glidePos = std::min (1.0, glidePos + glideStep * 16.0);
            const double s = glidePos * glidePos * (3.0 - 2.0 * glidePos);
            logHz = logFrom + (logTo - logFrom) * s;
        }
        f = std::exp (logHz) * bend;
    }

    // envelopes
    switch (stage)
    {
        case Stage::attack:
            amp += attackStep;
            if (amp >= 1.0)
            {
                amp = 1.0;
                stage = Stage::decay;
            }
            break;
        case Stage::decay: amp = sustain + (amp - sustain) * decayCoef; break;
        case Stage::release:
            amp *= releaseCoef;
            if (amp < 1e-5)
            {
                stage = Stage::idle;
                held = false;
                return 0.0f;
            }
            break;
        case Stage::idle: break;
    }
    fenv *= fenvCoef;

    const double inc = f / sr, funk = std::clamp ((double) p.funk, 0.0, 1.0);
    const double m = std::clamp ((p.pick - 0.05) / 0.45, 0.0, 1.0);
    const double body = std::clamp ((double) p.body, 0.0, 1.0), str = std::clamp ((double) p.string, 0.0, 1.0);
    const double velGain = 0.4 + 0.6 * velocity;
    double y = 0.0;
    switch (p.engine)
    {
        case Engine::analog:
        {
            const double t1 = advance (ph1, inc), inc2 = inc * std::pow (2.0, (4.0 + 14.0 * str) / 1200.0), t2 = advance (ph2, inc2);
            // BODY: a sine on the fundamental, in phase with the saw's (whose fundamental is -sin):
            // weight at the note's own pitch, never an octave under it
            const double osc = 0.5 * saw (t1, inc) + 0.5 * ((1.0 - m) * saw (t2, inc2) + m * pulse (t2, inc2, 0.5)) - 0.9 * body * std::sin (twoPi * t1);
            const double octaves = (1.5 + 5.0 * funk) * (0.5 + 0.5 * velocity) * fenv + (ghost ? 0.0 : 1.0); // (at rest an octave over: the closing filter barely bends the pitch)
            const double fc = std::min ({ 0.42 * sr, 16000.0, f * 1.2 * std::exp2 (octaves) });
            const double k = 3.4 * std::pow (funk, 1.2);
            y = ladder (0.6 * osc, fc, k) * (1.0 + 0.45 * k);
            break;
        }
        case Engine::pulse:
        {
            const double lfo = std::sin (twoPi * advance (phLfo, 0.7 / sr));
            const double w = std::clamp (0.5 - 0.42 * m + 0.2 * str * lfo, 0.05, 0.95);
            const double t1 = advance (ph1, inc);
            const double osc = 0.7 * pulse (t1, inc, w) + 0.9 * body * std::sin (twoPi * t1); // (the pulse's fundamental is +sin)
            const double octaves = (1.0 + 4.0 * funk) * (0.5 + 0.5 * velocity) * fenv + 1.8; // (open at rest: a polysynth bass is bright)
            const double fc = std::min ({ 0.42 * sr, 16000.0, f * 1.5 * std::exp2 (octaves) });
            const double k = 1.5 * funk;
            y = ladder (0.6 * osc, fc, k) * (1.0 + 0.45 * k);
            break;
        }
        case Engine::fm:
        {
            const double ratio = m < 0.34 ? 1.0 : m < 0.67 ? 2.0 : 3.0;
            const double indexTarget = ((0.5 + 6.0 * funk) * (0.4 + 0.6 * velocity) * fenv + 0.35 * sustain) * (ghost ? 0.3 : 1.0);
            fmIndex += fmIndexCoef * (indexTarget - fmIndex);
            const double mod = std::sin (twoPi * advance (phMod, inc * ratio) + 1.2 * str * fmPrev);
            fmPrev = mod;
            const double t1 = advance (ph1, inc);
            const double car = std::sin (twoPi * t1 + fmIndex * mod);
            y = 0.55 * car + 0.45 * body * std::sin (twoPi * t1);
            break;
        }
        case Engine::string: break;
    }
    y *= amp * velGain * 0.32;
    // DC blocker (the pulse's width moves its mean)
    dcY = y - dcX + 0.9995 * dcY;
    dcX = y;
    return (float) dcY;
}
} // namespace slapper
