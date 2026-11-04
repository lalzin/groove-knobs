#pragma once
// BALLROOM — the 10 presets (the spec's list): a mode, a SPACE, overrides.
// (The plugin will read the same from JSON files.)

#include "SpaceMap.h"
#include <functional>

namespace ballroom
{
struct Preset
{
    const char* name;
    Mode mode;
    double space;
    std::function<void (Params&)> overrides;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Tight Club", Mode::club, 0.20, [] (Params& p) { p.earlySizeMs = 25; p.highCutHz = 9000; } },
        { "Disco Ballroom", Mode::ballroom, 0.55, [] (Params& p) { p.earlySizeMs = 70; p.highMult = 0.55; } },
        { "Mirror Ball", Mode::mirror, 0.50, [] (Params& p) { p.lateModDepth = 0.75; p.earlyModDepth = 0.7; p.lateModRate = 1.6; p.earlyModRate = 2.2; p.highCutHz = 14000; p.highMult = 0.8; } },
        { "Vocal Plate", Mode::plate, 0.40, [] (Params& p) { p.highMult = 0.7; p.highCutHz = 12000; p.predelayMs = 25; p.bassMult = 0.8; } },
        { "Snare Chamber", Mode::studio, 0.35, [] (Params& p) { p.depth = 0.30; p.earlySizeMs = 45; p.earlyCross = 0.4; } },
        { "Warm Hall", Mode::ballroom, 0.70, [] (Params& p) { p.bassMult = 1.8; p.highCutHz = 5000; p.highMult = 0.4; } },
        { "Gated 80s", Mode::studio, 0.40, [] (Params& p) { p.earlySizeMs = 150; p.depth = 0.0; p.diffusion = 0.9; p.mix = 0.45; } },
        { "Velvet Pad", Mode::space, 0.80, [] (Params& p) { p.lateModDepth = 0.60; p.highMult = 0.45; p.highCutHz = 7000; } },
        { "Afterparty", Mode::space, 0.95, [] (Params& p) { p.decaySeconds = 20.0; p.highMult = 0.5; } },
        { "Frozen Floor", Mode::space, 0.90, [] (Params& p) { p.freeze = true; p.mix = 1.0; } },
    };
    return list;
}

inline Params presetParams (const Preset& pr)
{
    Params p;
    p.mode = pr.mode;
    p = space::apply (p, pr.space);
    if (pr.overrides)
        pr.overrides (p);
    return p;
}
} // namespace ballroom
