#pragma once

namespace gk
{
/** Look & feel for the bits JUCE draws itself (popup menus, tooltips). */
class GrooveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit GrooveLookAndFeel (const Palette& palette);

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override;
    juce::Font getPopupMenuFont() override { return GrooveFonts::font (17.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

private:
    const Palette& palette;
};
} // namespace gk
