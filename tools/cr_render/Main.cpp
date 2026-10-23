// cr_render — offline renderer / calibration tool for Crackle.
// Commands and options: see tools/gk_render/GkRender.h (shared by the suite).
//   cr_render calibrate plugins/crackle/Presets/*.json   (the makeup tables)

#include "CrackleProcessor.h"

#define GK_RENDER_PROCESSOR crackle::CrackleProcessor
#include "GkRender.h"

gk::render::Spec gk::render::makeSpec()
{
    using namespace gk::testsignals;
    Spec s;
    s.toolName = "cr_render";
    s.defaultSweepBars = 8;
    // music only (no pink noise): the band and the radio take the lows away, which
    // pink noise has little of and a mix a lot — calibrated on noise, a mix came
    // out 5-10 dB under; a lo-fi effect is judged on music
    s.calibrationSignals = [] (double sr, bool) -> std::array<juce::AudioBuffer<float>, 3>
    { return { electricPianoLoop (sr, 124.0, 4), grooveLoop (sr, 124.0, 4, true), grooveLoop (sr, 124.0, 4, true) }; };
    s.sweepLoops = [] (double sr, double bpm, int bars) -> std::vector<std::pair<juce::String, juce::AudioBuffer<float>>>
    { return { { "groove", grooveLoop (sr, bpm, bars, true) }, { "keys", electricPianoLoop (sr, bpm, bars) } }; };
    s.genLoop = [] (double sr, double bpm, int bars, bool keys) { return keys ? electricPianoLoop (sr, bpm, bars) : grooveLoop (sr, bpm, bars, true); };
    return s;
}
