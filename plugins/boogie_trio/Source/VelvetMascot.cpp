#include "VelvetMascot.h"
#include <BoogieTrioArt.h>

namespace bt
{
using gk::dspmath::smoothstep;

namespace
{
    /** Warm, slightly grainy "old photo" version of a frame (VINTAGE clones). */
    juce::Image makeSepia (const juce::Image& src, std::uint32_t seed)
    {
        auto img = src.createCopy();
        juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
        gk::testsignals::Rng rng (seed);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto c = d.getPixelColour (x, y);
                if (c.getAlpha() == 0)
                    continue;
                const float lum = 0.3f * c.getFloatRed() + 0.59f * c.getFloatGreen() + 0.11f * c.getFloatBlue();
                const float grain = 0.06f * rng.bipolar();
                auto tone = [&] (float k, float add, float orig) { return juce::jlimit (0.0f, 1.0f, 0.35f * orig + 0.65f * (lum * k + add) + grain); };
                d.setPixelColour (x, y, juce::Colour::fromFloatRGBA (tone (1.07f, 0.08f, c.getFloatRed()), tone (0.88f, 0.04f, c.getFloatGreen()),
                                                                    tone (0.62f, 0.0f, c.getFloatBlue()), c.getFloatAlpha()));
            }
        return img;
    }
} // namespace

VelvetMascot::VelvetMascot (BoogieTrioProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    showSleepZ = false; // the sleeping poses have their own "Z"
    for (int i = 0; i < numFrames(); ++i)
        sepiaFrames.push_back (makeSepia (getColourFrame (i), 11u + (std::uint32_t) i));

    // Four-point twinkle star, unit size, centred
    auto& star = sparkle[0];
    star.startNewSubPath (0.0f, -1.0f);
    star.quadraticTo (0.12f, -0.12f, 1.0f, 0.0f);
    star.quadraticTo (0.12f, 0.12f, 0.0f, 1.0f);
    star.quadraticTo (-0.12f, 0.12f, -1.0f, 0.0f);
    star.quadraticTo (-0.12f, -0.12f, 0.0f, -1.0f);
    star.closeSubPath();
}

std::vector<juce::Image> VelvetMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "velvet_" + juce::String (i) + "_png";
        const char* data = BTArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int VelvetMascot::chooseFrame (float amount, float bypass, bool)
{
    const auto& s = getState();
    const double dt = juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    wideSmoothed += ((proc.getWideParameter().get() ? 1.0f : 0.0f) - wideSmoothed) * (1.0f - std::exp (-(float) dt / 0.12f));

    if (numFrames() <= sleepFirst + 3)
        return 0;

    // Bypass: falls asleep on the violin, then the "Z" come and go
    if (bypass >= 0.5f)
    {
        if (! asleep)
        {
            asleep = true;
            sleepStart = s.time;
        }
        const double e = s.time - sleepStart;
        if (e < 0.5)
            return sleepFirst;
        if (e < 1.0)
            return sleepFirst + 1;
        return sleepFirst + 2 + (int) std::fmod (std::floor ((e - 1.0) / 0.9), 2.0);
    }
    asleep = false;

    // At rest: waiting, eyes closed, with a glance now and then (2-6 s apart)
    if (amount < 0.12f)
    {
        if (s.time > nextGlance)
        {
            glanceUntil = s.time + 1.2;
            nextGlance = s.time + 3.0 + 4.0 * random.nextDouble();
        }
        return s.time < glanceUntil ? glance : waiting;
    }
    if (amount < 0.25f)
        return ready;

    // Playing: the bow moves faster when the music is louder
    const double rate = (1.5 + 6.0 * musicLevel()) * (0.6 + 0.8 * amount);
    bowPosition += dt * rate;
    playFrame = playFirst + (int) std::fmod (std::floor (bowPosition), (double) playCount);
    return playFrame;
}

void VelvetMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation)
{
    const auto& s = getState();
    const float a = s.amount, awake = 1.0f - s.bypass;
    const float lfo = getFeatures().modulationPhase.load (std::memory_order_relaxed);
    const float sway = 0.07f * (0.25f + 0.75f * smoothstep (0.25f, 1.0f, a)) * awake;
    const float vib = smoothstep (0.5f, 1.0f, a) * awake;
    const float tremble = 0.9f * vib * (float) std::sin (2.0 * gk::dspmath::pi * proc.getBoogieTrioDsp().getVibratoPhase());
    const bool playing = s.frame >= playFirst && s.frame < playFirst + playCount;

    // Voice k sways on the chorus LFO at k × 120° (left clone 0°, Velvet 120°, right clone 240°)
    auto swayFor = [&] (int k) { return sway * (float) std::sin (2.0 * gk::dspmath::pi * (lfo + k / 3.0)); };

    // Clones: appear from ~45 %, about 40 % opacity at 50 %, nearly opaque from 75 %
    const float cloneOpacity = (0.4f * smoothstep (0.42f, 0.5f, a) + 0.52f * smoothstep (0.5f, 0.78f, a)) * awake;
    if (cloneOpacity > 0.005f)
    {
        const float spread = fig.getWidth() * gk::dspmath::lerp (0.48f, 0.66f, wideSmoothed);
        const bool vintage = proc.getVintageParameter().get();
        for (int side : { -1, 1 })
        {
            const int k = side < 0 ? 0 : 2;
            const juce::Point<float> feet (fig.getCentreX() + side * spread, fig.getBottom() - 10.0f);
            const auto cloneFig = fig.withSizeKeepingCentre (fig.getWidth() * 0.8f, fig.getHeight() * 0.8f)
                                      .withBottomY (feet.y)
                                      .withCentre ({ feet.x, feet.y - fig.getHeight() * 0.4f });
            const int frame = playing ? playFirst + (playFrame - playFirst + k) % playCount : s.frame;
            const auto t = juce::AffineTransform::rotation (swayFor (k), feet.x, feet.y).translated (tremble, s.nod * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.22f * cloneOpacity));
            g.fillEllipse (juce::Rectangle<float> (cloneFig.getWidth() * 0.55f, 7.0f).withCentre ({ feet.x, feet.y - 2.0f }));
            drawFrame (g, frame, cloneFig, t, cloneOpacity, vintage ? &sepiaFrames : nullptr);
        }
    }

    // Velvet, in front
    const juce::Point<float> feet (fig.getCentreX(), fig.getBottom());
    drawFrame (g, s.frame, fig, animation.followedBy (juce::AffineTransform::rotation (swayFor (1), feet.x, feet.y).translated (tremble, 0.0f)), 1.0f);
}

void VelvetMascot::paintOverlay (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    // Full trio: sparkles twinkling around the three of them
    const float show = smoothstep (0.88f, 1.0f, s.amount) * (1.0f - s.bypass);
    if (show <= 0.0f)
        return;
    const float spread = fig.getWidth() * gk::dspmath::lerp (0.48f, 0.66f, wideSmoothed);
    const juce::Point<float> spots[] { { -1.25f, 0.15f }, { -0.75f, -0.05f }, { -0.25f, 0.05f }, { 0.3f, -0.02f }, { 0.8f, 0.1f }, { 1.3f, 0.2f } };
    for (size_t i = 0; i < std::size (spots); ++i)
    {
        const float tw = 0.5f + 0.5f * (float) std::sin (s.time * 5.0 + (double) i * 1.7);
        const float size = 4.0f + 4.0f * tw;
        const auto c = palette.arcColour ((float) i / 5.0f);
        g.setColour (palette (c).withAlpha (show * (0.35f + 0.65f * tw)));
        g.fillPath (sparkle[0], juce::AffineTransform::scale (size).translated (fig.getCentreX() + spots[i].x * spread, fig.getY() + spots[i].y * fig.getHeight()));
    }
}
} // namespace bt
