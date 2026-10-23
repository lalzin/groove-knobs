// CRACKLE — GRAIN, the main knob: each section along its own curve (spec 2.3).
// Step 1: BAND, WOW, HISS, SATURATION. (VINYL, CRUSH, DROPOUT: steps 2-3.)
#pragma once
#include <algorithm>
#include <cmath>

namespace crackle::grain
{
inline float clamp01 (float x) { return std::clamp (x, 0.0f, 1.0f); }
inline float smoothstep (float a, float b, float x)
{
    const float t = clamp01 ((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
/** BAND: the band closes from the full range (0) to 35 % of it (1), steadily. */
inline float band (float g) { return std::pow (clamp01 (g), 1.2f); }
/** WOW: comes in between 0.2 and 0.6, then keeps growing a little to 1. */
inline float wow (float g) { return 0.7f * smoothstep (0.2f, 0.6f, g) + 0.3f * clamp01 ((g - 0.6f) / 0.4f); }
/** HISS: from 0 up to 0.6, then a plateau. */
inline float hiss (float g) { return smoothstep (0.0f, 0.6f, g); }
/** SATURATION: 0 to 1, gentle up to the knee at 0.4, then harder. */
inline float saturation (float g)
{
    g = clamp01 (g);
    return g < 0.4f ? 0.3f * g / 0.4f : 0.3f + 0.7f * (g - 0.4f) / 0.6f;
}
} // namespace crackle::grain
