#pragma once
// BALLROOM — the 22 presets: a mode, the room (every setting), and SPACE (the
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
        { "Disco Ballroom", Mode::ballroom, 0.35, [] (Params& p) { p.decaySeconds = 2.6; p.predelayMs = 18; p.highCutHz = 9500; p.depth = 0.62; p.earlySizeMs = 55; p.earlyCross = 0.3; p.earlyModRate = 0.5; p.earlyModDepth = 0.3; p.earlySend = 0.5; p.diffusion = 0.85; p.lateSize = 0.7; p.lateCross = 0.35; p.lateModRate = 0.45; p.lateModDepth = 0.35; p.bassMult = 1.1; p.bassXoverHz = 450; p.highMult = 0.55; p.highXoverHz = 5500; p.clear = 0.2; } },
        { "Tight Club", Mode::club, 0.30, [] (Params& p) { p.decaySeconds = 0.55; p.predelayMs = 2; p.highCutHz = 7500; p.depth = 0.3; p.earlySizeMs = 22; p.earlyCross = 0.55; p.earlyModRate = 0.4; p.earlyModDepth = 0.15; p.earlySend = 0.3; p.diffusion = 0.9; p.lateSize = 0.25; p.lateCross = 0.3; p.lateModRate = 0.6; p.lateModDepth = 0.15; p.bassMult = 1.3; p.bassXoverHz = 300; p.highMult = 0.45; p.highXoverHz = 4000; } },
        { "Drum Room", Mode::studio, 0.30, [] (Params& p) { p.decaySeconds = 0.75; p.predelayMs = 0; p.highCutHz = 16000; p.depth = 0.35; p.earlySizeMs = 15; p.earlyCross = 0.35; p.earlyModRate = 0.3; p.earlyModDepth = 0.1; p.earlySend = 0.4; p.diffusion = 0.9; p.lateSize = 0.2; p.lateCross = 0.25; p.lateModRate = 0.5; p.lateModDepth = 0.1; p.bassMult = 0.9; p.bassXoverHz = 500; p.highMult = 1; p.highXoverHz = 8000; } },
        { "Snare Chamber", Mode::studio, 0.32, [] (Params& p) { p.decaySeconds = 1.05; p.predelayMs = 6; p.highCutHz = 11000; p.depth = 0.45; p.earlySizeMs = 34; p.earlyCross = 0.4; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.5; p.diffusion = 0.95; p.lateSize = 0.35; p.lateCross = 0.3; p.lateModRate = 0.5; p.lateModDepth = 0.15; p.bassMult = 0.7; p.bassXoverHz = 600; p.highMult = 0.7; p.highXoverHz = 6000; } },
        { "Gated 80s", Mode::gate, 0.45, [] (Params& p) { p.decaySeconds = 0.35; p.predelayMs = 0; p.highCutHz = 10000; p.depth = 0.05; p.earlySizeMs = 190; p.earlyCross = 0.7; p.earlyModRate = 0.5; p.earlyModDepth = 0.1; p.earlySend = 0; p.diffusion = 1; p.lateSize = 0.3; p.lateCross = 0.3; p.lateModRate = 0.5; p.lateModDepth = 0.1; p.bassMult = 0.8; p.bassXoverHz = 500; p.highMult = 0.6; p.highXoverHz = 5000; } },
        { "Vocal Plate", Mode::plate, 0.28, [] (Params& p) { p.decaySeconds = 1.9; p.predelayMs = 38; p.highCutHz = 13000; p.depth = 0.95; p.earlySizeMs = 20; p.earlyCross = 0.2; p.earlyModRate = 0.4; p.earlyModDepth = 0.15; p.earlySend = 0.2; p.diffusion = 1; p.lateSize = 0.5; p.lateCross = 0.4; p.lateModRate = 0.7; p.lateModDepth = 0.3; p.bassMult = 0.55; p.bassXoverHz = 400; p.highMult = 0.8; p.highXoverHz = 7000; p.clear = 0.5; } },
        { "Mirror Ball", Mode::mirror, 0.40, [] (Params& p) { p.decaySeconds = 3.8; p.predelayMs = 12; p.highCutHz = 16000; p.depth = 0.7; p.earlySizeMs = 30; p.earlyCross = 0.6; p.earlyModRate = 2.4; p.earlyModDepth = 0.85; p.earlySend = 0.7; p.diffusion = 0.6; p.lateSize = 0.55; p.lateCross = 0.5; p.lateModRate = 1.8; p.lateModDepth = 0.9; p.bassMult = 0.8; p.bassXoverHz = 500; p.highMult = 0.85; p.highXoverHz = 7000; p.clear = 0.25; } },
        { "Warm Hall", Mode::ballroom, 0.35, [] (Params& p) { p.decaySeconds = 4.8; p.predelayMs = 32; p.highCutHz = 4800; p.depth = 0.8; p.earlySizeMs = 90; p.earlyCross = 0.3; p.earlyModRate = 0.3; p.earlyModDepth = 0.25; p.earlySend = 0.6; p.diffusion = 0.8; p.lateSize = 1; p.lateCross = 0.4; p.lateModRate = 0.3; p.lateModDepth = 0.4; p.bassMult = 2; p.bassXoverHz = 350; p.highMult = 0.3; p.highXoverHz = 3000; } },
        { "Cathedral", Mode::ballroom, 0.40, [] (Params& p) { p.decaySeconds = 9; p.predelayMs = 45; p.highCutHz = 5500; p.depth = 0.9; p.earlySizeMs = 160; p.earlyCross = 0.5; p.earlyModRate = 0.25; p.earlyModDepth = 0.3; p.earlySend = 0.7; p.diffusion = 0.9; p.lateSize = 1; p.lateCross = 0.6; p.lateModRate = 0.25; p.lateModDepth = 0.45; p.bassMult = 1.5; p.bassXoverHz = 300; p.highMult = 0.35; p.highXoverHz = 3500; p.clear = 0.3; } },
        { "Velvet Pad", Mode::space, 0.45, [] (Params& p) { p.decaySeconds = 7.5; p.predelayMs = 60; p.highCutHz = 6000; p.depth = 1; p.earlySizeMs = 60; p.earlyCross = 0.5; p.earlyModRate = 0.4; p.earlyModDepth = 0.5; p.earlySend = 0.8; p.diffusion = 0.9; p.lateSize = 0.9; p.lateCross = 0.7; p.lateModRate = 0.25; p.lateModDepth = 0.75; p.bassMult = 1.4; p.bassXoverHz = 300; p.highMult = 0.4; p.highXoverHz = 3500; p.clear = 0.35; } },
        { "Afterparty", Mode::space, 0.55, [] (Params& p) { p.decaySeconds = 28; p.predelayMs = 90; p.highCutHz = 8000; p.depth = 1; p.earlySizeMs = 80; p.earlyCross = 0.6; p.earlyModRate = 0.5; p.earlyModDepth = 0.5; p.earlySend = 1; p.diffusion = 1; p.lateSize = 1; p.lateCross = 0.8; p.lateModRate = 0.5; p.lateModDepth = 0.85; p.bassMult = 1.2; p.bassXoverHz = 400; p.highMult = 0.6; p.highXoverHz = 4500; p.clear = 0.45; } },
        { "Frozen Floor", Mode::space, 0.60, [] (Params& p) { p.decaySeconds = 100; p.predelayMs = 0; p.highCutHz = 7000; p.depth = 1; p.earlySizeMs = 60; p.earlyCross = 0.6; p.earlyModRate = 0.2; p.earlyModDepth = 0.2; p.earlySend = 1; p.diffusion = 1; p.lateSize = 1; p.lateCross = 0.8; p.lateModRate = 0.15; p.lateModDepth = 0.15; p.bassMult = 1; p.bassXoverHz = 400; p.highMult = 1; p.highXoverHz = 6000; p.clear = 0.5; } },
        { "Clear Vocal", Mode::ballroom, 0.35, [] (Params& p) { p.predelayMs = 40; p.highCutHz = 11000; p.depth = 0.85; p.earlySizeMs = 45; p.earlyCross = 0.3; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.5; p.diffusion = 0.85; p.lateSize = 0.75; p.lateCross = 0.4; p.lateModRate = 0.5; p.lateModDepth = 0.35; p.bassMult = 0.6; p.bassXoverHz = 350; p.highMult = 0.7; p.highXoverHz = 6000; p.clear = 0.8; p.decaySeconds = 3.2; } },
        { "Power Snare", Mode::gate, 0.40, [] (Params& p) { p.predelayMs = 0; p.highCutHz = 14000; p.depth = 0.25; p.earlySizeMs = 120; p.earlyCross = 0.8; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.1; p.diffusion = 1; p.lateSize = 0.15; p.lateCross = 0.4; p.lateModRate = 0.5; p.lateModDepth = 0.3; p.bassMult = 1; p.bassXoverHz = 450; p.highMult = 0.8; p.highXoverHz = 5000; p.clear = 0; p.decaySeconds = 0.25; } },
        { "Disco Strings", Mode::space, 0.40, [] (Params& p) { p.predelayMs = 30; p.highCutHz = 9000; p.depth = 0.95; p.earlySizeMs = 70; p.earlyCross = 0.3; p.earlyModRate = 0.4; p.earlyModDepth = 0.6; p.earlySend = 0.8; p.diffusion = 0.85; p.lateSize = 0.85; p.lateCross = 0.75; p.lateModRate = 0.8; p.lateModDepth = 0.8; p.bassMult = 0.8; p.bassXoverHz = 450; p.highMult = 0.6; p.highXoverHz = 5000; p.clear = 0.6; p.decaySeconds = 5; } },
        { "Funk Room", Mode::club, 0.30, [] (Params& p) { p.predelayMs = 4; p.highCutHz = 9000; p.depth = 0.35; p.earlySizeMs = 35; p.earlyCross = 0.5; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.4; p.diffusion = 0.95; p.lateSize = 0.3; p.lateCross = 0.4; p.lateModRate = 0.5; p.lateModDepth = 0.2; p.bassMult = 0.9; p.bassXoverHz = 300; p.highMult = 0.6; p.highXoverHz = 5000; p.clear = 0.3; p.decaySeconds = 0.8; } },
        { "Glitter Plate", Mode::plate, 0.30, [] (Params& p) { p.predelayMs = 20; p.highCutHz = 18000; p.depth = 0.9; p.earlySizeMs = 40; p.earlyCross = 0.3; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.5; p.diffusion = 1; p.lateSize = 0.6; p.lateCross = 0.6; p.lateModRate = 1.2; p.lateModDepth = 0.6; p.bassMult = 0.5; p.bassXoverHz = 500; p.highMult = 1; p.highXoverHz = 9000; p.clear = 0.4; p.decaySeconds = 2.6; } },
        { "Dub Chamber", Mode::club, 0.40, [] (Params& p) { p.predelayMs = 120; p.highCutHz = 3000; p.depth = 0.9; p.earlySizeMs = 10; p.earlyCross = 0.3; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.8; p.diffusion = 0.7; p.lateSize = 0.7; p.lateCross = 0.4; p.lateModRate = 0.3; p.lateModDepth = 0.45; p.bassMult = 2.5; p.bassXoverHz = 250; p.highMult = 0.2; p.highXoverHz = 2000; p.clear = 0.7; p.decaySeconds = 6; } },
        { "Slapback Club", Mode::studio, 0.30, [] (Params& p) { p.predelayMs = 0; p.highCutHz = 7000; p.depth = 0; p.earlySizeMs = 110; p.earlyCross = 0.2; p.earlyModRate = 0.4; p.earlyModDepth = 0.1; p.earlySend = 0; p.diffusion = 0.3; p.lateSize = 0.2; p.lateCross = 0.4; p.lateModRate = 0.5; p.lateModDepth = 0.3; p.bassMult = 0.8; p.bassXoverHz = 450; p.highMult = 0.5; p.highXoverHz = 5000; p.clear = 0.2; p.decaySeconds = 0.4; } },
        { "Chrome Room", Mode::mirror, 0.35, [] (Params& p) { p.predelayMs = 5; p.highCutHz = 15000; p.depth = 0.6; p.earlySizeMs = 18; p.earlyCross = 0.7; p.earlyModRate = 3; p.earlyModDepth = 0.5; p.earlySend = 0.6; p.diffusion = 0.5; p.lateSize = 0.3; p.lateCross = 0.4; p.lateModRate = 3; p.lateModDepth = 0.5; p.bassMult = 0.6; p.bassXoverHz = 450; p.highMult = 0.9; p.highXoverHz = 7000; p.clear = 0.2; p.decaySeconds = 1.2; } },
        { "Breathing Sky", Mode::space, 0.50, [] (Params& p) { p.predelayMs = 50; p.highCutHz = 10000; p.depth = 1; p.earlySizeMs = 90; p.earlyCross = 0.3; p.earlyModRate = 0.4; p.earlyModDepth = 0.4; p.earlySend = 1; p.diffusion = 1; p.lateSize = 1; p.lateCross = 0.8; p.lateModRate = 0.35; p.lateModDepth = 0.6; p.bassMult = 0.7; p.bassXoverHz = 300; p.highMult = 0.8; p.highXoverHz = 4500; p.clear = 1; p.decaySeconds = 12; } },
        { "Tiny Booth", Mode::studio, 0.30, [] (Params& p) { p.predelayMs = 0; p.highCutHz = 12000; p.depth = 0.4; p.earlySizeMs = 5; p.earlyCross = 0.5; p.earlyModRate = 0.4; p.earlyModDepth = 0.2; p.earlySend = 0.5; p.diffusion = 0.9; p.lateSize = 0; p.lateCross = 0.4; p.lateModRate = 0.5; p.lateModDepth = 0.05; p.bassMult = 0.7; p.bassXoverHz = 600; p.highMult = 0.8; p.highXoverHz = 6000; p.clear = 0; p.decaySeconds = 0.25; } },
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
