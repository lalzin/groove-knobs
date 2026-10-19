namespace gk
{
SmallKnob::SmallKnob (juce::RangedAudioParameter& parameter, const Palette& palette, juce::String captionText, juce::Colour c)
    : BigKnob (parameter, palette), pal (palette), caption (std::move (captionText)), colour (c)
{
    setTitle (caption);
}

void SmallKnob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float captionH = 14.0f;
    const auto knobArea = b.withTrimmedBottom (captionH);
    const float r = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.5f - 4.0f;
    const auto c = knobArea.getCentre();
    const float v = getValue();
    const float a0 = startAngle, a1 = startAngle + v * (endAngle - startAngle);

    // track and value arc
    juce::Path track, arc;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (pal (pal.track));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (v > 0.001f)
    {
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (pal (colour));
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float br = r - 6.0f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c.translated (0.0f, 2.5f)));
    g.setGradientFill (juce::ColourGradient (pal (pal.surface).brighter (0.25f), c.x, c.y - br, pal (pal.surface).darker (0.4f), c.x, c.y + br, false));
    g.fillEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c));
    g.setColour (pal (pal.outline));
    g.drawEllipse (juce::Rectangle<float> (2.0f * br, 2.0f * br).withCentre (c), 2.5f);
    // pointer
    const auto tip = c + juce::Point<float> (std::sin (a1), -std::cos (a1)) * (br - 3.0f);
    g.setColour (pal (pal.text));
    g.drawLine ({ c + (tip - c) * 0.25f, tip }, 3.0f);

    // caption, or the value while hovered / dragged
    const bool showValue = isMouseOverOrDragging();
    g.setFont (GrooveFonts::font (showValue ? 12.0f : 11.0f).withExtraKerningFactor (0.08f));
    g.setColour (pal (showValue ? pal.text : colour).withAlpha (0.95f));
    g.drawText (showValue ? getParameter().getCurrentValueAsText() : caption, b.withTop (b.getBottom() - captionH), juce::Justification::centred);
    if (hasKeyboardFocus (false))
    {
        g.setColour (pal (pal.text).withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r + 8.0f, 2.0f * r + 8.0f).withCentre (c), 1.0f);
    }
}
} // namespace gk
