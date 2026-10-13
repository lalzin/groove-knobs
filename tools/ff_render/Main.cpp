// ff_render — offline renderer / calibration tool for French Filter.
// Commands and options: see tools/gk_render/GkRender.h (shared by the suite).

#include "FrenchFilterProcessor.h"

#define GK_RENDER_PROCESSOR ff::FrenchFilterProcessor
#include "GkRender.h"

gk::render::Spec gk::render::makeSpec()
{
    using namespace gk::testsignals;
    Spec s;
    s.toolName = "ff_render";
    s.defaultSweepBars = 16; // centre → LP → centre → HP → centre
    s.calibrationSignals = [] (double sr, bool alternative) -> std::array<juce::AudioBuffer<float>, 3>
    {
        // Pink noise + the synthetic drum loop (kick, clap, hats); disco loop for the peak guard
        return { pinkNoise (2, (int) (6.0 * sr), -18.0f), grooveLoop (sr, 124.0, 4, alternative), grooveLoop (sr, 124.0, 4, true) };
    };
    s.sweepLoops = [] (double sr, double bpm, int bars) -> std::vector<std::pair<juce::String, juce::AudioBuffer<float>>>
    {
        return { { "drums", grooveLoop (sr, bpm, bars, false) }, { "disco", grooveLoop (sr, bpm, bars, true) } };
    };
    s.genLoop = [] (double sr, double bpm, int bars, bool music) { return grooveLoop (sr, bpm, bars, music); };
    return s;
}
