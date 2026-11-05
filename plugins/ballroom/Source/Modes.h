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

#include <array>

namespace ballroom
{
enum class Mode { studio, club, ballroom, plate, mirror, space, count };

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
};

inline const ModeShape& modeShape (Mode m)
{
    static const ModeShape shapes[(int) Mode::count] {
        // Studio
        { { 509, 619, 727, 829, 937, 1051, 1163, 1277 }, true, 0.35, 0.70, 4, 0.6, 1.0,
          6, 18, 0.6, 0.6, 0.5, 1.0 },
        // Club
        { { 997, 1171, 1327, 1489, 1657, 1811, 1987, 2141 }, false, 0.30, 0.65, 3, 0.8, 1.25,
          5, 12, 1.0, 0.7, 0.7, 1.2 },
        // Ballroom
        { { 1493, 1823, 2083, 2389, 2707, 2939, 3259, 3539 }, false, 0.25, 0.60, 4, 1.0, 1.1,
          4, 8, 1.4, 0.9, 0.8, 0.9 },
        // Plate
        { { 331, 419, 503, 593, 677, 761, 853, 941 }, true, 0.45, 0.78, 4, 0.7, 0.8,
          4, 0, 1.0, 0.0, 0.5, 0.0 },
        // Mirror
        { { 701, 709, 997, 1009, 1301, 1307, 1597, 1601 }, false, 0.30, 0.35, 2, 2.2, 1.0,
          4, 16, 0.4, 0.05, 2.5, 1.1 },
        // Space
        { { 2203, 2593, 2999, 3407, 3821, 4243, 4651, 5077 }, true, 0.30, 0.70, 4, 2.0, 1.0,
          6, 10, 1.8, 1.0, 1.5, 0.7 },
    };
    return shapes[(int) m];
}

inline const char* modeName (Mode m)
{
    static const char* names[] { "Studio", "Club", "Ballroom", "Plate", "Mirror", "Space" };
    return names[(int) m];
}
} // namespace ballroom
