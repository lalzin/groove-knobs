#pragma once
// BALLROOM — SPACE, the one knob: what it sets, 0 .. 1 (the spec's ranges).
//   MIX         10 % -> 45 %   (eased: most of the wet arrives early in the travel)
//   DECAY       0.3 s -> 8 s   (log)
//   LATE SIZE   20 % -> 100 %
//   DEPTH       30 % -> 80 %
//   PREDELAY    0 -> 40 ms     (late in the travel: small rooms stay tight)
//   MOD DEPTH   10 % -> 50 %   (late; the early follows at half)
// A preset is a mode, a SPACE and its overrides (applied after the map).

#include "Ballroom.h"
#include <cmath>

namespace ballroom
{
namespace space
{
    inline double mix (double s) { return 0.10 + 0.35 * std::pow (s, 0.8); }
    inline double decay (double s) { return 0.3 * std::pow (8.0 / 0.3, s); }
    inline double lateSize (double s) { return 0.20 + 0.80 * s; }
    inline double depth (double s) { return 0.30 + 0.50 * s; }
    inline double predelayMs (double s) { return 40.0 * s * s; }
    inline double modDepth (double s) { return 0.10 + 0.40 * s; }

    /** The parameters SPACE sets, on top of `base` (the others kept). */
    inline Params apply (Params base, double s)
    {
        s = std::clamp (s, 0.0, 1.0);
        base.mix = mix (s);
        base.decaySeconds = decay (s);
        base.lateSize = lateSize (s);
        base.depth = depth (s);
        base.predelayMs = predelayMs (s);
        base.lateModDepth = modDepth (s);
        base.earlyModDepth = 0.5 * modDepth (s);
        return base;
    }
} // namespace space
} // namespace ballroom
