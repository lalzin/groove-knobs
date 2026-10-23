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

/** "⋯": opens / closes the settings panel. */
class SlapperEditor::MoreButton final : public juce::Button
{
public:
    explicit MoreButton (const gk::Palette& p) : juce::Button ("Settings"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("More: slide, ghost notes, human feel, velocity, the string, release, sub, bend range, octave");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? amber : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        g.setColour (palette (getToggleState() ? palette.outline : palette.text));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b.getCentre().translated (7.0f * (float) i, 0.0f)));
    }

private:
    const gk::Palette& palette;
};

/** The settings the front panel leaves out, sorted: PLAY and STRING. */
class SlapperEditor::SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (SlapperProcessor& p, const gk::Palette& pal) : palette (pal)
    {
        setTitle ("More settings");
        // three rows of up to four: PLAY, STRING, SOUND
        const int rows[3][4] { { SlapperDsp::slideKnob, SlapperDsp::ghostKnob, SlapperDsp::humanKnob, SlapperDsp::velKnob },
                               { SlapperDsp::pickKnob, SlapperDsp::stringKnob, SlapperDsp::bodyKnob, SlapperDsp::releaseKnob },
                               { SlapperDsp::subKnob, SlapperDsp::bendKnob, SlapperDsp::octaveKnob, -1 } };
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                if (const int id = rows[r][c]; id >= 0)
                {
                    knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (id), palette, p.getConfig().knobs[(size_t) id].caption,
                                                                      palette.arcColour ((float) r / 2.0f)));
                    addAndMakeVisible (*knobs.back());
                    places.push_back ({ r, c });
                }
    }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        const int slotW = area.getWidth() / 4, rowH = 88;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (area.getX() + places[i].second * slotW, area.getY() + places[i].first * rowH + 14, slotW, rowH - 14)
                                     .withSizeKeepingCentre (64, 68));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (wood).brighter (0.15f), b.getCentreX(), b.getY(), palette (palette.backgroundEdge), b.getCentreX(),
                                                 b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (amber).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (cream).withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText ("MORE", b.withHeight (28.0f), juce::Justification::centred);
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        const int rowH = 88;
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        const char* captions[] { "PLAY", "STRING", "SOUND" };
        for (int row = 0; row < 3; ++row)
        {
            const auto r = juce::Rectangle<int> (area.getX() + 6, area.getY() + row * rowH, area.getWidth() - 12, 12).toFloat();
            g.setColour (palette (amber).withAlpha (0.9f));
            g.drawText (captions[row], r, juce::Justification::centredLeft);
            g.setColour (palette (cream).withAlpha (0.25f));
            g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);
        }
    }

private:
    const gk::Palette& palette;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::vector<std::pair<int, int>> places; // (row, column) of each knob
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
    addToCanvas (*mode, { 330, 370, 86, 38 }); // right, under TONE
    // ENGINE: a column left of Pluck
    engineSwitch = std::make_unique<Segments> (p.getEngineParameter(), getPalette(), "ENGINE", juce::StringArray { "STRING", "ANALOG", "FM", "PULSE" },
                                               "STRING: the modelled bass guitar. ANALOG: a mono-synth bass (saws, resonant filter). "
                                               "FM: an 80s FM bass. PULSE: a boogie polysynth bass. Each preset picks one");
    addToCanvas (*engineSwitch, { 8, 66, 74, 132 });
    // "⋯" (bottom right, under MODE) and its panel over Pluck and FUNK
    panel = std::make_unique<SettingsPanel> (p, getPalette());
    addToCanvas (*panel, { 14, 56, 392, 310 });
    panel->setVisible (false);
    more = std::make_unique<MoreButton> (getPalette());
    addToCanvas (*more, { 362, 418, 46, 26 });
    more->onClick = [this] { panel->setVisible (more->getToggleState()); };
}

SlapperEditor::~SlapperEditor() = default;

void SlapperEditor::setPanelOpen (bool open)
{
    more->setToggleState (open, juce::dontSendNotification);
    panel->setVisible (open);
}
} // namespace slapper
