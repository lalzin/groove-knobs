#pragma once

#include <groove_core/groove_core.h>

namespace bl
{
/** Twinkles: four-ray star glints that pop up, grow while turning a little,
    and fade (~0.3 to 0.7 s each). Spawned at a rate inside an ellipse (a
    mirror ball's facets); frozen, they hold where they are. Fixed seed, no
    allocation once built (a fixed pool). Message thread (the UI). */
class Sparkles
{
public:
    static constexpr int maxGlints = 24;

    /** Advances by dt seconds; `rate` new glints per second (spread over
        frames), inside `area` (an ellipse); `frozen`: nothing moves. */
    void advance (double dt, float rate, juce::Rectangle<float> area, bool frozen)
    {
        if (frozen || dt <= 0.0)
            return;
        for (auto& gl : glints)
            if (gl.life > 0.0f)
            {
                gl.age += (float) dt;
                if (gl.age >= gl.life)
                    gl.life = 0.0f;
            }
        owed += rate * (float) dt;
        while (owed >= 1.0f)
        {
            owed -= 1.0f;
            spawn (area);
        }
    }

    /** Draws them; `scale`: the glints' size (1 = ~12 px rays at full bloom). */
    void paint (juce::Graphics& g, juce::Colour core, juce::Colour halo, float scale = 1.0f, float opacity = 1.0f) const
    {
        for (const auto& gl : glints)
        {
            if (gl.life <= 0.0f)
                continue;
            const float t = gl.age / gl.life;                       // 0 .. 1
            const float bloom = std::sin (t * juce::MathConstants<float>::pi); // up, then down
            const float ray = scale * gl.size * (0.25f + 0.75f * bloom);
            const float a = opacity * juce::jlimit (0.0f, 1.0f, 1.2f * bloom);
            const float rot = gl.spin * t;
            // the halo
            g.setColour (halo.withAlpha (0.35f * a));
            g.fillEllipse (juce::Rectangle<float> (ray * 1.1f, ray * 1.1f).withCentre (gl.pos));
            // the star: four thin rays (a long cross, a short diagonal one)
            juce::Path star;
            for (int k = 0; k < 4; ++k)
            {
                const float ang = rot + (float) k * juce::MathConstants<float>::halfPi;
                const float len = ray, w = 0.16f * ray;
                juce::Path p;
                p.startNewSubPath (0.0f, -len);
                p.quadraticTo (w, 0.0f, 0.0f, 0.0f);
                p.quadraticTo (-w, 0.0f, 0.0f, -len);
                star.addPath (p, juce::AffineTransform::rotation (ang));
            }
            for (int k = 0; k < 4; ++k)
            {
                const float ang = rot + juce::MathConstants<float>::pi * 0.25f + (float) k * juce::MathConstants<float>::halfPi;
                juce::Path p;
                p.startNewSubPath (0.0f, -0.45f * ray);
                p.quadraticTo (0.1f * ray, 0.0f, 0.0f, 0.0f);
                p.quadraticTo (-0.1f * ray, 0.0f, 0.0f, -0.45f * ray);
                star.addPath (p, juce::AffineTransform::rotation (ang));
            }
            g.setColour (core.withAlpha (a));
            g.fillPath (star, juce::AffineTransform::translation (gl.pos));
            g.fillEllipse (juce::Rectangle<float> (0.22f * ray, 0.22f * ray).withCentre (gl.pos));
        }
    }

private:
    struct Glint
    {
        juce::Point<float> pos;
        float age = 0.0f, life = 0.0f, size = 1.0f, spin = 0.0f;
    };

    float random() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / 16777216.0f;
    }

    void spawn (juce::Rectangle<float> area)
    {
        Glint* slot = nullptr;
        for (auto& gl : glints)
            if (gl.life <= 0.0f)
            {
                slot = &gl;
                break;
            }
        if (slot == nullptr)
            return; // all busy: skip this one
        // a point inside the ellipse (polar, uniform)
        const float a = juce::MathConstants<float>::twoPi * random(), r = std::sqrt (random());
        slot->pos = { area.getCentreX() + 0.5f * area.getWidth() * r * std::cos (a), area.getCentreY() + 0.5f * area.getHeight() * r * std::sin (a) };
        slot->age = 0.0f;
        slot->life = 0.3f + 0.4f * random();
        slot->size = 7.0f + 7.0f * random();
        slot->spin = (random() - 0.5f) * 1.2f;
    }

    std::array<Glint, maxGlints> glints {};
    std::uint32_t seed = 0xd15c0u;
    float owed = 0.0f;
};
} // namespace bl
