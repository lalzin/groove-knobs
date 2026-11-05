#pragma once
// BALLROOM — SPACE, the one knob: the dry / wet, 0 .. 100 % (MIX follows it,
// and SPACE follows MIX). At 100 %: the reverb alone, as loud as the dry was
// (each mode's wet trim, Modes.h). The room itself (mode, decay, sizes, early,
// modulation, colour) is the preset's, every setting of it in ROOM.

#include "Ballroom.h"
#include <algorithm>

namespace ballroom
{
namespace space
{
    inline double mix (double s) { return std::clamp (s, 0.0, 1.0); }

    /** The parameters SPACE sets, on top of `base` (the others kept). */
    inline Params apply (Params base, double s)
    {
        base.mix = mix (s);
        return base;
    }
} // namespace space
} // namespace ballroom
