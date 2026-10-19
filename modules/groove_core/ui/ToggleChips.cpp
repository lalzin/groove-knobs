namespace gk
{
class ToggleChips::Chip final : public juce::Button
{
public:
    Chip (const juce::String& label, juce::Colour litColour, const Palette& p) : juce::Button (label), lit (litColour), palette (p)
    {
        setClickingTogglesState (true);
        setTitle (label);
        setWantsKeyboardFocus (true);
    }

    float preferredWidth() const { return juce::GlyphArrangement::getStringWidth (font(), getName()) + 18.0f; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.5f).translated (0.0f, down ? 1.0f : 0.0f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = getToggleState();

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (1.5f, 2.5f), rad);
        g.setColour (on ? palette (lit) : palette (palette.outline).withAlpha (0.85f));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? palette (palette.outline) : palette (lit).withAlpha (over ? 1.0f : 0.75f));
        g.drawRoundedRectangle (r.reduced (0.75f), rad, on ? 2.0f : 1.5f);

        g.setFont (font());
        g.setColour (on ? palette (palette.outline) : palette (palette.text).withAlpha (over ? 1.0f : 0.7f));
        g.drawText (getName(), r, juce::Justification::centred);

        if (hasKeyboardFocus (false))
        {
            g.setColour (palette (palette.text).withAlpha (0.6f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), rad + 1.0f, 1.0f);
        }
        if (warning)
        {
            const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getRight() - 4.0f, r.getY() + 3.0f });
            g.setColour (palette (palette.outline));
            g.fillEllipse (dot.expanded (1.5f));
            g.setColour (palette (juce::Colour (0xffffc43d)));
            g.fillEllipse (dot);
        }
    }

    bool warning = false;
    juce::String savedTooltip;

private:
    static juce::Font font() { return GrooveFonts::font (11.5f).withExtraKerningFactor (0.05f); }
    juce::Colour lit;
    const Palette& palette;
};

ToggleChips::ToggleChips (OneKnobProcessor& proc, const Palette& palette, bool showSwitches, bool column) : vertical (showSwitches || column)
{
    // The preset toggles (a row), or the global switches (a column)
    const auto& specs = showSwitches ? proc.getConfig().switches : proc.getConfig().toggles;
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const float t = specs.size() > 1 ? (showSwitches ? (float) i / (float) (specs.size() - 1) : 0.35f + 0.65f * (float) i / (float) (specs.size() - 1))
                                         : 0.6f;
        auto chip = std::make_unique<Chip> (specs[i].chipLabel, palette.arcColour (t), palette);
        chip->setTooltip (specs[i].name);
        addAndMakeVisible (*chip);
        auto& param = showSwitches ? proc.getSwitchParameter ((int) i) : proc.getToggleParameter ((int) i);
        attachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (param, *chip));
        chips.push_back (std::move (chip));
    }
    setTitle (showSwitches ? "Modes" : "Options");
}

void ToggleChips::setWarning (int index, bool on, const juce::String& tooltip)
{
    if (! juce::isPositiveAndBelow (index, (int) chips.size()))
        return;
    auto& c = *chips[(size_t) index];
    if (c.warning == on)
        return;
    c.warning = on;
    if (on)
        c.savedTooltip = c.getTooltip();
    c.setTooltip (on ? tooltip : (c.savedTooltip.isNotEmpty() ? c.savedTooltip : c.getName()));
    c.repaint();
}

void ToggleChips::setChipTooltip (int index, const juce::String& tooltip)
{
    if (juce::isPositiveAndBelow (index, (int) chips.size()))
        chips[(size_t) index]->setTooltip (tooltip);
}

void ToggleChips::setChipDimmed (int index, bool dimmed)
{
    if (juce::isPositiveAndBelow (index, (int) chips.size()))
        chips[(size_t) index]->setAlpha (dimmed ? 0.45f : 1.0f);
}

void ToggleChips::resized()
{
    constexpr float gap = 6.0f, h = 21.0f;
    if (vertical)
    {
        float y = 0.0f;
        for (auto& c : chips)
        {
            const float w = c->preferredWidth();
            c->setBounds (juce::Rectangle<float> (((float) getWidth() - w) * 0.5f, y, w, h).toNearestInt());
            y += h + gap;
        }
        return;
    }
    float total = -gap;
    for (auto& c : chips)
        total += c->preferredWidth() + gap;
    float x = ((float) getWidth() - total) * 0.5f;
    for (auto& c : chips)
    {
        const float w = c->preferredWidth();
        c->setBounds (juce::Rectangle<float> (x, ((float) getHeight() - h) * 0.5f, w, h).toNearestInt());
        x += w + gap;
    }
}
} // namespace gk
