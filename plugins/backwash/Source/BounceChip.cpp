#include "BounceChip.h"

namespace bw
{
/** The render, off the message thread. The renderer is created and destroyed
    on the message thread (by the chip). */
class BounceChip::Job final : public juce::Thread
{
public:
    Job (BounceChip& c, const juce::File& f, bounce::Request r)
        : juce::Thread ("Backwash bounce"), chip (&c), renderer (*c.renderer), input (f), request (std::move (r)), folder (c.folder),
          progress (c.progress)
    {
    }

    void run() override
    {
        auto rendered = bounce::render (renderer, request, input, folder,
                                      [this] (float p)
                                      {
                                          progress.store (p);
                                          return ! threadShouldExit();
                                      });
        if (threadShouldExit())
            return;
        juce::MessageManager::callAsync ([safe = chip, rendered]() mutable
                                         {
                                             if (auto* c = safe.getComponent())
                                                 c->finished (std::move (rendered));
                                         });
    }

private:
    juce::Component::SafePointer<BounceChip> chip;
    BackwashProcessor& renderer;
    juce::File input;
    bounce::Request request;
    juce::File folder;
    std::atomic<float>& progress;
};

namespace
{
    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
} // namespace

BounceChip::BounceChip (BackwashProcessor& p, const gk::Palette& pal) : live (p), palette (pal)
{
    setTitle ("Bounce");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updateTooltip();
}

BounceChip::~BounceChip()
{
    if (job != nullptr)
        job->stopThread (10000); // the render checks between blocks of 512 samples
    job.reset();
    renderer.reset();
}

void BounceChip::start (const juce::File& file)
{
    if (job != nullptr || ! file.existsAsFile())
        return;
    result = {};
    progress.store (0.0f);
    const auto request = bounce::requestFrom (live);
    renderer = bounce::makeRenderer (request);
    job = std::make_unique<Job> (*this, file, request);
    job->startThread (juce::Thread::Priority::low);
    repainter.startTimerHz (20);
    updateTooltip();
    repaint();
}

void BounceChip::finished (bounce::Result r)
{
    if (job != nullptr)
        job->stopThread (10000);
    job.reset();
    renderer.reset();
    repainter.stopTimer();
    result = std::move (r);
    updateTooltip();
    repaint();
    if (onFinished)
        onFinished();
}

void BounceChip::setDropHighlight (bool on)
{
    highlight = on;
    repaint();
}

void BounceChip::updateTooltip()
{
    if (job != nullptr)
        setTooltip (fr ("Backwash rend ton son avec ses souffles\xe2\x80\xa6"));
    else if (result.file.existsAsFile())
    {
        const int bars = result.preRollBars;
        setTooltip (fr ("Glisse ce WAV dans ton set, ") + juce::String (bars) + (bars > 1 ? fr (" mesures") : fr (" mesure"))
                    + fr (" avant l'original : les souffles tombent pile sur ses coups. Clic : le montrer dans le Finder. (")
                    + result.file.getFileName() + ")");
    }
    else if (result.error.isNotEmpty())
        setTooltip (result.error + fr (" \xe2\x80\x94 d\xc3\xa9pose un autre son ou clique pour en choisir un"));
    else
        setTooltip (fr ("WAV : d\xc3\xa9pose un son sur Backwash (ou clique ici). Il revient avec ses souffles, sans latence ni "
                        "m\xc3\xa9moire LIVE, cal\xc3\xa9 sur la grille : glisse-le ensuite dans ton set. Le son doit commencer sur un "
                        "premier temps."));
}

void BounceChip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto aqua = palette.accent, coral = palette.highLabel;
    const bool ready = job == nullptr && result.file.existsAsFile();
    const bool failed = job == nullptr && ! ready && result.error.isNotEmpty();

    g.setFont (gk::GrooveFonts::font (9.5f).withExtraKerningFactor (0.12f));
    g.setColour (palette (aqua).withAlpha (0.85f));
    g.drawText ("WAV", b.removeFromTop (captionHeight), juce::Justification::centred);

    const auto r = b.withSizeKeepingCentre (b.getWidth(), chipHeight).reduced (1.5f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.5f), rad);

    juce::String text = "DROP";
    juce::Colour fill = palette (palette.outline).withAlpha (0.9f), ink = palette (palette.text), edge = palette (aqua).withAlpha (over ? 1.0f : 0.8f);
    if (job != nullptr)
        text = juce::String (juce::roundToInt (progress.load() * 100.0f)) + " %";
    else if (ready)
    {
        text = "DRAG";
        fill = palette (coral);
        ink = palette (palette.outline);
        edge = palette (palette.outline);
    }
    else if (failed)
    {
        text = "!";
        edge = palette (coral);
    }
    if (highlight)
    {
        fill = palette (aqua);
        ink = palette (palette.outline);
        text = "DROP";
    }
    g.setColour (fill);
    g.fillRoundedRectangle (r, rad);
    if (job != nullptr)
    {
        // the progress, as a fill
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, progress.load())).toNearestInt());
        g.setColour (palette (aqua).withAlpha (0.45f));
        g.fillRoundedRectangle (r, rad);
        g.restoreState();
    }
    g.setColour (edge);
    if (! ready && ! highlight && job == nullptr)
    {
        // dashed outline: a place to drop something
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (0.75f), rad);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
    }
    else
        g.drawRoundedRectangle (r.reduced (0.75f), rad, 1.5f);

    g.setFont (gk::GrooveFonts::font (11.5f).withExtraKerningFactor (0.05f));
    g.setColour (ink);
    if (ready && ! highlight)
    {
        // "DRAG" + a small arrow out
        auto t = r.withTrimmedRight (12.0f);
        g.drawText (text, t, juce::Justification::centred);
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
        g.drawText (text, r, juce::Justification::centred);
}

void BounceChip::mouseDown (const juce::MouseEvent&) { dragging = false; }

void BounceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || job != nullptr || ! result.file.existsAsFile() || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ result.file.getFullPathName() }, false, this,
                                                                 [safe = juce::Component::SafePointer<BounceChip> (this)]
                                                                 {
                                                                     if (safe != nullptr)
                                                                         safe->dragging = false;
                                                                 });
}

void BounceChip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || ! contains (e.getPosition()) || job != nullptr)
        return;
    if (result.file.existsAsFile())
        result.file.revealToUser();
    else
        choose();
}

void BounceChip::choose()
{
    chooser = std::make_unique<juce::FileChooser> (fr ("Un son \xc3\xa0 passer dans Backwash"), juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<BounceChip> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->start (fc.getResult());
                          });
}

void DropOverlay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (palette (palette.backgroundEdge).withAlpha (0.72f));
    g.fillRoundedRectangle (b, 14.0f);
    juce::Path outline, dashed;
    outline.addRoundedRectangle (b.reduced (2.0f), 12.0f);
    const float dashes[] = { 10.0f, 7.0f };
    juce::PathStrokeType (3.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (palette (palette.accent));
    g.fillPath (dashed);

    auto t = b.withSizeKeepingCentre (b.getWidth() - 40.0f, 130.0f);
    g.setFont (gk::GrooveFonts::font (30.0f));
    g.setColour (palette (palette.text));
    g.drawText (juce::String::fromUTF8 ("L\xc3\xa2""che ton son"), t.removeFromTop (44.0f), juce::Justification::centred);
    g.setFont (gk::GrooveFonts::font (15.0f));
    g.setColour (palette (palette.accent));
    g.drawFittedText (juce::String::fromUTF8 ("Backwash le rend avec ses souffles, sans latence.\nPuis glisse le WAV dans ton set."),
                      t.toNearestInt(), juce::Justification::centredTop, 3);
}
} // namespace bw
