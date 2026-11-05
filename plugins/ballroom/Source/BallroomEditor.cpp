#include "BallroomEditor.h"
#include "MiraMascot.h"

namespace bl
{
namespace
{
    // Mira's colours: silver facets, the night, champagne, her gold shoes and rim, the navy of her glasses
    const juce::Colour silver (0xffc9ced6), silverDeep (0xff8b93a3), night (0xff14101f), champagne (0xffe8d8b0), gold (0xffd9a928),
        navy (0xff1c2a6b), white (0xffffffff);
} // namespace

/** SPACE's face: a mirror ball — facets on a sphere (rows of latitude, the
    columns turning with the knob), each a shade of silver with now and then
    a white flash or a touch of navy, the sphere's shading over them and a
    gold rim. The knob's cap is drawn on top by BigKnob. */
static void paintMirrorFace (juce::Graphics& g, juce::Rectangle<float> body, float value, float angle)
{
    juce::ignoreUnused (value);
    const auto c = body.getCentre();
    const float r = 0.5f * juce::jmin (body.getWidth(), body.getHeight());
    juce::Graphics::ScopedSaveState state (g);
    juce::Path disc;
    disc.addEllipse (body);
    g.reduceClipRegion (disc);
    g.setColour (silverDeep.darker (0.6f));
    g.fillEllipse (body);
    constexpr int rows = 11, cols = 22;
    const float pi = juce::MathConstants<float>::pi;
    auto shade = [] (int i, int j)
    {
        auto x = (std::uint32_t) (i * 73856093) ^ (std::uint32_t) (j * 19349663);
        x ^= x >> 13;
        x *= 0x5bd1e995u;
        x ^= x >> 15;
        const float h = (float) (x & 0xffffu) / 65535.0f;
        if (h > 0.93f)
            return white;
        if (h < 0.06f)
            return navy.interpolatedWith (silver, 0.35f);
        return silverDeep.interpolatedWith (silver, h);
    };
    const float spin = angle; // the facets turn with the knob
    for (int i = 0; i < rows; ++i)
    {
        const float lat0 = -0.5f * pi + pi * (float) i / rows, lat1 = -0.5f * pi + pi * (float) (i + 1) / rows;
        for (int j = 0; j < cols; ++j)
        {
            const float lon0 = 2.0f * pi * (float) j / cols + spin, lon1 = 2.0f * pi * (float) (j + 1) / cols + spin;
            if (std::cos (0.5f * (lon0 + lon1)) < -0.05f)
                continue; // the back of the ball
            auto pt = [&] (float lat, float lon) { return juce::Point<float> (c.x + r * std::cos (lat) * std::sin (lon), c.y - r * std::sin (lat)); };
            juce::Path q;
            q.startNewSubPath (pt (lat0, lon0));
            q.lineTo (pt (lat0, lon1));
            q.lineTo (pt (lat1, lon1));
            q.lineTo (pt (lat1, lon0));
            q.closeSubPath();
            g.setColour (shade (i, j));
            g.fillPath (q);
            g.setColour (night.withAlpha (0.55f));
            g.strokePath (q, juce::PathStrokeType (0.8f));
        }
    }
    // the sphere's light: bright upper left, dark lower right
    juce::ColourGradient light (white.withAlpha (0.35f), c.x - 0.45f * r, c.y - 0.5f * r, night.withAlpha (0.55f), c.x + 0.7f * r, c.y + 0.8f * r, true);
    g.setGradientFill (light);
    g.fillEllipse (body);
    // the gold rim
    g.setColour (gold);
    g.drawEllipse (body.reduced (1.5f), 3.0f);
}

gk::Palette BallroomEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff221b33); // the night, a touch lighter in the middle
    p.backgroundEdge = night;
    p.surface = juce::Colour (0xff302845);
    p.outline = juce::Colour (0xff09070f);
    p.text = champagne;
    p.accent = gold;
    p.beret = gold; // power button, "LALZIN"
    p.track = white.withAlpha (0.12f);
    p.pointer = night;
    p.plaque = champagne;
    p.arcStops = { silver, champagne, gold };
    // SPACE: a mirror ball seen from the front, facets of silver, a gold rim
    p.knobBands = { silver, silverDeep, white.withAlpha (1.0f).interpolatedWith (silver, 0.4f), silverDeep, silver, silverDeep };
    p.knobRings = {};
    p.knobRim = gold;
    p.pulse = champagne;
    p.arrowLeft = silver;
    p.arrowRight = gold;
    p.frameLeft = silver;
    p.frameRight = gold;
    p.lowLabel = silver;  // ROOM
    p.highLabel = gold;   // COSMOS
    return p;
}

/** ROOM: opens / closes the panel with every setting. */
class BallroomEditor::RoomButton final : public juce::Button
{
public:
    explicit RoomButton (const gk::Palette& p) : juce::Button ("ROOM"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("ROOM: every setting (MAIN, EARLY, LATE), the decay curves, the mode and FREEZE");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? gold : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        g.setColour (palette (getToggleState() ? night : champagne));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.14f));
        g.drawText ("ROOM", b, juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
};

/** CUSTOM: lit when a ROOM setting moved since SPACE last did. */
class BallroomEditor::CustomChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CustomChip (const gk::Palette& p) : palette (p)
    {
        setTooltip ("CUSTOM: a ROOM setting moved, SPACE no longer drives MIX, DECAY, SIZE, DEPTH, PREDELAY and the modulation. "
                    "Move SPACE to take them back.");
        setInterceptsMouseClicks (false, false);
    }
    void set (bool on)
    {
        if (on != lit)
        {
            lit = on;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (palette (lit ? champagne : palette.surface).withAlpha (lit ? 0.95f : 0.5f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (lit ? night : champagne.withAlpha (0.4f)));
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        g.drawText ("CUSTOM", b, juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
    bool lit = false;
};

/** The ROOM panel: three tabs of settings over Mira and SPACE. */
class BallroomEditor::RoomPanel final : public juce::Component
{
public:
    RoomPanel (BallroomProcessor& p, const gk::Palette& pal) : palette (pal), decayView (p, pal)
    {
        setTitle ("Room settings");
        const char* tabNames[] { "MAIN", "EARLY", "LATE" };
        for (int t = 0; t < 3; ++t)
        {
            auto b = std::make_unique<TabButton> (tabNames[t], pal);
            b->onClick = [this, t] { setTab (t); };
            addAndMakeVisible (*b);
            tabs.push_back (std::move (b));
        }
        using K = BallroomDsp;
        auto add = [&] (int onTab, K::Knob k, juce::Colour c)
        {
            knobs.push_back ({ onTab, std::make_unique<gk::SmallKnob> (p.getKnob (k), pal, p.getConfig().knobs[(size_t) k].caption, c) });
            addChildComponent (*knobs.back().knob);
        };
        for (auto k : { K::mixKnob, K::predelayKnob, K::decayKnob, K::highCutKnob, K::depthKnob })
            add (0, k, k == K::decayKnob || k == K::depthKnob ? gold : silver);
        for (auto k : { K::earlySizeKnob, K::earlyCrossKnob, K::earlyModRateKnob, K::earlyModDepthKnob, K::earlySendKnob, K::diffusionKnob })
            add (1, k, k == K::earlySizeKnob || k == K::earlySendKnob ? gold : silver);
        for (auto k : { K::lateSizeKnob, K::lateCrossKnob, K::lateModRateKnob, K::lateModDepthKnob, K::bassMultKnob, K::bassXoverKnob, K::highMultKnob,
                        K::highXoverKnob })
            add (2, k, k == K::lateSizeKnob || k == K::bassMultKnob || k == K::highMultKnob ? gold : silver);
        mode = std::make_unique<gk::ChoiceSelector> (p.getChoice (K::modeChoice), "MODE", gold, pal);
        mode->setTooltip ("MODE: Studio (a small dense room), Club (warm, marked early), Ballroom (a big hall), Plate (bright, no early), "
                          "Mirror (metallic, shimmering), Space (huge, very modulated)");
        addChildComponent (*mode);
        freeze = std::make_unique<gk::ChoiceSelector> (p.getChoice (K::freezeChoice), "FREEZE", champagne, pal);
        freeze->setTooltip ("FREEZE: the tail holds forever, nothing new comes in");
        addChildComponent (*freeze);
        addChildComponent (decayView);
        setTab (0);
    }

    void setTab (int t)
    {
        tab = juce::jlimit (0, 2, t);
        for (int i = 0; i < 3; ++i)
            tabs[(size_t) i]->setToggleState (i == tab, juce::dontSendNotification);
        for (auto& k : knobs)
            k.knob->setVisible (k.tab == tab);
        mode->setVisible (tab == 0);
        decayView.setVisible (tab == 0);
        freeze->setVisible (tab == 2);
        resized();
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);
        area.removeFromTop (24); // the title
        auto tabRow = area.removeFromTop (26);
        const int tw = tabRow.getWidth() / 3;
        for (auto& t : tabs)
            t->setBounds (tabRow.removeFromLeft (tw).reduced (3, 0));
        area.removeFromTop (8);
        const int slot = area.getWidth() / 4, rowH = 84;
        int i = 0;
        for (auto& k : knobs)
            if (k.tab == tab)
            {
                k.knob->setBounds (juce::Rectangle<int> (area.getX() + (i % 4) * slot, area.getY() + (i / 4) * rowH, slot, rowH).withSizeKeepingCentre (66, 72));
                ++i;
            }
        if (tab == 0)
        {
            // MAIN: MIX PREDELAY DECAY HIGH CUT / DEPTH, MODE / the decay curves
            mode->setBounds (juce::Rectangle<int> (area.getX() + slot, area.getY() + rowH, slot * 2, rowH).withSizeKeepingCentre (slot * 2 - 16, 38));
            decayView.setBounds (area.withTrimmedTop (2 * rowH + 6));
        }
        if (tab == 2)
            freeze->setBounds (juce::Rectangle<int> (area.getX(), area.getY() + 2 * rowH + 8, area.getWidth(), 40).withSizeKeepingCentre (130, 38));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (juce::Colour (0xff2b2240)), b.getCentreX(), b.getY(), palette (night), b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (gold).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (champagne).withAlpha (0.9f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText ("ROOM", b.withHeight (30.0f).translated (0.0f, 4.0f), juce::Justification::centred);
    }

    void tick (double dt) { decayView.tick (dt); }

private:
    class TabButton final : public juce::Button
    {
    public:
        TabButton (const juce::String& name, const gk::Palette& p) : juce::Button (name), palette (p) {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            g.setColour (palette (on ? champagne : palette.surface).withMultipliedBrightness (over && ! on ? 1.2f : 1.0f));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (palette (on ? night : champagne.withAlpha (0.7f)));
            g.setFont (gk::GrooveFonts::font (10.5f).withExtraKerningFactor (0.14f));
            g.drawText (getName(), b, juce::Justification::centred);
        }

    private:
        const gk::Palette& palette;
    };

    /** The decay, drawn: the early reflections (bars over EARLY SIZE, as
        loud as DEPTH leaves them) and the late tail of the lows, mids and
        highs (-60 dB at DECAY x BASS MULT, DECAY, DECAY x HIGH MULT), a light
        running along it; FREEZE: flat lines. */
    class DecayView final : public juce::Component
    {
    public:
        DecayView (BallroomProcessor& p, const gk::Palette& pal) : proc (p), palette (pal) { setInterceptsMouseClicks (false, false); }
        void tick (double dt)
        {
            const float decay = value (BallroomDsp::decayKnob);
            phase = std::fmod (phase + (float) dt / juce::jmax (0.4f, juce::jmin (6.0f, decay)), 1.0f);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (palette (night).withAlpha (0.6f));
            g.fillRoundedRectangle (b, 8.0f);
            const auto plot = b.reduced (10.0f, 8.0f).withTrimmedBottom (12.0f);
            const float decay = value (BallroomDsp::decayKnob), bass = value (BallroomDsp::bassMultKnob), high = value (BallroomDsp::highMultKnob);
            const float depth = value (BallroomDsp::depthKnob), earlyMs = value (BallroomDsp::earlySizeKnob), pre = value (BallroomDsp::predelayKnob);
            const bool frozen = proc.getChoice (BallroomDsp::freezeChoice).getIndex() == 1;
            const float span = juce::jlimit (0.5f, 30.0f, 1.15f * decay * juce::jmax (1.0f, bass)); // seconds shown
            auto x = [&] (float t) { return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, t / span); };
            auto y = [&] (float db) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };
            // grid: -20 / -40 dB
            g.setColour (palette (white).withAlpha (0.07f));
            for (float db : { -20.0f, -40.0f })
                g.drawHorizontalLine ((int) y (db), plot.getX(), plot.getRight());
            // the early reflections
            const float th = depth * juce::MathConstants<float>::halfPi;
            const float earlyLevel = std::cos (th);
            g.setColour (palette (champagne).withAlpha (0.75f));
            for (int k = 0; k < 10; ++k)
            {
                const float t = (pre + earlyMs * ((float) k + 0.5f) / 10.0f) * 0.001f;
                const float db = 20.0f * std::log10 (juce::jmax (1.0e-3f, earlyLevel * (1.0f - 0.7f * (float) k / 10.0f)));
                g.drawLine (x (t), y (db), x (t), plot.getBottom(), 2.0f);
            }
            // the late tails (the late level from DEPTH)
            const float lateDb = 20.0f * std::log10 (juce::jmax (1.0e-3f, std::sin (th)));
            struct Band { float rt; juce::Colour c; const char* name; };
            const Band bands[] { { decay * bass, silverDeep, "LOW" }, { decay, gold, "MID" }, { decay * high, silver, "HIGH" } };
            for (const auto& band : bands)
            {
                // a straight fall (in dB) until -60 dB: each band its own slope
                juce::Path p;
                const float t0 = pre * 0.001f + earlyMs * 0.0005f;
                p.startNewSubPath (x (t0), y (lateDb));
                if (frozen)
                    p.lineTo (x (span), y (lateDb));
                else
                {
                    const float tEnd = juce::jmin (span, t0 + band.rt * (60.0f + lateDb) / 60.0f);
                    p.lineTo (x (tEnd), y (lateDb - 60.0f * (tEnd - t0) / juce::jmax (0.05f, band.rt)));
                }
                g.setColour (palette (band.c).withAlpha (0.9f));
                g.strokePath (p, juce::PathStrokeType (2.0f));
            }
            // a light running along the mids' tail
            if (! frozen)
            {
                const float t = phase * span;
                const float db = lateDb - 60.0f / juce::jmax (0.05f, decay) * t;
                if (db > -60.0f)
                {
                    g.setColour (palette (white).withAlpha (0.9f));
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x (t), y (db) }));
                }
            }
            g.setFont (gk::GrooveFonts::font (9.0f).withExtraKerningFactor (0.1f));
            g.setColour (palette (champagne).withAlpha (0.7f));
            const auto legend = b.removeFromBottom (14.0f).reduced (10.0f, 0.0f);
            g.drawText (frozen ? juce::String ("FROZEN") : "DECAY " + juce::String (decay, decay < 10.0f ? 2 : 1) + " s", legend, juce::Justification::centredLeft);
            g.drawText ("LOW  MID  HIGH", legend, juce::Justification::centredRight);
        }

    private:
        float value (BallroomDsp::Knob k) { auto& p = proc.getKnob (k); return p.convertFrom0to1 (p.getValue()); }
        BallroomProcessor& proc;
        const gk::Palette& palette;
        float phase = 0.0f;
    };

    struct KnobEntry { int tab; std::unique_ptr<gk::SmallKnob> knob; };
    const gk::Palette& palette;
    std::vector<std::unique_ptr<TabButton>> tabs;
    std::vector<KnobEntry> knobs;
    std::unique_ptr<gk::ChoiceSelector> mode, freeze;
    DecayView decayView;
    int tab = 0;
};

BallroomEditor::BallroomEditor (BallroomProcessor& p)
    : OneKnobEditor (p, makePalette(), BL_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures& f, const gk::Palette& palette) { return std::make_unique<MiraMascot> (p, f, palette); }),
      proc (p)
{
    auto& pal = getPalette();
    getMascot().setBounds (60, 52, 300, 182); // Mira is tall (her chain)
    getKnob().setBodyPainter (paintMirrorFace); // SPACE: a mirror ball (Mira's colours)
    panel = std::make_unique<RoomPanel> (p, pal);
    addToCanvas (*panel, { 8, 50, 404, 364 }); // (ROOM and CUSTOM stay visible under it)
    panel->setVisible (false);
    room = std::make_unique<RoomButton> (pal);
    addToCanvas (*room, { 342, 420, 70, 28 });
    room->onClick = [this] { panel->setVisible (room->getToggleState()); };
    customChip = std::make_unique<CustomChip> (pal);
    addToCanvas (*customChip, { 10, 424, 74, 20 });
}

BallroomEditor::~BallroomEditor() = default;

void BallroomEditor::showRoom (int tab)
{
    room->setToggleState (tab >= 0, juce::dontSendNotification);
    panel->setVisible (tab >= 0);
    if (tab >= 0)
        panel->setTab (tab);
}

void BallroomEditor::editorTick (double dt)
{
    customChip->set (proc.isCustom());
    if (panel->isVisible())
        panel->tick (dt);
}
} // namespace bl
