namespace gk
{
//==============================================================================
/** Base-size canvas (420 × 520) scaled by the editor with an AffineTransform,
    so vectors and images are rendered at the physical resolution. */
class OneKnobEditor::Canvas final : public juce::Component
{
public:
    Canvas (const Palette& p, const juce::String& titleText, const juce::String& suite, const juce::String& company) : palette (p)
    {
        // "made by" credit, top left (balances the On/Off button)
        madeByPath = GrooveFonts::textPath ("made by", 10.5f, 40.0f, 22.0f, 0.08f);
        companyPath = GrooveFonts::textPath (company.toUpperCase(), 15.0f, 40.0f, 38.0f, 0.12f);
        setOpaque (true);
        titlePath = GrooveFonts::textPath (titleText.toUpperCase(), 40.0f, (float) baseWidth * 0.5f, 56.0f, 0.02f);
        suitePath = GrooveFonts::textPath (suite.toUpperCase(), 13.0f, (float) baseWidth * 0.5f, 19.0f, 0.3f);
    }

    void paint (juce::Graphics& g) override
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
      palette (std::move (pal)),
      lookAndFeel (palette),
      title (std::move (t)),
      mascot (makeMascot (p.getFeatures(), palette)),
      vblank (this, [this] (double ts) { tick (ts); })
{
    setLookAndFeel (&lookAndFeel);

    canvas = std::make_unique<Canvas> (palette, title, proc.getConfig().suiteName, proc.getConfig().companyName);
    addAndMakeVisible (*canvas);

    const auto& cfg = proc.getConfig();
    const bool customLabels = cfg.knobLowLabel.isNotEmpty() || cfg.knobHighLabel.isNotEmpty();
    knob = std::make_unique<BigKnob> (proc.getAmountParameter(), palette, cfg.bipolar,
                                      customLabels ? cfg.knobLowLabel : (cfg.bipolar ? cfg.lowSideLabel : juce::String()),
                                      customLabels ? cfg.knobHighLabel : (cfg.bipolar ? cfg.highSideLabel : juce::String()));
    presetBar = std::make_unique<PresetBar> (proc.getPresetParameter(), palette);
    presetTags = std::make_unique<PresetTags> (palette);
    power = std::make_unique<PowerButton> (palette);
    powerAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getBypassParameterObject(), *power);

    canvas->addAndMakeVisible (*mascot);
    canvas->addAndMakeVisible (*knob);
    canvas->addAndMakeVisible (*presetBar);
    if (cfg.toggles.empty())
        canvas->addAndMakeVisible (*presetTags);
    else
    {
        // Plugins with toggles show them as clickable chips in the same place
        toggleChips = std::make_unique<ToggleChips> (proc, palette);
        canvas->addAndMakeVisible (*toggleChips);
    }
    canvas->addAndMakeVisible (*power);

    // Layout on the base canvas (see spec §8.1)
    canvas->setBounds (0, 0, baseWidth, baseHeight);
    mascot->setBounds (60, 60, 300, 170);
    // 262 = 230 (ring diameter) + room for the glow and the excitement dashes
    knob->setBounds (juce::Rectangle<int> (262, 262).withCentre ({ 210, 345 }));
    presetBar->setBounds (0, 470, baseWidth, 50);
    presetTags->setBounds (juce::Rectangle<int> (200, 22).withCentre ({ 210, 448 })); // in the ring's opening
    if (toggleChips != nullptr)
        toggleChips->setBounds (juce::Rectangle<int> (200, 26).withCentre ({ 210, 448 }));
    power->setBounds (baseWidth - 44, 12, 32, 32);

    const double frameRate = 60.0;
    uiAmount.prepare (frameRate, 0.12);
    uiBypass.prepare (frameRate, 0.35);
    updateFromParameters (0.0, true);

    // Read the saved size first: setting the limits triggers resized().
    const float scale = juce::jlimit (minScale, maxScale, proc.editorScale.load());
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (baseWidth * minScale), juce::roundToInt (baseHeight * minScale), juce::roundToInt (baseWidth * maxScale),
                     juce::roundToInt (baseHeight * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));
}

OneKnobEditor::~OneKnobEditor()
{
    setLookAndFeel (nullptr);
}

void OneKnobEditor::resized()
{
    const float scale = juce::jlimit (minScale, maxScale, (float) getWidth() / (float) baseWidth);
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
    const int preset = proc.getPresetParameter().getIndex();
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
