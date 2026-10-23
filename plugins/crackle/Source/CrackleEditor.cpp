#include "CrackleEditor.h"
#include "HissMascot.h"

namespace crackle
{
namespace
{
const juce::Colour lilac (0xffa78bfa), ink (0xff1f1b24), paper (0xfff4ede1), warmGrey (0xff8a8178);
}

gk::Palette CrackleEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff2a2630); // ink, lifted a little
    p.backgroundEdge = ink;
    p.surface = juce::Colour (0xff38333f);
    p.outline = juce::Colour (0xff141218);
    p.text = paper;
    p.accent = lilac;
    p.beret = lilac; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = ink;
    p.plaque = paper;
    p.arcStops = { warmGrey, lilac };
    p.knobBands = { ink };
    p.knobRings = { ink, lilac.darker (0.3f), ink, warmGrey, ink };
    p.knobRim = lilac;
    p.pulse = lilac;
    p.arrowLeft = warmGrey;
    p.arrowRight = lilac;
    p.frameLeft = warmGrey;
    p.frameRight = lilac;
    p.lowLabel = warmGrey; // CLEAN
    p.highLabel = lilac;   // GRAIN
    return p;
}

/** The saturation TYPE: a row of segments (click one; arrows, space). */
class CrackleEditor::Segments final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segments (juce::AudioParameterChoice& p, const gk::Palette& pal, juce::String captionText, juce::StringArray segmentLabels)
        : param (p), palette (pal), caption (std::move (captionText)), labels (std::move (segmentLabels)),
          attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle (caption);
        setTooltip ("Saturation type: TUBE, TRANSFORMER, SPEAKER, TAPE, FOLD, HARD (FOCUS: the band that is saturated)");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
        g.setColour (palette (lilac).withAlpha (0.85f));
        g.drawText (caption, b.removeFromTop (13.0f), juce::Justification::centred);
        const auto r = b.withSizeKeepingCentre (b.getWidth(), 22.0f).reduced (1.5f);
        g.setColour (palette (palette.outline));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const int n = labels.size(), on = param.getIndex();
        const float w = (r.getWidth() - 4.0f) / (float) n;
        g.setFont (gk::GrooveFonts::font (8.5f).withExtraKerningFactor (0.02f));
        for (int i = 0; i < n; ++i)
        {
            const auto seg = juce::Rectangle<float> (r.getX() + 2.0f + w * (float) i, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
            if (i == on)
            {
                g.setColour (palette (lilac));
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            g.setColour (palette (i == on ? ink : paper).withAlpha (i == on ? 1.0f : 0.6f));
            g.drawText (labels[i], seg, juce::Justification::centred);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            choose ((int) (e.position.x / (float) getWidth() * (float) labels.size()));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const int i = param.getIndex();
        if (k == juce::KeyPress::leftKey)
            choose (i - 1);
        else if (k == juce::KeyPress::rightKey || k == juce::KeyPress::spaceKey)
            choose ((i + 1) % labels.size());
        else
            return false;
        return true;
    }

private:
    void choose (int i) { attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, labels.size() - 1, i)); }
    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::String caption;
    juce::StringArray labels;
    juce::ParameterAttachment attachment;
};

/** AUTO: lit while the knob follows GRAIN; click to give it back to GRAIN. */
class CrackleEditor::AutoLight final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AutoLight (juce::AudioParameterBool& p, const gk::Palette& pal) : param (p), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTooltip ("AUTO: this effect follows GRAIN. Move its knob to set it yourself; click here to give it back to GRAIN");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = param.get();
        g.setColour (palette (on ? lilac : palette.outline));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (palette (on ? ink : warmGrey));
        g.setFont (gk::GrooveFonts::font (7.5f).withExtraKerningFactor (0.08f));
        g.drawText ("AUTO", r, juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            attachment.setValueAsCompleteGesture (param.get() ? 0.0f : 1.0f);
    }

private:
    juce::AudioParameterBool& param;
    const gk::Palette& palette;
    juce::ParameterAttachment attachment;
};

/** "⋯": opens / closes the settings panel. */
class CrackleEditor::MoreButton final : public juce::Button
{
public:
    explicit MoreButton (const gk::Palette& p) : juce::Button ("Settings"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("Every effect, sorted: tape, vinyl, radio, saturation, digital");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? lilac : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        g.setColour (palette (getToggleState() ? ink : paper));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b.getCentre().translated (7.0f * (float) i, 0.0f)));
    }

private:
    const gk::Palette& palette;
};

/** Every setting, sorted by kind of effect, over Hiss and GRAIN. */
class CrackleEditor::SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (CrackleProcessor& p, const gk::Palette& pal) : palette (pal)
    {
        setTitle ("Effect settings");
        // groups: caption, then its knobs (one row each line of the table below)
        groups = { { "TAPE", { CrackleDsp::wowKnob, CrackleDsp::flutterKnob, CrackleDsp::hissKnob, CrackleDsp::dropoutKnob }, 0, 0 },
                   { "VINYL", { CrackleDsp::vinylKnob, CrackleDsp::noiseKnob }, 1, 0 },
                   { "RADIO", { CrackleDsp::bandKnob, CrackleDsp::radioKnob }, 1, 2 },
                   { "SATURATION", { CrackleDsp::saturationKnob, CrackleDsp::focusLowKnob, CrackleDsp::focusHighKnob }, 2, 0 },
                   { "DIGITAL", { CrackleDsp::crushKnob }, 2, 3 } };
        for (auto& gr : groups)
            for (int k : gr.knobs)
            {
                knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (k), palette, p.getConfig().knobs[(size_t) k].caption,
                                                                  palette.arcColour (0.5f + 0.25f * (float) gr.row)));
                addAndMakeVisible (*knobs.back());
                knobOf.push_back (k);
                if (CrackleProcessor::isEffectKnob (k))
                {
                    lights.push_back (std::make_unique<AutoLight> (p.getAutoParameter (k), palette));
                    addAndMakeVisible (*lights.back());
                    lightOf.push_back (k);
                }
            }
        type = std::make_unique<Segments> (p.getTypeParameter(), palette, "SATURATION TYPE",
                                           juce::StringArray { "TUBE", "TRANSF.", "SPEAKER", "TAPE", "FOLD", "HARD" });
        addAndMakeVisible (*type);
    }

    void resized() override
    {
        // three rows of four slots (each group from its column), then TYPE
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        const int slotW = area.getWidth() / 4, rowH = 84;
        size_t ki = 0, li = 0;
        for (auto& gr : groups)
            for (size_t j = 0; j < gr.knobs.size(); ++j, ++ki)
            {
                const auto slot = juce::Rectangle<int> (area.getX() + (gr.column + (int) j) * slotW, area.getY() + gr.row * rowH + 14, slotW, rowH - 14);
                knobs[ki]->setBounds (slot.withSizeKeepingCentre (64, 68));
                if (li < lightOf.size() && lightOf[li] == gr.knobs[j])
                    lights[li++]->setBounds (slot.getRight() - 26, slot.getY() - 2, 24, 11);
            }
        type->setBounds (area.getX(), area.getY() + 3 * rowH + 6, area.getWidth(), 38);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (juce::Colour (0xff3a3346)), b.getCentreX(), b.getY(), palette (ink), b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (lilac).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (paper).withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText ("EFFECTS", b.withHeight (28.0f), juce::Justification::centred);
        // each group's caption over its knobs, a thin line under it
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        const int slotW = area.getWidth() / 4, rowH = 84;
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        for (auto& gr : groups)
        {
            const auto r = juce::Rectangle<int> (area.getX() + gr.column * slotW + 6, area.getY() + gr.row * rowH, (int) gr.knobs.size() * slotW - 12, 12).toFloat();
            g.setColour (palette (lilac).withAlpha (0.9f));
            g.drawText (gr.caption, r, juce::Justification::centredLeft);
            g.setColour (palette (warmGrey).withAlpha (0.4f));
            g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);
        }
    }

private:
    struct Group
    {
        juce::String caption;
        std::vector<int> knobs;
        int row = 0, column = 0;
    };
    const gk::Palette& palette;
    std::vector<Group> groups;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::vector<int> knobOf, lightOf;
    std::vector<std::unique_ptr<AutoLight>> lights;
    std::unique_ptr<Segments> type;
};

CrackleEditor::CrackleEditor (CrackleProcessor& p)
    : OneKnobEditor (p, makePalette(), "Crackle",
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<HissMascot> (p, palette); })
{
    getMascot().setBounds (110, 54, 200, 180); // Hiss above GRAIN
    // MIX left of GRAIN, VOL right (Pew Pew's places)
    const std::pair<int, juce::Rectangle<int>> front[] { { CrackleDsp::mixKnob, { 10, 296, 64, 68 } }, { CrackleDsp::levelKnob, { 346, 296, 64, 68 } } };
    for (const auto& [k, r] : front)
    {
        frontKnobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (k), getPalette(), p.getConfig().knobs[(size_t) k].caption,
                                                               getPalette().arcColour (k == CrackleDsp::mixKnob ? 0.0f : 1.0f)));
        addToCanvas (*frontKnobs.back(), r);
    }
    panel = std::make_unique<SettingsPanel> (p, getPalette());
    addToCanvas (*panel, { 14, 56, 392, 360 });
    panel->setVisible (false);
    more = std::make_unique<MoreButton> (getPalette());
    addToCanvas (*more, { 362, 418, 46, 26 });
    more->onClick = [this] { panel->setVisible (more->getToggleState()); };
}

CrackleEditor::~CrackleEditor() = default;

void CrackleEditor::setPanelOpen (bool open)
{
    more->setToggleState (open, juce::dontSendNotification);
    panel->setVisible (open);
}
} // namespace crackle
