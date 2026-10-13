#pragma once

namespace gk
{
/** The global choice parameters (OneKnobConfig::choices) as a column of
    small captioned selectors: click (or wheel) opens the list of options. */
class ChoiceChips : public juce::Component
{
public:
    ChoiceChips (OneKnobProcessor& processor, const Palette& palette);
    ~ChoiceChips() override;

    void resized() override;

private:
    class Selector;
    std::vector<std::unique_ptr<Selector>> selectors;
};
} // namespace gk
