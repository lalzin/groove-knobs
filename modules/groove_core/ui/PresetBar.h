#pragma once

namespace gk
{
/** "◀  Filtered Disco  ▶" — a plaque with the preset name between two arrow
    buttons. Clicking the name opens a menu of all presets (current one
    ticked). Everything goes through the automatable "preset" parameter. */
class PresetBar : public juce::Component
{
public:
    PresetBar (juce::AudioParameterChoice& presetParameter, const Palette& palette);

    void paint (juce::Graphics&) override;
    void resized() override;

    void step (int delta);
    void showMenu();

private:
    class ArrowButton;
    class NameButton;

    void select (int index);

    juce::AudioParameterChoice& param;
    const Palette& palette;
    juce::ParameterAttachment attachment;
    int index = 0;

    std::unique_ptr<ArrowButton> prev, next;
    std::unique_ptr<NameButton> name;
    juce::Rectangle<float> plaque;
};
} // namespace gk
