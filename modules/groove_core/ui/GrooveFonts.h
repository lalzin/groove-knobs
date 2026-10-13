#pragma once

namespace gk
{
/** The suite's display font (Lilita One, SIL OFL 1.1, embedded as BinaryData
    with its licence). Falls back to the system font if loading fails. */
struct GrooveFonts
{
    static juce::Typeface::Ptr display();

    static juce::Font font (float height)
    {
        if (auto tf = display())
            return juce::Font (juce::FontOptions (tf).withHeight (height));
        return juce::Font (juce::FontOptions (height, juce::Font::bold));
    }

    /** Text as a path (for outlined cartoon lettering), baseline at y. */
    static juce::Path textPath (const juce::String& text, float height, float centreX, float baselineY, float kerning = 0.0f)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font (height).withExtraKerningFactor (kerning), text, 0.0f, 0.0f);
        juce::Path p;
        ga.createPath (p);
        const auto b = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (centreX - b.getCentreX(), baselineY));
        return p;
    }
};
} // namespace gk
