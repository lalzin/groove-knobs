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

/** A row of segments for a choice parameter, its caption above: MODE
    (MONO | POLY, a row), ENGINE (STRING | ANALOG | FM | PULSE, a column).
    Click a segment; keys: arrows, space for the next. */
class SlapperEditor::Segments final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segments (juce::AudioParameterChoice& p, const gk::Palette& pal, juce::String captionText, juce::StringArray segmentLabels, juce::String tip)
        : param (p), palette (pal), caption (std::move (captionText)), labels (std::move (segmentLabels)),
          attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle (caption);
        setTooltip (tip);
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
        g.setColour (palette (amber).withAlpha (0.85f));
        g.drawText (caption, b.removeFromTop (13.0f), juce::Justification::centred);
        // a row (MODE), or a column when taller than wide (ENGINE)
        const int n = labels.size(), on = param.getIndex();
        const bool column = vertical();
        const auto r = column ? b.reduced (1.5f) : b.withSizeKeepingCentre (b.getWidth(), 22.0f).reduced (1.5f);
        const float rad = column ? 11.0f : r.getHeight() * 0.5f;
        g.setColour (palette (palette.outline));
        g.fillRoundedRectangle (r, rad);
        const float w = ((column ? r.getHeight() : r.getWidth()) - 4.0f) / (float) n;
        g.setFont (gk::GrooveFonts::font (n > 2 && ! column ? 9.5f : 10.5f).withExtraKerningFactor (0.04f));
        for (int i = 0; i < n; ++i)
        {
            const auto seg = column ? juce::Rectangle<float> (r.getX() + 2.0f, r.getY() + 2.0f + w * (float) i, r.getWidth() - 4.0f, w)
                                    : juce::Rectangle<float> (r.getX() + 2.0f + w * (float) i, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
            if (i == on)
            {
                // the first segment amber, the others wine (POLY, the synths)
                g.setColour (palette (i == 0 ? amber : wine).withMultipliedBrightness (over ? 1.15f : 1.0f));
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            g.setColour (palette (i == on ? (i == 0 ? palette.outline : palette.text) : palette.text).withAlpha (i == on ? 1.0f : 0.55f));
            g.drawText (labels[i], seg, juce::Justification::centred);
        }
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (r, rad, 2.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            choose (vertical() ? (int) ((e.position.y - 13.0f) / (float) (getHeight() - 13) * (float) labels.size())
                               : (int) (e.position.x / (float) getWidth() * (float) labels.size()));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const int i = param.getIndex(), n = labels.size();
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
            choose ((i + 1) % n);
        else if (k == juce::KeyPress::leftKey || k == juce::KeyPress::upKey)
            choose (i - 1);
        else if (k == juce::KeyPress::rightKey || k == juce::KeyPress::downKey)
            choose (i + 1);
        else
            return false;
        return true;
    }

private:
    bool vertical() const noexcept { return getHeight() > getWidth(); }
    void choose (int i)
    {
        i = juce::jlimit (0, labels.size() - 1, i);
        attachment.setValueAsCompleteGesture ((float) i);
    }

    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::String caption;
    juce::StringArray labels;
    juce::ParameterAttachment attachment;
    bool over = false;
};

SlapperEditor::SlapperEditor (SlapperProcessor& p)
    : OneKnobEditor (p, makePalette(), "Slapper",
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<PluckMascot> (p, palette); })
{
    // Pluck is tall: the whole band above the knob (no preset tags under the knob)
    placeMascotAndKnob ({ 80, 50, 260, 190 }, juce::Rectangle<int> (262, 262).withCentre ({ 210, 345 }));
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
    mode = std::make_unique<Segments> (p.getModeParameter(), getPalette(), "MODE", juce::StringArray { "MONO", "POLY" },
                                       "MONO: one voice, overlapping notes slide. POLY: up to 6 voices");
    addToCanvas (*mode, { 330, 414, 86, 38 }); // bottom right, under TONE
    // ENGINE: a column left of Pluck
    engineSwitch = std::make_unique<Segments> (p.getEngineParameter(), getPalette(), "ENGINE", juce::StringArray { "STRING", "ANALOG", "FM", "PULSE" },
                                               "STRING: the modelled bass guitar. ANALOG: a mono-synth bass (saws, resonant filter). "
                                               "FM: an 80s FM bass. PULSE: a boogie polysynth bass. Each preset picks one");
    addToCanvas (*engineSwitch, { 8, 66, 74, 132 });
}

SlapperEditor::~SlapperEditor() = default;
} // namespace slapper
