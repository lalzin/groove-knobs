#pragma once

#include "SlapperProcessor.h"

namespace slapper
{
/** "Pluck", the sunburst bass. Only FUNK moves him (Alan): his pose climbs
    a ladder with the knob — eyes closed (round, soft), standing, smiling,
    arms out, laughing, arms up (the full slap); asleep in bypass. Nothing
    follows the notes or the beat. */
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
    int rung = 0; // the pose ladder's step (with hysteresis)
};
} // namespace slapper
