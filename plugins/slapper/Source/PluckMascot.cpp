#include "PluckMascot.h"
#include <SlapperArt.h>

namespace slapper
{
PluckMascot::PluckMascot (SlapperProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    showSleepZ = true;  // (the sheet's own "Zzz" was painted out: the code draws it)
    motion = 0.6f;
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

int PluckMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    const auto& s = getState();
    lastTime = s.time;
    if (numFrames() < numPoses)
        return 0;
    if (bypass >= 0.5f)
        return asleep;
    auto& tel = proc.getSlapperDsp().getTelemetry();
    const int hits = tel.hitCounter.load (std::memory_order_relaxed);
    if (hits != lastHits)
    {
        if (lastHits >= 0)
        {
            hitTime = s.time;
            const int kind = tel.lastKind.load (std::memory_order_relaxed);
            const float vel = tel.lastVelocity.load (std::memory_order_relaxed);
            hitFrame = kind == 3 ? ghost : kind == 2 ? pop : kind == 1 ? (vel > 0.85f && macro > 0.75f ? hype : slap) : armsOut;
        }
        lastHits = hits;
    }
    if (s.time - hitTime < 0.18)
        return hitFrame;
    if (! musicOn)
        return macro < 0.25f ? eyesClosed : rest;
    return macro > 0.8f ? laughing : smiling;
}
} // namespace slapper
