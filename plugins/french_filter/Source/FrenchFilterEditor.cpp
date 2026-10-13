#include "FrenchFilterEditor.h"
#include "FrenchFilterMascot.h"

namespace ff
{
gk::Palette FrenchFilterEditor::makePalette()
{
    gk::Palette p;
    const juce::Colour blue (0xff2f5bd8), cream (0xfff3e9d2), red (0xffe63946);
    p.background = juce::Colour (0xff36363c);
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = blue;
    p.beret = red;
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { blue, cream, red };
    p.knobBands = { blue, cream, red };
    p.arrowLeft = blue;
    p.arrowRight = red;
    return p;
}

FrenchFilterEditor::FrenchFilterEditor (FrenchFilterProcessor& p)
    : OneKnobEditor (p, makePalette(), FF_PRODUCT_NAME,
                     [] (gk::AudioFeatures& features, const gk::Palette& palette) { return std::make_unique<FrenchFilterMascot> (features, palette); })
{
}
} // namespace ff
