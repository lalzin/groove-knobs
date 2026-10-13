#pragma once

#include "BoogieTrioProcessor.h"

namespace bt
{
/** "Velvet", the disco violinist: the solo who becomes a trio.

    Same technique as French Filter's mascot (bitmap poses, BinaryData):
      0 %       alone, violin at rest, eyes closed, waiting (glances up now and then)
      ~15 %     violin up, ready
      25 %+     playing: the bow cycles through four poses at a speed that
                follows the music's level
      50 %      two clones appear left and right (40 % opacity, smaller, behind)
      75 %+     clones almost opaque; the three sway on the chorus LFO,
                at 0°, 120° and 240° (the three voices, as heard)
      100 %     sparkles
    WIDE spreads the clones further apart, VINTAGE gives them a warm,
    grainy "old photo" look; above 50 % the vibrato makes the trio tremble.
    Bypass: the clones fade out and Velvet falls asleep on his violin. */
class VelvetMascot final : public gk::MascotComponent
{
public:
    VelvetMascot (BoogieTrioProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    // Frame roles (see scripts/make_velvet_frames.sh)
    static constexpr int playFirst = 0, playCount = 4, ready = 4, glance = 5, waiting = 6, sleepFirst = 7;

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintCharacter (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    BoogieTrioProcessor& proc;
    std::vector<juce::Image> sepiaFrames;
    double lastTime = 0.0, sleepStart = 0.0, nextGlance = 3.0, glanceUntil = 0.0, bowPosition = 0.0;
    bool asleep = false;
    float wideSmoothed = 0.0f;
    int playFrame = 0;
    juce::Random random { 7 };
    std::array<juce::Path, 1> sparkle;
};
} // namespace bt
