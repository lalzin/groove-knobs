#include "KickLockEditor.h"
#include "PhazeMascot.h"

namespace kl
{
namespace
{
    // Phaze's colours: lime drum, cream skin, navy sneakers; the kick in orange
    const juce::Colour limeDeep (0xff4e9a1c), lime (0xffa4dc3a), orange (0xffff8a3d), cream (0xfff3e9d2), navy (0xff17233d);
    const juce::Colour green (0xff3ddc84), amber (0xffffb020), red (0xffff4d4d), grey (0xff7a8499);

    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    /** French decimal comma: "3,2". */
    juce::String num (double v, int decimals)
    {
        return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals).replaceCharacter ('.', ',');
    }

    juce::Colour statusColour (int st)
    {
        switch ((Status) st)
        {
            case Status::ok:
            case Status::ducking: return green;
            case Status::shifted: return amber;
            case Status::unstable:
            case Status::monoLoss: return red;
            case Status::weakSub:
            case Status::noKick:
            default: return grey;
        }
    }

    /** One word in the pill... */
    juce::String statusWord (int st, bool lock)
    {
        switch ((Status) st)
        {
            case Status::ok: return lock ? "LOCKED" : "IN PHASE";
            case Status::shifted: return lock ? "LOCKING" : "OFF PHASE";
            case Status::unstable: return "UNSTABLE";
            case Status::weakSub: return "LOW SUB";
            case Status::monoLoss: return "MONO LOSS";
            case Status::ducking: return "DUCKING";
            case Status::noKick:
            default: return "NO KICK";
        }
    }

    juce::String sourceSuffix (int source) { return source == 2 ? fr (" \xc2\xb7 BEAT") : source == 3 ? fr (" \xc2\xb7 LINK") : juce::String(); }

    /** ... and the sentence in its tooltip. */
    juce::String statusSentence (int st, bool lock)
    {
        switch ((Status) st)
        {
            case Status::ok: return lock ? "The sub is locked on the kick (under 5 degrees)" : "The sub is already in phase with the kick";
            case Status::shifted: return lock ? "Realigning the sub on the kick" : "The sub is off the kick's phase: turn LOCK on";
            case Status::unstable: return "Unstable phase: the sub moves too much to be locked (glide, FM, noise)";
            case Status::weakSub: return "The sub is too quiet to measure its phase";
            case Status::monoLoss: return "Mono sum: cancellation detected (the sides are out of phase)";
            case Status::ducking: return "Ducking only, no phase analysis";
            case Status::noKick:
            default: return "Nothing to follow: press play (TRIGGER Auto or Beat follow the tempo); for the kick itself, put Kick Lock Send "
                            "on the kick's track (or route it to the sidechain)";
        }
    }
} // namespace

//==============================================================================
/** The column panels behind the controls. */
class Panels final : public juce::Component
{
public:
    explicit Panels (const gk::Palette& p) : palette (p) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        auto panel = [&] (juce::Rectangle<float> r)
        {
            g.setColour (palette (palette.surface).withAlpha (0.55f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (palette (palette.outline).withAlpha (0.8f));
            g.drawRoundedRectangle (r, 10.0f, 1.5f);
        };
        panel ({ 4.0f, 66.0f, 194.0f, 370.0f });   // Phaze
        panel ({ 584.0f, 66.0f, 172.0f, 370.0f }); // parameters
        panel ({ 204.0f, 272.0f, 374.0f, 164.0f }); // MIX
        g.setColour (palette (cream).withAlpha (0.45f));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.12f));
        g.drawText ("DUCKING", juce::Rectangle<float> (592.0f, 263.0f, 156.0f, 14.0f), juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
};

//==============================================================================
/** [● status] at the top right: the engine's diagnosis in plain words. */
class KickLockEditor::StatusPill final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StatusPill (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
    }

    void update (int preview)
    {
        const int st = preview >= 0 ? preview : proc.getDsp().getTelemetry().status.load (std::memory_order_relaxed);
        const bool lock = proc.getLockParameter().get();
        const bool bypassed = proc.getBypassParameterObject().get();
        const int key = (bypassed ? 100 : st) + 1000 * proc.getDsp().getTelemetry().triggerSource.load (std::memory_order_relaxed)
                        + 10000 * proc.getDsp().getTelemetry().analysisReason.load (std::memory_order_relaxed);
        if (key != shownKey || lock != shownLock)
        {
            shownKey = key;
            shown = bypassed ? 100 : st;
            shownLock = lock;
            const int source = proc.getDsp().getTelemetry().triggerSource.load (std::memory_order_relaxed);
            text = bypassed ? juce::String ("BYPASS") : statusWord (st, lock) + (st != (int) Status::noKick ? sourceSuffix (source) : juce::String());
            const int reason = proc.getDsp().getTelemetry().analysisReason.load (std::memory_order_relaxed);
            static const char* why[] { "", ": PHASE is off", ": no kick heard (the host's beats: put Kick Lock Send on the kick's track, or use the sidechain)",
                                       ": no bass on this track (nothing under SUB CUT) - the sidechain works on anything" };
            setTooltip (bypassed ? juce::String ("Bypassed")
                                 : st == (int) Status::ducking ? statusSentence (st, lock) + why[juce::jlimit (0, 3, reason)]
                                 : statusSentence (st, lock) + (st == (int) Status::noKick ? juce::String()
                                                               : source == 2 ? juce::String (" (on the host's beats: no kick on the sidechain)")
                                                               : source == 3 ? " (the kicks of " + proc.getDsp().linkName() + ", through Kick Lock Send)"
                                                                             : juce::String()));
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        const auto c = shown == 100 ? grey : statusColour (shown);
        g.setColour (palette (juce::Colour (0xff121318)).withAlpha (0.85f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (palette (c).withAlpha (0.7f));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.5f);
        const auto dot = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ b.getX() + 14.0f, b.getCentreY() });
        g.setColour (palette (c).withAlpha (0.35f));
        g.fillEllipse (dot.expanded (3.5f));
        g.setColour (palette (c));
        g.fillEllipse (dot);
        g.setColour (palette (cream));
        g.setFont (gk::GrooveFonts::font (13.0f).withExtraKerningFactor (0.06f));
        g.drawFittedText (text, b.withTrimmedLeft (26.0f).withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

private:
    KickLockProcessor& proc;
    const gk::Palette& palette;
    int shown = -1, shownKey = -1;
    bool shownLock = false;
    juce::String text;
};

//==============================================================================
/** The phase of the sub at the kick: a dial (magnified around 0, where the
    5° window is), the value, and the confidence of the measurement. */
class KickLockEditor::PhaseGauge final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PhaseGauge (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
        setTooltip ("LIVE: the phase of the sub against its aim (in phase with the kick where the sub comes back under it, plus TARGET); green within 5 degrees. "
                    "PRECISE with LOCK: the correction made at each kick (what is left is 0: the best rotation is chosen on the collision itself) (the rotation that makes the kick and the sub add up the most).");
    }

    void update (int preview, bool snap)
    {
        auto& tel = proc.getDsp().getTelemetry();
        // PRECISE, LOCK on: what is left is 0 by construction (the best
        // rotation is chosen on the collision itself) — the dial shows the
        // correction made instead, and how well the kick and the sub match
        const bool turnedView = proc.getPreciseParameter().get() && proc.getLockParameter().get();
        if (turnedView != turned)
        {
            turned = turnedView;
            shownError = 1.0e9f; // (repaint)
        }
        const float e = turned ? (float) KickLockDsp::wrap180 (tel.kickPhaseDeg.load (std::memory_order_relaxed)) : tel.phaseErrorDeg.load (std::memory_order_relaxed);
        const float c = tel.phaseConfidence.load (std::memory_order_relaxed);
        const int st = preview >= 0 ? preview : tel.status.load (std::memory_order_relaxed);
        const float a = snap ? 1.0f : 0.25f;
        error += (e - error) * a;
        confidence += (c - confidence) * a;
        if (st != status || std::abs (error - shownError) > 0.05f || std::abs (confidence - shownConfidence) > 0.005f)
        {
            status = st;
            shownError = error;
            shownConfidence = confidence;
            repaint();
        }
    }

    /** -1..1 on the dial: square root of |error| / 180 (0..5° takes 1/6 of a side). */
    static float dialPos (float deg) noexcept { return (deg < 0.0f ? -1.0f : 1.0f) * std::sqrt (juce::jmin (1.0f, std::abs (deg) / 180.0f)); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool measuring = status != (int) Status::noKick && status != (int) Status::weakSub && status != (int) Status::ducking;
        g.setColour (palette (cream).withAlpha (0.55f));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.12f));
        g.drawText (turned ? "CORRECTION ON KICK" : "PHASE ON KICK", b.withHeight (14.0f), juce::Justification::centred);

        // the dial: an arc of 180° from -180 (left) to +180 (right)
        const juce::Point<float> centre (b.getCentreX(), b.getY() + 62.0f);
        const float r = 44.0f;
        auto angle = [] (float pos) { return pos * juce::MathConstants<float>::halfPi; };
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (palette (juce::Colours::white).withAlpha (0.12f));
        g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path ok;
        ok.addCentredArc (centre.x, centre.y, r, r, 0.0f, angle (dialPos (-5.0f)), angle (dialPos (5.0f)), true);
        g.setColour (palette (green).withAlpha (0.8f));
        g.strokePath (ok, juce::PathStrokeType (7.0f));
        for (float d : { -90.0f, 90.0f })
        {
            const float an = angle (dialPos (d));
            g.setColour (palette (cream).withAlpha (0.3f));
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (r + 5.0f, an), centre.getPointOnCircumference (r - 5.0f, an)), 1.2f);
        }
        if (measuring)
        {
            const float an = angle (dialPos (error));
            juce::Path needle;
            needle.startNewSubPath (centre);
            needle.lineTo (centre.getPointOnCircumference (r + 2.0f, an));
            g.setColour (palette (turned ? lime : statusColour (status)));
            g.strokePath (needle, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (palette (juce::Colour (0xff121318)));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre));
        // the value, under the dial's hub
        g.setColour (palette (measuring ? (turned ? lime : statusColour (status)) : grey));
        g.setFont (gk::GrooveFonts::font (17.0f));
        const auto value = measuring ? (error >= 0.05f ? "+" : "") + num (error, 1) + fr ("\xc2\xb0") : juce::String ("--");
        g.drawText (value, juce::Rectangle<float> (b.getWidth(), 20.0f).withPosition (b.getX(), centre.y + 4.0f), juce::Justification::centred);
        g.setColour (palette (cream).withAlpha (0.5f));
        g.setFont (gk::GrooveFonts::font (10.0f));
        g.drawText (turned ? juce::String ("best on the collision") : "confidence " + (measuring ? juce::String (juce::roundToInt (confidence * 100.0f)) + " %" : juce::String ("--")),
                    juce::Rectangle<float> (b.getWidth(), 13.0f).withPosition (b.getX(), centre.y + 23.0f), juce::Justification::centred);
    }

private:
    KickLockProcessor& proc;
    const gk::Palette& palette;
    float error = 0.0f, confidence = 0.0f, shownError = 1.0e9f, shownConfidence = -1.0f;
    bool turned = false; // PRECISE + LOCK: the correction made, not what is left
    int status = -1;
};

//==============================================================================
/** The oscilloscope, one beat from the last kick: the kick (orange) and the
    bass as heard, its low band only (lime: under SUB CUT, where the phase is), and over them the ducking curve, editable like
    LFOTool's: drag a point; double-click to add one (or remove it); drag the
    small ring in the middle of a segment to bend it; right-click for the
    presets. */
class KickLockEditor::Scope final : public juce::Component, public juce::SettableTooltipClient
{
public:
    using Node = KickLockDsp::CurveNode;
    static constexpr double preBeats = 0.06;

    Scope (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
        auto& tel = proc.getDsp().getTelemetry();
        sub.assign ((size_t) juce::jmax (1, tel.scopeSize), 0.0f);
        kick.assign (sub.size(), 0.0f);
        bass.assign (sub.size(), 0.0f);
        curvePath.preallocateSpace (4096);
        wavePath.preallocateSpace (16384);
        setTooltip ("Drag the points of the ducking curve, double-click to add or remove one, drag a segment's ring to bend it, "
                    "right-click for the presets.");
    }

    void update()
    {
        auto& tel = proc.getDsp().getTelemetry();
        const int size = tel.scopeSize;
        if (size <= 0 || (int) sub.size() != size)
            return;
        // the last kicks, newest first
        const int count = tel.kickEvents.load (std::memory_order_acquire);
        for (int i = 0; i < 4; ++i)
            kicks[(size_t) i] = count - i > 0 ? tel.recentKicks[(size_t) ((count - i) % 4)].load (std::memory_order_relaxed) : -1;
        write = tel.scopeWrite.load (std::memory_order_acquire);
        endTime = tel.scopeTime.load (std::memory_order_relaxed);
        for (int i = 0; i < size; ++i)
        {
            sub[(size_t) i] = tel.scopeSub[(size_t) i].load (std::memory_order_relaxed);
            kick[(size_t) i] = tel.scopeKick[(size_t) i].load (std::memory_order_relaxed);
            bass[(size_t) i] = tel.scopeBass[(size_t) i].load (std::memory_order_relaxed);
        }
        repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (palette (navy));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.5f);
        if (explainKnob >= 0)
        {
            paintExplanation (g, b);
            return;
        }
        g.setColour (palette (cream).withAlpha (0.5f));
        g.setFont (gk::GrooveFonts::font (10.5f).withExtraKerningFactor (0.12f));
        const bool beats = proc.getDsp().getTelemetry().triggerSource.load (std::memory_order_relaxed) == 2;
        const int source = proc.getDsp().getTelemetry().triggerSource.load (std::memory_order_relaxed);
        g.drawText (beats ? "TEMPO" : source == 3 ? "LINK" : "SIDECHAIN", b.reduced (12.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);
        g.drawText (beatsLabel(), b.reduced (12.0f, 4.0f).withHeight (14.0f).withTrimmedLeft (80.0f), juce::Justification::centredLeft);

        const auto m = mainArea();
        const int size = (int) sub.size();
        const double sr = 4.0 * size; // the rings hold 2 s at 1 in 8
        const int dec = KickTelemetry::scopeDecimation;
        auto valueAt = [&] (const std::vector<float>& ring, std::int64_t time, bool& ok)
        {
            const auto back = (endTime - time) / dec;
            ok = back >= 0 && back < size - 1;
            return ok ? ring[(size_t) ((((std::int64_t) write - 1 - back) % size + size) % size)] : 0.0f;
        };

        // grid: gain 0 / 50 / 100 %
        g.setColour (palette (juce::Colours::white).withAlpha (0.07f));
        for (float y : { 0.0f, 0.5f, 1.0f })
            g.drawHorizontalLine (juce::roundToInt (gainY (m, y)), m.getX(), m.getRight());

        // the kick to show: the latest one heard entirely over the window
        const double beat = 60.0 / bpmNow();
        const double winBeats = windowBeats();
        const auto pre = (std::int64_t) (preBeats * beat * sr), span = (std::int64_t) ((preBeats + winBeats) * beat * sr);
        std::int64_t shown = -1;
        for (auto k : kicks)
            if (k >= 0 && k - pre + span <= endTime && endTime - (k - pre) < (std::int64_t) (size - 2) * dec)
            {
                shown = k;
                break;
            }
        const float kickX = m.getX() + (float) (preBeats / (preBeats + winBeats)) * m.getWidth();
        if (shown >= 0)
        {
            // the waveforms themselves (Alan: the wave, not blocks): every
            // stored sample (1 in 8) on a line, the kick behind, the bass as
            // heard in front, under the curve
            // each wave at its own scale (the scope shows when and how, not
            // which is louder: a loud bass made the kick look tiny)
            float peakBass = 0.05f, peakKick = 0.05f;
            for (auto t = shown - pre; t < shown - pre + span; t += dec)
            {
                bool ok = false;
                peakBass = juce::jmax (peakBass, std::abs (valueAt (bass, t, ok)));
                peakKick = juce::jmax (peakKick, std::abs (valueAt (kick, t, ok)));
            }
            const float mid = m.getCentreY();
            for (int pass = beats ? 1 : 0; pass < 2; ++pass)
            {
                const auto& ring = pass == 0 ? kick : bass;
                const float half = m.getHeight() * 0.46f / (pass == 0 ? peakKick : peakBass);
                wavePath.clear();
                bool started = false;
                for (auto t = shown - pre; t < shown - pre + span; t += dec)
                {
                    bool ok = false;
                    const float v = valueAt (ring, t, ok);
                    if (! ok)
                        continue;
                    const float x = m.getX() + (float) (t - (shown - pre)) / (float) span * m.getWidth();
                    const float y = mid - juce::jlimit (-1.0f, 1.0f, v * half / (m.getHeight() * 0.5f)) * m.getHeight() * 0.5f;
                    if (! started)
                        wavePath.startNewSubPath (x, y);
                    else
                        wavePath.lineTo (x, y);
                    started = true;
                }
                g.setColour (palette (pass == 0 ? orange : lime).withAlpha (pass == 0 ? 0.55f : 0.9f));
                g.strokePath (wavePath, juce::PathStrokeType (pass == 0 ? 1.0f : 1.2f));
            }
        }
        else
        {
            g.setColour (palette (cream).withAlpha (0.85f));
            g.setFont (gk::GrooveFonts::font (15.0f));
            // why there is nothing to show
            auto& tl = proc.getDsp().getTelemetry();
            const auto linkNo = proc.getLinkChannelParameter().getCurrentChoiceName();
            const int trig = proc.getTriggerParameter().getIndex();
            const bool playing = tl.hostPlaying.load (std::memory_order_relaxed), linked = tl.linkSlot.load (std::memory_order_relaxed) >= 0;
            juce::String title, hint;
            if (! playing)
            {
                title = "PRESS PLAY";
                hint = linked ? "Kick Lock Send found on LINK " + linkNo : "no Kick Lock Send on LINK " + linkNo + ": the tempo will lead";
            }
            else if (trig == KickLockDsp::triggerSidechain)
            {
                title = "NO KICK ON THE SIDECHAIN";
                hint = "or TRIGGER Auto / Link, with Kick Lock Send on the kick's track";
            }
            else if (linked)
            {
                title = "WAITING FOR KICKS";
                hint = "Kick Lock Send on LINK " + linkNo + " sends nothing: is its kick playing, SENS high enough?";
            }
            else
            {
                title = "NO KICK LOCK SEND ON LINK " + linkNo;
                hint = "put Kick Lock Send on the kick's track, LINK " + linkNo;
            }
            g.drawFittedText (title, m.withTrimmedBottom (m.getHeight() * 0.5f).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
            g.setColour (palette (cream).withAlpha (0.6f));
            g.setFont (gk::GrooveFonts::font (11.5f));
            g.drawFittedText (hint, m.withTrimmedTop (m.getHeight() * 0.55f).withHeight (18.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        dashed (g, kickX, m.getY(), m.getBottom(), 0.75f);

        // the ducking curve (gain over the release, then held)
        const auto nodes = proc.curveFor (proc.getShapeParameter().getIndex());
        curvePath.clear();
        curvePath.startNewSubPath (m.getX(), gainY (m, 1.0f));
        curvePath.lineTo (kickX - 3.0f, gainY (m, 1.0f));
        const int steps = 160;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            curvePath.lineTo (curveX (m, x), gainY (m, KickLockDsp::evalCurve (nodes, x)));
        }
        curvePath.lineTo (m.getRight(), gainY (m, nodes.back().y));
        juce::Path fill (curvePath);
        fill.lineTo (m.getRight(), m.getY());
        fill.lineTo (m.getX(), m.getY());
        fill.closeSubPath();
        g.setColour (palette (juce::Colours::black).withAlpha (0.22f)); // what the ducking takes away
        g.fillPath (fill);
        g.setColour (palette (cream));
        g.strokePath (curvePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
        {
            const auto h = handlePos (m, nodes, (int) i);
            g.setColour (palette (cream).withAlpha (hover == handleId ((int) i) ? 1.0f : 0.55f));
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (h), 1.3f);
        }
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto pt = nodePos (m, nodes[i]);
            const float r = hover == (int) i || dragging == (int) i ? 6.5f : 5.0f;
            g.setColour (palette (lime));
            g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (pt));
            g.setColour (palette (palette.outline));
            g.drawEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (pt), 1.5f);
        }
        // where the ducking is now
        auto& tel = proc.getDsp().getTelemetry();
        const auto since = tel.outputClock.load (std::memory_order_relaxed) - tel.lastKickSample.load (std::memory_order_relaxed);
        const double rel = KickLockDsp::releaseBeats (proc.getReleaseParameter().getIndex()) * beat * sr;
        if (shown >= 0 && since >= 0 && (double) since < rel && ! proc.getBypassParameterObject().get())
        {
            const float x = (float) ((double) since / rel);
            g.setColour (palette (orange));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ curveX (m, x), gainY (m, KickLockDsp::evalCurve (nodes, x)) }));
        }
        g.setColour (palette (cream).withAlpha (0.4f));
        g.setFont (gk::GrooveFonts::font (9.0f));
        g.drawText ("100 %", juce::Rectangle<float> (m.getX() + 2.0f, gainY (m, 1.0f) + 1.0f, 40.0f, 10.0f), juce::Justification::topLeft);
        g.drawText (beats ? "BEAT" : "KICK", juce::Rectangle<float> (40.0f, 10.0f).withCentre ({ kickX, m.getBottom() + 6.0f }), juce::Justification::centred);

    }

    //==========================================================================
    /** TARGET (0), OFFSET (1), SUB CUT (2), WAVE START (3): the screen explains it for a moment. */
    enum Explained { explainTarget, explainOffset, explainSubCut, explainWaveStart };
    void explain (int which, double dt)
    {
        if (which >= 0)
        {
            explainKnob = which;
            explainLeft = 1.5;
        }
        else if (explainKnob >= 0 && (explainLeft -= dt) <= 0.0)
            explainKnob = -1;
        explainTime += dt;
        if (explainKnob >= 0)
            repaint();
    }
    bool isExplaining() const noexcept { return explainKnob >= 0; }

    void paintExplanation (juce::Graphics& g, juce::Rectangle<float> b)
    {
        auto area = b.reduced (12.0f, 6.0f);
        auto plain = [] (juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); };
        const float target = plain (proc.getTargetPhaseParameter()), offset = plain (proc.getOffsetParameter());
        const float cut = plain (proc.getSubCutoffParameter()), start = plain (proc.getWaveStartParameter());
        const float f0 = proc.getDsp().getTelemetry().f0Hz.load (std::memory_order_relaxed);
        const float hz = f0 > 1.0f ? f0 : 55.0f;
        const float pulse = 0.5f + 0.5f * (float) std::sin (explainTime * 5.0);
        static const char* titles[] { "TARGET  \xc2\xb7  the sub against the kick where they collide (0 = in phase)",
                                      "OFFSET  \xc2\xb7  the sub moved later / earlier than the kick",
                                      "SUB CUT  \xc2\xb7  the highest note whose sub is locked",
                                      "WAVE START  \xc2\xb7  the sub's cycle turned, on top of the lock" };
        g.setColour (palette (cream).withAlpha (0.85f));
        g.setFont (gk::GrooveFonts::font (11.5f).withExtraKerningFactor (0.04f));
        g.drawText (fr (titles[explainKnob]), area.withHeight (16.0f), juce::Justification::centredLeft);
        auto m = area.withTrimmedTop (22.0f).withTrimmedBottom (18.0f);
        const float mid = m.getCentreY(), amp = m.getHeight() * 0.36f;
        const float kickX = m.getX() + m.getWidth() * 0.36f;
        const float cycle = m.getWidth() * 0.3f; // one cycle of the sub on screen
        auto sine = [&] (float phaseAtKickDeg, float shiftPx, juce::Colour c, float width, bool dashedLine)
        {
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = m.getX() + m.getWidth() * (float) i / 200.0f;
                const float ph = juce::MathConstants<float>::twoPi * (x - kickX - shiftPx) / cycle + juce::degreesToRadians (phaseAtKickDeg);
                const float y = mid - amp * std::sin (ph);
                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }
            if (dashedLine)
            {
                juce::Path d;
                const float dashes[] { 5.0f, 4.0f };
                juce::PathStrokeType (width).createDashedStroke (d, p, dashes, 2);
                g.setColour (c);
                g.fillPath (d);
            }
            else
            {
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        };
        auto kickMark = [&]
        {
            // the kick: its line, and a small burst that flashes like a hit
            dashed (g, kickX, m.getY(), m.getBottom(), 0.8f);
            g.setColour (palette (orange).withAlpha (0.35f + 0.65f * pulse));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ kickX, m.getBottom() + 6.0f }));
            g.setColour (palette (cream).withAlpha (0.6f));
            g.setFont (gk::GrooveFonts::font (9.5f));
            g.drawText ("KICK", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ kickX + 22.0f, m.getBottom() + 6.0f }), juce::Justification::centred);
        };
        auto dial = [&] (juce::Point<float> c, float deg, const juce::String& label)
        {
            const float r = 20.0f;
            g.setColour (palette (juce::Colours::white).withAlpha (0.15f));
            g.drawEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c), 2.0f);
            // 0° = the rising zero crossing: at 9 o'clock... drawn as a phasor, 0° pointing right, turning anticlockwise
            const float a = juce::degreesToRadians (deg);
            const juce::Point<float> tip (c.x + r * std::cos (a), c.y - r * std::sin (a));
            g.setColour (palette (lime));
            g.drawLine (juce::Line<float> (c, tip), 2.5f);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip));
            g.setColour (palette (cream));
            g.setFont (gk::GrooveFonts::font (13.0f));
            g.drawText (label, juce::Rectangle<float> (80.0f, 16.0f).withCentre ({ c.x, c.y + r + 12.0f }), juce::Justification::centred);
        };
        auto dotOnWave = [&] (float x, float phaseDeg)
        {
            const float y = mid - amp * std::sin (juce::degreesToRadians (phaseDeg));
            g.setColour (palette (cream).withAlpha (0.4f + 0.6f * pulse));
            g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, y }));
        };
        g.setColour (palette (juce::Colours::white).withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (mid), m.getX(), m.getRight());
        g.setFont (gk::GrooveFonts::font (10.5f));
        const auto note = area.removeFromBottom (14.0f);
        switch (explainKnob)
        {
            case explainTarget:
            {
                sine (0.0f, 0.0f, palette (cream).withAlpha (0.25f), 1.2f, true); // 0°: rising through zero on the kick
                sine (target, 0.0f, palette (lime), 2.4f, false);
                kickMark();
                dotOnWave (kickX, target);
                dial ({ m.getRight() - 34.0f, m.getY() + 26.0f }, target, juce::String (juce::roundToInt (target)) + fr ("\xc2\xb0"));
                g.setColour (palette (cream).withAlpha (0.6f));
                g.drawText (fr ("dashed: 0\xc2\xb0, in phase with the kick where the sub comes back (the usual)"), note, juce::Justification::centredLeft);
                break;
            }
            case explainOffset:
            {
                const float pxPerMs = cycle * hz / 1000.0f;
                const float shift = juce::jlimit (-m.getWidth() * 0.3f, m.getWidth() * 0.3f, offset * pxPerMs);
                sine (target, 0.0f, palette (cream).withAlpha (0.25f), 1.2f, true); // locked, no offset
                sine (target, shift, palette (lime), 2.4f, false);
                kickMark();
                // the arrow from where it was to where it goes
                const float y = m.getY() + 8.0f;
                if (std::abs (shift) > 3.0f)
                {
                    juce::Path arrow;
                    arrow.addArrow (juce::Line<float> (kickX, y, kickX + shift, y), 2.0f, 9.0f, 9.0f);
                    g.setColour (palette (orange));
                    g.fillPath (arrow);
                }
                g.setColour (palette (cream));
                g.setFont (gk::GrooveFonts::font (13.0f));
                g.drawText ((offset >= 0.05f ? "+" : "") + num (offset, 1) + " ms " + (offset > 0.05f ? "(later)" : offset < -0.05f ? "(earlier)" : ""),
                            juce::Rectangle<float> (150.0f, 16.0f).withCentre ({ kickX + shift * 0.5f, y + 14.0f }), juce::Justification::centred);
                g.setColour (palette (cream).withAlpha (0.6f));
                g.setFont (gk::GrooveFonts::font (10.5f));
                g.drawText (fr ("dashed: where the lock puts the sub \xc2\xb7 lime: moved by OFFSET (") + num (hz, 0) + " Hz)", note, juce::Justification::centredLeft);
                break;
            }
            case explainSubCut:
            {
                // a frequency axis 20..500 Hz (log): what is locked, the cut, the note now
                auto xOf = [&] (float f) { return m.getX() + m.getWidth() * std::log (f / 20.0f) / std::log (500.0f / 20.0f); };
                const float base = m.getBottom() - 14.0f, top = m.getY() + 10.0f;
                juce::Path resp;
                for (int i = 0; i <= 160; ++i)
                {
                    const float f = 20.0f * std::pow (25.0f, (float) i / 160.0f);
                    const float level = f <= cut ? 1.0f : juce::jmax (0.0f, 1.0f - (f - cut) / 180.0f);
                    const float x = xOf (f), y = base - (base - top) * level;
                    if (i == 0)
                        resp.startNewSubPath (x, y);
                    else
                        resp.lineTo (x, y);
                }
                juce::Path fill (resp);
                fill.lineTo (xOf (500.0f), base);
                fill.lineTo (xOf (20.0f), base);
                fill.closeSubPath();
                g.setColour (palette (lime).withAlpha (0.18f));
                g.fillPath (fill);
                g.setColour (palette (lime));
                g.strokePath (resp, juce::PathStrokeType (2.2f));
                g.setColour (palette (juce::Colours::white).withAlpha (0.25f));
                g.drawHorizontalLine (juce::roundToInt (base), m.getX(), m.getRight());
                g.setFont (gk::GrooveFonts::font (9.5f));
                for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f })
                {
                    g.setColour (palette (cream).withAlpha (0.5f));
                    g.drawText (juce::String (juce::roundToInt (f)), juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ juce::jlimit (m.getX() + 10.0f, m.getRight() - 10.0f, xOf (f)), base + 8.0f }),
                                juce::Justification::centred);
                }
                dashed (g, xOf (cut), top, base, 0.8f);
                g.setColour (palette (cream));
                g.setFont (gk::GrooveFonts::font (13.0f));
                g.drawText ("SUB CUT " + juce::String (juce::roundToInt (cut)) + " Hz", juce::Rectangle<float> (130.0f, 16.0f).withCentre ({ xOf (cut), top - 2.0f }), juce::Justification::centred);
                if (f0 > 1.0f)
                {
                    const bool locked = f0 <= cut * 1.05f;
                    const float x = xOf (juce::jlimit (20.0f, 500.0f, f0));
                    g.setColour (palette (locked ? juce::Colour (0xff3ddc84) : juce::Colour (0xffff4d4d)).withAlpha (0.5f + 0.5f * pulse));
                    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, base - (base - top) * 0.5f }));
                    g.setFont (gk::GrooveFonts::font (11.0f));
                    g.drawText ("your note " + num (f0, 0) + " Hz: " + (locked ? "locked" : "above: not locked"),
                                juce::Rectangle<float> (170.0f, 14.0f).withCentre ({ juce::jlimit (m.getX() + 85.0f, m.getRight() - 85.0f, x), base - (base - top) * 0.5f - 16.0f }),
                                juce::Justification::centred);
                }
                g.setColour (palette (cream).withAlpha (0.6f));
                g.setFont (gk::GrooveFonts::font (10.5f));
                g.drawText ("only the fundamental under the cut is turned, the rest is never touched", note, juce::Justification::centredLeft);
                break;
            }
            case explainWaveStart:
            default:
            {
                // the cycle turned by WAVE START, on top of the lock: the dashed one without it
                sine (target, 0.0f, palette (cream).withAlpha (0.25f), 1.2f, true);
                sine (target - start, 0.0f, palette (lime), 2.4f, false);
                kickMark();
                dotOnWave (kickX, target - start);
                dial ({ m.getRight() - 34.0f, m.getY() + 26.0f }, -start, juce::String (juce::roundToInt (start)) + fr ("\xc2\xb0"));
                g.setColour (palette (cream).withAlpha (0.6f));
                g.drawText (fr ("dashed: without it \xc2\xb7 works with LOCK off too: a colour, not a fix"), note, juce::Justification::centredLeft);
                break;
            }
        }
    }

    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitTestCurve (e.position);
        if (h != hover)
        {
            hover = h;
            setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showPresets();
            return;
        }
        dragging = hitTestCurve (e.position);
        if (dragging < 0)
            return;
        edit = startEditing();
        lastY = e.position.y;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        const auto m = mainArea();
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        if (dragging >= handleBase)
        {
            // bend the segment: up = rises fast (negative tension)
            auto& n = edit[(size_t) (dragging - handleBase)];
            const float dir = edit[(size_t) (dragging - handleBase) + 1].y >= n.y ? 1.0f : -1.0f;
            n.tension = juce::jlimit (-12.0f, 12.0f, n.tension + dir * (e.position.y - lastY) * 0.08f * fine);
            lastY = e.position.y;
        }
        else
        {
            auto& n = edit[(size_t) dragging];
            const bool end = dragging == 0 || dragging == (int) edit.size() - 1;
            if (! end)
            {
                const float lo = edit[(size_t) dragging - 1].x + 0.005f, hi = edit[(size_t) dragging + 1].x - 0.005f;
                n.x = juce::jlimit (lo, hi, xFromCurve (m, e.position.x));
            }
            n.y = juce::jlimit (0.0f, 1.0f, yFromGain (m, e.position.y));
        }
        proc.setCustomCurve (edit);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hitTestCurve (e.position);
        edit = startEditing();
        if (h >= 0 && h < handleBase)
        {
            if (h > 0 && h < (int) edit.size() - 1) // the ends stay
                edit.erase (edit.begin() + h);
        }
        else if (h >= handleBase)
            edit[(size_t) (h - handleBase)].tension = 0.0f; // straight again
        else if ((int) edit.size() < KickLockDsp::maxCurveNodes)
        {
            const auto m = mainArea();
            const float x = xFromCurve (m, e.position.x);
            if (x > 0.0f && x < 1.0f)
            {
                Node n { x, juce::jlimit (0.0f, 1.0f, yFromGain (m, e.position.y)), 0.0f };
                edit.insert (std::upper_bound (edit.begin(), edit.end(), n, [] (const Node& a, const Node& b) { return a.x < b.x; }), n);
            }
        }
        proc.setCustomCurve (edit);
        hover = -1;
        repaint();
    }

private:
    static constexpr int handleBase = 1000;
    static int handleId (int segment) noexcept { return handleBase + segment; }

    juce::Rectangle<float> mainArea() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (14.0f); }
    double bpmNow() const { return juce::jmax (40.0, (double) proc.getFeatures().bpm.load (std::memory_order_relaxed)); }
    double windowBeats() const { return juce::jmax (1.0, KickLockDsp::releaseBeats (proc.getReleaseParameter().getIndex())); }
    juce::String beatsLabel() const { return fr ("1 beat \xc2\xb7 curve = RELEASE ") + proc.getReleaseParameter().getCurrentChoiceName(); }

    float curveX (juce::Rectangle<float> m, float x) const
    {
        const double total = preBeats + windowBeats(), rel = KickLockDsp::releaseBeats (proc.getReleaseParameter().getIndex());
        return m.getX() + (float) ((preBeats + x * rel) / total) * m.getWidth();
    }
    float xFromCurve (juce::Rectangle<float> m, float px) const
    {
        const double total = preBeats + windowBeats(), rel = KickLockDsp::releaseBeats (proc.getReleaseParameter().getIndex());
        return (float) juce::jlimit (0.0, 1.0, (((double) (px - m.getX()) / m.getWidth()) * total - preBeats) / rel);
    }
    static float gainY (juce::Rectangle<float> m, float gain) { return m.getBottom() - 4.0f - gain * (m.getHeight() - 10.0f); }
    static float yFromGain (juce::Rectangle<float> m, float y) { return (m.getBottom() - 4.0f - y) / (m.getHeight() - 10.0f); }
    juce::Point<float> nodePos (juce::Rectangle<float> m, const Node& n) const { return { curveX (m, n.x), gainY (m, n.y) }; }
    juce::Point<float> handlePos (juce::Rectangle<float> m, const std::vector<Node>& n, int seg) const
    {
        const float x = 0.5f * (n[(size_t) seg].x + n[(size_t) seg + 1].x);
        return { curveX (m, x), gainY (m, KickLockDsp::evalCurve (n, x)) };
    }

    int hitTestCurve (juce::Point<float> p) const
    {
        const auto m = mainArea();
        const auto nodes = proc.curveFor (proc.getShapeParameter().getIndex());
        for (size_t i = 0; i < nodes.size(); ++i)
            if (p.getDistanceFrom (nodePos (m, nodes[i])) < 9.0f)
                return (int) i;
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
            if (p.getDistanceFrom (handlePos (m, nodes, (int) i)) < 7.0f)
                return handleId ((int) i);
        return -1;
    }

    /** Editing a preset turns it into the custom curve (SHAPE = Custom). */
    std::vector<Node> startEditing()
    {
        auto& shapeParam = proc.getShapeParameter();
        const int current = shapeParam.getIndex();
        if (current != KickLockDsp::custom)
        {
            proc.setCustomCurve (proc.curveFor (current));
            shapeParam.beginChangeGesture();
            shapeParam.setValueNotifyingHost (shapeParam.convertTo0to1 ((float) KickLockDsp::custom));
            shapeParam.endChangeGesture();
        }
        return proc.getCustomCurve();
    }

    void showPresets()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Ducking curve");
        const char* names[] { "Exponential", "Linear", "S-curve", "Bounce" };
        for (int s = 0; s < 4; ++s)
            menu.addItem (names[s], [this, s]
                          {
                              auto& shapeParam = proc.getShapeParameter();
                              proc.setCustomCurve (KickLockDsp::presetCurve (s));
                              shapeParam.beginChangeGesture();
                              shapeParam.setValueNotifyingHost (shapeParam.convertTo0to1 ((float) KickLockDsp::custom));
                              shapeParam.endChangeGesture();
                              repaint();
                          });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void dashed (juce::Graphics& g, float x, float y0, float y1, float alpha)
    {
        juce::Path line, out;
        line.startNewSubPath (x, y0);
        line.lineTo (x, y1);
        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (out, line, dashes, 2);
        g.setColour (palette (juce::Colours::white).withAlpha (alpha));
        g.fillPath (out);
    }

    KickLockProcessor& proc;
    const gk::Palette& palette;
    std::vector<float> sub, kick, bass;
    int write = 0;
    std::int64_t endTime = 0;
    std::array<std::int64_t, 4> kicks { -1, -1, -1, -1 };
    juce::Path curvePath, wavePath;
    int explainKnob = -1;
    double explainLeft = 0.0, explainTime = 0.0;
    std::vector<Node> edit;
    int hover = -1, dragging = -1;
    float lastY = 0.0f;
};

//==============================================================================
class KickLockEditor::MetricsBar final : public juce::Component
{
public:
    MetricsBar (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
        reset.setButtonText ("Reset");
        reset.setTooltip ("Every setting back to its default");
        reset.onClick = [this] { proc.resetAllToDefaults(); };
        addAndMakeVisible (reset);
    }

    void update()
    {
        auto& tel = proc.getDsp().getTelemetry();
        const int st = tel.status.load (std::memory_order_relaxed);
        const bool measuring = st != (int) Status::noKick && st != (int) Status::weakSub && st != (int) Status::ducking;
        const double sr = juce::jmax (1.0, 4.0 * tel.scopeSize); // the rings hold 2 s at 1 in 8
        const float f0 = tel.f0Hz.load (std::memory_order_relaxed);
        const auto dot = fr ("  \xc2\xb7  ");
        juce::String t;
        t << "error " << (measuring ? num (tel.phaseErrorDeg.load (std::memory_order_relaxed), 1) + fr ("\xc2\xb0") : juce::String ("--")) << dot
          << "confidence " << (measuring ? num (tel.phaseConfidence.load (std::memory_order_relaxed), 2) : juce::String ("--")) << dot
          << "kick at collision " << (measuring ? num (tel.kickPhaseDeg.load (std::memory_order_relaxed), 0) + fr ("\xc2\xb0") : juce::String ("--")) << dot
          << "mono sum " << fr ("\xe2\x88\x92") << num (tel.monoSumLossDb.load (std::memory_order_relaxed), 1) << " dB" << dot
          << "f0 " << (f0 > 1.0f ? num (f0, 1) + " Hz" : juce::String ("--")) << dot
          << (tel.linkSlot.load (std::memory_order_relaxed) >= 0
              ? "link " + proc.getLinkChannelParameter().getCurrentChoiceName() + ": " + proc.getDsp().linkName() + " (" + juce::String (tel.linkKicks.load (std::memory_order_relaxed)) + " kicks)"
                    + (proc.getDsp().linkSendersOnChannel() > 1 ? " (" + juce::String (proc.getDsp().linkSendersOnChannel()) + " Sends on it!)" : juce::String())
                    + (proc.getDsp().linkLateKicks() > 0 ? " (late kicks: " + juce::String (proc.getDsp().linkLateKicks()) + ")" : juce::String()) + dot
              : juce::String())
          << "latency "
          << num (1000.0 * proc.getLatencySamples() / sr, 0) << " ms";
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void resized() override { reset.setBounds (getWidth() - 136, 8, 106, 24); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (palette (juce::Colours::black).withAlpha (0.25f));
        g.fillRect (getLocalBounds().toFloat().withTrimmedTop (2.0f));
        g.setColour (palette (cream).withAlpha (0.7f));
        g.setFont (gk::GrooveFonts::font (11.5f));
        g.drawFittedText (text, getLocalBounds().withTrimmedLeft (14).withTrimmedRight (146), juce::Justification::centredLeft, 1, 0.85f);
    }

private:
    class ResetButton final : public juce::Button
    {
    public:
        explicit ResetButton (const gk::Palette& p) : juce::Button ("Reset"), palette (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.0f).translated (0.0f, down ? 1.0f : 0.0f);
            g.setColour (palette (palette.surface).withMultipliedBrightness (over ? 1.25f : 1.0f));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (palette (palette.outline));
            g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.5f);
            g.setColour (palette (cream));
            g.setFont (gk::GrooveFonts::font (11.5f));
            g.drawText (getButtonText(), b, juce::Justification::centred);
        }

    private:
        const gk::Palette& palette;
    };

    KickLockProcessor& proc;
    const gk::Palette& palette;
    ResetButton reset { palette };
    juce::String text;
};

//==============================================================================
/** LOCK: a pill with a padlock, lit lime when the phase lock is on. */
/** LIVE / PRECISE: the engine (a toggle pill in the title bar). */
class KickLockEditor::EnginePill final : public juce::Button
{
public:
    explicit EnginePill (const gk::Palette& p) : juce::Button ("Engine"), palette (p)
    {
        setClickingTogglesState (true);
        setTitle ("Precise");
        setTooltip ("LIVE: the note's pitch is followed, 32 ms of latency (play live). PRECISE: no note detection - the bass's low band "
                    "and the kick are compared over the whole collision, seen ahead: ~360 ms of latency (the host compensates it: for mixing)");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const bool on = getToggleState();
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (on ? palette (lime) : palette (juce::Colour (0xff121318)).withAlpha (0.85f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        }
        g.setColour (on ? palette (palette.outline) : palette (lime).withAlpha (0.7f));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.5f);
        g.setFont (gk::GrooveFonts::font (14.0f).withExtraKerningFactor (0.08f));
        g.drawText (on ? "PRECISE" : "LIVE", b, juce::Justification::centred);
    }

private:
    const gk::Palette& palette;
};

class KickLockEditor::LockButton final : public juce::Button
{
public:
    explicit LockButton (const gk::Palette& p, bool phaseIcon = false) : juce::Button (phaseIcon ? "Phase" : "Lock"), palette (p), phase (phaseIcon)
    {
        setClickingTogglesState (true);
        setTitle (phaseIcon ? "Phase analysis" : "Lock");
        setTooltip (phaseIcon ? "PHASE: analyses the sub's phase at each kick (needed by LOCK). Off: the ducking alone, no analysis "
                                "(for a pad, a synth...). Off by itself when there is no bass or no kick heard."
                              : "LOCK: puts the sub in phase with every kick");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool on = getToggleState();
        const auto b = getLocalBounds().toFloat().reduced (2.0f).translated (0.0f, down ? 1.0f : 0.0f);
        if (on)
            g.setGradientFill (juce::ColourGradient (palette (lime), b.getX(), b.getY(), palette (limeDeep), b.getRight(), b.getBottom(), false));
        else
            g.setColour (palette (palette.surface));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        }
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
        const float alpha = isEnabled() ? 1.0f : 0.35f;
        if (phase)
        {
            // a sine
            const auto ink = on ? palette (palette.outline) : palette (cream).withAlpha (0.6f * alpha);
            const float cx = b.getX() + b.getHeight() * 0.62f, cy = b.getCentreY(), w = b.getHeight() * 0.5f, h = b.getHeight() * 0.18f;
            juce::Path sine;
            for (int i = 0; i <= 24; ++i)
            {
                const float x = cx - w * 0.5f + w * (float) i / 24.0f, y = cy - h * std::sin (juce::MathConstants<float>::twoPi * (float) i / 24.0f);
                if (i == 0)
                    sine.startNewSubPath (x, y);
                else
                    sine.lineTo (x, y);
            }
            g.setColour (ink);
            g.strokePath (sine, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (on ? palette (palette.outline) : palette (cream).withAlpha (0.75f * alpha));
            g.setFont (gk::GrooveFonts::font (16.0f).withExtraKerningFactor (0.08f));
            g.drawText ("PHASE", b.withTrimmedLeft (b.getHeight() * 1.05f), juce::Justification::centredLeft);
            return;
        }
        // padlock: body + shackle (open when off)
        const float s = b.getHeight() * 0.42f;
        const auto body = juce::Rectangle<float> (s, s * 0.78f).withCentre ({ b.getX() + b.getHeight() * 0.62f, b.getCentreY() + s * 0.22f });
        juce::Path shackle;
        const float lift = on ? 0.0f : s * 0.22f;
        shackle.addCentredArc (body.getCentreX(), body.getY() - lift, s * 0.3f, s * 0.36f, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
        const auto ink = on ? palette (palette.outline) : palette (cream).withAlpha (0.6f * alpha);
        g.setColour (ink);
        g.strokePath (shackle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (body, 2.0f);
        g.setColour (on ? palette (palette.outline) : palette (cream).withAlpha (0.75f * alpha));
        g.setFont (gk::GrooveFonts::font (16.0f).withExtraKerningFactor (0.08f));
        g.drawText ("LOCK", b.withTrimmedLeft (b.getHeight() * 1.05f), juce::Justification::centredLeft);
    }

private:
    const gk::Palette& palette;
    bool phase = false;
};

//==============================================================================
gk::Palette KickLockEditor::makePalette()
{
    gk::Palette p;
    p.background = juce::Colour (0xff35382f); // the suite's charcoal, a touch of Phaze's green
    p.backgroundEdge = juce::Colour (0xff1a1c17);
    p.surface = juce::Colour (0xff454b3c);
    p.outline = juce::Colour (0xff141414);
    p.text = cream;
    p.accent = lime;
    p.beret = lime; // power button, credit
    p.track = juce::Colours::white.withAlpha (0.12f);
    p.pointer = juce::Colour (0xff17171a);
    p.plaque = cream;
    p.arcStops = { lime, limeDeep };                 // MIX: lime → deep green
    p.knobBands = { lime, limeDeep, lime, limeDeep }; // the drum's shell
    p.pulse = orange;
    p.arrowLeft = lime;
    p.arrowRight = limeDeep;
    p.lowLabel = cream;                    // DRY
    p.highLabel = lime;                    // WET
    return p;
}

KickLockEditor::KickLockEditor (KickLockProcessor& p)
    : OneKnobEditor (p, makePalette(), KL_PRODUCT_NAME,
                     [&p] (gk::AudioFeatures&, const gk::Palette& palette) { return std::make_unique<PhazeMascot> (p, palette); }),
      proc (p)
{
    auto& pal = getPalette();
    setBackgroundCached (true);

    auto panels = std::make_unique<Panels> (pal);
    addToCanvas (*panels, { 0, 0, getBaseWidth(), getBaseHeight() });
    panels->toBack();
    panelsHolder = std::move (panels);

    placeMascotAndKnob ({ 26, 196, 150, 172 }, juce::Rectangle<int> (176, 176).withCentre ({ 391, 352 }));

    status = std::make_unique<StatusPill> (p, pal);
    addToCanvas (*status, { 576, 17, 132, 28 });
    engine = std::make_unique<EnginePill> (pal);
    engineAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getPreciseParameter(), *engine);
    // (under LOCK, where MODE was: Hold / Transition is gone, Alan)
    addToCanvas (*engine, { 488, 376, 86, 30 });
    gauge = std::make_unique<PhaseGauge> (p, pal);
    addToCanvas (*gauge, { 10, 70, 182, 104 });
    scope = std::make_unique<Scope> (p, pal);
    addToCanvas (*scope, { 204, 66, 374, 202 });
    metrics = std::make_unique<MetricsBar> (p, pal);
    addToCanvas (*metrics, { 0, 440, getBaseWidth(), 40 });

    // next to MIX: the kick threshold (left), LOCK and its MODE (right)
    lockButton = std::make_unique<LockButton> (pal);
    lockAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getLockParameter(), *lockButton);
    addToCanvas (*lockButton, { 488, 326, 86, 36 });
    phaseButton = std::make_unique<LockButton> (pal, true);
    phaseAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getPhaseParameter(), *phaseButton);
    addToCanvas (*phaseButton, { 488, 284, 86, 36 });

    trigger = std::make_unique<gk::ChoiceSelector> (p.getTriggerParameter(), "TRIGGER", orange, pal);
    trigger->setTooltip ("Auto: the kicks of Kick Lock Send on the same LINK number (put it on the kick's track), else the host's beats. "
                        "Link: Kick Lock Send only. Beat: the tempo only. Sidechain: the host's sidechain routing only.");
    addToCanvas (*trigger, { 210, 280, 84, 38 });
    linkSel = std::make_unique<gk::ChoiceSelector> (p.getLinkChannelParameter(), "LINK", lime, pal);
    linkSel->setTooltip ("The Kick Lock Send this one listens to: the same LINK number on the kick's track (0 to 10).");
    addToCanvas (*linkSel, { 210, 322, 84, 38 });

    // under Phaze: the bass's low end made mono (the bottom bar measures the
    // loss in mono; this cures it)
    monoSel = std::make_unique<gk::ChoiceSelector> (p.getMonoBelowParameter(), "MONO BELOW", lime, pal);
    monoSel->setTooltip ("Makes the bass mono under this frequency (fully below it, gently up to ~180 Hz above), with no phase shift: "
                         "the stereo of the highs untouched. Clubs and phones play the low end in mono.");
    addToCanvas (*monoSel, { 51, 386, 100, 38 });

    // the right column: ducking shape, release, the lock's settings, depth
    shape = std::make_unique<gk::ChoiceSelector> (p.getShapeParameter(), "SHAPE", orange, pal);
    release = std::make_unique<gk::ChoiceSelector> (p.getReleaseParameter(), "RELEASE", orange, pal);
    shape->setDisplayNames ({ "Expo", "Linear", "S-curve", "Bounce", "Custom" });
    addToCanvas (*shape, { 590, 274, 78, 38 });
    addToCanvas (*release, { 672, 274, 78, 38 });
    struct K { juce::RangedAudioParameter& param; const char* caption; juce::Colour colour; juce::Rectangle<int> at; };
    const K ks[] {
        { p.getTargetPhaseParameter(), "TARGET", lime, { 596, 76, 64, 68 } },
        { p.getOffsetParameter(), "OFFSET", lime, { 680, 76, 64, 68 } },
        { p.getSubCutoffParameter(), "SUB CUT", limeDeep.brighter (0.3f), { 596, 160, 64, 68 } },
        { p.getWaveStartParameter(), "WAVE START", limeDeep.brighter (0.3f), { 680, 160, 64, 68 } },
        { p.getDepthParameter(), "DEPTH", orange, { 638, 340, 64, 68 } },
        { p.getThresholdParameter(), "THRESHOLD", orange, { 222, 364, 64, 68 } },
    };
    for (const auto& k : ks)
    {
        knobs.push_back (std::make_unique<gk::SmallKnob> (k.param, pal, k.caption, k.colour));
        addToCanvas (*knobs.back(), k.at);
    }
    editorTick (0.0);
}

KickLockEditor::~KickLockEditor() = default;

void KickLockEditor::setPreviewExplanation (int which)
{
    scope->explain (which, 0.0);
    scope->repaint();
}

void KickLockEditor::setPreviewStatus (int st, int kicks)
{
    previewStatus = st;
    if (auto* m = dynamic_cast<PhazeMascot*> (&getMascot()))
        m->setPreview (st, kicks);
    snapForSnapshot();
}

void KickLockEditor::editorTick (double dt)
{
    const bool snap = dt <= 0.0;
    if (status == nullptr)
        return;
    status->update (previewStatus);
    gauge->update (previewStatus, snap);
    // LOCK needs the phase analysis
    if (const bool phaseOn = proc.getPhaseParameter().get(); lockButton->isEnabled() != phaseOn)
    {
        lockButton->setEnabled (phaseOn);
        engine->setEnabled (phaseOn);
    }
    // TARGET, OFFSET, SUB CUT, WAVE START (knobs 0..3): touched, hovered or
    // automated, the scope explains what it does (Alan: "trop obscur")
    {
        int which = -1;
        for (int k = 0; k < 4 && k < (int) knobs.size(); ++k)
        {
            const float v = knobs[(size_t) k]->getParameter().getValue();
            if (knobs[(size_t) k]->isMouseOverOrDragging() || (lastKnobValues[(size_t) k] >= 0.0f && std::abs (v - lastKnobValues[(size_t) k]) > 1.0e-5f))
                which = k;
            lastKnobValues[(size_t) k] = v;
        }
        scope->explain (which, snap ? 0.0 : dt);
    }
    scope->update();
    if (snap || ++frameCount % 4 == 0) // the numbers: 15 a second is plenty to read
        metrics->update();
}
} // namespace kl
