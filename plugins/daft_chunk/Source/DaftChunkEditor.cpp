#include "DaftChunkEditor.h"
#include "ChunkyMascot.h"
#include "WaveformView.h"
#include "IdeasView.h"

namespace daftchunk
{
using namespace colours;

gk::Palette DaftChunkEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff1b1918);
    p.backgroundEdge = black;
    p.surface = surface;
    p.outline = black;
    p.text = cream;
    p.accent = coral;
    p.beret = coral; // power button, "LALZIN"
    p.track = cream.withAlpha (0.12f);
    p.pointer = black;
    p.plaque = cream;
    p.arcStops = { cream, coral };
    p.knobBands = { coral, juce::Colour (0xffd8401f), coral, juce::Colour (0xffd8401f) }; // Chunky's blocks
    p.pulse = coral;
    p.arrowLeft = cream;
    p.arrowRight = coral;
    p.lowLabel = cream;  // ORDER
    p.highLabel = coral; // WILD
    return p;
}

/** The panels behind the controls. */
class DaftChunkEditor::Panels final : public juce::Component
{
public:
    Panels() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const juce::Rectangle<float> areas[] { { 6, 62, 848, 238 }, { 6, 306, 176, 206 }, { 188, 306, 480, 206 }, { 674, 306, 180, 206 }, { 6, 518, 848, 78 } };
        for (const auto& r : areas)
        {
            g.setColour (panel.withAlpha (0.95f));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (coral.withAlpha (0.25f));
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
        }
    }
};

/** A rounded button with a label: selected (coral) or not; disabled (greyed). */
class DaftChunkEditor::Pill final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Pill (juce::String t, std::function<void()> click, float fontSize = 13.0f) : text (std::move (t)), onClick (std::move (click)), size (fontSize)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    void setSelected (bool s)
    {
        if (s != selected)
        {
            selected = s;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = isEnabled();
        g.setColour (! on ? surface.withAlpha (0.6f) : selected ? coral : surface);
        g.fillRoundedRectangle (b, b.getHeight() * 0.35f);
        g.setColour (! on ? black.withAlpha (0.6f) : hover ? cream.withAlpha (0.8f) : black);
        g.drawRoundedRectangle (b, b.getHeight() * 0.35f, 1.5f);
        g.setColour (! on ? cream.withAlpha (0.3f) : selected ? black : cream);
        g.setFont (gk::GrooveFonts::font (size));
        g.drawFittedText (text, b.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (isEnabled() && onClick)
            onClick();
    }

private:
    juce::String text;
    std::function<void()> onClick;
    float size;
    bool selected = false, hover = false;
};

/** A small horizontal slider (0..1) with its value in %. */
class DaftChunkEditor::MiniSlider final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onChange;
    juce::String caption { "SENS" };
    float maximum = 1.0f; // the value shown at the right end (SWING: 75 %)
    void setValue (float v)
    {
        if (std::abs (v - value) > 1.0e-4f)
        {
            value = v;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (surface);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (coral);
        g.fillRoundedRectangle (b.withWidth (std::max (8.0f, b.getWidth() * value)), 6.0f);
        g.setColour (black);
        g.drawRoundedRectangle (b, 6.0f, 1.5f);
        g.setColour (cream);
        g.setFont (gk::GrooveFonts::font (12.0f));
        g.drawText (caption + " " + juce::String (juce::roundToInt (value * maximum * 100.0f)) + " %", b, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float v = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
        if (std::abs (v - value) > 0.009f)
        {
            value = std::round (v * 100.0f) / 100.0f;
            repaint();
            if (onChange)
                onChange (value);
        }
    }

private:
    float value = 0.5f;
};

/** Plain text in a dark pill (the file name, the key). */
class DaftChunkEditor::InfoLabel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (const juce::String& t, bool dimmed = false)
    {
        if (t != text || dimmed != dim)
        {
            text = t;
            dim = dimmed;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (black);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (cream.withAlpha (dim ? 0.45f : 0.95f));
        g.setFont (gk::GrooveFonts::font (12.5f));
        g.drawFittedText (text, b.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }

private:
    juce::String text;
    bool dim = false;
};

/** The FX page's picture: SWEEP's position over its bars (the low-pass opening) and PUMP's gain now. */
class DaftChunkEditor::FxMeter final : public juce::Component
{
public:
    explicit FxMeter (DaftChunkProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const auto& t = proc.getDsp().getTelemetry();
        const float sweep = t.sweep.load (std::memory_order_relaxed), pump = t.pump.load (std::memory_order_relaxed);
        auto b = getLocalBounds().toFloat();
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
        auto row = [&] (juce::Rectangle<float> r, const char* label, float v, bool active, const juce::String& text)
        {
            g.setColour (cream.withAlpha (0.55f));
            g.drawText (label, r.removeFromLeft (52.0f), juce::Justification::centredLeft);
            const auto bar = r.withSizeKeepingCentre (r.getWidth(), 12.0f);
            g.setColour (surface);
            g.fillRoundedRectangle (bar, 5.0f);
            if (active)
            {
                g.setColour (coral);
                g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, v))), 5.0f);
            }
            g.setColour (cream.withAlpha (active ? 0.9f : 0.35f));
            g.drawText (text, bar, juce::Justification::centred);
        };
        row (b.removeFromTop (26.0f), "SWEEP", sweep, sweep >= 0.0f, sweep >= 0.0f ? juce::String (juce::roundToInt (sweep * 100.0f)) + " % open" : juce::String ("off"));
        b.removeFromTop (6.0f);
        const float pumpDb = juce::Decibels::gainToDecibels (std::max (1.0e-3f, pump));
        row (b.removeFromTop (26.0f), "PUMP", pump, true, pump < 0.999f ? juce::String (pumpDb, 1) + " dB" : juce::String ("0 dB"));
    }

private:
    DaftChunkProcessor& proc;
};

/** A knob for a slice's own setting (not a host parameter): drag up / down
    (shift: finer), the wheel, double-click for the default. */
class DaftChunkEditor::SliceKnob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SliceKnob (juce::String c, float lo, float hi, float def, bool isBipolar, std::function<juce::String (float)> fmt)
        : caption (std::move (c)), minimum (lo), maximum (hi), defaultValue (def), bipolar (isBipolar), format (std::move (fmt))
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    std::function<void (float)> onChange;
    void setValue (float v)
    {
        v = juce::jlimit (minimum, maximum, v);
        if (std::abs (v - value) > 1.0e-6f)
        {
            value = v;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto dial = juce::Rectangle<float> (b.getCentreX() - 21.0f, b.getY() + 2.0f, 42.0f, 42.0f);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float t = (value - minimum) / (maximum - minimum);
        const float tz = bipolar ? (0.0f - minimum) / (maximum - minimum) : 0.0f;
        juce::Path track, arc;
        track.addCentredArc (dial.getCentreX(), dial.getCentreY(), 18.0f, 18.0f, 0.0f, a0, a1, true);
        g.setColour (cream.withAlpha (0.12f));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = a0 + (a1 - a0) * std::min (t, tz), to = a0 + (a1 - a0) * std::max (t, tz);
        if (to - from > 0.01f)
        {
            arc.addCentredArc (dial.getCentreX(), dial.getCentreY(), 18.0f, 18.0f, 0.0f, from, to, true);
            g.setColour (coral);
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (surface);
        g.fillEllipse (dial.reduced (8.0f));
        g.setColour (black);
        g.drawEllipse (dial.reduced (8.0f), 1.5f);
        const float ang = a0 + (a1 - a0) * t;
        const juce::Point<float> c = dial.getCentre();
        g.setColour (cream);
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (4.0f, ang), c.getPointOnCircumference (12.0f, ang)), 2.5f);
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.1f));
        g.setColour (coral);
        g.drawText (caption, juce::Rectangle<float> (b.getX(), dial.getBottom() + 1.0f, b.getWidth(), 11.0f), juce::Justification::centred);
        g.setColour (cream.withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (10.0f));
        g.drawText (format (value), juce::Rectangle<float> (b.getX(), dial.getBottom() + 12.0f, b.getWidth(), 12.0f), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { dragStart = value; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float range = maximum - minimum;
        set (dragStart - (float) e.getDistanceFromDragStartY() * range / (e.mods.isShiftDown() ? 800.0f : 160.0f));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (defaultValue); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { set (value + w.deltaY * (maximum - minimum) * 0.05f); }

private:
    void set (float v)
    {
        v = juce::jlimit (minimum, maximum, v);
        if (std::abs (v - value) < 1.0e-6f)
            return;
        value = v;
        repaint();
        if (onChange)
            onChange (value);
    }
    juce::String caption;
    float minimum, maximum, defaultValue, value = 0.0f, dragStart = 0.0f;
    bool bipolar;
    std::function<juce::String (float)> format;
};

//==============================================================================
DaftChunkEditor::DaftChunkEditor (DaftChunkProcessor& p)
    : OneKnobEditor (p, makePalette(), "DAFT CHUNK",
                     [&p] (gk::AudioFeatures& f, const gk::Palette& palette) { return std::make_unique<ChunkyMascot> (p, f, palette); }),
      proc (p)
{
    setBackgroundCached (true);
    panels = std::make_unique<Panels>();
    addToCanvas (*panels, { 0, 0, getBaseWidth(), getBaseHeight() });
    panels->toBack();

    // Chunky left, CHOP in the middle panel (left of the SON / TRANCHE pages)
    placeMascotAndKnob ({ 14, 316, 160, 190 }, juce::Rectangle<int> (124, 124).withCentre ({ 260, 404 }));
    getPresetBar().setBounds (534, 8, 284, 46); // the presets: top right, by the title

    wave = std::make_unique<WaveformView> (p);
    addToCanvas (*wave, { 14, 100, 832, 192 });
    keys = std::make_unique<SliceKeys> (p);
    addToCanvas (*keys, { 6, 602, 848, 52 });

    // the toolbar
    load = std::make_unique<Pill> ("LOAD", [this] { chooseFile(); });
    load->setTooltip ("Load a sample (WAV, AIFF, FLAC, MP3, up to 60 s). You can also drop it on the plug-in.");
    addToCanvas (*load, { 14, 68, 70, 26 });
    fileName = std::make_unique<InfoLabel>();
    addToCanvas (*fileName, { 90, 68, 110, 26 });

    bpm = std::make_unique<juce::Label>();
    bpm->setEditable (true);
    bpm->setJustificationType (juce::Justification::centred);
    bpm->setFont (gk::GrooveFonts::font (13.0f));
    bpm->setColour (juce::Label::backgroundColourId, black);
    bpm->setColour (juce::Label::textColourId, cream);
    bpm->setColour (juce::Label::outlineColourId, coral.withAlpha (0.6f));
    bpm->setColour (juce::Label::textWhenEditingColourId, cream);
    bpm->setColour (juce::TextEditor::backgroundColourId, black);
    bpm->setColour (juce::TextEditor::textColourId, cream);
    bpm->setColour (juce::TextEditor::highlightColourId, coral.withAlpha (0.5f));
    bpm->setTooltip ("The sample's tempo (estimated). Click to type it; /2 or x2 if the analysis found half or double; TAP to tap it in.");
    bpm->onTextChange = [this]
    {
        const double v = bpm->getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue();
        proc.setBpm (v > 0.0 ? v : -1.0);
        updateToolbar();
    };
    addToCanvas (*bpm, { 206, 68, 80, 26 });
    half = std::make_unique<Pill> (juce::String ("/2"), [this] { proc.halveBpm(); updateToolbar(); });
    twice = std::make_unique<Pill> (juce::String ("x2"), [this] { proc.doubleBpm(); updateToolbar(); });
    tapButton = std::make_unique<Pill> ("TAP", [this] { proc.tap (juce::Time::getMillisecondCounterHiRes() * 0.001); updateToolbar(); });
    tapButton->setTooltip ("Tape le tempo (3 coups au moins)");
    addToCanvas (*half, { 292, 68, 26, 26 });
    addToCanvas (*twice, { 324, 68, 26, 26 });
    addToCanvas (*tapButton, { 356, 68, 36, 26 });
    keyLabel = std::make_unique<InfoLabel>();
    keyLabel->setTooltip ("The key (a guide): pitched slices follow it");
    addToCanvas (*keyLabel, { 398, 68, 58, 26 });
    sync = std::make_unique<Pill> ("TEMPO", [this] { showSyncMenu(); }, 12.0f);
    addToCanvas (*sync, { 462, 68, 90, 26 });

    mode = std::make_unique<Pill> ("TRANSIENTS", [this] { showModeMenu(); }, 12.0f);
    mode->setTooltip (juce::String::fromUTF8 ("Slicing: TRANSIENTS (at every hit), GRID (1/4, 1/8, 1/16 from beat 1), MIXED (the grid, snapped to the nearest hit within \xc2\xb1""20 ms)"));
    addToCanvas (*mode, { 558, 68, 100, 26 });
    const int divs[] { 4, 8, 16 };
    for (int i = 0; i < 3; ++i)
    {
        const int d = divs[i];
        divisions[(size_t) i] = std::make_unique<Pill> ("1/" + juce::String (d), [this, d] { proc.setDivision (d); updateToolbar(); }, 11.5f);
        divisions[(size_t) i]->setTooltip ("The grid (GRID and MIXED)");
        addToCanvas (*divisions[(size_t) i], { 664 + 34 * i, 68, 30, 26 });
    }
    sensitivity = std::make_unique<MiniSlider>();
    sensitivity->setTooltip ("Transient sensitivity: left only the hard hits, right every little one");
    sensitivity->onChange = [this] (float v) { proc.setSensitivity (v); };
    addToCanvas (*sensitivity, { 768, 68, 78, 26 });

    // the generator (right panel) and the 8 ideas (the strip)
    dice = std::make_unique<Pill> ("LOOP CREATOR", [this] { proc.rollDice(); showPage (page); }, 15.0f);
    dice->setTooltip ("Creates a new loop from the slices: its length (LENGTH), its style (STYLE), and CHOP from the original order (ORDER) to wild chopping (WILD). It goes into a free slot below and plays, in time with the host.");
    length = std::make_unique<Pill> ("AUTO", [this] { showLengthMenu(); }, 11.5f);
    length->setTooltip ("The loop's length: AUTO (from the style), 1, 2 or 4 bars. Also changes the selected loop.");
    encore = std::make_unique<Pill> ("ENCORE", [this] { proc.encore(); }, 15.0f);
    encore->setTooltip ("ENCORE: 4 close variations of the selected loop (10 to 25 % of its steps change), instead of starting over");
    style = std::make_unique<Pill> ("FILTER TOUCH", [this] { showStyleMenu(); }, 12.0f);
    style->setTooltip ("The loop style: FILTER TOUCH (a short phrase, repeated), DISCO LOOP (faithful), VOCAL CHOP, STUTTER HOUSE, REVERSE PARTY, PITCH PARTY (a melody from one slice)");
    swing = std::make_unique<MiniSlider>();
    swing->caption = "SWING";
    swing->maximum = 0.75f;
    swing->setTooltip ("The swing of the loops' 16th notes (0 to 75 %)");
    swing->onChange = [this] (float v)
    {
        auto& sw = proc.getKnobParameter (DaftChunkEngine::swingKnob);
        sw.beginChangeGesture();
        sw.setValueNotifyingHost (sw.convertTo0to1 (v * 0.75f));
        sw.endChangeGesture();
    };
    undoButton = std::make_unique<Pill> ("UNDO", [this] { proc.undo(); }, 11.5f);
    redoButton = std::make_unique<Pill> ("REDO", [this] { proc.redo(); }, 11.5f);
    ideaInfo = std::make_unique<InfoLabel>();
    addToCanvas (*dice, { 682, 313, 164, 36 });
    addToCanvas (*encore, { 682, 354, 80, 24 });
    addToCanvas (*length, { 766, 354, 80, 24 });
    addToCanvas (*style, { 682, 382, 164, 24 });
    addToCanvas (*swing, { 682, 410, 164, 22 });
    addToCanvas (*undoButton, { 682, 436, 80, 22 });
    addToCanvas (*redoButton, { 766, 436, 80, 22 });
    addToCanvas (*ideaInfo, { 682, 462, 164, 20 });
    capture = std::make_unique<Pill> ("MIDI CAPTURE", [this]
    {
        auto& c = proc.getChoiceParameter (DaftChunkEngine::captureChoice);
        c.beginChangeGesture();
        c.setValueNotifyingHost (c.getIndex() == 1 ? 0.0f : 1.0f);
        c.endChangeGesture();
    }, 11.5f);
    capture->setTooltip ("MIDI CAPTURE: the loop playing also sends its notes to the MIDI output. In Ableton, a MIDI track with Daft Chunk as its input records them.");
    addToCanvas (*capture, { 682, 486, 164, 20 });
    ideaStrip = std::make_unique<IdeaStrip> (p);
    ideaStrip->onSelect = [this] { showPage (2); };
    addToCanvas (*ideaStrip, { 6, 518, 848, 78 });

    // the pages under the tabs: SON (the host parameters) and TRANCHE (the selected slice's own)
    tabSound = std::make_unique<Pill> ("SOUND", [this] { showPage (0); }, 12.0f);
    tabSlice = std::make_unique<Pill> ("SLICE", [this] { showPage (1); }, 12.0f);
    tabSound->setTooltip ("The sound of every slice: the envelope, the pitch, the filter, the velocity, the mode");
    tabSlice->setTooltip ("The selected slice's own settings (click a slice or a key)");
    tabSequence = std::make_unique<Pill> ("SEQUENCE", [this] { showPage (2); }, 12.0f);
    tabSequence->setTooltip ("The selected loop's steps, to correct by hand");
    tabFx = std::make_unique<Pill> ("FX", [this] { showPage (3); }, 12.0f);
    tabFx->setTooltip ("The output effects: PUMP (ducked on every beat), CRUSH, MIX, and SWEEP (the low-pass opening over the bars)");
    addToCanvas (*tabSound, { 340, 314, 54, 22 });
    addToCanvas (*tabSlice, { 397, 314, 50, 22 });
    addToCanvas (*tabSequence, { 450, 314, 78, 22 });
    addToCanvas (*tabFx, { 531, 314, 38, 22 });
    sliceTitle = std::make_unique<InfoLabel>();
    addToCanvas (*sliceTitle, { 572, 314, 90, 22 });
    {
        using E = DaftChunkEngine;
        struct K { int knob; juce::Rectangle<int> at; juce::Colour colour; const char* tip; };
        const K fx[] { { E::pumpKnob, { 340, 342, 66, 70 }, coral, "PUMP: the sound ducks on every beat and comes back, like a sidechain to the kick (locked to the host's tempo)" },
                       { E::crushKnob, { 410, 342, 66, 70 }, cream, "CRUSH: fewer bits, a lower rate: grit" },
                       { E::mixKnob, { 480, 342, 66, 70 }, cream, "MIX: the effects against the dry slices" } };
        for (const auto& k : fx)
        {
            fxKnobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (k.knob), getPalette(), p.getConfig().knobs[(size_t) k.knob].caption, k.colour));
            juce::ignoreUnused (k.tip); // (SmallKnob has no tooltip: the FX tab's says it all)
            addToCanvas (*fxKnobs.back(), k.at);
        }
        sweepSelector = std::make_unique<gk::ChoiceSelector> (p.getChoiceParameter (E::sweepChoice), "SWEEP", coral, getPalette());
        sweepSelector->setTooltip ("SWEEP: a low-pass that opens over 4, 8 or 16 bars, locked to the host's bars (the French touch gesture). "
                                   "IDEA: the playing loop's own (FILTER TOUCH loops open over 8 bars)");
        addToCanvas (*sweepSelector, { 562, 350, 96, 38 });
        fxMeter = std::make_unique<FxMeter> (p);
        addToCanvas (*fxMeter, { 340, 424, 320, 78 });
    }
    stepGrid = std::make_unique<StepGrid> (p);
    addToCanvas (*stepGrid, { 336, 342, 328, 162 });

    {
        using E = DaftChunkEngine;
        struct K { int knob; juce::Rectangle<int> at; juce::Colour colour; };
        const K ks[] { { E::attackKnob, { 336, 342, 66, 70 }, cream }, { E::decayKnob, { 402, 342, 66, 70 }, cream },
                       { E::sustainKnob, { 468, 342, 66, 70 }, cream }, { E::releaseKnob, { 534, 342, 66, 70 }, coral },
                       { E::pitchKnob, { 336, 424, 66, 70 }, coral }, { E::filterKnob, { 402, 424, 66, 70 }, coral },
                       { E::resoKnob, { 468, 424, 66, 70 }, cream }, { E::velocityKnob, { 534, 424, 66, 70 }, cream },
                       { E::volumeKnob, { 600, 430, 66, 70 }, cream } };
        for (const auto& k : ks)
        {
            soundKnobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (k.knob), getPalette(), p.getConfig().knobs[(size_t) k.knob].caption, k.colour));
            addToCanvas (*soundKnobs.back(), k.at);
        }
        modeSelector = std::make_unique<gk::ChoiceSelector> (p.getChoiceParameter (E::modeChoice), "MODE", coral, getPalette());
        modeSelector->setTooltip ("CHOKE: a new note cuts the last one (like an MPC pad). POLY: they overlap (16 voices)");
        addToCanvas (*modeSelector, { 602, 342, 62, 38 });
        playSelector = std::make_unique<gk::ChoiceSelector> (p.getChoiceParameter (E::playChoice), "PLAY", coral, getPalette());
        playSelector->setTooltip ("ONE SHOT: the slice plays to its end. GATE: releasing the key starts the RELEASE");
        addToCanvas (*playSelector, { 602, 386, 62, 38 });
    }

    {
        auto st = [] (float v) { const int x = juce::roundToInt (v); return (x > 0 ? "+" : "") + juce::String (x) + " st"; };
        auto db = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
        auto oct = [] (float v)
        {
            if (std::abs (v) < 0.01f)
                return juce::String ("OPEN");
            const auto [lp, hp] = DaftChunkEngine::filterCutoffs (v);
            const double hz = v < 0.0f ? lp : hp;
            return juce::String (v < 0.0f ? "LP " : "HP ") + (hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + "k" : juce::String (juce::roundToInt (hz)));
        };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        sliceKnobs[0] = std::make_unique<SliceKnob> ("PITCH", -24.0f, 24.0f, 0.0f, true, st);
        sliceKnobs[1] = std::make_unique<SliceKnob> ("GAIN", -24.0f, 12.0f, 0.0f, true, db);
        sliceKnobs[2] = std::make_unique<SliceKnob> ("FILTER", -1.0f, 1.0f, 0.0f, true, oct);
        sliceKnobs[3] = std::make_unique<SliceKnob> ("START", 0.0f, 0.9f, 0.0f, false, pct);
        sliceKnobs[4] = std::make_unique<SliceKnob> ("END", 0.0f, 0.9f, 0.0f, false, pct);
        const char* tips[] { "This slice's pitch (on top of the global PITCH)", "This slice's volume",
                             "This slice's filter: left a low-pass, right a high-pass (on top of the global FILTER)", "Fine start: trims the slice's start",
                             "Fine end: trims the slice's end" };
        const juce::Rectangle<int> at[] { { 336, 342, 66, 70 }, { 402, 342, 66, 70 }, { 468, 342, 66, 70 }, { 534, 342, 66, 70 }, { 600, 342, 66, 70 } };
        for (int i = 0; i < 5; ++i)
        {
            sliceKnobs[(size_t) i]->setTooltip (juce::String::fromUTF8 (tips[i]));
            sliceKnobs[(size_t) i]->onChange = [this, i] (float v)
            {
                auto sp = proc.getSliceParams (proc.getSelectedSlice());
                (i == 0 ? sp.pitch : i == 1 ? sp.gainDb : i == 2 ? sp.filter : i == 3 ? sp.start : sp.end) = v;
                proc.setSliceParams (proc.getSelectedSlice(), sp);
            };
            addToCanvas (*sliceKnobs[(size_t) i], at[i]);
        }
        reverse = std::make_unique<Pill> ("REVERSE", [this]
        {
            auto sp = proc.getSliceParams (proc.getSelectedSlice());
            sp.reverse = ! sp.reverse;
            proc.setSliceParams (proc.getSelectedSlice(), sp);
        }, 12.0f);
        reverse->setTooltip ("Plays the slice backwards");
        resetSlice = std::make_unique<Pill> ("RESET", [this] { proc.resetSliceParams (proc.getSelectedSlice()); }, 12.0f);
        resetSlice->setTooltip ("Puts this slice back to zero");
        applyAll = std::make_unique<Pill> ("TO ALL", [this] { proc.applyToAllSlices (proc.getSelectedSlice()); }, 12.0f);
        applyAll->setTooltip ("Gives this slice's settings to every slice");
        lockButton = std::make_unique<Pill> ("LOCK", [this] { proc.toggleLock (proc.getSelectedSlice()); }, 12.0f);
        lockButton->setTooltip ("Locked, the slice keeps its place: LOOP CREATOR never moves it");
        addToCanvas (*reverse, { 338, 440, 80, 30 });
        addToCanvas (*lockButton, { 422, 440, 74, 30 });
        addToCanvas (*resetSlice, { 500, 440, 70, 30 });
        addToCanvas (*applyAll, { 574, 440, 88, 30 });
    }
    showPage (0);

    updateToolbar();
    editorTick (0.0);
}

DaftChunkEditor::~DaftChunkEditor() = default;

void DaftChunkEditor::updateToolbar()
{
    const auto* s = proc.getSample();
    const bool has = s != nullptr && ! proc.isAnalysing();
    if (proc.isAnalysing())
        fileName->set (juce::String::fromUTF8 ("Analysing\xe2\x80\xa6"), true);
    else if (s != nullptr)
        fileName->set (s->name);
    else
        fileName->set (proc.getStatus().isNotEmpty() ? proc.getStatus() : juce::String ("No sample"), true);
    fileName->setTooltip (s != nullptr ? (s->path.isNotEmpty() ? s->path : s->name) + (proc.getStatus().isNotEmpty() ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + proc.getStatus() : juce::String()) : proc.getStatus());

    if (! bpm->isBeingEdited())
        bpm->setText (has ? juce::String (proc.getBpm(), 2) + " BPM" : juce::String (juce::String::fromUTF8 ("\xe2\x80\x94 BPM")), juce::dontSendNotification);
    keyLabel->set (has ? s->analysis.key.name() : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), ! has);
    for (auto* b : { half.get(), twice.get(), tapButton.get() })
        b->setEnabled (has);
    bpm->setEnabled (has);

    {
        const int sy = proc.getSync();
        const double host = proc.getHostBpm();
        sync->setText (sy == DaftChunkEngine::tempo && proc.isStretching() ? juce::String::fromUTF8 ("STRETCHING\xe2\x80\xa6")
                       : sy == DaftChunkEngine::free ? juce::String ("FREE") : sy == DaftChunkEngine::tempo ? juce::String ("TEMPO") : juce::String ("VINYL"));
        sync->setSelected (sy != DaftChunkEngine::free && host > 0.0);
        sync->setTooltip ("SYNC. TEMPO: the slices at the host's tempo, same pitch (stretched in the background). VINYL: at the tempo by speed, the pitch follows. FREE: the sample's own speed. "
                          + (host > 0.0 ? "Host: " + juce::String (host, 2) + " BPM" : juce::String ("No host tempo (standalone): FREE")));
    }
    const auto& st = proc.getSettings();
    mode->setText (st.mode == SliceMode::transients ? "TRANSIENTS" : st.mode == SliceMode::grid ? "GRID" : "MIXED");
    mode->setSelected (true);
    const int divs[] { 4, 8, 16 };
    for (int i = 0; i < 3; ++i)
    {
        divisions[(size_t) i]->setSelected (st.division == divs[i]);
        divisions[(size_t) i]->setEnabled (st.mode != SliceMode::transients);
    }
    sensitivity->setValue (st.sensitivity);
    sensitivity->setEnabled (st.mode != SliceMode::grid);
    sensitivity->setAlpha (st.mode != SliceMode::grid ? 1.0f : 0.4f);
}

void DaftChunkEditor::editorTick (double)
{
    if (wave != nullptr)
        wave->refresh();
    if (keys != nullptr)
        keys->refresh();
    const bool busy = proc.isAnalysing();
    const double host = proc.getHostBpm();
    const bool stretching = proc.isStretching();
    if (std::abs (host - shownHost) > 0.005 || stretching != wasStretching || proc.getSync() != shownSync)
    {
        shownHost = host;
        wasStretching = stretching;
        shownSync = proc.getSync();
        if (load != nullptr)
            updateToolbar();
    }
    if (proc.getKitVersion() != shownVersion || busy != wasBusy || busy)
    {
        shownVersion = proc.getKitVersion();
        wasBusy = busy;
        if (load != nullptr)
            updateToolbar();
    }
    if (fxMeter != nullptr && page == 3)
        fxMeter->repaint();
    if (ideaStrip != nullptr)
    {
        ideaStrip->refresh();
        stepGrid->refresh();
        if (proc.getIdeasVersion() != shownIdeas || proc.getKitVersion() != shownGenKit || proc.getStatus() != shownStatus)
        {
            shownIdeas = proc.getIdeasVersion();
            shownGenKit = proc.getKitVersion();
            shownStatus = proc.getStatus();
            updateGenerator();
            if (page == 1)
                updateSlicePage();
        }
        const float sw = proc.getKnobParameter (DaftChunkEngine::swingKnob).convertFrom0to1 (proc.getKnobParameter (DaftChunkEngine::swingKnob).getValue());
        swing->setValue (sw / 0.75f);
        capture->setSelected (proc.getChoiceParameter (DaftChunkEngine::captureChoice).getIndex() == 1);
        {
            const char* names[] { "AUTO", "1 BAR", "2 BARS", "4 BARS" };
            length->setText (names[juce::jlimit (0, 3, proc.getChoiceParameter (DaftChunkEngine::lengthChoice).getIndex())]);
        }
        style->setText (styleName ((Style) proc.getChoiceParameter (DaftChunkEngine::styleChoice).getIndex()));
    }
    if (proc.getSelectedSlice() != shownSelected || proc.getKitVersion() != shownSliceVersion)
    {
        shownSelected = proc.getSelectedSlice();
        shownSliceVersion = proc.getKitVersion();
        if (sliceTitle != nullptr)
            updateSlicePage();
    }
}

void DaftChunkEditor::showPage (int index)
{
    page = index;
    tabSound->setSelected (page == 0);
    tabSlice->setSelected (page == 1);
    tabSequence->setSelected (page == 2);
    tabFx->setSelected (page == 3);
    for (auto& k : fxKnobs)
        k->setVisible (page == 3);
    sweepSelector->setVisible (page == 3);
    fxMeter->setVisible (page == 3);
    stepGrid->setVisible (page == 2);
    lockButton->setVisible (page == 1);
    for (auto& k : soundKnobs)
        k->setVisible (page == 0);
    modeSelector->setVisible (page == 0);
    playSelector->setVisible (page == 0);
    for (auto& k : sliceKnobs)
        k->setVisible (page == 1);
    for (auto* b : { reverse.get(), resetSlice.get(), applyAll.get() })
        b->setVisible (page == 1);
    sliceTitle->setVisible (page == 1);
    updateSlicePage();
}

void DaftChunkEditor::updateSlicePage()
{
    const auto kit = proc.getKit();
    const int sel = proc.getSelectedSlice();
    const bool has = kit != nullptr && juce::isPositiveAndBelow (sel, (int) kit->slices.size());
    const auto sp = proc.getSliceParams (sel);
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    sliceTitle->set (has ? juce::String (sel + 1) + dot + typeName (kit->slices[(size_t) sel].type) + dot + SliceKeys::noteName (DaftChunkEngine::baseNote + sel)
                         : juce::String ("NO SLICE"),
                     ! has);
    const float v[] { sp.pitch, sp.gainDb, sp.filter, sp.start, sp.end };
    for (int i = 0; i < 5; ++i)
    {
        sliceKnobs[(size_t) i]->setValue (v[i]);
        sliceKnobs[(size_t) i]->setEnabled (has);
    }
    reverse->setSelected (sp.reverse);
    lockButton->setSelected (proc.isLocked (sel));
    for (auto* b : { reverse.get(), resetSlice.get(), applyAll.get(), lockButton.get() })
        b->setEnabled (has);
}

void DaftChunkEditor::zoomWaveform (double factor, float at)
{
    wave->refresh();
    wave->zoomAround ((float) wave->getWidth() * at, factor);
}

void DaftChunkEditor::snapView()
{
    snapForSnapshot();
    updateToolbar();
    editorTick (0.0);
}

void DaftChunkEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), acceptedExtensions());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                                  proc.loadFile (f);
                              updateToolbar();
                          });
}

void DaftChunkEditor::updateGenerator()
{
    const bool has = proc.getSample() != nullptr && proc.getKit() != nullptr && ! proc.getKit()->slices.empty();
    dice->setEnabled (has);
    dice->setSelected (has);
    const int sel = proc.getSelectedIdea();
    encore->setEnabled (has && sel >= 0);
    undoButton->setEnabled (proc.canUndo());
    redoButton->setEnabled (proc.canRedo());
    if (proc.getStatus().isNotEmpty() && proc.getSample() != nullptr)
        ideaInfo->set (proc.getStatus(), true);
    else if (sel >= 0)
    {
        const auto& pt = proc.getIdea (sel).pattern;
        ideaInfo->set ("LOOP " + juce::String (sel + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (pt.bars) + (pt.bars > 1 ? " BARS" : " BAR")
                       + juce::String::fromUTF8 (" \xc2\xb7 #") + juce::String ((juce::int64) pt.seed % 100000));
        ideaInfo->setTooltip ("The loop's seed: the same seed gives the same loop. CHOP " + juce::String (juce::roundToInt (pt.chop * 100.0f)) + " %, " + styleName (pt.style));
    }
    else
        ideaInfo->set (has ? juce::String ("Click LOOP CREATOR!") : juce::String ("Load a sample"), true);
}

void DaftChunkEditor::showLengthMenu()
{
    juce::PopupMenu m;
    auto& c = proc.getChoiceParameter (DaftChunkEngine::lengthChoice);
    const char* names[] { "AUTO (from the style)", "1 BAR", "2 BARS", "4 BARS" };
    for (int i = 0; i < 4; ++i)
        m.addItem (i + 1, names[i], true, c.getIndex() == i);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (length.get()),
                     [this] (int r)
                     {
                         if (r < 1)
                             return;
                         auto& ch = proc.getChoiceParameter (DaftChunkEngine::lengthChoice);
                         ch.beginChangeGesture();
                         ch.setValueNotifyingHost (ch.convertTo0to1 ((float) (r - 1)));
                         ch.endChangeGesture();
                     });
}

void DaftChunkEditor::showStyleMenu()
{
    juce::PopupMenu m;
    auto& c = proc.getChoiceParameter (DaftChunkEngine::styleChoice);
    for (int i = 0; i < numStyles; ++i)
        m.addItem (i + 1, styleName ((Style) i), true, c.getIndex() == i);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (style.get()),
                     [this] (int r)
                     {
                         if (r < 1)
                             return;
                         auto& ch = proc.getChoiceParameter (DaftChunkEngine::styleChoice);
                         ch.beginChangeGesture();
                         ch.setValueNotifyingHost (ch.convertTo0to1 ((float) (r - 1)));
                         ch.endChangeGesture();
                         style->setText (styleName ((Style) (r - 1)));
                     });
}

void DaftChunkEditor::showSyncMenu()
{
    juce::PopupMenu m;
    const int cur = proc.getSync();
    const double host = proc.getHostBpm();
    m.addSectionHeader (host > 0.0 ? "Host: " + juce::String (host, 2) + " BPM" : juce::String ("No host tempo"));
    m.addItem (2, "TEMPO (same pitch)", true, cur == DaftChunkEngine::tempo);
    m.addItem (3, "VINYL (the pitch follows)", true, cur == DaftChunkEngine::vinyl);
    m.addItem (1, "FREE (the sample's own speed)", true, cur == DaftChunkEngine::free);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sync.get()),
                     [this] (int r)
                     {
                         if (r >= 1)
                             proc.setSync (r - 1);
                         updateToolbar();
                     });
}

void DaftChunkEditor::showModeMenu()
{
    juce::PopupMenu m;
    const auto cur = proc.getSettings().mode;
    m.addItem (1, "TRANSIENTS", true, cur == SliceMode::transients);
    m.addItem (2, "GRID", true, cur == SliceMode::grid);
    m.addItem (3, "MIXED", true, cur == SliceMode::mixed);
    m.addSeparator();
    m.addItem (4, "Back to the automatic cuts", proc.hasUserCuts());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mode.get()),
                     [this] (int r)
                     {
                         if (r >= 1 && r <= 3)
                             proc.setMode ((SliceMode) (r - 1));
                         else if (r == 4)
                             proc.resetCuts();
                         updateToolbar();
                     });
}

bool DaftChunkEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAcceptedFile (f))
            return true;
    return false;
}

void DaftChunkEditor::fileDragEnter (const juce::StringArray&, int, int) { wave->setDropHighlight (true); }
void DaftChunkEditor::fileDragExit (const juce::StringArray&) { wave->setDropHighlight (false); }

void DaftChunkEditor::filesDropped (const juce::StringArray& files, int, int)
{
    wave->setDropHighlight (false);
    for (const auto& f : files)
        if (isAcceptedFile (f))
        {
            proc.loadFile (juce::File (f));
            break;
        }
    updateToolbar();
}
} // namespace daftchunk
