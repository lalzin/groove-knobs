namespace gk
{
//==============================================================================
/** Base-size canvas (420 × 520) scaled by the editor with an AffineTransform,
    so vectors and images are rendered at the physical resolution. */
class OneKnobEditor::Canvas final : public juce::Component
{
public:
    Canvas (const Palette& p, const juce::String& titleText, const juce::String& suite, const juce::String& company, int width) : palette (p)
    {
        // "made by" credit, top left (balances the On/Off button)
        madeByPath = GrooveFonts::textPath ("made by", 10.5f, 40.0f, 22.0f, 0.08f);
        companyPath = GrooveFonts::textPath (company.toUpperCase(), 15.0f, 40.0f, 38.0f, 0.12f);
        setOpaque (true);
        titlePath = GrooveFonts::textPath (titleText.toUpperCase(), 40.0f, (float) width * 0.5f, 56.0f, 0.02f);
        suitePath = GrooveFonts::textPath (suite.toUpperCase(), 13.0f, (float) width * 0.5f, 19.0f, 0.3f);
    }

    /** Draw the (static) background from an image rendered once at the
        physical resolution, instead of the vignette and lettering on every
        repaint of a child (the mascot, the knob) — the UI's main cost. */
    bool cacheBackground = false;

    void paint (juce::Graphics& g) override
    {
        if (! cacheBackground)
        {
            paintBackground (g);
            return;
        }
        const float s = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int w = juce::roundToInt ((float) getWidth() * s), h = juce::roundToInt ((float) getHeight() * s);
        if (! cached.isValid() || cached.getWidth() != w || cached.getHeight() != h || ! juce::exactlyEqual (cachedSaturation, palette.saturation))
        {
            cached = juce::Image (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
            juce::Graphics cg (cached);
            cg.addTransform (juce::AffineTransform::scale ((float) w / (float) getWidth(), (float) h / (float) getHeight()));
            paintBackground (cg);
            cachedSaturation = palette.saturation;
        }
        g.drawImage (cached, getLocalBounds().toFloat());
    }

    void paintBackground (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();

        // Background: soft vignette
        juce::ColourGradient bg (palette (palette.background), b.getCentreX(), b.getHeight() * 0.42f, palette (palette.backgroundEdge), 0.0f,
                                 b.getHeight(), true);
        g.setGradientFill (bg);
        g.fillAll();

        // Suite name, small, above the title
        g.setColour (palette (palette.text).withAlpha (0.55f));
        g.fillPath (suitePath);

        // Credit
        g.setColour (palette (palette.text).withAlpha (0.5f));
        g.fillPath (madeByPath);
        g.setColour (palette (palette.outline));
        g.strokePath (companyPath, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (palette (palette.beret));
        g.fillPath (companyPath);

        // Title: cartoon lettering (shadow, thick outline, cream fill)
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillPath (titlePath, juce::AffineTransform::translation (2.5f, 4.0f));
        g.setColour (palette (palette.outline));
        g.strokePath (titlePath, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (palette (palette.text));
        g.fillPath (titlePath);
    }

private:
    const Palette& palette;
    juce::Path titlePath, suitePath, madeByPath, companyPath;
    juce::Image cached;
    float cachedSaturation = -1.0f;
};

//==============================================================================
/** Invisible link over the "made by" credit: hand cursor, a small underline
    on hover, opens the company's page. */
class OneKnobEditor::CreditLink final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CreditLink (const Palette& p, const juce::String& link) : palette (p), url (link)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (url.toString (false).fromFirstOccurrenceOf ("://", false, false));
        setTitle ("LALZIN website");
        setWantsKeyboardFocus (true);
    }

    void paint (juce::Graphics& g) override
    {
        if (! over && ! hasKeyboardFocus (false))
            return;
        // underline under the company name
        g.setColour (palette (palette.beret).withAlpha (0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) getWidth() - 14.0f, 2.0f).withCentre ({ getWidth() * 0.5f, (float) getHeight() - 3.0f }), 1.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    std::function<void()> onMenu; // right-click: the update-check menu

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu)
            onMenu();
        else if (contains (e.getPosition()))
            url.launchInDefaultBrowser();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k != juce::KeyPress::returnKey && k != juce::KeyPress::spaceKey)
            return false;
        url.launchInDefaultBrowser();
        return true;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    const Palette& palette;
    juce::URL url;
    bool over = false;
};

//==============================================================================
/** "MAJ 0.2.0": a newer version exists; a click opens its download page. */
class OneKnobEditor::UpdateBadge final : public juce::Button
{
public:
    explicit UpdateBadge (const Palette& p) : juce::Button ("Update"), palette (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        onClick = [this] { if (url.isNotEmpty()) juce::URL (url).launchInDefaultBrowser(); };
    }

    void show (const UpdateCheck::Result& r)
    {
        url = r.url;
        setButtonText ("MAJ " + r.latest);
        setTitle ("Update available: " + r.latest);
        setTooltip (juce::String::fromUTF8 ("Nouvelle version ") + r.latest + juce::String::fromUTF8 (" â cliquer pour la tÃ©lÃ©") + "charger");
        setVisible (true);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f).translated (0.0f, down ? 1.0f : 0.0f);
        g.setColour (palette (palette.beret).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.5f);
        g.setColour (palette (palette.text));
        g.setFont (GrooveFonts::font (9.5f).withExtraKerningFactor (0.06f));
        g.drawText (getButtonText(), b, juce::Justification::centred);
    }

private:
    const Palette& palette;
    juce::String url;
};

//==============================================================================
class OneKnobEditor::PowerButton final : public juce::Button
{
public:
    explicit PowerButton (const Palette& p) : juce::Button ("Bypass"), palette (p)
    {
        setClickingTogglesState (true);
        setTitle ("Bypass");
        setTooltip ("Bypass");
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool bypassed = getToggleState();
        g.setColour (palette (bypassed ? palette.surface : palette.beret).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillEllipse (b);
        g.setColour (palette (palette.outline));
        g.drawEllipse (b, 2.5f);

        const auto c = b.getCentre();
        const float r = b.getWidth() * 0.26f;
        juce::Path icon;
        icon.addCentredArc (c.x, c.y, r, r, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        icon.startNewSubPath (c.x, c.y - r * 1.25f);
        icon.lineTo (c.x, c.y - r * 0.15f);
        g.setColour (palette (palette.text).withAlpha (bypassed ? 0.5f : 1.0f));
        g.strokePath (icon, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    const Palette& palette;
};

//==============================================================================
OneKnobEditor::OneKnobEditor (OneKnobProcessor& p, Palette pal, juce::String t, const MascotFactory& makeMascot)
    : AudioProcessorEditor (p),
      proc (p),
      canvasWidth (juce::jmax (100, p.getConfig().editorWidth)),
      canvasHeight (juce::jmax (100, p.getConfig().editorHeight)),
      palette (std::move (pal)),
      lookAndFeel (palette),
      title (std::move (t)),
      mascot (makeMascot (p.getFeatures(), palette)),
      vblank (this, [this] (double ts) { tick (ts); })
{
    setLookAndFeel (&lookAndFeel);

    canvas = std::make_unique<Canvas> (palette, title, proc.getConfig().suiteName, proc.getConfig().companyName, canvasWidth);
    addAndMakeVisible (*canvas);

    const auto& cfg = proc.getConfig();
    const bool customLabels = cfg.knobLowLabel.isNotEmpty() || cfg.knobHighLabel.isNotEmpty();
    knob = std::make_unique<BigKnob> (proc.getAmountParameter(), palette, cfg.bipolar,
                                      customLabels ? cfg.knobLowLabel : (cfg.bipolar ? cfg.lowSideLabel : juce::String()),
                                      customLabels ? cfg.knobHighLabel : (cfg.bipolar ? cfg.highSideLabel : juce::String()));
    if (proc.hasPresetParameter())
        presetBar = std::make_unique<PresetBar> (proc.getPresetParameter(), palette);
    presetTags = std::make_unique<PresetTags> (palette);
    power = std::make_unique<PowerButton> (palette);
    powerAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getBypassParameterObject(), *power);

    canvas->addAndMakeVisible (*mascot);
    canvas->addAndMakeVisible (*knob);
    if (presetBar != nullptr)
        canvas->addAndMakeVisible (*presetBar);
    if (cfg.toggles.empty())
        canvas->addAndMakeVisible (*presetTags);
    else
    {
        // Plugins with toggles show them as clickable chips in the same place
        // (with small knobs, the chips stack under the first one: the ring's
        // opening is too narrow for more than two)
        toggleChips = std::make_unique<ToggleChips> (proc, palette, false, cfg.editorBuildsControls && ! cfg.knobs.empty());
        canvas->addAndMakeVisible (*toggleChips);
    }
    if (! cfg.switches.empty() && cfg.editorBuildsControls)
    {
        switchChips = std::make_unique<ToggleChips> (proc, palette, true);
        canvas->addAndMakeVisible (*switchChips);
    }
    if (! cfg.choices.empty() && cfg.editorBuildsControls)
    {
        choiceChips = std::make_unique<ChoiceChips> (proc, palette);
        canvas->addAndMakeVisible (*choiceChips);
    }
    for (size_t k = 0; k < (cfg.editorBuildsControls ? cfg.knobs.size() : 0); ++k)
    {
        const float along = cfg.knobs.size() > 1 ? juce::jmin (1.0f, (float) k) : 0.5f; // left: start of the arc, right: its end
        smallKnobs.push_back (std::make_unique<SmallKnob> (proc.getKnobParameter ((int) k), palette, cfg.knobs[k].caption, palette.arcColour (along)));
        canvas->addAndMakeVisible (*smallKnobs.back());
    }
    canvas->addAndMakeVisible (*power);
    if (cfg.companyUrl.isNotEmpty())
    {
        creditLink = std::make_unique<CreditLink> (palette, cfg.companyUrl);
        canvas->addAndMakeVisible (*creditLink);
        creditLink->setBounds (2, 10, 76, 40); // over "made by LALZIN"
    }
    if (cfg.version.isNotEmpty() && cfg.updateSlug.isNotEmpty())
    {
        updateBadge = std::make_unique<UpdateBadge> (palette);
        canvas->addChildComponent (*updateBadge);
        updateBadge->setBounds (80, 9, 66, 17); // right of "made by", above the title
        juce::Component::SafePointer<UpdateBadge> safe (updateBadge.get());
        updateCheck = std::make_unique<UpdateCheck> (cfg.updateSlug, cfg.version, cfg.updateUrl, [safe] (const UpdateCheck::Result& r)
                                                     {
                                                         if (safe != nullptr)
                                                             safe->show (r);
                                                     });
        if (creditLink != nullptr)
            creditLink->onMenu = [this]
            {
                juce::PopupMenu m;
                m.addSectionHeader (proc.getConfig().productName + " " + proc.getConfig().version);
                m.addItem (juce::String::fromUTF8 ("VÃ©rifier les mises Ã  jour (une fois par jour)"), true, UpdateCheck::isEnabled(),
                           [] { UpdateCheck::setEnabled (! UpdateCheck::isEnabled()); });
                m.addItem (juce::String::fromUTF8 ("VÃ©rifier maintenant"), UpdateCheck::isEnabled(), false,
                           [this] { if (updateCheck != nullptr) updateCheck->checkNow(); });
                m.addItem ("lalzin.com", [this] { juce::URL (proc.getConfig().companyUrl).launchInDefaultBrowser(); });
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (creditLink.get()));
            };
    }

    // Layout on the base canvas (see spec §8.1)
    canvas->setBounds (0, 0, canvasWidth, canvasHeight);
    mascot->setBounds (60, 60, 300, 170);
    // 262 = 230 (ring diameter) + room for the glow and the excitement dashes
    knob->setBounds (juce::Rectangle<int> (262, 262).withCentre ({ 210, 345 }));
    if (presetBar != nullptr)
        presetBar->setBounds (0, 470, canvasWidth, 50);
    presetTags->setBounds (juce::Rectangle<int> (200, 22).withCentre ({ 210, 448 })); // in the ring's opening
    if (toggleChips != nullptr)
        toggleChips->setBounds (juce::Rectangle<int> (200, 26).withCentre ({ 210, 448 }));
    if (switchChips != nullptr)
        switchChips->setBounds (8, 300, 68, 60); // left of the knob
    if (choiceChips != nullptr)
        choiceChips->setBounds (336, 290, 78, 90); // right of the knob
    if (! smallKnobs.empty())
    {
        // Small knobs: the first left of the big knob, the second right of
        // it (with the choices, if any, underneath)
        smallKnobs[0]->setBounds (10, 296, 64, 68);
        if (smallKnobs.size() > 1)
            smallKnobs[1]->setBounds (346, 296, 64, 68);
        if (smallKnobs.size() > 2)
            smallKnobs[2]->setBounds (346, 226, 64, 68); // a third one above the second (e.g. the volume)
        if (choiceChips != nullptr)
            choiceChips->setBounds (338, 368, 78, 40);
        if (toggleChips != nullptr)
            toggleChips->setBounds (8, 372, 70, 80);
    }
    power->setBounds (canvasWidth - 44, 12, 32, 32);

    const double frameRate = 60.0;
    uiAmount.prepare (frameRate, 0.12);
    uiBypass.prepare (frameRate, 0.35);
    updateFromParameters (0.0, true);

    // Read the saved size first: setting the limits triggers resized().
    const float scale = juce::jlimit (minScale, maxScale, proc.editorScale.load());
    setResizable (true, true);
    setResizeLimits (juce::roundToInt ((float) canvasWidth * minScale), juce::roundToInt ((float) canvasHeight * minScale),
                     juce::roundToInt ((float) canvasWidth * maxScale), juce::roundToInt ((float) canvasHeight * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) canvasWidth / (double) canvasHeight);

    setSize (juce::roundToInt ((float) canvasWidth * scale), juce::roundToInt ((float) canvasHeight * scale));
}

OneKnobEditor::~OneKnobEditor()
{
    setLookAndFeel (nullptr);
}

void OneKnobEditor::resized()
{
    const float scale = juce::jlimit (minScale, maxScale, (float) getWidth() / (float) canvasWidth);
    canvas->setTransform (juce::AffineTransform::scale (scale));
    proc.editorScale.store (scale);
}

void OneKnobEditor::updateFromParameters (double dt, bool snap)
{
    const float amount = proc.getAmountParameter().getValue();
    const float bypass = proc.getBypassParameterObject().get() ? 1.0f : 0.0f;
    if (snap)
    {
        uiAmount.snap (amount);
        uiBypass.snap (bypass);
    }
    else
    {
        uiAmount.setTarget (amount);
        uiBypass.setTarget (bypass);
        const int steps = juce::jmax (1, juce::roundToInt (dt * 60.0));
        for (int i = 0; i < steps; ++i)
        {
            uiAmount.next();
            uiBypass.next();
        }
    }

    // Whole UI desaturates to 40 % when bypassed
    palette.saturation = 1.0f - 0.6f * uiBypass.getCurrent();
    if (std::abs (palette.saturation - lastSaturation) > 0.002f)
    {
        lastSaturation = palette.saturation;
        canvas->repaint();
    }

    // The mascot follows the signed knob position (side + distance from rest)
    const float macro = proc.macroFor (uiAmount.getCurrent());
    if (snap)
        mascot->snap (macro, uiBypass.getCurrent());
    else
        mascot->advance (dt, macro, uiBypass.getCurrent());

    knob->setBeatPhase (mascot->getState().playing ? mascot->getState().beatPhase : 0.0f);

    // Tags: the active side's signature (bipolar), nothing at the centre
    const int preset = proc.hasPresetParameter() ? proc.getPresetParameter().getIndex() : 0;
    const float target = proc.macroFor (proc.getAmountParameter().getValue());
    const int side = proc.getConfig().bipolar ? (target < 0.0f ? -1 : (target > 0.0f ? 1 : 0)) : 1;
    if (preset != shownPreset || side != shownSide)
    {
        shownPreset = preset;
        shownSide = side;
        const auto& json = proc.getPresets()[preset].json;
        if (! proc.getConfig().bipolar)
            presetTags->setTags (json.getProperty ("tags", {}));
        else if (side == 0)
            presetTags->setTags ({});
        else
            presetTags->setTags (json.getProperty (side < 0 ? "low" : "high", {}).getProperty ("tags", {}));
    }

    editorTick (dt);
}

void OneKnobEditor::tick (double timestampSeconds)
{
    const double dt = lastTimestamp < 0.0 ? 1.0 / 60.0 : timestampSeconds - lastTimestamp;
    lastTimestamp = timestampSeconds;
    updateFromParameters (dt, false);
}

void OneKnobEditor::snapForSnapshot()
{
    updateFromParameters (0.0, true);
    lastSaturation = palette.saturation;
    canvas->repaint();
}
} // namespace gk

namespace gk
{
void OneKnobEditor::addToCanvas (juce::Component& c, juce::Rectangle<int> bounds)
{
    canvas->addAndMakeVisible (c);
    c.setBounds (bounds);
}
} // namespace gk

namespace gk
{
void OneKnobEditor::setBackgroundCached (bool cached)
{
    canvas->cacheBackground = cached;
    canvas->repaint();
}
} // namespace gk

namespace gk
{
void OneKnobEditor::placeMascotAndKnob (juce::Rectangle<int> mascotBounds, juce::Rectangle<int> knobBounds)
{
    mascot->setBounds (mascotBounds);
    knob->setBounds (knobBounds);
    presetTags->setVisible (false);
}
} // namespace gk
