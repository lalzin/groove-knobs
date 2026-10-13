#pragma once

namespace gk
{
/** All UI colours of a Groove Knobs plugin. Each plugin builds its own
    palette (accent, knob body stripes...), the components only read it.
    `saturation` (1 = normal) desaturates everything, e.g. when bypassed. */
struct Palette
{
    juce::Colour background { 0xff0e1a3a };
    juce::Colour backgroundEdge { 0xff08112a };
    juce::Colour surface { 0xff16264f };
    juce::Colour outline { 0xff121212 };
    juce::Colour text { 0xfff5f3ee };
    juce::Colour accent { 0xff3d7bff };
    juce::Colour beret { 0xffe63946 };
    juce::Colour track { juce::Colours::white.withAlpha (0.12f) };
    juce::Colour pointer { 0xff17171a };
    juce::Colour plaque { 0xffede3cc };

    /** Progress arc colours from 0 % to 100 % (2 or 3 stops). */
    std::vector<juce::Colour> arcStops { juce::Colour (0xfff5f3ee), juce::Colour (0xff3d7bff) };
    /** Vertical bands of the knob body (left to right); one colour = plain. */
    std::vector<juce::Colour> knobBands { juce::Colour (0xff16264f) };
    /** Wavy band edges (amplitude in px, 0 = straight). */
    float knobBandWave = 0.0f;
    /** Colour of the knob's "breathing" glow (BigKnob::setPulse). */
    juce::Colour pulse { 0x00000000 };
    /** Preset bar arrows (left, right). */
    juce::Colour arrowLeft { 0xff3d7bff }, arrowRight { 0xff3d7bff };
    /** Preset bar frame gradient; transparent = use the arrow colours. */
    juce::Colour frameLeft { 0x00000000 }, frameRight { 0x00000000 };
    /** Knob end labels; transparent = use the arc's end colours. */
    juce::Colour lowLabel { 0x00000000 }, highLabel { 0x00000000 };

    float saturation = 1.0f;

    /** Applies the current desaturation to a colour. */
    juce::Colour operator() (juce::Colour c) const
    {
        if (saturation >= 0.999f)
            return c;
        return c.withSaturation (c.getSaturation() * saturation);
    }

    /** Colour along the progress arc, t in 0..1. */
    juce::Colour arcColour (float t) const
    {
        if (arcStops.empty())
            return accent;
        if (arcStops.size() == 1)
            return arcStops.front();
        const float x = juce::jlimit (0.0f, 1.0f, t) * (float) (arcStops.size() - 1);
        const int i = juce::jmin ((int) x, (int) arcStops.size() - 2);
        return arcStops[(size_t) i].interpolatedWith (arcStops[(size_t) i + 1], x - (float) i);
    }

    /** The default Groove Knobs palette (spec colours). */
    static Palette grooveDefault() { return {}; }
};
} // namespace gk
