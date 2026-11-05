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
    // the pose he would take now (wanted), then held: whatever the rotation's
    // speed, a pose stays at least minHold s (the frames never flicker with a
    // fast rotation)
    auto wanted = [&]() -> int
    {
        if (bypass > 0.5f)
            return armsCrossed;
        if (macro < 0.02f || ! musicOn)
            return handsOnHips;
        auto& t = proc.getDsp().getTelemetry();
        const float speed = std::abs (t.turnsPerSecond.load (std::memory_order_relaxed));
        if (speed > 4.0f)
        {
            // the head spin: crouch a moment, then the two spin poses (the cap
            // flies off above 10 turns / s), at a steady pace, not the speed's
            if (spinStarted < 0.0)
                spinStarted = s.time;
            if (s.time - spinStarted < 0.3)
                return crouch;
            const bool a = std::fmod ((s.time - spinStarted) / spinPoseSeconds, 2.0) < 1.0;
            return speed > 10.0f ? (a ? capOffA : capOffB) : (a ? spinA : spinB);
        }
        spinStarted = -1.0;
        if (speed <= followBelowTurns)
        {
            // slow: he follows the sound (a hand on the headphone on its side, looking aside when behind)
            const float az = t.azimuthDeg.load (std::memory_order_relaxed);
            if (az > 35.0f && az < 145.0f)
                return handRight;
            if (az < -35.0f && az > -145.0f)
                return handLeft;
            if (std::abs (az) >= 145.0f)
                return lookingAside;
        }
        // in front, or the sound going round faster than he could follow: the groove, a pose a beat
        return s.beatPhase < 0.5f ? standing : leaning;
    }();
    const bool spinPose = wanted == crouch || wanted == spinA || wanted == spinB || wanted == capOffA || wanted == capOffB;
    const double hold = spinPose ? spinPoseSeconds : minHold;
    if (current < 0 || wanted == armsCrossed || wanted == handsOnHips || s.time - lastChange >= hold * 0.98)
    {
        if (wanted != current)
        {
            current = wanted;
            lastChange = s.time;
        }
    }
    return current;
}
} // namespace hs
