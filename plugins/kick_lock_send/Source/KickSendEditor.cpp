#include "KickSendEditor.h"
#include <KickLockArt.h>

namespace kls
{
namespace
{
    const juce::Colour limeDeep (0xff4e9a1c), lime (0xffa4dc3a), orange (0xffff8a3d), cream (0xfff3e9d2), green (0xff3ddc84), grey (0xff7a8499);

    std::vector<juce::Image> phazeFrames()
    {
        std::vector<juce::Image> frames;
        for (int i = 0;; ++i)
        {
            int size = 0;
            const auto name = "phaze_" + juce::String (i) + "_png";
            const char* data = KLArt::getNamedResource (name.toRawUTF8(), size);
            if (data == nullptr)
                break;
            if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
                frames.push_back (img);
        }
        return frames;
    }
} // namespace

/** "LINKED → Kick Lock" / "WAITING FOR KICK LOCK", and a dot that flashes on each kick. */
class KickSendEditor::LinkStatus final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LinkStatus (KickSendProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
        setTooltip ("Put Kick Lock on the bass: it receives this track's kicks (TRIGGER Auto or Link), no sidechain routing needed. "
                    "The host must be playing.");
    }

    void update (double dt)
    {
        auto& sender = proc.getDsp().getSender();
        const int st = ! sender.isConnected() ? 0 : sender.isListenedTo() ? 2 : 1;
        const int kicks = proc.getDsp().getTelemetry().kicks.load (std::memory_order_relaxed);
        flash = kicks != lastKicks ? 1.0f : juce::jmax (0.0f, flash - (float) dt / 0.15f);
        lastKicks = kicks;
        if (st != state || flash > 0.0f || juce::exactlyEqual (flash, 0.0f) != wasDark)
        {
            state = st;
            wasDark = juce::exactlyEqual (flash, 0.0f);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        const auto c = state == 2 ? green : grey;
        g.setColour (palette (juce::Colour (0xff121318)).withAlpha (0.85f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (c).withAlpha (0.7f));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.5f);
        const auto dot = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ b.getX() + 15.0f, b.getCentreY() });
        g.setColour (palette (orange).withAlpha (0.25f + 0.75f * flash));
        g.fillEllipse (dot);
        g.setColour (palette (cream));
        g.setFont (gk::GrooveFonts::font (13.0f).withExtraKerningFactor (0.05f));
        const juce::String text = state == 2 ? juce::String::fromUTF8 ("LINKED \xe2\x86\x92 KICK LOCK") : state == 1 ? "WAITING FOR KICK LOCK" : "LINK UNAVAILABLE";
        g.drawFittedText (text, b.withTrimmedLeft (28.0f).withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

private:
    KickSendProcessor& proc;
    const gk::Palette& palette;
    int state = -1, lastKicks = -1;
    float flash = 0.0f;
    bool wasDark = true;
};

gk::Palette KickSendEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff35382f);
    p.backgroundEdge = juce::Colour (0xff1a1c17);
    p.surface = juce::Colour (0xff454b3c);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = lime;
    p.beret = lime;
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff17171a);
    p.plaque = cream;
    p.arcStops = { orange, lime };
    p.knobBands = { lime, limeDeep, lime, limeDeep };
    p.pulse = orange;
    p.arrowLeft = lime;
    p.arrowRight = limeDeep;
    p.lowLabel = cream;
    p.highLabel = lime;
    return p;
}

KickSendEditor::KickSendEditor (KickSendProcessor& p)
    : OneKnobEditor (p, makePalette(), KLS_PRODUCT_NAME,
                     [] (gk::AudioFeatures& f, const gk::Palette& palette)
                     {
                         auto m = std::make_unique<gk::MascotComponent> (f, palette);
                         m->setFrames (phazeFrames());
                         gk::MascotComponent::Poses poses;
                         poses.groove = { 13, 14, 15, 16 }; // a foot tap on every beat
                         poses.calm = 4;
                         poses.highLadder = { 5, 7, 13 };
                         poses.drop = { 0, 2 };
                         poses.sleep = 8;
                         m->setPoses (poses);
                         return m;
                     })
{
    setBackgroundCached (true);
    status = std::make_unique<LinkStatus> (p, getPalette());
    addToCanvas (*status, { 90, 474, 240, 32 });
}

KickSendEditor::~KickSendEditor() = default;

void KickSendEditor::editorTick (double dt)
{
    if (status != nullptr)
        status->update (dt);
}
} // namespace kls
