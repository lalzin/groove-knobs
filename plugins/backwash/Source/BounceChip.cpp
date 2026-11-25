#include "BounceChip.h"

namespace bw
{
namespace
{
    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
} // namespace

BounceChip::BounceChip (BackwashProcessor& p, const gk::Palette& pal) : live (p), palette (pal)
{
    setTitle ("WAV");
    updateTooltip();
    startTimerHz (5);
}

void BounceChip::timerCallback()
{
    if (forced)
        return;
    const bool now = live.getBackwashDsp().getTelemetry().liveMemoryComplete.load();
    if (now != ready)
    {
        ready = now;
        updateTooltip();
        repaint();
    }
}

void BounceChip::updateTooltip()
{
    setMouseCursor (ready ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (ready)
        setTooltip (fr ("Glisse-moi dans ton set : les souffles de la mesure entendue, en WAV, avec les r\xc3\xa9glages en cours. "
                        "Pose-le sur une nouvelle piste, sur une barre de mesure, en boucle : tu peux retirer Backwash, plus aucune latence."));
    else
        setTooltip (fr ("WAV : lance la lecture. D\xc3\xa8s que Backwash a entendu une mesure (ton clap, ta snare\xe2\x80\xa6), "
                        "glisse ses souffles d'ici dans ton set."));
}

void BounceChip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto aqua = palette.accent, coral = palette.highLabel;

    g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
    g.setColour (palette (aqua).withAlpha (0.85f));
    g.drawText ("WAV", b.removeFromTop (captionHeight), juce::Justification::centred);

    const auto r = b.withSizeKeepingCentre (b.getWidth(), chipHeight).reduced (1.5f).translated (0.0f, dragging ? 1.0f : 0.0f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.5f), rad);

    g.setFont (gk::GrooveFonts::font (11.5f).withExtraKerningFactor (0.05f));
    if (ready)
    {
        g.setColour (palette (coral).brighter (over ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, rad);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (r.reduced (0.75f), rad, 1.5f);
        g.drawText ("DRAG", r.withTrimmedRight (12.0f), juce::Justification::centred);
        // a small arrow, out of the plugin
        juce::Path arrow;
        const float cx = r.getRight() - 12.0f, cy = r.getCentreY();
        arrow.startNewSubPath (cx - 2.5f, cy + 2.5f);
        arrow.lineTo (cx + 2.5f, cy - 2.5f);
        arrow.startNewSubPath (cx - 1.0f, cy - 2.5f);
        arrow.lineTo (cx + 2.5f, cy - 2.5f);
        arrow.lineTo (cx + 2.5f, cy + 1.0f);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // waiting for a bar: dashed, dim
        g.setColour (palette (palette.outline).withAlpha (0.9f));
        g.fillRoundedRectangle (r, rad);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (0.75f), rad);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (palette (aqua).withAlpha (0.55f));
        g.fillPath (dashed);
        g.setColour (palette (palette.text).withAlpha (0.45f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xa2 \xe2\x80\xa2 \xe2\x80\xa2"), r, juce::Justification::centred);
    }
}

void BounceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! ready || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    repaint();
    // Rendered now, with the settings of this moment (a fraction of a second)
    const auto file = bounce::writeLoop (live, folder);
    if (! file.existsAsFile())
    {
        dragging = false;
        repaint();
        return;
    }
    lastFile = file;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                 [safe = juce::Component::SafePointer<BounceChip> (this)]
                                                                 {
                                                                     if (safe != nullptr)
                                                                     {
                                                                         safe->dragging = false;
                                                                         safe->repaint();
                                                                     }
                                                                 });
}

void BounceChip::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging && contains (e.getPosition()) && lastFile.existsAsFile())
        lastFile.revealToUser();
    dragging = false;
    repaint();
}
} // namespace bw
