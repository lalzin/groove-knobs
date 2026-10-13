#pragma once

namespace gk
{
/** First-order DC blocking high-pass: y[n] = x[n] − x[n−1] + R·y[n−1]. */
class DcBlocker
{
public:
    void prepare (double sampleRate, double cutoffHz = 10.0) noexcept
    {
        r = std::exp (-2.0 * dspmath::pi * cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0; }

    inline float process (float x) noexcept
    {
        const double y = (double) x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return (float) y;
    }

private:
    double r = 0.999, x1 = 0.0, y1 = 0.0;
};
} // namespace gk
