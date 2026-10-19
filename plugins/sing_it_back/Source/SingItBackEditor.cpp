#include "SingItBackEditor.h"

namespace sib
{
gk::Palette SingItBackEditor::makePalette()
{
    gk::Palette p;
    const juce::Colour orange (0xffff8a3d), teal (0xff1fb5a8), plum (0xff5b2a86), cream (0xfff3e9d2);
    p.background = juce::Colour (0xff36363c);
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = orange;
    p.beret = orange; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { teal, orange };
    p.knobBands = { orange, plum };
    p.arrowLeft = teal;
    p.arrowRight = orange;
    p.lowLabel = teal;     // SOLO
    p.highLabel = orange;  // CROWD
    return p;
}

SingItBackEditor::SingItBackEditor (SingItBackProcessor& p)
    : OneKnobEditor (p, makePalette(), SIB_PRODUCT_NAME,
                     [] (gk::AudioFeatures& f, const gk::Palette& palette) { return std::make_unique<gk::MascotComponent> (f, palette); })
{
}
} // namespace sib
