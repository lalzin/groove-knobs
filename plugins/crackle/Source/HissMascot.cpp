#include "HissMascot.h"
#include <CrackleArt.h>

namespace crackle
{
HissMascot::HissMascot (CrackleProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    motion = 0.6f;
    repaintEvery = 2; // 30 fps
}

std::vector<juce::Image> HissMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "hiss_" + juce::String (i) + "_png";
        const char* data = CRArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int HissMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    const auto& s = getState();
    if (numFrames() < numPoses)
        return 0;
    if (bypass >= 0.5f)
        return eyesClosed;
    auto& tel = proc.getCrackleDsp().getTelemetry();
    const int crackles = tel.crackles.load (std::memory_order_relaxed), dropouts = tel.dropouts.load (std::memory_order_relaxed);
    if (dropouts != lastDropouts)
    {
        if (lastDropouts >= 0 && dropouts > lastDropouts)
            droppedAt = s.time;
        lastDropouts = dropouts;
    }
    if (crackles != lastCrackles)
    {
        // (a startle now and then, not on every one of 30 crackles a second)
        if (lastCrackles >= 0 && crackles > lastCrackles && s.time - startledAt > 1.5)
            startledAt = s.time;
        lastCrackles = crackles;
    }
    if (s.time - droppedAt < 0.25)
        return eyesClosed;
    if (s.time - startledAt < 0.2)
        return startled;
    if (tel.crush.load (std::memory_order_relaxed) > 0.3f)
        return ((int) (s.time * 2.0) & 1) ? squint : squintOther;
    if (! musicOn)
        return macro > 0.8f ? armsCrossed : rest;
    // with the music: a foot tap every half second, cheerful when GRAIN is high
    return ((int) (s.time * 2.0) & 1) ? footUp : (macro > 0.7f ? laughing : cheerful);
}
} // namespace crackle
