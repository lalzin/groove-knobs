#include "SingItBackEditor.h"
#include "SugarMascot.h"

namespace sib
{
namespace
{
    const juce::Colour orange (0xffff8a3d), teal (0xff1fb5a8), plum (0xff5b2a86), cream (0xfff3e9d2);

    juce::String keyName (int pc) { return Harmony::noteName (pc); }
} // namespace

//==============================================================================
/** The bottom bar: ◀ key ▶ | scale ▾ | SNAP | the note being sung. */
class SingItBackEditor::ScaleBar final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ScaleBar (SingItBackProcessor& p, const gk::Palette& pal)
        : proc (p), palette (pal),
          keyAttachment (p.getKeyParameter(), [this] (float) { repaint(); }, nullptr),
          scaleAttachment (p.getScaleParameter(), [this] (float) { repaint(); }, nullptr),
          snapAttachment (p.getSnapParameter(), [this] (float) { repaint(); }, nullptr)
    {
        setTitle ("Scale");
        setWantsKeyboardFocus (true);
        keyAttachment.sendInitialUpdate();
        scaleAttachment.sendInitialUpdate();
        snapAttachment.sendInitialUpdate();
    }

    void setDegree (int pitchClass, float voiced)
    {
        if (pitchClass >= 0)
            shownDegree = pitchClass;
        if (pitchClass != lastDegree || std::abs (voiced - lastVoiced) > 0.02f)
        {
            lastDegree = pitchClass;
            lastVoiced = voiced;
            repaint (zone (degreeZone).toNearestInt().expanded (2));
        }
    }

    void resized() override
    {
        plaque = getLocalBounds().toFloat().reduced (6.0f, 4.0f);
    }

    void paint (juce::Graphics& g) override
    {
        // plaque: shadow, teal → orange → plum frame, cream inside (as the preset bars)
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (plaque.translated (2.0f, 4.0f), 9.0f);
        juce::ColourGradient frame (palette (teal), plaque.getX(), 0.0f, palette (plum), plaque.getRight(), 0.0f, false);
        frame.addColour (0.5, palette (orange));
        g.setGradientFill (frame);
        g.fillRoundedRectangle (plaque, 9.0f);
        const auto inner = plaque.reduced (5.0f, 4.0f);
        g.setColour (palette (palette.plaque));
        g.fillRoundedRectangle (inner, 5.0f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (plaque, 9.0f, 3.0f);
        g.drawRoundedRectangle (inner, 5.0f, 1.5f);

        const auto ink = palette (palette.outline);
        // key: ◀ name ▶
        for (int z : { keyDown, keyUp })
        {
            const auto r = value (z).reduced (5.0f, 7.0f);
            juce::Path tri;
            if (z == keyDown)
                tri.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
            else
                tri.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
            g.setColour (palette (z == keyDown ? teal : orange).withMultipliedBrightness (hot == z ? 1.2f : 1.0f));
            g.fillPath (tri);
            g.setColour (ink);
            g.strokePath (tri, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (ink);
        g.setFont (gk::GrooveFonts::font (24.0f));
        g.drawFittedText (keyName (proc.getKeyParameter().getIndex()), value (keyZone).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        caption (g, "TON", zone (keyDown).getUnion (zone (keyUp)));

        // scale ▾
        {
            caption (g, "GAMME", zone (scaleZone));
            g.setColour (ink.withAlpha (hot == scaleZone ? 1.0f : 0.9f));
            g.setFont (gk::GrooveFonts::font (18.0f));
            g.drawFittedText (proc.getScaleParameter().getCurrentChoiceName(), value (scaleZone).withTrimmedRight (14.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.75f);
            juce::Path tri;
            const float cx = zone (scaleZone).getRight() - 9.0f, cy = value (scaleZone).getCentreY() + 1.0f;
            tri.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
            g.setColour (palette (orange));
            g.fillPath (tri);
        }

        // SNAP: a pill, lit when on
        {
            const bool on = proc.getSnapParameter().get();
            caption (g, "SNAP", zone (snapZone));
            const auto r = value (snapZone).reduced (4.0f, 2.0f);
            g.setColour (palette (on ? teal : palette.surface).withMultipliedBrightness (hot == snapZone ? 1.15f : 1.0f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (ink);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 2.0f);
            g.setColour (palette (on ? palette.outline : palette.text).withAlpha (on ? 1.0f : 0.6f));
            g.setFont (gk::GrooveFonts::font (14.0f).withExtraKerningFactor (0.08f));
            g.drawText (on ? "ON" : "OFF", r, juce::Justification::centred);
        }

        // the note she is singing (dim between phrases)
        {
            caption (g, "NOTE", zone (degreeZone));
            const float a = 0.3f + 0.7f * lastVoiced;
            g.setColour (palette (plum).withAlpha (a));
            g.setFont (gk::GrooveFonts::font (23.0f));
            g.drawFittedText (shownDegree >= 0 ? keyName (shownDegree) : juce::String ("-"), value (degreeZone).toNearestInt(), juce::Justification::centred, 1,
                              0.8f);
        }

        // separators
        g.setColour (ink.withAlpha (0.25f));
        for (int z : { scaleZone, snapZone, degreeZone })
            g.fillRect (zone (z).getX() - 1.0f, inner.getY() + 7.0f, 1.0f, inner.getHeight() - 14.0f);
        if (hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.text).withAlpha (0.6f));
            g.drawRoundedRectangle (plaque.expanded (1.5f), 10.0f, 1.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHot (zoneAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHot (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        switch (zoneAt (e.position))
        {
            case keyDown: stepKey (-1); break;
            case keyUp: stepKey (1); break;
            case keyZone: keyMenu(); break;
            case scaleZone: scaleMenu(); break;
            case snapZone: snapAttachment.setValueAsCompleteGesture (proc.getSnapParameter().get() ? 0.0f : 1.0f); break;
            default: break;
        }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (juce::exactlyEqual (w.deltaY, 0.0f))
            return;
        const int z = zoneAt (e.position), d = w.deltaY > 0.0f ? 1 : -1;
        if (z == scaleZone)
            scaleAttachment.setValueAsCompleteGesture ((float) ((proc.getScaleParameter().getIndex() - d + Harmony::numScales) % Harmony::numScales));
        else if (z <= keyUp)
            stepKey (d);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::downKey)
            stepKey (-1);
        else if (k == juce::KeyPress::rightKey || k == juce::KeyPress::upKey)
            stepKey (1);
        else if (k == juce::KeyPress::returnKey)
            scaleMenu();
        else if (k == juce::KeyPress::spaceKey)
            snapAttachment.setValueAsCompleteGesture (proc.getSnapParameter().get() ? 0.0f : 1.0f);
        else
            return false;
        return true;
    }

private:
    enum Zone { keyDown, keyZone, keyUp, scaleZone, snapZone, degreeZone, numZones };

    static constexpr float captionH = 14.0f;

    /** The caption above a zone's value: readable (11 px, dark ink). */
    void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> z) const
    {
        g.setColour (palette (palette.outline).withAlpha (0.72f));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.14f));
        g.drawText (text, z.withHeight (captionH), juce::Justification::centred);
    }
    juce::Rectangle<float> value (int z) const { return zone (z).withTrimmedTop (captionH - 1.0f); }

    juce::Rectangle<float> zone (int z) const
    {
        // widths of the zones across the plaque
        static const float widths[numZones] = { 30.0f, 66.0f, 30.0f, 128.0f, 68.0f, 0.0f };
        auto r = plaque.reduced (7.0f, 5.0f);
        for (int i = 0; i < z; ++i)
            r.removeFromLeft (widths[i]);
        return z == degreeZone ? r : r.removeFromLeft (widths[z]);
    }
    int zoneAt (juce::Point<float> p) const
    {
        for (int z = 0; z < numZones; ++z)
            if (zone (z).contains (p))
                return z;
        return -1;
    }
    void setHot (int z)
    {
        if (z == hot)
            return;
        hot = z;
        setMouseCursor (z >= 0 && z != degreeZone ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    void stepKey (int d) { keyAttachment.setValueAsCompleteGesture ((float) ((proc.getKeyParameter().getIndex() + d + 12) % 12)); }
    void keyMenu()
    {
        juce::PopupMenu m;
        for (int i = 0; i < 12; ++i)
            m.addItem (juce::PopupMenu::Item (keyName (i)).setTicked (i == proc.getKeyParameter().getIndex()).setAction ([this, i] {
                keyAttachment.setValueAsCompleteGesture ((float) i);
            }));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (zone (keyZone).toNearestInt())));
    }
    void scaleMenu()
    {
        juce::PopupMenu m;
        const auto& choices = proc.getScaleParameter().choices;
        for (int i = 0; i < choices.size(); ++i)
            m.addItem (juce::PopupMenu::Item (choices[i]).setTicked (i == proc.getScaleParameter().getIndex()).setAction ([this, i] {
                scaleAttachment.setValueAsCompleteGesture ((float) i);
            }));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (zone (scaleZone).toNearestInt())));
    }

    SingItBackProcessor& proc;
    const gk::Palette& palette;
    juce::ParameterAttachment keyAttachment, scaleAttachment, snapAttachment;
    juce::Rectangle<float> plaque;
    int hot = -1, lastDegree = -2, shownDegree = -1;
    float lastVoiced = 0.0f;
};

//==============================================================================
/** "⋯": opens / closes the advanced panel. */
class SingItBackEditor::MoreButton final : public juce::Button
{
public:
    explicit MoreButton (const gk::Palette& p) : juce::Button ("More settings"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("Spread, humanize, dropouts, room");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? orange : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
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

//==============================================================================
/** The advanced settings, over Sugar's stage: four small knobs and the room size. */
class SingItBackEditor::AdvancedPanel final : public juce::Component
{
public:
    AdvancedPanel (SingItBackProcessor& p, const gk::Palette& pal) : palette (pal)
    {
        const juce::RangedAudioParameter* params[] = { &p.getSpreadParameter(), &p.getHumanizeParameter(), &p.getDropoutsParameter(), &p.getRoomParameter() };
        const char* captions[] = { "SPREAD", "HUMAN", "DROPS", "ROOM" };
        const juce::Colour colours[] = { teal, orange, orange, plum.brighter (0.4f) };
        for (int i = 0; i < 4; ++i)
        {
            knobs.push_back (std::make_unique<gk::SmallKnob> (*const_cast<juce::RangedAudioParameter*> (params[i]), palette, captions[i], colours[i]));
            addAndMakeVisible (*knobs.back());
        }
        roomSize = std::make_unique<gk::ChoiceSelector> (p.getRoomSizeParameter(), "ROOM SIZE", plum.brighter (0.4f), palette);
        addAndMakeVisible (*roomSize);
        setTitle ("Advanced settings");
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10, 8);
        b.removeFromTop (24);
        auto row = b.removeFromTop (72);
        const int w = row.getWidth() / 4;
        for (auto& k : knobs)
            k->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (64, 68));
        roomSize->setBounds (b.withSizeKeepingCentre (110, 38));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (plum).darker (0.55f), b.getCentreX(), b.getY(), palette (palette.backgroundEdge), b.getCentreX(),
                                                 b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (orange).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (cream).withAlpha (0.8f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText (juce::String::fromUTF8 ("R\xc3\x89GLAGES DE LA FOULE"), b.withHeight (30.0f), juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::unique_ptr<gk::ChoiceSelector> roomSize;
};

//==============================================================================
gk::Palette SingItBackEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff36363c); // the suite's charcoal
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = orange;
    p.beret = orange; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { orange, plum.brighter (0.25f) }; // CROWD: orange → plum
    p.knobBands = { orange, plum, orange, plum };
    p.knobBandWave = 2.5f;
    p.pulse = orange; // breathes with the crowd's singing
    p.arrowLeft = teal;
    p.arrowRight = orange;
    p.lowLabel = orange;                // SOLO
    p.highLabel = plum.brighter (0.45f); // CROWD
    return p;
}

SingItBackEditor::SingItBackEditor (SingItBackProcessor& p)
    : OneKnobEditor (p, makePalette(), SIB_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<SugarMascot> (p, palette); }),
      proc (p)
{
    auto& pal = getPalette();
    getMascot().setBounds (10, 58, 400, 172); // Sugar and her crowd need the whole band
    setBackgroundCached (true);

    style = std::make_unique<gk::ChoiceSelector> (p.getStyleParameter(), "STYLE", orange, pal);
    response = std::make_unique<gk::ChoiceSelector> (p.getResponseParameter(), juce::String::fromUTF8 ("R\xc3\x89PONSE"), teal, pal);
    harmony = std::make_unique<gk::ChoiceSelector> (p.getHarmonyParameter(), "HARMONIE", orange, pal);
    voices = std::make_unique<gk::ChoiceSelector> (p.getVoicesParameter(), "VOIX", plum.brighter (0.45f), pal);
    style->setTooltip ("Crowd style: who sings what, and how");
    response->setTooltip ("Together, or the crowd sings your phrase back half a bar / a bar later");
    addToCanvas (*style, { 6, 290, 80, 38 });
    addToCanvas (*response, { 6, 340, 80, 38 });
    addToCanvas (*harmony, { 334, 290, 80, 38 });
    addToCanvas (*voices, { 334, 340, 80, 38 });

    volume = std::make_unique<gk::SmallKnob> (p.getVolumeParameter(), pal, "VOL", orange);
    addToCanvas (*volume, { 10, 386, 64, 68 });

    scaleBar = std::make_unique<ScaleBar> (p, pal);
    addToCanvas (*scaleBar, { 0, 460, baseWidth, 60 });

    advanced = std::make_unique<AdvancedPanel> (p, pal);
    addToCanvas (*advanced, { 30, 62, 360, 168 });
    advanced->setVisible (false);
    more = std::make_unique<MoreButton> (pal);
    addToCanvas (*more, { 362, 412, 46, 26 });
    more->onClick = [this] { advanced->setVisible (more->getToggleState()); };
}

SingItBackEditor::~SingItBackEditor() = default;

void SingItBackEditor::setPreview (bool singing, int syllable, bool advancedOpen)
{
    static_cast<SugarMascot&> (getMascot()).setPreview (singing, syllable);
    previewDegree = singing ? 4 : -1; // Mi / nothing
    more->setToggleState (advancedOpen, juce::dontSendNotification);
    advanced->setVisible (advancedOpen);
}

void SingItBackEditor::editorTick (double dt)
{
    auto& tel = proc.getDsp().getTelemetry();
    // the note being sung, in the scale bar
    const bool voiced = previewDegree >= -1 ? previewDegree >= 0 : tel.voicedFlag.load (std::memory_order_relaxed);
    scaleBar->setDegree (previewDegree >= -1 ? previewDegree : tel.degreePitchClass.load (std::memory_order_relaxed), voiced ? 1.0f : 0.0f);
    // the knob breathes with the crowd's singing
    float sing = 0.0f;
    for (auto& l : tel.voiceLevel)
        sing = juce::jmax (sing, l.load (std::memory_order_relaxed));
    const float target = juce::jlimit (0.0f, 1.0f, 5.0f * sing) * proc.getCrowdParameter().getValue();
    pulse += (target - pulse) * (dt <= 0.0 ? 1.0f : (float) juce::jmin (1.0, dt / 0.25));
    // in steps of 1/8: the big knob repaints when the glow visibly changes,
    // not on every frame
    getKnob().setPulse (std::round (pulse * 8.0f) / 8.0f);
}
} // namespace sib
