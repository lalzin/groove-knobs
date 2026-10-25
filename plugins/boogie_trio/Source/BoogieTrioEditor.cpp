#include "BoogieTrioEditor.h"
#include "VelvetMascot.h"

namespace bt
{
namespace
{
const juce::Colour violet (0xff7a4fd0), orange (0xfff29a38), pink (0xffe8508a), cream (0xfff3e9d2);
}

gk::Palette BoogieTrioEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff36363c); // same charcoal as French Filter
    p.backgroundEdge = juce::Colour (0xff1b1b1f);
    p.surface = juce::Colour (0xff45454d);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = orange;
    p.beret = pink; // power button
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff1a1a1d);
    p.plaque = juce::Colour (0xffefe6d0);
    p.arcStops = { cream, orange, pink };
    p.knobBands = { violet, orange, pink }; // the three voices
    p.arrowLeft = violet;
    p.arrowRight = orange;
    p.frameLeft = violet;
    p.frameRight = pink;
    p.lowLabel = violet;  // SOLO
    p.highLabel = pink;   // TRIO
    return p;
}

/** "⋯": opens / closes the settings panel. */
class BoogieTrioEditor::MoreButton final : public juce::Button
{
public:
    explicit MoreButton (const gk::Palette& p) : juce::Button ("Settings"), palette (p)
    {
        setClickingTogglesState (true);
        setTooltip ("More: chorus rate, depth, vibrato, tape drift, tone, low cut, feedback, room, tremolo, phaser, mix, width");
        setWantsKeyboardFocus (true);
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (palette (getToggleState() ? orange : palette.surface).withMultipliedBrightness (over ? 1.15f : 1.0f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        g.setColour (palette (getToggleState() ? palette.outline : palette.text));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b.getCentre().translated (7.0f * (float) i, 0.0f)));
    }

private:
    const gk::Palette& palette;
};

/** The settings the front panel leaves out, sorted: VOICES, COLOUR, MOTION. */
class BoogieTrioEditor::SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (BoogieTrioProcessor& p, const gk::Palette& pal) : palette (pal)
    {
        setTitle ("More settings");
        for (int id = 0; id < BoogieTrioDsp::numKnobs; ++id)
        {
            const int row = id / 4;
            knobs.push_back (std::make_unique<gk::SmallKnob> (p.getKnobParameter (id), palette, p.getConfig().knobs[(size_t) id].caption,
                                                              juce::Colour (rowColours[row])));
            addAndMakeVisible (*knobs.back());
        }
    }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        const int slotW = area.getWidth() / 4;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (juce::Rectangle<int> (area.getX() + (int) (i % 4) * slotW, area.getY() + (int) (i / 4) * rowH + 14, slotW, rowH - 14)
                                     .withSizeKeepingCentre (64, 68));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (palette (violet).darker (0.9f), b.getCentreX(), b.getY(), palette (palette.backgroundEdge), b.getCentreX(),
                                                 b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (palette (orange).withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        g.setColour (palette (cream).withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (12.0f).withExtraKerningFactor (0.2f));
        g.drawText ("MORE", b.withHeight (28.0f), juce::Justification::centred);
        const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (30);
        g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.14f));
        const char* captions[] { "VOICES", "COLOUR", "MOTION" };
        for (int row = 0; row < 3; ++row)
        {
            const auto r = juce::Rectangle<int> (area.getX() + 6, area.getY() + row * rowH, area.getWidth() - 12, 12).toFloat();
            g.setColour (palette (juce::Colour (rowColours[row])).withAlpha (0.95f));
            g.drawText (captions[row], r, juce::Justification::centredLeft);
            g.setColour (palette (cream).withAlpha (0.25f));
            g.fillRect (r.getX(), r.getBottom() + 1.0f, r.getWidth(), 1.0f);
        }
    }

private:
    static constexpr int rowH = 88;
    static constexpr juce::uint32 rowColours[3] { 0xff9a78e6, 0xfff29a38, 0xffe8508a }; // the three voices (violet lifted for the dark panel)
    const gk::Palette& palette;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
};

BoogieTrioEditor::BoogieTrioEditor (BoogieTrioProcessor& p)
    : OneKnobEditor (p, makePalette(), BT_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<VelvetMascot> (p, palette); })
{
    // The trio needs the full width of the mascot band
    getMascot().setBounds (10, 58, 400, 172);
    // "⋯" bottom right, its panel over Velvet and the knob
    panel = std::make_unique<SettingsPanel> (p, getPalette());
    addToCanvas (*panel, { 14, 56, 392, 310 });
    panel->setVisible (false);
    more = std::make_unique<MoreButton> (getPalette());
    addToCanvas (*more, { 362, 418, 46, 26 });
    more->onClick = [this] { panel->setVisible (more->getToggleState()); };
}

BoogieTrioEditor::~BoogieTrioEditor() = default;

void BoogieTrioEditor::setPanelOpen (bool open)
{
    more->setToggleState (open, juce::dontSendNotification);
    panel->setVisible (open);
}
} // namespace bt
