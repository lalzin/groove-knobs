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

HissMascot::HissMascot (CrackleProcessor& p, const gk::Palette& pal) : MascotComponent (p.getFeatures(), pal)
{
    setFrames (loadFrames());
    motion = 0.0f;      // no bounce, no nod: only the knob moves him
    repaintEvery = 2;   // 30 fps (the reels)
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

int HissMascot::chooseFrame (float macro, float bypass, bool)
{
    const auto& s = getState();
    const float dt = lastTime < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 0.1, s.time - lastTime);
    lastTime = s.time;
    if (numFrames() < numPoses)
        return 0;
    const bool live = bypass < 0.5f;
    const float g = live ? juce::jlimit (0.0f, 1.0f, macro) : 0.0f;

    // the reels: their speed from GRAIN alone, eased
    const float target = live ? 0.8f + 5.0f * g : 0.0f;
    reelSpeed += (target - reelSpeed) * juce::jmin (1.0f, dt / 0.4f);
    reelAngle += reelSpeed * dt;
    // the pixelated double: there from where the bitcrush comes in, still
    pixel = live ? grain::crush (g) : 0.0f;

    if (! live)
        return eyesClosed;
    // the ladder: calm -> smiling -> cheerful -> laughing -> squinting (the top), a
    // little hysteresis so he never flickers between two steps
    static constexpr float steps[] { 0.12f, 0.35f, 0.6f, 0.85f };
    constexpr int numSteps = 4;
    while (rung < numSteps && g > steps[rung] + 0.02f)
        ++rung;
    while (rung > 0 && g < steps[rung - 1] - 0.02f)
        --rung;
    static constexpr int poses[] { rest, smiling, cheerful, laughing, squint };
    return poses[rung];
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
    if (pixel > 0.01f)
    {
        // the top of GRAIN (the bitcrush): a pixelated double stands behind him,
        // a little to the left — drawn small, then blown up block by block
        const float block = 3.0f + 4.0f * pixel;
        const auto area = getLocalBounds();
        const int w = juce::jmax (1, (int) ((float) area.getWidth() / block)), h = juce::jmax (1, (int) ((float) area.getHeight() / block));
        juce::Image small (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics sg (small);
            sg.addTransform (juce::AffineTransform::scale (1.0f / block));
            drawFrame (sg, getState().frame, fig, animation.translated (-0.08f * fig.getWidth(), -0.03f * fig.getHeight()), 1.0f);
        }
        juce::Graphics::ScopedSaveState keep (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.setOpacity (0.5f * pixel);
        g.drawImage (small, area.toFloat(), juce::RectanglePlacement::stretchToFit);
    }
    // Hiss himself stays sharp
    drawFrame (g, getState().frame, fig, animation, 1.0f);
    drawReels (g, fig, animation);
}
} // namespace crackle
