#include "HeadSpinEditor.h"
#include "TopView.h"
#include "KikoMascot.h"

namespace hs
{
namespace
{
    // Kiko's colours: the cap's pink, the trousers' navy, the headphones' blue, white
    const juce::Colour pink (0xffff2d78), navy (0xff1b2a5e), blue (0xff2456c8), white (0xffffffff), ink (0xff0e1226);

    /** A small drawing of shape `k` (0..5) in `r`. */
    juce::Path shapeIcon (int k, juce::Rectangle<float> r)
    {
        juce::Path p;
        for (int i = 0; i <= 96; ++i)
        {
            const auto q = headspin::shapes::base (k, i / 96.0);
            const juce::Point<float> pt (r.getCentreX() + (float) q.x * r.getWidth() * 0.34f, r.getCentreY() - (float) (q.y - (k == 4 ? 0.8 : 0.0)) * r.getHeight() * 0.34f);
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        p.closeSubPath();
        return p;
    }
} // namespace

gk::Palette HeadSpinEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff1a1f38); // a night navy (Kiko's trousers, darker)
    p.backgroundEdge = juce::Colour (0xff0b0e1c);
    p.surface = juce::Colour (0xff2a3156);
    p.outline = juce::Colour (0xff070914);
    p.text = white;
    p.accent = pink;
    p.beret = pink; // power button, "LALZIN"
    p.track = white.withAlpha (0.12f);
    p.pointer = ink;
    p.plaque = white;
    p.arcStops = { white, pink };                  // MIX: white -> the cap's pink
    p.knobBands = { pink, navy, pink, navy };       // the big knob: Kiko's stripes (Kick Lock's drum)
    p.pulse = blue;
    p.arrowLeft = white;
    p.arrowRight = pink;
    p.lowLabel = white; // DRY
    p.highLabel = pink; // SPIN
    return p;
}

/** The panels behind the controls (Kick Lock's layout): left (Kiko, the
    shapes, the direction), the view from above, MIX under it, the right
    column (the motion). */
class HeadSpinEditor::Panels final : public juce::Component
{
public:
    explicit Panels (const gk::Palette& pal) : palette (pal) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const juce::Rectangle<float> areas[] { { 6, 64, 190, 368 }, { 202, 64, 356, 238 }, { 202, 308, 356, 124 }, { 564, 64, 190, 368 } };
        for (const auto& r : areas)
        {
            g.setColour (palette (juce::Colour (0xff222846)).withAlpha (0.92f));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (palette (blue).withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
        }
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.16f));
        g.setColour (palette (white).withAlpha (0.55f));
        g.drawText ("MOTION", juce::Rectangle<float> (564, 66, 190, 14), juce::Justification::centred);
        g.drawText ("FROM ABOVE", juce::Rectangle<float> (214, 68, 120, 14), juce::Justification::centredLeft);
    }

private:
    const gk::Palette& palette;
};

/** The shapes, 4 x 2: the six, CUSTOM (the drawn one: its icon is the
    drawing) and the pencil (DRAW: the next stroke round Kiko in the view
    becomes CUSTOM). Click a shape; drag across them to morph in between. */
class HeadSpinEditor::ShapeSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ShapeSelector (HeadSpinProcessor& p, TopView& v, const gk::Palette& pal) : proc (p), view (v), param (p.getKnob (HeadSpinDsp::shapeKnob)), palette (pal)
    {
        setTitle ("Shape");
        setTooltip ("SHAPE: click one, or drag across them to morph from one to the next. The pencil: draw your own path round Kiko "
                    "in the view (one stroke); it becomes CUSTOM");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    static constexpr int pencil = 7;

    void paint (juce::Graphics& g) override
    {
        const float value = param.convertFrom0to1 (param.getValue());
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        g.setColour (palette (pink).withAlpha (0.9f));
        g.drawText ("SHAPE", getLocalBounds().removeFromTop (13).toFloat(), juce::Justification::centred);
        for (int k = 0; k < 8; ++k)
        {
            const auto cell = cellBounds (k);
            const bool isPencil = k == pencil;
            const float closeness = isPencil ? (view.isDrawMode() ? 1.0f : 0.0f) : juce::jmax (0.0f, 1.0f - std::abs (value - (float) k));
            g.setColour (palette (closeness > 0.5f ? (isPencil ? blue : pink) : palette.surface).withAlpha (closeness > 0.5f ? 0.25f + 0.5f * closeness : 1.0f));
            g.fillRoundedRectangle (cell, 7.0f);
            g.setColour (palette (palette.outline));
            g.drawRoundedRectangle (cell, 7.0f, 1.5f);
            g.setColour (palette (closeness > 0.0f ? white : white.withAlpha (0.55f)));
            const auto inner = cell.reduced (4.0f);
            if (isPencil)
                drawPencil (g, inner);
            else if (k == headspin::shapes::custom)
                g.strokePath (customIcon (inner), juce::PathStrokeType (closeness > 0.5f ? 2.0f : 1.4f, juce::PathStrokeType::curved));
            else
                g.strokePath (shapeIcon (k, inner), juce::PathStrokeType (closeness > 0.5f ? 2.2f : 1.5f, juce::PathStrokeType::curved));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        downAt = e.position;
        onPencil = cellAt (e.position) == pencil;
        if (! onPencil)
            param.beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onPencil || (e.position.getDistanceFrom (downAt) < 4.0f && ! dragged))
            return;
        dragged = true;
        set (valueAt (e.position));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onPencil)
        {
            if (cellAt (e.position) == pencil)
                view.setDrawMode (! view.isDrawMode());
            return;
        }
        if (! dragged && cellAt (e.position) >= 0 && cellAt (e.position) < pencil)
            set ((float) cellAt (e.position));
        param.endChangeGesture();
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().withTrimmedTop (15.0f); }
    juce::Rectangle<float> cellBounds (int k) const
    {
        const auto a = area();
        const float w = a.getWidth() / 4.0f, h = a.getHeight() / 2.0f;
        return juce::Rectangle<float> (a.getX() + w * (float) (k % 4), a.getY() + h * (float) (k / 4), w, h).reduced (2.5f);
    }
    int cellAt (juce::Point<float> p) const
    {
        for (int k = 0; k < 8; ++k)
            if (cellBounds (k).expanded (2.5f).contains (p))
                return k;
        return -1;
    }
    /** Dragging: the position along the two rows read as one line (0 .. 6). */
    float valueAt (juce::Point<float> p) const
    {
        const auto a = area();
        const int row = p.y < a.getCentreY() ? 0 : 1;
        const float col = juce::jlimit (0.0f, 3.999f, (p.x - a.getX()) / (a.getWidth() / 4.0f));
        return juce::jlimit (0.0f, 6.0f, (float) (row * 4) + col - 0.5f);
    }
    void set (float v) { param.setValueNotifyingHost (param.convertTo0to1 (v)); }

    juce::Path customIcon (juce::Rectangle<float> r) const
    {
        const auto& pts = proc.getCustomShape();
        juce::Path p;
        if (pts.empty())
        {
            // not drawn yet: a dashed circle
            juce::Path c;
            c.addEllipse (r.withSizeKeepingCentre (r.getHeight() * 0.68f, r.getHeight() * 0.68f));
            const float dashes[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.4f).createDashedStroke (p, c, dashes, 2);
            return p;
        }
        for (size_t i = 0; i < pts.size(); i += 4)
        {
            const juce::Point<float> q (r.getCentreX() + (float) pts[i].x * r.getWidth() * 0.22f, r.getCentreY() - (float) pts[i].y * r.getHeight() * 0.22f);
            if (i == 0)
                p.startNewSubPath (q);
            else
                p.lineTo (q);
        }
        p.closeSubPath();
        return p;
    }
    void drawPencil (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        juce::Path pen;
        const float l = r.getHeight() * 0.36f, w = r.getHeight() * 0.09f;
        pen.addRectangle (-w, -l, 2.0f * w, 1.55f * l);
        pen.addTriangle (-w, 0.55f * l, w, 0.55f * l, 0.0f, 0.95f * l);
        g.fillPath (pen, juce::AffineTransform::rotation (0.7f).translated (r.getCentre()));
    }

    HeadSpinProcessor& proc;
    TopView& view;
    juce::RangedAudioParameter& param;
    const gk::Palette& palette;
    juce::Point<float> downAt;
    bool dragged = false, onPencil = false;
};

/** DIRECTION: ↻ left to right (clockwise from above) / ↺ right to left. */
class HeadSpinEditor::DirectionButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DirectionButton (juce::AudioParameterChoice& p, const gk::Palette& pal)
        : param (p), palette (pal), attachment (p, [this] (float) { repaint(); }, nullptr)
    {
        setTitle ("Direction");
        setTooltip ("DIRECTION: left to right (clockwise seen from above: the sound crosses in front from left to right) or right to left");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        attachment.sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        const bool l2r = param.getIndex() == 0;
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (palette.surface).withMultipliedBrightness (over ? 1.25f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (pink));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        // the arrow round a little circle
        const auto c = juce::Point<float> (b.getX() + b.getHeight() * 0.6f, b.getCentreY());
        const float r = b.getHeight() * 0.27f;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, l2r ? 0.6f : -0.6f, l2r ? 5.4f : -5.4f, true);
        g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float end = l2r ? 5.4f : -5.4f;
        const juce::Point<float> tip (c.x + r * std::sin (end), c.y - r * std::cos (end));
        juce::Path head;
        head.addTriangle (-4.0f, -3.5f, 4.0f, -3.5f, 0.0f, 4.0f);
        g.fillPath (head, juce::AffineTransform::rotation (end + (l2r ? -1.4f : 1.4f)).translated (tip));
        g.setColour (palette (white));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.06f));
        g.drawText (l2r ? juce::String::fromUTF8 ("L \xe2\x86\x92 R") : juce::String::fromUTF8 ("R \xe2\x86\x92 L"),
                    b.withTrimmedLeft (b.getHeight() * 1.1f), juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()))
            flip();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k != juce::KeyPress::returnKey && k != juce::KeyPress::spaceKey)
            return false;
        flip();
        return true;
    }

private:
    void flip() { attachment.setValueAsCompleteGesture (param.getIndex() == 0 ? 1.0f : 0.0f); }
    juce::AudioParameterChoice& param;
    const gk::Palette& palette;
    juce::ParameterAttachment attachment;
    bool over = false;
};

/** The mono check, discreet: lit when the mono sum loses more than 3 dB
    beyond what the pan law itself loses at this position. */
class HeadSpinEditor::MonoCheck final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MonoCheck (const gk::Palette& pal) : palette (pal)
    {
        setTooltip ("Mono check: the mono sum (L + R) against the stereo. The pan law itself loses up to 3 dB when the sound is on one side; "
                    "this lights when it loses more than 3 dB beyond that.");
    }
    void set (float excessDb)
    {
        const bool now = excessDb < -3.0f;
        if (now != warn || std::abs (excessDb - shown) > 0.3f)
        {
            warn = now;
            shown = excessDb;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (palette (warn ? pink : palette.surface).withAlpha (warn ? 0.9f : 0.6f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (warn ? ink : white.withAlpha (0.55f)));
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.1f));
        g.drawText (warn ? "MONO " + juce::String (shown, 1) + " dB" : juce::String ("MONO OK"), b, juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
    bool warn = false;
    float shown = 0.0f;
};

HeadSpinEditor::HeadSpinEditor (HeadSpinProcessor& p)
    : OneKnobEditor (p, makePalette(), HS_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures& f, const gk::Palette& palette) { return std::make_unique<KikoMascot> (p, f, palette); }),
      proc (p)
{
    auto& pal = getPalette();
    setBackgroundCached (true);
    panels = std::make_unique<Panels> (pal);
    addToCanvas (*panels, { 0, 0, getBaseWidth(), getBaseHeight() });
    panels->toBack();

    // Kiko (small, left), MIX under the view from above; the presets along the bottom
    placeMascotAndKnob ({ 14, 70, 174, 198 }, juce::Rectangle<int> (146, 146).withCentre ({ 380, 372 }));
    getPresetBar().setBounds (190, 434, 380, 46);
    view = std::make_unique<TopView> (p, pal);
    addToCanvas (*view, { 206, 66, 348, 234 });

    // left: the shapes and the direction
    shapes = std::make_unique<ShapeSelector> (p, *view, pal);
    view->onDrawModeChanged = [this] { shapes->repaint(); };
    addToCanvas (*shapes, { 14, 268, 174, 112 });
    direction = std::make_unique<DirectionButton> (p.getChoice (HeadSpinDsp::directionChoice), pal);
    addToCanvas (*direction, { 22, 386, 158, 36 });

    // right: the motion, two columns
    struct K { HeadSpinDsp::Knob knob; juce::Rectangle<int> at; juce::Colour colour; };
    const K ks[] { { HeadSpinDsp::phaseKnob, { 576, 146, 66, 70 }, white },
                   { HeadSpinDsp::distanceKnob, { 666, 146, 66, 70 }, blue },
                   { HeadSpinDsp::grooveKnob, { 576, 216, 66, 70 }, pink },
                   { HeadSpinDsp::rotationKnob, { 666, 216, 66, 70 }, white },
                   { HeadSpinDsp::widthKnob, { 576, 286, 66, 70 }, blue },
                   { HeadSpinDsp::depthKnob, { 666, 286, 66, 70 }, blue },
                   { HeadSpinDsp::focusKnob, { 621, 356, 66, 70 }, pink },
                   // under the view, right of MIX: the level
                   { HeadSpinDsp::levelKnob, { 474, 318, 66, 70 }, white } };
    for (const auto& k : ks)
    {
        knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnob (k.knob), pal, p.getConfig().knobs[(size_t) k.knob].caption, k.colour));
        addToCanvas (*knobs.back(), k.at);
    }
    // the speed: RATE in Hz or the DIVISION in Sync (the same place), and its switch
    rateHz = std::make_unique<gk::SmallKnob> (p.getKnob (HeadSpinDsp::rateHzKnob), pal, "RATE", pink);
    rateSync = std::make_unique<gk::SmallKnob> (p.getChoice (HeadSpinDsp::rateSyncChoice), pal, "DIVISION", pink);
    addToCanvas (*rateHz, { 576, 76, 66, 70 });
    addToCanvas (*rateSync, { 576, 76, 66, 70 });
    mode = std::make_unique<gk::ChoiceSelector> (p.getChoice (HeadSpinDsp::rateModeChoice), "MODE", pink, pal);
    mode->setTooltip ("Hz: a speed in turns per second. Sync: a turn lasts a musical division of the host's tempo, locked to the bar");
    addToCanvas (*mode, { 660, 92, 80, 38 });

    // under the view, left of MIX: retrigger and render; right: the mono check
    retrigger = std::make_unique<gk::ChoiceSelector> (p.getChoice (HeadSpinDsp::retriggerChoice), "RETRIGGER", pink, pal);
    retrigger->setTooltip ("Puts the sound back on its START point: on every MIDI note, or on every transient of the input");
    addToCanvas (*retrigger, { 214, 320, 88, 38 });
    render = std::make_unique<gk::ChoiceSelector> (p.getChoice (HeadSpinDsp::renderChoice), "RENDER", blue, pal);
    render->setTooltip ("Speakers: the head model for loudspeakers. Headphones: binaural (HRTF) — coming next, Speakers meanwhile");
    addToCanvas (*render, { 214, 372, 88, 38 });
    mono = std::make_unique<MonoCheck> (pal);
    addToCanvas (*mono, { 462, 396, 90, 22 });
    editorTick (0.0);
}

HeadSpinEditor::~HeadSpinEditor() = default;

void HeadSpinEditor::editorTick (double dt)
{
    const bool sync = proc.getChoice (HeadSpinDsp::rateModeChoice).getIndex() == 1;
    rateHz->setVisible (! sync);
    rateSync->setVisible (sync);
    if (shapes != nullptr)
        shapes->repaint();
    auto& t = proc.getDsp().getTelemetry();
    if (mono != nullptr)
        mono->set (t.monoLossDb.load (std::memory_order_relaxed) - t.panLawLossDb.load (std::memory_order_relaxed));
    if (view != nullptr && dt > 0.0)
        view->tick (dt);
}

void HeadSpinEditor::snapView()
{
    snapForSnapshot();
    editorTick (0.0);
    if (view != nullptr)
        view->snapForSnapshot();
}
} // namespace hs
