#include "FrenchFilterMascot.h"
#include <FrenchFilterArt.h>

namespace ff
{
FrenchFilterMascot::FrenchFilterMascot (gk::AudioFeatures& f, const gk::Palette& p) : MascotComponent (f, p)
{
    setFrames (loadFrames());
    setPoses (poses());

    // Eighth note, ~16 px tall, origin at the note head
    note.addEllipse (-5.0f, -3.5f, 10.0f, 7.0f);
    note.addRectangle (3.2f, -17.0f, 2.0f, 15.0f);
    note.startNewSubPath (5.2f, -17.0f);
    note.quadraticTo (11.0f, -13.0f, 9.0f, -7.0f);
    note.quadraticTo (9.5f, -12.0f, 5.2f, -13.0f);
    note.closeSubPath();
}

std::vector<juce::Image> FrenchFilterMascot::loadFrames()
{
    // mascot_0.png, mascot_1.png ... until one is missing
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "mascot_" + juce::String (i) + "_png";
        const char* data = FFArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

gk::MascotComponent::Poses FrenchFilterMascot::poses()
{
    // Frame numbers: see scripts/make_mascot_frames.sh
    //  0 worried, hands on hips   1 slumped, sad          2 skeptical side-eye
    //  3 alert                    4 smirk, hands on hips   5 wink + pointing
    //  6 dancing, arms out        7 dancing, eyes closed   8 dancing with notes
    //  9 pointing, laughing      10 fists up              11 hands over the ears
    // 12 star eyes, jumping      13 arms up, jumping      14 finger guns
    // 15 finger guns, walking    16 sunglasses
    Poses p;
    p.groove = { 6, 5, 7, 9, 8 };      // centre + music: he dances
    p.calm = 4;                        // centre, silence
    p.lowLadder = { 3, 2, 0, 1 };      // low-pass: alert → skeptical → worried → muffled and sad
    p.highLadder = { 3, 10, 11 };      // high-pass: alert → hyped by the riser → ears covered
    p.drop = { 12, 13, 14, 15, 16 };   // back to the centre: the drop
    p.sleep = 1;
    return p;
}

void FrenchFilterMascot::paintOverlay (juce::Graphics& g, const State& s, juce::Rectangle<float> figure)
{
    // Music notes drifting up around the head during the drop euphoria.
    const float show = s.joy * (1.0f - s.bypass);
    if (show <= 0.0f)
        return;

    const float ph = s.playing ? s.beatPhase : (float) gk::dspmath::frac (s.time * 0.8);
    for (int i = 0; i < 2; ++i)
    {
        const float side = i == 0 ? -1.0f : 1.0f;
        const float p = (float) gk::dspmath::frac (ph + 0.5f * (float) i);
        const float x = figure.getCentreX() + side * (figure.getWidth() * 0.62f + 10.0f * p);
        const float y = figure.getY() + figure.getHeight() * 0.35f - 34.0f * p;
        const auto c = i == 0 ? palette.accent : palette.beret;
        g.setColour (palette (c).withAlpha (show * (1.0f - p)));
        g.fillPath (note, juce::AffineTransform::rotation (side * 0.2f).scaled (0.9f + 0.3f * p).translated (x, y));
    }
}
} // namespace ff
