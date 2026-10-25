#include "PewPewEditor.h"
#include "TomiMascot.h"

namespace pp
{
namespace
{
const juce::Colour gold (0xffe8b531), cherry (0xffd7336b), indigo (0xff3b2e7e), cream (0xfff3e9d2);
}

gk::Palette PewPewEditor::makePalette()
{
    gk::Palette p;
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

/** WAVE: the preset's oscillator (PRESET) or another, a row of segments. */
class PewPewEditor::WaveSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveSwitch (juce::AudioParameterChoice& p, const gk::Palette& pal)
        : param (p), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle ("Wave");
        setTooltip ("WAVE: PRESET keeps the preset's oscillator; SINE, TRI, SQUARE (8-bit), FM, METAL (ring-modulated bell)");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        g.setColour (palette (gold).withAlpha (0.95f));
        g.drawText ("WAVE", b.removeFromLeft (50.0f), juce::Justification::centredLeft);
        const auto r = b.withSizeKeepingCentre (b.getWidth(), 22.0f).reduced (1.5f);
        g.setColour (palette (palette.outline));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const int n = labels.size(), on = param.getIndex();
        const float w = (r.getWidth() - 4.0f) / (float) n;
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.04f));
        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<float> seg (r.getX() + 2.0f + w * (float) i, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
            if (i == on)
            {
                g.setColour (palette (i == 0 ? gold : cherry).withMultipliedBrightness (over ? 1.15f : 1.0f));
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
            }
            g.setColour (palette (i == on ? (i == 0 ? palette.outline : palette.text) : palette.text).withAlpha (i == on ? 1.0f : 0.55f));
            g.drawText (labels[i], seg, juce::Justification::centred);
        }
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 2.0f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.text).withAlpha (0.6f));
            g.drawRoundedRectangle (r.expanded (1.5f), r.getHeight() * 0.5f + 1.5f, 1.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()) && e.position.x > 50.0f)
            choose ((int) ((e.position.x - 50.0f) / ((float) getWidth() - 50.0f) * (float) labels.size()));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const int i = param.getIndex();
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
            choose ((i + 1) % labels.size());
        else if (k == juce::KeyPress::leftKey)
            choose (i - 1);
        else if (k == juce::KeyPress::rightKey)
            choose (i + 1);
        else
            return false;
        return true;
    }

private:
    void choose (int i) { attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, labels.size() - 1, i)); }

    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::ParameterAttachment attachment;
    const juce::StringArray labels { "PRESET", "SINE", "TRI", "SQUARE", "FM", "METAL" };
    bool over = false;
};

/** "⋯": opens / closes the settings panel. */
class PewPewEditor::MoreButton final : public juce::Button
{
public:
    explicit MoreButton (const gk::Palette& p) : juce::Button ("Settings"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("More: wave, tune, decay, glide, detune, drive, click, noise, tone, wobble, room size, echo feedback and width, velocity, key track, roll");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? gold : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
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

/** The settings the front panel leaves out: WAVE, then TOM, COLOUR, FX, PLAY. */
class PewPewEditor::SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (PewPewProcessor& p, const gk::Palette& pal) : palette (pal), wave (p.getWaveParameter(), pal)
    {
        setTitle ("More settings");
        addAndMakeVisible (wave);
        for (int id = PewPewDsp::tuneKnob; id < PewPewDsp::numKnobs; ++id)
        {
            const int row = (id - PewPewDsp::tuneKnob) / 4;
            knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (id), palette, p.getConfig().knobs[(size_t) id].caption,
                                                              palette (rowColour (row))));
            addAndMakeVisible (*knobs.back());
        }
    }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (headerH);
        wave.setBounds (area.getX() + 6, area.getY(), area.getWidth() - 12, waveH);
        const int slotW = area.getWidth() / 4, top = area.getY() + waveH;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (area.getX() + (int) (i % 4) * slotW, top + (int) (i / 4) * rowH + 13, slotW, rowH - 13)
                                     .withSizeKeepingCentre (64, 66));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (indigo), b.getCentreX(), b.getY(), palette (palette.backgroundEdge), b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (gold).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (cream).withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText ("MORE", b.withHeight ((float) headerH), juce::Justification::centred);
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (headerH + waveH);
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        const char* captions[] { "TOM", "COLOUR", "FX", "PLAY" };
        for (int row = 0; row < 4; ++row)
        {
            const auto r = juce::Rectangle<int> (area.getX() + 6, area.getY() + row * rowH, area.getWidth() - 12, 12).toFloat();
            g.setColour (palette (rowColour (row)).withAlpha (0.95f));
            g.drawText (captions[row], r, juce::Justification::centredLeft);
            g.setColour (palette (cream).withAlpha (0.25f));
            g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);
        }
    }

private:
    static juce::Colour rowColour (int row) { return row % 2 == 0 ? gold : cherry.brighter (0.25f); }
    static constexpr int headerH = 28, waveH = 34, rowH = 82;
    const gk::Palette& palette;
    WaveSwitch wave;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
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
    // "⋯" left, above ROOM (VOL's place on the right); its panel over Tomi and the knob
    panel = std::make_unique<SettingsPanel> (p, getPalette());
    addToCanvas (*panel, { 4, 50, 412, 418 }); // (wide enough to cover SYNC and MODE)
    panel->setVisible (false);
    more = std::make_unique<MoreButton> (getPalette());
    addToCanvas (*more, closedButton);
    more->onClick = [this] { setPanelOpen (more->getToggleState()); };
}

PewPewEditor::~PewPewEditor() = default;

void PewPewEditor::setPanelOpen (bool open)
{
    more->setToggleState (open, juce::dontSendNotification);
    panel->setVisible (open);
    // open: the button moves into the panel's top-left corner (it closes it
    // from there instead of covering a knob)
    more->setBounds (open ? openButton : closedButton);
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
