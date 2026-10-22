// SLAPPER — FUNK, the main knob: what it moves, each along its own curve.
//
//   0    a round, soft bass: soft fingers / thumb, high action (no clank),
//        hardly any rebound, clean amp, a little darker, more sub;
//   0.5  a balanced slap;
//   1    an aggressive slap: hard thumb, low action (the string slams into
//        the frets), a strong rebound, a driven amp, brighter, less sub.
//
// FUNK adds to the advanced knobs (SATURATION, TIMBRE), it does not replace
// them: a preset sets FUNK, then its knobs.
#pragma once
#include <algorithm>
#include <cmath>

namespace slapper::funk
{
inline float clamp01 (float x) { return std::clamp (x, 0.0f, 1.0f); }

/** Hardness of the thumb / fingers (contact time, brightness): rises early. */
inline float hardness (float f) { return std::pow (clamp01 (f), 0.8f); }
/** How easily the string reaches the frets (the action): late — the clank is the top of the knob. */
inline float clank (float f) { return 0.15f + 0.85f * std::pow (clamp01 (f), 1.5f); }
/** The thumb's rebound (its second strike): only from the middle up. */
inline float rebound (float f) { return 0.05f + 0.40f * clamp01 (f) * clamp01 (f); }
/** Added to SATURATION. */
inline float driveAdd (float f) { return 0.45f * clamp01 (f) * clamp01 (f); }
/** Added to TIMBRE: darker at 0, brighter at 1. */
inline float toneAdd (float f) { return -0.25f + 0.60f * clamp01 (f); }
/** The sub sine: rounder at 0. */
inline float sub (float f) { return 0.75f - 0.35f * clamp01 (f); }
} // namespace slapper::funk
