#pragma once

#include "KickLockProcessor.h"

namespace kl
{
/** "Phaze", the kick drum, reacts to the plugin's real state (the status
    computed by the engine, the kicks it detects, the ducking):
      - no kick on the sidechain, or bypass: asleep;
      - Décalé (realigning): puzzled, "?";
      - Instable / Somme en perte: worried, hands up;
      - Sub faible: calm, a little jump on each kick;
      - OK: taps its foot on every kick, and cheers every fourth one;
      - every kick: dips with the ducking (deeper with DEPTH).
    The wave under its feet (the sub, its phase at the kick) is drawn by
    SubWave in the editor. */
class PhazeMascot final : public gk::MascotComponent
{
public:
    PhazeMascot (KickLockProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    // Frame roles (scripts/make_phaze_frames.sh)
    static constexpr int happyA = 0, calmA = 4, calmJump = 6, asleep = 8, puzzledA = 9, tapA = 13, worriedA = 17, numPoses = 21;

    /** Screenshots: shows this status (and kick count) whatever the engine says. */
    void setPreview (int status, int kicks) noexcept
    {
        previewStatus = status;
        previewKicks = kicks;
    }

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintCharacter (juce::Graphics& g, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;

private:
    KickLockProcessor& proc;
    double lastTime = 0.0, kickTime = -10.0, moodSince = 0.0;
    int lastKickEvents = -1, kicksInMood = 0, mood = -1, previewStatus = -1, previewKicks = 0;
    float dip = 0.0f;
};
} // namespace kl
