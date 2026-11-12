#include "IdeasView.h"
#include "WaveformView.h"

namespace daftchunk
{
using namespace colours;

namespace
{
    juce::Colour stepColour (const Kit* kit, const Step& s)
    {
        if (s.cut)
            return black;
        if (kit == nullptr || s.slice < 0 || s.slice >= (int) kit->slices.size())
            return surface;
        return forType (kit->slices[(size_t) s.slice].type);
    }

    bool darkInk (const Kit* kit, const Step& s)
    {
        if (kit == nullptr || s.slice < 0 || s.slice >= (int) kit->slices.size())
            return false;
        const auto t = kit->slices[(size_t) s.slice].type;
        return t == SliceType::hat || t == SliceType::voice;
    }

    void drawPlay (juce::Graphics& g, juce::Rectangle<float> r, bool playing)
    {
        juce::Path p;
        const auto c = r.reduced (r.getHeight() * 0.25f);
        if (playing)
            p.addRectangle (c);
        else
            p.addTriangle (c.getX(), c.getY(), c.getRight(), c.getCentreY(), c.getX(), c.getBottom());
        g.fillPath (p);
    }

    void drawStar (juce::Graphics& g, juce::Rectangle<float> r, bool filled)
    {
        juce::Path p;
        p.addStar (r.getCentre(), 5, r.getHeight() * 0.18f, r.getHeight() * 0.42f, 0.0f);
        if (filled)
            g.fillPath (p);
        else
            g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    void drawCross (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.reduced (r.getHeight() * 0.3f);
        g.drawLine ({ c.getTopLeft(), c.getBottomRight() }, 1.8f);
        g.drawLine ({ c.getBottomLeft(), c.getTopRight() }, 1.8f);
    }
} // namespace

//==============================================================================
IdeaStrip::IdeaStrip (DaftChunkProcessor& p) : proc (p)
{
    setTooltip (juce::String::fromUTF8 ("Les id\xc3\xa9""es : chaque D\xc3\x89 en remplit une. Lecture (cal\xc3\xa9""e sur Ableton), \xc3\xa9toile pour la garder, "
                                        "croix pour la jeter. Clic sur une id\xc3\xa9""e : la page S\xc3\x89QUENCE la montre."));
}

void IdeaStrip::refresh()
{
    const int step = proc.getDsp().getTelemetry().step.load (std::memory_order_relaxed);
    if (proc.getIdeasVersion() != shownVersion || step != shownStep || proc.getKitVersion() != shownKit)
    {
        shownVersion = proc.getIdeasVersion();
        shownStep = step;
        shownKit = proc.getKitVersion();
        kit = proc.getKit();
        repaint();
    }
}

juce::Rectangle<float> IdeaStrip::slot (int i) const
{
    const auto b = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    const float w = (b.getWidth() - 7 * 6.0f) / 8.0f;
    return { b.getX() + (float) i * (w + 6.0f), b.getY(), w, b.getHeight() };
}

juce::Rectangle<float> IdeaStrip::playButton (int i) const { const auto s = slot (i); return { s.getX() + 4.0f, s.getBottom() - 22.0f, 26.0f, 18.0f }; }
juce::Rectangle<float> IdeaStrip::starButton (int i) const { const auto s = slot (i); return { s.getRight() - 50.0f, s.getBottom() - 22.0f, 22.0f, 18.0f }; }
juce::Rectangle<float> IdeaStrip::crossButton (int i) const { const auto s = slot (i); return { s.getRight() - 26.0f, s.getBottom() - 22.0f, 22.0f, 18.0f }; }

void IdeaStrip::paint (juce::Graphics& g)
{
    for (int i = 0; i < DaftChunkProcessor::numIdeas; ++i)
    {
        const auto r = slot (i);
        const auto& idea = proc.getIdea (i);
        const bool playing = proc.getPlayingIdea() == i, selected = proc.getSelectedIdea() == i;
        g.setColour (idea.used ? surface : surface.withAlpha (0.45f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (playing ? coral : selected ? cream : black);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, playing || selected ? 2.0f : 1.5f);
        g.setFont (gk::GrooveFonts::font (12.0f));
        g.setColour (cream.withAlpha (idea.used ? 0.95f : 0.3f));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + 3.0f, 20.0f, 14.0f), juce::Justification::centredLeft);
        if (! idea.used)
        {
            g.setFont (gk::GrooveFonts::font (10.0f));
            g.drawText ("VIDE", r, juce::Justification::centred);
            continue;
        }
        const auto& pt = idea.pattern;
        g.setFont (gk::GrooveFonts::font (9.5f));
        g.setColour (cream.withAlpha (0.6f));
        g.drawText (juce::String (pt.bars) + (pt.bars > 1 ? " MES." : " MESURE"), juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 3.0f, r.getWidth() - 26.0f, 14.0f),
                    juce::Justification::centredRight);
        // the steps in miniature
        const auto map = juce::Rectangle<float> (r.getX() + 5.0f, r.getY() + 20.0f, r.getWidth() - 10.0f, 16.0f);
        const int n = pt.numSteps();
        const float w = map.getWidth() / (float) std::max (1, n);
        for (int k = 0; k < n; ++k)
        {
            const auto& s = pt.steps[(size_t) k];
            const auto cell = juce::Rectangle<float> (map.getX() + w * (float) k, map.getY(), std::max (1.0f, w - (n <= 16 ? 1.0f : 0.0f)), map.getHeight());
            if (s.slice >= 0 || s.cut)
            {
                g.setColour (stepColour (kit.get(), s));
                g.fillRect (cell.withTrimmedTop (s.repeat > 1 ? 0.0f : 4.0f));
            }
            if (playing && k == shownStep)
            {
                g.setColour (cream);
                g.fillRect (cell.withHeight (2.0f).translated (0.0f, map.getHeight() + 1.0f));
            }
        }
        g.setColour (playing ? coral : cream.withAlpha (0.85f));
        drawPlay (g, playButton (i), playing);
        g.setColour (idea.starred ? coral : cream.withAlpha (0.6f));
        drawStar (g, starButton (i), idea.starred);
        g.setColour (cream.withAlpha (0.6f));
        drawCross (g, crossButton (i));
    }
}

void IdeaStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < DaftChunkProcessor::numIdeas; ++i)
    {
        if (! slot (i).contains (e.position))
            continue;
        if (! proc.getIdea (i).used)
            return;
        if (playButton (i).expanded (3.0f).contains (e.position))
            proc.togglePlay (i);
        else if (starButton (i).expanded (2.0f).contains (e.position))
            proc.toggleStar (i);
        else if (crossButton (i).expanded (2.0f).contains (e.position))
            proc.deleteIdea (i);
        else
        {
            proc.selectIdea (i);
            if (onSelect)
                onSelect();
        }
        return;
    }
}

//==============================================================================
StepGrid::StepGrid (DaftChunkProcessor& p) : proc (p)
{
    setTooltip (juce::String::fromUTF8 ("Les pas de l'id\xc3\xa9""e choisie. Clic : la tranche s\xc3\xa9lectionn\xc3\xa9""e sur ce pas (encore : le vider). "
                                        "Molette : une autre tranche (maj : la hauteur). Clic droit : inverser, r\xc3\xa9p\xc3\xa9ter, hauteur, couper."));
}

void StepGrid::refresh()
{
    const int step = proc.getDsp().getTelemetry().step.load (std::memory_order_relaxed);
    const int idea = proc.getSelectedIdea();
    if (proc.getIdeasVersion() != shownVersion || idea != shownIdea || proc.getKitVersion() != shownKit
        || (idea >= 0 && idea == proc.getPlayingIdea() && step != shownStep))
    {
        if (idea != shownIdea)
            page = 0;
        shownVersion = proc.getIdeasVersion();
        shownIdea = idea;
        shownStep = step;
        shownKit = proc.getKitVersion();
        kit = proc.getKit();
        if (idea >= 0)
            page = juce::jlimit (0, proc.getIdea (idea).pattern.bars - 1, page);
        repaint();
    }
}

juce::Rectangle<float> StepGrid::barButton (int b) const
{
    return { (float) getWidth() - 4.0f * 30.0f + (float) b * 30.0f, 0.0f, 27.0f, 18.0f };
}

juce::Rectangle<float> StepGrid::cell (int k) const
{
    const float top = 24.0f, w = (float) getWidth() / 8.0f, h = ((float) getHeight() - top - 4.0f) / 2.0f;
    return { (float) (k % 8) * w, top + (float) (k / 8) * (h + 4.0f), w, h };
}

int StepGrid::stepAt (juce::Point<float> p) const
{
    const int idea = proc.getSelectedIdea();
    if (idea < 0)
        return -1;
    for (int k = 0; k < 16; ++k)
        if (cell (k).contains (p))
        {
            const int s = page * 16 + k;
            return s < proc.getIdea (idea).pattern.numSteps() ? s : -1;
        }
    return -1;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int idea = proc.getSelectedIdea();
    g.setFont (gk::GrooveFonts::font (11.0f));
    if (idea < 0)
    {
        g.setColour (cream.withAlpha (0.5f));
        g.drawFittedText (juce::String::fromUTF8 ("Lance le D\xc3\x89, puis choisis une id\xc3\xa9""e en bas : ses pas s'affichent ici."), getLocalBounds().reduced (10),
                          juce::Justification::centred, 3);
        return;
    }
    const auto& pt = proc.getIdea (idea).pattern;
    g.setColour (cream.withAlpha (0.8f));
    g.drawText (juce::String::fromUTF8 ("ID\xc3\x89""E ") + juce::String (idea + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + styleName (pt.style),
                juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth() - 125.0f, 18.0f), juce::Justification::centredLeft);
    for (int b = 0; b < 4; ++b)
    {
        const auto r = barButton (b);
        const bool exists = b < pt.bars;
        g.setColour (! exists ? surface.withAlpha (0.4f) : b == page ? coral : surface);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (! exists ? cream.withAlpha (0.2f) : b == page ? black : cream);
        g.drawText (juce::String (b + 1), r, juce::Justification::centred);
    }
    const bool playing = proc.getPlayingIdea() == idea;
    for (int k = 0; k < 16; ++k)
    {
        const int s = page * 16 + k;
        if (s >= pt.numSteps())
            break;
        const auto& st = pt.steps[(size_t) s];
        const auto r = cell (k).reduced (2.0f);
        g.setColour (st.slice >= 0 || st.cut ? stepColour (kit.get(), st) : (k % 4 == 0 ? surface.brighter (0.15f) : surface));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (playing && s == shownStep ? cream : black);
        g.drawRoundedRectangle (r, 5.0f, playing && s == shownStep ? 2.5f : 1.2f);
        const auto ink = darkInk (kit.get(), st) ? black : cream;
        g.setColour (ink);
        if (st.cut)
        {
            g.setFont (gk::GrooveFonts::font (10.0f));
            g.drawText ("CUT", r, juce::Justification::centred);
            continue;
        }
        if (st.slice < 0)
            continue;
        g.setFont (gk::GrooveFonts::font (14.0f));
        g.drawText (juce::String (st.slice + 1), r.withTrimmedBottom (r.getHeight() * 0.4f), juce::Justification::centred);
        juce::String marks;
        if (st.reverse)
            marks << "R ";
        if (st.repeat > 1)
            marks << "x" << st.repeat << " ";
        if (st.pitch != 0)
            marks << (st.pitch > 0 ? "+" : "") << st.pitch;
        g.setFont (gk::GrooveFonts::font (9.5f));
        g.drawText (marks.trim(), r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred);
    }
}

void StepGrid::edit (int k, const std::function<void (Step&)>& change)
{
    const int idea = proc.getSelectedIdea();
    if (idea < 0 || k < 0)
        return;
    auto s = proc.getIdea (idea).pattern.steps[(size_t) k];
    change (s);
    proc.setStep (idea, k, s);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int idea = proc.getSelectedIdea();
    if (idea < 0)
        return;
    for (int b = 0; b < proc.getIdea (idea).pattern.bars; ++b)
        if (barButton (b).contains (e.position))
        {
            page = b;
            repaint();
            return;
        }
    const int k = stepAt (e.position);
    if (k < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        const auto st = proc.getIdea (idea).pattern.steps[(size_t) k];
        juce::PopupMenu m;
        m.addItem (1, "Inverser", st.slice >= 0, st.reverse);
        m.addItem (2, juce::String::fromUTF8 ("R\xc3\xa9p\xc3\xa9ter x2 (1/32)"), st.slice >= 0, st.repeat == 2);
        m.addItem (3, juce::String::fromUTF8 ("R\xc3\xa9p\xc3\xa9ter x4 (1/64)"), st.slice >= 0, st.repeat == 4);
        m.addItem (4, "Hauteur +1", st.slice >= 0);
        m.addItem (5, "Hauteur -1", st.slice >= 0);
        m.addItem (6, "Hauteur +12", st.slice >= 0);
        m.addItem (7, "Hauteur -12", st.slice >= 0);
        m.addItem (8, juce::String::fromUTF8 ("Hauteur \xc3\xa0 0"), st.slice >= 0 && st.pitch != 0);
        m.addSeparator();
        m.addItem (9, "Couper (silence)", true, st.cut);
        m.addItem (10, "Vider", st.slice >= 0 || st.cut);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                         [this, k] (int r)
                         {
                             edit (k, [r] (Step& s)
                             {
                                 switch (r)
                                 {
                                     case 1: s.reverse = ! s.reverse; break;
                                     case 2: s.repeat = s.repeat == 2 ? 1 : 2; break;
                                     case 3: s.repeat = s.repeat == 4 ? 1 : 4; break;
                                     case 4: s.pitch = std::min (24, s.pitch + 1); break;
                                     case 5: s.pitch = std::max (-24, s.pitch - 1); break;
                                     case 6: s.pitch = std::min (24, s.pitch + 12); break;
                                     case 7: s.pitch = std::max (-24, s.pitch - 12); break;
                                     case 8: s.pitch = 0; break;
                                     case 9: s = {}; s.cut = true; break;
                                     case 10: s = {}; break;
                                     default: break;
                                 }
                             });
                         });
        return;
    }
    const int sel = proc.getSelectedSlice();
    edit (k, [sel] (Step& s)
    {
        if (s.slice == sel && ! s.cut)
            s = {}; // again: empty
        else
            s = { sel, 0, 1, false, false, 1.0f };
    });
    proc.getDsp().preview (sel);
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int k = stepAt (e.position);
    if (k < 0 || kit == nullptr || kit->slices.empty())
        return;
    wheelAccum += w.deltaY;
    if (std::abs (wheelAccum) < 0.12f)
        return;
    const int dir = wheelAccum > 0.0f ? 1 : -1;
    wheelAccum = 0.0f;
    const int n = (int) kit->slices.size();
    const bool pitch = e.mods.isShiftDown();
    edit (k, [dir, n, pitch] (Step& s)
    {
        if (pitch)
        {
            if (s.slice >= 0)
                s.pitch = juce::jlimit (-24, 24, s.pitch + dir);
            return;
        }
        s.cut = false;
        s.slice = s.slice < 0 ? 0 : ((s.slice + dir) % n + n) % n;
    });
}
} // namespace daftchunk
