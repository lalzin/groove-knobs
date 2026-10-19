#pragma once

#include "PewPewProcessor.h"

namespace pp
{
/** "Tomi", the disco drummer, in front of his drum.

    Every MIDI note is a stick hit (from the engine's trigger counter, at the
    nearest frame), its force follows the velocity. The pose depends on the
    knob (calm strike at 0 % → twirl → sparkles → sticks up → euphoric jump at
    100 %) and on the moment of the hit, never on a plain timer. The drum is
    drawn by code: its skin swells and lifts with the real pitch. ROOM shows as
    rings on the floor, ECHO as ghost Tomis replaying the hit at the echo
    times. Bypass: he falls asleep sitting by the drum ("Zzz"). */
class TomiMascot final : public gk::MascotComponent
{
public:
    TomiMascot (PewPewProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    // Frame roles (scripts/make_tomi_frames.sh)
    static constexpr int calmFirst = 0, smile = 4, twirl = 5, sparkleFirst = 6, sticksUpFirst = 8, jump = 10;
    static constexpr int strikeRaised = 11, strikeLift = 12, strikeHit = 13, strikeUp = 14, enthusiasm = 15, sleepFirst = 16;

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
    int hitPose (float amount) const noexcept;
    juce::Rectangle<float> drumArea (juce::Rectangle<float> figure) const noexcept;
    double echoSeconds() noexcept;

    PewPewProcessor& proc;
    int lastTrigger = 0, hitIndex = 0, idleFrame = calmFirst;
    double lastTime = 0.0, hitTime = -10.0, idleFlip = 0.0, bypassSince = -1.0;
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
