#include "TomiMascot.h"
#include <PewPewArt.h>

namespace pp
{
using gk::dspmath::smoothstep;

TomiMascot::TomiMascot (PewPewProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    showSleepZ = true;
    star.startNewSubPath (0.0f, -1.0f);
    star.quadraticTo (0.15f, -0.15f, 1.0f, 0.0f);
    star.quadraticTo (0.15f, 0.15f, 0.0f, 1.0f);
    star.quadraticTo (-0.15f, 0.15f, -1.0f, 0.0f);
    star.quadraticTo (-0.15f, -0.15f, 0.0f, -1.0f);
    star.closeSubPath();
    wave.preallocateSpace (64);
    lastTrigger = p.getPewDsp().getTelemetry().triggerCounter.load();
}

std::vector<juce::Image> TomiMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "tomi_" + juce::String (i) + "_png";
        const char* data = PPArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

double TomiMascot::echoSeconds() noexcept
{
    const double bpm = juce::jmax (20.0, (double) getFeatures().bpm.load());
    return PewPewDsp::divisionBeats (proc.getDivisionParameter().getIndex()) * 60.0 / bpm;
}

int TomiMascot::chooseFrame (float amount, float bypass, bool musicOn)
{
    juce::ignoreUnused (musicOn);
    const auto& s = getState();
    const double dt = juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    auto& tel = proc.getPewDsp().getTelemetry();

    // A new note: a hit at this frame
    const int trig = tel.triggerCounter.load (std::memory_order_relaxed);
    if (hitOverride >= 0.0f)
    {
        if (hitTime < -5.0)
        {
            hitTime = s.time - hitOverride;
            hitVelocity = 1.0f;
            hitPitch = 1.0f;
            hitLevel = levelFor (amount, 1.0f);
            ++hitIndex;
        }
    }
    else if (trig != lastTrigger && bypass < 0.5f)
    {
        lastTrigger = trig;
        hitTime = s.time;
        hitVelocity = juce::jlimit (0.2f, 1.0f, tel.lastTriggerVelocity.load (std::memory_order_relaxed));
        hitPitch = juce::jmax (tel.pitchPosition.load (std::memory_order_relaxed), amount);
        ++hitIndex;
        recentHits[(size_t) (recentCount % (int) recentHits.size())] = s.time;
        hitLevel = levelFor (amount, hitVelocity);
        recentPoses[(size_t) (recentCount % (int) recentPoses.size())] = frameOf (hitLevel, impact);
        ++recentCount;
        mutableState().squash = -0.05f * hitVelocity; // the force of the hit
        // stars from the skin at high energy, everywhere at the top
        int burst = hitLevel >= 5 ? 3 + 2 * (hitLevel - 4) : 0;
        const auto fig = getFigureArea();
        const auto drum = drumArea (fig);
        for (auto& st : stars)
        {
            if (burst <= 0)
                break;
            if (st.life > 0.0f)
                continue;
            const bool everywhere = hitLevel >= 7 && random.nextFloat() < 0.5f;
            st.pos = everywhere ? juce::Point<float> (getWidth() * random.nextFloat(), getHeight() * random.nextFloat() * 0.8f)
                                : drum.getCentre().translated ((random.nextFloat() - 0.5f) * drum.getWidth(), -drum.getHeight() * 0.3f);
            const float ang = juce::MathConstants<float>::pi * (1.1f + 0.8f * random.nextFloat());
            const float speed = everywhere ? 15.0f : 60.0f + 80.0f * random.nextFloat();
            st.vel = { std::cos (ang) * speed, std::sin (ang) * speed };
            st.life = 1.0f;
            st.size = 2.5f + 3.5f * random.nextFloat();
            --burst;
        }
    }
    else if (bypass >= 0.5f)
        lastTrigger = trig;

    const double since = s.time - hitTime;
    hitEnv = since >= 0.0 ? (float) std::exp (-since / 0.12) * hitVelocity : 0.0f;
    lift = hitEnv * juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * hitPitch);
    for (auto& st : stars)
        if (st.life > 0.0f)
        {
            st.vel.y += 120.0f * (float) dt;
            st.pos += st.vel * (float) dt;
            st.life -= (float) dt / 0.7f;
        }

    if (numFrames() <= sleepFirst + 2)
        return 0;

    // Bypass: falls asleep by the drum (eyes closing, then the stick pillow)
    if (bypass >= 0.5f)
    {
        if (bypassSince < 0.0)
            bypassSince = s.time;
        const double asleep = s.time - bypassSince;
        if (dt <= 0.0 && hitOverride < 0.0f) // snapshot: straight to sleep
            return sleepFirst + 2;
        return asleep < 0.6 ? sleepFirst : asleep < 1.3 ? sleepFirst + 1 : sleepFirst + 2;
    }
    bypassSince = -1.0;

    // The hit: impact at once (the sound starts with the note), then the
    // follow-through, then the stick back up, ready
    if (since >= 0.0 && since < 0.11)
        return frameOf (hitLevel, impact);
    if (since >= 0.0 && since < 0.26)
        return frameOf (hitLevel, follow);

    // Between hits: ready, in the style of the knob's energy (velocity of
    // the last hit kept); tapping the tempo while the host plays without notes
    const int level = since >= 0.0 && since < 2.0 ? hitLevel : levelFor (amount, 0.8f);
    if (s.playing && since > 1.0)
    {
        const int beat = (int) std::floor (s.time * getFeatures().bpm.load() / 60.0);
        return frameOf (level, (beat & 1) ? downswing : raised);
    }
    return frameOf (level, raised);
}

juce::Rectangle<float> TomiMascot::drumArea (juce::Rectangle<float> fig) const noexcept
{
    // A floor tom in front of Tomi, its skin centred where the sticks land on
    // the impact frames (measured on the sprites: 65-82 % of the width,
    // 81-90 % of the height, feet at 97 %)
    const float w = fig.getWidth() * 0.46f, h = fig.getHeight() * 0.2f;
    const float skinY = fig.getY() + fig.getHeight() * 0.87f;
    return { fig.getX() + fig.getWidth() * 0.70f - w * 0.5f, skinY - h * 0.18f, w, h };
}

void TomiMascot::paintBehind (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    const juce::Colour gold (0xffe8b531), cherry (0xffd7336b), indigo (0xff3b2e7e), cream (0xfff3e9d2);
    const auto drum = drumArea (fig);
    const float awake = 1.0f - s.bypass;
    const float room = proc.getRoomParameter().getValue() * awake;

    // ROOM: rings on the floor around the drum, from every recent hit
    if (room > 0.01f)
    {
        const int n = juce::jmin (recentCount, (int) recentHits.size());
        for (int k = 0; k < n; ++k)
        {
            const double age = s.time - recentHits[(size_t) k];
            if (age < 0.0 || age > 1.6)
                continue;
            const float t = (float) (age / 1.6);
            for (int ring = 0; ring < 1 + (int) (2.0f * room); ++ring)
            {
                const float tt = juce::jlimit (0.0f, 1.0f, t - 0.12f * (float) ring);
                const float rw = drum.getWidth() * (0.7f + 1.6f * tt), rh = rw * 0.18f;
                g.setColour (palette (gold).withAlpha (room * 0.55f * (1.0f - tt)));
                g.drawEllipse (juce::Rectangle<float> (rw, rh).withCentre ({ drum.getCentreX(), fig.getBottom() - 4.0f }), 2.0f);
            }
        }
    }

    // ECHO: ghosts of Tomi replaying the hit at the echo times
    const float echo = proc.getEchoParameter().getValue() * awake;
    if (echo > 0.02f && numFrames() > sleepFirst)
    {
        const int ghosts = juce::jlimit (1, 3, 1 + (int) std::floor (echo * 2.99f));
        const double gap = echoSeconds();
        const int n = juce::jmin (recentCount, (int) recentHits.size());
        for (int k = 0; k < n; ++k)
            for (int gIdx = 1; gIdx <= ghosts; ++gIdx)
            {
                const double age = s.time - (recentHits[(size_t) k] + gap * gIdx);
                if (age < 0.0 || age > 0.25)
                    continue;
                const float alpha = (float) (1.0 - age / 0.25) * (0.22f + 0.28f * echo) * std::pow (0.7f, (float) (gIdx - 1));
                const auto ghostFig = fig.translated (-fig.getWidth() * 0.16f * (float) gIdx, 0.0f);
                drawFrame (g, recentPoses[(size_t) k], ghostFig, juce::AffineTransform(), alpha);
            }
    }
}

void TomiMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation)
{
    drawFrame (g, getState().frame, fig, animation, 1.0f);
}

void TomiMascot::paintDrum (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    juce::ignoreUnused (s);
    const juce::Colour gold (0xffe8b531), cherry (0xffd7336b), indigo (0xff3b2e7e), cream (0xfff3e9d2);
    const auto drum = drumArea (fig);
    // The drum: shell (indigo, gold hoops, cherry lugs) and the skin, which
    // swells and lifts with the pitch of the hit
    const float skinH = drum.getHeight() * 0.36f;
    const auto top = juce::Rectangle<float> (drum.getWidth(), skinH).withCentre ({ drum.getCentreX(), drum.getY() + skinH * 0.5f });
    const auto shell = drum.withTrimmedTop (skinH * 0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (juce::Rectangle<float> (drum.getWidth() * 1.05f, 10.0f).withCentre ({ drum.getCentreX(), drum.getBottom() + 2.0f }));
    juce::Path body;
    body.addRectangle (shell.withTrimmedBottom (skinH * 0.5f));
    body.addEllipse (shell.withTop (shell.getBottom() - skinH));
    g.setGradientFill (juce::ColourGradient (palette (indigo).brighter (0.25f), shell.getX(), 0.0f, palette (indigo).darker (0.5f), shell.getRight(), 0.0f, false));
    g.fillPath (body);
    g.setColour (palette (gold));
    g.fillRect (shell.getX(), shell.getBottom() - skinH * 0.5f - 6.0f, shell.getWidth(), 4.0f);
    for (int k = 0; k < 4; ++k) // lugs
    {
        const float x = shell.getX() + shell.getWidth() * (0.14f + 0.24f * (float) k);
        g.setColour (palette (cherry));
        g.fillRoundedRectangle (x - 3.0f, shell.getY() + skinH * 0.55f, 6.0f, shell.getHeight() * 0.42f, 3.0f);
    }
    g.setColour (palette (juce::Colour (0xff141414)));
    g.strokePath (body, juce::PathStrokeType (2.5f));

    // skin: cherry rim, gold hoop, concentric rings; the centre rises
    const float swell = 1.0f + 0.05f * lift;
    g.setColour (palette (cherry));
    g.fillEllipse (top);
    g.setColour (palette (gold));
    g.fillEllipse (top.reduced (3.0f));
    const int rings = 4;
    for (int i = 0; i < rings; ++i)
    {
        const float t = (float) i / (float) rings;
        const auto r = top.reduced (6.0f).withSizeKeepingCentre (top.reduced (6.0f).getWidth() * (1.0f - 0.8f * t) * (1.0f + (swell - 1.0f) * t),
                                                                  top.reduced (6.0f).getHeight() * (1.0f - 0.8f * t) * (1.0f + (swell - 1.0f) * t))
                         .translated (0.0f, -6.0f * lift * t);
        g.setColour (palette ((i & 1) ? gold : indigo).brighter (0.3f * lift * t));
        g.fillEllipse (r);
    }
    g.setColour (palette (cream).withAlpha (0.25f + 0.5f * lift));
    g.fillEllipse (top.withSizeKeepingCentre (top.getWidth() * 0.18f, top.getHeight() * 0.18f).translated (0.0f, -6.0f * lift));
    g.setColour (palette (juce::Colour (0xff141414)));
    g.drawEllipse (top, 2.5f);


    // the impact on the skin (the sprite's own sparks are partly behind it)
    if (hitEnv > 0.05f && s.bypass < 0.5f)
    {
        const auto c = top.getCentre().translated (0.0f, -4.0f * lift);
        g.setColour (palette (cream).withAlpha (0.9f * hitEnv));
        g.fillPath (star, juce::AffineTransform::scale (6.0f + 10.0f * hitEnv, 4.0f + 6.0f * hitEnv).translated (c));
        g.setColour (palette (gold).withAlpha (0.8f * hitEnv));
        for (int k = 0; k < 6; ++k)
        {
            const float ang = juce::MathConstants<float>::pi * (1.05f + 0.18f * (float) k);
            const float r0 = 6.0f, r1 = 10.0f + 14.0f * hitEnv;
            g.drawLine (c.x + std::cos (ang) * r0, c.y + std::sin (ang) * r0 * 0.6f, c.x + std::cos (ang) * r1, c.y + std::sin (ang) * r1 * 0.6f, 2.0f);
        }
    }
}

void TomiMascot::paintOverlay (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    // The drum in front of Tomi, its skin where the sticks land
    paintDrum (g, s, fig);

    const juce::Colour gold (0xffe8b531), cherry (0xffd7336b), cream (0xfff3e9d2);
    // Stars / sparkles from the hits
    for (const auto& st : stars)
        if (st.life > 0.0f)
        {
            g.setColour (palette (st.life > 0.5f ? cream : gold).withAlpha (juce::jmin (1.0f, st.life * 1.4f)));
            g.fillPath (star, juce::AffineTransform::scale (st.size * (0.6f + 0.4f * st.life)).translated (st.pos));
        }
    // Sequins on his edges (cherry, twinkling) from 50 %, glasses glint at 100 %
    if (const float show = smoothstep (0.4f, 0.6f, s.amount) * (1.0f - s.bypass); show > 0.0f)
        for (int i = 0; i < 6; ++i)
        {
            const float tw = 0.5f + 0.5f * (float) std::sin (s.time * 7.0 + i * 2.1);
            const float ang = -2.6f + 0.9f * (float) i;
            const auto p = fig.getCentre().translated (0.0f, -fig.getHeight() * 0.12f)
                           + juce::Point<float> (std::cos (ang), std::sin (ang)) * fig.getHeight() * 0.2f;
            g.setColour (palette (cherry).withAlpha (show * (0.3f + 0.7f * tw)));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (p));
        }
    if (const float glint = smoothstep (0.85f, 1.0f, s.amount) * (1.0f - s.bypass) * (0.4f + 0.6f * hitEnv); glint > 0.01f)
    {
        const auto p = fig.getCentre().translated (fig.getHeight() * 0.07f, -fig.getHeight() * 0.27f);
        g.setColour (palette (cream).withAlpha (glint));
        g.fillPath (star, juce::AffineTransform::scale (5.0f + 3.0f * hitEnv).translated (p));
    }
}
} // namespace pp
