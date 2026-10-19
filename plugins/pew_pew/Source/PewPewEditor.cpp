#include "PewPewEditor.h"
#include "TomiMascot.h"

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
    // The PEW knob is a drum skin seen from above: cherry rim, indigo / gold rings
    p.knobBands = { indigo };
    p.knobRings = { indigo, gold, indigo, gold, indigo };
    p.knobRim = cherry;
    p.pulse = cherry; // the halo on every hit above 80 %
    p.arrowLeft = gold;
    p.arrowRight = cherry;
    p.frameLeft = gold;
    p.frameRight = cherry;
    p.lowLabel = gold;    // PLAIN
    p.highLabel = cherry; // PEW
    return p;
}

PewPewEditor::PewPewEditor (PewPewProcessor& p)
    : OneKnobEditor (p, makePalette(), "Pew Pew",
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<TomiMascot> (p, palette); }),
      proc (p)
{
    getMascot().setBounds (10, 58, 400, 172); // Tomi and his drum need the whole band
}

void PewPewEditor::setHitOverride (float secondsAgo)
{
    static_cast<TomiMascot&> (getMascot()).setHitOverride (secondsAgo);
}

void PewPewEditor::editorTick (double)
{
    auto& tomi = static_cast<TomiMascot&> (getMascot());
    const float amount = proc.getAmountParameter().getValue();
    // The skin breathes with the pitch of each hit; above 80 % a halo pulses
    getKnob().setSkinLift (tomi.skinLift());
    getKnob().setPulse (amount > 0.8f ? tomi.hitEnvelope() : 0.0f);
}
} // namespace pp
