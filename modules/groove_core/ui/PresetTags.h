#pragma once

namespace gk
{
/** Small pills showing what makes the current preset special (its "tags"
    array in the preset JSON), drawn in the opening at the bottom of the
    knob ring. */
class PresetTags : public juce::Component
{
public:
    explicit PresetTags (const Palette& p) : palette (p)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setTags (const juce::var& tagsArray)
    {
        juce::StringArray t;
        if (const auto* arr = tagsArray.getArray())
            for (const auto& v : *arr)
                t.add (v.toString().toUpperCase());
        if (t == tags)
            return;
        tags = t;
        setDescription (tags.joinIntoString (", "));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (tags.isEmpty())
            return;
        const auto font = GrooveFonts::font (11.5f).withExtraKerningFactor (0.06f);
        constexpr float pad = 7.0f, gap = 5.0f, h = 17.0f;
        std::vector<float> widths;
        float total = -gap;
        for (const auto& t : tags)
        {
            widths.push_back (juce::GlyphArrangement::getStringWidth (font, t) + 2.0f * pad);
            total += widths.back() + gap;
        }
        float x = ((float) getWidth() - total) * 0.5f;
        const float y = ((float) getHeight() - h) * 0.5f;
        g.setFont (font);
        for (int i = 0; i < tags.size(); ++i)
        {
            const juce::Rectangle<float> r (x, y, widths[(size_t) i], h);
            g.setColour (palette (palette.outline).withAlpha (0.85f));
            g.fillRoundedRectangle (r, h * 0.5f);
            g.setColour (palette (palette.arcColour ((float) (i + 1) / (float) (tags.size() + 1))).withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.75f), h * 0.5f, 1.5f);
            g.setColour (palette (palette.text));
            g.drawText (tags[i], r, juce::Justification::centred);
            x += widths[(size_t) i] + gap;
        }
    }

private:
    const Palette& palette;
    juce::StringArray tags;
};
} // namespace gk
