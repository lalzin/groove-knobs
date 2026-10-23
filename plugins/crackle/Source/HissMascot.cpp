#include "HissMascot.h"
#include <CrackleArt.h>

namespace crackle
{
namespace
{
// the two reel holes of the cassette's window, per pose (fractions of the frame;
// measured on the frames; none on the arms-crossed pose: the arms hide them)
struct Reels { float lx, ly, rx, ry; bool shown; };
constexpr Reels reelsOf[] {
    { 0.509f, 0.552f, 0.661f, 0.537f, true }, { 0.512f, 0.487f, 0.658f, 0.476f, true }, { 0.505f, 0.582f, 0.654f, 0.574f, true },
    { 0.508f, 0.596f, 0.655f, 0.585f, true }, { 0.521f, 0.527f, 0.664f, 0.513f, true }, { 0.516f, 0.483f, 0.664f, 0.465f, true },
    { 0.497f, 0.549f, 0.650f, 0.541f, true }, { 0.514f, 0.542f, 0.659f, 0.529f, true }, { 0.515f, 0.536f, 0.669f, 0.519f, true },
    { 0.537f, 0.522f, 0.691f, 0.473f, true }, { 0.314f, 0.475f, 0.475f, 0.531f, true }, { 0.514f, 0.548f, 0.663f, 0.532f, true },
    { 0.519f, 0.464f, 0.653f, 0.444f, true }, { 0.513f, 0.533f, 0.659f, 0.517f, true }, { 0.502f, 0.551f, 0.647f, 0.542f, true },
    { 0.0f, 0.0f, 0.0f, 0.0f, false } };
const juce::Colour reelInk (0xff1f1b24), reelCream (0xfff4ede1), dust (0xffd9cfc0);
} // namespace

HissMascot::HissMascot (CrackleProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal), proc (p)
{
    setFrames (loadFrames());
    motion = 0.6f;
    repaintEvery = 1; // 60 fps: the reels turn
}

std::vector<juce::Image> HissMascot::loadFrames()
{
    std::vector<juce::Image> frames;
    for (int i = 0;; ++i)
    {
        int size = 0;
        const auto name = "hiss_" + juce::String (i) + "_png";
        const char* data = CRArt::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            break;
        if (auto img = juce::ImageCache::getFromMemory (data, size); img.isValid())
            frames.push_back (img);
    }
    return frames;
}

int HissMascot::chooseFrame (float macro, float bypass, bool musicOn)
{
    const auto& s = getState();
    const float dt = lastTime < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    if (numFrames() < numPoses)
        return 0;
    auto& tel = proc.getCrackleDsp().getTelemetry();
    const bool live = bypass < 0.5f;

    // dropouts: the reels stop dead (then pick up again)
    const int dropouts = tel.dropouts.load (std::memory_order_relaxed);
    if (dropouts != lastDropouts)
    {
        if (lastDropouts >= 0 && dropouts > lastDropouts && live)
            droppedAt = s.time;
        lastDropouts = dropouts;
    }
    // crackles: dust every time, a start now and then
    const int crackles = tel.crackles.load (std::memory_order_relaxed);
    if (crackles != lastCrackles)
    {
        if (lastCrackles >= 0 && crackles > lastCrackles && live)
        {
            flash = 1.0f;
            // a few specks of dust off the body
            const auto fig = getFigureArea();
            for (int n = 0, made = 0; n < (int) specks.size() && made < 3; ++n)
                if (specks[(size_t) n].life <= 0.0f)
                {
                    auto& sp = specks[(size_t) n];
                    sp.x = fig.getX() + fig.getWidth() * (0.35f + 0.4f * random());
                    sp.y = fig.getY() + fig.getHeight() * (0.25f + 0.4f * random());
                    sp.vx = (random() - 0.5f) * 60.0f;
                    sp.vy = -30.0f - 50.0f * random();
                    sp.life = 1.0f;
                    ++made;
                }
            if (s.time - startledAt > 1.2)
            {
                startledAt = s.time;
                jumpV = -90.0f; // a little start
            }
        }
        lastCrackles = crackles;
    }

    // the reels: their speed follows WOW and GRAIN, a dropout stops them, they pick up in ~0.3 s
    const bool dropped = s.time - droppedAt < 0.18;
    const float target = live ? 1.2f + 6.0f * tel.wow.load (std::memory_order_relaxed) + 3.0f * macro : 0.0f;
    reelSpeed = dropped ? 0.0f : reelSpeed + (target - reelSpeed) * juce::jmin (1.0f, dt / 0.3f);
    reelAngle += reelSpeed * dt;
    // the start: a damped jump
    jumpV += (-jump * 900.0f - jumpV * 14.0f) * dt;
    jump += jumpV * dt;
    // the specks, the flash
    for (auto& sp : specks)
        if (sp.life > 0.0f)
        {
            sp.x += sp.vx * dt;
            sp.y += sp.vy * dt;
            sp.vy += 60.0f * dt;
            sp.life -= dt / 0.7f;
        }
    flash = juce::jmax (0.0f, flash - dt / 0.15f);
    // CRUSH: short bursts of pixels, more and bigger with more crush
    const float crush = live ? tel.crush.load (std::memory_order_relaxed) : 0.0f;
    if (crush > 0.05f && s.time - crushBurstAt > 1.6 - 1.0 * crush)
        crushBurstAt = s.time;
    const float burst = crush > 0.05f ? juce::jmax (0.0f, 1.0f - (float) (s.time - crushBurstAt) / 0.28f) : 0.0f;
    pixel = burst * (0.3f + 0.7f * crush);

    if (! live)
        return eyesClosed;
    if (dropped || s.time - droppedAt < 0.3)
        return eyesClosed;
    if (s.time - startledAt < 0.2)
        return startled;
    if (crush > 0.3f && burst > 0.0f)
        return ((int) (s.time * 2.0) & 1) ? squint : squintOther;
    if (! musicOn)
        return macro > 0.8f ? armsCrossed : rest;
    // with the music: a foot tap every half second, cheerful when GRAIN is high
    return ((int) (s.time * 2.0) & 1) ? footUp : (macro > 0.7f ? laughing : cheerful);
}

void HissMascot::drawReels (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation) const
{
    const int frame = juce::jlimit (0, numPoses - 1, getState().frame);
    const auto& r = reelsOf[frame];
    if (! r.shown)
        return;
    const float radius = fig.getWidth() * 0.03f;
    for (const auto& [fx, fy] : { std::pair { r.lx, r.ly }, std::pair { r.rx, r.ry } })
    {
        juce::Point<float> c (fig.getX() + fx * fig.getWidth(), fig.getY() + fy * fig.getHeight());
        c = c.transformedBy (animation);
        // the hub: a cream ring, its three teeth turning
        g.setColour (reelInk);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.3f, radius * 2.3f).withCentre (c));
        g.setColour (reelCream);
        g.drawEllipse (juce::Rectangle<float> (radius * 1.6f, radius * 1.6f).withCentre (c), radius * 0.35f);
        for (int t = 0; t < 3; ++t)
        {
            const float a = reelAngle + (float) t * juce::MathConstants<float>::twoPi / 3.0f;
            g.drawLine ({ c, c + juce::Point<float> (std::cos (a), std::sin (a)) * radius * 0.8f }, radius * 0.3f);
        }
    }
}

void HissMascot::paintCharacter (juce::Graphics& g, juce::Rectangle<float> fig, const juce::AffineTransform& animation)
{
    const auto moved = animation.translated (0.0f, jump);
    if (pixel <= 0.01f)
    {
        drawFrame (g, getState().frame, fig, moved, 1.0f);
        drawReels (g, fig, moved);
        return;
    }
    // CRUSH: the cassette drawn small, then blown up block by block
    const float block = 2.0f + 5.0f * pixel;
    const auto area = getLocalBounds();
    const int w = juce::jmax (1, (int) ((float) area.getWidth() / block)), h = juce::jmax (1, (int) ((float) area.getHeight() / block));
    juce::Image small (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics sg (small);
        sg.addTransform (juce::AffineTransform::scale (1.0f / block));
        drawFrame (sg, getState().frame, fig, moved, 1.0f);
        drawReels (sg, fig, moved);
    }
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (small, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void HissMascot::paintOverlay (juce::Graphics& g, const State&, juce::Rectangle<float>)
{
    // the dust off a crackle
    for (const auto& sp : specks)
        if (sp.life > 0.0f)
        {
            g.setColour (dust.withAlpha (0.9f * sp.life));
            const float r = 1.2f + 1.6f * sp.life;
            g.fillEllipse (sp.x - r, sp.y - r, 2.0f * r, 2.0f * r);
        }
}
} // namespace crackle
