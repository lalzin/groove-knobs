#include "BoogieTrioEditor.h"
#include "VelvetMascot.h"

namespace bt
{
gk::Palette BoogieTrioEditor::makePalette()
{
    gk::Palette p;
    const juce::Colour violet (0xff7a4fd0), orange (0xfff29a38), pink (0xffe8508a), cream (0xfff3e9d2);
    p.background = juce::Colour (0xff36363c); // same charcoal as French Filter
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = orange;
    p.beret = pink; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { cream, orange, pink };
    p.knobBands = { violet, orange, pink }; // the three voices
    p.arrowLeft = violet;
    p.arrowRight = orange;
    p.frameLeft = violet;
    p.frameRight = pink;
    p.lowLabel = violet;  // SOLO
    p.highLabel = pink;   // TRIO
    return p;
}

BoogieTrioEditor::BoogieTrioEditor (BoogieTrioProcessor& p)
    : OneKnobEditor (p, makePalette(), BT_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<VelvetMascot> (p, palette); })
{
    // The trio needs the full width of the mascot band
    getMascot().setBounds (10, 58, 400, 172);
}
} // namespace bt
