#pragma once

namespace gk
{
/** Clickable chips under the knob, one per toggle parameter (e.g. VINTAGE,
    WIDE). Same pill shape as PresetTags; lit = filled with the arc colours. */
class ToggleChips : public juce::Component
{
public:
    /** `showSwitches`: the global switches (always stacked in a column);
        `column`: the toggles stacked in a column too. */
    ToggleChips (OneKnobProcessor& processor, const Palette& palette, bool showSwitches = false, bool column = false);

    void resized() override;

    /** Small warning dot on chip `index`, with its tooltip (empty = none). */
    void setWarning (int index, bool on, const juce::String& tooltip);

    void setChipTooltip (int index, const juce::String& tooltip);
    void setChipDimmed (int index, bool dimmed);

private:
    class Chip;
    std::vector<std::unique_ptr<Chip>> chips;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> attachments;
    bool vertical = false;
};
} // namespace gk
