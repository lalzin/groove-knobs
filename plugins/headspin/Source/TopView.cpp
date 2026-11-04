#include "TopView.h"

namespace hs
{
namespace
{
    const juce::Colour pink (0xffff4fa3), ink (0xff121212), white (0xffffffff);
    constexpr float pi = juce::MathConstants<float>::pi;
} // namespace

TopView::TopView (HeadSpinProcessor& p, gk::AudioFeatures& features, const gk::Palette& pal) : MascotComponent (features, pal), proc (p)
{
    setTitle ("Top view");
    setDescription ("The sound's path around the head, seen from above. Drag it to stretch it, drag outside it or with alt to turn it, "
                    "double-click to reset.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    rebuildPath();
}

float TopView::param (HeadSpinDsp::Knob k)
{
    auto& p = proc.getKnob (k);
    return p.convertFrom0to1 (p.getValue());
}

void TopView::setParam (HeadSpinDsp::Knob k, float plain)
{
    auto& p = proc.getKnob (k);
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
}

juce::Point<float> TopView::toScreen (double x, double y) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float s = 0.5f * juce::jmin (b.getWidth(), b.getHeight()) / unitsToEdge;
    return { b.getCentreX() + (float) x * s, b.getCentreY() - (float) y * s };
}

juce::Point<double> TopView::toUnits (juce::Point<float> p) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float s = 0.5f * juce::jmin (b.getWidth(), b.getHeight()) / unitsToEdge;
    return { (double) ((p.x - b.getCentreX()) / s), (double) ((b.getCentreY() - p.y) / s) };
}

void TopView::rebuildPath()
{
    const float shape = param (HeadSpinDsp::shapeKnob), w = param (HeadSpinDsp::widthKnob), d = param (HeadSpinDsp::depthKnob),
                rot = param (HeadSpinDsp::rotationKnob);
    if (std::abs (shape - builtShape) > 1.0e-4f)
    {
        bank.buildMorph (shape, table); // (message thread, ~1 ms, only when SHAPE moves)
        builtShape = shape;
        pathWidth = -1.0f;
    }
    if (juce::exactlyEqual (w, pathWidth) && juce::exactlyEqual (d, pathDepth) && juce::exactlyEqual (rot, pathRotation))
        return;
    pathWidth = w;
    pathDepth = d;
    pathRotation = rot;
    const double c = std::cos (rot * pi / 180.0), s = std::sin (rot * pi / 180.0);
    path.clear();
    for (int k = 0; k <= 256; ++k)
    {
        const auto p = table.at (k / 256.0);
        const double x = p.x * w, y = p.y * d;
        const juce::Point<float> q ((float) (x * c + y * s), (float) (-x * s + y * c));
        if (k == 0)
            path.startNewSubPath (q);
        else
            path.lineTo (q);
    }
    path.closeSubPath();
}

void TopView::tick (double dt)
{
    rebuildPath();
    time += dt;
    auto& t = proc.getDsp().getTelemetry();
    dot = { t.x.load (std::memory_order_relaxed), t.y.load (std::memory_order_relaxed) };
    trailHead = (trailHead + 1) % trailLength;
    trail[(size_t) trailHead] = dot;
    trailFilled = trailFilled || trailHead == trailLength - 1;

    // the head: turns towards the sound (up to 70°), spins above 4 turns / s,
    // the cap flies off above 10
    const float speed = std::abs (t.turnsPerSecond.load (std::memory_order_relaxed));
    const float az = t.azimuthDeg.load (std::memory_order_relaxed);
    const float k = 1.0f - std::exp (-(float) dt / 0.12f);
    headTurn += (juce::jlimit (-70.0f, 70.0f, std::abs (az) > 110.0f ? 0.0f : az) - headTurn) * k;
    spinAmount += ((speed > 4.0f ? 1.0f : 0.0f) - spinAmount) * (1.0f - std::exp (-(float) dt / 0.3f));
    spinAngle = std::fmod (spinAngle + (float) dt * 360.0f * juce::jmin (speed, 3.0f) * spinAmount, 360.0f);
    const float capTarget = speed > 10.0f ? 0.5f + 0.5f * std::sin ((float) time * 2.0f * pi / 1.6f) : 0.0f;
    capLift += (capTarget - capLift) * (1.0f - std::exp (-(float) dt / 0.08f));
    repaint();
}

void TopView::snapForSnapshot()
{
    rebuildPath();
    auto& t = proc.getDsp().getTelemetry();
    dot = { t.x.load(), t.y.load() };
    // a trail along the path behind the dot (the direction it comes from)
    const bool l2r = proc.getChoice (HeadSpinDsp::directionChoice).getIndex() == 0;
    const float a0 = std::atan2 (dot.x, dot.y), r = juce::jmax (0.3f, std::hypot (dot.x, dot.y));
    for (int i = 0; i < trailLength; ++i)
    {
        const float a = a0 - (l2r ? 1.0f : -1.0f) * 0.022f * (float) (trailLength - 1 - i);
        trail[(size_t) i] = { r * std::sin (a), r * std::cos (a) };
    }
    trailHead = trailLength - 1;
    trailFilled = true;
    headTurn = juce::jlimit (-70.0f, 70.0f, std::abs (t.azimuthDeg.load()) > 110.0f ? 0.0f : t.azimuthDeg.load());
}

void TopView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto centre = b.getCentre();
    const float s = 0.5f * juce::jmin (b.getWidth(), b.getHeight()) / unitsToEdge;
    const float bypass = getState().bypass;
    const float dim = 1.0f - 0.6f * bypass;

    // The floor: rings at r = 0.5, 1 (the ears' circle), 1.5, and the axes
    g.setColour (palette (white).withAlpha (0.05f));
    g.fillEllipse (juce::Rectangle<float> (2.0f * 1.55f * s, 2.0f * 1.55f * s).withCentre (centre));
    for (float r : { 0.5f, 1.0f, 1.5f })
    {
        g.setColour (palette (white).withAlpha (r == 1.0f ? 0.16f : 0.08f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r * s, 2.0f * r * s).withCentre (centre), 1.0f);
    }
    g.setColour (palette (white).withAlpha (0.07f));
    g.drawLine (centre.x - 1.55f * s, centre.y, centre.x + 1.55f * s, centre.y, 1.0f);
    g.drawLine (centre.x, centre.y - 1.55f * s, centre.x, centre.y + 1.55f * s, 1.0f);
    g.setFont (gk::GrooveFonts::font (9.0f).withExtraKerningFactor (0.15f));
    g.setColour (palette (white).withAlpha (0.35f));
    g.drawText ("FRONT", juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ centre.x, centre.y - 1.55f * s + 8.0f }), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ centre.x - 1.55f * s + 9.0f, centre.y }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ centre.x + 1.55f * s - 9.0f, centre.y }), juce::Justification::centred);

    // The path: a neon line (glow, then the line)
    const auto toView = juce::AffineTransform::scale (s, -s).translated (centre);
    juce::Path p (path);
    p.applyTransform (toView);
    g.setColour (palette (pink).withAlpha (0.18f * dim));
    g.strokePath (p, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (palette (pink).withAlpha (0.85f * dim));
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The head (Kiko, drawn in code until his sprites)
    paintHead (g, centre, 0.34f * s);

    // The trail (oldest faint and thin), then the dot
    const int count = trailFilled ? trailLength : trailHead + 1;
    for (int i = 1; i < count; ++i)
    {
        const int a = (trailHead - i + 1 + trailLength) % trailLength, c = (trailHead - i + trailLength) % trailLength;
        const float f = 1.0f - (float) i / (float) count;
        g.setColour (palette (pink).withAlpha (0.75f * f * dim));
        const auto p1 = toScreen (trail[(size_t) a].x, trail[(size_t) a].y), p2 = toScreen (trail[(size_t) c].x, trail[(size_t) c].y);
        if (p1.getDistanceFrom (p2) < 0.4f * s) // (no line across a jump)
            g.drawLine ({ p1, p2 }, 1.0f + 6.0f * f);
    }
    const auto d = toScreen (dot.x, dot.y);
    juce::ColourGradient glow (palette (pink).withAlpha (0.55f * dim), d, palette (pink).withAlpha (0.0f), d.translated (22.0f, 0.0f), true);
    g.setGradientFill (glow);
    g.fillEllipse (juce::Rectangle<float> (44.0f, 44.0f).withCentre (d));
    g.setColour (palette (white).withAlpha (dim));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (d));
    g.setColour (palette (pink));
    g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (d), 2.0f);
}

void TopView::paintHead (juce::Graphics& g, juce::Point<float> c, float r)
{
    // seen from above, the nose up; turned by headTurn, spun by spinAngle
    const float angle = (headTurn + spinAngle) * pi / 180.0f;
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::rotation (angle).translated (c));

    // speed lines while spinning (drawn round the head, behind it)
    if (spinAmount > 0.05f)
    {
        g.setColour (palette (white).withAlpha (0.45f * spinAmount));
        for (int k = 0; k < 3; ++k)
        {
            juce::Path arc;
            const float a0 = (float) k * 2.0f * pi / 3.0f;
            arc.addCentredArc (0.0f, 0.0f, r * 1.55f, r * 1.55f, 0.0f, a0, a0 + 1.1f, true);
            g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    // shoulders
    g.setColour (palette (white));
    g.fillRoundedRectangle (juce::Rectangle<float> (-1.45f * r, -0.15f * r, 2.9f * r, 0.75f * r), 0.35f * r);
    g.setColour (palette (ink));
    g.drawRoundedRectangle (juce::Rectangle<float> (-1.45f * r, -0.15f * r, 2.9f * r, 0.75f * r), 0.35f * r, 2.5f);
    // headphones: the band over the top, the cups on the ears
    g.setColour (palette (ink));
    for (float side : { -1.0f, 1.0f })
    {
        const auto cup = juce::Rectangle<float> (0.42f * r, 0.62f * r).withCentre ({ side * 0.98f * r, 0.0f });
        g.fillRoundedRectangle (cup, 0.16f * r);
        g.setColour (palette (pink));
        g.drawRoundedRectangle (cup.reduced (0.07f * r), 0.12f * r, 2.0f);
        g.setColour (palette (ink));
    }
    // the head, the nose (front, up)
    g.setColour (palette (juce::Colour (0xfff2c09a)));
    g.fillEllipse (-0.82f * r, -0.82f * r, 1.64f * r, 1.64f * r);
    juce::Path nose;
    nose.addTriangle (-0.16f * r, -0.74f * r, 0.16f * r, -0.74f * r, 0.0f, -1.02f * r);
    g.fillPath (nose);
    g.setColour (palette (ink));
    g.strokePath (nose, juce::PathStrokeType (2.0f));
    g.drawEllipse (-0.82f * r, -0.82f * r, 1.64f * r, 1.64f * r, 2.5f);
    // the cap, backwards (the visor at the back of the head); it flies off
    {
        const float lift = capLift;
        juce::Graphics::ScopedSaveState capState (g);
        g.addTransform (juce::AffineTransform::scale (1.0f + 0.35f * lift).translated (0.0f, -1.6f * r * lift));
        const auto crown = juce::Rectangle<float> (1.5f * r, 1.5f * r).withCentre ({ 0.0f, 0.05f * r });
        juce::Path visor;
        visor.addCentredArc (0.0f, 0.55f * r, 0.72f * r, 0.55f * r, 0.0f, pi * 0.5f, pi * 1.5f, true);
        visor.closeSubPath();
        g.setColour (palette (pink).darker (0.25f));
        g.fillPath (visor);
        g.setColour (palette (ink));
        g.strokePath (visor, juce::PathStrokeType (2.2f));
        g.setColour (palette (pink));
        g.fillEllipse (crown);
        g.setColour (palette (ink));
        g.drawEllipse (crown, 2.5f);
        // seams and the button
        g.setColour (palette (ink).withAlpha (0.5f));
        g.drawLine (0.0f, crown.getY() + 2.0f, 0.0f, crown.getBottom() - 2.0f, 1.5f);
        g.drawLine (crown.getX() + 2.0f, crown.getCentreY(), crown.getRight() - 2.0f, crown.getCentreY(), 1.5f);
        g.setColour (palette (white));
        g.fillEllipse (juce::Rectangle<float> (0.22f * r, 0.22f * r).withCentre (crown.getCentre()));
    }
}

void TopView::mouseMove (const juce::MouseEvent& e)
{
    const auto u = toUnits (e.position);
    setMouseCursor (std::hypot (u.x, u.y) > 1.5 ? juce::MouseCursor::UpDownLeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void TopView::mouseDown (const juce::MouseEvent& e)
{
    const auto u = toUnits (e.position);
    startWidth = param (HeadSpinDsp::widthKnob);
    startDepth = param (HeadSpinDsp::depthKnob);
    startRotation = param (HeadSpinDsp::rotationKnob);
    startAngle = std::atan2 (u.x, u.y);
    const bool rotate = e.mods.isAltDown() || e.mods.isPopupMenu() || std::hypot (u.x, u.y) > 1.5;
    drag = rotate ? Drag::rotate : Drag::stretch;
    if (drag == Drag::rotate)
        proc.getKnob (HeadSpinDsp::rotationKnob).beginChangeGesture();
    else
    {
        // the grabbed point in the shape's frame (rotation undone, stretch kept)
        const double a = startRotation * pi / 180.0, c = std::cos (a), s = std::sin (a);
        grabLocal = { u.x * c - u.y * s, u.x * s + u.y * c };
        proc.getKnob (HeadSpinDsp::widthKnob).beginChangeGesture();
        proc.getKnob (HeadSpinDsp::depthKnob).beginChangeGesture();
    }
}

void TopView::mouseDrag (const juce::MouseEvent& e)
{
    const auto u = toUnits (e.position);
    if (drag == Drag::rotate)
    {
        double deg = startRotation + (std::atan2 (u.x, u.y) - startAngle) * 180.0 / pi;
        deg -= 360.0 * std::floor ((deg + 180.0) / 360.0);
        setParam (HeadSpinDsp::rotationKnob, (float) deg);
    }
    else if (drag == Drag::stretch)
    {
        // the grabbed point follows the mouse: each axis scales by the ratio
        // (an axis barely grabbed is left alone)
        const double a = startRotation * pi / 180.0, c = std::cos (a), s = std::sin (a);
        const juce::Point<double> now { u.x * c - u.y * s, u.x * s + u.y * c };
        if (std::abs (grabLocal.x) > 0.15)
            setParam (HeadSpinDsp::widthKnob, juce::jlimit (0.2f, 1.0f, (float) (startWidth * std::abs (now.x) / std::abs (grabLocal.x))));
        if (std::abs (grabLocal.y) > 0.15)
            setParam (HeadSpinDsp::depthKnob, juce::jlimit (0.2f, 1.0f, (float) (startDepth * std::abs (now.y) / std::abs (grabLocal.y))));
    }
}

void TopView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::rotate)
        proc.getKnob (HeadSpinDsp::rotationKnob).endChangeGesture();
    else if (drag == Drag::stretch)
    {
        proc.getKnob (HeadSpinDsp::widthKnob).endChangeGesture();
        proc.getKnob (HeadSpinDsp::depthKnob).endChangeGesture();
    }
    drag = Drag::none;
}

void TopView::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto k : { HeadSpinDsp::widthKnob, HeadSpinDsp::depthKnob, HeadSpinDsp::rotationKnob })
    {
        auto& p = proc.getKnob (k);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    }
}
} // namespace hs
