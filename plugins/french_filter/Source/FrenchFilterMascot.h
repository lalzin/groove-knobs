#pragma once

#include <groove_core/groove_core.h>

namespace ff
{
/** French Filter's mascot: the beret character (17 poses cut from two sprite
    sheets, BinaryData) plus floating music notes during the drop euphoria. */
class FrenchFilterMascot final : public gk::MascotComponent
{
public:
    FrenchFilterMascot (gk::AudioFeatures& features, const gk::Palette& palette);

    static std::vector<juce::Image> loadFrames();
    static Poses poses();

protected:
    void paintOverlay (juce::Graphics&, const State&, juce::Rectangle<float> figure) override;

private:
    juce::Path note; // built once
};
} // namespace ff
