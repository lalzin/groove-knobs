// CRACKLE — GRAIN, the main knob: each section along its own curve (spec 2.3).
// BAND, WOW, HISS, SATURATION (step 1), VINYL, DROPOUT (step 2), CRUSH, RADIO (step 3).
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
/** FLUTTER: after the wow (a worn machine flutters once it already wobbles). */
inline float flutter (float g) { return smoothstep (0.3f, 0.8f, g); }
/** VINYL: a gentle slope from 0 to 0.35, then a plateau. */
inline float vinyl (float g) { return smoothstep (0.0f, 0.35f, g); }
/** DROPOUT: nothing under 0.5, then more and more likely. */
inline float dropout (float g) { return clamp01 ((g - 0.5f) / 0.5f); }
/** CRUSH: nothing under 0.7, then up to 1. */
inline float crush (float g) { return clamp01 ((g - 0.7f) / 0.3f); }
/** RADIO: the top of the knob, coming in from 0.55. */
inline float radio (float g) { return 0.8f * smoothstep (0.55f, 0.95f, g); }
} // namespace crackle::grain

#include "CrackleChain.h"

namespace crackle
{
/** The knobs: GRAIN, and one knob per effect. An effect's knob at `automatic`
    follows GRAIN (its curve above); once moved, it is its own. NOISE is the
    level of every added noise (0.5 = as set, 0 none, 1 +6 dB). */
struct Knobs
{
    static constexpr float automatic = -1.0f;
    float grain = 0.0f;
    float band = automatic, wow = automatic, flutter = automatic, hiss = automatic, saturation = automatic, vinyl = automatic, dropout = automatic;
    float crush = automatic, radio = automatic;
    float noise = 0.5f;

    Amounts amounts() const
    {
        auto pick = [this] (float knob, float (*curve) (float)) { return knob >= 0.0f ? knob : curve (grain); };
        Amounts a;
        a.band = pick (band, grain::band);
        a.wow = pick (wow, grain::wow);
        a.flutter = pick (flutter, grain::flutter);
        a.hiss = pick (hiss, grain::hiss);
        a.saturation = pick (saturation, grain::saturation);
        a.vinyl = pick (vinyl, grain::vinyl);
        a.dropout = pick (dropout, grain::dropout);
        a.crush = pick (crush, grain::crush);
        a.radio = pick (radio, grain::radio);
        a.noise = noise;
        return a;
    }
};
} // namespace crackle
