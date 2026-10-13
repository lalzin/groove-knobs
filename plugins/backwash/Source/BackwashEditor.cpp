#include "BackwashEditor.h"
#include "SwellMascot.h"

namespace bw
{
gk::Palette BackwashEditor::makePalette()
{
    gk::Palette p;
    const juce::Colour teal (0xff0f7c8c), aqua (0xff4fd1c5), coral (0xffff7a59), cream (0xfff3e9d2);
    p.background = juce::Colour (0xff36363c); // same charcoal as the rest of the suite
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = aqua;
    p.beret = coral; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { cream, aqua, coral };
    // Knob disc in Swell's own colours (his blue body), a lighter wavy band of
    // the same blue in the middle for the water — not a three-colour flag
    const juce::Colour swellDeep (0xff1a4682), swellBody (0xff2060a0), swellLight (0xff3478c0);
    juce::ignoreUnused (swellBody);
    p.knobBands = { swellDeep, swellLight, swellDeep };
    p.knobBandWave = 5.0f; // wavy band edges, like waves
    p.pulse = aqua;        // the arc breathes with each swell
    p.arrowLeft = teal;
    p.arrowRight = coral;
    p.frameLeft = teal;
    p.frameRight = coral;
    p.lowLabel = teal;   // CALM
    p.highLabel = coral; // TSUNAMI
    return p;
}

BackwashEditor::BackwashEditor (BackwashProcessor& p)
    : OneKnobEditor (p, makePalette(), BW_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<SwellMascot> (p, palette); }),
      proc (p)
{
    getMascot().setBounds (10, 58, 400, 172); // the wave needs the whole band
}

void BackwashEditor::setInhaleOverride (float inhale)
{
    inhaleOverride = inhale;
    static_cast<SwellMascot&> (getMascot()).setInhaleOverride (inhale);
}

void BackwashEditor::editorTick (double)
{
    auto& swell = static_cast<SwellMascot&> (getMascot());
    // The arc breathes during each swell (amount > 0, host playing), and goes
    // dark at once on the hit: you see it before you hear it.
    const bool live = getMascot().getState().playing && proc.getAmountParameter().getValue() > 0.0f;
    getKnob().setPulse (inhaleOverride >= 0.0f ? inhaleOverride : (live ? swell.getInhale() : 0.0f));

    if (auto* chips = getToggleChips())
        chips->setWarning (BackwashDsp::longToggle, proc.getBackwashDsp().getTelemetry().swellClamped.load(),
                           juce::String::fromUTF8 ("Tempo trop lent : souffle raccourci"));
}
} // namespace bw
