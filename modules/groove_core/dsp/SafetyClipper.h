#pragma once

namespace gk
{
/** Output safety net. Exactly the identity below the threshold (−1 dBFS),
    then a tanh knee that approaches the ceiling (−0.1 dBFS) asymptotically.
    The curve and its slope are continuous at the threshold. */
class SafetyClipper
{
public:
    static constexpr float threshold = 0.89125094f; // −1 dBFS
    static constexpr float ceiling = 0.98855309f;   // −0.1 dBFS

    static inline float process (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= threshold)
            return x;
        constexpr float range = ceiling - threshold;
        const float y = threshold + range * std::tanh ((a - threshold) / range);
        return std::copysign (y, x);
    }
};
} // namespace gk
