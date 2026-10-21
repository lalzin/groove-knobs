#include "PhazeMascot.h"
#include <KickLockArt.h>

namespace kl
{
PhazeMascot::PhazeMascot (KickLockProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    showSleepZ = false; // the sleeping pose has its own "Zzz"
    motion = 0.25f;     // the poses carry the moves (Alan: the animation took too much room)
    repaintEvery = 2;   // 30 fps
}

std::vector<juce::Image> PhazeMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "phaze_" + juce::String (i) + "_png";
        const char* data = KLArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int PhazeMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    juce::ignoreUnused (macro, musicOn);
    const auto& s = getState();
    const double dt = juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    const bool snapshot = dt <= 0.0;
    auto& tel = proc.getDsp().getTelemetry();

    // kicks: each one is a step of the dance (and a dip, with the ducking)
    const int events = previewStatus >= 0 ? previewKicks : tel.kickEvents.load (std::memory_order_relaxed);
    if (events != lastKickEvents)
    {
        if (lastKickEvents >= 0 && ! snapshot)
        {
            kickTime = s.time;
            ++kicksInMood;
        }
        lastKickEvents = events;
    }
    const float duck = previewStatus >= 0 ? 0.0f : 1.0f - tel.duckGain.load (std::memory_order_relaxed);
    const float target = bypass < 0.5f ? juce::jlimit (0.0f, 1.0f, duck) : 0.0f;
    dip = snapshot ? target : dip + (target - dip) * (target > dip ? 0.6f : 1.0f - std::exp (-(float) dt / 0.08f));

    if (numFrames() < numPoses)
        return 0;
    const int status = previewStatus >= 0 ? previewStatus : tel.status.load (std::memory_order_relaxed);
    int m = (int) Status::noKick;
    if (bypass < 0.5f)
        m = status;
    if (m != mood)
    {
        mood = m;
        moodSince = s.time;
        kicksInMood = previewStatus >= 0 ? previewKicks : 0;
    }
    const double inMood = s.time - moodSince;
    auto cycle = [&] (double seconds) { return snapshot ? 0 : (int) std::floor (inMood / seconds); };
    switch ((Status) mood)
    {
        case Status::noKick: return asleep;
        case Status::unstable:
        case Status::monoLoss: return worriedA + cycle (0.35) % 4;
        case Status::shifted: return puzzledA + cycle (0.6) % 4;
        case Status::weakSub:
        {
            if (s.time - kickTime < 0.18)
                return calmJump;
            static const int calm[] { calmA, calmA + 1, calmA + 3, calmA + 1 };
            return calm[cycle (2.0) % 4];
        }
        case Status::ok:
        default:
            // a foot tap per kick; every fourth kick, a cheer (held ~a beat)
            if (kicksInMood % 4 == 3 && (snapshot || s.time - kickTime < 0.45))
                return happyA + (kicksInMood / 4) % 4;
            return tapA + kicksInMood % 4;
    }
}

void PhazeMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> figure, const juce::AffineTransform& animation)
{
    // the ducking: down (and a little squashed) with the gain, at the feet
    const float d = juce::jlimit (0.0f, 1.0f, dip);
    const juce::Point<float> feet (figure.getCentreX(), figure.getBottom());
    const auto t = animation.followedBy (juce::AffineTransform::translation (-feet.x, -feet.y)
                                             .scaled (1.0f + 0.02f * d, 1.0f - 0.04f * d)
                                             .translated (feet.x, feet.y + 2.0f * d));
    drawFrame (g, getState().frame, figure, t, 1.0f);
}
} // namespace kl
