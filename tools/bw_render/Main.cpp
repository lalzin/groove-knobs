// bw_render — offline renderer for Backwash (listening renders).
// Commands and options: see tools/gk_render/GkRender.h (shared by the suite).

#include "BackwashProcessor.h"

#define GK_RENDER_PROCESSOR bw::BackwashProcessor
#include "GkRender.h"

gk::render::Spec gk::render::makeSpec()
{
    using namespace gk::testsignals;
    Spec s;
    s.toolName = "bw_render";
    s.defaultSweepBars = 8;
    s.sweepAmount = [] (double) { return 1.0f; }; // listening renders: each preset at 100 %
    s.calibrationSignals = [] (double sr, bool) -> std::array<juce::AudioBuffer<float>, 3>
    { return { pinkNoise (2, (int) (6.0 * sr), -18.0f), backbeatLoop (sr, 124.0, 4), vocalPhrase (sr, 124.0, 4) }; };
    s.sweepLoops = [] (double sr, double bpm, int bars) -> std::vector<std::pair<juce::String, juce::AudioBuffer<float>>>
    { return { { "drums", backbeatLoop (sr, bpm, bars) }, { "vocal", vocalPhrase (sr, bpm, bars) } }; };
    s.genLoop = [] (double sr, double bpm, int bars, bool vocal) { return vocal ? vocalPhrase (sr, bpm, bars) : backbeatLoop (sr, bpm, bars); };
    return s;
}
