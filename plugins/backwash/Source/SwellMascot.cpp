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
        mutableState().squash = -0.12f; // bounce
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

    const double beats = s.playing ? s.beatPhase + std::floor (s.time * 2.0) : s.time * 2.0; // ~120 BPM when stopped
    const int beat = (int) std::floor (s.playing ? s.time * (getFeatures().bpm.load() / 60.0) : beats);

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
    if (amount < 0.38f)
        return paddleFirst + (beat & 1); // paddling, one stroke per beat
    if (amount < 0.6f)
        return (beat / 2) & 1 ? kneel : stand;
    if (amount < 0.9f)
        return crouchFirst + beat % 3;
    return exhale > 0.3f || (beat & 1) ? euphoric : crouchFirst + 2;
}

void SwellMascot::paintBehind (juce::Graphics& g, const State& s, juce::Rectangle<float> fig)
{
    const auto b = getLocalBounds().toFloat();
    const float sea = fig.getBottom() - 6.0f;
    const float awake = 1.0f - s.bypass;
    const float amount = seaHeight;

    // Wave height: the knob, the rhythm, the breath (digs in), the hit (falls back)
    const float beatSwing = s.playing ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * s.beatPhase) : 0.5f + 0.5f * (float) std::sin (s.time * 2.2);
    const float height = awake * (6.0f + 112.0f * amount) * (1.0f + 0.12f * beatSwing + 0.35f * inhale - 0.25f * waveDrop) + 2.0f;
    const float crestX = b.getX() + b.getWidth() * (0.72f - 0.1f * inhale + 0.03f * beatSwing);
    const float t = (float) s.time;

    wavePath.clear();
    foamPath.clear();
    wavePath.startNewSubPath (b.getX(), b.getBottom());
    bool first = true;
    for (float x = b.getX(); x <= b.getRight() + 0.5f; x += 4.0f)
    {
        // A swell rising towards the crest on the right, ripples riding on it
        const float u = (x - b.getX()) / b.getWidth();
        const float d = (x - crestX) / (b.getWidth() * 0.32f);
        const float bump = std::exp (-d * d) * (x < crestX ? 1.0f : 0.75f);
        const float ripple = 3.0f * std::sin (u * 18.0f + t * 2.2f) + 2.0f * std::sin (u * 31.0f - t * 1.7f);
        const float y = sea - height * (0.18f + 0.82f * bump) - ripple * (0.4f + 0.6f * amount) * awake;
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

    // From 75 %: the lip curls over Swell (a tube at 100 %)
    if (const float curl = smoothstep (0.7f, 1.0f, amount) * awake; curl > 0.01f)
    {
        const float lipX = crestX - b.getWidth() * (0.08f + 0.18f * curl + 0.06f * inhale);
        const float top = sea - height;
        curlPath.clear();
        curlPath.startNewSubPath (crestX + 12.0f, top + 4.0f);
        curlPath.quadraticTo (crestX - 10.0f, top - 18.0f * curl, lipX, top + 6.0f + 22.0f * curl);
        curlPath.quadraticTo (lipX + 18.0f, top + 10.0f, crestX - 4.0f, top + 14.0f);
        curlPath.closeSubPath();
        g.setColour (palette (aqua).withAlpha (0.9f));
        g.fillPath (curlPath);
        g.setColour (palette (cream).withAlpha (0.9f));
        g.strokePath (curlPath, juce::PathStrokeType (2.5f));
    }

    // SHIMMER: sparkles twinkling on the crest
    if (proc.getShimmerParameter().get() && awake > 0.5f && amount > 0.05f)
        for (int i = 0; i < 6; ++i)
        {
            const float tw = 0.5f + 0.5f * (float) std::sin (s.time * 6.0 + i * 1.9);
            const float x = crestX + (float) (i - 3) * 22.0f;
            const float d = (x - crestX) / (b.getWidth() * 0.32f);
            const float y = sea - height * (0.18f + 0.82f * std::exp (-d * d)) - 8.0f;
            g.setColour (palette (cream).withAlpha (0.35f + 0.65f * tw));
            g.fillPath (sparkle, juce::AffineTransform::scale (3.0f + 3.0f * tw).translated (x, y));
        }
}

void SwellMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation)
{
    const auto& s = getState();
    const bool longSwell = proc.getLongParameter().get();
    // Inhale: the body grows and lifts (more with LONG), exhale: back at once
    const float k = inhale * (longSwell ? 1.25f : 1.0f);
    const float grow = 0.09f * k;
    const juce::Point<float> feet (fig.getCentreX(), fig.getBottom());
    const float bob = s.bypass > 0.5f ? 2.5f * (float) std::sin (s.time * 1.4) : 0.0f; // floating asleep
    const auto breath = juce::AffineTransform::scale (1.0f + grow * 0.7f, 1.0f + grow, feet.x, feet.y).translated (0.0f, -10.0f * k + bob);
    drawFrame (g, s.frame, fig, animation.followedBy (breath), 1.0f);
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
