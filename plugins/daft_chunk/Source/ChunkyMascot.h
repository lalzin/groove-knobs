#pragma once

#include "DaftChunkProcessor.h"

namespace daftchunk
{
/** Chunky, the block robot (Assets/chunky_0..31.png, scripts/make_chunky_frames.sh).
      - idle: the bounce (0..3);
      - analysing a sample: he shakes the dice (4..6);
      - a sample ready: he hands a block to the right (12..15), once;
      - the dice (DÉ, ENCORE): he shakes it (4..7), then hands the idea over;
      - CHOP high (> 75 %): he glitches (8..11) and his blocks slip sideways;
      - slices playing: he dances, one pose per slice played (16..29);
      - bypass: sitting (31).
    A pose holds at least 0.12 s (no flicker on fast slices). */
class ChunkyMascot final : public gk::MascotComponent
{
public:
    ChunkyMascot (DaftChunkProcessor& p, gk::AudioFeatures& features, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    enum Pose
    {
        idle0 = 0, idle1, idle2, hop, dice0, dice1, dice2, diceHappy, glitch0, glitch1, glitch2, surprised,
        holdBlock, giveBlock, giveFar, thumbsUp, dance0 = 16, danceLast = 29, sitting0 = 30, sitting = 31
    };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintCharacter (juce::Graphics& g, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;

private:
    DaftChunkProcessor& proc;
    int current = idle0, lastSlice = -1, danceStep = 0, seenKit = 0;
    double lastChange = 0.0, giveStarted = -1.0, diceStarted = -1.0;
    int seenDice = 0;
    bool wasAnalysing = false;
    float glitch = 0.0f;
};
} // namespace daftchunk
