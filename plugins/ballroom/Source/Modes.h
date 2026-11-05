#pragma once
// BALLROOM — the six modes (original designs): each is a set of delay
// lengths, a feedback matrix, diffusion settings and an early pattern.
//
//   Studio   small dense neutral room: short lines, dense early taps, flat-ish
//   Club     medium room, warm (lows held), marked early reflections
//   Ballroom big hall: long lines, sparse wide early taps, a broad tail
//   Plate    bright plate: very short lines, heavy input diffusion, no early
//   Mirror   metallic, modulated reflections (the disco shimmer): short
//            regular early taps with feedback-like spacing, strong modulation
//   Space    very large, very modulated, the tail can run (almost) forever
//   Gate     the 80s gated / non-linear room: a dense flat burst of early
//            reflections that stops dead (the late only if DEPTH asks)

#include <array>

namespace ballroom
{
enum class Mode { studio, club, ballroom, plate, mirror, space, gate, count };

struct ModeShape
{
    // Late: the 8 line lengths at LATE SIZE 100 %, samples at 48 kHz (primes)
    std::array<double, 8> lineSamples48k;
    bool hadamard;            // the feedback matrix: Hadamard (denser mixing) or Householder
    double sizeMin;           // the lengths at LATE SIZE 0 %, as a fraction of the above
    double inputDiffusion;    // the late input allpasses' gain at DIFFUSION 1
    int inputAllpasses;       // 2 .. 4 per side
    double lateModScale;      // x LATE MOD DEPTH
    double lowWarmth;         // extra x on BASS MULT (the mode's character), 1 = none
    // Early
    int earlyAllpasses;       // 4 .. 8 in series
    int taps;                 // the multi-tap, per side (0: no early)
    double tapShape;          // tap gains (1 - position)^shape: 0 = flat (gated), 2 = fast fall
    double tapJitter;         // how irregular the tap positions are (0 = even spacing)
    double earlyModScale;     // x EARLY MOD DEPTH
    double earlyGain;         // the early level against the late (the mode's balance)
    // Density and level
    double loopDiffusion;     // the allpass inside each late line's loop, at DIFFUSION 1 (the echo density builds up fast)
    double earlySmear;        // the allpasses after the early taps (a cluster rather than distinct echoes), at DIFFUSION 1
    double earlyTrimDb, lateTrimDb; // the early and the late each made as loud as the dry at MIX 100 %
                                    // (pink noise notes, render.cpp "calibration")
};

inline const ModeShape& modeShape (Mode m)
{
    static const ModeShape shapes[(int) Mode::count] {
        // Studio
        { { 509, 619, 727, 829, 937, 1051, 1163, 1277 }, true, 0.35, 0.70, 4, 0.6, 1.0,
          6, 18, 0.6, 0.6, 0.5, 1.0, 0.55, 0.45, 0.8, -7.7 },
        // Club
        { { 997, 1171, 1327, 1489, 1657, 1811, 1987, 2141 }, true, 0.30, 0.65, 3, 0.8, 1.25,
          5, 12, 1.0, 0.7, 0.7, 1.2, 0.50, 0.35, -1.5, -8.9 },
        // Ballroom
        { { 1493, 1823, 2083, 2389, 2707, 2939, 3259, 3539 }, true, 0.25, 0.60, 4, 1.0, 1.1,
          4, 8, 1.4, 0.9, 0.8, 0.9, 0.65, 0.35, 2.7, -3.6 },
        // Plate
        { { 331, 419, 503, 593, 677, 761, 853, 941 }, true, 0.45, 0.78, 4, 0.7, 0.8,
          4, 0, 1.0, 0.0, 0.5, 0.0, 0.62, 0.0, 0.0, -12.5 },
        // Mirror
        { { 701, 709, 997, 1009, 1301, 1307, 1597, 1601 }, false, 0.30, 0.35, 2, 2.2, 1.0,
          4, 16, 0.4, 0.05, 2.5, 1.1, 0.20, 0.0, 0.2, -13.4 },
        // Space
        { { 2203, 2593, 2999, 3407, 3821, 4243, 4651, 5077 }, true, 0.30, 0.70, 4, 2.0, 1.0,
          6, 10, 1.8, 1.0, 1.5, 0.7, 0.68, 0.40, 4.7, -1.0 },
        // Gate
        { { 409, 487, 563, 661, 743, 827, 919, 1009 }, true, 0.40, 0.70, 3, 0.5, 0.9,
          6, 24, 0.0, 1.0, 0.3, 1.4, 0.50, 0.25, -1.5, -8.9 },
    };
    return shapes[(int) m];
}

inline const char* modeName (Mode m)
{
    static const char* names[] { "Studio", "Club", "Ballroom", "Plate", "Mirror", "Space", "Gate" };
    return names[(int) m];
}
} // namespace ballroom
