#include "DaftChunkEditor.h"
#include "ChunkyMascot.h"
#include "WaveformView.h"

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
        const juce::Rectangle<float> areas[] { { 6, 62, 848, 238 }, { 6, 306, 176, 206 }, { 188, 306, 480, 206 }, { 674, 306, 180, 206 } };
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
        g.drawText ("SENS " + juce::String (juce::roundToInt (value * 100.0f)) + " %", b, juce::Justification::centred);
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

/** The 8 idea slots (step 3): empty for now. */
class DaftChunkEditor::IdeaSlots final : public juce::Component, public juce::SettableTooltipClient
{
public:
    IdeaSlots() { setTooltip (juce::String::fromUTF8 ("Les 8 emplacements d'id\xc3\xa9""es : les boucles que g\xc3\xa9n\xc3\xa8re le D\xc3\x89, \xc3\xa0 glisser dans Ableton (\xc3\xa9tape 3)")); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = (b.getWidth() - 6.0f) / 2.0f, h = (b.getHeight() - 9.0f) / 4.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto r = juce::Rectangle<float> (b.getX() + (float) (i % 2) * (w + 6.0f), b.getY() + (float) (i / 2) * (h + 3.0f), w, h);
            g.setColour (surface.withAlpha (0.7f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (cream.withAlpha (0.25f));
            g.setFont (gk::GrooveFonts::font (12.0f));
            g.drawText (juce::String (i + 1), r, juce::Justification::centred);
        }
    }
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

    wave = std::make_unique<WaveformView> (p);
    addToCanvas (*wave, { 14, 100, 832, 192 });
    keys = std::make_unique<SliceKeys> (p);
    addToCanvas (*keys, { 6, 518, 848, 56 });

    // the toolbar
    load = std::make_unique<Pill> ("CHARGER", [this] { chooseFile(); });
    load->setTooltip ("Charger un sample (WAV, AIFF, FLAC, MP3, 60 s au plus). Tu peux aussi le glisser sur le plug-in.");
    addToCanvas (*load, { 14, 68, 80, 26 });
    fileName = std::make_unique<InfoLabel>();
    addToCanvas (*fileName, { 100, 68, 150, 26 });

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
    bpm->setTooltip (juce::String::fromUTF8 ("Le tempo du sample (estim\xc3\xa9). Clique pour le taper au clavier ; /2 ou x2 si l'analyse a pris la moiti\xc3\xa9 ou le double ; TAP pour le donner \xc3\xa0 la main."));
    bpm->onTextChange = [this]
    {
        const double v = bpm->getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue();
        proc.setBpm (v > 0.0 ? v : -1.0);
        updateToolbar();
    };
    addToCanvas (*bpm, { 256, 68, 84, 26 });
    half = std::make_unique<Pill> (juce::String ("/2"), [this] { proc.halveBpm(); updateToolbar(); });
    twice = std::make_unique<Pill> (juce::String ("x2"), [this] { proc.doubleBpm(); updateToolbar(); });
    tapButton = std::make_unique<Pill> ("TAP", [this] { proc.tap (juce::Time::getMillisecondCounterHiRes() * 0.001); updateToolbar(); });
    tapButton->setTooltip ("Tape le tempo (3 coups au moins)");
    addToCanvas (*half, { 344, 68, 28, 26 });
    addToCanvas (*twice, { 376, 68, 28, 26 });
    addToCanvas (*tapButton, { 408, 68, 40, 26 });
    keyLabel = std::make_unique<InfoLabel>();
    keyLabel->setTooltip (juce::String::fromUTF8 ("La tonalit\xc3\xa9 (indicative) : les tranches pitch\xc3\xa9""es la suivront"));
    addToCanvas (*keyLabel, { 454, 68, 66, 26 });

    mode = std::make_unique<Pill> ("TRANSITOIRES", [this] { showModeMenu(); }, 12.0f);
    mode->setTooltip (juce::String::fromUTF8 ("D\xc3\xa9""coupe : TRANSITOIRES (\xc3\xa0 chaque attaque), GRILLE (1/4, 1/8, 1/16 depuis le temps 1), MIXTE (la grille, cal\xc3\xa9""e sur l'attaque la plus proche \xc3\xa0 \xc2\xb1""20 ms)"));
    addToCanvas (*mode, { 526, 68, 118, 26 });
    const int divs[] { 4, 8, 16 };
    for (int i = 0; i < 3; ++i)
    {
        const int d = divs[i];
        divisions[(size_t) i] = std::make_unique<Pill> ("1/" + juce::String (d), [this, d] { proc.setDivision (d); updateToolbar(); }, 11.5f);
        divisions[(size_t) i]->setTooltip ("La grille (GRILLE et MIXTE)");
        addToCanvas (*divisions[(size_t) i], { 650 + 36 * i, 68, 34, 26 });
    }
    sensitivity = std::make_unique<MiniSlider>();
    sensitivity->setTooltip (juce::String::fromUTF8 ("Sensibilit\xc3\xa9 aux transitoires : \xc3\xa0 gauche seulement les coups francs, \xc3\xa0 droite le moindre petit"));
    sensitivity->onChange = [this] (float v) { proc.setSensitivity (v); };
    addToCanvas (*sensitivity, { 762, 68, 84, 26 });

    // the generator (step 3), with the idea slots on the right
    dice = std::make_unique<Pill> (juce::String::fromUTF8 ("D\xc3\x89"), [] {}, 15.0f);
    encore = std::make_unique<Pill> ("ENCORE", [] {}, 15.0f);
    style = std::make_unique<Pill> ("FILTER TOUCH", [] {}, 12.0f);
    for (auto* b : { dice.get(), encore.get(), style.get() })
    {
        b->setEnabled (false);
        b->setTooltip (juce::String::fromUTF8 ("\xc3\x89tape 3 : le g\xc3\xa9n\xc3\xa9rateur d'id\xc3\xa9""es"));
    }
    addToCanvas (*dice, { 682, 316, 78, 32 });
    addToCanvas (*encore, { 766, 316, 80, 32 });
    addToCanvas (*style, { 682, 354, 164, 26 });
    ideas = std::make_unique<IdeaSlots>();
    addToCanvas (*ideas, { 682, 388, 164, 116 });

    // the pages under the tabs: SON (the host parameters) and TRANCHE (the selected slice's own)
    tabSound = std::make_unique<Pill> ("SON", [this] { showPage (0); }, 12.0f);
    tabSlice = std::make_unique<Pill> ("TRANCHE", [this] { showPage (1); }, 12.0f);
    tabSound->setTooltip (juce::String::fromUTF8 ("Le son de toutes les tranches : l'enveloppe, le pitch, le filtre, la v\xc3\xa9locit\xc3\xa9, le mode"));
    tabSlice->setTooltip (juce::String::fromUTF8 ("Les r\xc3\xa9glages de la tranche s\xc3\xa9lectionn\xc3\xa9""e (clique une tranche ou une touche)"));
    addToCanvas (*tabSound, { 340, 314, 64, 22 });
    addToCanvas (*tabSlice, { 408, 314, 82, 22 });
    sliceTitle = std::make_unique<InfoLabel>();
    addToCanvas (*sliceTitle, { 494, 314, 168, 22 });

    {
        using E = DaftChunkEngine;
        struct K { int knob; juce::Rectangle<int> at; juce::Colour colour; };
        const K ks[] { { E::attackKnob, { 336, 342, 66, 70 }, cream }, { E::decayKnob, { 402, 342, 66, 70 }, cream },
                       { E::sustainKnob, { 468, 342, 66, 70 }, cream }, { E::releaseKnob, { 534, 342, 66, 70 }, coral },
                       { E::pitchKnob, { 336, 424, 66, 70 }, coral }, { E::cutoffKnob, { 402, 424, 66, 70 }, coral },
                       { E::resoKnob, { 468, 424, 66, 70 }, cream }, { E::velocityKnob, { 534, 424, 66, 70 }, cream },
                       { E::volumeKnob, { 600, 430, 66, 70 }, cream } };
        for (const auto& k : ks)
        {
            soundKnobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (k.knob), getPalette(), p.getConfig().knobs[(size_t) k.knob].caption, k.colour));
            addToCanvas (*soundKnobs.back(), k.at);
        }
        modeSelector = std::make_unique<gk::ChoiceSelector> (p.getChoiceParameter (E::modeChoice), "MODE", coral, getPalette());
        modeSelector->setTooltip (juce::String::fromUTF8 ("COUPE : une note coupe la pr\xc3\xa9""c\xc3\xa9""dente (comme un pad de MPC). POLY : elles se superposent (16 voix)"));
        addToCanvas (*modeSelector, { 602, 342, 62, 38 });
        playSelector = std::make_unique<gk::ChoiceSelector> (p.getChoiceParameter (E::playChoice), "LECTURE", coral, getPalette());
        playSelector->setTooltip (juce::String::fromUTF8 ("ONE SHOT : la tranche joue jusqu'au bout. GATE : rel\xc3\xa2""cher la touche lance le RELEASE"));
        addToCanvas (*playSelector, { 602, 386, 62, 38 });
    }

    {
        auto st = [] (float v) { const int x = juce::roundToInt (v); return (x > 0 ? "+" : "") + juce::String (x) + " st"; };
        auto db = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
        auto oct = [] (float v) { return v > -0.05f ? juce::String ("OPEN") : juce::String (v, 1) + " oct"; };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        sliceKnobs[0] = std::make_unique<SliceKnob> ("PITCH", -24.0f, 24.0f, 0.0f, true, st);
        sliceKnobs[1] = std::make_unique<SliceKnob> ("GAIN", -24.0f, 12.0f, 0.0f, true, db);
        sliceKnobs[2] = std::make_unique<SliceKnob> ("FILTER", -8.0f, 0.0f, 0.0f, false, oct);
        sliceKnobs[3] = std::make_unique<SliceKnob> (juce::String::fromUTF8 ("D\xc3\x89""BUT"), 0.0f, 0.9f, 0.0f, false, pct);
        sliceKnobs[4] = std::make_unique<SliceKnob> ("FIN", 0.0f, 0.9f, 0.0f, false, pct);
        const char* tips[] { "Le pitch de cette tranche (en plus du PITCH global)", "Le volume de cette tranche",
                             "Le filtre de cette tranche : sous le FILTER global, en octaves", "Le d\xc3\xa9""but fin : rogne le d\xc3\xa9""but de la tranche",
                             "La fin fine : rogne la fin de la tranche" };
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
        reverse = std::make_unique<Pill> ("INVERSER", [this]
        {
            auto sp = proc.getSliceParams (proc.getSelectedSlice());
            sp.reverse = ! sp.reverse;
            proc.setSliceParams (proc.getSelectedSlice(), sp);
        }, 12.0f);
        reverse->setTooltip (juce::String::fromUTF8 ("Joue la tranche \xc3\xa0 l'envers"));
        resetSlice = std::make_unique<Pill> (juce::String::fromUTF8 ("R\xc3\x89INIT."), [this] { proc.resetSliceParams (proc.getSelectedSlice()); }, 12.0f);
        resetSlice->setTooltip (juce::String::fromUTF8 ("Remet cette tranche \xc3\xa0 z\xc3\xa9ro"));
        applyAll = std::make_unique<Pill> (juce::String::fromUTF8 ("\xc3\x80 TOUTES"), [this] { proc.applyToAllSlices (proc.getSelectedSlice()); }, 12.0f);
        applyAll->setTooltip (juce::String::fromUTF8 ("Donne les r\xc3\xa9glages de cette tranche \xc3\xa0 toutes les tranches"));
        addToCanvas (*reverse, { 340, 440, 100, 30 });
        addToCanvas (*resetSlice, { 448, 440, 90, 30 });
        addToCanvas (*applyAll, { 546, 440, 116, 30 });
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
        fileName->set (juce::String::fromUTF8 ("Analyse\xe2\x80\xa6"), true);
    else if (s != nullptr)
        fileName->set (s->name);
    else
        fileName->set (proc.getStatus().isNotEmpty() ? proc.getStatus() : juce::String ("Pas de sample"), true);
    fileName->setTooltip (s != nullptr ? (s->path.isNotEmpty() ? s->path : s->name) + (proc.getStatus().isNotEmpty() ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + proc.getStatus() : juce::String()) : proc.getStatus());

    if (! bpm->isBeingEdited())
        bpm->setText (has ? juce::String (proc.getBpm(), 2) + " BPM" : juce::String (juce::String::fromUTF8 ("\xe2\x80\x94 BPM")), juce::dontSendNotification);
    keyLabel->set (has ? s->analysis.key.name() : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), ! has);
    for (auto* b : { half.get(), twice.get(), tapButton.get() })
        b->setEnabled (has);
    bpm->setEnabled (has);

    const auto& st = proc.getSettings();
    mode->setText (st.mode == SliceMode::transients ? "TRANSITOIRES" : st.mode == SliceMode::grid ? "GRILLE" : "MIXTE");
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
    if (proc.getKitVersion() != shownVersion || busy != wasBusy || busy)
    {
        shownVersion = proc.getKitVersion();
        wasBusy = busy;
        if (load != nullptr)
            updateToolbar();
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
    sliceTitle->set (has ? "TRANCHE " + juce::String (sel + 1) + dot + typeName (kit->slices[(size_t) sel].type) + dot + SliceKeys::noteName (DaftChunkEngine::baseNote + sel)
                         : juce::String ("PAS DE TRANCHE"),
                     ! has);
    const float v[] { sp.pitch, sp.gainDb, sp.filter, sp.start, sp.end };
    for (int i = 0; i < 5; ++i)
    {
        sliceKnobs[(size_t) i]->setValue (v[i]);
        sliceKnobs[(size_t) i]->setEnabled (has);
    }
    reverse->setSelected (sp.reverse);
    for (auto* b : { reverse.get(), resetSlice.get(), applyAll.get() })
        b->setEnabled (has);
}

void DaftChunkEditor::snapView()
{
    snapForSnapshot();
    updateToolbar();
    editorTick (0.0);
}

void DaftChunkEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Charger un sample", juce::File(), acceptedExtensions());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                                  proc.loadFile (f);
                              updateToolbar();
                          });
}

void DaftChunkEditor::showModeMenu()
{
    juce::PopupMenu m;
    const auto cur = proc.getSettings().mode;
    m.addItem (1, "TRANSITOIRES", true, cur == SliceMode::transients);
    m.addItem (2, "GRILLE", true, cur == SliceMode::grid);
    m.addItem (3, "MIXTE", true, cur == SliceMode::mixed);
    m.addSeparator();
    m.addItem (4, "Revenir aux coupes automatiques", proc.hasUserCuts());
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
