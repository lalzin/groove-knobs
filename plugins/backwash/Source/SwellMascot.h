#pragma once

#include "BackwashProcessor.h"

namespace bw
{
/** "Swell", the surfer, and the wave behind him (vector, drawn by code).

    Poses (bitmaps from the sheet) follow the knob: beach (0 %) → paddling
    (25 %) → standing on the board (50 %) → crouched in a big wave (75 %) →
    euphoric under the curl (100 %); bypass: asleep (first pose), flat sea.

    The heart of it is the breath: the processor announces each hit 2 s in
    advance (lookahead), so during every swell Swell inhales (body grows,
    lifts) in sync with the real sound, and the wave digs in; on the hit he
    exhales at once (bounce, water drops) and the wave falls back. The wave
    also moves with the rhythm (beat phase), sparkles with SHIMMER. */
class SwellMascot final : public gk::MascotComponent
{
public:
    SwellMascot (BackwashProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    /** Screenshots: fixes the breath (0..1), -1 = live. */
    void setInhaleOverride (float value) noexcept { inhaleOverride = value; }

    /** Current breath 0..1 (for the knob glow). */
    float getInhale() const noexcept { return inhale; }

    static constexpr int sleepPose = 0, beachFirst = 1, paddleFirst = 4, kneel = 6, stand = 7, crouchFirst = 8, euphoric = 11;

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintBehind (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;
    void paintCharacter (juce::Graphics&, juce::Rectangle<float> figure, const juce::AffineTransform& animation) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    void updateBreath (double dt, float amount, float bypass);
    void spawnDrops (juce::Point<float> origin, int count);

    BackwashProcessor& proc;
    double lastTime = 0.0, idleFlip = 0.0;
    float inhale = 0.0f, inhaleOverride = -1.0f, exhale = 0.0f, waveDrop = 0.0f, seaHeight = 0.0f;
    int idleFrame = beachFirst;
    juce::Random random { 17 };

    struct Drop { juce::Point<float> pos, vel; float life = 0.0f; };
    std::array<Drop, 24> drops {};

    // Reused paths (no allocation in the paint loop once grown)
    juce::Path wavePath, foamPath, curlPath, sparkle;
};
} // namespace bw
