#pragma once

#include "SingItBackProcessor.h"

namespace sib
{
/** "Sugar", the disco diva, and her crowd.

    She sings when the voice sings (mouth open only on voiced sound: a new pose
    on every syllable, then on every beat of a long note) and her energy
    follows CROWD: arm out at the bottom, pointing up in the middle, jumping
    with sparkles at the top. Between phrases she is calm, or invites the
    crowd when it is big (and points at it while it answers, RÉPONSE). After a
    long phrase at the top: hand on heart. Bypass: asleep.

    The crowd is drawn by code: one silhouette per voice (VOICES), left and
    right of her, shown as they come in, dimmed and sinking on a dropout,
    glowing and bobbing with what each voice actually sings. */
class SugarMascot final : public gk::MascotComponent
{
public:
    SugarMascot (SingItBackProcessor& processor, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();

    // Frame roles (scripts/make_sugar_frames.sh)
    static constexpr int calmA = 0, inviting = 4, pointingA = 8, jumpA = 12, calmB = 16, armOut = 20, pointingB = 24, jumpWink = 28,
                         heart = 30, asleepSitting = 31, asleepLying = 32;
    /** Energy tier while singing: 0 arm out, 1 pointing up, 2 arm up with a jump now and then. */
    static int tierFor (float crowd) noexcept { return crowd < 0.3f ? 0 : crowd < 0.8f ? 1 : 2; }

    /** Screenshots: pretends the voice sings (or not) with the crowd answering. */
    void setPreview (bool singing, int syllable) noexcept
    {
        preview = true;
        previewSinging = singing;
        syllables = syllable;
    }

protected:
    int chooseFrame (float macro, float bypass, bool musicOn) override;
    void paintBehind (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    struct Singer { float gain = 0.0f, level = 0.0f, bob = 0.0f; };
    juce::Rectangle<float> singerArea (int voice, int count, juce::Rectangle<float> figure) const noexcept;

    SingItBackProcessor& proc;
    double lastTime = 0.0, lastVoiced = -10.0, phraseStart = -1.0, heartUntil = -1.0, bypassSince = -1.0;
    bool wasVoiced = false, preview = false, previewSinging = false;
    int syllables = 0, beatAtSyllable = 0, currentMood = -1, poseStep = 0;
    double poseSince = 0.0;
    float crowdAnswer = 0.0f; // the crowd's level while she is silent
    std::array<Singer, SingItBackDsp::maxVoices> singers {};
    juce::Path note;
};
} // namespace sib
