#pragma once

#include "CrackleProcessor.h"

namespace crackle
{
/** "Hiss", the cassette. The poses come from the sheet; everything that moves
    with the sound is drawn by code:
      - the reels turn (in the two holes of the cassette's window), faster
        with WOW and GRAIN; a dropout stops them dead, then they pick up again;
      - a crackle: a little start (now and then, not 30 a second) and specks
        of dust flying off;
      - CRUSH: the body pixelates in short bursts (bigger blocks with more);
      - foot tapping with the music, cheerful when GRAIN is high, asleep in
        bypass. */
class HissMascot final : public gk::MascotComponent
{
public:
    HissMascot (CrackleProcessor& processor, const gk::Palette& palette);
    static std::vector<juce::Image> loadFrames();

    // frame roles (scripts/make_hiss_frames.sh)
    enum Frame { rest = 0, startled = 5, cheerful = 6, squint = 9, squintOther = 10, eyesClosed = 11, footUp = 12, laughing = 14, armsCrossed = 15, numPoses = 16 };

    /** 0..1, a flash on every crackle (the GRAIN ring's glow follows it). */
    float crackleFlash() const noexcept { return flash; }

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintCharacter (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    struct Speck { float x = 0, y = 0, vx = 0, vy = 0, life = 0; };
    void drawReels (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) const;

    CrackleProcessor& proc;
    int lastCrackles = -1, lastDropouts = -1;
    double lastTime = -1.0, startledAt = -10.0, droppedAt = -10.0, crushBurstAt = -10.0;
    float reelAngle = 0.0f, reelSpeed = 0.0f, jump = 0.0f, jumpV = 0.0f, flash = 0.0f, pixel = 0.0f;
    std::array<Speck, 24> specks {};
    std::uint32_t seed = 0x51ed2701u;
    float random() noexcept
    {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return (float) (seed >> 8) / 16777216.0f;
    }
};
} // namespace crackle
