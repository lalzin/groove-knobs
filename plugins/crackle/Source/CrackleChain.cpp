#include "CrackleChain.h"
#include <algorithm>
#include <cmath>
#include <complex>

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

float Chain::Pink::next (Rng& r) noexcept
{
    // Voss-McCartney: row i is redrawn every 2^i samples (the lowest set bit of the counter)
    ++counter;
    int row = 0;
    for (std::uint32_t c = counter; (c & 1u) == 0u && row < 4; c >>= 1)
        ++row;
    sum -= rows[(size_t) row];
    rows[(size_t) row] = r.bipolar();
    sum += rows[(size_t) row];
    return (sum + r.bipolar()) / 6.0f;
}

void Chain::Riaa::design (double fs)
{
    // H(s) = (1 + s t2) / ((1 + s t1)(1 + s t3)), bilinear, normalised to 0 dB at 1 kHz
    const double t1 = 3180e-6, t2 = 318e-6, t3 = 75e-6, K = 2.0 * fs;
    const double n0 = 1.0 + t2 * K, n1 = 2.0, n2 = 1.0 - t2 * K;          // (numerator times (1 + z^-1), a zero at Nyquist)
    const double d0 = t1 * t3 * K * K + (t1 + t3) * K + 1.0, d1 = 2.0 - 2.0 * t1 * t3 * K * K, d2 = t1 * t3 * K * K - (t1 + t3) * K + 1.0;
    b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    // the gain at 1 kHz
    const double w = 2.0 * pi * 1000.0 / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const double g = std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    b0 /= g; b1 /= g; b2 /= g;
    hpCoef = std::exp (-2.0 * pi * 100.0 / fs);
}

double Chain::Riaa::run (double x) noexcept
{
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    // 1-pole high-pass, 25 Hz
    hpOut = hpCoef * (hpOut + y - hpState);
    hpState = y;
    // (plus a little of the raw signal: a cartridge and a modern chain keep some
    // tick in the top — full de-emphasis alone sounded muffled)
    return hpOut + 0.3 * x;
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
    for (auto& sa : saturator)
        sa.prepare (2.0 * sr);
    for (auto& r : riaa)
        r.design (sr);
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
    now.noise = 0.5f;
    pink = {};
    crackles = {};
    for (auto& sa : saturator)
        sa.reset();
    radioHp = radioLp = {};
    held = {};
    holdCount = 0;
    radioFadePhase = staticEnv = staticHp = 0.0;
    staticDecay = std::exp (-1.0 / (0.004 * sr));
    for (auto& r : riaa)
        r.x1 = r.x2 = r.y1 = r.y2 = r.hpState = r.hpOut = 0.0;
    dropGain = 1.0;
    drop = Drop::none;
    dropFade = std::max (1, (int) (0.001 * sr));
    crackleCount = dropoutCount = 0;
}

void Chain::process (float* left, float* right, int numSamples, const Amounts& target) noexcept
{
    float* io[2] { left, right };
    const double gRadioHp = std::tan (pi * 450.0 / sr), gRadioLp = std::tan (pi * 2800.0 / sr);
    std::array<double, 2> stage {};
    double gHp = 0.0, gLp = 0.0, gHissHp = std::tan (pi * 2000.0 / sr), gHissLp = std::tan (pi * std::min (8000.0, 0.45 * sr) / sr);
    const double k = 1.4142135623730951; // Butterworth
    const int N = (int) halfBand.size();
    for (int i = 0; i < numSamples; ++i)
    {
        now.band += smooth * (target.band - now.band);
        now.wow += smooth * (target.wow - now.wow);
        now.hiss += smooth * (target.hiss - now.hiss);
        now.saturation += smooth * (target.saturation - now.saturation);
        now.noise += smooth * (target.noise - now.noise);
        now.vinyl += smooth * (target.vinyl - now.vinyl);
        now.flutter += smooth * (target.flutter - now.flutter);
        now.crush += smooth * (target.crush - now.crush);
        now.radio += smooth * (target.radio - now.radio);
        now.dropout += smooth * (target.dropout - now.dropout);
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
        const double fl = std::clamp ((double) now.flutter, 0.0, 1.0);
        const double swing = w * maxWowSeconds * sr * (0.5 - 0.5 * std::cos (2.0 * pi * wowPhase))
                           + fl * 0.00012 * sr * (0.5 - 0.5 * std::cos (2.0 * pi * flutterPhase));
        const double d = minDelay + swing;
        const int di = (int) d;
        const float t = (float) (d - di);

        const double sat = std::clamp ((double) now.saturation, 0.0, 1.0);
        const double noiseGain = 2.0 * std::clamp ((double) now.noise, 0.0, 1.0); // (0.5: 0 dB, 1: +6 dB)
        const double hissLevel = 0.025 * std::pow (std::clamp ((double) now.hiss, 0.0, 1.0), 1.3) * noiseGain;

        // VINYL: crackles at a rate that grows with it (up to ~30 a second): mostly
        // ticks, now and then a pop; raw impulses (the de-emphasis rounds them, below)
        const double vin = std::clamp ((double) now.vinyl, 0.0, 1.0);
        if (vin > 0.0 && rng.uniform() < (float) ((0.5 + 30.0 * vin * vin) / sr))
            for (auto& c : crackles)
                if (! c.on)
                {
                    c.on = true;
                    c.age = 0;
                    const bool pop = rng.uniform() < 0.05f;
                    c.pulse = std::max (1, (int) ((pop ? 0.001 + 0.002 * rng.uniform() : 0.0001 + 0.0003 * rng.uniform()) * sr));
                    const float u = rng.uniform();
                    c.amp = (pop ? 0.09f : 0.125f) * (0.15f + 0.85f * u * u * u); // (weighted: mostly small)
                    c.sign = rng.uniform() < 0.5f ? -1.0f : 1.0f;
                    c.tail = 0.25f;
                    c.tailDecay = (float) std::exp (-1.0 / ((0.005 + 0.015 * rng.uniform()) * 0.2 * sr)); // (-60 dB in 5-20 ms)
                    const float pan = 0.5f + 0.3f * rng.bipolar();
                    c.panL = std::sqrt (1.0f - pan);
                    c.panR = std::sqrt (pan);
                    ++crackleCount;
                    break;
                }
        double crackleL = 0.0, crackleR = 0.0;
        for (auto& c : crackles)
            if (c.on)
            {
                double v = 0.0;
                if (c.age < c.pulse)
                    v = c.sign * std::sin (pi * (c.age + 0.5) / c.pulse); // the impulse
                v += c.tail * rng.bipolar() * 0.5;                        // its grainy tail
                c.tail *= c.tailDecay;
                crackleL += c.amp * v * c.panL;
                crackleR += c.amp * v * c.panR;
                if (++c.age > c.pulse && c.tail < 1e-4f)
                    c.on = false;
            }
        const double surface = 0.004 * vin; // the pink floor: very low (after the de-emphasis)

        // DROPOUTS: from GRAIN 0.5, rare (up to ~1.5 a second at 1): 2-15 ms at 0, 1 ms fades
        const double dro = std::clamp ((double) now.dropout, 0.0, 1.0);
        switch (drop)
        {
            case Drop::none:
                if (dro > 0.0 && rng.uniform() < (float) (1.5 * dro * dro / sr))
                {
                    drop = Drop::falling;
                    dropHold = (int) ((0.002 + 0.013 * rng.uniform()) * sr);
                    ++dropoutCount;
                }
                break;
            case Drop::falling:
                dropGain -= 1.0 / dropFade;
                if (dropGain <= 0.0) { dropGain = 0.0; drop = Drop::held; }
                break;
            case Drop::held:
                if (--dropHold <= 0) drop = Drop::rising;
                break;
            case Drop::rising:
                dropGain += 1.0 / dropFade;
                if (dropGain >= 1.0) { dropGain = 1.0; drop = Drop::none; }
                break;
        }
        // (a smooth fade: raised cosine of the linear ramp)
        const double dropShape = 0.5 - 0.5 * std::cos (pi * dropGain);

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
                    down[(size_t) pos] = (float) saturator[(size_t) ch].process (u, target.satType, sat, target.focusLow, target.focusHigh);
                    if (phase == 1)
                        for (int j = 0; j < N; ++j)
                            out += halfBand[(size_t) j] * down[(size_t) ((pos - j) & 63)];
                }
                x = out;
            }
            stage[(size_t) ch] = x;
        }

        // CRUSH: bit depth 16 -> 6, sample-and-hold 1 -> 12 samples (the aliasing is the point)
        const double cr = std::clamp ((double) now.crush, 0.0, 1.0);
        if (cr > 0.0)
        {
            const double bits = 16.0 - 10.0 * cr, steps = std::exp2 (bits - 1.0);
            const int hold = 1 + (int) std::lround (11.0 * std::pow (cr, 1.5));
            if (++holdCount >= hold)
            {
                holdCount = 0;
                for (int ch = 0; ch < 2; ++ch)
                    held[(size_t) ch] = std::round (stage[(size_t) ch] * steps) / steps;
            }
            for (int ch = 0; ch < 2; ++ch)
                stage[(size_t) ch] = held[(size_t) ch];
        }

        // RADIO: the band of an AM set (450 Hz - 2.8 kHz), mono, fading slowly in and out
        const double ra = std::clamp ((double) now.radio, 0.0, 1.0);
        double radioStatic = 0.0;
        if (ra > 0.0)
        {
            const double mid = 0.5 * (stage[0] + stage[1]);
            radioFadePhase += (0.08 + 0.05 * drift) / sr;
            radioFadePhase -= std::floor (radioFadePhase);
            const double fade = 1.0 - ra * 0.45 * (0.5 + 0.5 * std::sin (2.0 * pi * radioFadePhase)) * (0.6 + 0.4 * std::sin (2.0 * pi * 2.7 * radioFadePhase));
            for (int ch = 0; ch < 2; ++ch)
            {
                const double mono = stage[(size_t) ch] + ra * (mid - stage[(size_t) ch]);
                const double narrow = radioLp[(size_t) ch].lp (radioHp[(size_t) ch].hp (mono, gRadioHp, 0.9), gRadioLp, 0.9) * 1.4; // (a little resonance)
                stage[(size_t) ch] = (mono + ra * (narrow - mono)) * fade;
            }
            // static: light impulsive noise, bursts that come and go
            if (rng.uniform() < (float) (60.0 * ra / sr))
                staticEnv = 0.4 + 0.6 * rng.uniform();
            staticEnv *= staticDecay;
            const double n = rng.bipolar();
            staticHp += 0.5 * (n - staticHp); // (thin: a crude high-pass of the noise)
            radioStatic = ra * 0.04 * (staticEnv * (n - staticHp) + 0.15 * (n - staticHp));
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            double x = stage[(size_t) ch];
            // tape hiss: white noise, 2-8 kHz, a different noise per channel
            if (hissLevel > 0.0)
            {
                const double n = hissLp[(size_t) ch].lp (hissHp[(size_t) ch].hp (rng.bipolar(), gHissHp, k), gHissLp, k);
                x += hissLevel * n;
            }
            // vinyl: the surface (pink) and the crackles, under NOISE
            if (vin > 0.0)
                x += noiseGain * riaa[(size_t) ch].run (surface * pink[(size_t) ch].next (rng) + (ch == 0 ? crackleL : crackleR));
            // radio static, under NOISE
            x += noiseGain * radioStatic;
            // a dropout takes everything (the tape lifts off the head)
            x *= dropShape;
            io[ch][i] = (float) x;
        }
        upPos = (upPos + 2) & 63;
        write = (write + 1) & mask;
    }
}
} // namespace crackle
