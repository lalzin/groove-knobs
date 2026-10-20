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

/** SOLO | MULTI, bottom right: one pew at a time, or up to 8 overlapping. */
class PewPewEditor::ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSwitch (juce::AudioParameterChoice& p, const gk::Palette& pal)
        : param (p), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle ("Voice mode");
        setTooltip ("SOLO: each note cuts the previous one. MULTI: up to 8 pews overlap");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour gold (0xffe8b531), cherry (0xffd7336b);
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
        g.setColour (palette (gold).withAlpha (0.85f));
        g.drawText ("MODE", b.removeFromTop (13.0f), juce::Justification::centred);
        const auto r = b.withSizeKeepingCentre (b.getWidth(), 22.0f).reduced (1.5f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (1.5f, 2.5f), rad);
        g.setColour (palette (palette.outline));
        g.fillRoundedRectangle (r, rad);
        const bool solo = param.getIndex() == 1;
        // the lit half
        auto lit = r.reduced (2.0f);
        lit = solo ? lit.removeFromLeft (lit.getWidth() * 0.5f) : lit.removeFromRight (lit.getWidth() * 0.5f);
        g.setColour (palette (solo ? cherry : gold).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (lit, lit.getHeight() * 0.5f);
        g.setFont (gk::GrooveFonts::font (10.5f).withExtraKerningFactor (0.05f));
        auto halves = r;
        const auto left = halves.removeFromLeft (halves.getWidth() * 0.5f);
        g.setColour (palette (solo ? palette.outline : palette.text).withAlpha (solo ? 1.0f : 0.55f));
        g.drawText ("SOLO", left, juce::Justification::centred);
        g.setColour (palette (solo ? palette.text : palette.outline).withAlpha (solo ? 0.55f : 1.0f));
        g.drawText ("MULTI", halves, juce::Justification::centred);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (r, rad, 2.0f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.text).withAlpha (0.6f));
            g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 1.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            set (e.position.x < (float) getWidth() * 0.5f); // click a half to choose it
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
            set (param.getIndex() != 1);
        else if (k == juce::KeyPress::leftKey)
            set (true);
        else if (k == juce::KeyPress::rightKey)
            set (false);
        else
            return false;
        return true;
    }

private:
    void set (bool solo) { attachment.setValueAsCompleteGesture (solo ? 1.0f : 0.0f); }

    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::ParameterAttachment attachment;
    bool over = false;
};

PewPewEditor::PewPewEditor (PewPewProcessor& p)
    : OneKnobEditor (p, makePalette(), "Pew Pew",
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<TomiMascot> (p, palette); }),
      proc (p)
{
    getMascot().setBounds (10, 58, 400, 172); // Tomi and his drum need the whole band
    // SOLO | MULTI bottom right, under SYNC
    mode = std::make_unique<ModeSwitch> (p.getModeParameter(), getPalette());
    addToCanvas (*mode, { 330, 414, 86, 38 });
}

PewPewEditor::~PewPewEditor() = default;

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
