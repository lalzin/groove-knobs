#pragma once

#include "CrackleProcessor.h"

namespace crackle
{
/** "Hiss", the cassette. Only GRAIN moves him (Alan: "the mascot moves too
    much, only the one knob should"): his pose climbs a ladder with the knob,
    his reels turn faster with it, and from where the bitcrush comes in a
    still, pixelated double stands behind him. Nothing follows the audio
    events (crackles, dropouts, beats); asleep in bypass. */
class HissMascot final : public gk::MascotComponent
{
public:
    HissMascot (CrackleProcessor& processor, const gk::Palette& palette);
    static std::vector<juce::Image> loadFrames();

    // frame roles (scripts/make_hiss_frames.sh)
    enum Frame { rest = 0, smiling = 2, startled = 5, cheerful = 6, squint = 9, squintOther = 10, eyesClosed = 11, footUp = 12, laughing = 14, armsCrossed = 15, numPoses = 16 };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintCharacter (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;

private:
    void drawReels (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) const;

    double lastTime = -1.0;
    float reelAngle = 0.0f, reelSpeed = 0.0f, pixel = 0.0f;
    int rung = 0; // the pose ladder's step (with hysteresis)
};
} // namespace crackle
