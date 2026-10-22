#include "SlapperEditor.h"
#include "PluckMascot.h"

namespace slapper
{
namespace
{
const juce::Colour amber (0xffd9822b), cream (0xfffff1d6), wood (0xff3b1f12), wine (0xff9e2a2b);
}

gk::Palette SlapperEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff36363c); // the suite's charcoal
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = amber;
    p.beret = wine; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = wood;
    p.plaque = cream;
    p.arcStops = { amber, wine };
    // FUNK: a sunburst seen from above, wood rim
    p.knobBands = { wood };
    p.knobRings = { wood, amber, wood, amber, wood };
    p.knobRim = wine;
    p.pulse = amber;
    p.arrowLeft = amber;
    p.arrowRight = wine;
    p.frameLeft = amber;
    p.frameRight = wine;
    p.lowLabel = amber;  // ROUND
    p.highLabel = wine;  // FUNK
    return p;
}

/** MONO | POLY: one string (legato slides, ghost notes) or six. */
class SlapperEditor::ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSwitch (juce::AudioParameterChoice& p, const gk::Palette& pal)
        : param (p), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle ("Voice mode");
        setTooltip ("MONO: one string, overlapping notes slide. POLY: up to 6 strings");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
        g.setColour (palette (amber).withAlpha (0.85f));
        g.drawText ("MODE", b.removeFromTop (13.0f), juce::Justification::centred);
        const auto r = b.withSizeKeepingCentre (b.getWidth(), 22.0f).reduced (1.5f);
        const float rad = r.getHeight() * 0.5f;
        g.setColour (palette (palette.outline));
        g.fillRoundedRectangle (r, rad);
        const bool isPoly = param.getIndex() == 1;
        auto lit = r.reduced (2.0f);
        lit = isPoly ? lit.removeFromRight (lit.getWidth() * 0.5f) : lit.removeFromLeft (lit.getWidth() * 0.5f);
        g.setColour (palette (isPoly ? wine : amber).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (lit, lit.getHeight() * 0.5f);
        g.setFont (gk::GrooveFonts::font (10.5f).withExtraKerningFactor (0.05f));
        auto halves = r;
        const auto left = halves.removeFromLeft (halves.getWidth() * 0.5f);
        g.setColour (palette (isPoly ? palette.text : palette.outline).withAlpha (isPoly ? 0.55f : 1.0f));
        g.drawText ("MONO", left, juce::Justification::centred);
        g.setColour (palette (isPoly ? palette.text : palette.text).withAlpha (isPoly ? 1.0f : 0.55f));
        g.drawText ("POLY", halves, juce::Justification::centred);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (r, rad, 2.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            attachment.setValueAsCompleteGesture (e.position.x < (float) getWidth() * 0.5f ? 0.0f : 1.0f);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
            attachment.setValueAsCompleteGesture (param.getIndex() == 1 ? 0.0f : 1.0f);
        else
            return false;
        return true;
    }

private:
    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::ParameterAttachment attachment;
    bool over = false;
};

SlapperEditor::SlapperEditor (SlapperProcessor& p)
    : OneKnobEditor (p, makePalette(), "Slapper",
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<PluckMascot> (p, palette); })
{
    getMascot().setBounds (80, 50, 260, 190); // Pluck is tall: the whole band above the knob
    // the knobs a player reaches for, around FUNK (Pew Pew's places)
    struct Place { int knob; juce::Rectangle<int> bounds; float along; };
    const Place places[] { { SlapperDsp::attackKnob, { 10, 296, 64, 68 }, 0.0f },
                           { SlapperDsp::muteKnob, { 10, 226, 64, 68 }, 0.3f },
                           { SlapperDsp::toneKnob, { 346, 296, 64, 68 }, 1.0f },
                           { SlapperDsp::levelKnob, { 346, 226, 64, 68 }, 0.7f },
                           { SlapperDsp::driveKnob, { 10, 372, 64, 68 }, 0.5f } }; // bottom left, facing MODE
    for (const auto& pl : places)
    {
        knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (pl.knob), getPalette(),
                                                          p.getConfig().knobs[(size_t) pl.knob].caption, getPalette().arcColour (pl.along)));
        addToCanvas (*knobs.back(), pl.bounds);
    }
    mode = std::make_unique<ModeSwitch> (p.getModeParameter(), getPalette());
    addToCanvas (*mode, { 330, 414, 86, 38 }); // bottom right, under TONE
}

SlapperEditor::~SlapperEditor() = default;
} // namespace slapper
