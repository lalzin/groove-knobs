#pragma once

#include "HeadSpinProcessor.h"

namespace hs
{
/** The view from above: the head in the middle (the nose up = the front),
    the path around it and the sound, a glowing dot with a trail that shows
    which way it goes. Drag the path to stretch it (WIDTH / DEPTH: the point
    grabbed follows the mouse), drag outside it or with alt to turn it
    (ROTATION); a double-click puts the three back.

    It is the editor's "mascot" (the frame drives its animation every
    frame); the head is drawn in code until Kiko's sprites (step 5). It
    turns towards the sound, spins on itself above 4 turns / s, and its cap
    flies off above 10. */
class TopView final : public gk::MascotComponent
{
public:
    TopView (HeadSpinProcessor& p, gk::AudioFeatures& features, const gk::Palette& palette);

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
    void paintHead (juce::Graphics& g, juce::Point<float> c, float radius);
    float param (HeadSpinDsp::Knob k);
    void setParam (HeadSpinDsp::Knob k, float plain);

    HeadSpinProcessor& proc;
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

    float headTurn = 0.0f, spinAngle = 0.0f, spinAmount = 0.0f, capLift = 0.0f;
    double time = 0.0;

    enum class Drag { none, stretch, rotate };
    Drag drag = Drag::none;
    juce::Point<double> grabLocal; // the grabbed point, in the shape's own frame (before rotation)
    float startWidth = 1.0f, startDepth = 1.0f, startRotation = 0.0f;
    double startAngle = 0.0;
};
} // namespace hs
