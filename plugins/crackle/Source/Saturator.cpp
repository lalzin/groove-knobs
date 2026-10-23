#include "Saturator.h"
#include <algorithm>
#include <cmath>

namespace crackle
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// the drive at amount 1, per type (amount 0: 1)
double driveFor (SatType t, double amount) noexcept
{
    switch (t)
    {
        case SatType::tube: return 1.0 + 6.0 * amount;
        case SatType::transformer: return 1.0 + 6.0 * amount;
        case SatType::speaker: return 1.5 + 8.0 * amount;
        case SatType::tape: return 1.0 + 5.0 * amount;
        case SatType::fold: return 1.0 + 3.0 * amount;
        case SatType::hard: return 1.0 + 7.0 * amount;
        case SatType::count: break;
    }
    return 1.0;
}

// the level each type gains when driven (dB at amount 1, measured on music:
// render.cpp), taken back so the types compare by character, not loudness
double makeupDb (SatType t) noexcept
{
    static const double db[] { 11.1, 3.9, 3.0, 5.4, 7.0, 8.4 };
    return t == SatType::count ? 0.0 : db[(int) t];
}

// each type's curve, without its filters
double shape (double v, SatType t, double d) noexcept
{
    switch (t)
    {
        case SatType::tube: return std::tanh (d * v + 0.35) - std::tanh (0.35); // biased: even harmonics
        case SatType::transformer: return std::atan (d * v);
        case SatType::speaker:
        {
            const double u = d * v; // soft on the push, hard on the pull: a cone at its limit
            return u > 0.0 ? 0.8 * std::tanh (u / 0.8) : std::max (-0.5, u);
        }
        case SatType::tape: return std::tanh (d * v);
        case SatType::fold: return std::sin (0.5 * pi * d * v);
        case SatType::hard:
        {
            const double u = d * v, a = std::fabs (u);
            const double y = a < 0.9 ? a : 0.9 + 0.1 * std::tanh ((a - 0.9) / 0.1);
            return u < 0.0 ? -y : y;
        }
        case SatType::count: break;
    }
    return v;
}
} // namespace

const char* Saturator::name (SatType t)
{
    static const char* n[] { "tube", "transformer", "speaker", "tape", "fold", "hard" };
    return n[(int) t];
}

double Saturator::Svf::run (double x, double g, double k, double& lp, double& bp, double& hp) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    lp = v2;
    bp = v1;
    hp = x - k * v1 - v2;
    return lp;
}

void Saturator::prepare (double rate)
{
    fs = rate;
    lowPre.c = lowPost.c = 1.0 - std::exp (-2.0 * pi * 120.0 / fs);  // TRANSFORMER: the lows
    highPre.c = highPost.c = 1.0 - std::exp (-2.0 * pi * 3000.0 / fs); // TAPE: the highs
    reset();
}

void Saturator::reset()
{
    focusHp = focusLp = speakerHp = speakerLp = {};
    lowPre.z = lowPost.z = highPre.z = highPost.z = 0.0;
    lastLo = lastHi = -1.0;
    lastAmount = -1.0;
}

double Saturator::core (double v, SatType t, double d) noexcept
{
    switch (t)
    {
        case SatType::transformer:
        {
            // the lows driven twice as hard (the core saturates on them first), given back after
            const double y = shape (v + lowPre.lp (v), t, d);
            return y - 0.5 * lowPost.lp (y);
        }
        case SatType::tape:
        {
            // the highs pushed into the curve first (pre-emphasis), lowered after
            const double y = shape (v + (v - highPre.lp (v)), t, d);
            return y - 0.5 * (y - highPost.lp (y));
        }
        case SatType::speaker:
        {
            double y = shape (v, t, d);
            // the cone rattles past its limit: noise following how far past
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            const double excess = std::max (0.0, std::fabs (d * v) - 0.5);
            y += 0.25 * excess * ((double) (seed >> 8) / 8388608.0 - 1.0);
            // a small, tired speaker: 180 Hz - 4 kHz, a little peak at the top
            double lp, bp, hp;
            speakerHp.run (y, std::tan (pi * 180.0 / fs), 1.4, lp, bp, hp);
            speakerLp.run (hp, std::tan (pi * 4000.0 / fs), 0.9, lp, bp, hp);
            return lp;
        }
        case SatType::tube:
        case SatType::fold:
        case SatType::hard:
        case SatType::count: break;
    }
    return shape (v, t, d);
}

double Saturator::process (double x, SatType type, double amount, double focusLowHz, double focusHighHz) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    if (amount <= 0.0)
        return x;
    // FOCUS: only this band is saturated, the rest passes clean
    if (std::fabs (focusLowHz - lastLo) > 1e-9 || std::fabs (focusHighHz - lastHi) > 1e-9)
    {
        lastLo = focusLowHz;
        lastHi = focusHighHz;
        gFocusLo = std::tan (pi * std::clamp (focusLowHz, 10.0, 0.45 * fs) / fs);
        gFocusHi = std::tan (pi * std::clamp (focusHighHz, 20.0, 0.45 * fs) / fs);
    }
    double lp, bp, hp, band;
    focusHp.run (x, gFocusLo, 1.4142, lp, bp, hp);
    focusLp.run (hp, gFocusHi, 1.4142, band, bp, hp);
    const double rest = x - band;
    // the drive and the level (recomputed only when the type or the amount moves)
    if (type != lastType || std::fabs (amount - lastAmount) > 1e-4)
    {
        lastType = type;
        lastAmount = amount;
        drive = driveFor (type, amount);
        // the curve's largest output up to a -6 dB peak brought to -6 dB (its
        // largest, not its value at 0.5: FOLD folds back through 0 there), and
        // the type's makeup taken back
        double top = 1e-6;
        for (int i = 1; i <= 8; ++i)
            top = std::max (top, std::fabs (shape (0.0625 * i, type, drive) - shape (0.0, type, drive)));
        gain = 0.5 / top * std::pow (10.0, -makeupDb (type) * amount / 20.0);
    }
    const double d = drive;
    const double shaped = core (band, type, d) * gain;
    const double wet = std::min (1.0, 4.0 * amount);
    return rest + band + wet * (shaped - band);
}
} // namespace crackle
