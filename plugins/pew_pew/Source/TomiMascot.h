#pragma once

#include "PewPewProcessor.h"

namespace pp
{
/** "Tomi", the disco drummer, and his floor tom.

    Every MIDI note is a stick hit (from the engine's trigger counter, at the
    nearest frame): impact, then follow-through, then the stick back up. The
    hit style follows the energy, i.e. the knob and the velocity: one hand
    and cheerful at the bottom, two hands, star eyes, gritted teeth, and
    dizzy at the very top (8 styles); never a plain timer. The drum is
    drawn by code: its skin swells and lifts with the real pitch. ROOM shows as
    rings on the floor, ECHO as ghost Tomis replaying the hit at the echo
    times. Bypass: he falls asleep sitting by the drum ("Zzz"). */
class TomiMascot final : public gk::MascotComponent
{
public:
    TomiMascot (PewPewProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    // Frame roles (scripts/make_tomi_frames.sh): 8 hit styles by energy, four
    // phases each (stick raised, downswing, impact, follow-through), then sleep
    static constexpr int numLevels = 8, raised = 0, downswing = 1, impact = 2, follow = 3, sleepFirst = 32;
    static int frameOf (int level, int phase) noexcept { return juce::jlimit (0, numLevels - 1, level) * 4 + phase; }
    /** Energy 0..1 → hit style: the knob, and how hard the note was played. */
    static int levelFor (float amount, float velocity) noexcept
    {
        const float energy = amount * (0.55f + 0.45f * velocity);
        return juce::jlimit (0, numLevels - 1, (int) std::floor (energy * numLevels));
    }

    /** 1 on a hit, decaying (~120 ms): the knob's halo and skin lift. */
    float hitEnvelope() const noexcept { return hitEnv; }
    /** Pitch position (0..1) of the latest hit, as the skin lift. */
    float skinLift() const noexcept { return lift; }

    /** Screenshots: shows a hit `secondsAgo` (velocity 1), -1 = live. */
    void setHitOverride (float secondsAgo) noexcept { hitOverride = secondsAgo; }

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintBehind (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;
    void paintCharacter (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    juce::Rectangle<float> drumArea (juce::Rectangle<float> figure) const noexcept;
    double echoSeconds() noexcept;

    PewPewProcessor& proc;
    int lastTrigger = 0, hitIndex = 0, hitLevel = 0;
    double lastTime = 0.0, hitTime = -10.0, bypassSince = -1.0;
    float hitVelocity = 1.0f, hitPitch = 0.0f, hitEnv = 0.0f, lift = 0.0f, hitOverride = -1.0f;
    std::array<double, 8> recentHits {}; // times of the last hits (ghosts, floor rings)
    std::array<int, 8> recentPoses {};
    int recentCount = 0;
    juce::Random random { 7 };

    struct Star { juce::Point<float> pos, vel; float life = 0.0f, size = 1.0f; };
    std::array<Star, 32> stars {};
    juce::Path star, wave;
};
} // namespace pp
