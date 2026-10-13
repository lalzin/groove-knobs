#include <GrooveCoreAssets.h>

namespace gk
{
juce::Typeface::Ptr GrooveFonts::display()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (GrooveAssets::LilitaOneRegular_ttf,
                                                                             (size_t) GrooveAssets::LilitaOneRegular_ttfSize);
    return tf;
}

GrooveLookAndFeel::GrooveLookAndFeel (const Palette& p) : palette (p)
{
    setColour (juce::PopupMenu::backgroundColourId, palette.plaque);
    setColour (juce::PopupMenu::textColourId, palette.outline);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, palette.text);
    setColour (juce::TooltipWindow::backgroundColourId, palette.plaque);
    setColour (juce::TooltipWindow::textColourId, palette.outline);
}

juce::Typeface::Ptr GrooveLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (auto tf = GrooveFonts::display())
        return tf;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void GrooveLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (palette.plaque);
    g.setColour (palette.outline);
    g.drawRect (0, 0, width, height, 2);
}
} // namespace gk
