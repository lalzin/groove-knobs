#include "PluckMascot.h"
#include <SlapperArt.h>

namespace slapper
{
PluckMascot::PluckMascot (SlapperProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal)
{
    setFrames (loadFrames());
    showSleepZ = true;  // (the sheet's own "Zzz" was painted out: the code draws it)
    motion = 0.0f; // no bounce, no nod: only FUNK moves him
    repaintEvery = 2;   // 30 fps
}

std::vector<juce::Image> PluckMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "pluck_" + juce::String (i) + "_png";
        const char* data = SLArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int PluckMascot::chooseFrame (float macro, float bypass, bool)
{
    if (numFrames() < numPoses)
        return 0;
    if (bypass >= 0.5f)
        return asleep;
    // only FUNK moves him (no reaction to the notes, the beat, the level): a
    // ladder from a round, sleepy bass to the full slap, a little hysteresis so
    // he never flickers between two steps
    static constexpr float steps[] { 0.15f, 0.35f, 0.55f, 0.75f, 0.9f };
    constexpr int numSteps = 5;
    const float f = juce::jlimit (0.0f, 1.0f, macro);
    while (rung < numSteps && f > steps[rung] + 0.02f)
        ++rung;
    while (rung > 0 && f < steps[rung - 1] - 0.02f)
        --rung;
    static constexpr int poses[] { eyesClosed, rest, smiling, armsOut, laughing, hype };
    return poses[rung];
}
} // namespace slapper
