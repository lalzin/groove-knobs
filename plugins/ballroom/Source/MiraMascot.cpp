#include "MiraMascot.h"
#include <BallroomArt.h>

namespace bl
{
namespace
{
    const juce::Colour silver (0xffc9ced6), champagne (0xffe8d8b0), white (0xffffffff);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    float hash (int n) // 0..1, fixed
    {
        auto x = (std::uint32_t) n * 2654435761u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        return (float) (x & 0xffffu) / 65535.0f;
    }
} // namespace

MiraMascot::MiraMascot (BallroomProcessor& p, gk::AudioFeatures& features, const gk::Palette& pal) : MascotComponent (features, pal), proc (p)
{
    setFrames (loadFrames());
    motion = 0.3f;    // a little bounce, no more: the poses do the dancing
    repaintEvery = 2; // 30 fps
}

std::vector<juce::Image> MiraMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "mira_" + juce::String (i) + "_png";
        const char* data = BLArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int MiraMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    const auto& s = getState();
    const double dt = lastTime < 0.0 ? 0.0 : juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    space = juce::jlimit (0.0f, 1.0f, macro);
    frozen = proc.getDsp().getTelemetry().frozen.load (std::memory_order_relaxed);
    const float lv = proc.getDsp().getTelemetry().wetLevel.load (std::memory_order_relaxed);
    level += (juce::jlimit (0.0f, 1.0f, (gk::dspmath::gainToDb (lv) + 48.0f) / 40.0f) - level) * (float) juce::jmin (1.0, dt / 0.12);
    // the light spots turn (not when frozen)
    if (! frozen && bypass < 0.5f)
    {
        spin += dt * (0.25 + 0.9 * space);
        sparkleClock += dt;
    }
    if (numFrames() < numPoses)
        return 0;
    if (bypass > 0.5f)
        return restHands;
    if (frozen)
        return handsUpSmallOh;
    // SPACE's ladder; with the music playing she changes pose on the beat
    static const int calm[] { standing, kneesBent, standing2, handOnHip };
    static const int open[] { armsOpen, shrug, armsWide, armsOpen };
    static const int disco[] { point, hero, pointKick, kick };
    static const int high[] { armsUpHigh, armsUp, armsUpLaugh, armsOutUp };
    const int* set = space < 0.25f ? calm : space < 0.5f ? open : space < 0.75f ? disco : high;
    if (! (musicOn && s.playing))
        return set[0];
    const int beat = (int) std::floor (getFeatures().beats.load (std::memory_order_relaxed));
    if (space > 0.9f && beat % 8 == 7)
        return beat % 16 == 7 ? turnedOh : turnedSurprised; // a turn now and then, at the top
    return set[beat & 3];
}

void MiraMascot::paintBehind (juce::Graphics& g, const State& st, juce::Rectangle<float> figure)
{
    // light spots turning round her, more and wider as SPACE goes up
    const float dim = 1.0f - 0.7f * st.bypass;
    const int count = 4 + (int) std::round (14.0f * space);
    const auto c = figure.getCentre().translated (0.0f, -0.08f * figure.getHeight());
    for (int i = 0; i < count; ++i)
    {
        const float a = (float) spin * (i % 2 == 0 ? 1.0f : 0.7f) + twoPi * hash (i);
        const float r = figure.getWidth() * (0.42f + 0.22f * hash (i + 97)) * (0.8f + 0.35f * space);
        const juce::Point<float> p (c.x + r * std::cos (a), c.y + 0.55f * r * std::sin (a));
        const float sz = 3.0f + 5.0f * hash (i + 31) * (0.5f + space);
        const float alpha = (0.35f + 0.45f * hash (i + 53)) * dim * (0.5f + 0.5f * space);
        const auto col = palette (i % 3 == 0 ? champagne : white);
        // a soft halo, then the spot: light thrown by the ball
        g.setColour (col.withAlpha (0.25f * alpha));
        g.fillEllipse (juce::Rectangle<float> (sz * 3.4f, sz * 2.2f).withCentre (p));
        g.setColour (col.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (sz * 1.6f, sz).withCentre (p));
    }
}

void MiraMascot::paintOverlay (juce::Graphics& g, const State& st, juce::Rectangle<float> figure)
{
    // her facets sparkle with the reverb's level (still when frozen)
    if (st.bypass > 0.5f)
        return;
    const auto ball = juce::Rectangle<float> (figure.getWidth() * 0.46f, figure.getHeight() * 0.40f)
                          .withCentre ({ figure.getCentreX(), figure.getY() + 0.40f * figure.getHeight() });
    const int count = (int) std::round (2.0f + 10.0f * level);
    const double t = sparkleClock * 6.0;
    for (int i = 0; i < count; ++i)
    {
        const int slot = i * 7 + (int) std::floor (t + hash (i)); // each sparkle moves on after a while
        const float life = (float) (t + hash (i) - std::floor (t + hash (i)));
        const float glow = std::sin (life * juce::MathConstants<float>::pi) * (0.35f + 0.65f * level);
        const juce::Point<float> p (ball.getX() + ball.getWidth() * hash (slot), ball.getY() + ball.getHeight() * hash (slot + 1000));
        if (std::hypot ((p.x - ball.getCentreX()) / ball.getWidth(), (p.y - ball.getCentreY()) / ball.getHeight()) > 0.5f)
            continue;
        const float r = 2.0f + 5.0f * glow;
        juce::Path star;
        star.startNewSubPath (p.x, p.y - r);
        star.quadraticTo (p, { p.x + r, p.y });
        star.quadraticTo (p, { p.x, p.y + r });
        star.quadraticTo (p, { p.x - r, p.y });
        star.quadraticTo (p, { p.x, p.y - r });
        g.setColour (palette (white).withAlpha (juce::jlimit (0.0f, 1.0f, glow)));
        g.fillPath (star);
        g.setColour (palette (silver).withAlpha (0.4f * glow));
        g.fillEllipse (juce::Rectangle<float> (r * 0.8f, r * 0.8f).withCentre (p));
    }
}
} // namespace bl
