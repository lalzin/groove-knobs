#pragma once

#include "SlapperProcessor.h"

namespace slapper
{
/** "Pluck", the sunburst bass. Test version (step 3): each note shows its
    pose for a moment (slap: the hand on the strings, pop: a jump, ghost:
    eyes closed, hands on the body; a hard slap with FUNK high: arms up),
    otherwise he stands, smiles with the music, sleeps in bypass. The code
    effects (boom, sparks, sweat) come with the interface, step 5. */
class PluckMascot final : public gk::MascotComponent
{
public:
    PluckMascot (SlapperProcessor& processor, const gk::Palette& palette);
    static std::vector<juce::Image> loadFrames();

    // frame roles (scripts/make_pluck_frames.sh)
    enum Frame { rest, slap, pop, ghost, hype, laughing, asleep, smiling, armsOut, eyesClosed, numPoses };

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;

private:
    SlapperProcessor& proc;
    int lastHits = -1, hitFrame = rest;
    double lastTime = 0.0, hitTime = -10.0;
};
} // namespace slapper
