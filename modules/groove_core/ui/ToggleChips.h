#pragma once

namespace gk
{
/** Clickable chips under the knob, one per toggle parameter (e.g. VINTAGE,
    WIDE). Same pill shape as PresetTags; lit = filled with the arc colours. */
class ToggleChips : public juce::Component
{
public:
    ToggleChips (OneKnobProcessor& processor, const Palette& palette);

    void resized() override;

    /** Small warning dot on chip `index`, with its tooltip (empty = none). */
    void setWarning (int index, bool on, const juce::String& tooltip);

private:
    class Chip;
    std::vector<std::unique_ptr<Chip>> chips;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> attachments;
};
} // namespace gk
