#pragma once

namespace gk
{
/** Topology-preserving-transform state variable filter (Zavalishin, "The Art
    of VA Filter Design"; trapezoidal integration, zero-delay feedback).

    Unlike direct-form biquads it stays well behaved when its cutoff and Q are
    modulated at audio rate, which is what fast knob automation needs. States
    are kept in double precision so very low cutoffs at high (oversampled)
    rates do not lose resolution. */
class TptSvf
{
public:
    struct Coeffs
    {
        double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.0;

        /** fc and fs in Hz; q > 0. fc must be below fs / 2 (callers clamp). */
        static Coeffs make (double fc, double fs, double q) noexcept
        {
            return fromGK (prewarp (fc, fs), 1.0 / q);
        }

        /** g = tan(pi·fc/fs) (see prewarp), k = 1/Q. Cheap: no transcendental,
            so it can run every sample while g and k are interpolated. */
        static Coeffs fromGK (double g, double k) noexcept
        {
            Coeffs c;
            c.k = k;
            c.a1 = 1.0 / (1.0 + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }

        static double prewarp (double fc, double fs) noexcept { return std::tan (dspmath::pi * fc / fs); }
    };

    void reset() noexcept { ic1 = ic2 = 0.0; }

    /** Returns the low-pass output. */
    inline double processLowpass (double v0, const Coeffs& c) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v2;
    }

    /** Returns the band-pass output (unity gain at the centre frequency). */
    inline double processBandpass (double v0, const Coeffs& c) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return c.k * v1;
    }

    /** Returns the high-pass output (used by analysis code). */
    inline double processHighpass (double v0, const Coeffs& c) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v0 - c.k * v1 - v2;
    }

    /** Soft-limits the band-pass integrator state: the resonance saturates
        ("growls") instead of ringing louder. Bounded, so always stable.
        `limit` is the state level where the curve bends (<= 0: linear). */
    inline void saturateResonance (double limit) noexcept
    {
        if (limit > 0.0)
            ic1 = limit * std::tanh (ic1 / limit);
    }

private:
    double ic1 = 0.0, ic2 = 0.0;
};
} // namespace gk
