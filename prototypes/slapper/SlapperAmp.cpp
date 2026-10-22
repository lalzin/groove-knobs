#include "SlapperAmp.h"
#include <algorithm>
#include <cmath>

namespace slapper
{
namespace
{
constexpr double pi = 3.14159265358979323846;
}

Amp::Biquad Amp::lowShelf (double fs, double f, double db, double q)
{
    const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
    const double a0 = (A + 1) + (A - 1) * cw + s;
    Biquad b;
    b.b0 = (float) (A * ((A + 1) - (A - 1) * cw + s) / a0);
    b.b1 = (float) (2 * A * ((A - 1) - (A + 1) * cw) / a0);
    b.b2 = (float) (A * ((A + 1) - (A - 1) * cw - s) / a0);
    b.a1 = (float) (-2 * ((A - 1) + (A + 1) * cw) / a0);
    b.a2 = (float) (((A + 1) + (A - 1) * cw - s) / a0);
    return b;
}

Amp::Biquad Amp::highShelf (double fs, double f, double db, double q)
{
    const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
    const double a0 = (A + 1) - (A - 1) * cw + s;
    Biquad b;
    b.b0 = (float) (A * ((A + 1) + (A - 1) * cw + s) / a0);
    b.b1 = (float) (-2 * A * ((A - 1) + (A + 1) * cw) / a0);
    b.b2 = (float) (A * ((A + 1) + (A - 1) * cw - s) / a0);
    b.a1 = (float) (2 * ((A - 1) - (A + 1) * cw) / a0);
    b.a2 = (float) (((A + 1) - (A - 1) * cw - s) / a0);
    return b;
}

Amp::Biquad Amp::peak (double fs, double f, double db, double q)
{
    const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al / A;
    Biquad b;
    b.b0 = (float) ((1 + al * A) / a0);
    b.b1 = (float) (-2 * cw / a0);
    b.b2 = (float) ((1 - al * A) / a0);
    b.a1 = (float) (-2 * cw / a0);
    b.a2 = (float) ((1 - al / A) / a0);
    return b;
}

Amp::Biquad Amp::bandPass (double fs, double f, double q)
{
    // constant 0 dB peak gain
    const double w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
    Biquad b;
    b.b0 = (float) (al / a0);
    b.b1 = 0;
    b.b2 = (float) (-al / a0);
    b.a1 = (float) (-2 * cw / a0);
    b.a2 = (float) ((1 - al) / a0);
    return b;
}

Amp::Biquad Amp::lowPass (double fs, double f, double q)
{
    const double w = 2 * pi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
    Biquad b;
    b.b0 = (float) ((1 - cw) * 0.5 / a0);
    b.b1 = (float) ((1 - cw) / a0);
    b.b2 = b.b0;
    b.a1 = (float) (-2 * cw / a0);
    b.a2 = (float) ((1 - al) / a0);
    return b;
}

void Amp::prepare (double sampleRate)
{
    sr = sampleRate;
    // the half-band low-pass for 2x (31 taps, Blackman-windowed sinc at fs/4)
    const int N = 31, C = N / 2;
    hb.assign ((size_t) N, 0.0f);
    double sum = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const int m = n - C;
        const double sinc = m == 0 ? 0.5 : std::sin (pi * m / 2.0) / (pi * m);
        const double win = 0.42 - 0.5 * std::cos (2 * pi * n / (N - 1)) + 0.08 * std::cos (4 * pi * n / (N - 1));
        hb[(size_t) n] = (float) (sinc * win);
        sum += sinc * win;
    }
    for (auto& v : hb)
        v = (float) (v / sum);
    // the cabinet: 80 Hz (the sub), 220 Hz (the body), 600 Hz (the honk), 1.8 kHz (the cone's edge)
    const double f[4] { 80.0, 220.0, 600.0, 1800.0 }, q[4] { 1.1, 1.4, 1.8, 2.2 };
    for (int i = 0; i < 4; ++i)
        cab[i] = bandPass (sr, f[i], q[i]);
    cabDirect = lowPass (sr, 4500.0, 0.8);
    designedTone = designedSub = -9.0f;
    design();
    reset();
}

void Amp::design()
{
    if (designedTone == tone && designedSub == sub)
        return;
    designedTone = tone;
    designedSub = sub;
    // the velocity pickup tilts the low end down: a shelf under 120 Hz gives it back
    subShelf = lowShelf (sr, 120.0, 4.0 + 6.0 * sub);
    lowS = lowShelf (sr, 200.0, -6.0 * tone);
    highS = highShelf (sr, 2500.0, 8.0 * tone);
    mid = peak (sr, 700.0, -2.5, 0.9); // a light mid scoop: funk
    cabGain = { 0.9f + 0.9f * sub, 0.55f, 0.35f, 0.25f };
}

void Amp::reset()
{
    upHist.fill (0.0f);
    downHist.fill (0.0f);
    upPos = downPos = 0;
    for (auto* b : { &subShelf, &lowS, &highS, &mid, &cab[0], &cab[1], &cab[2], &cab[3], &cabDirect })
        b->clear();
}

float Amp::saturate (float x) const noexcept
{
    // y = tanh (g x + bias) - tanh (bias): asymmetric (a little even harmonic), unity-ish level
    const float g = 1.0f + 14.0f * drive, bias = 0.18f * drive;
    return (std::tanh (g * x + bias) - std::tanh (bias)) / (0.35f + 0.65f * std::sqrt (g));
}

float Amp::process (float x) noexcept
{
    design();
    // tone before the preamp (the bass's own controls), then the sub shelf
    x = subShelf.run (x);
    x = lowS.run (highS.run (mid.run (x)));
    // 2x: the input and a zero, each through the half-band, saturated, the half-band again, one kept
    const int N = (int) hb.size();
    float out = 0.0f;
    for (int phase = 0; phase < 2; ++phase)
    {
        upHist[(size_t) upPos] = phase == 0 ? 2.0f * x : 0.0f;
        float up = 0.0f;
        for (int k = 0; k < N; ++k)
            up += hb[(size_t) k] * upHist[(size_t) ((upPos - k) & 63)];
        upPos = (upPos + 1) & 63;
        downHist[(size_t) downPos] = saturate (up);
        if (phase == 1)
            for (int k = 0; k < N; ++k)
                out += hb[(size_t) k] * downHist[(size_t) ((downPos - k) & 63)];
        downPos = (downPos + 1) & 63;
    }
    // the cabinet: its four modes in parallel, and the cone's direct sound
    float c = 0.5f * cabDirect.run (out);
    for (int i = 0; i < 4; ++i)
        c += cabGain[(size_t) i] * cab[i].run (out);
    // a soft limiter, no hard clipping
    return 0.95f * std::tanh (c / 0.95f);
}
} // namespace slapper
