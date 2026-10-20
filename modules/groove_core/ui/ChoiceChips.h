#pragma once

namespace gk
{
/** One captioned selector for a choice parameter (caption on top, a chip
    with the current option): click / Return opens the list, wheel and arrow
    keys step through it. */
class ChoiceSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceSelector (juce::AudioParameterChoice& parameter, const juce::String& caption, juce::Colour colour, const Palette& palette);

    static constexpr float captionHeight = 13.0f, chipHeight = 21.0f;

    /** Optional display names for the options (e.g. translated); empty = the parameter's. */
    void setDisplayNames (juce::StringArray names) { displayNames = std::move (names); repaint(); }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void step (int delta);
    void showMenu();

private:
    void set (int index);
    juce::String nameOf (int index) const;

    juce::AudioParameterChoice& param;
    juce::String caption;
    juce::Colour lit;
    const Palette& palette;
    juce::ParameterAttachment attachment;
    juce::StringArray displayNames;
    bool over = false, down = false;
};

/** The global choice parameters (OneKnobConfig::choices) as a column of
    small captioned selectors: click (or wheel) opens the list of options. */
class ChoiceChips : public juce::Component
{
public:
    ChoiceChips (OneKnobProcessor& processor, const Palette& palette);
    ~ChoiceChips() override;

    void resized() override;

private:
    std::vector<std::unique_ptr<ChoiceSelector>> selectors;
};
} // namespace gk
