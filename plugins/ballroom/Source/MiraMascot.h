#pragma once

#include "BallroomProcessor.h"

namespace bl
{
/** Mira, the disco ball. Her pose follows SPACE (and the beat when the music
    plays): calm sway -> arms open -> the disco point -> arms up (and a turn at
    the very top); FREEZE: she stops, hands up, and everything holds still;
    bypass: at rest. Drawn in code: light spots turning round her (more and
    faster as SPACE goes up) and her facets sparkling with the reverb's level.
    Frames: Assets/mira_0..20.png (scripts/make_mira_frames.sh). */
class MiraMascot final : public gk::MascotComponent
{
public:
    MiraMascot (BallroomProcessor& p, gk::AudioFeatures& features, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    enum Pose
    {
        standing = 0, kneesBent = 1, standing2 = 2, kick = 3, kickStep = 4, handsUpOh = 5, armsUp = 6, armsUpLaugh = 7,
        pointKick = 8, handsUpSmallOh = 9, armsUpHigh = 10, point = 11, restHands = 12, shrug = 13, armsWide = 14, turnedOh = 15,
        handOnHip = 16, armsOpen = 17, armsOutUp = 18, turnedSurprised = 19, hero = 20, numPoses = 21
    };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintBehind (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    BallroomProcessor& proc;
    double spin = 0.0, lastTime = -1.0, sparkleClock = 0.0;
    float space = 0.0f, level = 0.0f;
    bool frozen = false;
};
} // namespace bl
