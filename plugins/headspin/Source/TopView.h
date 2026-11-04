#pragma once

#include "HeadSpinProcessor.h"

namespace hs
{
/** The view from above: the head in the middle (the nose up = the front),
    the path around it and the sound, a glowing dot with a trail that shows
    which way it goes. Drag the path to stretch it (WIDTH / DEPTH: the point
    grabbed follows the mouse), drag outside it or with alt to turn it
    (ROTATION); a double-click puts the three back.

    Kiko is in the middle, seen from above (Alan's drawing, turned so his
    face points to the front): he turns his head towards the sound (up to
    70°) and goes into a head spin above 4 turns / s (speed lines). The
    small Kiko seen from the front is the editor's mascot (KikoMascot). */
class TopView final : public juce::Component
{
public:
    TopView (HeadSpinProcessor& p, const gk::Palette& palette);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Every frame (from the editor): reads the telemetry, moves the trail and the head. */
    void tick (double dt);
    /** Screenshots: no motion, a full trail. */
    void snapForSnapshot();

    static constexpr float unitsToEdge = 1.6f; // the view shows the head's surroundings up to r = 1.6

private:
    juce::Point<float> toScreen (double x, double y) const noexcept;
    juce::Point<double> toUnits (juce::Point<float> p) const noexcept;
    void rebuildPath();
    void paintKiko (juce::Graphics& g, juce::Point<float> c, float unit);
    float param (HeadSpinDsp::Knob k);
    void setParam (HeadSpinDsp::Knob k, float plain);

    HeadSpinProcessor& proc;
    const gk::Palette& palette;
    juce::Image kiko, kikoScaled; // from above, face down as drawn; and pre-scaled to the screen
    headspin::ShapeBank bank;
    headspin::ShapeTable table;
    float builtShape = -1.0f;
    juce::Path path; // in units, after stretch / rotation
    float pathWidth = -1.0f, pathDepth = -1.0f, pathRotation = 1.0e9f;

    static constexpr int trailLength = 28;
    std::array<juce::Point<float>, trailLength> trail {}; // in units
    int trailHead = 0;
    bool trailFilled = false;
    juce::Point<float> dot { 0.0f, 1.0f };

    float headTurn = 0.0f, spinAngle = 0.0f, spinAmount = 0.0f;
    double time = 0.0;

    enum class Drag { none, stretch, rotate };
    Drag drag = Drag::none;
    juce::Point<double> grabLocal; // the grabbed point, in the shape's own frame (before rotation)
    float startWidth = 1.0f, startDepth = 1.0f, startRotation = 0.0f;
    double startAngle = 0.0;
};
} // namespace hs
