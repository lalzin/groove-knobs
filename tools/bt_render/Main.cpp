// bt_render — offline renderer / calibration tool for Boogie Trio.
// Commands and options: see tools/gk_render/GkRender.h (shared by the suite).

#include "BoogieTrioProcessor.h"

#define GK_RENDER_PROCESSOR bt::BoogieTrioProcessor
#include "GkRender.h"

gk::render::Spec gk::render::makeSpec()
{
    using namespace gk::testsignals;
    Spec s;
    s.toolName = "bt_render";
    s.defaultSweepBars = 8; // 0 → 100 → 0 %
    s.calibrationSignals = [] (double sr, bool) -> std::array<juce::AudioBuffer<float>, 3>
    {
        // Pink noise + the held Am7 pad; the electric-piano loop for the peak guard
        return { pinkNoise (2, (int) (6.0 * sr), -18.0f), chordPad (sr, 6.0, -3.0f), electricPianoLoop (sr, 124.0, 4) };
    };
    s.sweepLoops = [] (double sr, double bpm, int bars) -> std::vector<std::pair<juce::String, juce::AudioBuffer<float>>>
    {
        return { { "pad", chordPad (sr, bars * 4 * 60.0 / bpm, -3.0f) }, { "keys", electricPianoLoop (sr, bpm, bars) } };
    };
    s.genLoop = [] (double sr, double bpm, int bars, bool keys)
    { return keys ? electricPianoLoop (sr, bpm, bars) : chordPad (sr, bars * 4 * 60.0 / bpm, -3.0f); };
    return s;
}
