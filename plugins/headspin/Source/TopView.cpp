#include "TopView.h"
#include "KikoMascot.h"

namespace hs
{
namespace
{
    const juce::Colour pink (0xffff2d78), ink (0xff0e1226), white (0xffffffff);
    constexpr float pi = juce::MathConstants<float>::pi;
} // namespace

TopView::TopView (HeadSpinProcessor& p, const gk::Palette& pal) : proc (p), palette (pal), kiko (KikoMascot::loadTopView())
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
    const float bypass = proc.getBypassParameterObject().get() ? 1.0f : 0.0f;
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

    // Kiko, from above
    paintKiko (g, centre, s);

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

void TopView::paintKiko (juce::Graphics& g, juce::Point<float> c, float unit)
{
    // turned towards the sound (headTurn) and spinning (spinAngle); the
    // drawing has his face down: +180° puts it to the front (up)
    const float angle = (180.0f + headTurn + spinAngle) * pi / 180.0f;
    // speed lines while spinning, round him
    if (spinAmount > 0.05f)
    {
        g.setColour (palette (white).withAlpha (0.5f * spinAmount));
        for (int k = 0; k < 3; ++k)
        {
            juce::Path arc;
            const float a0 = (float) k * 2.0f * pi / 3.0f - spinAngle * pi / 180.0f;
            arc.addCentredArc (c.x, c.y, 0.62f * unit, 0.62f * unit, 0.0f, a0, a0 + 1.0f, true);
            g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    if (! kiko.isValid())
        return;
    // 1.1 of the ears' radius tall; the turn pivots on his head (42 % down the drawing)
    const float h = 1.1f * unit, w = h * (float) kiko.getWidth() / (float) kiko.getHeight();
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int px = juce::roundToInt (h * scale);
    if (! kikoScaled.isValid() || kikoScaled.getHeight() != px)
        kikoScaled = kiko.rescaled (juce::roundToInt (w * scale), px, juce::Graphics::highResamplingQuality);
    const juce::Point<float> pivot (0.5f * w, 0.42f * h);
    g.setOpacity (1.0f);
    g.drawImageTransformed (kikoScaled, juce::AffineTransform::scale (w / (float) kikoScaled.getWidth(), h / (float) kikoScaled.getHeight())
                                            .translated (-pivot.x, -pivot.y)
                                            .rotated (angle)
                                            .translated (c));
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
