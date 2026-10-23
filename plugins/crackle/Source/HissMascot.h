#pragma once

#include "CrackleProcessor.h"

namespace crackle
{
/** "Hiss", the cassette. Test version: startled on a big crackle, eyes
    closed through a dropout, squinting while CRUSH bites, cheerful with the
    music, asleep (eyes closed) in bypass. The code effects (dust, pixels,
    the reels turning) come with the interface. */
class HissMascot final : public gk::MascotComponent
{
public:
    HissMascot (CrackleProcessor& processor, const gk::Palette& palette);
    static std::vector<juce::Image> loadFrames();

    // frame roles (scripts/make_hiss_frames.sh)
    enum Frame { rest = 0, startled = 5, cheerful = 6, squint = 9, squintOther = 10, eyesClosed = 11, footUp = 12, laughing = 14, armsCrossed = 15, numPoses = 16 };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;

private:
    CrackleProcessor& proc;
    int lastCrackles = -1, lastDropouts = -1;
    double startledAt = -10.0, droppedAt = -10.0;
};
} // namespace crackle
