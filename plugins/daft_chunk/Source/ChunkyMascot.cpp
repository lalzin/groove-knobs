#include "ChunkyMascot.h"
#include <DaftChunkArt.h>

namespace daftchunk
{
ChunkyMascot::ChunkyMascot (DaftChunkProcessor& p, gk::AudioFeatures& features, const gk::Palette& pal)
    : MascotComponent (features, pal), proc (p)
{
    setFrames (loadFrames());
    motion = 0.5f;
    repaintEvery = 2; // 30 fps
}

std::vector<juce::Image> ChunkyMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "chunky_" + juce::String (i) + "_png";
        const char* data = DCArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int ChunkyMascot::chooseFrame (float macro, float bypass, bool)
{
    const double t = getState().time;
    glitch += 0.15f * ((macro > 0.75f ? (macro - 0.75f) * 4.0f : 0.0f) - glitch);
    auto hold = [&] (int pose, double seconds)
    {
        if (pose != current && t - lastChange >= seconds)
        {
            current = pose;
            lastChange = t;
        }
        return current;
    };
    if (bypass > 0.5f)
        return hold (sitting, 0.0);

    const bool analysing = proc.isAnalysing();
    if (analysing)
    {
        wasAnalysing = true;
        return hold (dice0 + (int) std::fmod (t / 0.12, 3.0), 0.1);
    }
    if (wasAnalysing || proc.getKitVersion() != seenKit)
    {
        // a new sample ready: he hands it over (only after an analysis, not for every cut moved)
        if (wasAnalysing && proc.getSample() != nullptr)
            giveStarted = t;
        wasAnalysing = false;
        seenKit = proc.getKitVersion();
    }
    if (giveStarted >= 0.0)
    {
        const double g = t - giveStarted;
        if (g < 1.8)
            return hold (g < 0.3 ? holdBlock : g < 0.6 ? giveBlock : g < 1.2 ? giveFar : thumbsUp, 0.0);
        giveStarted = -1.0;
    }

    const int slice = proc.getDsp().getTelemetry().slice.load (std::memory_order_relaxed);
    if (slice >= 0)
    {
        if (slice != lastSlice)
        {
            lastSlice = slice;
            danceStep = (danceStep + 1) % (danceLast - dance0 + 1);
        }
        return hold (dance0 + danceStep, 0.12);
    }
    lastSlice = -1;
    if (macro > 0.75f)
        return hold (glitch0 + (int) std::fmod (t / 0.15, 4.0), 0.12);
    return hold (idle0 + (int) std::fmod (t / 0.35, 3.0), 0.3);
}

void ChunkyMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> figure, const juce::AffineTransform& animation)
{
    if (glitch < 0.02f)
    {
        MascotComponent::paintCharacter (g, figure, animation);
        return;
    }
    // the glitch: the figure in horizontal bands, each slipping sideways a little
    const int bands = 7;
    const double t = getState().time;
    for (int b = 0; b < bands; ++b)
    {
        const auto band = figure.withHeight (figure.getHeight() / bands).withY (figure.getY() + figure.getHeight() * b / bands);
        const float shift = glitch * 7.0f * (float) std::sin (t * 23.0 + b * 2.1) * (b % 2 == 0 ? 1.0f : -0.6f);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (band.expanded (figure.getWidth(), 0.0f).getSmallestIntegerContainer());
        drawFrame (g, getState().frame, figure, animation.translated (shift, 0.0f), 1.0f);
    }
}
} // namespace daftchunk
