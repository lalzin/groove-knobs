#include "KikoMascot.h"
#include <HeadSpinArt.h>

namespace hs
{
KikoMascot::KikoMascot (HeadSpinProcessor& p, gk::AudioFeatures& features, const gk::Palette& pal) : MascotComponent (features, pal), proc (p)
{
    setFrames (loadFrames());
    motion = 0.35f;   // a little bounce on the transients, no more
    repaintEvery = 2; // 30 fps
}

std::vector<juce::Image> KikoMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "kiko_" + juce::String (i) + "_png";
        const char* data = HSArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    // 16: the hand on the headphone, mirrored (the sound on the left)
    if (frames.size() > handRight)
    {
        const auto& src = frames[handRight];
        juce::Image flipped (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::Graphics g (flipped);
        g.drawImageTransformed (src, juce::AffineTransform::scale (-1.0f, 1.0f).translated ((float) src.getWidth(), 0.0f));
        frames.push_back (flipped);
    }
    return frames;
}

juce::Image KikoMascot::loadTopView()
{
    int size = 0;
    const char* data = HSArt::getNamedResource ("kiko_top_png", size);
    return data != nullptr ? juce::ImageCache::getFromMemory (data, size) : juce::Image();
}

int KikoMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    if (numFrames() < numPoses)
        return 0;
    const auto& s = getState();
    if (bypass > 0.5f)
        return armsCrossed;
    if (macro < 0.02f || ! musicOn)
        return handsOnHips;
    auto& t = proc.getDsp().getTelemetry();
    const float speed = std::abs (t.turnsPerSecond.load (std::memory_order_relaxed));
    if (speed > 10.0f)
        return std::fmod (s.time * 3.0, 1.0) < 0.5 ? capOffA : capOffB;
    if (speed > 4.0f)
    {
        // into the spin (crouch, a moment), then the two spin poses at a pace
        // that follows the speed
        if (spinStarted < 0.0)
            spinStarted = s.time;
        if (s.time - spinStarted < 0.25)
            return crouch;
        return std::fmod ((s.time - spinStarted) * juce::jmin (6.0f, 0.6f * speed), 1.0) < 0.5 ? spinA : spinB;
    }
    spinStarted = -1.0;
    const float az = t.azimuthDeg.load (std::memory_order_relaxed);
    if (az > 35.0f && az < 145.0f)
        return handRight;
    if (az < -35.0f && az > -145.0f)
        return handLeft;
    if (std::abs (az) >= 145.0f)
        return lookingAside;
    // in front: the groove, on the beat
    return s.beatPhase < 0.5f ? standing : leaning;
}
} // namespace hs
