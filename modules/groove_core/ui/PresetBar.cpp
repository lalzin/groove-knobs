namespace gk
{
class PresetBar::ArrowButton final : public juce::Button
{
public:
    ArrowButton (const juce::String& title, bool pointsLeft, const Palette& p) : juce::Button (title), left (pointsLeft), palette (p)
    {
        setTitle (title);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (3.0f);
        juce::Path arrow;
        const float w = b.getWidth(), h = b.getHeight();
        // Chunky cartoon arrow: triangle head + short shaft
        arrow.startNewSubPath (0.0f, h * 0.5f);
        arrow.lineTo (w * 0.55f, 0.0f);
        arrow.lineTo (w * 0.55f, h * 0.3f);
        arrow.lineTo (w, h * 0.3f);
        arrow.lineTo (w, h * 0.7f);
        arrow.lineTo (w * 0.55f, h * 0.7f);
        arrow.lineTo (w * 0.55f, h);
        arrow.closeSubPath();
        auto t = juce::AffineTransform::translation (b.getX(), b.getY());
        if (! left)
            t = juce::AffineTransform::scale (-1.0f, 1.0f, w * 0.5f, 0.0f).translated (b.getX(), b.getY());
        if (down)
            t = t.translated (0.0f, 1.5f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (arrow, t.translated (2.0f, 3.0f));
        const auto c = palette (left ? palette.arrowLeft : palette.arrowRight);
        g.setColour (over ? c.brighter (0.25f) : c);
        g.fillPath (arrow, t);
        g.setColour (palette (palette.outline));
        g.strokePath (arrow, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
        if (hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.text).withAlpha (0.6f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
        }
    }

private:
    bool left;
    const Palette& palette;
};

class PresetBar::NameButton final : public juce::Button
{
public:
    NameButton (const Palette& p) : juce::Button ("Preset"), palette (p)
    {
        setTitle ("Preset");
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (palette (palette.outline).withAlpha (over ? 1.0f : 0.92f));
        g.setFont (GrooveFonts::font (23.0f));
        g.drawFittedText (getButtonText(), b.toNearestInt().reduced (8, 0), juce::Justification::centred, 1, 0.8f);
        if (over || hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.outline).withAlpha (0.35f));
            const float tw = juce::jmin (b.getWidth() - 20.0f, juce::GlyphArrangement::getStringWidth (GrooveFonts::font (23.0f), getButtonText()));
            g.fillRoundedRectangle (juce::Rectangle<float> (tw, 2.5f).withCentre ({ b.getCentreX(), b.getBottom() - 6.0f }), 1.0f);
        }
    }

private:
    const Palette& palette;
};

PresetBar::PresetBar (juce::AudioParameterChoice& p, const Palette& pal)
    : param (p),
      palette (pal),
      attachment (p, [this] (float v)
                  {
                      index = juce::jlimit (0, param.choices.size() - 1, juce::roundToInt (v));
                      name->setButtonText (param.choices[index]);
                      name->setDescription ("Current preset: " + param.choices[index] + ". Click to choose.");
                      repaint();
                  })
{
    prev = std::make_unique<ArrowButton> ("Previous preset", true, palette);
    next = std::make_unique<ArrowButton> ("Next preset", false, palette);
    name = std::make_unique<NameButton> (palette);
    prev->onClick = [this] { step (-1); };
    next->onClick = [this] { step (1); };
    name->onClick = [this] { showMenu(); };
    addAndMakeVisible (*prev);
    addAndMakeVisible (*name);
    addAndMakeVisible (*next);
    setTitle ("Presets");
    attachment.sendInitialUpdate();
}

void PresetBar::resized()
{
    auto b = getLocalBounds();
    prev->setBounds (b.removeFromLeft (64).withSizeKeepingCentre (46, 34).translated (6, 0));
    next->setBounds (b.removeFromRight (64).withSizeKeepingCentre (46, 34).translated (-6, 0));
    plaque = b.toFloat().reduced (4.0f, 7.0f);
    name->setBounds (plaque.toNearestInt());
}

void PresetBar::paint (juce::Graphics& g)
{
    // Drop shadow, tricolour frame, cream plaque
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (plaque.translated (2.0f, 4.0f), 9.0f);

    juce::ColourGradient frame (palette (palette.arrowLeft), plaque.getX(), 0.0f, palette (palette.arrowRight), plaque.getRight(), 0.0f, false);
    frame.addColour (0.5, palette (palette.plaque));
    g.setGradientFill (frame);
    g.fillRoundedRectangle (plaque, 9.0f);

    const auto inner = plaque.reduced (6.0f, 5.0f);
    g.setColour (palette (palette.plaque));
    g.fillRoundedRectangle (inner, 5.0f);

    g.setColour (palette (palette.outline));
    g.drawRoundedRectangle (plaque, 9.0f, 3.0f);
    g.drawRoundedRectangle (inner, 5.0f, 1.5f);

    // rivets
    for (auto p : { inner.getTopLeft().translated (6, 6), inner.getTopRight().translated (-6, 6), inner.getBottomLeft().translated (6, -6),
                    inner.getBottomRight().translated (-6, -6) })
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (p));
}

void PresetBar::select (int newIndex)
{
    const int n = param.choices.size();
    newIndex = ((newIndex % n) + n) % n;
    attachment.setValueAsCompleteGesture ((float) newIndex);
}

void PresetBar::step (int delta)
{
    select (index + delta);
}

void PresetBar::showMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < param.choices.size(); ++i)
        m.addItem (juce::PopupMenu::Item (param.choices[i]).setTicked (i == index).setAction ([safe = juce::Component::SafePointer<PresetBar> (this), i] { if (safe != nullptr) safe->select (i); }));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (name.get()).withMinimumWidth (name->getWidth()));
}
} // namespace gk
