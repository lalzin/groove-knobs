#include "WaveformView.h"

namespace daftchunk
{
namespace colours
{
    juce::Colour forType (SliceType t)
    {
        switch (t)
        {
            case SliceType::kick: return juce::Colour (0xffa3241a);  // brick red
            case SliceType::snare: return coral;
            case SliceType::hat: return cream;
            case SliceType::voice: return juce::Colour (0xffffa88a); // peach
            case SliceType::tonal: return juce::Colour (0xffd25a28); // burnt tomato
            case SliceType::noise: break;
        }
        return juce::Colour (0xff7a706a); // warm grey
    }

    const char* letterFor (SliceType t)
    {
        switch (t)
        {
            case SliceType::kick: return "K";
            case SliceType::snare: return "S";
            case SliceType::hat: return "H";
            case SliceType::voice: return "V";
            case SliceType::tonal: return "T";
            case SliceType::noise: break;
        }
        return "B";
    }
} // namespace colours

//==============================================================================
WaveformView::WaveformView (DaftChunkProcessor& p) : proc (p)
{
    setTooltip (juce::String::fromUTF8 ("Les tranches du sample. Molette (ou pincement) : zoom ; maj + molette : avancer ; la barre du bas : o\xc3\xb9 tu es (double-clic : tout). "
                                        "Glisse une coupe pour la d\xc3\xa9placer, glisse le drapeau 1 pour placer le premier temps. ")
                + juce::String::fromUTF8 ("Double-clic : une coupe en plus. Clic droit (ou alt-clic) sur une coupe : l'enlever. Clic sur une tranche : l'\xc3\xa9""couter."));
}

void WaveformView::setDropHighlight (bool on)
{
    if (on != dropHighlight)
    {
        dropHighlight = on;
        repaint();
    }
}

void WaveformView::refresh()
{
    const auto& tel = proc.getDsp().getTelemetry();
    const int s = tel.slice.load (std::memory_order_relaxed);
    const float pos = tel.position.load (std::memory_order_relaxed);
    const bool busy = proc.isAnalysing();
    const float prog = proc.getAnalysisProgress();
    bool dirty = false;
    if (proc.getKitVersion() != shownVersion || proc.getSelectedSlice() != shownSelected)
    {
        if (kit == nullptr || proc.getKit() == nullptr || proc.getKit()->sample != kit->sample)
        {
            viewStart = 0.0; // another sample: all of it
            viewLength = -1.0;
        }
        kit = proc.getKit();
        shownVersion = proc.getKitVersion();
        shownSelected = proc.getSelectedSlice();
        dirty = true;
    }
    if (s != shownSlice || (s >= 0 && std::abs (pos - shownPosition) > 0.002f) || busy != shownBusy
        || (busy && std::abs (prog - shownProgress) > 0.005f) || proc.getStatus() != shownStatus)
        dirty = true;
    shownSlice = s;
    shownPosition = pos;
    shownBusy = busy;
    shownProgress = prog;
    shownStatus = proc.getStatus();
    if (dirty)
        repaint();
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f).withTrimmedTop (16.0f).withTrimmedBottom (14.0f);
}

juce::Rectangle<float> WaveformView::overviewArea() const
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    return { b.getX(), b.getBottom() - 11.0f, b.getWidth() - 3.0f * 34.0f - 4.0f, 10.0f };
}

juce::Rectangle<float> WaveformView::zoomButton (int i) const
{
    const auto o = overviewArea();
    return { o.getRight() + 4.0f + (float) i * 34.0f, o.getY() - 1.0f, 32.0f, 12.0f };
}

double WaveformView::total() const noexcept
{
    return kit != nullptr && kit->sample != nullptr ? std::max (1.0, (double) kit->sample->analysis.numSamples) : 1.0;
}

void WaveformView::clampView()
{
    const double n = total();
    if (viewLength > 0.0)
    {
        viewLength = juce::jlimit (std::min (n, 200.0), n, viewLength); // down to ~200 samples across
        if (viewLength >= n * 0.999)
            viewLength = -1.0;
    }
    viewStart = viewLength > 0.0 ? juce::jlimit (0.0, n - viewLength, viewStart) : 0.0;
}

void WaveformView::zoomAround (float x, double factor)
{
    const double anchor = sampleFor (x), frac = (x - waveArea().getX()) / std::max (1.0f, waveArea().getWidth());
    viewLength = visible() / std::max (1.0e-3, factor);
    clampView();
    viewStart = anchor - juce::jlimit (0.0, 1.0, frac) * visible();
    clampView();
    repaint();
}

void WaveformView::resetZoom()
{
    viewLength = -1.0;
    viewStart = 0.0;
    repaint();
}

float WaveformView::xFor (double sample) const
{
    const auto r = waveArea();
    return r.getX() + (float) ((sample - viewStart) / visible()) * r.getWidth();
}

double WaveformView::sampleFor (float x) const
{
    const auto r = waveArea();
    return juce::jlimit (0.0, total(), viewStart + (double) ((x - r.getX()) / r.getWidth()) * visible());
}

int WaveformView::cutNear (float x) const
{
    if (kit == nullptr)
        return -1;
    int best = -1;
    float bestD = 5.0f;
    for (int i = 1; i < (int) kit->slices.size(); ++i)
        if (const float d = std::abs (xFor ((double) kit->slices[(size_t) i].start) - x); d <= bestD)
        {
            bestD = d;
            best = i;
        }
    return best;
}

bool WaveformView::onBeatFlag (juce::Point<float> p) const
{
    if (kit == nullptr || kit->sample == nullptr)
        return false;
    return p.y < 18.0f && std::abs (p.x - (xFor (proc.getFirstBeat()) + 6.0f)) < 10.0f;
}

int WaveformView::sliceAt (float x) const
{
    if (kit == nullptr)
        return -1;
    const double s = sampleFor (x);
    for (int i = 0; i < (int) kit->slices.size(); ++i)
        if (s >= (double) kit->slices[(size_t) i].start && s < (double) kit->slices[(size_t) i].end)
            return i;
    return -1;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRoundedRectangle (bounds, 8.0f);
    const auto r = waveArea();

    const auto* s = kit != nullptr ? kit->sample.get() : nullptr;
    if (s == nullptr || proc.isAnalysing())
    {
        g.setFont (gk::GrooveFonts::font (17.0f));
        g.setColour (colours::cream.withAlpha (0.7f));
        if (proc.isAnalysing())
        {
            const float p = proc.getAnalysisProgress();
            g.drawText (juce::String::fromUTF8 ("ANALYSE\xe2\x80\xa6 ") + juce::String (juce::roundToInt (p * 100.0f)) + " %", r.withTrimmedBottom (24.0f), juce::Justification::centred);
            const auto bar = juce::Rectangle<float> (r.getCentreX() - 140.0f, r.getCentreY() + 14.0f, 280.0f, 8.0f);
            g.setColour (colours::surface);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (colours::coral);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * p), 4.0f);
        }
        else
        {
            g.drawText (proc.getStatus().isNotEmpty() ? proc.getStatus() : juce::String ("Glisse un sample ici (WAV, AIFF, FLAC, MP3), ou CHARGER"),
                        r, juce::Justification::centred);
        }
        if (dropHighlight)
        {
            g.setColour (colours::coral);
            g.drawRoundedRectangle (bounds.reduced (1.5f), 8.0f, 3.0f);
        }
        return;
    }

    const auto& a = s->analysis;
    // the overview under it: where the view is in the whole sample, and the zoom buttons
    {
        const auto o = overviewArea();
        g.setColour (colours::surface);
        g.fillRoundedRectangle (o, 3.0f);
        const float x0 = o.getX() + (float) (viewStart / total()) * o.getWidth();
        const float w0 = std::max (6.0f, (float) (visible() / total()) * o.getWidth());
        g.setColour (isZoomed() ? colours::coral : colours::cream.withAlpha (0.25f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x0, o.getY(), w0, o.getHeight()), 3.0f);
        const char* labels[] { "-", "+", "TOUT" };
        for (int i = 0; i < 3; ++i)
        {
            const auto zb = zoomButton (i);
            const bool on = i == 1 || isZoomed();
            g.setColour (on ? colours::surface.brighter (0.2f) : colours::surface);
            g.fillRoundedRectangle (zb, 3.0f);
            g.setColour (colours::cream.withAlpha (on ? 0.9f : 0.3f));
            g.setFont (gk::GrooveFonts::font (i == 2 ? 9.0f : 12.0f));
            g.drawText (i == 2 && isZoomed() ? "x" + juce::String (juce::roundToInt (total() / visible())) : juce::String (labels[i]), zb, juce::Justification::centred);
        }
    }
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (juce::Rectangle<float> (r.getX(), bounds.getY(), r.getWidth(), r.getBottom() - bounds.getY()).getSmallestIntegerContainer());
    // the slices: a tint and a coloured tab with the number and the type
    for (int i = 0; i < (int) kit->slices.size(); ++i)
    {
        const auto& sl = kit->slices[(size_t) i];
        const float x0 = xFor ((double) sl.start), x1 = xFor ((double) sl.end);
        const auto c = colours::forType (sl.type);
        const bool playing = i == shownSlice;
        g.setColour (c.withAlpha (playing ? 0.30f : (i % 2 == 0 ? 0.13f : 0.08f)));
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        const auto tab = juce::Rectangle<float> (x0, bounds.getY() + 2.0f, x1 - x0, 14.0f);
        g.setColour (c.withAlpha (playing ? 1.0f : 0.85f));
        g.fillRect (tab.reduced (0.5f, 0.0f));
        if (tab.getWidth() > 16.0f)
        {
            g.setColour (sl.type == SliceType::hat || sl.type == SliceType::voice ? colours::black : colours::cream);
            g.setFont (gk::GrooveFonts::font (10.5f));
            const juce::String label = tab.getWidth() > 34.0f ? juce::String (i + 1) + " " + colours::letterFor (sl.type) : juce::String (colours::letterFor (sl.type));
            g.drawText (label, tab.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }

    // the selected slice (the TRANCHE page): outlined, its fine start / end and reverse shown
    if (const int sel = proc.getSelectedSlice(); sel >= 0 && sel < (int) kit->slices.size())
    {
        const auto& sl = kit->slices[(size_t) sel];
        const auto sp = proc.getSliceParams (sel);
        const double len = (double) (sl.end - sl.start);
        const float x0 = xFor ((double) sl.start), x1 = xFor ((double) sl.end);
        const float p0 = xFor ((double) sl.start + sp.start * len), p1 = xFor ((double) sl.end - sp.end * len);
        g.setColour (colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x0, r.getY(), p0 - x0, r.getHeight()));
        g.fillRect (juce::Rectangle<float> (p1, r.getY(), x1 - p1, r.getHeight()));
        g.setColour (colours::cream);
        g.drawRect (juce::Rectangle<float> (x0, bounds.getY() + 2.0f, x1 - x0, r.getBottom() - bounds.getY() - 2.0f), 1.5f);
        if (sp.reverse && x1 - x0 > 20.0f)
        {
            g.setFont (gk::GrooveFonts::font (10.0f));
            g.drawText ("<<", juce::Rectangle<float> (x0, r.getBottom() - 14.0f, x1 - x0, 12.0f), juce::Justification::centred);
        }
    }

    // the beat grid from beat 1 (bars brighter)
    {
        const double beat = a.beatSamples (proc.getBpm()), fb = proc.getFirstBeat();
        if (beat > 1.0 && visible() / beat < 2000.0)
        {
            const double first = fb + std::ceil ((viewStart - fb) / beat) * beat;
            for (double p = first; p < viewStart + visible(); p += beat)
            {
                const long k = std::lround ((p - fb) / beat);
                const bool bar = ((k % 4) + 4) % 4 == 0;
                g.setColour (colours::cream.withAlpha (bar ? 0.22f : 0.08f));
                g.fillRect (juce::Rectangle<float> (xFor (p) - 0.5f, r.getY(), 1.0f, r.getHeight()));
            }
        }
    }

    // the waveform: min / max per pixel column (the analysis's peaks far out, the samples themselves close in)
    {
        const int w = juce::roundToInt (r.getWidth());
        const float mid = r.getCentreY(), half = r.getHeight() * 0.46f;
        const auto& audio = s->audio;
        const int chans = audio.getNumChannels();
        const double perPixel = visible() / std::max (1, w), n = total();
        const int np = (int) a.peakMax.size();
        const bool fromPeaks = perPixel >= n / np;
        if (perPixel < 2.0)
        {
            // very close: the curve through the samples, and the samples as dots
            juce::Path line;
            const auto i0 = (int64_t) std::floor (viewStart), i1 = std::min ((int64_t) n - 1, (int64_t) std::ceil (viewStart + visible()) + 1);
            for (auto i = i0; i <= i1; ++i)
            {
                float v = 0.0f;
                for (int c = 0; c < chans; ++c)
                    v += audio.getSample (c, (int) i) / (float) chans;
                const juce::Point<float> pt (xFor ((double) i), mid - juce::jlimit (-1.0f, 1.0f, v) * half);
                if (i == i0)
                    line.startNewSubPath (pt);
                else
                    line.lineTo (pt);
                if (perPixel < 0.25)
                {
                    g.setColour (colours::cream);
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (pt));
                }
            }
            g.setColour (colours::cream.withAlpha (0.9f));
            g.strokePath (line, juce::PathStrokeType (1.5f));
            g.setColour (colours::cream.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (r.getX(), mid - 0.5f, r.getWidth(), 1.0f)); // the zero line
        }
        else
        {
            juce::Path p;
            for (int x = 0; x < w; ++x)
            {
                const double s0 = viewStart + x * perPixel, s1 = s0 + perPixel;
                float lo = 0.0f, hi = 0.0f;
                if (fromPeaks)
                {
                    const int b0 = (int) (s0 / n * np), b1 = std::max (b0 + 1, (int) (s1 / n * np));
                    for (int b = b0; b < std::min (b1, np); ++b)
                    {
                        lo = std::min (lo, a.peakMin[(size_t) b]);
                        hi = std::max (hi, a.peakMax[(size_t) b]);
                    }
                }
                else
                    for (auto i = (int64_t) s0; i < std::min ((int64_t) n, (int64_t) std::ceil (s1)); ++i)
                        for (int c = 0; c < chans; ++c)
                        {
                            lo = std::min (lo, audio.getSample (c, (int) i));
                            hi = std::max (hi, audio.getSample (c, (int) i));
                        }
                p.addRectangle (r.getX() + (float) x, mid - juce::jmin (1.0f, hi) * half, 1.0f, std::max (1.0f, (juce::jmin (1.0f, hi) - juce::jmax (-1.0f, lo)) * half));
            }
            g.setColour (colours::cream.withAlpha (0.88f));
            g.fillPath (p);
        }
    }

    // beyond the 64th slice: not sliced, dimmed
    if (! kit->slices.empty() && kit->slices.back().end < a.numSamples)
    {
        const float x = xFor ((double) kit->slices.back().end);
        g.setColour (colours::black.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x, r.getY(), r.getRight() - x, r.getHeight()));
        if (r.getRight() - x > 120.0f)
        {
            g.setColour (colours::cream.withAlpha (0.5f));
            g.setFont (gk::GrooveFonts::font (11.0f));
            g.drawText (juce::String::fromUTF8 ("64 tranches au plus"), juce::Rectangle<float> (x, r.getBottom() - 18.0f, r.getRight() - x, 14.0f), juce::Justification::centred);
        }
    }

    // the cuts
    for (int i = 1; i < (int) kit->slices.size(); ++i)
    {
        const float x = xFor ((double) kit->slices[(size_t) i].start);
        const bool hot = i == hoverCut || i == dragCut;
        g.setColour (hot ? colours::cream : colours::coral);
        g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.75f), r.getY(), hot ? 2.0f : 1.5f, r.getHeight()));
    }

    // the playhead
    if (shownSlice >= 0 && shownSlice < (int) kit->slices.size())
    {
        const auto& sl = kit->slices[(size_t) shownSlice];
        const float x = xFor ((double) sl.start + shownPosition * (double) (sl.end - sl.start));
        g.setColour (colours::cream);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, r.getY(), 2.0f, r.getHeight()));
    }

    // beat 1: a flag on top
    {
        const float x = xFor (proc.getFirstBeat());
        g.setColour (colours::coral);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY() + 2.0f, 2.0f, r.getBottom() - bounds.getY() - 2.0f));
        const auto flag = juce::Rectangle<float> (x, bounds.getY() + 2.0f, 14.0f, 14.0f);
        g.setColour (hoverFlag || dragFlag ? colours::cream : colours::coral);
        g.fillRoundedRectangle (flag, 3.0f);
        g.setColour (colours::black);
        g.setFont (gk::GrooveFonts::font (11.0f));
        g.drawText ("1", flag, juce::Justification::centred);
    }

    if (dropHighlight)
    {
        g.setColour (colours::coral);
        g.drawRoundedRectangle (bounds.reduced (1.5f), 8.0f, 3.0f);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const int c = cutNear (e.position.x);
    const bool f = onBeatFlag (e.position);
    if (c != hoverCut || f != hoverFlag)
    {
        hoverCut = f ? -1 : c;
        hoverFlag = f;
        setMouseCursor (f || c >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    hoverCut = -1;
    hoverFlag = false;
    repaint();
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (kit == nullptr || kit->sample == nullptr || proc.isAnalysing())
        return;
    // the zoom buttons and the overview
    for (int i = 0; i < 3; ++i)
        if (zoomButton (i).contains (e.position))
        {
            if (i == 2)
                resetZoom();
            else
                zoomAround (waveArea().getCentreX(), i == 1 ? 2.0 : 0.5);
            return;
        }
    if (overviewArea().expanded (0.0f, 3.0f).contains (e.position))
    {
        draggingOverview = true;
        mouseDrag (e);
        return;
    }
    if (onBeatFlag (e.position))
    {
        dragFlag = true;
        return;
    }
    const int c = cutNear (e.position.x);
    if (c >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
    {
        proc.removeCut (c);
        hoverCut = -1;
        return;
    }
    if (c >= 0)
    {
        dragCut = c;
        return;
    }
    if (const int s = sliceAt (e.position.x); s >= 0)
    {
        proc.selectSlice (s);
        proc.getDsp().preview (s);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingOverview)
    {
        // the view centred where the overview is dragged
        const auto o = overviewArea();
        viewStart = (double) ((e.position.x - o.getX()) / o.getWidth()) * total() - visible() * 0.5;
        clampView();
        repaint();
        return;
    }
    if (dragFlag)
        proc.setFirstBeat (sampleFor (e.position.x));
    else if (dragCut >= 0)
        proc.moveCut (dragCut, (int64_t) sampleFor (e.position.x));
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    draggingOverview = false;
    dragFlag = false;
    dragCut = -1;
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (overviewArea().expanded (0.0f, 3.0f).contains (e.position))
    {
        resetZoom();
        return;
    }
    if (! waveArea().expanded (0.0f, 16.0f).contains (e.position))
        return;
    if (kit == nullptr || kit->sample == nullptr || cutNear (e.position.x) >= 0 || onBeatFlag (e.position))
        return;
    proc.addCut ((int64_t) sampleFor (e.position.x));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (kit == nullptr || kit->sample == nullptr)
        return;
    const bool sideways = std::abs (w.deltaX) > std::abs (w.deltaY) || e.mods.isShiftDown();
    if (sideways)
    {
        // move along (a sideways swipe, or shift + wheel)
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        viewStart -= (double) d * visible() * 0.6;
        clampView();
        repaint();
        return;
    }
    zoomAround (e.position.x, std::pow (2.0, (double) w.deltaY * (w.isSmooth ? 2.0 : 1.0)));
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (kit != nullptr && kit->sample != nullptr)
        zoomAround (e.position.x, (double) scaleFactor);
}

//==============================================================================
SliceKeys::SliceKeys (DaftChunkProcessor& p) : proc (p) {}

juce::String SliceKeys::noteName (int midi)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 2);
}

void SliceKeys::refresh()
{
    const int s = proc.getDsp().getTelemetry().slice.load (std::memory_order_relaxed);
    if (proc.getKitVersion() != shownVersion)
    {
        kit = proc.getKit();
        shownVersion = proc.getKitVersion();
        const int n = kit != nullptr ? (int) kit->slices.size() : 0;
        first = juce::jlimit (0, std::max (0, n - visible), first);
        repaint();
    }
    if (proc.getSelectedSlice() != shownSelected)
    {
        shownSelected = proc.getSelectedSlice();
        repaint();
    }
    if (s != shownSlice)
    {
        shownSlice = s;
        if (s >= 0 && (s < first || s >= first + visible)) // follow what plays
            first = juce::jlimit (0, std::max (0, (int) kit->slices.size() - visible), s - visible / 2);
        repaint();
    }
}

juce::Rectangle<float> SliceKeys::arrow (bool right) const
{
    auto b = getLocalBounds().toFloat();
    return right ? b.removeFromRight (22.0f) : b.withWidth (22.0f);
}

juce::Rectangle<float> SliceKeys::keyArea (int k) const
{
    const auto b = getLocalBounds().toFloat().reduced (26.0f, 0.0f);
    const float w = b.getWidth() / visible;
    return { b.getX() + w * (float) k, b.getY(), w, b.getHeight() };
}

void SliceKeys::paint (juce::Graphics& g)
{
    const int n = kit != nullptr ? (int) kit->slices.size() : 0;
    for (int k = 0; k < visible; ++k)
    {
        const int i = first + k;
        const auto r = keyArea (k).reduced (2.0f, 1.0f);
        const bool has = i < n;
        const auto c = has ? colours::forType (kit->slices[(size_t) i].type) : colours::surface;
        const bool playing = i == shownSlice, selectedKey = has && i == proc.getSelectedSlice();
        g.setColour (has ? c.withAlpha (playing ? 1.0f : 0.78f) : c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (playing || selectedKey ? colours::cream : colours::black);
        g.drawRoundedRectangle (r, 6.0f, playing ? 2.5f : selectedKey ? 2.0f : 1.5f);
        const auto ink = has && (kit->slices[(size_t) i].type == SliceType::hat || kit->slices[(size_t) i].type == SliceType::voice) ? colours::black : colours::cream;
        g.setColour (has ? ink : colours::cream.withAlpha (0.3f));
        g.setFont (gk::GrooveFonts::font (14.0f));
        g.drawText (has ? juce::String (i + 1) : juce::String(), r.withTrimmedBottom (r.getHeight() * 0.45f), juce::Justification::centred);
        g.setFont (gk::GrooveFonts::font (10.0f));
        g.drawText (noteName (DaftChunkEngine::baseNote + i) + (has ? juce::String (" ") + colours::letterFor (kit->slices[(size_t) i].type) : juce::String()),
                    r.withTrimmedTop (r.getHeight() * 0.5f), juce::Justification::centred);
    }
    for (bool right : { false, true })
    {
        const bool can = right ? first + visible < n : first > 0;
        g.setColour (can ? colours::coral : colours::surface);
        const auto a = arrow (right).reduced (3.0f, 12.0f);
        juce::Path p;
        if (right)
            p.addTriangle (a.getX(), a.getY(), a.getRight(), a.getCentreY(), a.getX(), a.getBottom());
        else
            p.addTriangle (a.getRight(), a.getY(), a.getX(), a.getCentreY(), a.getRight(), a.getBottom());
        g.fillPath (p);
    }
}

void SliceKeys::scrollBy (int keys)
{
    const int n = kit != nullptr ? (int) kit->slices.size() : 0;
    first = juce::jlimit (0, std::max (0, n - visible), first + keys);
    repaint();
}

void SliceKeys::mouseDown (const juce::MouseEvent& e)
{
    if (arrow (false).contains (e.position))
        return scrollBy (-visible / 2);
    if (arrow (true).contains (e.position))
        return scrollBy (visible / 2);
    for (int k = 0; k < visible; ++k)
        if (keyArea (k).contains (e.position) && kit != nullptr && first + k < (int) kit->slices.size())
        {
            proc.selectSlice (first + k);
            proc.getDsp().preview (first + k);
        }
}

void SliceKeys::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
    if (std::abs (d) > 0.01f)
        scrollBy (d > 0.0f ? -1 : 1);
}
} // namespace daftchunk
