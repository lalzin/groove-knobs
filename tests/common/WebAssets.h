#pragma once

// Visual assets for the website (lalzin.com), rendered from the real plugin
// UIs and mascot frames — shared by ff/bt/bw_screenshots --web <out-dir>:
//   <out>/<slug>/ui-hero.png (2x), ui-hero@3x.png, ui-<state>.png (2x)
//   <out>/<slug>/mascot.png, poses/<n>.png (transparent), poses-sheet.png
//   <out>/<slug>/og.png (1200x630), card-square.png (1080x1080), icon-512.png
//   <out>/groove-knobs-og.png, groove-knobs-banner.png (when all of them exist)

#include <groove_core/groove_core.h>
#include <iostream>

namespace gkweb
{
struct PluginInfo
{
    juce::String slug, name, taglineFr, taglineEn;
    gk::Palette palette;
    juce::File assetsDir;             // plugins/<dir>/Assets
    juce::String framePrefix;         // "mascot_", "velvet_", "swell_"
    int heroFrame = 0;
    std::vector<int> poses;           // exported as poses/<n>.png
};

struct Shot
{
    juce::String name;  // "hero", "lp50", ...
    juce::Image image;  // editor snapshot (any scale)
};

inline bool writePng (const juce::Image& img, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream os (file);
    const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
    return ok;
}

inline juce::Image frame (const PluginInfo& info, int index)
{
    return juce::ImageFileFormat::loadFrom (info.assetsDir.getChildFile (info.framePrefix + juce::String (index) + ".png"));
}

/** Text as a path, left-aligned at x (or centred when centreX), baseline y. */
inline juce::Path textPath (const juce::String& text, float height, float x, float baseline, float kerning, bool centre = false)
{
    auto p = gk::GrooveFonts::textPath (text, height, 0.0f, baseline, kerning);
    const auto b = p.getBounds();
    p.applyTransform (juce::AffineTransform::translation (centre ? x - b.getCentreX() : x - b.getX(), 0.0f));
    return p;
}

/** Cartoon lettering, like the plugin titles: shadow, thick outline, fill. */
inline void cartoonText (juce::Graphics& g, const juce::Path& p, juce::Colour fill, juce::Colour outline, float stroke)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (p, juce::AffineTransform::translation (stroke * 0.35f, stroke * 0.6f));
    g.setColour (outline);
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (fill);
    g.fillPath (p);
}

inline void background (juce::Graphics& g, juce::Rectangle<float> r, const gk::Palette& pal, juce::Point<float> glowAt, float glowRadius)
{
    g.setGradientFill (juce::ColourGradient (pal.background.brighter (0.08f), r.getCentreX(), r.getY(), pal.backgroundEdge.darker (0.4f), r.getCentreX(),
                                             r.getBottom(), false));
    g.fillRect (r);
    // two coloured glows (the arc's ends), soft
    const auto a = pal.arcColour (0.5f), c = pal.beret;
    g.setGradientFill (juce::ColourGradient (a.withAlpha (0.30f), glowAt, a.withAlpha (0.0f), glowAt.translated (glowRadius, 0.0f), true));
    g.fillRect (r);
    const auto second = glowAt.translated (glowRadius * 0.9f, glowRadius * 0.45f);
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.18f), second, c.withAlpha (0.0f), second.translated (glowRadius * 0.8f, 0.0f), true));
    g.fillRect (r);
}

/** The plugin window, rounded, with a soft shadow, fitted in `area`. */
inline juce::Rectangle<float> drawWindow (juce::Graphics& g, const juce::Image& ui, juce::Rectangle<float> area, float rotation = 0.0f)
{
    const float s = juce::jmin (area.getWidth() / (float) ui.getWidth(), area.getHeight() / (float) ui.getHeight());
    const auto r = juce::Rectangle<float> ((float) ui.getWidth() * s, (float) ui.getHeight() * s).withCentre (area.getCentre());
    const float radius = 14.0f * s * (float) ui.getWidth() / 840.0f;
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (rotation, r.getCentreX(), r.getCentreY()));
    for (int i = 6; i >= 1; --i) // soft shadow
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (r.translated (0.0f, 6.0f + 3.0f * (float) i).expanded (3.0f * (float) i), radius + 3.0f * (float) i);
    }
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState inner (g);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f); // drawImage uses the current colour's alpha
        g.drawImage (ui, r);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (clip, juce::PathStrokeType (1.5f));
    return r;
}

inline void drawMascot (juce::Graphics& g, const juce::Image& m, juce::Rectangle<float> area, juce::Justification just = juce::Justification::centred)
{
    if (! m.isValid())
        return;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const float s = juce::jmin (area.getWidth() / (float) m.getWidth(), area.getHeight() / (float) m.getHeight());
    const auto r = just.appliedToRectangle (juce::Rectangle<float> ((float) m.getWidth() * s, (float) m.getHeight() * s), area);
    // contact shadow under the feet
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillEllipse (juce::Rectangle<float> (r.getWidth() * 0.6f, r.getHeight() * 0.06f).withCentre ({ r.getCentreX(), r.getBottom() - r.getHeight() * 0.02f }));
    g.setOpacity (1.0f); // drawImage uses the current colour's alpha
    g.drawImage (m, r);
}

inline void pill (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour fill, juce::Colour ink, float textH)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (2.0f, 3.0f), r.getHeight() * 0.5f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (ink);
    g.setFont (gk::GrooveFonts::font (textH).withExtraKerningFactor (0.06f));
    g.drawText (text, r, juce::Justification::centred);
}

inline float textWidth (const juce::String& t, float h, float kerning = 0.06f)
{
    return juce::GlyphArrangement::getStringWidth (gk::GrooveFonts::font (h).withExtraKerningFactor (kerning), t);
}

/** Pills in a row from x, returns the right edge. */
inline float pills (juce::Graphics& g, const juce::StringArray& items, float x, float y, float h, juce::Colour fill, juce::Colour ink)
{
    for (const auto& t : items)
    {
        const float w = textWidth (t, h * 0.5f) + h;
        pill (g, t, { x, y, w, h }, fill, ink, h * 0.5f);
        x += w + h * 0.35f;
    }
    return x;
}

inline void writePlugin (const PluginInfo& info, const std::vector<Shot>& shots, const juce::File& outRoot)
{
    const auto dir = outRoot.getChildFile (info.slug);
    dir.createDirectory();
    const auto& pal = info.palette;
    const auto cream = pal.text, ink = pal.outline;
    const auto& hero3x = shots.front().image;
    // the 2x size keeps the window's own shape: 840 x 1040 for the one-knob
    // 420 x 520 layout (as before), 1520 x 960 for a 760 x 480 one (Kick Lock)
    const int w2x = hero3x.getWidth() * 2 / 3, h2x = hero3x.getHeight() * 2 / 3;
    const auto hero2x = hero3x.rescaled (w2x, h2x, juce::Graphics::highResamplingQuality);

    // 1. UI screenshots
    writePng (hero3x, dir.getChildFile ("ui-hero@3x.png"));
    writePng (hero2x, dir.getChildFile ("ui-hero.png"));
    for (size_t i = 1; i < shots.size(); ++i)
        writePng (shots[i].image.getWidth() > w2x ? shots[i].image.rescaled (w2x, h2x, juce::Graphics::highResamplingQuality) : shots[i].image,
                  dir.getChildFile ("ui-" + shots[i].name + ".png"));

    // 2. Mascot (transparent) and its poses
    const auto hero = frame (info, info.heroFrame);
    writePng (hero, dir.getChildFile ("mascot.png"));
    std::vector<juce::Image> poseImages;
    for (int p : info.poses)
        if (auto img = frame (info, p); img.isValid())
        {
            writePng (img, dir.getChildFile ("poses/" + juce::String (p) + ".png"));
            poseImages.push_back (img);
        }
    {
        // review sheet of every frame, numbered
        std::vector<juce::Image> all;
        for (int i = 0;; ++i)
        {
            auto img = frame (info, i);
            if (! img.isValid())
                break;
            all.push_back (img);
        }
        const int cell = 260, cols = 6, rows = ((int) all.size() + cols - 1) / cols;
        juce::Image sheet (juce::Image::RGB, cols * cell, juce::jmax (1, rows) * cell, true);
        juce::Graphics g (sheet);
        g.fillAll (pal.background);
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto r = juce::Rectangle<float> ((float) (i % cols) * cell, (float) (i / cols) * cell, (float) cell, (float) cell);
            drawMascot (g, all[i], r.reduced (14.0f).withTrimmedTop (16.0f));
            g.setColour (cream);
            g.setFont (gk::GrooveFonts::font (18.0f));
            g.drawText (juce::String ((int) i), r.reduced (6.0f), juce::Justification::topLeft);
        }
        writePng (sheet, dir.getChildFile ("poses-sheet.png"));
    }

    const juce::StringArray formats { "VST3", "AU", "macOS", "Windows" };

    // 3. Open Graph / social share, 1200 x 630
    {
        juce::Image img (juce::Image::RGB, 1200, 630, true);
        juce::Graphics g (img);
        const auto b = img.getBounds().toFloat();
        background (g, b, pal, { 300.0f, 330.0f }, 520.0f);
        const bool wide = hero3x.getWidth() > hero3x.getHeight();
        drawWindow (g, hero3x, wide ? juce::Rectangle<float> { 24.0f, 60.0f, 520.0f, 510.0f } : juce::Rectangle<float> { 40.0f, 30.0f, 470.0f, 570.0f }, -0.03f);
        g.setColour (cream.withAlpha (0.55f));
        g.fillPath (textPath ("GROOVE KNOBS", 20.0f, 560.0f, 92.0f, 0.3f));
        cartoonText (g, textPath (info.name.toUpperCase(), info.name.length() > 10 ? 74.0f : 88.0f, 556.0f, 182.0f, 0.02f), cream, ink, 9.0f);
        g.setColour (cream.withAlpha (0.9f));
        g.setFont (gk::GrooveFonts::font (30.0f));
        g.drawFittedText (info.taglineEn, { 560, 205, 380, 90 }, juce::Justification::topLeft, 2);
        g.fillPath (textPath ("made by", 18.0f, 562.0f, 352.0f, 0.08f));
        cartoonText (g, textPath ("LALZIN", 34.0f, 560.0f, 392.0f, 0.12f), pal.beret, ink, 4.0f);
        pills (g, formats, 560.0f, 520.0f, 40.0f, ink.withAlpha (0.85f), cream);
        drawMascot (g, hero, { 880.0f, 270.0f, 300.0f, 260.0f }, juce::Justification::centredBottom);
        writePng (img, dir.getChildFile ("og.png"));
    }

    // 4. Square card (Instagram, product grid), 1080 x 1080
    {
        juce::Image img (juce::Image::RGB, 1080, 1080, true);
        juce::Graphics g (img);
        const auto b = img.getBounds().toFloat();
        background (g, b, pal, { 400.0f, 620.0f }, 620.0f);
        g.setColour (cream.withAlpha (0.55f));
        g.fillPath (textPath ("GROOVE KNOBS", 24.0f, 540.0f, 92.0f, 0.3f, true));
        cartoonText (g, textPath (info.name.toUpperCase(), 104.0f, 540.0f, 200.0f, 0.02f, true), cream, ink, 11.0f);
        const bool wide = hero3x.getWidth() > hero3x.getHeight();
        drawWindow (g, hero3x, wide ? juce::Rectangle<float> { 50.0f, 300.0f, 640.0f, 560.0f } : juce::Rectangle<float> { 90.0f, 250.0f, 560.0f, 700.0f }, -0.035f);
        drawMascot (g, hero, { 600.0f, 520.0f, 440.0f, 400.0f }, juce::Justification::centredBottom);
        g.setColour (cream.withAlpha (0.6f));
        g.fillPath (textPath ("made by", 20.0f, 830.0f, 1000.0f, 0.08f, true));
        cartoonText (g, textPath ("LALZIN", 40.0f, 830.0f, 1046.0f, 0.12f, true), pal.beret, ink, 4.5f);
        writePng (img, dir.getChildFile ("card-square.png"));
    }

    // 5. Icon, 512 x 512 (rounded, transparent corners)
    {
        juce::Image img (juce::Image::ARGB, 512, 512, true);
        juce::Graphics g (img);
        juce::Path shape;
        shape.addRoundedRectangle (8.0f, 8.0f, 496.0f, 496.0f, 110.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (shape);
            background (g, img.getBounds().toFloat(), pal, { 256.0f, 300.0f }, 330.0f);
            drawMascot (g, hero, { 40.0f, 40.0f, 432.0f, 440.0f }, juce::Justification::centredBottom);
        }
        g.setColour (pal.arcColour (0.5f).withAlpha (0.8f));
        g.strokePath (shape, juce::PathStrokeType (6.0f));
        writePng (img, dir.getChildFile ("icon-512.png"));
    }
}

/** The plug-ins in the suite visuals; for the banner, Kick Lock shows its
    Send (a one-knob window: Kick Lock's own, wide, did not fit among them). */
inline const std::vector<std::pair<juce::String, juce::String>>& suiteVisuals()
{
    static const std::vector<std::pair<juce::String, juce::String>> v {
        { "french-filter", "" }, { "boogie-trio", "" }, { "backwash", "" }, { "pew-pew", "" }, { "sing-it-back", "" }, { "kick-lock", "ui-send@3x.png" }, { "slapper", "" }, { "crackle", "" }
    };
    return v;
}

/** The suite visuals, from the plugins' outputs already on disk (the second
    of each pair: another window for the banner, empty = the same). */
inline void writeSuite (const juce::File& outRoot, const std::vector<std::pair<juce::String, juce::String>>& slugsAndBanner, const gk::Palette& pal)
{
    std::vector<juce::Image> uis, bannerUis;
    for (const auto& [slug, banner] : slugsAndBanner)
    {
        uis.push_back (juce::ImageFileFormat::loadFrom (outRoot.getChildFile (slug + "/ui-hero@3x.png")));
        bannerUis.push_back (banner.isEmpty() ? uis.back() : juce::ImageFileFormat::loadFrom (outRoot.getChildFile (slug + "/" + banner)));
    }
    for (size_t k = 0; k < uis.size(); ++k)
        if (! uis[k].isValid() || ! bannerUis[k].isValid())
        {
            std::cout << "suite visuals skipped: render every plugin first\n";
            return;
        }
    const auto cream = pal.text, ink = pal.outline;

    auto compose = [&] (int w, int h, float titleH, juce::Rectangle<float> windows, const std::vector<juce::Image>& shown, const juce::String& file)
    {
        juce::Image img (juce::Image::RGB, w, h, true);
        juce::Graphics g (img);
        const auto b = img.getBounds().toFloat();
        background (g, b, pal, { b.getCentreX(), b.getHeight() * 0.6f }, b.getWidth() * 0.45f);
        g.setColour (cream.withAlpha (0.55f));
        g.fillPath (textPath ("by LALZIN  -  with groove and love", titleH * 0.26f, b.getCentreX(), titleH * 1.62f, 0.2f, true));
        cartoonText (g, textPath ("GROOVE KNOBS", titleH, b.getCentreX(), titleH * 1.18f, 0.03f, true), cream, ink, titleH * 0.1f);
        // the windows side by side, fanned out, the middle one on top
        const int n = (int) shown.size();
        const float ww = windows.getWidth() / (float) n;
        std::vector<int> order;
        for (int k = 0; k < n; ++k)
            order.push_back (k);
        const float mid = 0.5f * (float) (n - 1);
        std::stable_sort (order.begin(), order.end(), [&] (int x, int y) { return std::abs ((float) x - mid) > std::abs ((float) y - mid); });
        for (int k : order)
        {
            const float side = mid > 0.0f ? ((float) k - mid) / mid : 0.0f; // -1 .. 1
            const auto area = juce::Rectangle<float> (windows.getX() + ww * (float) k, windows.getY() + windows.getHeight() * 0.05f * std::abs (side), ww,
                                                      windows.getHeight())
                                  .expanded (ww * (n > 3 ? 0.22f : 0.08f), 0.0f);
            // a wide window (Kick Lock) gets a wider slot, or it is a stamp
            const bool wideUi = shown[(size_t) k].getWidth() > shown[(size_t) k].getHeight();
            drawWindow (g, shown[(size_t) k], wideUi ? area.withSizeKeepingCentre (area.getWidth() * 1.4f, area.getHeight() * 0.8f) : area, 0.06f * side);
        }
        writePng (img, outRoot.getChildFile (file));
    };
    compose (1200, 630, 64.0f, { 150.0f, 130.0f, 900.0f, 470.0f }, uis, "groove-knobs-og.png");
    compose (2400, 1000, 120.0f, { 360.0f, 230.0f, 1680.0f, 740.0f }, bannerUis, "groove-knobs-banner.png");
}
} // namespace gkweb
