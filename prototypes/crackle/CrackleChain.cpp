#include "CrackleChain.h"
#include <algorithm>
#include <cmath>

namespace crackle
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double maxWowSeconds = 0.004; // the delay's swing at WOW 1 (peak to peak)
constexpr int minDelay = 2;             // samples: the cubic reads one sample ahead of the swing
} // namespace

double Chain::Svf::lp (double x, double g, double k) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    return v2;
}

double Chain::Svf::hp (double x, double g, double k) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    return x - k * v1 - v2;
}

void Chain::prepare (double sampleRate)
{
    sr = sampleRate;
    int n = 1;
    while (n < (int) (maxWowSeconds * 2.0 * sr) + 16)
        n <<= 1;
    for (auto& d : delay)
        d.assign ((size_t) n, 0.0f);
    mask = n - 1;
    // half-band low-pass for the 2x saturation (31 taps, Blackman-windowed sinc)
    const int N = 31, C = N / 2;
    halfBand.assign ((size_t) N, 0.0f);
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const int m = i - C;
        const double s = m == 0 ? 0.5 : std::sin (pi * m / 2.0) / (pi * m);
        const double w = 0.42 - 0.5 * std::cos (2 * pi * i / (N - 1)) + 0.08 * std::cos (4 * pi * i / (N - 1));
        halfBand[(size_t) i] = (float) (s * w);
        sum += s * w;
    }
    for (auto& v : halfBand)
        v = (float) (v / sum);
    smooth = (float) (1.0 - std::exp (-1.0 / (0.020 * sr)));
    reset();
}

void Chain::reset()
{
    for (auto* f : { &hp[0], &hp[1], &lp[0], &lp[1], &hissHp[0], &hissHp[1], &hissLp[0], &hissLp[1] })
        *f = {};
    for (auto& d : delay)
        std::fill (d.begin(), d.end(), 0.0f);
    write = 0;
    for (auto& h : upHist)
        h.fill (0.0f);
    for (auto& h : downHist)
        h.fill (0.0f);
    upPos = downPos = 0;
    wowPhase = flutterPhase = drift = driftTarget = 0.0;
    driftCountdown = 0;
    rng = {};
    now = {};
}

void Chain::process (float* left, float* right, int numSamples, const Amounts& target) noexcept
{
    float* io[2] { left, right };
    double gHp = 0.0, gLp = 0.0, gHissHp = std::tan (pi * 2000.0 / sr), gHissLp = std::tan (pi * std::min (8000.0, 0.45 * sr) / sr);
    const double k = 1.4142135623730951; // Butterworth
    const int N = (int) halfBand.size();
    for (int i = 0; i < numSamples; ++i)
    {
        now.band += smooth * (target.band - now.band);
        now.wow += smooth * (target.wow - now.wow);
        now.hiss += smooth * (target.hiss - now.hiss);
        now.saturation += smooth * (target.saturation - now.saturation);
        if ((i & 15) == 0)
        {
            // BAND: HP 40 -> 350 Hz, LP 18 -> 3.5 kHz, on a log scale
            const double b = std::clamp ((double) now.band, 0.0, 1.0);
            gHp = std::tan (pi * 40.0 * std::pow (350.0 / 40.0, b) / sr);
            gLp = std::tan (pi * std::min (0.45 * sr, 18000.0 * std::pow (3500.0 / 18000.0, b)) / sr);
        }

        // WOW: 0.5 Hz (its rate drifting 0.4-0.7 Hz, as a worn capstan) and FLUTTER 6-10 Hz, tiny
        if (--driftCountdown <= 0)
        {
            driftCountdown = (int) (0.25 * sr);
            driftTarget = rng.bipolar();
        }
        drift += (driftTarget - drift) * (1.0 / (0.5 * sr));
        wowPhase += (0.55 + 0.15 * drift) / sr;
        flutterPhase += (7.5 + 1.5 * drift) / sr;
        wowPhase -= std::floor (wowPhase);
        flutterPhase -= std::floor (flutterPhase);
        const double w = std::clamp ((double) now.wow, 0.0, 1.0);
        const double swing = w * maxWowSeconds * sr * (0.5 - 0.5 * std::cos (2.0 * pi * wowPhase))
                           + w * 0.00006 * sr * (0.5 - 0.5 * std::cos (2.0 * pi * flutterPhase));
        const double d = minDelay + swing;
        const int di = (int) d;
        const float t = (float) (d - di);

        const double sat = std::clamp ((double) now.saturation, 0.0, 1.0);
        const double drive = 1.0 + 5.0 * sat, bias = 0.12 * sat, wet = std::min (1.0, 4.0 * sat);
        // (normalised so a -6 dB peak comes out at -6 dB: louder parts squashed, quiet ones lifted)
        const double norm = 0.5 / (std::tanh (0.5 * drive + bias) - std::tanh (bias));
        const double hissLevel = 0.025 * std::pow (std::clamp ((double) now.hiss, 0.0, 1.0), 1.3);

        for (int ch = 0; ch < 2; ++ch)
        {
            double x = io[ch][i];
            // band
            x = lp[(size_t) ch].lp (hp[(size_t) ch].hp (x, gHp, k), gLp, k);
            // wow & flutter: write, then read back at the swinging delay (Catmull-Rom)
            auto& buf = delay[(size_t) ch];
            buf[(size_t) write] = (float) x;
            auto at = [&] (int back) { return buf[(size_t) ((write - back) & mask)]; };
            // cubic Hermite between y1 (di samples back, t = 0) and y0 (di + 1 back, t = 1)
            const float y1 = at (di), y0 = at (di + 1), yAfter = at (di - 1), yBefore = at (di + 2);
            const float m0 = 0.5f * (y0 - yAfter), m1 = 0.5f * (yBefore - y1);
            const float tt = t * t, ttt = tt * t;
            x = (2 * ttt - 3 * tt + 1) * y1 + (ttt - 2 * tt + t) * m0 + (-2 * ttt + 3 * tt) * y0 + (ttt - tt) * m1;
            // saturation, 2x oversampled: the input and a zero through the half-band, the
            // shaper, back. Always in the path (linear at 0: no dry / wet mix of two
            // differently delayed signals — a comb), its delay reported (latencySamples)
            {
                auto& up = upHist[(size_t) ch];
                auto& down = downHist[(size_t) ch];
                double out = 0.0;
                for (int phase = 0; phase < 2; ++phase)
                {
                    const int pos = (upPos + phase) & 63;
                    up[(size_t) pos] = phase == 0 ? (float) (2.0 * x) : 0.0f;
                    double u = 0.0;
                    for (int j = 0; j < N; ++j)
                        u += halfBand[(size_t) j] * up[(size_t) ((pos - j) & 63)];
                    down[(size_t) pos] = (float) (u + wet * ((std::tanh (drive * u + bias) - std::tanh (bias)) * norm - u));
                    if (phase == 1)
                        for (int j = 0; j < N; ++j)
                            out += halfBand[(size_t) j] * down[(size_t) ((pos - j) & 63)];
                }
                x = out;
            }
            // tape hiss: white noise, 2-8 kHz, a different noise per channel
            if (hissLevel > 0.0)
            {
                const double n = hissLp[(size_t) ch].lp (hissHp[(size_t) ch].hp (rng.bipolar(), gHissHp, k), gHissLp, k);
                x += hissLevel * n;
            }
            io[ch][i] = (float) x;
        }
        upPos = (upPos + 2) & 63;
        write = (write + 1) & mask;
    }
}
} // namespace crackle
