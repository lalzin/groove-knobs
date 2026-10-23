namespace gk
{
namespace
{
    class KnobValueInterface final : public juce::AccessibilityValueInterface
    {
    public:
        explicit KnobValueInterface (juce::RangedAudioParameter& p) : param (p) {}
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return param.getValue(); }
        void setValue (double v) override
        {
            param.beginChangeGesture();
            param.setValueNotifyingHost ((float) juce::jlimit (0.0, 1.0, v));
            param.endChangeGesture();
        }
        juce::String getCurrentValueAsString() const override { return param.getCurrentValueAsText(); }
        void setValueAsString (const juce::String& s) override { setValue (param.getValueForText (s)); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }

    private:
        juce::RangedAudioParameter& param;
    };
} // namespace

BigKnob::BigKnob (juce::RangedAudioParameter& p, const Palette& pal, bool isBipolar, juce::String lo, juce::String hi)
    : param (p),
      palette (pal),
      bipolar (isBipolar),
      lowLabel (std::move (lo)),
      highLabel (std::move (hi)),
      attachment (p, [this] (float plain)
                  {
                      value = param.convertTo0to1 (plain);
                      repaint();
                  })
{
    setTitle (param.getName (64));
    setDescription ("Turn to filter. Double-click to reset.");
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    attachment.sendInitialUpdate();
}

void BigKnob::setBeatPhase (float phase)
{
    if (intensity() > 0.8f && std::abs (phase - beatPhase) > 0.01f)
    {
        beatPhase = phase;
        repaint();
    }
}

void BigKnob::setSkinLift (float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (std::abs (amount - skinLift) > 0.004f || (juce::exactlyEqual (amount, 0.0f) && skinLift > 0.0f))
    {
        skinLift = amount;
        repaint();
    }
}

void BigKnob::setPulse (float amount)
{
    amount = dspmath::clamp01 (amount);
    if (std::abs (amount - pulse) > 0.004f || (juce::exactlyEqual (amount, 0.0f) && pulse > 0.0f))
    {
        pulse = amount;
        repaint();
    }
}

void BigKnob::resized()
{
    rebuildPaths();
}

bool BigKnob::hitTest (int x, int y)
{
    return centre.getDistanceFrom ({ (float) x, (float) y }) <= ringRadius + 12.0f;
}

void BigKnob::rebuildPaths()
{
    const auto b = getLocalBounds().toFloat();
    centre = b.getCentre();
    const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    ringRadius = outer - 7.0f - 16.0f; // ring centre line (14 px ring), room for glow + lines
    bodyRadius = ringRadius - 22.0f;

    trackPath.clear();
    trackPath.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);

    // Body: vertical colour bands (clipped to the disc at paint time)
    bandPaths.clear();
    const auto disc = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    const int nb = juce::jmax (1, (int) palette.knobBands.size());
    for (int i = 0; i < nb; ++i)
    {
        juce::Path band;
        const float x0 = disc.getX() + disc.getWidth() * (float) i / (float) nb, x1 = x0 + disc.getWidth() / (float) nb + 0.5f;
        const float wave = palette.knobBandWave;
        if (wave <= 0.0f)
            band.addRectangle (x0, disc.getY(), x1 - x0, disc.getHeight());
        else
        {
            // Wavy edges (outer edges pushed out of the disc, clipped anyway)
            auto edge = [&] (float x, float y) { return x + wave * std::sin (y / disc.getHeight() * juce::MathConstants<float>::twoPi * 2.5f); };
            const float left = i == 0 ? x0 - 2.0f * wave : x0, right = i == nb - 1 ? x1 + 2.0f * wave : x1;
            band.startNewSubPath (edge (left, disc.getY()), disc.getY());
            for (float y = disc.getY(); y <= disc.getBottom(); y += 3.0f)
                band.lineTo (edge (left, y), y);
            for (float y = disc.getBottom(); y >= disc.getY(); y -= 3.0f)
                band.lineTo (edge (right, y), y);
            band.closeSubPath();
        }
        bandPaths.push_back (band);
    }
    bodyOutline.clear();
    bodyOutline.addEllipse (disc);

    // Pointer cap: an elongated, slightly pointed "chicken head", drawn at
    // angle 0 (pointing up) and rotated in paint().
    const float len = bodyRadius * 0.92f, wide = bodyRadius * 0.42f;
    capPath.clear();
    capPath.startNewSubPath (0.0f, -len);
    capPath.cubicTo (wide * 0.9f, -len * 0.92f, wide, -len * 0.25f, wide * 0.9f, len * 0.35f);
    capPath.cubicTo (wide * 0.8f, len * 0.75f, -wide * 0.8f, len * 0.75f, -wide * 0.9f, len * 0.35f);
    capPath.cubicTo (-wide, -len * 0.25f, -wide * 0.9f, -len * 0.92f, 0.0f, -len);
    capPath.closeSubPath();

    // Glossy specular streak along one flank + a small hot spot near the tip
    capHighlight.clear();
    capHighlight.startNewSubPath (-wide * 0.50f, -len * 0.62f);
    capHighlight.quadraticTo (-wide * 0.86f, -len * 0.12f, -wide * 0.62f, len * 0.36f);
    capHighlight.quadraticTo (-wide * 0.60f, -len * 0.10f, -wide * 0.30f, -len * 0.66f);
    capHighlight.closeSubPath();
    capHighlight.addEllipse (-wide * 0.30f, -len * 0.80f, wide * 0.22f, wide * 0.14f);

    // Cartoon "excitement" dashes, three on each upper side (shown above 80 %)
    const float dashAngles[] { -1.35f, -0.95f, -0.55f, 0.55f, 0.95f, 1.35f };
    for (size_t i = 0; i < excitementLines.size(); ++i)
    {
        juce::Path l;
        l.startNewSubPath (centre.getPointOnCircumference (outer - 13.0f, dashAngles[i]));
        l.lineTo (centre.getPointOnCircumference (outer - 1.0f, dashAngles[i]));
        excitementLines[i] = l;
    }

    // Side labels under the two ends of the ring (bipolar)
    lowLabelPos = centre.getPointOnCircumference (ringRadius + 2.0f, startAngle).translated (-4.0f, 20.0f);
    highLabelPos = centre.getPointOnCircumference (ringRadius + 2.0f, endAngle).translated (4.0f, 20.0f);
}

void BigKnob::paint (juce::Graphics& g)
{
    const auto pal = [this] (juce::Colour c) { return palette (c); };
    const float angle = angleFor (value);
    constexpr float ringWidth = 14.0f;

    // Track
    g.setColour (pal (palette.track));
    g.strokePath (trackPath, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Progress arc from the rest position (0 %, or the top when bipolar),
    // coloured along its length (small segments)
    const float from = juce::jmin (origin(), value), to = juce::jmax (origin(), value);
    const float span = to - from;
    const float level = intensity();
    if (span > 0.0005f)
    {
        const float glow = dspmath::smoothstep (0.8f, 1.0f, level);

        // Glow: a few wide translucent pieces (fewer seams than per segment)
        if (glow > 0.0f)
        {
            constexpr int pieces = 6;
            for (int s = 0; s < pieces; ++s)
            {
                const float t0 = from + span * (float) s / (float) pieces, t1 = from + span * (float) (s + 1) / (float) pieces;
                juce::Path glowSeg;
                glowSeg.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, angleFor (t0), angleFor (t1), true);
                g.setColour (pal (palette.arcColour (0.5f * (t0 + t1))).withAlpha (0.2f * glow));
                g.strokePath (glowSeg, juce::PathStrokeType (ringWidth + 12.0f, juce::PathStrokeType::curved,
                                                             s == 0 || s == pieces - 1 ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }
        }

        const int segments = juce::jmax (2, (int) (span * 64.0f));
        for (int s = 0; s < segments; ++s)
        {
            const float t0 = from + span * (float) s / (float) segments, t1 = from + span * (float) (s + 1) / (float) segments;
            juce::Path seg;
            seg.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, angleFor (t0), angleFor (t1) + 0.01f, true);
            const auto c = pal (palette.arcColour (value < origin() ? t0 : t1));
            g.setColour (c);
            const bool lastOrFirst = s == 0 || s == segments - 1;
            g.strokePath (seg, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved,
                                                     lastOrFirst ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
        }
    }

    // Centre tick (bipolar): where the knob is neutral
    if (bipolar)
    {
        g.setColour (pal (palette.text).withAlpha (0.55f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (ringRadius - 10.0f, 0.0f), centre.getPointOnCircumference (ringRadius - 4.0f, 0.0f)), 2.5f);

    }

    // End labels (LP / HP, SOLO / TRIO...): the side the knob leans to is lit
    if (lowLabel.isNotEmpty() || highLabel.isNotEmpty())
    {
        const auto lowColour = palette.lowLabel.isTransparent() ? palette.arcColour (0.0f) : palette.lowLabel;
        const auto highColour = palette.highLabel.isTransparent() ? palette.arcColour (1.0f) : palette.highLabel;
        const float pivot = bipolar ? 0.5f : 0.5f;
        const auto font = GrooveFonts::font (15.0f);
        g.setFont (font);
        // Long labels move outwards so they never reach the chips in the middle
        auto outwards = [&font] (const juce::String& t) { return juce::jmax (0.0f, 0.5f * juce::GlyphArrangement::getStringWidth (font, t) - 22.0f); };
        g.setColour (pal (lowColour).withAlpha (value < pivot ? 1.0f : 0.55f));
        g.drawText (lowLabel, juce::Rectangle<float> (90.0f, 18.0f).withCentre (lowLabelPos.translated (-outwards (lowLabel), 0.0f)),
                    juce::Justification::centred);
        g.setColour (pal (highColour).withAlpha (value > pivot ? 1.0f : 0.55f));
        g.drawText (highLabel, juce::Rectangle<float> (90.0f, 18.0f).withCentre (highLabelPos.translated (outwards (highLabel), 0.0f)),
                    juce::Justification::centred);
    }

    // Breathing glow (e.g. Backwash: grows during the swell, gone on the hit)
    if (pulse > 0.0f && ! palette.pulse.isTransparent() && span > 0.0005f)
    {
        const float end = from + span * pulse;
        juce::Path glowArc;
        const float a0 = value < origin() ? angleFor (to - span * pulse) : angleFor (from);
        const float a1 = value < origin() ? angleFor (to) : angleFor (end);
        glowArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, a0, a1, true);
        const auto c = pal (palette.pulse);
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (c.withAlpha (0.12f * pulse * (float) (4 - k)));
            g.strokePath (glowArc, juce::PathStrokeType (ringWidth + 6.0f * (float) k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c.withAlpha (0.85f * pulse));
        g.strokePath (glowArc, juce::PathStrokeType (ringWidth * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Excitement dashes, pulsing on the beat above 80 %
    if (const float ex = dspmath::smoothstep (0.8f, 0.95f, level); ex > 0.0f)
    {
        const float beatPulse = 0.65f + 0.35f * std::exp (-beatPhase * 5.0f);
        g.setColour (pal (palette.text).withAlpha (ex * beatPulse));
        for (const auto& l : excitementLines)
            g.strokePath (l, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body: drop shadow, bands, outline
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre.translated (0.0f, 5.0f)));
    if (bodyPainter)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bodyOutline);
        bodyPainter (g, juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), value, angle);
    }
    else if (! palette.knobRings.empty())
    {
        // Drum skin seen from above: a rim, concentric rings; the centre
        // lifts (bigger, lighter, its shadow grows) with setSkinLift
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bodyOutline);
        g.setColour (pal (palette.knobRim));
        g.fillPath (bodyOutline);
        const float skin = bodyRadius * 0.86f;
        const int n = (int) palette.knobRings.size();
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;                       // 0 outside .. 1 centre
            const float lift = 1.0f + 0.03f * skinLift * t;               // the centre rises most
            const float r = skin * (1.0f - t) * lift;
            const auto c = centre.translated (0.0f, -2.5f * skinLift * t);
            if (i > 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.10f + 0.12f * skinLift));
                g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c.translated (0.0f, 1.5f + 2.0f * skinLift)));
            }
            g.setColour (pal (palette.knobRings[(size_t) i]).brighter (0.25f * skinLift * t));
            g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        }
    }
    else
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bodyOutline);
        for (size_t i = 0; i < bandPaths.size(); ++i)
        {
            g.setColour (pal (palette.knobBands[i % palette.knobBands.size()]));
            g.fillPath (bandPaths[i]);
        }
    }
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bodyOutline);
        // soft shading for volume
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), centre.x - bodyRadius * 0.5f, centre.y - bodyRadius * 0.7f,
                                                 juce::Colours::black.withAlpha (0.25f), centre.x + bodyRadius * 0.5f, centre.y + bodyRadius, true));
        g.fillPath (bodyOutline);
    }
    g.setColour (pal (palette.outline));
    g.strokePath (bodyOutline, juce::PathStrokeType (5.0f));

    // Pointer cap
    if (pointerVisible)
    {
    const auto rot = juce::AffineTransform::rotation (angle).translated (centre);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (capPath, juce::AffineTransform::rotation (angle).translated (centre.translated (3.0f, 6.0f)));
    g.setColour (pal (palette.pointer));
    g.fillPath (capPath, rot);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillPath (capHighlight, rot);
    g.setColour (pal (palette.outline));
    g.strokePath (capPath, juce::PathStrokeType (3.0f), rot);

    // Indicator line near the tip
    const float len = bodyRadius * 0.92f;
    g.setColour (pal (palette.text));
    g.drawLine (juce::Line<float> (centre.getPointOnCircumference (len * 0.55f, angle), centre.getPointOnCircumference (len * 0.85f, angle)), 4.0f);
    }

    // Value badge while hovering / dragging / focused
    if (hovering || dragging || hasKeyboardFocus (false))
    {
        // as wide as its text (French Filter's "HP 100 %" ended in "..." in 64 px)
        const auto text = param.getCurrentValueAsText();
        const auto font = GrooveFonts::font (21.0f);
        const float width = juce::jmax (64.0f, juce::GlyphArrangement::getStringWidth (font, text) + 22.0f);
        const auto badge = juce::Rectangle<float> (width, 34.0f).withCentre (centre);
        g.setColour (pal (palette.plaque));
        g.fillRoundedRectangle (badge, 17.0f);
        g.setColour (pal (palette.outline));
        g.drawRoundedRectangle (badge, 17.0f, 3.0f);
        g.setFont (font);
        g.drawText (text, badge, juce::Justification::centred);
    }
}

void BigKnob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void BigKnob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void BigKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu (e);
        return;
    }
    dragging = true;
    dragStartValue = value;
    dragAnchorY = e.position.y;
    dragFine = e.mods.isShiftDown() || e.mods.isCommandDown();
    attachment.beginGesture();
    repaint();
}

void BigKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown();
    if (fine != dragFine)
    {
        // Re-anchor when the fine modifier changes, so the knob never jumps.
        dragFine = fine;
        dragStartValue = value;
        dragAnchorY = e.position.y;
    }
    const float delta = (dragAnchorY - e.position.y) / dragPixelsForFullRange * (fine ? 0.1f : 1.0f);
    float v = juce::jlimit (0.0f, 1.0f, dragStartValue + delta);
    // Centre magnet (bipolar, coarse drag): easy to land exactly on neutral
    if (bipolar && ! fine && std::abs (v - 0.5f) < 0.015f)
        v = 0.5f;
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (v));
}

void BigKnob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    attachment.endGesture();
    repaint();
}

void BigKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        setFromUser (param.getDefaultValue()); // the default (= the rest position, except e.g. Pew Pew's 60 %)
}

void BigKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dir = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    if (juce::exactlyEqual (dir, 0.0f))
        return;
    // Notched wheels: 1 % per notch. Trackpads: proportional.
    float step = w.isSmooth ? dir * 0.2f : (dir > 0.0f ? 0.01f : -0.01f);
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
        step *= 0.1f;
    setFromUser (value + step);
}

bool BigKnob::keyPressed (const juce::KeyPress& key)
{
    const bool fine = key.getModifiers().isShiftDown();
    const float step = fine ? 0.001f : 0.01f;
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
        setFromUser (value + step);
    else if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
        setFromUser (value - step);
    else if (key == juce::KeyPress::pageUpKey)
        setFromUser (value + 0.1f);
    else if (key == juce::KeyPress::pageDownKey)
        setFromUser (value - 0.1f);
    else if (key == juce::KeyPress::homeKey)
        setFromUser (origin());
    else if (key == juce::KeyPress::endKey)
        setFromUser (1.0f);
    else
        return false;
    return true;
}

void BigKnob::setFromUser (float newValue)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, newValue)));
}

void BigKnob::showContextMenu (const juce::MouseEvent& e)
{
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* host = editor->getHostContext())
            if (auto menu = host->getContextMenuForParameter (&param))
            {
                menu->showNativeMenu (editor->getMouseXYRelative());
                return;
            }

    juce::PopupMenu m;
    m.addItem (bipolar ? "Back to centre" : "Reset to 0 %", [safe = juce::Component::SafePointer<BigKnob> (this)]
               {
                   if (safe != nullptr)
                       safe->setFromUser (safe->origin());
               });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }));
}

std::unique_ptr<juce::AccessibilityHandler> BigKnob::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<KnobValueInterface> (param) });
}
} // namespace gk
