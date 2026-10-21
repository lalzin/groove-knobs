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
    juce::String num (double v, int decimals) { return juce::String (v, decimals).replaceCharacter ('.', ','); }

    juce::Colour statusColour (int st)
    {
        switch ((Status) st)
        {
            case Status::ok: return green;
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
            case Status::noKick:
            default: return "NO KICK";
        }
    }

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
            case Status::noKick:
            default: return "No kick on the sidechain: in Live, open Sidechain in the plug-in's title bar and pick the kick track in Audio From";
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
        const int key = bypassed ? 100 : st;
        if (key != shown || lock != shownLock)
        {
            shown = key;
            shownLock = lock;
            text = bypassed ? juce::String ("BYPASS") : statusWord (st, lock);
            setTooltip (bypassed ? juce::String ("Bypassed") : statusSentence (st, lock));
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
    int shown = -1;
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
        setTooltip ("Phase of the sub on the kick, against the target (TARGET). Green: within 5 degrees.");
    }

    void update (int preview, bool snap)
    {
        auto& tel = proc.getDsp().getTelemetry();
        const float e = tel.phaseErrorDeg.load (std::memory_order_relaxed);
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
        const bool measuring = status != (int) Status::noKick && status != (int) Status::weakSub;
        g.setColour (palette (cream).withAlpha (0.55f));
        g.setFont (gk::GrooveFonts::font (11.0f).withExtraKerningFactor (0.12f));
        g.drawText ("PHASE ON KICK", b.withHeight (14.0f), juce::Justification::centred);

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
            g.setColour (palette (statusColour (status)));
            g.strokePath (needle, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (palette (juce::Colour (0xff121318)));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre));
        // the value, under the dial's hub
        g.setColour (palette (measuring ? statusColour (status) : grey));
        g.setFont (gk::GrooveFonts::font (17.0f));
        const auto value = measuring ? (error >= 0.05f ? "+" : "") + num (error, 1) + fr ("\xc2\xb0") : juce::String ("--");
        g.drawText (value, juce::Rectangle<float> (b.getWidth(), 20.0f).withPosition (b.getX(), centre.y + 4.0f), juce::Justification::centred);
        g.setColour (palette (cream).withAlpha (0.5f));
        g.setFont (gk::GrooveFonts::font (10.0f));
        g.drawText ("confidence " + (measuring ? juce::String (juce::roundToInt (confidence * 100.0f)) + " %" : juce::String ("--")),
                    juce::Rectangle<float> (b.getWidth(), 13.0f).withPosition (b.getX(), centre.y + 23.0f), juce::Justification::centred);
    }

private:
    KickLockProcessor& proc;
    const gk::Palette& palette;
    float error = 0.0f, confidence = 0.0f, shownError = 1.0e9f, shownConfidence = -1.0f;
    int status = -1;
};

//==============================================================================
/** The oscilloscope: the sub as heard around the last kicks, overlaid (when
    it is locked they fall on top of each other and cross zero on the dashed
    line — the kick), the kick's envelope; underneath, the last 2 seconds. */
class KickLockEditor::Scope final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr double preSeconds = 0.025, postSeconds = 0.075;
    static constexpr int shownKicks = 4;

    Scope (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal)
    {
        auto& tel = proc.getDsp().getTelemetry();
        sub.assign ((size_t) juce::jmax (1, tel.scopeSize), 0.0f);
        kick.assign (sub.size(), 0.0f);
        for (auto& path : subPaths)
            path.preallocateSpace (4096);
        kickPath.preallocateSpace (4096);
        overview.preallocateSpace (4096);
        setTooltip ("The sub as heard around the last kicks (lime), the kick's envelope (orange). Locked: the curves overlap "
                    "and cross zero on the dashed line.");
    }

    void update()
    {
        auto& tel = proc.getDsp().getTelemetry();
        const int size = tel.scopeSize;
        if (size <= 0 || (int) sub.size() != size)
            return;
        // the kicks, as they come (output time = the kick's own time)
        const auto k = tel.lastKickSample.load (std::memory_order_relaxed);
        if (k >= 0 && k != lastKick)
        {
            lastKick = k;
            for (int i = shownKicks - 1; i > 0; --i)
                pending[(size_t) i] = pending[(size_t) i - 1];
            pending[0] = k;
            pendingCount = juce::jmin (shownKicks, pendingCount + 1);
        }
        write = tel.scopeWrite.load (std::memory_order_acquire);
        endTime = tel.scopeTime.load (std::memory_order_relaxed);
        for (int i = 0; i < size; ++i)
        {
            sub[(size_t) i] = tel.scopeSub[(size_t) i].load (std::memory_order_relaxed);
            kick[(size_t) i] = tel.scopeKick[(size_t) i].load (std::memory_order_relaxed);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (palette (navy));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (palette (palette.outline));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.5f);
        const auto lane = b.reduced (10.0f, 0.0f).removeFromBottom (40.0f).withTrimmedBottom (8.0f);
        const auto main = b.reduced (10.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (46.0f);
        g.setColour (palette (cream).withAlpha (0.5f));
        g.setFont (gk::GrooveFonts::font (10.5f).withExtraKerningFactor (0.12f));
        g.drawText ("OSCILLOSCOPE", b.reduced (12.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);
        g.drawText (fr ("last kicks \xc2\xb7 100 ms"), b.reduced (12.0f, 4.0f).withHeight (14.0f), juce::Justification::centredRight);

        const int size = (int) sub.size();
        const double sr = 4.0 * size; // the rings hold 2 s at 1 in 8
        const int dec = KickTelemetry::scopeDecimation;
        auto valueAt = [&] (const std::vector<float>& ring, std::int64_t time, bool& ok)
        {
            const auto back = (endTime - time) / dec;
            ok = back >= 0 && back < size - 1;
            return ok ? ring[(size_t) ((((std::int64_t) write - 1 - back) % size + size) % size)] : 0.0f;
        };

        // centre line and the kick (target) line
        g.setColour (palette (juce::Colours::white).withAlpha (0.1f));
        g.drawHorizontalLine (juce::roundToInt (main.getCentreY()), main.getX(), main.getRight());
        const float kickX = main.getX() + (float) (preSeconds / (preSeconds + postSeconds)) * main.getWidth();
        {
            juce::Path line;
            line.startNewSubPath (kickX, main.getY());
            line.lineTo (kickX, main.getBottom());
            juce::Path dashed;
            const float dashes[] { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (palette (juce::Colours::white).withAlpha (0.75f));
            g.fillPath (dashed);
        }

        // the windows that have been heard entirely, oldest first
        const auto pre = (std::int64_t) (preSeconds * sr), post = (std::int64_t) (postSeconds * sr);
        float peak = 0.05f; // a weak sub stays small (not blown up to full height)
        int ready = 0;
        std::array<std::int64_t, shownKicks> wins {};
        for (int i = 0; i < pendingCount; ++i)
            if (pending[(size_t) i] + post <= endTime && endTime - (pending[(size_t) i] - pre) < (std::int64_t) (size - 2) * dec)
                wins[(size_t) ready++] = pending[(size_t) i];
        for (int w = 0; w < ready; ++w)
            for (auto t = wins[(size_t) w] - pre; t <= wins[(size_t) w] + post; t += dec)
            {
                bool ok = false;
                peak = juce::jmax (peak, std::abs (valueAt (sub, t, ok)));
            }
        gain += ((0.85f / peak) - gain) * (gain <= 0.0f ? 1.0f : 0.2f);
        const float half = main.getHeight() * 0.5f;
        for (int w = ready - 1; w >= 0; --w)
        {
            auto& path = subPaths[(size_t) w];
            path.clear();
            bool started = false;
            for (auto t = wins[(size_t) w] - pre; t <= wins[(size_t) w] + post; t += dec)
            {
                bool ok = false;
                const float v = valueAt (sub, t, ok);
                if (! ok)
                    continue;
                const float x = main.getX() + (float) (t - (wins[(size_t) w] - pre)) / (float) (pre + post) * main.getWidth();
                const float y = main.getCentreY() - juce::jlimit (-1.0f, 1.0f, v * gain) * half;
                if (! started)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
                started = true;
            }
            const float alpha = w == 0 ? 1.0f : 0.5f - 0.12f * (float) w;
            g.setColour (palette (lime).withAlpha (alpha));
            g.strokePath (path, juce::PathStrokeType (w == 0 ? 2.2f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (ready > 0)
        {
            // the latest kick's envelope (orange), from the bottom
            kickPath.clear();
            bool started = false;
            float env = 0.0f;
            const float holdDecay = (float) std::exp (-(double) dec / (0.05 * sr));
            for (auto t = wins[0] - pre; t <= wins[0] + post; t += dec)
            {
                bool ok = false;
                const float raw = valueAt (kick, t, ok);
                if (! ok)
                    continue;
                // the detector's 1 ms envelope follows each cycle: held, it reads as the kick's envelope
                env = juce::jmax (raw, env * holdDecay);
                const float v = env;
                const float x = main.getX() + (float) (t - (wins[0] - pre)) / (float) (pre + post) * main.getWidth();
                const float y = main.getBottom() - juce::jlimit (0.0f, 1.0f, v) * main.getHeight() * 0.9f;
                if (! started)
                    kickPath.startNewSubPath (x, y);
                else
                    kickPath.lineTo (x, y);
                started = true;
            }
            g.setColour (palette (orange).withAlpha (0.9f));
            g.strokePath (kickPath, juce::PathStrokeType (1.6f));
        }
        else
        {
            // nothing on the sidechain: how to route it
            g.setColour (palette (cream).withAlpha (0.85f));
            g.setFont (gk::GrooveFonts::font (15.0f));
            g.drawText ("NO KICK ON THE SIDECHAIN", main.withTrimmedBottom (main.getHeight() * 0.45f), juce::Justification::centredBottom);
            g.setColour (palette (cream).withAlpha (0.6f));
            g.setFont (gk::GrooveFonts::font (11.5f));
            g.drawFittedText (fr ("Live: plug-in title bar \xe2\x96\xb8 Sidechain \xe2\x96\xb8 Audio From \xe2\x96\xb8 your kick track"),
                              main.withTrimmedTop (main.getHeight() * 0.58f).withHeight (18.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        g.setColour (palette (cream).withAlpha (0.55f));
        g.setFont (gk::GrooveFonts::font (9.5f));
        g.drawText ("KICK", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ kickX, main.getBottom() + 6.0f }), juce::Justification::centred);

        // the last 2 s: the sub's level (lime) and the kick (orange), per column
        g.setColour (palette (juce::Colours::black).withAlpha (0.25f));
        g.fillRoundedRectangle (lane, 4.0f);
        const int cols = juce::jmax (1, (int) lane.getWidth());
        const int per = juce::jmax (1, size / cols);
        overview.clear();
        float kickHold = 0.0f;
        for (int c = 0; c < cols; ++c)
        {
            float s = 0.0f, k = 0.0f;
            for (int j = 0; j < per; ++j)
            {
                const int idx = ((write + c * per + j) % size + size) % size; // oldest first
                s = juce::jmax (s, std::abs (sub[(size_t) idx]));
                k = juce::jmax (k, kick[(size_t) idx]);
            }
            const float x = lane.getX() + (float) c;
            const float hs = juce::jlimit (0.0f, 1.0f, s * gain) * lane.getHeight() * 0.45f;
            overview.addRectangle (x, lane.getCentreY() - hs, 1.0f, juce::jmax (0.5f, 2.0f * hs));
            kickHold = juce::jmax (k, kickHold * 0.8f);
            if (kickHold > 0.05f)
            {
                const float hk = juce::jlimit (0.0f, 1.0f, kickHold) * lane.getHeight();
                g.setColour (palette (orange).withAlpha (0.75f));
                g.fillRect (x, lane.getBottom() - hk, 1.0f, hk);
            }
        }
        g.setColour (palette (lime).withAlpha (0.8f));
        g.fillPath (overview);
        g.setColour (palette (cream).withAlpha (0.4f));
        g.setFont (gk::GrooveFonts::font (9.0f));
        g.drawText ("2 s", lane.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
    }

private:
    KickLockProcessor& proc;
    const gk::Palette& palette;
    std::vector<float> sub, kick;
    int write = 0;
    std::int64_t endTime = 0, lastKick = -1;
    std::array<std::int64_t, shownKicks> pending {};
    int pendingCount = 0;
    float gain = 0.0f;
    std::array<juce::Path, shownKicks> subPaths;
    juce::Path kickPath, overview;
};

//==============================================================================
/** Phaze's wave: the sub under its feet. Its crest passes the drum (the
    centre) on each kick when the phase is right; it lags by the measured
    error otherwise, wobbles when unstable, splits in two on a mono loss, and
    lies flat asleep (no kick, bypass). */
class KickLockEditor::SubWave final : public juce::Component
{
public:
    SubWave (KickLockProcessor& p, const gk::Palette& pal) : proc (p), palette (pal) { path.preallocateSpace (1024); }

    void update (double dt, int preview, bool snap)
    {
        auto& tel = proc.getDsp().getTelemetry();
        status = preview >= 0 ? preview : tel.status.load (std::memory_order_relaxed);
        const float e = preview >= 0 ? (preview == (int) Status::ok ? 0.0f : 60.0f) : tel.phaseErrorDeg.load (std::memory_order_relaxed);
        error += (e - error) * (snap ? 1.0f : 0.2f);
        const int events = tel.kickEvents.load (std::memory_order_relaxed);
        if (events != kicks)
        {
            kicks = events;
            sinceKick = 0.0;
        }
        else
            sinceKick += dt;
        time += dt;
        const bool asleep = proc.getBypassParameterObject().get() || status == (int) Status::noKick;
        const float target = asleep ? 0.0f : 1.0f;
        awake += (target - awake) * (snap ? 1.0f : 0.08f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        const double bpm = juce::jmax (40.0, (double) proc.getFeatures().bpm.load (std::memory_order_relaxed));
        const double beat = 60.0 / bpm;
        // phase of the wave at the centre: the crest (90°) on the kick, a
        // cycle per beat, minus the error measured at the kick
        const double phase0 = juce::MathConstants<double>::twoPi * (0.25 + sinceKick / beat) - error * juce::MathConstants<double>::pi / 180.0;
        const float amp = b.getHeight() * 0.42f * awake;
        const bool unstable = status == (int) Status::unstable, split = status == (int) Status::monoLoss;
        auto drawWave = [&] (double offset, juce::Colour c, float width)
        {
            path.clear();
            const int n = juce::jmax (2, (int) b.getWidth() / 2);
            for (int i = 0; i <= n; ++i)
            {
                const float x = b.getX() + b.getWidth() * (float) i / (float) n;
                // 1.5 cycles across, the centre being the drum
                double ph = phase0 + offset + juce::MathConstants<double>::twoPi * 1.5 * ((double) i / n - 0.5);
                float a = amp;
                if (unstable)
                {
                    ph += 0.9 * std::sin (time * 7.3 + i * 0.21);
                    a *= 0.75f + 0.25f * (float) std::sin (time * 5.1 + i * 0.13);
                }
                const float y = b.getCentreY() - a * (float) std::sin (ph);
                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            g.setColour (palette (c));
            g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        g.setColour (palette (juce::Colours::black).withAlpha (0.25f));
        g.fillRoundedRectangle (b.expanded (2.0f, 4.0f), 8.0f);
        if (split)
        {
            drawWave (0.0, lime.withAlpha (0.9f), 2.5f);
            drawWave (juce::MathConstants<double>::pi, cream.withAlpha (0.85f), 2.5f);
        }
        else
            drawWave (0.0, lime, 3.0f);
        // the drum's spot: flashes orange on each kick
        const float flash = (float) juce::jmax (0.0, 1.0 - sinceKick / 0.15) * awake;
        g.setColour (palette (orange).withAlpha (0.35f + 0.65f * flash));
        g.fillEllipse (juce::Rectangle<float> (6.0f + 4.0f * flash, 6.0f + 4.0f * flash).withCentre ({ b.getCentreX(), b.getY() + 2.0f }));
        g.setColour (palette (cream).withAlpha (0.4f));
        g.setFont (gk::GrooveFonts::font (9.0f));
        g.drawText ("SUB", b.withTrimmedLeft (2.0f), juce::Justification::topLeft);
    }

private:
    KickLockProcessor& proc;
    const gk::Palette& palette;
    juce::Path path;
    int status = (int) Status::noKick, kicks = -1;
    double sinceKick = 10.0, time = 0.0;
    float error = 0.0f, awake = 0.0f;
};

//==============================================================================
/** The bottom band: the numbers, and the reset button. */
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
        const bool measuring = st != (int) Status::noKick && st != (int) Status::weakSub;
        const double sr = juce::jmax (1.0, 4.0 * tel.scopeSize); // the rings hold 2 s at 1 in 8
        const float f0 = tel.f0Hz.load (std::memory_order_relaxed);
        const auto dot = fr ("  \xc2\xb7  ");
        juce::String t;
        t << "error " << (measuring ? num (tel.phaseErrorDeg.load (std::memory_order_relaxed), 1) + fr ("\xc2\xb0") : juce::String ("--")) << dot
          << "confidence " << (measuring ? num (tel.phaseConfidence.load (std::memory_order_relaxed), 2) : juce::String ("--")) << dot
          << "mono sum " << fr ("\xe2\x88\x92") << num (tel.monoSumLossDb.load (std::memory_order_relaxed), 1) << " dB" << dot
          << "f0 " << (f0 > 1.0f ? num (f0, 1) + " Hz" : juce::String ("--")) << dot << "latency "
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
class KickLockEditor::LockButton final : public juce::Button
{
public:
    explicit LockButton (const gk::Palette& p) : juce::Button ("Lock"), palette (p)
    {
        setClickingTogglesState (true);
        setTitle ("Lock");
        setTooltip ("LOCK: puts the sub in phase with every kick");
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
        // padlock: body + shackle (open when off)
        const float s = b.getHeight() * 0.42f;
        const auto body = juce::Rectangle<float> (s, s * 0.78f).withCentre ({ b.getX() + b.getHeight() * 0.62f, b.getCentreY() + s * 0.22f });
        juce::Path shackle;
        const float lift = on ? 0.0f : s * 0.22f;
        shackle.addCentredArc (body.getCentreX(), body.getY() - lift, s * 0.3f, s * 0.36f, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
        const auto ink = on ? palette (palette.outline) : palette (cream).withAlpha (0.6f);
        g.setColour (ink);
        g.strokePath (shackle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (body, 2.0f);
        g.setColour (on ? palette (palette.outline) : palette (cream).withAlpha (0.75f));
        g.setFont (gk::GrooveFonts::font (16.0f).withExtraKerningFactor (0.08f));
        g.drawText ("LOCK", b.withTrimmedLeft (b.getHeight() * 1.05f), juce::Justification::centredLeft);
    }

private:
    const gk::Palette& palette;
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
    gauge = std::make_unique<PhaseGauge> (p, pal);
    addToCanvas (*gauge, { 10, 70, 182, 104 });
    wave = std::make_unique<SubWave> (p, pal);
    addToCanvas (*wave, { 10, 378, 182, 54 });
    scope = std::make_unique<Scope> (p, pal);
    addToCanvas (*scope, { 204, 66, 374, 202 });
    metrics = std::make_unique<MetricsBar> (p, pal);
    addToCanvas (*metrics, { 0, 440, getBaseWidth(), 40 });

    // next to MIX: the kick threshold (left), LOCK and its MODE (right)
    lockButton = std::make_unique<LockButton> (pal);
    lockAttachment = std::make_unique<juce::ButtonParameterAttachment> (proc.getLockParameter(), *lockButton);
    addToCanvas (*lockButton, { 488, 284, 86, 36 });
    mode = std::make_unique<gk::ChoiceSelector> (p.getLockModeParameter(), "MODE", lime, pal);
    mode->setTooltip ("Hold: the correction holds from one kick to the next. Transition: only around the kick, then back to the original bass.");
    addToCanvas (*mode, { 492, 332, 80, 38 });

    // the right column: ducking shape, release, the lock's settings, depth
    shape = std::make_unique<gk::ChoiceSelector> (p.getShapeParameter(), "SHAPE", orange, pal);
    release = std::make_unique<gk::ChoiceSelector> (p.getReleaseParameter(), "RELEASE", orange, pal);
    shape->setDisplayNames ({ "Expo", "Linear", "S-curve", "Bounce" });
    addToCanvas (*shape, { 590, 274, 78, 38 });
    addToCanvas (*release, { 672, 274, 78, 38 });
    struct K { juce::RangedAudioParameter& param; const char* caption; juce::Colour colour; juce::Rectangle<int> at; };
    const K ks[] {
        { p.getTargetPhaseParameter(), "TARGET", lime, { 596, 76, 64, 68 } },
        { p.getOffsetParameter(), "OFFSET", lime, { 680, 76, 64, 68 } },
        { p.getSubCutoffParameter(), "SUB CUT", limeDeep.brighter (0.3f), { 596, 160, 64, 68 } },
        { p.getWaveStartParameter(), "WAVE START", limeDeep.brighter (0.3f), { 680, 160, 64, 68 } },
        { p.getDepthParameter(), "DEPTH", orange, { 638, 340, 64, 68 } },
        { p.getThresholdParameter(), "THRESHOLD", orange, { 222, 316, 64, 68 } },
    };
    for (const auto& k : ks)
    {
        knobs.push_back (std::make_unique<gk::SmallKnob> (k.param, pal, k.caption, k.colour));
        addToCanvas (*knobs.back(), k.at);
    }
    editorTick (0.0);
}

KickLockEditor::~KickLockEditor() = default;

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
    wave->update (snap ? 0.0 : dt, previewStatus, snap);
    scope->update();
    if (snap || ++frameCount % 4 == 0) // the numbers: 15 a second is plenty to read
        metrics->update();
}
} // namespace kl
