#pragma once

namespace gk
{
/** A small secondary knob (e.g. ROOM, ECHO): the big knob's interactions
    (vertical drag, Shift for fine, wheel, double-click = default, keys,
    right-click = host menu) with a compact look: thin arc in the palette's
    arc colours, dark disc, pointer line, caption underneath, value in % on
    hover. */
class SmallKnob final : public BigKnob
{
public:
    SmallKnob (juce::RangedAudioParameter& parameter, const Palette& palette, juce::String caption, juce::Colour colour);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return getLocalBounds().contains (x, y); }

private:
    const Palette& pal;
    juce::String caption;
    juce::Colour colour;
};
} // namespace gk
