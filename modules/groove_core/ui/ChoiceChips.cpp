namespace gk
{
ChoiceSelector::ChoiceSelector (juce::AudioParameterChoice& p, const juce::String& captionText, juce::Colour colour, const Palette& pal)
    : param (p), caption (captionText), lit (colour), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
{
    setTitle (caption);
    setTooltip (p.getName (64));
    setWantsKeyboardFocus (true);
    attachment.sendInitialUpdate();
}

juce::String ChoiceSelector::nameOf (int index) const
{
    return juce::isPositiveAndBelow (index, displayNames.size()) ? displayNames[index] : param.choices[index];
}

void ChoiceSelector::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setFont (GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
    g.setColour (palette (lit).withAlpha (0.85f));
    g.drawText (caption, b.removeFromTop (captionHeight), juce::Justification::centred);

    const auto r = b.withSizeKeepingCentre (b.getWidth(), chipHeight).reduced (1.5f).translated (0.0f, down ? 1.0f : 0.0f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.5f), rad);
    g.setColour (palette (palette.outline).withAlpha (0.9f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (palette (lit).withAlpha (over ? 1.0f : 0.8f));
    g.drawRoundedRectangle (r.reduced (0.75f), rad, 1.5f);

    g.setFont (GrooveFonts::font (11.5f).withExtraKerningFactor (0.05f));
    g.setColour (palette (palette.text));
    g.drawText (nameOf (param.getIndex()), r.withTrimmedRight (10.0f), juce::Justification::centred);
    // small "open" triangle
    juce::Path tri;
    const float cx = r.getRight() - 10.0f, cy = r.getCentreY();
    tri.addTriangle (cx - 3.0f, cy - 1.5f, cx + 3.0f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (palette (lit));
    g.fillPath (tri);
    if (hasKeyboardFocus (false))
    {
        g.setColour (palette (palette.text).withAlpha (0.6f));
        g.drawRoundedRectangle (r.expanded (1.0f), rad + 1.0f, 1.0f);
    }
}

void ChoiceSelector::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    repaint();
    if (contains (e.getPosition()))
        showMenu();
}

void ChoiceSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! juce::exactlyEqual (w.deltaY, 0.0f))
        step (w.deltaY > 0.0f ? -1 : 1);
}

bool ChoiceSelector::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
        showMenu();
    else if (k == juce::KeyPress::upKey || k == juce::KeyPress::leftKey)
        step (-1);
    else if (k == juce::KeyPress::downKey || k == juce::KeyPress::rightKey)
        step (1);
    else
        return false;
    return true;
}

void ChoiceSelector::step (int delta)
{
    const int n = param.choices.size();
    set ((param.getIndex() + delta + n) % n);
}

void ChoiceSelector::set (int index)
{
    attachment.setValueAsCompleteGesture ((float) index);
}

void ChoiceSelector::showMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < param.choices.size(); ++i)
        menu.addItem (i + 1, nameOf (i), true, i == param.getIndex());
    juce::Component::SafePointer<ChoiceSelector> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->set (result - 1);
                        });
}

ChoiceChips::ChoiceChips (OneKnobProcessor& proc, const Palette& palette)
{
    const auto& specs = proc.getConfig().choices;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const float t = specs.size() > 1 ? (float) i / (float) (specs.size() - 1) : 0.5f;
        auto s = std::make_unique<ChoiceSelector> (proc.getChoiceParameter ((int) i), specs[i].caption, palette.arcColour (t), palette);
        addAndMakeVisible (*s);
        selectors.push_back (std::move (s));
    }
    setTitle ("Timing");
}

ChoiceChips::~ChoiceChips() = default;

void ChoiceChips::resized()
{
    const int h = (int) (ChoiceSelector::captionHeight + ChoiceSelector::chipHeight) + 2;
    int y = 0;
    for (auto& s : selectors)
    {
        s->setBounds (0, y, getWidth(), h);
        y += h + 6;
    }
}
} // namespace gk
