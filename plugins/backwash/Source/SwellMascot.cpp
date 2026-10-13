#include "SwellMascot.h"
#include <BackwashArt.h>

namespace bw
{
using gk::dspmath::smoothstep;
using gk::dspmath::lerp;

SwellMascot::SwellMascot (BackwashProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    sparkle.startNewSubPath (0.0f, -1.0f);
    sparkle.quadraticTo (0.12f, -0.12f, 1.0f, 0.0f);
    sparkle.quadraticTo (0.12f, 0.12f, 0.0f, 1.0f);
    sparkle.quadraticTo (-0.12f, 0.12f, -1.0f, 0.0f);
    sparkle.quadraticTo (-0.12f, -0.12f, 0.0f, -1.0f);
    sparkle.closeSubPath();
    wavePath.preallocateSpace (600);
    foamPath.preallocateSpace (400);
    curlPath.preallocateSpace (100);
}

std::vector<juce::Image> SwellMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "swell_" + juce::String (i) + "_png";
        const char* data = BWArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

void SwellMascot::spawnDrops (juce::Point<float> origin, int count)
{
    for (auto& d : drops)
    {
        if (count <= 0)
            break;
        if (d.life > 0.0f)
            continue;
        const float angle = juce::MathConstants<float>::pi * (1.15f + 0.7f * random.nextFloat());
        const float speed = 70.0f + 90.0f * random.nextFloat();
        d.pos = origin.translated ((random.nextFloat() - 0.5f) * 30.0f, 0.0f);
        d.vel = { std::cos (angle) * speed, std::sin (angle) * speed };
        d.life = 1.0f;
        --count;
    }
}

void SwellMascot::updateBreath (double dt, float amount, float bypass)
{
    const auto& tel = proc.getBackwashDsp().getTelemetry();
    float target = 0.0f;
    if (inhaleOverride >= 0.0f)
        target = inhaleOverride;
    else if (amount > 0.0f && bypass < 0.5f)
    {
        const int toNext = tel.samplesToNextHit.load (std::memory_order_relaxed);
        const int swell = juce::jmax (1, tel.swellSamples.load (std::memory_order_relaxed));
        if (toNext >= 0 && toNext < swell)
            target = 1.0f - (float) toNext / (float) swell;
    }

    // The hit: the breath was high and is gone → exhale at once
    if (inhaleOverride < 0.0f && inhale > 0.55f && target < 0.2f)
    {
        exhale = 1.0f;
        waveDrop = 1.0f;
        mutableState().squash = -0.05f; // small bounce
        const auto fig = getFigureArea();
        spawnDrops ({ fig.getCentreX(), fig.getY() + fig.getHeight() * 0.35f }, 8 + (int) (10.0f * amount));
    }
    // Rise follows the real swell; the release is instantaneous (the cut)
    inhale = target < inhale || dt <= 0.0 ? target : inhale + (target - inhale) * (1.0f - std::exp (-(float) dt / 0.03f));
    exhale *= std::exp (-(float) dt / 0.18f);
    waveDrop *= std::exp (-(float) dt / 0.25f);
    const float seaTarget = bypass >= 0.5f ? 0.0f : amount;
    seaHeight = dt <= 0.0 ? seaTarget : seaHeight + (seaTarget - seaHeight) * (1.0f - std::exp (-(float) dt / 0.25f)); // snap on snapshots

    for (auto& d : drops)
        if (d.life > 0.0f)
        {
            d.vel.y += 380.0f * (float) dt;
            d.pos += d.vel * (float) dt;
            d.life -= (float) dt / 0.8f;
        }
}

int SwellMascot::chooseFrame (float amount, float bypass, bool musicOn)
{
    juce::ignoreUnused (musicOn);
    const auto& s = getState();
    const double dt = juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    updateBreath (dt, amount, bypass);

    if (numFrames() <= euphoric)
        return 0;
    if (bypass >= 0.5f)
        return sleepPose;

    const int beat = (int) std::floor (s.time * (s.playing ? getFeatures().bpm.load() / 60.0 : 2.0)); // ~120 BPM when stopped

    if (amount < 0.12f)
    {
        // On the beach, waiting for a wave; a little shuffle now and then
        if (s.time > idleFlip)
        {
            idleFrame = beachFirst + random.nextInt (3);
            idleFlip = s.time + 1.5 + 2.5 * random.nextDouble();
        }
        return idleFrame;
    }
    // One stable pose per zone (Alan: the rhythm must not keep changing his
    // look); only the paddling keeps a slow stroke, one per bar.
    if (amount < 0.38f)
        return paddleFirst + ((beat / 4) & 1);
    if (amount < 0.6f)
        return stand;
    if (amount < 0.9f)
        return crouchFirst;
    return euphoric; // 100 %: all smiles, all the time
}

void SwellMascot::paintBehind (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    const auto b = getLocalBounds().toFloat();
    const float sea = fig.getBottom() - 6.0f;
    const float awake = 1.0f - s.bypass;
    const float amount = seaHeight;

    // Wave height: the knob, the rhythm, the breath (digs in), the hit (falls back)
    const float beatSwing = s.playing ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * s.beatPhase) : 0.5f + 0.5f * (float) std::sin (s.time * 2.2);
    const float rawHeight = awake * (6.0f + 112.0f * amount) * (1.0f + 0.06f * beatSwing + 0.25f * inhale - 0.2f * waveDrop) + 2.0f;
    // Never above the band: the crest and the curl stay inside (soft knee,
    // no flat cut at the top)
    const float maxHeight = sea - b.getY() - 30.0f, knee = 0.75f * maxHeight;
    const float height = rawHeight <= knee ? rawHeight : knee + (maxHeight - knee) * std::tanh ((rawHeight - knee) / (maxHeight - knee));
    const float crestX = b.getX() + b.getWidth() * (0.72f - 0.06f * inhale + 0.015f * beatSwing);
    const float t = (float) s.time;

    // A swell rising towards the crest on the right, ripples riding on it
    auto surfaceY = [&] (float x)
    {
        const float u = (x - b.getX()) / b.getWidth();
        const float d = (x - crestX) / (b.getWidth() * 0.32f);
        const float bump = std::exp (-d * d) * (1.0f - 0.25f * smoothstep (0.0f, 0.8f, d)); // back slope lower, no step at the crest
        const float ripple = 3.0f * std::sin (u * 18.0f + t * 2.2f) + 2.0f * std::sin (u * 31.0f - t * 1.7f);
        return juce::jmax (b.getY() + 4.0f, sea - height * (0.18f + 0.82f * bump) - ripple * (0.4f + 0.6f * amount) * awake);
    };

    wavePath.clear();
    foamPath.clear();
    wavePath.startNewSubPath (b.getX(), b.getBottom());
    bool first = true;
    for (float x = b.getX(); x <= b.getRight() + 0.5f; x += 4.0f)
    {
        const float y = surfaceY (x);
        wavePath.lineTo (x, y);
        if (first)
            foamPath.startNewSubPath (x, y);
        else
            foamPath.lineTo (x, y);
        first = false;
    }
    wavePath.lineTo (b.getRight(), b.getBottom());
    wavePath.closeSubPath();

    const juce::Colour teal (0xff0f7c8c), aqua (0xff4fd1c5), cream (0xfff3e9d2);
    // Fades out at the bottom of the band (no hard edge above the knob)
    juce::ColourGradient fill (palette (aqua).withAlpha (0.95f), crestX, sea - height, palette (teal).darker (0.35f).withAlpha (0.0f), crestX, b.getBottom(), false);
    fill.addColour (juce::jlimit (0.0, 1.0, (double) (height / (b.getBottom() - (sea - height)))), palette (teal).withAlpha (0.9f));
    g.setGradientFill (fill);
    g.fillPath (wavePath);
    g.setColour (palette (cream).withAlpha (0.85f));
    g.strokePath (foamPath, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // From 75 %: the lip curls over Swell (a tube at 100 %). One roll that
    // leaves the wave surface right of the crest, arches over it and hooks
    // down to the left; the hollow underneath goes back into the wave body,
    // so there is no seam. Only the outer edge gets the foam line.
    if (const float curl = smoothstep (0.7f, 1.0f, amount) * awake; curl > 0.01f)
    {
        const float top = surfaceY (crestX);
        const float startX = crestX + 56.0f * curl;
        const juce::Point<float> start (startX, surfaceY (startX));
        // Everything grows with `curl`: at its start the roll is just a
        // swelling of the crest, no step
        const float lipX = crestX - b.getWidth() * (0.13f + 0.03f * inhale) * curl;
        const juce::Point<float> lip (lipX, top + 36.0f * curl);
        const juce::Point<float> c1 (crestX + 18.0f, top - 26.0f * curl), c2 (lipX - 14.0f, top - 8.0f * curl);
        const juce::Point<float> hollow (crestX - 4.0f * curl, top + 28.0f * curl);

        curlPath.clear();
        curlPath.startNewSubPath (start);
        curlPath.cubicTo (c1, c2, lip);
        curlPath.quadraticTo ({ lipX + 10.0f * curl, top + 12.0f * curl }, hollow);
        curlPath.lineTo (start.translated (0.0f, 14.0f * curl));
        curlPath.closeSubPath();
        g.setGradientFill (fill); // the wave's own gradient: no seam
        g.fillPath (curlPath);

        juce::Path edge;
        edge.preallocateSpace (40);
        edge.startNewSubPath (start);
        edge.cubicTo (c1, c2, lip);
        g.setColour (palette (cream).withAlpha (0.9f * juce::jmin (1.0f, curl * 3.0f)));
        g.strokePath (edge, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // The inside of the tube, a thin darker line under the lip
        juce::Path inner;
        inner.startNewSubPath (lip);
        inner.quadraticTo ({ lipX + 10.0f * curl, top + 12.0f * curl }, hollow);
        g.setColour (palette (teal).withAlpha (0.55f));
        g.strokePath (inner, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // SHIMMER: sparkles twinkling on the crest
    if (proc.getShimmerParameter().get() && awake > 0.5f && amount > 0.05f)
        for (int i = 0; i < 6; ++i)
        {
            const float tw = 0.5f + 0.5f * (float) std::sin (s.time * 6.0 + i * 1.9);
            const float x = crestX + (float) (i - 3) * 22.0f;
            const float d = (x - crestX) / (b.getWidth() * 0.32f);
            const float y = sea - height * (0.18f + 0.82f * std::exp (-d * d) * (1.0f - 0.25f * smoothstep (0.0f, 0.8f, d))) - 8.0f;
            g.setColour (palette (cream).withAlpha (0.35f + 0.65f * tw));
            g.fillPath (sparkle, juce::AffineTransform::scale (3.0f + 3.0f * tw).translated (x, y));
        }
}

void SwellMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform&)
{
    const auto& s = getState();
    const bool longSwell = proc.getLongParameter().get();
    // A calmer version of the suite's animation: the rhythm (nod, transient
    // squash, pose pop) only nudges him, the design stays readable
    const juce::Point<float> feet (fig.getCentreX(), fig.getBottom());
    const float sy = 1.0f - 0.35f * s.squash + 0.01f * s.breath + 0.02f * s.pop;
    const float sx = 1.0f + 0.15f * s.squash + 0.005f * s.breath + 0.01f * s.pop;
    const auto calm = juce::AffineTransform::translation (-feet.x, -feet.y)
                          .scaled (sx, sy)
                          .rotated (0.03f * s.sway - 0.08f * s.bypass)
                          .translated (feet.x, feet.y + 1.0f * s.nod);
    // Inhale: he grows a little and lifts (more with LONG), exhale: back at once
    const float k = inhale * (longSwell ? 1.25f : 1.0f);
    const float grow = 0.04f * k;
    const float bob = s.bypass > 0.5f ? 2.5f * (float) std::sin (s.time * 1.4) : 0.0f; // floating asleep
    const auto breath = juce::AffineTransform::scale (1.0f + grow * 0.7f, 1.0f + grow, feet.x, feet.y).translated (0.0f, -5.0f * k + bob);
    drawFrame (g, s.frame, fig, calm.followedBy (breath), 1.0f);
}

void SwellMascot::paintOverlay (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    const juce::Colour aqua (0xff4fd1c5), cream (0xfff3e9d2);
    // Water drops thrown on the hit
    for (const auto& d : drops)
        if (d.life > 0.0f)
        {
            g.setColour (palette (aqua).withAlpha (juce::jmin (1.0f, d.life * 1.5f)));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 7.0f).withCentre (d.pos));
            g.setColour (palette (cream).withAlpha (0.8f * d.life));
            g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (d.pos.translated (-1.0f, -1.5f)));
        }

    // 100 %: euphoric — sparkles around him
    if (const float show = smoothstep (0.9f, 1.0f, s.amount) * (1.0f - s.bypass); show > 0.0f)
        for (int i = 0; i < 5; ++i)
        {
            const float tw = 0.5f + 0.5f * (float) std::sin (s.time * 5.0 + i * 1.3);
            const float ang = -2.4f + 0.55f * (float) i;
            const auto p = fig.getCentre().translated (0.0f, -fig.getHeight() * 0.1f) + juce::Point<float> (std::cos (ang), std::sin (ang)) * fig.getHeight() * 0.62f;
            g.setColour (palette (cream).withAlpha (show * (0.3f + 0.7f * tw)));
            g.fillPath (sparkle, juce::AffineTransform::scale (3.0f + 3.0f * tw).translated (p));
        }
}
} // namespace bw
