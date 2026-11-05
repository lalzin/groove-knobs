#pragma once

#include "HeadSpinProcessor.h"

namespace hs
{
/** Kiko, the b-boy, seen from the front (the small figure, left panel; the
    view from above is TopView). His pose follows where the sound is and how
    fast it goes, but his frames never change faster than every 0.45 s
    (whatever the speed, Alan: "it changes frame too fast, it's ugly"):
      - slow (up to 0.5 turn / s), he follows the sound: on the right, a hand
        on the right headphone; on the left, mirrored; behind, looking aside;
      - in front, or faster: he grooves on the beat (standing, leaning);
      - above 4 turns / s: the head spin (crouch, then the two spin poses, 0.4 s each);
      - above 10 turns / s: the cap flies off (same pace);
      - bypass: arms crossed; MIX at 0 or no sound: hands on hips.
    Frames: Assets/kiko_0..15.png (scripts/make_kiko_frames.sh), plus the
    mirrored "hand on headphone" (16). */
class KikoMascot final : public gk::MascotComponent
{
public:
    KikoMascot (HeadSpinProcessor& p, gk::AudioFeatures& features, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();
    /** The view from above (Assets/kiko_top.png), face down as drawn. */
    static juce::Image loadTopView();

    enum Pose
    {
        standing = 0, leaning = 1, standing2 = 2, lookingAside = 3, standing3 = 4, handRight = 5, standing4 = 6, handLeftAlt = 7,
        crouch = 8, spinA = 9, spinB = 10, armsCrossed = 11, standing5 = 12, capOffA = 13, capOffB = 14, handsOnHips = 15,
        handLeft = 16, numPoses = 17
    };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;

private:
    HeadSpinProcessor& proc;
    static constexpr double minHold = 0.45, spinPoseSeconds = 0.4;
    static constexpr float followBelowTurns = 0.5f;
    double spinStarted = -1.0, lastChange = 0.0;
    int current = -1;
};
} // namespace hs
