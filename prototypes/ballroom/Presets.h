#pragma once
// BALLROOM — the 12 presets: a mode, the room (every setting), and SPACE (the
// dry / wet). Generated with the plugin's JSON files (plugins/ballroom/Presets).

#include "SpaceMap.h"
#include <functional>

namespace ballroom
{
struct Preset
{
    const char* name;
    Mode mode;
    double space;
    std::function<void (Params&)> room;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Disco Ballroom", Mode::ballroom, 0.35, [] (Params& p) { p.decaySeconds = 2.6; p.predelayMs = 18; p.highCutHz = 9500; p.depth = 0.62; p.earlySizeMs = 55; p.earlyCross = 0.3; p.earlyModRate = 0.5; p.earlyModDepth = 0.3; p.earlySend = 0.5; p.diffusion = 0.85; p.lateSize = 0.7; p.lateCross = 0.35; p.lateModRate = 0.45; p.lateModDepth = 0.35; p.bassMult = 1.1; p.bassXoverHz = 450; p.highMult = 0.55; p.highXoverHz = 5500; } },
        { "Tight Club", Mode::club, 0.30, [] (Params& p) { p.decaySeconds = 0.55; p.predelayMs = 2; p.highCutHz = 7500; p.depth = 0.3; p.earlySizeMs = 22; p.earlyCross = 0.55; p.earlyModRate = 0.4; p.earlyModDepth = 0.15; p.earlySend = 0.3; p.diffusion = 0.9; p.lateSize = 0.25; p.lateCross = 0.3; p.lateModRate = 0.6; p.lateModDepth = 0.15; p.bassMult = 1.3; p.bassXoverHz = 300; p.highMult = 0.45; p.highXoverHz = 4000; } },
        { "Drum Room", Mode::studio, 0.30, [] (Params& p) { p.decaySeconds = 0.75; p.predelayMs = 0; p.highCutHz = 16000; p.depth = 0.35; p.earlySizeMs = 15; p.earlyCross = 0.35; p.earlyModRate = 0.3; p.earlyModDepth = 0.1; p.earlySend = 0.4; p.diffusion = 0.9; p.lateSize = 0.2; p.lateCross = 0.25; p.lateModRate = 0.5; p.lateModDepth = 0.1; p.bassMult = 0.9; p.bassXoverHz = 500; p.highMult = 1; p.highXoverHz = 8000; } },
        { "Snare Chamber", Mode::studio, 0.32, [] (Params& p) { p.decaySeconds = 1.05; p.predelayMs = 6; p.highCutHz = 11000; p.depth = 0.45; p.earlySizeMs = 34; p.earlyCross = 0.4; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.5; p.diffusion = 0.95; p.lateSize = 0.35; p.lateCross = 0.3; p.lateModRate = 0.5; p.lateModDepth = 0.15; p.bassMult = 0.7; p.bassXoverHz = 600; p.highMult = 0.7; p.highXoverHz = 6000; } },
        { "Gated 80s", Mode::gate, 0.45, [] (Params& p) { p.decaySeconds = 0.35; p.predelayMs = 0; p.highCutHz = 10000; p.depth = 0.05; p.earlySizeMs = 190; p.earlyCross = 0.7; p.earlyModRate = 0.5; p.earlyModDepth = 0.1; p.earlySend = 0; p.diffusion = 1; p.lateSize = 0.3; p.lateCross = 0.3; p.lateModRate = 0.5; p.lateModDepth = 0.1; p.bassMult = 0.8; p.bassXoverHz = 500; p.highMult = 0.6; p.highXoverHz = 5000; } },
        { "Vocal Plate", Mode::plate, 0.28, [] (Params& p) { p.decaySeconds = 1.9; p.predelayMs = 38; p.highCutHz = 13000; p.depth = 0.95; p.earlySizeMs = 20; p.earlyCross = 0.2; p.earlyModRate = 0.4; p.earlyModDepth = 0.15; p.earlySend = 0.2; p.diffusion = 1; p.lateSize = 0.5; p.lateCross = 0.4; p.lateModRate = 0.7; p.lateModDepth = 0.3; p.bassMult = 0.55; p.bassXoverHz = 400; p.highMult = 0.8; p.highXoverHz = 7000; } },
        { "Mirror Ball", Mode::mirror, 0.40, [] (Params& p) { p.decaySeconds = 3.8; p.predelayMs = 12; p.highCutHz = 16000; p.depth = 0.7; p.earlySizeMs = 30; p.earlyCross = 0.6; p.earlyModRate = 2.4; p.earlyModDepth = 0.85; p.earlySend = 0.7; p.diffusion = 0.6; p.lateSize = 0.55; p.lateCross = 0.5; p.lateModRate = 1.8; p.lateModDepth = 0.9; p.bassMult = 0.8; p.bassXoverHz = 500; p.highMult = 0.85; p.highXoverHz = 7000; } },
        { "Warm Hall", Mode::ballroom, 0.35, [] (Params& p) { p.decaySeconds = 4.8; p.predelayMs = 32; p.highCutHz = 4800; p.depth = 0.8; p.earlySizeMs = 90; p.earlyCross = 0.3; p.earlyModRate = 0.3; p.earlyModDepth = 0.25; p.earlySend = 0.6; p.diffusion = 0.8; p.lateSize = 1; p.lateCross = 0.4; p.lateModRate = 0.3; p.lateModDepth = 0.4; p.bassMult = 2; p.bassXoverHz = 350; p.highMult = 0.3; p.highXoverHz = 3000; } },
        { "Cathedral", Mode::ballroom, 0.40, [] (Params& p) { p.decaySeconds = 9; p.predelayMs = 45; p.highCutHz = 5500; p.depth = 0.9; p.earlySizeMs = 160; p.earlyCross = 0.5; p.earlyModRate = 0.25; p.earlyModDepth = 0.3; p.earlySend = 0.7; p.diffusion = 0.9; p.lateSize = 1; p.lateCross = 0.6; p.lateModRate = 0.25; p.lateModDepth = 0.45; p.bassMult = 1.5; p.bassXoverHz = 300; p.highMult = 0.35; p.highXoverHz = 3500; } },
        { "Velvet Pad", Mode::space, 0.45, [] (Params& p) { p.decaySeconds = 7.5; p.predelayMs = 60; p.highCutHz = 6000; p.depth = 1; p.earlySizeMs = 60; p.earlyCross = 0.5; p.earlyModRate = 0.4; p.earlyModDepth = 0.5; p.earlySend = 0.8; p.diffusion = 0.9; p.lateSize = 0.9; p.lateCross = 0.7; p.lateModRate = 0.25; p.lateModDepth = 0.75; p.bassMult = 1.4; p.bassXoverHz = 300; p.highMult = 0.4; p.highXoverHz = 3500; } },
        { "Afterparty", Mode::space, 0.55, [] (Params& p) { p.decaySeconds = 28; p.predelayMs = 90; p.highCutHz = 8000; p.depth = 1; p.earlySizeMs = 80; p.earlyCross = 0.6; p.earlyModRate = 0.5; p.earlyModDepth = 0.5; p.earlySend = 1; p.diffusion = 1; p.lateSize = 1; p.lateCross = 0.8; p.lateModRate = 0.5; p.lateModDepth = 0.85; p.bassMult = 1.2; p.bassXoverHz = 400; p.highMult = 0.6; p.highXoverHz = 4500; } },
        { "Frozen Floor", Mode::space, 0.60, [] (Params& p) { p.decaySeconds = 100; p.predelayMs = 0; p.highCutHz = 7000; p.depth = 1; p.earlySizeMs = 60; p.earlyCross = 0.6; p.earlyModRate = 0.2; p.earlyModDepth = 0.2; p.earlySend = 1; p.diffusion = 1; p.lateSize = 1; p.lateCross = 0.8; p.lateModRate = 0.15; p.lateModDepth = 0.15; p.bassMult = 1; p.bassXoverHz = 400; p.highMult = 1; p.highXoverHz = 6000; } },
    };
    return list;
}

inline Params presetParams (const Preset& pr)
{
    Params p;
    p.mode = pr.mode;
    if (pr.room)
        pr.room (p);
    return space::apply (p, pr.space);
}
} // namespace ballroom
