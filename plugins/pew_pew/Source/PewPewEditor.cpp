#include "PewPewEditor.h"

namespace pp
{
gk::Palette PewPewEditor::makePalette()
{
    gk::Palette p;
    const juce::Colour gold (0xffe8b531), cherry (0xffd7336b), indigo (0xff3b2e7e), cream (0xfff3e9d2);
    p.background = juce::Colour (0xff36363c); // same charcoal as the rest of the suite
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = gold;
    p.beret = cherry; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { gold, cherry };
    p.knobBands = { indigo, gold, indigo };
    p.arrowLeft = gold;
    p.arrowRight = cherry;
    p.frameLeft = gold;
    p.frameRight = cherry;
    p.lowLabel = gold;    // PLAIN
    p.highLabel = cherry; // PEW
    return p;
}

PewPewEditor::PewPewEditor (PewPewProcessor& p)
    : OneKnobEditor (p, makePalette(), PP_PRODUCT_NAME,
                     [] (gk::AudioFeatures& f, const gk::Palette& palette) { return std::make_unique<gk::MascotComponent> (f, palette); })
{
}
} // namespace pp
