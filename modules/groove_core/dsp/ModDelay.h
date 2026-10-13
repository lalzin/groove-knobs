#pragma once

namespace gk
{
/** Circular delay line with modulated read taps, read with 4-point cubic
    Hermite interpolation (linear interpolation dulls the highs and adds
    modulation noise when the delay moves). Allocates in prepare() only. */
class ModDelay
{
public:
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        int size = 1;
        while (size < (int) std::ceil (maxDelaySeconds * sampleRate) + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    inline void push (float x) noexcept
    {
        writePos = (writePos + 1) & mask;
        buffer[(size_t) writePos] = x;
    }

    /** Reads `delaySamples` (>= 1) behind the last pushed sample. */
    inline float read (double delaySamples) const noexcept
    {
        const double pos = (double) writePos - delaySamples;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

private:
    inline float at (int i) const noexcept { return buffer[(size_t) (i & mask)]; }

    std::vector<float> buffer { 0.0f };
    int mask = 0, writePos = 0;
};

/** Pink noise (Paul Kellet's refined filter), deterministic, unit RMS. */
class PinkNoise
{
public:
    explicit PinkNoise (std::uint32_t seed = 12345u) : rng (seed) {}

    void reset (std::uint32_t seed) noexcept
    {
        rng = testsignals::Rng (seed);
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
    }

    inline float next() noexcept
    {
        const double w = rng.bipolar();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return (float) (pink * unitRmsScale);
    }

private:
    // The filter fed with uniform white noise in [-1, 1) has an RMS of ~1.79
    // (measured over 10 s, see bt test "Pink noise generator is unit RMS").
    static constexpr double unitRmsScale = 0.5598;
    testsignals::Rng rng;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};
} // namespace gk
